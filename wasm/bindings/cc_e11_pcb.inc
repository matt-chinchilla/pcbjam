// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e11, the board face: textually included by pcbnew_embind.cpp after cc_rules_pcb.inc,
// outside any KICAD_MERGED_EMBIND guard (include subdir pcbnew; the merged image compiles
// that TU with -DKICAD_MERGED_EMBIND). It sees the TU's includes and helpers (pcbFrame(),
// the cc_output namespace, cc_fp_items.h's declarations in the merged image only) and calls them without editing them.
//
// Every definition of the lane lives in namespace cc_e11 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

// 3d-e5, the board's model lines (the Models window, PE33): kicadBoardModels reads every
// footprint's model lines and the board's embedded files, kicadBoardSetModels rewrites many
// footprints' lines in ONE commit, kicadBoardEmbedModels keeps staged model files inside the
// board as KiCad's Collect And Embed 3D Models does (pcbnew/tools/pcb_control.cpp,
// CollectAndEmbed3DModels). Merged image only (KICAD_MERGED_EMBIND): the staged model probe
// lives in the 3D viewer library, and occ_service has no editor to serve.
#ifdef KICAD_MERGED_EMBIND

#include <board_commit.h>
#include <embedded_files.h>
#include <filename_resolver.h>
#include <footprint.h>
#include <project_pcb.h>
#include <wildcards_and_files_ext.h>

#include <wx/dir.h>
#include <wx/filefn.h>

#include <cmath>
#include <map>
#include <set>

// The engine's staged model probe (3d-viewer/3d_cache/pcbjam_model_fetch.h, PCBJam's, not on
// the bindings' include path): the absolute path of a body staged under /pcbjam/3dmodels, "" on
// a miss. A pure path probe.
namespace PCBJAM_3D
{
wxString FindStagedModel( const wxString& aModelRef );
}

namespace cc_e11
{
namespace pcb
{

constexpr size_t kMaxItems = 4096;
constexpr size_t kMaxLines = 64;
constexpr size_t kMaxPath = 1024;
constexpr long long kMaxEmbedFile = 25LL * 1024 * 1024;
constexpr long long kMaxEmbedTotal = 512LL * 1024 * 1024;

/** A uuid as the page sends it: lowercase 8-4-4-4-12. */
bool isUuid( const std::string& aId )
{
    if( aId.size() != 36 )
        return false;

    for( size_t i = 0; i < aId.size(); ++i )
    {
        const char c = aId[i];

        if( i == 8 || i == 13 || i == 18 || i == 23 )
        {
            if( c != '-' )
                return false;
        }
        else if( !( ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'f' ) ) )
        {
            return false;
        }
    }

    return true;
}

/** The board's footprint with this uuid, or nullptr. */
FOOTPRINT* footprintOf( PCB_EDIT_FRAME* aFrame, const std::string& aId )
{
    BOARD_ITEM* item = testResolve( aFrame, aId );

    if( !item || item->Type() != PCB_FOOTPRINT_T )
        return nullptr;

    return static_cast<FOOTPRINT*>( item );
}

json vec3Json( const VECTOR3D& aVec )
{
    return json::array( { aVec.x, aVec.y, aVec.z } );
}

/** [x, y, z], three finite numbers. */
bool readVec3( const json& aValue, VECTOR3D& aOut )
{
    if( !aValue.is_array() || aValue.size() != 3 )
        return false;

    for( const json& n : aValue )
    {
        if( !n.is_number() || !std::isfinite( n.get<double>() ) )
            return false;
    }

    aOut = VECTOR3D( aValue[0].get<double>(), aValue[1].get<double>(), aValue[2].get<double>() );
    return true;
}

/** A model path: 1 to 1,024 characters, no control character. */
bool isModelPath( const std::string& aPath )
{
    if( aPath.empty() || aPath.size() > kMaxPath )
        return false;

    for( unsigned char c : aPath )
    {
        if( c < 0x20 || c == 0x7f )
            return false;
    }

    return true;
}

/** One ModelLine without found: exactly {path, offset, rotate, scale, opacity, show}. */
bool readModelLine( const json& aLine, FP_3DMODEL& aOut )
{
    if( !aLine.is_object() || aLine.size() != 6 )
        return false;

    auto path = aLine.find( "path" );
    auto offset = aLine.find( "offset" );
    auto rotate = aLine.find( "rotate" );
    auto scale = aLine.find( "scale" );
    auto opacity = aLine.find( "opacity" );
    auto show = aLine.find( "show" );

    if( path == aLine.end() || !path->is_string() || offset == aLine.end()
            || rotate == aLine.end() || scale == aLine.end() || opacity == aLine.end()
            || !opacity->is_number() || show == aLine.end() || !show->is_boolean() )
    {
        return false;
    }

    const std::string text = path->get<std::string>();
    const double      alpha = opacity->get<double>();

    if( !isModelPath( text ) || !std::isfinite( alpha ) || alpha < 0.0 || alpha > 1.0 )
        return false;

    if( !readVec3( *offset, aOut.m_Offset ) || !readVec3( *rotate, aOut.m_Rotation )
            || !readVec3( *scale, aOut.m_Scale ) )
    {
        return false;
    }

    aOut.m_Filename = wxString::FromUTF8( text.c_str() );
    aOut.m_Opacity = alpha;
    aOut.m_Show = show->get<bool>();
    return true;
}

/** KiCad's own word for an embedded file's type (common/embedded_files.cpp). */
const char* embeddedTypeWord( EMBEDDED_FILES::EMBEDDED_FILE::FILE_TYPE aType )
{
    switch( aType )
    {
    case EMBEDDED_FILES::EMBEDDED_FILE::FILE_TYPE::FONT:      return "font";
    case EMBEDDED_FILES::EMBEDDED_FILE::FILE_TYPE::MODEL:     return "model";
    case EMBEDDED_FILES::EMBEDDED_FILE::FILE_TYPE::WORKSHEET: return "worksheet";
    case EMBEDDED_FILES::EMBEDDED_FILE::FILE_TYPE::DATASHEET: return "datasheet";
    default:                                                  return "other";
    }
}

/**
 * Where a model line resolves now: "embedded" (a kicad-embed:// name the footprint or the
 * board holds), "project" (KiCad's own resolver finds the file, outside the staged bodies),
 * "staged" (a body staged under /pcbjam/3dmodels), else "missing".
 */
const char* modelFound( FILENAME_RESOLVER* aResolver, const wxString& aProjectPath,
                        BOARD* aBoard, FOOTPRINT* aFootprint, const wxString& aPath )
{
    const wxString embedPrefix = wxString( FILEEXT::KiCadUriPrefix ) + wxS( "://" );

    if( aPath.StartsWith( embedPrefix ) )
    {
        const wxString name = aPath.Mid( embedPrefix.length() );

        if( ( aFootprint->GetEmbeddedFiles() && aFootprint->GetEmbeddedFiles()->HasFile( name ) )
                || ( aBoard->GetEmbeddedFiles() && aBoard->GetEmbeddedFiles()->HasFile( name ) ) )
        {
            return "embedded";
        }

        return "missing";
    }

    const wxString stagedRoot = wxS( "/pcbjam/3dmodels/" );

    if( aResolver )
    {
        const wxString resolved = aResolver->ResolvePath( aPath, aProjectPath, {} );

        if( !resolved.IsEmpty() && wxFileName::FileExists( resolved ) )
            return resolved.StartsWith( stagedRoot ) ? "staged" : "project";
    }

    if( !PCBJAM_3D::FindStagedModel( aPath ).IsEmpty() )
        return "staged";

    return "missing";
}

/** One model update, read whole before anything is queued. */
struct MODEL_SET
{
    std::string             uuid;
    std::vector<FP_3DMODEL> models;
};

/** One file to keep inside the board. */
struct MODEL_EMBED
{
    std::string uuid;
    size_t      index = 0;
    std::string file;
    std::string alt;
};

/** "/tmp/cc-embed/<n>/<name>": its folder (an embed run folder) and a plain name. */
bool splitEmbedFile( const std::string& aFile, std::string& aDir )
{
    const size_t slash = aFile.rfind( '/' );

    if( slash == std::string::npos || slash + 1 >= aFile.size() )
        return false;

    aDir = aFile.substr( 0, slash );
    const std::string name = aFile.substr( slash + 1 );

    return cc_output::IsEmbedRunDir( aDir ) && name != "." && name != ".."
           && aDir.find( '/', cc_output::EmbedRoot().size() ) == std::string::npos;
}

} // namespace pcb


// board.models (cap boardModels): every footprint's model lines and the board's embedded files.
// {"footprints": [{uuid, ref, models: [{path, offset, rotate, scale, opacity, show, found}],
// kind: {smd, tht, unspecified, notInPos, dnp}}], "embedded": [{name, type, bytes}]}; "" with no
// board or while an open is in flight. A read: synchronous, nothing changed.
std::string boardModels()
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return "";

    BOARD*             board = fr->GetBoard();
    PROJECT*           project = board->GetProject();
    FILENAME_RESOLVER* resolver =
            project ? PROJECT_PCB::Get3DFilenameResolver( project ) : nullptr;
    const wxString     projectPath = project ? project->GetProjectPath() : wxString();
    const wxString     variant = board->GetCurrentVariant();
    wxLogNull          noLog;

    json footprints = json::array();

    for( FOOTPRINT* fp : board->Footprints() )
    {
        json models = json::array();

        for( const FP_3DMODEL& model : fp->Models() )
        {
            json line = json::object();
            line["path"] = model.m_Filename.utf8_string();
            line["offset"] = pcb::vec3Json( model.m_Offset );
            line["rotate"] = pcb::vec3Json( model.m_Rotation );
            line["scale"] = pcb::vec3Json( model.m_Scale );
            line["opacity"] = model.m_Opacity;
            line["show"] = model.m_Show;
            line["found"] = pcb::modelFound( resolver, projectPath, board, fp, model.m_Filename );
            models.push_back( std::move( line ) );
        }

        const int attributes = fp->GetAttributes();
        json      kind = json::object();
        kind["smd"] = ( attributes & FP_SMD ) != 0;
        kind["tht"] = ( attributes & FP_THROUGH_HOLE ) != 0;
        kind["unspecified"] = ( attributes & ( FP_SMD | FP_THROUGH_HOLE ) ) == 0;
        kind["notInPos"] = fp->GetExcludedFromPosFilesForVariant( variant );
        kind["dnp"] = fp->GetDNPForVariant( variant );

        json entry = json::object();
        entry["uuid"] = fp->m_Uuid.AsStdString();
        entry["ref"] = fp->GetReference().utf8_string();
        entry["models"] = std::move( models );
        entry["kind"] = std::move( kind );
        footprints.push_back( std::move( entry ) );
    }

    json embedded = json::array();

    if( const EMBEDDED_FILES* files = board->GetEmbeddedFiles() )
    {
        for( const auto& [name, file] : files->EmbeddedFileMap() )
        {
            if( !file )
                continue;

            json entry = json::object();
            entry["name"] = name.utf8_string();
            entry["type"] = pcb::embeddedTypeWord( file->type );
            entry["bytes"] = file->decompressedData.size();
            embedded.push_back( std::move( entry ) );
        }
    }

    json out = json::object();
    out["footprints"] = std::move( footprints );
    out["embedded"] = std::move( embedded );
    return out.dump();
}


// board.setModels (cap boardModels): {"label": "Update model names" | "Change models", "items":
// [{uuid, models: [{path, offset, rotate, scale, opacity, show}]}]}, 1 to 4,096 items (each a
// footprint of the board, none twice), at most 64 lines each, every path 1 to 1,024 characters
// with no control character. Read whole first; false (nothing queued) for any other shape, with
// no board, or while an open is in flight. True once ONE BOARD_COMMIT (Modify per footprint,
// pushed as the label: one undo step) is queued on the apply coroutine, which re-resolves every
// footprint there and pushes nothing if one is gone (the host waits for kicadCollabBusy).
bool boardSetModels( std::string aArgsJson )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 2 )
        return false;

    auto label = args.find( "label" );
    auto items = args.find( "items" );

    if( label == args.end() || !label->is_string() || items == args.end() || !items->is_array()
            || items->empty() || items->size() > pcb::kMaxItems )
    {
        return false;
    }

    const std::string labelText = label->get<std::string>();

    if( labelText != "Update model names" && labelText != "Change models" )
        return false;

    std::vector<pcb::MODEL_SET> sets;
    std::set<std::string>       seen;

    for( const json& item : *items )
    {
        if( !item.is_object() || item.size() != 2 )
            return false;

        auto uuid = item.find( "uuid" );
        auto models = item.find( "models" );

        if( uuid == item.end() || !uuid->is_string() || models == item.end()
                || !models->is_array() || models->size() > pcb::kMaxLines )
        {
            return false;
        }

        pcb::MODEL_SET set;
        set.uuid = uuid->get<std::string>();

        if( !pcb::isUuid( set.uuid ) || !seen.insert( set.uuid ).second
                || !pcb::footprintOf( fr, set.uuid ) )
        {
            return false;
        }

        for( const json& line : *models )
        {
            FP_3DMODEL model;

            if( !pcb::readModelLine( line, model ) )
                return false;

            set.models.push_back( model );
        }

        sets.push_back( std::move( set ) );
    }

    const wxString commitLabel = wxString::FromUTF8( labelText.c_str() );

    pcbjam_collab::runOnCoroutine( fr, [fr, sets, commitLabel]() {   // re-resolve (S4)
        std::vector<FOOTPRINT*> live;

        for( const pcb::MODEL_SET& set : sets )
        {
            FOOTPRINT* fp = pcb::footprintOf( fr, set.uuid );

            if( !fp )
                return;

            live.push_back( fp );
        }

        BOARD_COMMIT commit( fr );

        for( size_t i = 0; i < sets.size(); ++i )
        {
            commit.Modify( live[i] );
            live[i]->Models() = sets[i].models;
        }

        commit.Push( commitLabel );
    } );

    return true;
}


// board.embedModels (cap boardModels): {"items": [{uuid, index, file, alt?}]}, 1 to 4,096,
// every file and alt in ONE embed run folder "/tmp/cc-embed/<n>/<name>" (cc_output's
// IsEmbedRunDir), each at most 25 MiB and all together at most 512 MiB, index a model line of
// that footprint, no line twice. Each file is added to the BOARD's embedded files as KiCad's
// Collect And Embed adds it (EMBEDDED_FILES::AddFile, its type from the extension: model), the
// line's path set to the file's link (kicad-embed://<name>), alt (a VRML's STEP sibling) added
// beside it; ONE commit "Keep models inside the board"; the folder removed. Answers {embedded:
// lines relinked, names: [every file name added], bytes: their total} once that work is queued
// on the apply coroutine (the host waits for kicadCollabBusy); "" on refusal (nothing queued,
// the folder left to the host).
std::string boardEmbedModels( std::string aArgsJson )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return "";

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 1 )
        return "";

    auto items = args.find( "items" );

    if( items == args.end() || !items->is_array() || items->empty()
            || items->size() > pcb::kMaxItems )
    {
        return "";
    }

    std::vector<pcb::MODEL_EMBED>          embeds;
    std::set<std::pair<std::string, size_t>> lines;
    std::map<std::string, long long>       files;   // path -> bytes, each file counted once
    std::string                            runDir;

    auto takeFile = [&]( const std::string& aFile ) -> bool
                    {
                        std::string dir;

                        if( !pcb::splitEmbedFile( aFile, dir ) || ( !runDir.empty() && dir != runDir ) )
                            return false;

                        runDir = dir;

                        const wxString path = wxString::FromUTF8( aFile.c_str() );

                        if( !wxFileName::FileExists( path ) )
                            return false;

                        const wxULongLong size = wxFileName::GetSize( path );

                        if( size == wxInvalidSize || size.GetValue() > (unsigned long long) pcb::kMaxEmbedFile )
                            return false;

                        files[aFile] = (long long) size.GetValue();
                        return true;
                    };

    for( const json& item : *items )
    {
        if( !item.is_object() || item.size() < 3 || item.size() > 4 )
            return "";

        auto uuid = item.find( "uuid" );
        auto index = item.find( "index" );
        auto file = item.find( "file" );
        auto alt = item.find( "alt" );

        if( uuid == item.end() || !uuid->is_string() || index == item.end()
                || !index->is_number_integer() || index->get<long long>() < 0
                || file == item.end() || !file->is_string()
                || ( item.size() == 4 && ( alt == item.end() || !alt->is_string() ) ) )
        {
            return "";
        }

        pcb::MODEL_EMBED embed;
        embed.uuid = uuid->get<std::string>();
        embed.index = (size_t) index->get<long long>();
        embed.file = file->get<std::string>();

        if( alt != item.end() )
            embed.alt = alt->get<std::string>();

        FOOTPRINT* fp = pcb::isUuid( embed.uuid ) ? pcb::footprintOf( fr, embed.uuid ) : nullptr;

        if( !fp || embed.index >= fp->Models().size()
                || !lines.insert( { embed.uuid, embed.index } ).second || !takeFile( embed.file )
                || ( !embed.alt.empty() && !takeFile( embed.alt ) ) )
        {
            return "";
        }

        embeds.push_back( std::move( embed ) );
    }

    long long total = 0;
    json      names = json::array();
    std::set<std::string> nameSet;

    for( const auto& [path, bytes] : files )
    {
        total += bytes;
        nameSet.insert( path.substr( path.rfind( '/' ) + 1 ) );
    }

    if( total > pcb::kMaxEmbedTotal )
        return "";

    for( const std::string& name : nameSet )
        names.push_back( name );

    pcbjam_collab::runOnCoroutine( fr, [fr, embeds, runDir]() {   // re-resolve (S4)
        const wxString folder = wxString::FromUTF8( runDir.c_str() );
        auto           removeFolder = [&]()
                                      {
                                          wxFileName::Rmdir( folder, wxPATH_RMDIR_RECURSIVE );
                                      };

        BOARD*          board = fr->GetBoard();
        EMBEDDED_FILES* boardFiles = board ? board->GetEmbeddedFiles() : nullptr;

        if( !boardFiles )
        {
            removeFolder();
            return;
        }

        std::vector<FOOTPRINT*> live;

        for( const pcb::MODEL_EMBED& embed : embeds )
        {
            FOOTPRINT* fp = pcb::footprintOf( fr, embed.uuid );

            if( !fp || embed.index >= fp->Models().size() )
            {
                removeFolder();
                return;
            }

            live.push_back( fp );
        }

        // The files first: a file KiCad cannot add leaves the board as it was (the names this
        // call added are taken out again) and pushes nothing.
        std::vector<wxString>                                  added;
        std::vector<EMBEDDED_FILES::EMBEDDED_FILE*>            links;
        wxLogNull                                              noLog;

        auto addFile = [&]( const std::string& aPath ) -> EMBEDDED_FILES::EMBEDDED_FILE*
                       {
                           wxFileName     fname( wxString::FromUTF8( aPath.c_str() ) );
                           const bool     had = boardFiles->HasFile( fname.GetFullName() );
                           auto*          file = boardFiles->AddFile( fname, false );

                           if( file && !had )
                               added.push_back( fname.GetFullName() );

                           return file;
                       };

        for( const pcb::MODEL_EMBED& embed : embeds )
        {
            EMBEDDED_FILES::EMBEDDED_FILE* file = addFile( embed.file );

            if( !file || ( !embed.alt.empty() && !addFile( embed.alt ) ) )
            {
                for( const wxString& name : added )
                    boardFiles->RemoveFile( name );

                removeFolder();
                return;
            }

            links.push_back( file );
        }

        BOARD_COMMIT          commit( fr );
        std::set<FOOTPRINT*>  modified;

        for( size_t i = 0; i < embeds.size(); ++i )
        {
            if( modified.insert( live[i] ).second )
                commit.Modify( live[i] );

            live[i]->Models()[embeds[i].index].m_Filename = links[i]->GetLink();
        }

        commit.Push( wxS( "Keep models inside the board" ) );
        removeFolder();
    } );

    json out = json::object();
    out["embedded"] = embeds.size();
    out["names"] = std::move( names );
    out["bytes"] = total;
    return out.dump();
}

} // namespace cc_e11

#endif // KICAD_MERGED_EMBIND

EMSCRIPTEN_BINDINGS( cc_e11_pcb )
{
#ifdef KICAD_MERGED_EMBIND
    function( "kicadBoardModels", &cc_e11::boardModels );
    function( "kicadBoardSetModels", &cc_e11::boardSetModels );
    function( "kicadBoardEmbedModels", &cc_e11::boardEmbedModels );
#endif
}
