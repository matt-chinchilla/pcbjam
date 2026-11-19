// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e4, the footprint editor face: textually included by fpedit_embind.cpp (its own
// TU, merged image only, include subdir pcbnew, no KICAD_MERGED_EMBIND). It sees that TU's
// includes, fpedit_common.h once it lands, cc_fp_items.h and cc_output_common.h.
//
// Every definition of the lane lives in namespace cc_e4 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.
//
// fp-e2 (FP spec 5.1, 7.1): kicadFpEditOpen, kicadFpEditState and kicadFpEditTake (the gate
// code of build G), then kicadFpEditNew, kicadFpEditMarkSaved, kicadFpEditSelection and
// kicadFpEditSelect. fp-e-items (FP spec 7.2): the item kind table and the four item exports.
// fp-e3: the pad kind (cc_item_pad.h), kicadFpEditPadDefaultsGet, kicadFpEditPadDefaultsSet
// and kicadFpEditEnumerate. fp-e4a: the text kinds field, text, textbox, table and barcode
// (cc_item_text.h), no export of their own (the four item exports serve them).

#include <map>
#include <set>
#include <memory>
#include <utility>
#include <vector>

#include <advanced_config.h>
#include <io/kicad/kicad_io_utils.h>
#include <ki_exception.h>
#include <lib_id.h>
#include <pad.h>
#include <pcb_field.h>
#include <pcb_draw_panel_gal.h>
#include <pcb_io/kicad_sexpr/pcb_io_kicad_sexpr.h>
#include <pcb_io/kicad_sexpr/pcb_io_kicad_sexpr_parser.h>
#include <pcbnew_settings.h>
#include <richio.h>
#include <tool/tool_manager.h>
#include <tools/pad_tool.h>
#include <tools/pcb_selection_tool.h>
#include <tool/actions.h>
#include <view/view_controls.h>
#include <dialogs/dialog_enum_pads.h>
#include <netinfo.h>

#include "cc_item_pad.h"
#include "cc_item_text.h"

namespace cc_e4
{

namespace fp
{

using namespace cc_fpedit;

/** What the page last opened: its token (echoed by the state), the door and the names it gave. */
struct SESSION
{
    std::string token;
    std::string mode = "library";
    std::string lib;
    std::string name;
};

SESSION& session()
{
    static SESSION s;
    return s;
}

const char* typeName( const FOOTPRINT* aFootprint )
{
    const int attrs = aFootprint ? aFootprint->GetAttributes() : 0;

    if( attrs & FP_THROUGH_HOLE )
        return "through_hole";

    if( attrs & FP_SMD )
        return "smd";

    return "unspecified";
}

/** FpEditState (FP spec 7.1). */
json stateJson( FOOTPRINT_EDIT_FRAME* aFrame )
{
    FOOTPRINT* footprint = currentFootprint( aFrame );
    bool       padEdit = false;

    if( aFrame && aFrame->GetToolManager() )
    {
        if( PAD_TOOL* padTool = aFrame->GetToolManager()->GetTool<PAD_TOOL>() )
            padEdit = padTool->InPadEditMode();
    }

    json out = json::object();
    out["token"] = session().token;
    out["open"] = footprint != nullptr;
    out["name"] = footprint ? utf8( footprint->GetFPID().GetUniStringLibItemName() ) : std::string();
    out["lib"] = footprint ? session().lib : std::string();
    out["mode"] = session().mode;
    out["modified"] = footprint && aFrame->GetScreen() && aFrame->GetScreen()->IsContentModified();
    out["depth"] = aFrame->GetUndoCommandCount();
    out["padCount"] = footprint ? static_cast<int>( footprint->Pads().size() ) : 0;
    out["type"] = typeName( footprint );
    out["padEditMode"] = padEdit;
    return out;
}

/**
 * One (footprint ...) read by KiCad's own s-expression parser (the reader FP_CACHE::Load runs
 * over a .kicad_mod file), owned; nullptr for text KiCad cannot read or that holds no footprint.
 */
std::unique_ptr<FOOTPRINT> readFootprint( const std::string& aText )
{
    try
    {
        STRING_LINE_READER        reader( aText, wxT( "fpedit" ) );
        PCB_IO_KICAD_SEXPR_PARSER parser( &reader, nullptr, nullptr );
        std::unique_ptr<BOARD_ITEM> item( parser.Parse() );

        if( FOOTPRINT* footprint = dynamic_cast<FOOTPRINT*>( item.get() ) )
        {
            item.release();
            return std::unique_ptr<FOOTPRINT>( footprint );
        }
    }
    catch( ... )
    {
    }

    return nullptr;
}

/**
 * The footprint as library text, by KiCad's own formatter, from a copy reset exactly as
 * FOOTPRINT_EDIT_FRAME::SaveFootprintInLibrary and PCB_IO_KICAD_SEXPR::FootprintSave reset
 * theirs before FP_CACHE::Save writes it (library id without nickname, every child's flags
 * cleared, angle 0, the front side, no parent, no group, no nets; CTL_FOR_LIBRARY, prettified
 * in the mode PRETTIFIED_FILE_OUTPUTFORMATTER takes). Every child keeps its uuid (a clone).
 */
std::string formatFootprint( FOOTPRINT_EDIT_FRAME* aFrame, const FOOTPRINT* aFootprint )
{
    try
    {
        std::unique_ptr<FOOTPRINT> copy( static_cast<FOOTPRINT*>( aFootprint->Clone() ) );

        copy->SetFPID( LIB_ID( wxEmptyString, aFootprint->GetFPID().GetLibItemName() ) );
        copy->RunOnChildren(
                []( BOARD_ITEM* child )
                {
                    child->ClearFlags();
                },
                RECURSE_MODE::RECURSE );

        copy->SetOrientation( ANGLE_0 );

        if( copy->GetLayer() != F_Cu )
        {
            PCBNEW_SETTINGS* cfg = aFrame ? aFrame->GetPcbNewSettings() : nullptr;
            copy->Flip( copy->GetPosition(),
                        cfg ? cfg->m_FlipDirection : FLIP_DIRECTION::TOP_BOTTOM );
        }

        copy->SetParent( nullptr );
        copy->SetParentGroup( nullptr );
        copy->ClearAllNets();

        PCB_IO_KICAD_SEXPR io( CTL_FOR_LIBRARY );
        STRING_FORMATTER   fmt;
        io.SetOutputFormatter( &fmt );
        io.Format( copy.get() );
        io.SetOutputFormatter( nullptr );

        std::string text = fmt.GetString();
        KICAD_FORMAT::Prettify( text, ADVANCED_CFG::GetCfg().m_CompactSave
                                              ? KICAD_FORMAT::FORMAT_MODE::COMPACT_TEXT_PROPERTIES
                                              : KICAD_FORMAT::FORMAT_MODE::NORMAL );
        return text;
    }
    catch( ... )
    {
        return std::string();
    }
}

/**
 * The open's and the new's tail, on the apply coroutine, the frame already cleared (Clear_Pcb
 * with no question) and aFootprint owned by no one: the body of
 * FOOTPRINT_EDIT_FRAME::LoadFootprintFromLibrary less the library load and the tree sync
 * (the footprint added at the origin, Reference and Value refilled
 * when empty unless placed, zoom to fit, unmodified, undo cleared). The footprint is added under
 * an empty nickname (KiCad's own editable case, so no read-only info bar is raised for a
 * nickname the frame's table does not hold) and takes aLib on the next turn of the event loop,
 * after ReloadFootprint's own CallAfter has read it.
 */
void installFootprint( FOOTPRINT_EDIT_FRAME* frame, FOOTPRINT* footprint, const std::string& aName,
                       const std::string& aLib, const std::string& aMode,
                       const std::string& aToken, bool aNew )
{
    const wxString name = wxString::FromUTF8( aName.c_str() );
    const wxString lib = wxString::FromUTF8( aLib.c_str() );

    footprint->SetFPID( LIB_ID( wxEmptyString, name ) );

    frame->GetCanvas()->GetViewControls()->SetCrossHairCursorPosition( VECTOR2D( 0, 0 ), false );
    frame->AddFootprintToBoard( footprint );

    if( aNew )
    {
        // FOOTPRINT_EDITOR_CONTROL::NewFootprint: nets and net classes for the new pads.
        frame->GetBoard()->BuildListOfNets();
        footprint->SetPosition( VECTOR2I( 0, 0 ) );
    }

    footprint->ClearFlags();

    if( aMode != "placed" )
    {
        if( footprint->Reference().GetText().IsEmpty() )
            footprint->SetReference( wxT( "Ref**" ) );

        if( footprint->Value().GetText().IsEmpty() )
            footprint->SetValue( wxT( "Val**" ) );
    }

    frame->Zoom_Automatique( false );
    frame->ClearUndoRedoList();
    frame->GetScreen()->SetContentModified( false );
    frame->UpdateView();
    frame->GetCanvas()->Refresh();

    if( !lib.IsEmpty() )
    {
        frame->CallAfter(
                [name, lib]()
                {
                    FOOTPRINT* fp = currentFootprint( fpEditFrame() );

                    if( fp && fp->GetFPID().GetUniStringLibItemName() == name
                            && fp->GetFPID().GetLibNickname().empty() )
                    {
                        fp->SetFPID( LIB_ID( lib, name ) );
                    }
                } );
    }

    session().token = aToken;
    session().mode = aMode;
    session().lib = aLib;
    session().name = aName;
}

/** Every item of the footprint by its uuid (lowercase), children of groups and pads included. */
std::map<std::string, BOARD_ITEM*> itemsByUuid( FOOTPRINT* aFootprint )
{
    std::map<std::string, BOARD_ITEM*> out;

    if( !aFootprint )
        return out;

    aFootprint->RunOnChildren(
            [&]( BOARD_ITEM* child )
            {
                if( child )
                    out[utf8( child->m_Uuid.AsString() )] = child;
            },
            RECURSE_MODE::RECURSE );

    return out;
}

} // namespace fp

// ── exports ───────────────────────────────────────────────────────────────────────────

// fpedit.open: {text, name, lib, mode: "library"|"placed"|"new", discard, token}, those six
// keys only. text ONE (footprint ...) of at most 8 MB read by KiCad's own parser; name 1 to
// 1,024 characters, lib 0 to 1,024; opened at the origin with no library row and no tree sync,
// undo cleared, unmodified. False (nothing queued) for any other shape, text KiCad cannot read,
// or a modified footprint open without discard.
bool kicadFpEditOpen( std::string aArgsJson )
{
    using namespace cc_fpedit;

    FOOTPRINT_EDIT_FRAME* fr = readyFrame();

    if( !fr )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded()
            || !onlyKeys( args, { "text", "name", "lib", "mode", "discard", "token" } )
            || !hasKeys( args, { "text", "name", "lib", "mode", "discard", "token" } ) )
    {
        return false;
    }

    std::optional<wxString>    name = textOf( args["name"], 1, TEXT_MAX );
    std::optional<wxString>    lib = textOf( args["lib"], 0, TEXT_MAX );
    std::optional<bool>        discard = boolOf( args["discard"] );
    std::optional<std::string> token = tokenOf( args["token"] );

    if( !args["text"].is_string() || !args["mode"].is_string() || !name || !lib || !discard
            || !token )
    {
        return false;
    }

    const std::string mode = args["mode"].get<std::string>();

    if( mode != "library" && mode != "placed" && mode != "new" )
        return false;

    const std::string& text = args["text"].get_ref<const std::string&>();

    if( text.empty() || text.size() > FOOTPRINT_TEXT_MAX )
        return false;

    if( !*discard && fr->IsContentModified() )
        return false;

    std::unique_ptr<FOOTPRINT> footprint = fp::readFootprint( text );

    if( !footprint )
        return false;

    auto held = std::make_shared<std::unique_ptr<FOOTPRINT>>( std::move( footprint ) );
    const std::string n = utf8( *name );
    const std::string l = utf8( *lib );
    const std::string t = *token;

    pcbjam_collab::runOnCoroutine( fr, [held, n, l, mode, t]() {
        FOOTPRINT_EDIT_FRAME* frame = cc_fpedit::fpEditFrame();   // re-resolved on the coroutine

        if( !frame || !held || !*held )
            return;

        frame->Clear_Pcb( false );
        fp::installFootprint( frame, held->release(), n, l, mode, t, /*aNew*/ false );
    } );

    return true;
}

// fpedit.state: FpEditState (FP spec 7.1); "" with no footprint editor frame. A read: answers
// while a load is parked too (the page polls depth for ev.edited).
std::string kicadFpEditState()
{
    FOOTPRINT_EDIT_FRAME* fr = cc_fpedit::fpEditFrame();

    if( !fr )
        return "";

    return cc_fpedit::dumpJson( fp::stateJson( fr ) );
}

// fpedit.take: the open footprint as library text by KiCad's own formatter, from a copy reset
// as SaveFootprintInLibrary resets its own (uuids and embedded files kept): {name, text}. A
// pure read; "" with nothing open, an open in flight or a dialog up.
std::string kicadFpEditTake()
{
    using namespace cc_fpedit;

    FOOTPRINT_EDIT_FRAME* fr = readyFrame();
    FOOTPRINT*            footprint = currentFootprint( fr );

    if( !footprint )
        return "";

    const std::string text = fp::formatFootprint( fr, footprint );

    if( text.empty() )
        return "";

    json out = json::object();
    out["name"] = utf8( footprint->GetFPID().GetUniStringLibItemName() );
    out["text"] = text;
    return dumpJson( out );
}

// fpedit.new: {name, lib, type: "smd"|"through_hole"|"unspecified", discard, token}, those five
// keys only: PCB_BASE_FRAME::CreateNewFootprint( name, "" ) (no library: the page keeps names
// unique), its type set, then installed as fpedit.open installs one (mode "new"). False
// (nothing queued) for any other shape or a modified footprint open without discard.
bool kicadFpEditNew( std::string aArgsJson )
{
    using namespace cc_fpedit;

    FOOTPRINT_EDIT_FRAME* fr = readyFrame();

    if( !fr )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !onlyKeys( args, { "name", "lib", "type", "discard", "token" } )
            || !hasKeys( args, { "name", "lib", "type", "discard", "token" } ) )
    {
        return false;
    }

    std::optional<wxString>    name = textOf( args["name"], 1, TEXT_MAX );
    std::optional<wxString>    lib = textOf( args["lib"], 0, TEXT_MAX );
    std::optional<bool>        discard = boolOf( args["discard"] );
    std::optional<std::string> token = tokenOf( args["token"] );

    if( !name || !lib || !discard || !token || !args["type"].is_string() )
        return false;

    const std::string type = args["type"].get<std::string>();
    int               attrs = 0;

    if( type == "smd" )
        attrs = FP_SMD;
    else if( type == "through_hole" )
        attrs = FP_THROUGH_HOLE;
    else if( type != "unspecified" )
        return false;

    if( !*discard && fr->IsContentModified() )
        return false;

    const std::string n = utf8( *name );
    const std::string l = utf8( *lib );
    const std::string t = *token;

    pcbjam_collab::runOnCoroutine( fr, [n, l, t, attrs]() {
        FOOTPRINT_EDIT_FRAME* frame = cc_fpedit::fpEditFrame();   // re-resolved on the coroutine

        if( !frame )
            return;

        // FOOTPRINT_EDITOR_CONTROL::NewFootprint's order: the frame cleared, then the footprint
        // made against the new board's design settings.
        frame->Clear_Pcb( false );

        FOOTPRINT* footprint =
                frame->CreateNewFootprint( wxString::FromUTF8( n.c_str() ), wxEmptyString );

        if( !footprint )
            return;

        footprint->SetAttributes( attrs );
        fp::installFootprint( frame, footprint, n, l, "new", t, /*aNew*/ true );
    } );

    return true;
}

// fpedit.markSaved: clears the modified flag when aToken is the state's (the page wrote what it
// took), as FOOTPRINT_EDIT_FRAME::ClearModify does after a save. No undo step.
bool kicadFpEditMarkSaved( std::string aToken )
{
    using namespace cc_fpedit;

    FOOTPRINT_EDIT_FRAME* fr = readyFrame();

    if( !fr || !currentFootprint( fr ) || fp::session().token.empty()
            || aToken != fp::session().token )
    {
        return false;
    }

    fr->ClearModify();
    fr->UpdateTitle();
    return true;
}

// fpedit selection: {uuids} of the selection tool's selection, in its order; "" with no
// footprint editor frame.
std::string kicadFpEditSelection()
{
    using namespace cc_fpedit;

    FOOTPRINT_EDIT_FRAME* fr = fpEditFrame();

    if( !fr || !fr->GetToolManager() )
        return "";

    PCB_SELECTION_TOOL* tool = fr->GetToolManager()->GetTool<PCB_SELECTION_TOOL>();
    json                uuids = json::array();

    if( tool )
    {
        for( EDA_ITEM* item : tool->GetSelection() )
        {
            if( item )
                uuids.push_back( utf8( item->m_Uuid.AsString() ) );
        }
    }

    json out = json::object();
    out["uuids"] = std::move( uuids );
    return dumpJson( out );
}

// select in an fpedit frame: {uuids: string[0..4096], zoom: bool}, those two keys only, every
// uuid an item of the open footprint; the selection replaced by them (an empty list clears
// it), the view fitted to them with zoom. No undo step.
bool kicadFpEditSelect( std::string aArgsJson )
{
    using namespace cc_fpedit;

    FOOTPRINT_EDIT_FRAME* fr = readyFrame();
    FOOTPRINT*            footprint = currentFootprint( fr );

    if( !footprint )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !onlyKeys( args, { "uuids", "zoom" } )
            || !hasKeys( args, { "uuids", "zoom" } ) )
    {
        return false;
    }

    std::optional<std::vector<std::string>> uuids = uuidsOf( args["uuids"], 0, UUIDS_MAX );
    std::optional<bool>                     zoom = boolOf( args["zoom"] );

    if( !uuids || !zoom )
        return false;

    std::map<std::string, BOARD_ITEM*> byUuid = fp::itemsByUuid( footprint );

    for( const std::string& u : *uuids )
    {
        if( !byUuid.count( u ) )
            return false;
    }

    const std::vector<std::string> ids = *uuids;
    const bool                     fit = *zoom;

    pcbjam_collab::runOnCoroutine( fr, [ids, fit]() {
        FOOTPRINT_EDIT_FRAME* frame = cc_fpedit::fpEditFrame();
        FOOTPRINT*            live = cc_fpedit::currentFootprint( frame );

        if( !live || !frame->GetToolManager() )
            return;

        PCB_SELECTION_TOOL* tool = frame->GetToolManager()->GetTool<PCB_SELECTION_TOOL>();

        if( !tool )
            return;

        std::map<std::string, BOARD_ITEM*> byId = fp::itemsByUuid( live );
        EDA_ITEMS                          picked;

        for( const std::string& u : ids )
        {
            auto it = byId.find( u );

            if( it != byId.end() )
                picked.push_back( it->second );
        }

        tool->ClearSelection();

        if( !picked.empty() )
        {
            tool->AddItemsToSel( &picked );

            if( fit )
                frame->GetToolManager()->RunAction( ACTIONS::zoomFitSelection );
        }

        frame->GetCanvas()->Refresh();
    } );

    return true;
}

} // namespace cc_e4

// ── fp-e-items: the item kind table (cc_fp_items.h's single definitions) ──────────────

namespace cc_fpitems
{

/** The table: a function local static, so a registrar never runs before it exists. */
static std::vector<CcItemKind>& kindTable()
{
    static std::vector<CcItemKind> table;
    return table;
}

void ccRegisterItemKind( const CcItemKind& aKind )
{
    if( !aKind.name || !aKind.get || !aKind.set || !aKind.add )
        return;

    for( const CcItemKind& k : kindTable() )
    {
        if( std::string( k.name ) == aKind.name )
            return;   // a kind registered twice keeps its first row
    }

    kindTable().push_back( aKind );
}

const CcItemKind* ccItemKind( const std::string& aName )
{
    for( const CcItemKind& k : kindTable() )
    {
        if( aName == k.name )
            return &k;
    }

    return nullptr;
}

const char* ccItemKindName( const BOARD_ITEM* aItem )
{
    if( !aItem )
        return nullptr;

    switch( aItem->Type() )
    {
    case PCB_PAD_T:             return "pad";
    case PCB_FIELD_T:           return "field";
    case PCB_TEXT_T:            return "text";
    case PCB_TEXTBOX_T:         return "textbox";
    case PCB_SHAPE_T:           return "shape";
    case PCB_DIM_ALIGNED_T:
    case PCB_DIM_LEADER_T:
    case PCB_DIM_CENTER_T:
    case PCB_DIM_RADIAL_T:
    case PCB_DIM_ORTHOGONAL_T:  return "dimension";
    case PCB_ZONE_T:            return "zone";
    case PCB_TABLE_T:           return "table";
    case PCB_BARCODE_T:         return "barcode";
    case PCB_REFERENCE_IMAGE_T: return "image";
    case PCB_POINT_T:           return "point";
    default:                    return nullptr;
    }
}

json ccFpItemJson( const BOARD_ITEM* aItem, const VECTOR2I& aOrigin, bool aBoardFrame )
{
    const char*       name = ccItemKindName( aItem );
    const CcItemKind* kind = name ? ccItemKind( name ) : nullptr;

    if( !kind )
        return json();

    json out = kind->get( const_cast<BOARD_ITEM*>( aItem ), aOrigin, aBoardFrame );

    if( !out.is_object() )
        return json();

    out["kind"] = name;
    out["uuid"] = std::string( aItem->m_Uuid.AsString().utf8_str() );
    return out;
}

bool ccFpItemApply( BOARD_ITEM* aItem, const json& aSet, BOARD_COMMIT& aCommit,
                    const VECTOR2I& aOrigin, bool aBoardFrame )
{
    const char*       name = ccItemKindName( aItem );
    const CcItemKind* kind = name ? ccItemKind( name ) : nullptr;

    if( !kind || !aSet.is_object() || aSet.empty() || aSet.contains( "kind" )
            || aSet.contains( "uuid" ) )
    {
        return false;
    }

    return kind->set( aItem, aSet, aCommit, aOrigin, aBoardFrame );
}

} // namespace cc_fpitems

namespace cc_e4
{

namespace items
{

using namespace cc_fpedit;

const char* const KIND_NAMES[] = { "pad", "field", "text", "textbox", "shape", "dimension",
                                   "zone", "table", "barcode", "image", "point" };

bool isKindName( const std::string& aName )
{
    for( const char* k : KIND_NAMES )
    {
        if( aName == k )
            return true;
    }

    return false;
}

/** The footprint's own items (KiCad's child order, no group, no cell) that have a kind row. */
std::vector<BOARD_ITEM*> topItems( FOOTPRINT* aFootprint )
{
    std::vector<BOARD_ITEM*> out;

    aFootprint->RunOnChildren(
            [&]( BOARD_ITEM* child )
            {
                const char* name = cc_fpitems::ccItemKindName( child );

                if( name && cc_fpitems::ccItemKind( name ) )
                    out.push_back( child );
            },
            RECURSE_MODE::NO_RECURSE );

    return out;
}

std::map<std::string, BOARD_ITEM*> topByUuid( FOOTPRINT* aFootprint )
{
    std::map<std::string, BOARD_ITEM*> out;

    for( BOARD_ITEM* item : topItems( aFootprint ) )
        out[utf8( item->m_Uuid.AsString() )] = item;

    return out;
}

/** The record less "kind" and "uuid" (what a kind's add reads). */
json bodyOf( const json& aRecord )
{
    json body = aRecord;
    body.erase( "kind" );
    body.erase( "uuid" );
    return body;
}

/** A new item of aRecord's kind made by its row, its uuid the page's; nullptr when refused. */
BOARD_ITEM* makeItem( FOOTPRINT* aFootprint, const json& aRecord, const VECTOR2I& aOrigin )
{
    const cc_fpitems::CcItemKind* kind =
            cc_fpitems::ccItemKind( aRecord["kind"].get<std::string>() );

    if( !kind )
        return nullptr;

    BOARD_ITEM* item = nullptr;

    try
    {
        item = kind->add( aFootprint, bodyOf( aRecord ), aOrigin, /*aBoardFrame*/ false );
    }
    catch( ... )
    {
        item = nullptr;
    }

    if( !item )
        return nullptr;

    const_cast<KIID&>( item->m_Uuid ) =
            KIID( wxString::FromUTF8( aRecord["uuid"].get<std::string>().c_str() ) );
    return item;
}

} // namespace items

// fpedit.items.get: {uuids?: string[1..4096], kinds?: FpItemKind[1..11], layers?:
// string[1..256]}, those keys only; {items} in KiCad's child order (fields, pads, zones,
// points, drawings), each record by its kind's row with kind and uuid. A listed uuid must be an
// item of the footprint; layers keeps an item on any listed layer. "" for any other shape or
// with nothing open.
std::string kicadFpEditItemsGet( std::string aArgsJson )
{
    using namespace cc_fpedit;

    FOOTPRINT_EDIT_FRAME* fr = readyFrame();
    FOOTPRINT*            footprint = currentFootprint( fr );

    if( !footprint )
        return "";

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !onlyKeys( args, { "uuids", "kinds", "layers" } ) )
        return "";

    std::optional<std::vector<std::string>> uuids;
    std::set<std::string>                   kinds;
    std::optional<LSET>                     layers;

    if( args.contains( "uuids" ) )
    {
        uuids = uuidsOf( args["uuids"], 1, UUIDS_MAX );

        if( !uuids )
            return "";
    }

    if( args.contains( "kinds" ) )
    {
        const json& k = args["kinds"];

        if( !k.is_array() || k.empty() || k.size() > 11 )
            return "";

        for( const json& n : k )
        {
            if( !n.is_string() || !items::isKindName( n.get<std::string>() )
                    || !kinds.insert( n.get<std::string>() ).second )
            {
                return "";
            }
        }
    }

    if( args.contains( "layers" ) )
    {
        layers = layersOf( args["layers"] );

        if( !layers )
            return "";
    }

    std::vector<BOARD_ITEM*> all = items::topItems( footprint );
    std::set<std::string>    wanted;

    if( uuids )
    {
        std::map<std::string, BOARD_ITEM*> byUuid = items::topByUuid( footprint );

        for( const std::string& u : *uuids )
        {
            if( !byUuid.count( u ) )
                return "";

            wanted.insert( u );
        }
    }

    const VECTOR2I origin = footprint->GetPosition();
    json           list = json::array();

    for( BOARD_ITEM* item : all )
    {
        if( uuids && !wanted.count( utf8( item->m_Uuid.AsString() ) ) )
            continue;

        if( !kinds.empty() && !kinds.count( cc_fpitems::ccItemKindName( item ) ) )
            continue;

        if( layers && ( item->GetLayerSet() & *layers ).none() )
            continue;

        json record = cc_fpitems::ccFpItemJson( item, origin, /*aBoardFrame*/ false );

        if( record.is_object() )
            list.push_back( std::move( record ) );
    }

    json out = json::object();
    out["items"] = std::move( list );
    return dumpJson( out );
}

// fpedit.items.set: {uuids: string[1..4096], set: object}, those two keys only; every uuid an
// item of the footprint and all of ONE kind; the patch is that kind's (only keys every item
// has). Checked before anything is queued by a dry run on a clone of the first item; on the
// coroutine every item takes it in ONE commit "Edit" (one undo step), all or nothing.
bool kicadFpEditItemsSet( std::string aArgsJson )
{
    using namespace cc_fpedit;

    FOOTPRINT_EDIT_FRAME* fr = readyFrame();
    FOOTPRINT*            footprint = currentFootprint( fr );

    if( !footprint )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !onlyKeys( args, { "uuids", "set" } )
            || !hasKeys( args, { "uuids", "set" } ) )
    {
        return false;
    }

    std::optional<std::vector<std::string>> uuids = uuidsOf( args["uuids"], 1, UUIDS_MAX );
    const json&                             patch = args["set"];

    if( !uuids || !patch.is_object() || patch.empty() || patch.contains( "kind" )
            || patch.contains( "uuid" ) )
    {
        return false;
    }

    std::map<std::string, BOARD_ITEM*> byUuid = items::topByUuid( footprint );
    std::string                        kindName;

    for( const std::string& u : *uuids )
    {
        auto it = byUuid.find( u );

        if( it == byUuid.end() )
            return false;

        const std::string k = cc_fpitems::ccItemKindName( it->second );

        if( !kindName.empty() && k != kindName )
            return false;

        kindName = k;
    }

    const VECTOR2I origin = footprint->GetPosition();

    // The dry run: the patch on a clone of the first item, staged on a commit that is never
    // pushed (in the footprint editor a child is its own undo level, so the commit copies the
    // clone alone, and frees the copy when it goes).
    {
        std::unique_ptr<BOARD_ITEM> probe(
                static_cast<BOARD_ITEM*>( byUuid[uuids->front()]->Clone() ) );
        BOARD_COMMIT scratch( fr );
        bool         ok = false;

        try
        {
            ok = cc_fpitems::ccFpItemApply( probe.get(), patch, scratch, origin, false );
        }
        catch( ... )
        {
            ok = false;
        }

        if( !ok )
            return false;
    }

    const std::vector<std::string> ids = *uuids;
    const json                     p = patch;

    return commitOnCoroutine( wxT( "Edit" ),
            [ids, p]( FOOTPRINT_EDIT_FRAME*, FOOTPRINT* aFootprint, BOARD_COMMIT& aCommit )
            {
                std::map<std::string, BOARD_ITEM*> live = items::topByUuid( aFootprint );
                const VECTOR2I                     at = aFootprint->GetPosition();

                for( const std::string& u : ids )
                {
                    auto it = live.find( u );

                    if( it == live.end() )
                        return false;

                    try
                    {
                        if( !cc_fpitems::ccFpItemApply( it->second, p, aCommit, at, false ) )
                            return false;
                    }
                    catch( ... )
                    {
                        return false;
                    }
                }

                return true;
            } );
}

// fpedit.items.add: {items: NewItem[1..512]}, that key only; each record {kind, uuid, ...the
// kind's keys}, the uuid page made, lowercase, new to the footprint and unique in the list.
// Every record is made once before anything is queued (and thrown away) to check it; on the
// coroutine the items are made again and added in ONE commit "Add" (one undo step).
bool kicadFpEditItemsAdd( std::string aArgsJson )
{
    using namespace cc_fpedit;

    FOOTPRINT_EDIT_FRAME* fr = readyFrame();
    FOOTPRINT*            footprint = currentFootprint( fr );

    if( !footprint )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !onlyKeys( args, { "items" } ) || !args.contains( "items" ) )
        return false;

    const json& list = args["items"];

    if( !list.is_array() || list.empty() || list.size() > NEW_ITEMS_MAX )
        return false;

    std::set<std::string> taken;

    footprint->RunOnChildren(
            [&]( BOARD_ITEM* child )
            {
                taken.insert( utf8( child->m_Uuid.AsString() ) );
            },
            RECURSE_MODE::RECURSE );

    taken.insert( utf8( footprint->m_Uuid.AsString() ) );

    const VECTOR2I origin = footprint->GetPosition();

    for( const json& record : list )
    {
        if( !record.is_object() || !record.contains( "kind" ) || !record.contains( "uuid" )
                || !record["kind"].is_string() || !record["uuid"].is_string()
                || !items::isKindName( record["kind"].get<std::string>() )
                || !isLowerUuid( record["uuid"].get<std::string>() )
                || !taken.insert( record["uuid"].get<std::string>() ).second )
        {
            return false;
        }

        std::unique_ptr<BOARD_ITEM> probe( items::makeItem( footprint, record, origin ) );

        if( !probe )
            return false;
    }

    const json records = list;

    return commitOnCoroutine( wxT( "Add" ),
            [records]( FOOTPRINT_EDIT_FRAME*, FOOTPRINT* aFootprint, BOARD_COMMIT& aCommit )
            {
                const VECTOR2I           at = aFootprint->GetPosition();
                std::vector<BOARD_ITEM*> made;

                for( const json& record : records )
                {
                    BOARD_ITEM* item = items::makeItem( aFootprint, record, at );

                    if( !item )
                    {
                        for( BOARD_ITEM* m : made )
                            delete m;

                        return false;
                    }

                    made.push_back( item );
                }

                for( BOARD_ITEM* item : made )
                    aCommit.Add( item );

                return true;
            } );
}

// fpedit.items.remove: {uuids: string[1..4096]}, that key only; every uuid an item of the
// footprint with a kind, never the Reference or Value field; ONE commit "Delete".
bool kicadFpEditItemsRemove( std::string aArgsJson )
{
    using namespace cc_fpedit;

    FOOTPRINT_EDIT_FRAME* fr = readyFrame();
    FOOTPRINT*            footprint = currentFootprint( fr );

    if( !footprint )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !onlyKeys( args, { "uuids" } ) || !args.contains( "uuids" ) )
        return false;

    std::optional<std::vector<std::string>> uuids = uuidsOf( args["uuids"], 1, UUIDS_MAX );

    if( !uuids )
        return false;

    std::map<std::string, BOARD_ITEM*> byUuid = items::topByUuid( footprint );

    for( const std::string& u : *uuids )
    {
        auto it = byUuid.find( u );

        if( it == byUuid.end() )
            return false;

        if( it->second->Type() == PCB_FIELD_T )
        {
            const PCB_FIELD* field = static_cast<const PCB_FIELD*>( it->second );

            if( field->IsReference() || field->IsValue() )
                return false;
        }
    }

    const std::vector<std::string> ids = *uuids;

    return commitOnCoroutine( wxT( "Delete" ),
            [ids]( FOOTPRINT_EDIT_FRAME* aFrame, FOOTPRINT* aFootprint, BOARD_COMMIT& aCommit )
            {
                std::map<std::string, BOARD_ITEM*> live = items::topByUuid( aFootprint );

                if( aFrame->GetToolManager() )
                {
                    if( PCB_SELECTION_TOOL* tool =
                                aFrame->GetToolManager()->GetTool<PCB_SELECTION_TOOL>() )
                    {
                        tool->ClearSelection();
                    }
                }

                for( const std::string& u : ids )
                {
                    auto it = live.find( u );

                    if( it == live.end() )
                        return false;

                    aCommit.Remove( it->second );
                }

                return true;
            } );
}


// ── fp-e3: the pad kind, the master pad, the numbering loop ──────────────────────────

/** The pad row of the item kind table (registered once, here in the fpedit TU). */
const bool kPadKindRegistered =
        ( cc_fpitems::ccRegisterItemKind( { "pad", &padkind::kindGet, &padkind::kindSet,
                                             &padkind::kindAdd } ),
          true );

// ── fp-e4a: the text kinds (no export: the item exports dispatch to them) ─────────────

const bool kTextKindsRegistered =
        ( cc_fpitems::ccRegisterItemKind( { "field", &textkind::kindGet, &textkind::kindSet,
                                             &textkind::addField } ),
          cc_fpitems::ccRegisterItemKind( { "text", &textkind::kindGet, &textkind::kindSet,
                                             &textkind::addText } ),
          cc_fpitems::ccRegisterItemKind( { "textbox", &textkind::kindGet, &textkind::kindSet,
                                             &textkind::addTextbox } ),
          cc_fpitems::ccRegisterItemKind( { "table", &textkind::kindGet, &textkind::kindSet,
                                             &textkind::addTable } ),
          cc_fpitems::ccRegisterItemKind( { "barcode", &textkind::kindGet, &textkind::kindSet,
                                             &textkind::addBarcode } ),
          true );

// fpedit.padDefaults.get: the master pad KiCad gives every new pad (BOARD_DESIGN_SETTINGS::
// m_Pad_Master, what DefaultPadProperties edits), as a pad record with no kind and no uuid,
// positions from 0, 0. "" with no ready frame.
std::string kicadFpEditPadDefaultsGet()
{
    using namespace cc_fpedit;

    FOOTPRINT_EDIT_FRAME* fr = readyFrame();

    if( !fr || !fr->GetBoard() )
        return "";

    const PAD* master = fr->GetDesignSettings().m_Pad_Master.get();

    if( !master )
        return "";

    return dumpJson( padkind::padJson( master, VECTOR2I( 0, 0 ) ) );
}

// fpedit.padDefaults.set: {set: <pad keys>}, that key only; the patch onto the master pad by
// the pad dialog's rules (checked on a copy first, refused where the dialog refuses). Not an
// undo step and not a modification of the footprint (KiCad's Default Pad Properties is
// neither); the next pad the pad tool places takes it. A number given becomes the pad tool's
// last number, as the dialog does.
bool kicadFpEditPadDefaultsSet( std::string aArgsJson )
{
    using namespace cc_fpedit;

    FOOTPRINT_EDIT_FRAME* fr = readyFrame();

    if( !fr || !fr->GetBoard() )
        return false;

    PAD* master = fr->GetDesignSettings().m_Pad_Master.get();

    if( !master )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !onlyKeys( args, { "set" } ) || !args.contains( "set" )
            || !args["set"].is_object() || args["set"].empty() )
    {
        return false;
    }

    padkind::PAD_PATCH patch;

    if( !padkind::readPatch( args["set"], patch )
            || !padkind::patchTakes( master, patch, VECTOR2I( 0, 0 ) ) )
    {
        return false;
    }

    if( !padkind::applyPatch( master, patch, VECTOR2I( 0, 0 ) ) )
        return false;

    // The master pad is a pattern: no net (TransferDataFromWindow).
    master->SetNetCode( NETINFO_LIST::UNCONNECTED );

    if( patch.number && fr->GetToolManager() )
    {
        if( PAD_TOOL* padTool = fr->GetToolManager()->GetTool<PAD_TOOL>() )
            padTool->SetLastPadNumber( master->GetNumber() );
    }

    return true;
}

// fpedit.enumerate: {start: 0..999, step: 0..999, prefix: string[0..64]}, those three keys
// exactly (the dialog's spin ranges and its prefix box); starts KiCad's own click in order
// numbering loop with them through PAD_TOOL::enumerateLoop (fp-e1b's split of EnumeratePads):
// no dialog, the reader clicks or drags across pads, Esc ends it, KiCad commits it as ONE undo
// step "Renumber Pads". False when the loop cannot start (no pads, pad edit mode) or for any
// other shape. The loop waits on tool events (a coroutine wait), so it is queued on the apply
// coroutine, as pcbShow3DViewer runs its tool, and the call answers at once: run from the
// call's own stack the first wait threw SuspendError (fix e4-1). The checks EnumeratePads makes
// before its loop are made here first, so true means the loop starts.
bool kicadFpEditEnumerate( std::string aArgsJson )
{
    using namespace cc_fpedit;

    FOOTPRINT_EDIT_FRAME* fr = readyFrame();
    FOOTPRINT*            footprint = currentFootprint( fr );

    if( !footprint || !fr->GetToolManager() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !onlyKeys( args, { "start", "step", "prefix" } )
            || !hasKeys( args, { "start", "step", "prefix" } ) )
    {
        return false;
    }

    std::optional<int>      start = intOf( args["start"], 0, 999 );
    std::optional<int>      step = intOf( args["step"], 0, 999 );
    std::optional<wxString> prefix = textOf( args["prefix"], 0, 64 );

    if( !start || !step || !prefix )
        return false;

    PAD_TOOL* padTool = fr->GetToolManager()->GetTool<PAD_TOOL>();

    if( !padTool || padTool->InPadEditMode() || footprint->Pads().empty() )
        return false;

    SEQUENTIAL_PAD_ENUMERATION_PARAMS params;
    params.m_start_number = *start;
    params.m_step = *step;
    params.m_prefix = *prefix;   // the dialog always sets it, empty included

    pcbjam_collab::runOnCoroutine( fr, [params]() {
        FOOTPRINT_EDIT_FRAME* frame = cc_fpedit::fpEditFrame();   // re-resolved on the coroutine

        if( !frame || !frame->GetToolManager() )
            return;

        if( PAD_TOOL* tool = frame->GetToolManager()->GetTool<PAD_TOOL>() )
            tool->enumerateLoop( params );
    } );

    return true;
}

} // namespace cc_e4

EMSCRIPTEN_BINDINGS( cc_e4_fpedit )
{
    function( "kicadFpEditOpen", &cc_e4::kicadFpEditOpen );
    function( "kicadFpEditState", &cc_e4::kicadFpEditState );
    function( "kicadFpEditTake", &cc_e4::kicadFpEditTake );
    function( "kicadFpEditNew", &cc_e4::kicadFpEditNew );
    function( "kicadFpEditMarkSaved", &cc_e4::kicadFpEditMarkSaved );
    function( "kicadFpEditSelection", &cc_e4::kicadFpEditSelection );
    function( "kicadFpEditSelect", &cc_e4::kicadFpEditSelect );
    function( "kicadFpEditItemsGet", &cc_e4::kicadFpEditItemsGet );
    function( "kicadFpEditItemsSet", &cc_e4::kicadFpEditItemsSet );
    function( "kicadFpEditItemsAdd", &cc_e4::kicadFpEditItemsAdd );
    function( "kicadFpEditItemsRemove", &cc_e4::kicadFpEditItemsRemove );
    function( "kicadFpEditPadDefaultsGet", &cc_e4::kicadFpEditPadDefaultsGet );
    function( "kicadFpEditPadDefaultsSet", &cc_e4::kicadFpEditPadDefaultsSet );
    function( "kicadFpEditEnumerate", &cc_e4::kicadFpEditEnumerate );
}
