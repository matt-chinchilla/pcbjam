// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e3, the board face: textually included by pcbnew_embind.cpp after cc_rules_pcb.inc,
// outside any KICAD_MERGED_EMBIND guard (include subdir pcbnew; the merged image compiles
// that TU with -DKICAD_MERGED_EMBIND). It sees the TU's includes and helpers (pcbFrame(),
// the cc_output namespace, cc_fp_items.h's declarations in the merged image only) and calls them without editing them.
//
// Every definition of the lane lives in namespace cc_e3 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

#include <tools/board_item_host.h>
#include <pcb_target.h>
#include <pcb_generator.h>
#include <pcb_reference_image.h>
#include <lib_id.h>

#include <algorithm>
#include <set>

namespace cc_e3
{
namespace pcb
{
using json = nlohmann::json;

/** The run folders of the board item writes (m4-e): /tmp/cc-boarditems/<n>. */
const char* const BOARD_ITEMS_ROOT = "/tmp/cc-boarditems/";

/** A board's own record kind for an item (addendum 3.4), or "" for none. */
std::string boardRecordKind( BOARD_ITEM* aItem )
{
    switch( aItem->Type() )
    {
    case PCB_PAD_T:             return "pad";
    case PCB_SHAPE_T:           return "shape";
    case PCB_TEXTBOX_T:         return "textbox";
    case PCB_TABLE_T:           return "table";
    case PCB_BARCODE_T:         return "barcode";
    case PCB_REFERENCE_IMAGE_T: return "image";
    case PCB_POINT_T:           return "point";
    case PCB_GROUP_T:           return "group";
    case PCB_TARGET_T:          return "target";

    case PCB_DIM_ALIGNED_T:
    case PCB_DIM_LEADER_T:
    case PCB_DIM_CENTER_T:
    case PCB_DIM_RADIAL_T:
    case PCB_DIM_ORTHOGONAL_T:
        return "dimension";

    case PCB_ZONE_T:
    {
        // A copper zone of the board itself stays run A's zone sheet.
        ZONE* zone = static_cast<ZONE*>( aItem );

        if( !zone->GetIsRuleArea() && !zone->GetParentFootprint() && zone->IsOnCopperLayer() )
            return "";

        return "zone";
    }

    case PCB_GENERATOR_T:
        return static_cast<PCB_GENERATOR*>( aItem )->GetGeneratorType() == wxS( "tuning_pattern" )
                       ? "tuning"
                       : "";

    default:
        return "";
    }
}

/** A kind the board answers itself, beside FP 7.2's (addendum 3.5). */
bool boardOnlyKind( const std::string& aKind )
{
    return aKind == "group" || aKind == "target" || aKind == "tuning";
}

json groupRecord( PCB_GROUP* aGroup )
{
    json out = json::object();
    out["kind"] = "group";
    out["uuid"] = toUtf8( aGroup->m_Uuid.AsString() );
    out["name"] = ccText( aGroup->GetName() );
    out["locked"] = aGroup->IsLocked();
    out["members"] = (int) aGroup->GetItems().size();
    out["designBlock"] = aGroup->HasDesignBlockLink()
                                 ? ccText( aGroup->GetDesignBlockLibId().Format().wx_str() )
                                 : std::string();
    return out;
}

json targetRecord( PCB_TARGET* aTarget )
{
    json out = json::object();
    out["kind"] = "target";
    out["uuid"] = toUtf8( aTarget->m_Uuid.AsString() );
    out["layer"] = ccLayer( aTarget->GetLayer() );
    out["x"] = ccMm( aTarget->GetPosition().x );
    out["y"] = ccMm( aTarget->GetPosition().y );
    out["shape"] = aTarget->GetShape() ? "x" : "plus";
    out["size"] = ccMm( aTarget->GetSize() );
    out["width"] = ccMm( aTarget->GetWidth() );
    return out;
}

/** The item record of an item of aKind, or null when no reader answers it. */
json itemRecord( BOARD_ITEM* aItem, const std::string& aKind )
{
    if( aKind == "group" )
        return groupRecord( static_cast<PCB_GROUP*>( aItem ) );

    if( aKind == "target" )
        return targetRecord( static_cast<PCB_TARGET*>( aItem ) );

    if( aKind == "tuning" )
        return CcTuningPatternRecord( aItem );

#ifdef KICAD_MERGED_EMBIND
    // FP 7.2's kinds through the item table (R1), in the board's coordinates: the board's
    // origin, the board frame's facts (a pad's angle relative to its footprint).
    return cc_fpitems::ccFpItemJson( aItem, VECTOR2I( 0, 0 ), /*aBoardFrame*/ true );
#else
    return json();
#endif
}

/** Every net of the board, by the records' name, sorted ("no net" left out). */
json netChoices( BOARD* aBoard )
{
    std::vector<std::string> names;

    for( NETINFO_ITEM* net : aBoard->GetNetInfo() )
    {
        if( net && net->GetNetCode() > 0 )
            names.push_back( ccNet( net->GetNetname() ) );
    }

    std::sort( names.begin(), names.end() );
    names.erase( std::unique( names.begin(), names.end() ), names.end() );
    return json( names );
}

/** The BoardRecordSheet of an item of aKind, or null when its reader answers nothing. */
json recordSheet( BOARD* aBoard, BOARD_ITEM* aItem, const std::string& aKind, const json& aNets )
{
    json item = itemRecord( aItem, aKind );

    if( !item.is_object() )
        return json();

    FOOTPRINT* fp = aItem->GetParentFootprint();
    json       sheet = json::object();

    sheet["kind"] = "record";
    sheet["uuid"] = toUtf8( aItem->m_Uuid.AsString() );
    sheet["parent"] = fp ? toUtf8( fp->m_Uuid.AsString() ) : std::string();
    sheet["parentRef"] = fp ? ccText( fp->GetReference() ) : std::string();

    if( aKind == "pad" )
    {
        sheet["net"] = ccNet( static_cast<PAD*>( aItem )->GetNetname() );
        sheet["netChoices"] = aNets;
    }
    else
    {
        sheet["net"] = nullptr;
        sheet["netChoices"] = json::array();
    }

    sheet["item"] = std::move( item );
    return sheet;
}

/** A group set: name (at most 1,024 bytes), locked, designBlock ('lib:name' KiCad parses, or ''). */
bool groupSetValid( const json& aSet )
{
    for( auto it = aSet.begin(); it != aSet.end(); ++it )
    {
        if( it.key() == "name" )
        {
            if( !it.value().is_string() || it.value().get<std::string>().size() > 1024 )
                return false;
        }
        else if( it.key() == "locked" )
        {
            if( !it.value().is_boolean() )
                return false;
        }
        else if( it.key() == "designBlock" )
        {
            if( !it.value().is_string() )
                return false;

            const std::string text = it.value().get<std::string>();
            LIB_ID            libId;

            // DIALOG_GROUP_PROPERTIES refuses a link LIB_ID cannot parse.
            if( !text.empty() && libId.Parse( wxString::FromUTF8( text.c_str() ), true ) >= 0 )
                return false;
        }
        else
        {
            return false;
        }
    }

    return true;
}

void groupApply( PCB_GROUP* aGroup, const json& aSet, BOARD_COMMIT& aCommit )
{
    // DIALOG_GROUP_PROPERTIES::TransferDataFromWindow's own steps.
    aCommit.Modify( aGroup, nullptr, RECURSE_MODE::RECURSE );

    if( aSet.contains( "name" ) )
        aGroup->SetName( wxString::FromUTF8( aSet["name"].get<std::string>().c_str() ) );

    if( aSet.contains( "locked" ) )
        aGroup->SetLocked( aSet["locked"].get<bool>() );

    if( aSet.contains( "designBlock" ) )
    {
        const std::string text = aSet["designBlock"].get<std::string>();
        LIB_ID            libId;

        if( !text.empty() )
            libId.Parse( wxString::FromUTF8( text.c_str() ), true );

        aGroup->SetDesignBlockLibId( libId );
    }
}

/** A target set: shape, size (at least 1 mil, the dialog's floor), width, x, y, layer. */
bool targetSetValid( BOARD* aBoard, const json& aSet )
{
    for( auto it = aSet.begin(); it != aSet.end(); ++it )
    {
        const json& v = it.value();

        if( it.key() == "shape" )
        {
            if( !v.is_string() || ( v != "plus" && v != "x" ) )
                return false;
        }
        else if( it.key() == "size" )
        {
            if( !v.is_number() || !std::isfinite( v.get<double>() ) || v.get<double>() < 0.0254
                    || v.get<double>() > 1000.0 )
            {
                return false;
            }
        }
        else if( it.key() == "width" )
        {
            if( !v.is_number() || !std::isfinite( v.get<double>() ) || v.get<double>() < 0.0
                    || v.get<double>() > 1000.0 )
            {
                return false;
            }
        }
        else if( it.key() == "x" || it.key() == "y" )
        {
            if( !v.is_number() || !std::isfinite( v.get<double>() ) || std::abs( v.get<double>() ) > 2000.0 )
                return false;
        }
        else if( it.key() == "layer" )
        {
            if( !v.is_string() || ccLayerByName( aBoard, v.get<std::string>() ) == UNDEFINED_LAYER )
                return false;
        }
        else
        {
            return false;
        }
    }

    return true;
}

void targetApply( BOARD* aBoard, PCB_TARGET* aTarget, const json& aSet, BOARD_COMMIT& aCommit )
{
    aCommit.Modify( aTarget );

    if( aSet.contains( "width" ) )
        aTarget->SetWidth( pcbIUScale.mmToIU( aSet["width"].get<double>() ) );

    if( aSet.contains( "size" ) )
        aTarget->SetSize( pcbIUScale.mmToIU( aSet["size"].get<double>() ) );

    if( aSet.contains( "shape" ) )
        aTarget->SetShape( aSet["shape"] == "x" ? 1 : 0 );

    if( aSet.contains( "x" ) || aSet.contains( "y" ) )
    {
        VECTOR2I pos = aTarget->GetPosition();

        if( aSet.contains( "x" ) )
            pos.x = pcbIUScale.mmToIU( aSet["x"].get<double>() );

        if( aSet.contains( "y" ) )
            pos.y = pcbIUScale.mmToIU( aSet["y"].get<double>() );

        aTarget->SetPosition( pos );
    }

    if( aSet.contains( "layer" ) )
        aTarget->SetLayer( ccLayerByName( aBoard, aSet["layer"].get<std::string>() ) );
}

/** One record set, read and checked before anything is queued. */
struct RECORDS_SET
{
    long long                run = 0;
    std::string              dir;
    std::vector<std::string> uuids;
    std::string              kind;
    json                     set;
    bool                     hasNet = false;
    std::string              net;   // the records' name, "" for no net
    json                     rest;  // the set less "net", for the item table
};

/** aArgs read whole: false for any other shape, an unknown uuid, or items of another kind. */
bool readRecordsSet( PCB_EDIT_FRAME* aFrame, const std::string& aArgs, RECORDS_SET& aOut )
{
    json args = json::parse( aArgs, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 5 )
        return false;

    for( const char* key : { "run", "dir", "uuids", "kind", "set" } )
    {
        if( !args.contains( key ) )
            return false;
    }

    if( !args["run"].is_number_integer() || args["run"].get<long long>() < 0
            || !args["dir"].is_string() || !args["uuids"].is_array() || !args["kind"].is_string()
            || !args["set"].is_object() || args["set"].empty() )
    {
        return false;
    }

    aOut.run = args["run"].get<long long>();
    aOut.dir = args["dir"].get<std::string>();
    aOut.kind = args["kind"].get<std::string>();
    aOut.set = args["set"];

    if( !ccIsRunDir( aOut.dir, BOARD_ITEMS_ROOT ) || aOut.set.contains( "kind" )
            || aOut.set.contains( "uuid" ) )
    {
        return false;
    }

    const json& uuids = args["uuids"];

    if( uuids.empty() || uuids.size() > 4096 )
        return false;

    BOARD*                board = aFrame->GetBoard();
    std::set<std::string> seen;

    for( const json& u : uuids )
    {
        if( !u.is_string() || !ccIsLowerUuid( u.get<std::string>() )
                || !seen.insert( u.get<std::string>() ).second )
        {
            return false;
        }

        BOARD_ITEM* item = ccResolve( board, u.get<std::string>() );

        if( !item || boardRecordKind( item ) != aOut.kind )
            return false;

        aOut.uuids.push_back( u.get<std::string>() );
    }

    if( aOut.kind == "group" )
        return groupSetValid( aOut.set );

    if( aOut.kind == "target" )
        return targetSetValid( board, aOut.set );

    if( aOut.kind == "tuning" )
        return CcTuningSetValid( aOut.set );

    aOut.rest = aOut.set;

    if( aOut.kind == "pad" && aOut.set.contains( "net" ) )
    {
        const json& net = aOut.set["net"];

        if( !net.is_string() )
            return false;

        aOut.hasNet = true;
        aOut.net = net.get<std::string>();

        if( !aOut.net.empty() && !ccFindNet( board, aOut.net ) )
            return false;

        aOut.rest.erase( "net" );
    }

    return true;
}

/** One item's share of a records set, into aCommit; false when its kind refuses the set. */
bool applyRecordsSet( PCB_EDIT_FRAME* aFrame, BOARD_ITEM* aItem, const RECORDS_SET& aSet,
                      BOARD_COMMIT& aCommit )
{
    BOARD* board = aFrame->GetBoard();

    if( aSet.kind == "group" )
    {
        groupApply( static_cast<PCB_GROUP*>( aItem ), aSet.set, aCommit );
        return true;
    }

    if( aSet.kind == "target" )
    {
        targetApply( board, static_cast<PCB_TARGET*>( aItem ), aSet.set, aCommit );
        return true;
    }

    if( aSet.kind == "tuning" )
        return CcTuningPatternApply( aFrame, aItem, aSet.set, aCommit );

#ifdef KICAD_MERGED_EMBIND
    if( !aSet.rest.empty()
            && !cc_fpitems::ccFpItemApply( aItem, aSet.rest, aCommit, VECTOR2I( 0, 0 ),
                                           /*aBoardFrame*/ true ) )
    {
        return false;
    }

    if( aSet.hasNet )
    {
        // The pad dialog's net selector: the named net, or no net.
        PAD* pad = static_cast<PAD*>( aItem );

        if( aSet.rest.empty() )
            aCommit.Modify( pad );

        if( aSet.net.empty() )
            pad->SetNetCode( NETINFO_LIST::UNCONNECTED );
        else if( NETINFO_ITEM* net = ccFindNet( board, aSet.net ) )
            pad->SetNet( net );
        else
            return false;
    }

    return true;
#else
    return false;
#endif
}

} // namespace pcb


// A marker (engine run 15, m4-e): this engine's board editor asks the page before KiCad's
// properties dialogs of every board item run A's ask leaves to KiCad (pad, shape, dimension,
// text box, barcode, reference image, rule area or keepout or non copper zone, point, target,
// group, tuning pattern) and before the new text, text box, barcode and picture dialogs
// (kicad pcbnew/tools/board_item_host.h CcAskHostBoardItem, window.kicadHostBoardItem), so the
// page may take those requests (the island's ev.boardItem, cap boardItemKinds).
bool kicadHostBoardItems()
{
    return true;
}


#ifdef KICAD_MERGED_EMBIND
// board.item.get with full (cap boardItemKinds): aArgsJson exactly {"uuids": [1 to 512
// lowercase uuids]}; answers {"items": [BoardRecordSheet...]} for each asked uuid the board
// holds whose kind addendum 3.4 lists (a table cell is M12's and left out), in the order asked;
// any other uuid is left out. "" for any other shape. A read: synchronous.
std::string kicadBoardItemRecords( std::string aArgsJson )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return "";

    pcb::json args = pcb::json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 1 || !args.contains( "uuids" )
            || !args["uuids"].is_array() || args["uuids"].empty() || args["uuids"].size() > 512 )
    {
        return "";
    }

    for( const pcb::json& u : args["uuids"] )
    {
        if( !u.is_string() || !ccIsLowerUuid( u.get<std::string>() ) )
            return "";
    }

    BOARD*                board = fr->GetBoard();
    const pcb::json       nets = pcb::netChoices( board );
    pcb::json             items = pcb::json::array();
    std::set<std::string> answered;

    for( const pcb::json& u : args["uuids"] )
    {
        const std::string id = u.get<std::string>();
        BOARD_ITEM*       item = ccResolve( board, id );

        if( !item || !answered.insert( id ).second )
            continue;

        const std::string kind = pcb::boardRecordKind( item );

        if( kind.empty() )
            continue;

        pcb::json sheet = pcb::recordSheet( board, item, kind, nets );

        if( sheet.is_object() )
            items.push_back( std::move( sheet ) );
    }

    pcb::json out = pcb::json::object();
    out["items"] = std::move( items );
    return out.dump( -1, ' ', false, pcb::json::error_handler_t::replace );
}


// board.item.set, the record form (cap boardItemKinds): aArgsJson exactly {"run": int >= 0,
// "dir": "/tmp/cc-boarditems/<n>", "uuids": [1 to 4,096], "kind", "set"}, every uuid an item of
// that kind, the set read whole first (a pad's "net" a name the board holds or ""; a group's,
// target's or tuning pattern's keys of addendum 3.5). False (nothing queued) for anything else.
// True once ONE BOARD_COMMIT "Edit" is queued on the apply coroutine, every item re-resolved
// there and changed through its kind's own apply (the item table's for FP 7.2's kinds); a kind
// refusing its share reverts the whole commit. Record <dir>/items.json {run, applied, depth}.
bool kicadBoardItemRecordsSet( std::string aArgsJson )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return false;

    pcb::RECORDS_SET parsed;

    if( !pcb::readRecordsSet( fr, aArgsJson, parsed ) )
        return false;

    ccEnsureDir( parsed.dir );

    pcbjam_collab::runOnCoroutine( fr, [fr, parsed]() {   // re-resolve on the coroutine (S4)
        BOARD_COMMIT commit( fr );
        bool         applied = true;

        for( const std::string& id : parsed.uuids )
        {
            BOARD_ITEM* live = ccResolve( fr->GetBoard(), id );

            if( !live || pcb::boardRecordKind( live ) != parsed.kind
                    || !pcb::applyRecordsSet( fr, live, parsed, commit ) )
            {
                applied = false;
                break;
            }
        }

        if( applied )
            commit.Push( _( "Edit" ) );
        else
            commit.Revert();

        pcb::json record = pcb::json::object();
        record["run"] = parsed.run;
        record["applied"] = applied;
        record["depth"] = fr->GetUndoCommandCount();
        ccWriteJson( parsed.dir, "items.json", record );
    } );

    return true;
}
#endif // KICAD_MERGED_EMBIND


// board.tuning.get (cap boardTuning): the tuning tool's live settings (addendum 3.5's
// TuningSettings), or "" when no tuning tool runs, no board frame or an open in flight. A read.
std::string kicadBoardTuningGet()
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return "";

    return CcTuningLiveGet();
}


// board.tuning.set (cap boardTuning): aArgsJson exactly {"run": int >= 0, "dir":
// "/tmp/cc-boarditems/<n>", "set": Partial<TuningSettings> less mode and fromRules}, read whole.
// False (nothing handed over) for any other shape or when no tuning tool runs. True once the
// set is handed to the tuning tool, whose own loop applies it as its dialog's OK (the pattern
// being drawn is not committed: no undo step) and then writes <dir>/tuning.json
// {run, settings}.
bool kicadBoardTuningSet( std::string aArgsJson )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return false;

    pcb::json args = pcb::json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 3 || !args.contains( "run" )
            || !args.contains( "dir" ) || !args.contains( "set" ) )
    {
        return false;
    }

    if( !args["run"].is_number_integer() || args["run"].get<long long>() < 0
            || !args["dir"].is_string() || !CcTuningSetValid( args["set"] ) )
    {
        return false;
    }

    const long long   run = args["run"].get<long long>();
    const std::string dir = args["dir"].get<std::string>();

    if( !ccIsRunDir( dir, pcb::BOARD_ITEMS_ROOT ) )
        return false;

    return CcTuningLiveSet( args["set"],
                            [run, dir]( const pcb::json& aSettings )
                            {
                                ccEnsureDir( dir );

                                pcb::json record = pcb::json::object();
                                record["run"] = run;
                                record["settings"] = aSettings;
                                ccWriteJson( dir, "tuning.json", record );
                            } );
}


// board.image.place (cap boardImagePlace): aArgsJson exactly {"path": "/tmp/cc-stage/<n>/<file>"},
// a PNG or JPEG KiCad reads. False when the file does not read as a picture, another tool runs,
// or for any other shape. True once KiCad's own reference image tool is queued with the picture
// as its parameter (the file box skipped): the picture hangs on the pointer on the active layer
// and KiCad's commit "Place Image" lands on the press.
bool kicadBoardPlaceImage( std::string aArgsJson )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() || !fr->ToolStackIsEmpty() )
        return false;

    pcb::json args = pcb::json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 1 || !args.contains( "path" )
            || !args["path"].is_string() )
    {
        return false;
    }

    const std::string path = args["path"].get<std::string>();
    const size_t      slash = path.rfind( '/' );

    if( slash == std::string::npos || slash + 1 >= path.size()
            || !ccIsRunDir( path.substr( 0, slash ), "/tmp/cc-stage/" ) )
    {
        return false;
    }

    const wxString file = wxString::FromUTF8( path.c_str() );

    if( !wxFileExists( file ) )
        return false;

    // The tool's own read of the file, made here so a file that is no picture answers false.
    PCB_REFERENCE_IMAGE* image = new PCB_REFERENCE_IMAGE( fr->GetBoard(), VECTOR2I( 0, 0 ) );

    if( !image->GetReferenceImage().ReadImageFile( file ) )
    {
        delete image;
        return false;
    }

    pcbjam_collab::runOnCoroutine( fr, [fr, image]() {
        // As the tool's file box branch leaves a new picture: new, moving, on the active layer.
        image->SetFlags( IS_NEW | IS_MOVING );
        image->SetLayer( fr->GetActiveLayer() );
        fr->GetToolManager()->RunAction<PCB_REFERENCE_IMAGE*>( PCB_ACTIONS::placeReferenceImage,
                                                               image );
    } );

    return true;
}

} // namespace cc_e3

EMSCRIPTEN_BINDINGS( cc_e3_pcb )
{
    function( "kicadHostBoardItems", &cc_e3::kicadHostBoardItems );
#ifdef KICAD_MERGED_EMBIND
    function( "kicadBoardItemRecords", &cc_e3::kicadBoardItemRecords );
    function( "kicadBoardItemRecordsSet", &cc_e3::kicadBoardItemRecordsSet );
#endif
    function( "kicadBoardTuningGet", &cc_e3::kicadBoardTuningGet );
    function( "kicadBoardTuningSet", &cc_e3::kicadBoardTuningSet );
    function( "kicadBoardPlaceImage", &cc_e3::kicadBoardPlaceImage );
}
