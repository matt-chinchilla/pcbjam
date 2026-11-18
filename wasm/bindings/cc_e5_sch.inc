// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e5, the schematic face: textually included by eeschema_embind.cpp after
// cc_rules_sch.inc, outside any KICAD_MERGED_EMBIND guard (include subdir eeschema; the
// merged image compiles that TU with -DKICAD_MERGED_EMBIND). It sees the TU's includes and
// helpers (schFrame(), the cc_output namespace) and calls them without editing them.
//
// Every definition of the lane lives in namespace cc_e5 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

#include <wx/dialog.h>
#include <wx/filename.h>
#include <wx/toplevel.h>

#include <sys/stat.h>

#include <cstdio>
#include <optional>
#include <set>
#include <vector>

#include <embedded_files.h>
#include <font/font.h>

namespace cc_e5
{
namespace sch
{
using json = nlohmann::json;

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

/** The schematic editor when it may be asked anything, else nullptr. */
SCH_EDIT_FRAME* readySchFrame()
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() || dialogUp() )
        return nullptr;

    return fr;
}

} // namespace sch
} // namespace cc_e5

// ── m13-e: the document's embedded files (properties addendum 10.1), schematic half ──────────
//
// The same rules in the board face and the schematic face (one translation unit each, so each
// face holds its own copy in its nested namespace): PANEL_EMBEDDED_FILES' own steps on the
// document's EMBEDDED_FILES (the BOARD or the SCHEMATIC), with no window.

namespace cc_e5::sch
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
} // namespace cc_e5::sch

namespace cc_e5::sch
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
} // namespace cc_e5::sch

namespace cc_e5
{
// ── m13-e: the schematic's embedded files (kicadEmbeddedFilesGet, Set, Extract: the schematic
// halves; cc_e5_dispatch.inc picks them in a schematic frame) ────────────────────────────────

/** The schematic half of kicadEmbeddedFilesGet: {"owner": "schematic", ...}; "" with no frame. */
std::string embeddedFilesGetSch()
{
    SCH_EDIT_FRAME* fr = sch::readySchFrame();

    if( !fr )
        return "";

    // SCHEMATIC::GetFonts' walk: every sheet's texts.
    std::vector<KIFONT::FONT*> candidates;

    for( const SCH_SHEET_PATH& sheet : fr->Schematic().Hierarchy() )
    {
        for( SCH_ITEM* item : sheet.LastScreen()->Items() )
        {
            if( EDA_TEXT* text = dynamic_cast<EDA_TEXT*>( item ) )
            {
                if( KIFONT::FONT* font = text->GetFont(); font && !font->IsStroke() )
                    candidates.push_back( font );
            }
        }
    }

    return sch::embfiles::documentJson( fr->Schematic().GetEmbeddedFiles(), "schematic",
                                        candidates )
            .dump( -1, ' ', false, nlohmann::json::error_handler_t::replace );
}

/** The schematic half of kicadEmbeddedFilesSet (the board half's rules; Schematic Setup's). */
bool embeddedFilesSetSch( std::string aArgsJson )
{
    nlohmann::json args = nlohmann::json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() )
        return false;

    sch::embfiles::SET_ARGS a;

    if( !sch::embfiles::readSet( args, a ) )
        return false;

    SCH_EDIT_FRAME* fr = sch::readySchFrame();

    if( !fr || !sch::embfiles::fits( fr->Schematic().GetEmbeddedFiles(), a ) )
        return false;

    pcbjam_collab::runOnCoroutine( fr, [a]() {
        SCH_EDIT_FRAME* frame = schFrame();   // re-resolved on the coroutine
        EMBEDDED_FILES* files = frame ? frame->Schematic().GetEmbeddedFiles() : nullptr;
        const bool      applied = files && sch::embfiles::applySet( files, a );

        if( applied )
            frame->OnModify();

        sch::embfiles::writeSetRecord( a.dir, files, applied );
    } );

    return true;
}

/** The schematic half of kicadEmbeddedFileExtract. */
bool embeddedFileExtractSch( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = sch::readySchFrame();

    if( !fr )
        return false;

    return sch::embfiles::extract( fr->Schematic().GetEmbeddedFiles(), aArgsJson );
}

// ── st-placefile-e: Import Sheet from a staged file (kicadPlaceFromFile, the schematic half) ──

/**
 * The schematic half of kicadPlaceFromFile: exactly {"kind": "sheet", "path": "/tmp/cc-stage/
 * <n>/<file>.kicad_sch"}. On the apply coroutine SCH_ACTIONS::importSheet with the path as its
 * parameter (SCH_DRAWING_TOOLS::ImportSheet reads it at once and skips its file dialog): the
 * sheet or its contents on the pointer as the editor's design block options say, ONE commit on
 * the press. The parameter is a static kept past the call. False for any other shape, kind or
 * path, no ready frame.
 */
bool placeFromFileSch( std::string aArgsJson )
{
    std::optional<std::string> path = sch::placefile::pathOf( aArgsJson, "sheet", ".kicad_sch" );
    SCH_EDIT_FRAME*            fr = sch::readySchFrame();

    if( !path || !fr )
        return false;

    const wxString file = wxString::FromUTF8( path->c_str() );

    pcbjam_collab::runOnCoroutine( fr, [file]() {
        static wxString s_sheetFile;   // the action's parameter, alive past the call

        SCH_EDIT_FRAME* frame = schFrame();   // re-resolved on the coroutine

        if( !frame )
            return;

        s_sheetFile = file;
        frame->GetToolManager()->RunAction<wxString*>( SCH_ACTIONS::importSheet, &s_sheetFile );
    } );

    return true;
}

} // namespace cc_e5

EMSCRIPTEN_BINDINGS( cc_e5_sch )
{
}
