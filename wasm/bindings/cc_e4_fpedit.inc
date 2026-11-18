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
// kicadFpEditSelect.

#include <memory>
#include <utility>

#include <advanced_config.h>
#include <io/kicad/kicad_io_utils.h>
#include <ki_exception.h>
#include <lib_id.h>
#include <pad.h>
#include <pcb_draw_panel_gal.h>
#include <pcb_io/kicad_sexpr/pcb_io_kicad_sexpr.h>
#include <pcb_io/kicad_sexpr/pcb_io_kicad_sexpr_parser.h>
#include <pcbnew_settings.h>
#include <richio.h>
#include <tool/tool_manager.h>
#include <tools/pad_tool.h>
#include <view/view_controls.h>

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
 * The open's and the new's tail, on the apply coroutine: the body of
 * FOOTPRINT_EDIT_FRAME::LoadFootprintFromLibrary less the library load and the tree sync
 * (Clear_Pcb with no question, the footprint added at the origin, Reference and Value refilled
 * when empty unless placed, zoom to fit, unmodified, undo cleared). The footprint is added under
 * an empty nickname (KiCad's own editable case, so no read-only info bar is raised for a
 * nickname the frame's table does not hold) and takes aLib on the next turn of the event loop,
 * after ReloadFootprint's own CallAfter has read it.
 */
void installFootprint( std::shared_ptr<std::unique_ptr<FOOTPRINT>> aHeld, const std::string& aName,
                       const std::string& aLib, const std::string& aMode,
                       const std::string& aToken, bool aNew )
{
    FOOTPRINT_EDIT_FRAME* frame = fpEditFrame();   // re-resolved on the coroutine

    if( !frame || !aHeld || !*aHeld )
        return;

    const wxString name = wxString::FromUTF8( aName.c_str() );
    const wxString lib = wxString::FromUTF8( aLib.c_str() );

    frame->Clear_Pcb( false );

    FOOTPRINT* footprint = aHeld->release();
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
        fp::installFootprint( held, n, l, mode, t, /*aNew*/ false );
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

} // namespace cc_e4

EMSCRIPTEN_BINDINGS( cc_e4_fpedit )
{
    function( "kicadFpEditOpen", &cc_e4::kicadFpEditOpen );
    function( "kicadFpEditState", &cc_e4::kicadFpEditState );
    function( "kicadFpEditTake", &cc_e4::kicadFpEditTake );
}
