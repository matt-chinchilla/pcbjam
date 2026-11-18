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
#include <wx/dialog.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/toplevel.h>

#include <memory>
#include <set>

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
