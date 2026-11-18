// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e5, the board face: textually included by pcbnew_embind.cpp after cc_rules_pcb.inc,
// outside any KICAD_MERGED_EMBIND guard (include subdir pcbnew; the merged image compiles
// that TU with -DKICAD_MERGED_EMBIND). It sees the TU's includes and helpers (pcbFrame(),
// the cc_output namespace, cc_fp_items.h's declarations in the merged image only) and calls them without editing them.
//
// Every definition of the lane lives in namespace cc_e5 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

#include <pcb_io/pcb_io.h>
#include <pcb_io/pcb_io_mgr.h>
#include <tools/pcb_control.h>
#include <wx/dialog.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/toplevel.h>

#include <memory>
#include <set>

#include <sys/stat.h>

#include <cstdio>
#include <optional>
#include <set>
#include <vector>

#include <embedded_files.h>
#include <font/font.h>

namespace cc_e5
{
namespace pcb
{
using json = nlohmann::json;

constexpr size_t FOOTPRINT_TEXT_MAX = 8u * 1024u * 1024u;   // one (footprint ...) (FP spec 5.1)
constexpr size_t CONVERT_FOOTPRINTS_MAX = 2000;             // kicadFootprintConvert's bounds
constexpr size_t CONVERT_TEXT_MAX = 8u * 1024u * 1024u;
constexpr size_t PATH_MAX_CHARS = 1024;

/** Where a file to convert is staged by the island: /tmp/cc-import/<n>/<file>. */
const std::string& importRoot()
{
    static const std::string root = "/tmp/cc-import/";
    return root;
}

/** A shown dialog of any kind (KiCad's or wx's): the frame is not to be driven meanwhile. */
bool dialogUp()
{
    for( wxWindowList::const_iterator it = wxTopLevelWindows.begin();
         it != wxTopLevelWindows.end(); ++it )
    {
        wxWindow* w = *it;

        if( w && w->IsShown() && wxDynamicCast( w, wxDialog ) )
            return true;
    }

    return false;
}

/** The board editor when it may be asked anything, else nullptr. */
PCB_EDIT_FRAME* readyBoardFrame()
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() || dialogUp() )
        return nullptr;

    return fr;
}

FOOTPRINT* footprintByUuid( BOARD* aBoard, const std::string& aUuid )
{
    if( !aBoard )
        return nullptr;

    for( FOOTPRINT* fp : aBoard->Footprints() )
    {
        if( toUtf8( fp->m_Uuid.AsString() ) == aUuid )
            return fp;
    }

    return nullptr;
}

/** A page token: 1 to 64 printable ASCII characters. */
bool isToken( const json& aValue )
{
    if( !aValue.is_string() )
        return false;

    const std::string& t = aValue.get_ref<const std::string&>();

    if( t.empty() || t.size() > 64 )
        return false;

    for( char c : t )
    {
        if( c < 0x20 || c > 0x7e )
            return false;
    }

    return true;
}

/**
 * A footprint formatted as a footprint library writes it (PCB_IO_KICAD_SEXPR with
 * CTL_FOR_LIBRARY, prettified as FP_CACHE::Save's PRETTIFIED_FILE_OUTPUTFORMATTER does): the
 * footprint's own uuid, position and library nickname left out, every child keeping its uuid,
 * no pad nets, the embedded files included. aFootprint is detached and its nets cleared first.
 */
std::string libraryText( FOOTPRINT& aFootprint )
{
    aFootprint.SetParent( nullptr );
    aFootprint.SetParentGroup( nullptr );
    aFootprint.ClearAllNets();

    PCB_IO_KICAD_SEXPR io( CTL_FOR_LIBRARY );
    STRING_FORMATTER   fmt;
    io.SetOutputFormatter( &fmt );
    io.Format( &aFootprint );

    std::string out = fmt.GetString();
    KICAD_FORMAT::Prettify( out );
    return out;
}

/**
 * FP_CACHE's own reset of a footprint before a library save (pcb_io_kicad_sexpr.cpp,
 * PCB_IO_KICAD_SEXPR::FootprintSave): orientation 0, on the front (flipped with the editor's
 * flip direction, KiCad's top to bottom when there is no setting).
 */
void resetAsLibrary( FOOTPRINT& aFootprint, FLIP_DIRECTION aFlip )
{
    aFootprint.SetOrientation( ANGLE_0 );

    if( aFootprint.GetLayer() != F_Cu )
        aFootprint.Flip( aFootprint.GetPosition(), aFlip );
}

/** The file to convert: a file (or a .pretty folder) inside a run folder under importRoot(). */
bool isImportPath( const std::string& aPath )
{
    if( aPath.empty() || aPath.size() > PATH_MAX_CHARS )
        return false;

    const size_t slash = aPath.rfind( '/' );

    if( slash == std::string::npos || slash + 1 >= aPath.size() )
        return false;

    const std::string dir = aPath.substr( 0, slash );

    if( !cc_output::IsRunDir( dir, importRoot() ) )
        return false;

    return aPath.find( ".." ) == std::string::npos && aPath.find( '\0' ) == std::string::npos;
}

json convertError( const char* aCode, const wxString& aMessage = wxString() )
{
    json out = json::object();
    out["error"] = aCode;

    if( !aMessage.IsEmpty() )
        out["message"] = toUtf8( aMessage ).substr( 0, 1024 );

    return out;
}

} // namespace pcb
} // namespace cc_e5

// ── m13-e: the document's embedded files (properties addendum 10.1), board half ──────────
//
// The same rules in the board face and the schematic face (one translation unit each, so each
// face holds its own copy in its nested namespace): PANEL_EMBEDDED_FILES' own steps on the
// document's EMBEDDED_FILES (the BOARD or the SCHEMATIC), with no window.

namespace cc_e5::pcb
{
namespace embfiles
{
using json = nlohmann::json;
using EF = EMBEDDED_FILES::EMBEDDED_FILE;

constexpr size_t    FILES_MAX = 1024;                     // files in a read
constexpr size_t    EMBED_NAME_CHARS = 255;
constexpr size_t    PATH_MAX_CHARS = 1024;
constexpr long long BYTES_MAX = 25ll * 1024ll * 1024ll;   // an added file

inline std::string u8( const wxString& aText )
{
    return std::string( aText.utf8_str() );
}

inline const char* typeName( EF::FILE_TYPE aType )
{
    switch( aType )
    {
    case EF::FILE_TYPE::FONT:      return "font";
    case EF::FILE_TYPE::MODEL:     return "model";
    case EF::FILE_TYPE::WORKSHEET: return "worksheet";
    case EF::FILE_TYPE::DATASHEET: return "datasheet";
    default:                       return "other";
    }
}

inline std::optional<EF::FILE_TYPE> typeOf( const json& aValue )
{
    if( !aValue.is_string() )
        return std::nullopt;

    const std::string& t = aValue.get_ref<const std::string&>();

    if( t == "font" )      return EF::FILE_TYPE::FONT;
    if( t == "model" )     return EF::FILE_TYPE::MODEL;
    if( t == "worksheet" ) return EF::FILE_TYPE::WORKSHEET;
    if( t == "datasheet" ) return EF::FILE_TYPE::DATASHEET;
    if( t == "other" )     return EF::FILE_TYPE::OTHER;

    return std::nullopt;
}

/** A file's name as a file name: 1 to 255 characters, no separator, no control, not . or .. */
inline bool isFileName( const wxString& aName )
{
    if( aName.IsEmpty() || aName.length() > EMBED_NAME_CHARS || aName == wxT( "." ) || aName == wxT( ".." )
            || aName.Contains( wxT( "/" ) ) || aName.Contains( wxT( "\\" ) ) )
    {
        return false;
    }

    for( wxUniChar c : aName )
    {
        if( c.GetValue() < 0x20 || c.GetValue() == 0x7f )
            return false;
    }

    return true;
}

inline std::optional<wxString> nameOf( const json& aValue )
{
    if( !aValue.is_string() )
        return std::nullopt;

    const std::string& raw = aValue.get_ref<const std::string&>();

    if( raw.empty() || raw.size() > 4 * EMBED_NAME_CHARS || raw.find( '\0' ) != std::string::npos )
        return std::nullopt;

    wxString name = wxString::FromUTF8( raw.c_str() );

    if( !isFileName( name ) )
        return std::nullopt;

    return name;
}

/** A run folder under aRoot (the plot's rule). */
inline std::optional<std::string> dirOf( const json& aValue, const std::string& aRoot )
{
    if( !aValue.is_string() )
        return std::nullopt;

    const std::string& d = aValue.get_ref<const std::string&>();

    if( d.size() > PATH_MAX_CHARS || !cc_output::IsRunDir( d, aRoot ) )
        return std::nullopt;

    return d;
}

/** A file's decompressed size, read from a copy (the document's file is left as it is). */
inline long long bytesOf( const EF& aFile )
{
    if( !aFile.decompressedData.empty() )
        return static_cast<long long>( aFile.decompressedData.size() );

    EF copy = aFile;

    if( EMBEDDED_FILES::DecompressAndDecode( copy ) != EMBEDDED_FILES::RETURN_CODE::OK )
        return -1;

    return static_cast<long long>( copy.decompressedData.size() );
}

/** The files of aFiles, each with the nested sets (footprints, symbols) that also carry it. */
inline json filesJson( EMBEDDED_FILES* aFiles )
{
    json out = json::array();

    for( const auto& [name, file] : aFiles->EmbeddedFileMap() )
    {
        if( out.size() >= FILES_MAX )
            break;

        if( !file )
            continue;

        const wxString key = name;
        int            held = 0;

        aFiles->RunOnNestedEmbeddedFiles(
                [&]( EMBEDDED_FILES* aNested )
                {
                    if( aNested && aNested->HasFile( key ) )
                        ++held;
                } );

        json f = json::object();
        f["name"] = u8( name );
        f["type"] = typeName( file->type );
        f["bytes"] = bytesOf( *file );
        f["heldBy"] = held;
        out.push_back( std::move( f ) );
    }

    return out;
}

/**
 * A font's file name: FONT::m_fontFileName is protected and its public reader is OUTLINE_FONT's
 * (font/outline_font.h needs FreeType's include folder, which the embind compile does not
 * have); a pointer to the member formed through a derived class's name reads KiCad's own field
 * (the class is never made).
 */
struct FONT_FILE_NAME : public KIFONT::FONT
{
    static const wxString& of( const KIFONT::FONT* aFont )
    {
        return aFont->*( &FONT_FILE_NAME::m_fontFileName );
    }
};

/**
 * The whole read: the owner, the fonts switch, the files, and the outline fonts the document
 * uses. aCandidates are the fonts of the document's texts, walked as the document's own
 * GetFonts() walks them; each is kept only when GetFonts() (KiCad's embedding permission
 * filter) holds it. OUTLINE_FONT's one public base is FONT, so the two pointers share an
 * address; they are compared as addresses only.
 */
inline json documentJson( EMBEDDED_FILES* aFiles, const char* aOwner,
                          const std::vector<KIFONT::FONT*>& aCandidates )
{
    std::set<const void*> usable;

    for( KIFONT::OUTLINE_FONT* font : aFiles->GetFonts() )
        usable.insert( static_cast<const void*>( font ) );

    json                  fonts = json::array();
    std::set<const void*> seen;

    for( KIFONT::FONT* font : aCandidates )
    {
        const void* key = static_cast<const void*>( font );

        if( !font || !usable.count( key ) || !seen.insert( key ).second )
            continue;

        json f = json::object();
        f["name"] = u8( font->GetName() );
        f["embedded"] = aFiles->HasFile( wxFileName( FONT_FILE_NAME::of( font ) ).GetFullName() );
        fonts.push_back( std::move( f ) );
    }

    json out = json::object();
    out["owner"] = aOwner;
    out["fontsEmbedded"] = aFiles->GetAreFontsEmbedded();
    out["files"] = filesJson( aFiles );
    out["fonts"] = std::move( fonts );
    return out;
}

struct SET_ARGS
{
    std::string   op;          // add, remove, fonts
    std::string   dir;         // the record's run folder ("" for none)
    std::string   path;        // add: the staged file
    wxString      name;        // add, remove
    EF::FILE_TYPE type = EF::FILE_TYPE::OTHER;
    bool          alsoHeld = false;
    bool          embed = false;
};

/**
 * The closed shapes of kicadEmbeddedFilesSet, read whole: {"run", "dir", "op": "add", "path",
 * "name", "type"} (the staged file inside dir, a regular file of at most 25 MB), {"op":
 * "remove", "name", "alsoHeld"} or {"op": "fonts", "embed"}; the last two may also carry "run"
 * and "dir" (both or neither) for the record. dir is an embed run folder (/tmp/cc-embed/<n>).
 */
inline bool readSet( const json& aArgs, SET_ARGS& aOut )
{
    if( !aArgs.is_object() || !aArgs.contains( "op" ) || !aArgs["op"].is_string() )
        return false;

    aOut.op = aArgs["op"].get<std::string>();

    const bool hasRun = aArgs.contains( "run" );
    const bool hasDir = aArgs.contains( "dir" );

    if( hasRun != hasDir )
        return false;

    size_t keys = 1 + ( hasRun ? 2 : 0 );

    if( hasRun )
    {
        const json& run = aArgs["run"];

        if( !run.is_number_integer() || run.get<long long>() < 0 )
            return false;

        std::optional<std::string> dir = dirOf( aArgs["dir"], cc_output::EmbedRoot() );

        if( !dir )
            return false;

        aOut.dir = *dir;
    }

    if( aOut.op == "add" )
    {
        if( !hasRun || !aArgs.contains( "path" ) || !aArgs.contains( "name" )
                || !aArgs.contains( "type" ) || aArgs.size() != keys + 3
                || !aArgs["path"].is_string() )
        {
            return false;
        }

        std::optional<wxString>      name = nameOf( aArgs["name"] );
        std::optional<EF::FILE_TYPE> type = typeOf( aArgs["type"] );
        const std::string            path = aArgs["path"].get<std::string>();
        const size_t                 slash = path.rfind( '/' );

        if( !name || !type || path.size() > PATH_MAX_CHARS || slash == std::string::npos
                || path.substr( 0, slash ) != aOut.dir || slash + 1 >= path.size()
                || path.find( '\0' ) != std::string::npos
                || !isFileName( wxString::FromUTF8( path.substr( slash + 1 ).c_str() ) ) )
        {
            return false;
        }

        struct stat st;

        if( ::stat( path.c_str(), &st ) != 0 || !S_ISREG( st.st_mode )
                || static_cast<long long>( st.st_size ) > BYTES_MAX )
        {
            return false;
        }

        aOut.path = path;
        aOut.name = *name;
        aOut.type = *type;
        return true;
    }

    if( aOut.op == "remove" )
    {
        if( !aArgs.contains( "name" ) || !aArgs.contains( "alsoHeld" ) || aArgs.size() != keys + 2
                || !aArgs["alsoHeld"].is_boolean() )
        {
            return false;
        }

        std::optional<wxString> name = nameOf( aArgs["name"] );

        if( !name )
            return false;

        aOut.name = *name;
        aOut.alsoHeld = aArgs["alsoHeld"].get<bool>();
        return true;
    }

    if( aOut.op == "fonts" )
    {
        if( !aArgs.contains( "embed" ) || aArgs.size() != keys + 1 || !aArgs["embed"].is_boolean() )
            return false;

        aOut.embed = aArgs["embed"].get<bool>();
        return true;
    }

    return false;
}

/** A set's precondition against the document (a removal names a file it holds). */
inline bool fits( EMBEDDED_FILES* aFiles, const SET_ARGS& aArgs )
{
    return aArgs.op != "remove" || aFiles->HasFile( aArgs.name );
}

/**
 * The set on the document, PANEL_EMBEDDED_FILES' steps: an add reads the file by KiCad's own
 * AddFile (its read, compression and hash) into a staging set and moves it in under its name
 * and type (a file of that name replaced, as the panel's Overwrite); a removal removes the
 * file and, with alsoHeld, the copies the footprints or symbols carry (TransferDataFromWindow's
 * "Delete from footprints (symbols) as well?"); fonts is onFontEmbedClick then
 * SetAreFontsEmbedded (the font files dropped, then with embed the document's own EmbedFonts(),
 * which adds the outline fonts GetFonts() lists). False when KiCad could not read the file or the name is gone.
 */
inline bool applySet( EMBEDDED_FILES* aFiles, const SET_ARGS& aArgs )
{
    if( aArgs.op == "add" )
    {
        EMBEDDED_FILES staging;
        wxFileName     source( wxString::FromUTF8( aArgs.path.c_str() ) );
        EF*            file = staging.AddFile( source, true );

        if( !file )
            return false;

        staging.RemoveFile( source.GetFullName(), false );   // ownership leaves the staging set
        file->name = aArgs.name;
        file->type = aArgs.type;

        if( aFiles->HasFile( aArgs.name ) )
            aFiles->RemoveFile( aArgs.name );

        aFiles->AddFile( file );
        return true;
    }

    if( aArgs.op == "remove" )
    {
        if( !aFiles->HasFile( aArgs.name ) )
            return false;

        if( aArgs.alsoHeld )
        {
            aFiles->RunOnNestedEmbeddedFiles(
                    [&]( EMBEDDED_FILES* aNested )
                    {
                        if( aNested && aNested->HasFile( aArgs.name ) )
                            aNested->RemoveFile( aArgs.name, true );
                    } );
        }

        aFiles->RemoveFile( aArgs.name );
        return true;
    }

    // fonts
    std::vector<wxString> fontFiles;

    for( const auto& [name, file] : aFiles->EmbeddedFileMap() )
    {
        if( file && file->type == EF::FILE_TYPE::FONT )
            fontFiles.push_back( name );
    }

    for( const wxString& name : fontFiles )
        aFiles->RemoveFile( name );

    if( aArgs.embed )
        aFiles->EmbedFonts();   // the document's own: AddFile of each GetFonts() file, as FONT

    aFiles->SetAreFontsEmbedded( aArgs.embed );
    return true;
}

/** The record of a set: {"files": [...], "error": "" or "not_applied"}, written last. */
inline void writeSetRecord( const std::string& aDir, EMBEDDED_FILES* aFiles, bool aApplied )
{
    if( aDir.empty() )
        return;

    json done = json::object();
    done["files"] = aFiles ? filesJson( aFiles ) : json::array();
    done["error"] = aApplied ? "" : "not_applied";
    cc_output::WriteRecord( aDir, "embedded.json", done );
}

/**
 * kicadEmbeddedFileExtract's body: {"dir": "/tmp/cc-embed-out/<n>", "name"} writes the file's
 * decompressed bytes to <dir>/<name> (decompressed from a copy), then .cc-embed-out.json
 * {"name", "bytes", "error"}. False for any other shape or a name the document does not hold.
 */
inline bool extract( EMBEDDED_FILES* aFiles, const std::string& aArgsJson )
{
    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 2 || !args.contains( "dir" )
            || !args.contains( "name" ) )
    {
        return false;
    }

    std::optional<std::string> dir = dirOf( args["dir"], "/tmp/cc-embed-out/" );
    std::optional<wxString>    name = nameOf( args["name"] );

    if( !dir || !name || !aFiles->HasFile( *name ) )
        return false;

    const EF* held = aFiles->GetEmbeddedFile( *name );

    if( !held )
        return false;

    EF   copy = *held;
    bool ok = !copy.decompressedData.empty()
              || EMBEDDED_FILES::DecompressAndDecode( copy ) == EMBEDDED_FILES::RETURN_CODE::OK;

    cc_output::EnsureDir( *dir );

    long long written = 0;

    if( ok )
    {
        const std::string out = *dir + "/" + u8( *name );

        if( FILE* f = std::fopen( out.c_str(), "wb" ) )
        {
            const size_t n = copy.decompressedData.size();
            ok = std::fwrite( copy.decompressedData.data(), 1, n, f ) == n;
            ok = ( std::fclose( f ) == 0 ) && ok;
            written = ok ? static_cast<long long>( n ) : 0;
        }
        else
        {
            ok = false;
        }
    }

    json done = json::object();
    done["name"] = u8( *name );
    done["bytes"] = written;
    done["error"] = ok ? "" : "extract_failed";
    cc_output::WriteRecord( *dir, ".cc-embed-out.json", done );
    return true;
}

} // namespace embfiles
} // namespace cc_e5::pcb

namespace cc_e5::pcb
{
namespace placefile
{
using json = nlohmann::json;

constexpr size_t    PATH_MAX_CHARS = 1024;
constexpr long long BYTES_MAX = 25ll * 1024ll * 1024ll;   // place.file's bound

/**
 * kicadPlaceFromFile's closed shape, exactly {"kind": aKind, "path"}: path a regular file of at
 * most 25 MB directly inside a stage run folder (/tmp/cc-stage/<n>, the island's, never the
 * project folder), its extension aExt. The path, or nothing.
 */
inline std::optional<std::string> pathOf( const std::string& aArgsJson, const char* aKind,
                                          const char* aExt )
{
    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 2 || !args.contains( "kind" )
            || !args.contains( "path" ) || args["kind"] != json( aKind ) || !args["path"].is_string() )
    {
        return std::nullopt;
    }

    const std::string path = args["path"].get<std::string>();
    const size_t      slash = path.rfind( '/' );
    const std::string ext = aExt;

    if( path.size() > PATH_MAX_CHARS || path.find( '\0' ) != std::string::npos
            || slash == std::string::npos
            || !cc_output::IsRunDir( path.substr( 0, slash ), "/tmp/cc-stage/" ) )
    {
        return std::nullopt;
    }

    // A file name of at least one character before the extension.
    const std::string name = path.substr( slash + 1 );

    if( name.size() <= ext.size() || name.compare( name.size() - ext.size(), ext.size(), ext ) != 0 )
        return std::nullopt;

    struct stat st;

    if( ::stat( path.c_str(), &st ) != 0 || !S_ISREG( st.st_mode )
            || static_cast<long long>( st.st_size ) > BYTES_MAX )
    {
        return std::nullopt;
    }

    return path;
}

} // namespace placefile
} // namespace cc_e5::pcb

namespace cc_e5
{

// ── fp-e7: the board round trip (FP spec 5.1, kicadFootprintForEditor) ──────────────────────

/**
 * footprint.forEditor (pcb frame, cap footprintEdit): aArgsJson exactly {"uuid": a placed
 * footprint's lowercase uuid}. FOOTPRINT_EDIT_FRAME::LoadFootprintFromBoard's copy
 * (load_select_footprint.cpp) with the BOARD's uuids kept (no map: the footprint editor is
 * another engine): cloned, flags cleared, unlocked (its pads too), nets cleared, at the origin,
 * flipped to the front with the board editor's flip direction, angle 0; formatted as a library
 * writes it. Answers {"name", "libId", "ref", "text"}; "" for any other shape, no board, an open
 * in flight, a dialog up, a uuid the board does not place, or a text over 8 MB. A read.
 */
std::string footprintForEditor( std::string aArgsJson )
{
    PCB_EDIT_FRAME* fr = pcb::readyBoardFrame();

    if( !fr )
        return "";

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 1 )
        return "";

    auto uuid = args.find( "uuid" );

    if( uuid == args.end() || !uuid->is_string() || !ccIsLowerUuid( uuid->get<std::string>() ) )
        return "";

    FOOTPRINT* source = pcb::footprintByUuid( fr->GetBoard(), uuid->get<std::string>() );

    if( !source )
        return "";

    std::string text;

    try
    {
        std::unique_ptr<FOOTPRINT> copy( static_cast<FOOTPRINT*>( source->Clone() ) );   // uuids kept
        copy->SetParent( fr->GetBoard() );
        copy->SetParentGroup( nullptr );
        copy->ClearFlags();
        copy->SetLocked( false );

        copy->RunOnChildren(
                []( BOARD_ITEM* aItem )
                {
                    if( aItem->Type() == PCB_PAD_T )
                        aItem->SetLocked( false );

                    aItem->ClearFlags();
                },
                RECURSE_MODE::RECURSE );

        copy->ClearAllNets();
        copy->SetPosition( VECTOR2I( 0, 0 ) );

        if( copy->GetLayer() != F_Cu )
            copy->Flip( copy->GetPosition(), fr->GetPcbNewSettings()->m_FlipDirection );

        copy->SetOrientation( ANGLE_0 );
        text = pcb::libraryText( *copy );
    }
    catch( ... )
    {
        return "";
    }

    if( text.empty() || text.size() > pcb::FOOTPRINT_TEXT_MAX )
        return "";

    json out = json::object();
    out["name"] = toUtf8( source->GetFPID().GetLibItemName().wx_str() );
    out["libId"] = toUtf8( source->GetFPID().Format().wx_str() );
    out["ref"] = toUtf8( source->GetReference() );
    out["text"] = std::move( text );
    return out.dump( -1, ' ', false, json::error_handler_t::replace );
}

/**
 * footprint.push (pcb frame, cap footprintEdit): aArgsJson exactly {"uuid", "text", "libId",
 * "token"}: uuid a placed footprint's lowercase uuid, text ONE (footprint ...) of at most 8 MB
 * (parsed here, before anything is queued: an unreadable text answers false), libId "" (the
 * placed part keeps its FPID) or "lib:name" (set in the same commit, so the part names the
 * design's copy), token the page's 1 to 64 printable ASCII characters. On the apply coroutine,
 * FOOTPRINT_EDIT_FRAME::SaveFootprintToBoard( false )'s update body (footprint_libraries_utils.cpp):
 * a child whose uuid the placed footprint holds keeps it, any other takes a fresh KIID; pads
 * re-bound to the board's nets by name; ApplyDefaultSettings with the flags a fresh footprint
 * editor carries (BOARD_DESIGN_SETTINGS' m_StyleFPFields, m_StyleFPText, m_StyleFPShapes,
 * m_StyleFPDimensions and m_StyleFPBarcodes, all false by default, so nothing is restyled);
 * PCB_EDIT_FRAME::ExchangeFootprint with KiCad's defaults; ONE commit "Update footprint".
 */
bool footprintPushToBoard( std::string aArgsJson )
{
    PCB_EDIT_FRAME* fr = pcb::readyBoardFrame();

    if( !fr )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 4 )
        return false;

    auto uuid = args.find( "uuid" );
    auto text = args.find( "text" );
    auto libId = args.find( "libId" );
    auto token = args.find( "token" );

    if( uuid == args.end() || text == args.end() || libId == args.end() || token == args.end() )
        return false;

    if( !uuid->is_string() || !ccIsLowerUuid( uuid->get<std::string>() ) || !text->is_string()
            || !libId->is_string() || !pcb::isToken( *token ) )
        return false;

    const std::string& body = text->get_ref<const std::string&>();

    if( body.empty() || body.size() > pcb::FOOTPRINT_TEXT_MAX
            || body.find( '\0' ) != std::string::npos )
        return false;

    const size_t first = body.find_first_not_of( " \t\r\n" );

    if( first == std::string::npos || body.compare( first, 10, "(footprint" ) != 0 )
        return false;

    const std::string& lib = libId->get_ref<const std::string&>();
    std::optional<LIB_ID> relink;

    if( !lib.empty() )
    {
        if( lib.size() > 1024 )
            return false;

        LIB_ID id;

        if( id.Parse( wxString::FromUTF8( lib.c_str() ) ) >= 0 || !id.IsValid()
                || id.GetLibNickname().empty() || id.GetLibItemName().empty() )
            return false;

        relink = id;
    }

    const std::string id = uuid->get<std::string>();

    if( !pcb::footprintByUuid( fr->GetBoard(), id ) )
        return false;

    // Parsed with no board, as a footprint library reads it: the parser's version rules come
    // from the text's own (version ...) line.
    std::shared_ptr<FOOTPRINT> parsed;

    try
    {
        PCB_IO_KICAD_SEXPR io;
        BOARD_ITEM*        item = io.Parse( wxString::FromUTF8( body.c_str(), body.size() ) );

        if( !item )
            return false;

        if( item->Type() != PCB_FOOTPRINT_T )
        {
            delete item;
            return false;
        }

        parsed.reset( static_cast<FOOTPRINT*>( item ) );
    }
    catch( ... )
    {
        return false;
    }

    pcbjam_collab::runOnCoroutine( fr, [id, parsed, relink]() {
        PCB_EDIT_FRAME* frame = pcbFrame();   // re-resolved on the coroutine (S4)

        if( !frame || !frame->GetBoard() )
            return;

        BOARD*     board = frame->GetBoard();
        FOOTPRINT* source = pcb::footprintByUuid( board, id );

        if( !source )
            return;

        frame->GetToolManager()->RunAction( ACTIONS::selectionClear );

        // A fresh footprint for the board, cloned from the parsed one (Clone keeps the uuids).
        FOOTPRINT* fresh = static_cast<FOOTPRINT*>( parsed->Clone() );
        fresh->SetParent( board );
        fresh->SetParentGroup( nullptr );
        fresh->SetLink( niluuid );

        std::set<KIID> held;
        held.insert( source->m_Uuid );
        source->RunOnChildren( [&]( BOARD_ITEM* aChild ) { held.insert( aChild->m_Uuid ); },
                               RECURSE_MODE::RECURSE );

        auto fixUuid =
                [&]( KIID& aUuid )
                {
                    if( !held.count( aUuid ) )
                        aUuid = KIID();
                };

        const_cast<KIID&>( fresh->m_Uuid ) = source->m_Uuid;
        fresh->RunOnChildren( [&]( BOARD_ITEM* aChild )
                              { fixUuid( const_cast<KIID&>( aChild->m_Uuid ) ); },
                              RECURSE_MODE::RECURSE );

        // The nets the footprint carries, re-bound to the board's by name (KiCad's own loop).
        fresh->RunOnChildren(
                [&]( BOARD_ITEM* aChild )
                {
                    if( BOARD_CONNECTED_ITEM* conn = dynamic_cast<BOARD_CONNECTED_ITEM*>( aChild ) )
                    {
                        NETINFO_ITEM* net = conn->GetNet();
                        auto&         netmap = board->GetNetInfo().NetsByName();

                        if( net )
                        {
                            auto it = netmap.find( net->GetNetname() );

                            if( it != netmap.end() )
                                conn->SetNet( it->second );
                        }
                    }
                },
                RECURSE_MODE::RECURSE );

        fresh->SetFPID( relink ? *relink : source->GetFPID() );

        // The flags a fresh footprint editor's BOARD_DESIGN_SETTINGS carries: all false.
        fresh->ApplyDefaultSettings( *board, false, false, false, false, false );

        BOARD_COMMIT commit( frame );
        frame->ExchangeFootprint( source, fresh, commit );
        commit.Push( wxT( "Update footprint" ) );
        fresh->ClearFlags();
        frame->GetCanvas()->Refresh();
    } );

    return true;
}

// ── fp-e7: footprint files to KiCad's text (kicadFootprintConvert, the board half) ───────────

/**
 * footprint.convert (cap footprintConvert): aArgsJson exactly {"path": "/tmp/cc-import/<n>/<file>"}.
 * A file one plugin reads as a library (PCB_IO_MGR::GuessPluginTypeFromLibPath, then
 * FootprintEnumerate and each FootprintLoad: PCB_IO_MGR::ConvertLibrary's road), else as one
 * footprint (the plugin whose CanReadFootprint takes it, then ImportFootprint:
 * FOOTPRINT_EDIT_FRAME::ImportFootprint's road, its file box and message boxes left out). Each
 * footprint reset as a library save resets it and formatted as one: {"format": KiCad's name for
 * the plugin, "footprints": [{"name", "text"}]}; {"error": "too_large"} past 2,000 footprints or
 * 8 MB of text; {"error": "unreadable", "message"} when no plugin reads it or KiCad's reader
 * throws. "" for args of any other shape, no frame, an open in flight or a dialog up. A read.
 * Defined here once (external linkage), called by both halves (board and footprint editor).
 */
std::string footprintConvertBody( const std::string& aArgsJson, FLIP_DIRECTION aFlip )
{
    if( pcbjam_open::busy() || pcb::dialogUp() )
        return "";

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 1 )
        return "";

    auto pathIt = args.find( "path" );

    if( pathIt == args.end() || !pathIt->is_string() )
        return "";

    const std::string path = pathIt->get<std::string>();

    if( !pcb::isImportPath( path ) )
        return "";

    const wxString fullPath = wxString::FromUTF8( path.c_str() );
    wxLogNull      noLog;   // wx's log window stays shut on every road

    if( !wxFileName::FileExists( fullPath ) && !wxFileName::DirExists( fullPath ) )
        return pcb::convertError( "unreadable", wxT( "The file is not there." ) ).dump();

    json   footprints = json::array();
    size_t textBytes = 0;
    bool   tooLarge = false;

    auto add =
            [&]( FOOTPRINT* aFootprint, const wxString& aName ) -> bool
            {
                std::unique_ptr<FOOTPRINT> fp( aFootprint );

                if( !fp )
                    return true;

                if( footprints.size() >= pcb::CONVERT_FOOTPRINTS_MAX )
                {
                    tooLarge = true;
                    return false;
                }

                fp->SetFPID( LIB_ID( wxEmptyString, aName ) );
                pcb::resetAsLibrary( *fp, aFlip );
                std::string text = pcb::libraryText( *fp );
                textBytes += text.size();

                if( textBytes > pcb::CONVERT_TEXT_MAX )
                {
                    tooLarge = true;
                    return false;
                }

                json row = json::object();
                row["name"] = toUtf8( aName );
                row["text"] = std::move( text );
                footprints.push_back( std::move( row ) );
                return true;
            };

    PCB_IO_MGR::PCB_FILE_T type = PCB_IO_MGR::FILE_TYPE_NONE;

    try
    {
        type = PCB_IO_MGR::GuessPluginTypeFromLibPath( fullPath );

        if( type == PCB_IO_MGR::NESTED_TABLE || type == PCB_IO_MGR::PCBJAM_FP )
            type = PCB_IO_MGR::FILE_TYPE_NONE;

        if( type != PCB_IO_MGR::FILE_TYPE_NONE )
        {
            // The library road: every footprint the file holds.
            IO_RELEASER<PCB_IO> pi( PCB_IO_MGR::FindPlugin( type ) );

            if( !pi )
                return pcb::convertError( "unreadable" ).dump();

            wxArrayString names;
            pi->FootprintEnumerate( names, fullPath, /*aBestEfforts*/ false );

            if( names.size() > pcb::CONVERT_FOOTPRINTS_MAX )
                return pcb::convertError( "too_large" ).dump();

            for( const wxString& name : names )
            {
                if( !add( pi->FootprintLoad( fullPath, name, /*aKeepUUID*/ false ), name ) )
                    break;
            }
        }
        else
        {
            // The one footprint road: the first plugin with library extensions that reads it.
            for( const auto& plugin : PCB_IO_MGR::PLUGIN_REGISTRY::Instance()->AllPlugins() )
            {
                if( plugin.m_type == PCB_IO_MGR::PCBJAM_FP )
                    continue;

                IO_RELEASER<PCB_IO> probe( plugin.m_createFunc() );

                if( !probe || probe->GetLibraryFileDesc().m_FileExtensions.empty() )
                    continue;

                if( probe->CanReadFootprint( fullPath ) )
                {
                    type = plugin.m_type;
                    break;
                }
            }

            if( type == PCB_IO_MGR::FILE_TYPE_NONE )
                return pcb::convertError( "unreadable", wxT( "Not a footprint file." ) ).dump();

            IO_RELEASER<PCB_IO> pi( PCB_IO_MGR::FindPlugin( type ) );

            if( !pi )
                return pcb::convertError( "unreadable" ).dump();

            wxString name;
            add( pi->ImportFootprint( fullPath, name ), name );
        }
    }
    catch( const IO_ERROR& ioe )
    {
        return pcb::convertError( "unreadable", ioe.What() ).dump();
    }
    catch( ... )
    {
        return pcb::convertError( "unreadable" ).dump();
    }

    if( tooLarge )
        return pcb::convertError( "too_large" ).dump();

    if( footprints.empty() )
        return pcb::convertError( "unreadable", wxT( "No footprint was read." ) ).dump();

    json out = json::object();
    out["format"] = toUtf8( PCB_IO_MGR::ShowType( type ) );
    out["footprints"] = std::move( footprints );
    return out.dump( -1, ' ', false, json::error_handler_t::replace );
}

// ── m13-e: the board's embedded files (kicadEmbeddedFilesGet, Set, Extract: the board halves) ──

/** The board half of kicadEmbeddedFilesGet: {"owner": "board", ...}; "" with no ready frame. */
std::string embeddedFilesGetPcb()
{
    PCB_EDIT_FRAME* fr = pcb::readyBoardFrame();

    if( !fr )
        return "";

    // BOARD::GetFonts' walk: the board's drawings that are texts.
    std::vector<KIFONT::FONT*> candidates;

    for( BOARD_ITEM* item : fr->GetBoard()->Drawings() )
    {
        if( EDA_TEXT* text = dynamic_cast<EDA_TEXT*>( item ) )
        {
            if( KIFONT::FONT* font = text->GetFont(); font && font->IsOutline() )
                candidates.push_back( font );
        }
    }

    return pcb::embfiles::documentJson( fr->GetBoard()->GetEmbeddedFiles(), "board", candidates )
            .dump( -1, ' ', false, json::error_handler_t::replace );
}

/**
 * The board half of kicadEmbeddedFilesSet: the closed shapes read whole, checked against the
 * board, then on the apply coroutine the set with NO undo step (Board Setup makes none) and
 * the board marked modified; embedded.json written last when a run folder is given. False for
 * any other shape, no ready frame, a removal of a name the board does not hold.
 */
bool embeddedFilesSetPcb( std::string aArgsJson )
{
    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() )
        return false;

    pcb::embfiles::SET_ARGS a;

    if( !pcb::embfiles::readSet( args, a ) )
        return false;

    PCB_EDIT_FRAME* fr = pcb::readyBoardFrame();

    if( !fr || !pcb::embfiles::fits( fr->GetBoard()->GetEmbeddedFiles(), a ) )
        return false;

    pcbjam_collab::runOnCoroutine( fr, [a]() {
        PCB_EDIT_FRAME*  frame = pcbFrame();   // re-resolved on the coroutine
        EMBEDDED_FILES*  files = ( frame && frame->GetBoard() ) ? frame->GetBoard()->GetEmbeddedFiles()
                                                                 : nullptr;
        const bool       applied = files && pcb::embfiles::applySet( files, a );

        if( applied )
            frame->OnModify();

        pcb::embfiles::writeSetRecord( a.dir, files, applied );
    } );

    return true;
}

/** The board half of kicadEmbeddedFileExtract (a read of the board; the file and record written). */
bool embeddedFileExtractPcb( std::string aArgsJson )
{
    PCB_EDIT_FRAME* fr = pcb::readyBoardFrame();

    if( !fr )
        return false;

    return pcb::embfiles::extract( fr->GetBoard()->GetEmbeddedFiles(), aArgsJson );
}

// ── st-placefile-e: Append Board from a staged file (kicadPlaceFromFile, the board half) ──────

/**
 * The board half of kicadPlaceFromFile: exactly {"kind": "board", "path": "/tmp/cc-stage/<n>/
 * <file>.kicad_pcb"}. On the apply coroutine PCB_CONTROL::AppendBoard with the KiCad plugin
 * for the path (AppendBoardFromFile's own choice, KICTL_KICAD_ONLY, in place of its file
 * dialog): the board's items on the pointer, ONE commit "Append Board" on the press (a cancel
 * reverts). False for any other shape, kind or path, no ready frame.
 */
bool placeFromFilePcb( std::string aArgsJson )
{
    std::optional<std::string> path = pcb::placefile::pathOf( aArgsJson, "board", ".kicad_pcb" );
    PCB_EDIT_FRAME*            fr = pcb::readyBoardFrame();

    if( !path || !fr || !fr->GetToolManager()->GetTool<PCB_CONTROL>() )
        return false;

    const wxString file = wxString::FromUTF8( path->c_str() );

    pcbjam_collab::runOnCoroutine( fr, [file]() {
        PCB_EDIT_FRAME* frame = pcbFrame();   // re-resolved on the coroutine
        PCB_CONTROL*    control = frame ? frame->GetToolManager()->GetTool<PCB_CONTROL>() : nullptr;

        if( !control )
            return;

        PCB_IO_MGR::PCB_FILE_T type = PCB_IO_MGR::FindPluginTypeFromBoardPath( file, KICTL_KICAD_ONLY );
        IO_RELEASER<PCB_IO>    pi( PCB_IO_MGR::FindPlugin( type ) );

        if( pi )
            control->AppendBoard( *pi, file );
    } );

    return true;
}

/** The board half of kicadFootprintConvert (cc_e5_dispatch.inc picks it in a board frame). */
std::string footprintConvertPcb( std::string aArgsJson )
{
    PCB_EDIT_FRAME* fr = pcb::readyBoardFrame();

    if( !fr )
        return "";

    return footprintConvertBody( aArgsJson, fr->GetPcbNewSettings()->m_FlipDirection );
}

} // namespace cc_e5

EMSCRIPTEN_BINDINGS( cc_e5_pcb )
{
    function( "kicadFootprintForEditor", &cc_e5::footprintForEditor );
    function( "kicadFootprintPushToBoard", &cc_e5::footprintPushToBoard );
}
