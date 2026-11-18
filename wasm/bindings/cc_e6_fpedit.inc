// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e6, the footprint editor face: textually included by fpedit_embind.cpp (its own
// TU, merged image only, include subdir pcbnew, no KICAD_MERGED_EMBIND). It sees that TU's
// includes, fpedit_common.h once it lands, cc_fp_items.h and cc_output_common.h.
//
// Every definition of the lane lives in namespace cc_e6 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

#include <algorithm>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include <wx/arrstr.h>
#include <wx/filename.h>

#include <board_commit.h>
#include <fix_board_shape.h>
#include <footprint.h>
#include <frame_type.h>
#include <locale_io.h>
#include <pcb_base_edit_frame.h>
#include <pcb_draw_panel_gal.h>
#include <pcb_group.h>
#include <pcb_shape.h>
#include <pcbnew_settings.h>
#include <import_gfx/dxf_import_plugin.h>
#include <import_gfx/graphics_import_mgr.h>
#include <import_gfx/graphics_import_plugin.h>
#include <import_gfx/graphics_importer_pcbnew.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>
#include <tools/pcb_actions.h>
#include <tools/pcb_selection_tool.h>
#include <view/view_controls.h>

namespace cc_e6
{
namespace fp
{
using nlohmann::json;

/** The largest drawing file read (the page caps a chosen file at 25 MB). */
constexpr unsigned long long IMPORT_FILE_MAX = 25ULL * 1024 * 1024;

/** At most this many uuids in an answer; at most this many warning lines. */
constexpr size_t IMPORT_UUIDS_MAX = 4096;
constexpr size_t IMPORT_WARNINGS_MAX = 64;

/** kicadImportGraphics' closed arguments, read whole before anything is queued. */
struct IMPORT_ARGS
{
    wxString                    path;
    bool                        pointer = false;
    std::optional<PCB_LAYER_ID> layer;
    double                      scale = 1.0;
    VECTOR2D                    originMm{ 0.0, 0.0 };
    std::optional<double>       lineWidthMm;
    DXF_IMPORT_UNITS            dxfUnits = DXF_IMPORT_UNITS::DEFAULT;
    bool                        group = false;
    bool                        fixDiscontinuities = false;
    double                      toleranceMm = 1.0;
};

/** A drawing file KiCad's importer may read: absolute, no "..", a regular file of at most 25 MB. */
inline bool importablePath( const wxString& aPath )
{
    if( aPath.IsEmpty() || !aPath.StartsWith( wxS( "/" ) ) || aPath.Contains( wxS( ".." ) ) )
        return false;

    wxFileName fn( aPath );

    if( !fn.FileExists() )
        return false;

    const wxULongLong size = fn.GetSize();

    if( size == wxInvalidSize )
        return false;

    const unsigned long long bytes = size.GetValue();

    return bytes > 0 && bytes <= IMPORT_FILE_MAX;
}

/** The DXF units by the page's word, or nothing. */
inline std::optional<DXF_IMPORT_UNITS> dxfUnitsOf( const json& aValue )
{
    if( !aValue.is_string() )
        return std::nullopt;

    const std::string& u = aValue.get_ref<const std::string&>();

    if( u == "mm" )    return DXF_IMPORT_UNITS::MM;
    if( u == "cm" )    return DXF_IMPORT_UNITS::CM;
    if( u == "inch" )  return DXF_IMPORT_UNITS::INCH;
    if( u == "mil" )   return DXF_IMPORT_UNITS::MILS;
    if( u == "feet" )  return DXF_IMPORT_UNITS::FEET;

    return std::nullopt;
}

inline std::optional<IMPORT_ARGS> readImportArgs( const std::string& aArgsJson )
{
    using namespace cc_fpedit;

    const json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded()
            || !onlyKeys( args, { "path", "layer", "scale", "origin", "lineWidth", "dxfUnits",
                                  "group", "fixDiscontinuities", "tolerance", "place" } )
            || !hasKeys( args, { "path", "place" } ) )
    {
        return std::nullopt;
    }

    IMPORT_ARGS a;

    std::optional<wxString> path = textOf( args["path"], 2, 1024 );

    if( !path || !importablePath( *path ) )
        return std::nullopt;

    a.path = *path;

    if( !args["place"].is_string() )
        return std::nullopt;

    const std::string& place = args["place"].get_ref<const std::string&>();

    if( place != "at" && place != "pointer" )
        return std::nullopt;

    a.pointer = place == "pointer";

    if( args.contains( "layer" ) )
    {
        a.layer = layerOf( args["layer"] );

        if( !a.layer )
            return std::nullopt;
    }

    if( args.contains( "scale" ) )
    {
        std::optional<double> scale = numberOf( args["scale"], 1e-6, 1e6 );

        if( !scale || *scale <= 0.0 )
            return std::nullopt;

        a.scale = *scale;
    }

    if( args.contains( "origin" ) )
    {
        const json& o = args["origin"];

        if( !o.is_array() || o.size() != 2 )
            return std::nullopt;

        std::optional<double> x = numberOf( o[0], -1e5, 1e5 );
        std::optional<double> y = numberOf( o[1], -1e5, 1e5 );

        if( !x || !y )
            return std::nullopt;

        a.originMm = VECTOR2D( *x, *y );
    }

    if( args.contains( "lineWidth" ) && !args["lineWidth"].is_null() )
    {
        a.lineWidthMm = numberOf( args["lineWidth"], 0.0, 100.0 );

        if( !a.lineWidthMm )
            return std::nullopt;
    }

    if( args.contains( "dxfUnits" ) )
    {
        std::optional<DXF_IMPORT_UNITS> units = dxfUnitsOf( args["dxfUnits"] );

        if( !units )
            return std::nullopt;

        a.dxfUnits = *units;
    }

    for( const char* key : { "group", "fixDiscontinuities" } )
    {
        if( !args.contains( key ) )
            continue;

        std::optional<bool> b = boolOf( args[key] );

        if( !b )
            return std::nullopt;

        ( std::string( key ) == "group" ? a.group : a.fixDiscontinuities ) = *b;
    }

    if( args.contains( "tolerance" ) )
    {
        std::optional<double> tol = numberOf( args["tolerance"], 0.0, 100.0 );

        if( !tol )
            return std::nullopt;

        a.toleranceMm = *tol;
    }

    return a;
}

/** KiCad's warnings of the import, one line each, empty lines dropped. */
inline json warningLines( const wxString& aMessages )
{
    json out = json::array();

    for( const wxString& line : wxSplit( aMessages, '\n', '\0' ) )
    {
        wxString t = line;
        t.Trim( true ).Trim( false );

        if( t.IsEmpty() )
            continue;

        if( out.size() >= IMPORT_WARNINGS_MAX )
            break;

        out.push_back( cc_fpedit::utf8( t ) );
    }

    return out;
}

/** The board editor or the footprint editor when it may be driven, else nullptr. */
inline PCB_BASE_EDIT_FRAME* readyBoardFrame()
{
    PCB_BASE_EDIT_FRAME* fr =
            wxTheApp ? dynamic_cast<PCB_BASE_EDIT_FRAME*>( wxTheApp->GetTopWindow() ) : nullptr;

    if( !fr || pcbjam_open::busy() || cc_fpedit::dialogUp() )
        return nullptr;

    const FRAME_T type = fr->GetFrameType();

    if( type != FRAME_PCB_EDITOR && type != FRAME_FOOTPRINT_EDITOR )
        return nullptr;

    if( !fr->GetBoard() || !fr->GetModel() )
        return nullptr;

    return fr;
}

/**
 * The items KiCad's importer made, held between the synchronous read and the queued commit:
 * deleted here unless the commit took them.
 */
struct STAGED
{
    std::vector<BOARD_ITEM*> items;
    bool                     taken = false;

    ~STAGED()
    {
        if( !taken )
        {
            for( BOARD_ITEM* item : items )
                delete item;
        }
    }
};

} // namespace fp

/**
 * kicadImportGraphics' board half, for the board editor and the footprint editor: the body of
 * DIALOG_IMPORT_GRAPHICS::TransferDataFromWindow (pcbnew/import_gfx/dialog_import_graphics.cpp
 * 174) on the page's arguments, then DRAWING_TOOL::PlaceImportedGraphics' placement (group,
 * joined gaps) as ONE commit "Import Graphics" at the given point, or staged on the pointer
 * through KiCad's move tool as PCB_CONTROL::placeBoardItems does (a click pushes it, Esc reverts
 * it). "" for any other shape of arguments, no ready frame or a file no plugin reads.
 */
std::string importGraphicsBoard( std::string aArgsJson )
{
    using fp::json;

    PCB_BASE_EDIT_FRAME* fr = fp::readyBoardFrame();

    if( !fr )
        return "";

    std::optional<fp::IMPORT_ARGS> parsed = fp::readImportArgs( aArgsJson );

    if( !parsed )
        return "";

    const fp::IMPORT_ARGS a = *parsed;

    if( a.layer && !fr->GetBoard()->IsLayerEnabled( *a.layer ) )
        return "";

    GRAPHICS_IMPORT_MGR                     mgr;
    std::unique_ptr<GRAPHICS_IMPORT_PLUGIN> plugin =
            mgr.GetPluginByExt( wxFileName( a.path ).GetExt() );

    if( !plugin )
        return "";

    BOARD_ITEM_CONTAINER* model = fr->GetModel();
    GRAPHICS_IMPORTER_PCBNEW importer( model );

    // The dialog's own reading of the place, scale and units.
    PCBNEW_SETTINGS* cfg = fr->GetPcbNewSettings();
    double           xscale = a.scale;
    double           yscale = a.scale;

    if( cfg && cfg->m_Display.m_DisplayInvertXAxis )
        xscale *= -1.0;

    if( cfg && cfg->m_Display.m_DisplayInvertYAxis )
        yscale *= -1.0;

    if( DXF_IMPORT_PLUGIN* dxf = dynamic_cast<DXF_IMPORT_PLUGIN*>( plugin.get() ) )
    {
        dxf->SetUnit( a.dxfUnits );

        if( a.lineWidthMm )
            importer.SetLineWidthMM( *a.lineWidthMm );
    }
    else
    {
        importer.SetLineWidthMM( 0.0 );
    }

    importer.SetPlugin( std::move( plugin ) );
    importer.SetLayer( a.layer ? *a.layer : fr->GetActiveLayer() );
    importer.SetImportOffsetMM( VECTOR2D( a.originMm.x / xscale, a.originMm.y / yscale ) );

    {
        LOCALE_IO dummy;    // Ensure floats can be read.

        if( importer.Load( a.path ) )
            importer.Import( VECTOR2D( a.scale, a.scale ) );
    }

    json out;
    out["warnings"] = fp::warningLines( importer.GetMessages() );

    auto staged = std::make_shared<fp::STAGED>();
    json uuids = json::array();

    for( std::unique_ptr<EDA_ITEM>& ptr : importer.GetItems() )
    {
        if( !ptr || !ptr->IsBOARD_ITEM() )
            continue;

        BOARD_ITEM* item = static_cast<BOARD_ITEM*>( ptr.release() );
        staged->items.push_back( item );

        if( uuids.size() < fp::IMPORT_UUIDS_MAX )
            uuids.push_back( cc_fpedit::utf8( item->m_Uuid.AsString() ) );
    }

    importer.ClearItems();

    out["items"] = staged->items.size();
    out["uuids"] = uuids;

    if( staged->items.empty() )
        return cc_fpedit::dumpJson( out );

    if( a.fixDiscontinuities )
    {
        std::vector<PCB_SHAPE*> shapes;

        for( BOARD_ITEM* item : staged->items )
        {
            if( PCB_SHAPE* shape = dynamic_cast<PCB_SHAPE*>( item ) )
                shapes.push_back( shape );
        }

        ConnectBoardShapes( shapes, pcbIUScale.mmToIU( a.toleranceMm ) );
    }

    const FRAME_T type = fr->GetFrameType();

    pcbjam_collab::runOnCoroutine( fr, [staged, a, model, type]() {
        PCB_BASE_EDIT_FRAME* frame = wxTheApp ? dynamic_cast<PCB_BASE_EDIT_FRAME*>(
                                                        wxTheApp->GetTopWindow() )
                                              : nullptr;

        // The frame or the footprint it edits changed since the read: nothing lands.
        if( !frame || frame->GetFrameType() != type || frame->GetModel() != model )
            return;

        TOOL_MANAGER*            tm = frame->GetToolManager();
        std::vector<BOARD_ITEM*> newItems;
        std::vector<BOARD_ITEM*> selectedItems;
        PCB_GROUP*               group = nullptr;

        tm->RunAction( ACTIONS::cancelInteractive );

        if( a.group && staged->items.size() >= 2 )
        {
            group = new PCB_GROUP( model );
            newItems.push_back( group );
            selectedItems.push_back( group );
        }

        for( BOARD_ITEM* item : staged->items )
        {
            newItems.push_back( item );

            if( group )
                group->AddItem( item );
            else
                selectedItems.push_back( item );
        }

        BOARD_COMMIT commit( frame );

        tm->RunAction( ACTIONS::selectionClear );

        EDA_ITEMS selItems( selectedItems.begin(), selectedItems.end() );
        tm->RunAction<EDA_ITEMS*>( ACTIONS::selectItems, &selItems );

        for( BOARD_ITEM* item : newItems )
            commit.Add( item );

        staged->taken = true;   // the commit owns them now: a revert deletes them

        if( !a.pointer )
        {
            commit.Push( _( "Import Graphics" ) );
            return;
        }

        // On the pointer: PCB_CONTROL::placeBoardItems' hand over to the move tool.
        KIGFX::VIEW_CONTROLS* vc = frame->GetCanvas()->GetViewControls();
        PCB_SELECTION&        selection = tm->GetTool<PCB_SELECTION_TOOL>()->GetSelection();

        if( selection.Empty() )
        {
            commit.Revert();
            return;
        }

        if( BOARD_ITEM* top = dynamic_cast<BOARD_ITEM*>( selection.GetTopLeftItem() ) )
            selection.SetReferencePoint( top->GetPosition() );

        vc->SetCursorPosition( vc->GetMousePosition(), false );
        tm->ProcessEvent( EVENTS::SelectedEvent );

        if( tm->RunSynchronousAction( PCB_ACTIONS::move, &commit ) )
            commit.Push( _( "Import Graphics" ) );
        else
            commit.Revert();
    } );

    return cc_fpedit::dumpJson( out );
}

} // namespace cc_e6

EMSCRIPTEN_BINDINGS( cc_e6_fpedit )
{
}
