// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e6, the schematic face: textually included by eeschema_embind.cpp after
// cc_rules_sch.inc, outside any KICAD_MERGED_EMBIND guard (include subdir eeschema; the
// merged image compiles that TU with -DKICAD_MERGED_EMBIND). It sees the TU's includes and
// helpers (schFrame(), the cc_output namespace) and calls them without editing them.
//
// Every definition of the lane lives in namespace cc_e6 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

#include <cmath>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <wx/dialog.h>
#include <wx/arrstr.h>
#include <wx/filename.h>
#include <wx/toplevel.h>

#include <frame_type.h>
#include <lib_symbol.h>
#include <locale_io.h>
#include <sch_base_frame.h>
#include <sch_commit.h>
#include <sch_draw_panel.h>
#include <sch_edit_frame.h>
#include <sch_line.h>
#include <sch_screen.h>
#include <symbol_edit_frame.h>
#include <import_gfx/dxf_import_plugin.h>
#include <import_gfx/graphics_import_mgr.h>
#include <import_gfx/graphics_import_plugin.h>
#include <import_gfx/graphics_importer_lib_symbol.h>
#include <import_gfx/graphics_importer_sch.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>
#include <tools/sch_actions.h>
#include <tools/sch_selection_tool.h>
#include <view/view.h>
#include <view/view_controls.h>

namespace cc_e6
{
namespace sch
{
using nlohmann::json;

constexpr unsigned long long IMPORT_FILE_MAX = 25ULL * 1024 * 1024;
constexpr size_t      IMPORT_UUIDS_MAX = 4096;
constexpr size_t      IMPORT_WARNINGS_MAX = 64;

/**
 * kicadImportGraphics' closed arguments as the schematic and symbol editors read them: the
 * board keys (layer, group, fixDiscontinuities, tolerance) are accepted and ignored, as KiCad's
 * schematic dialog has none of them.
 */
struct IMPORT_ARGS
{
    wxString              path;
    bool                  pointer = false;
    double                scale = 1.0;
    VECTOR2D              originMm{ 0.0, 0.0 };
    std::optional<double> lineWidthMm;
    DXF_IMPORT_UNITS      dxfUnits = DXF_IMPORT_UNITS::DEFAULT;
};

inline std::optional<double> numberIn( const json& aValue, double aMin, double aMax )
{
    if( !aValue.is_number() )
        return std::nullopt;

    const double v = aValue.get<double>();

    if( !std::isfinite( v ) || v < aMin || v > aMax )
        return std::nullopt;

    return v;
}

inline bool importablePath( const std::string& aRaw, wxString& aPath )
{
    if( aRaw.size() < 2 || aRaw.size() > 4096 || aRaw.find( '\0' ) != std::string::npos )
        return false;

    aPath = wxString::FromUTF8( aRaw.c_str(), aRaw.size() );

    if( aPath.IsEmpty() || !aPath.StartsWith( wxS( "/" ) ) || aPath.Contains( wxS( ".." ) ) )
        return false;

    for( wxUniChar c : aPath )
    {
        if( c < 0x20 || c == 0x7f )
            return false;
    }

    wxFileName fn( aPath );

    if( !fn.FileExists() )
        return false;

    const wxULongLong size = fn.GetSize();

    if( size == wxInvalidSize )
        return false;

    const unsigned long long bytes = size.GetValue();

    return bytes > 0 && bytes <= IMPORT_FILE_MAX;
}

inline std::optional<IMPORT_ARGS> readImportArgs( const std::string& aArgsJson )
{
    const json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || !args.contains( "path" )
            || !args.contains( "place" ) )
    {
        return std::nullopt;
    }

    static const std::vector<std::string> keys = { "path", "layer", "scale", "origin",
                                                   "lineWidth", "dxfUnits", "group",
                                                   "fixDiscontinuities", "tolerance", "place" };

    for( auto it = args.begin(); it != args.end(); ++it )
    {
        if( std::find( keys.begin(), keys.end(), it.key() ) == keys.end() )
            return std::nullopt;
    }

    IMPORT_ARGS a;

    if( !args["path"].is_string()
            || !importablePath( args["path"].get_ref<const std::string&>(), a.path ) )
    {
        return std::nullopt;
    }

    if( !args["place"].is_string() )
        return std::nullopt;

    const std::string& place = args["place"].get_ref<const std::string&>();

    if( place != "at" && place != "pointer" )
        return std::nullopt;

    a.pointer = place == "pointer";

    // The board keys keep their shapes so one argument set serves every frame.
    if( args.contains( "layer" ) && !args["layer"].is_string() )
        return std::nullopt;

    for( const char* key : { "group", "fixDiscontinuities" } )
    {
        if( args.contains( key ) && !args[key].is_boolean() )
            return std::nullopt;
    }

    if( args.contains( "tolerance" ) && !numberIn( args["tolerance"], 0.0, 100.0 ) )
        return std::nullopt;

    if( args.contains( "scale" ) )
    {
        std::optional<double> scale = numberIn( args["scale"], 1e-6, 1e6 );

        if( !scale || *scale <= 0.0 )
            return std::nullopt;

        a.scale = *scale;
    }

    if( args.contains( "origin" ) )
    {
        const json& o = args["origin"];

        if( !o.is_array() || o.size() != 2 )
            return std::nullopt;

        std::optional<double> x = numberIn( o[0], -1e5, 1e5 );
        std::optional<double> y = numberIn( o[1], -1e5, 1e5 );

        if( !x || !y )
            return std::nullopt;

        a.originMm = VECTOR2D( *x, *y );
    }

    if( args.contains( "lineWidth" ) && !args["lineWidth"].is_null() )
    {
        a.lineWidthMm = numberIn( args["lineWidth"], 0.0, 100.0 );

        if( !a.lineWidthMm )
            return std::nullopt;
    }

    if( args.contains( "dxfUnits" ) )
    {
        if( !args["dxfUnits"].is_string() )
            return std::nullopt;

        const std::string& u = args["dxfUnits"].get_ref<const std::string&>();

        if( u == "mm" )         a.dxfUnits = DXF_IMPORT_UNITS::MM;
        else if( u == "cm" )    a.dxfUnits = DXF_IMPORT_UNITS::CM;
        else if( u == "inch" )  a.dxfUnits = DXF_IMPORT_UNITS::INCH;
        else if( u == "mil" )   a.dxfUnits = DXF_IMPORT_UNITS::MILS;
        else if( u == "feet" )  a.dxfUnits = DXF_IMPORT_UNITS::FEET;
        else                    return std::nullopt;
    }

    return a;
}

inline std::string utf8( const wxString& aText )
{
    const wxScopedCharBuffer buf = aText.utf8_str();
    return std::string( buf.data(), buf.length() );
}

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

        out.push_back( utf8( t ) );
    }

    return out;
}

/** A shown dialog of any kind (KiCad's or wx's): the frame is not to be driven meanwhile. */
inline bool dialogUp()
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

/** The schematic editor or the symbol editor when it may be driven, else nullptr. */
inline SCH_BASE_FRAME* readySchFrame()
{
    SCH_BASE_FRAME* fr =
            wxTheApp ? dynamic_cast<SCH_BASE_FRAME*>( wxTheApp->GetTopWindow() ) : nullptr;

    if( !fr || pcbjam_open::busy() || dialogUp() )
        return nullptr;

    const FRAME_T type = fr->GetFrameType();

    if( type == FRAME_SCH )
        return fr->GetScreen() ? fr : nullptr;

    if( type == FRAME_SCH_SYMBOL_EDITOR )
        return static_cast<SYMBOL_EDIT_FRAME*>( fr )->GetCurSymbol() ? fr : nullptr;

    return nullptr;
}

/** The importer's items between the read and the queued commit, deleted unless taken. */
struct STAGED
{
    std::vector<SCH_ITEM*> items;
    bool                   taken = false;

    ~STAGED()
    {
        if( !taken )
        {
            for( SCH_ITEM* item : items )
                delete item;
        }
    }
};

} // namespace sch

/**
 * kicadImportGraphics' schematic half, for the schematic editor (GRAPHICS_IMPORTER_SCH) and the
 * symbol editor (GRAPHICS_IMPORTER_LIB_SYMBOL on the symbol and unit it edits): the body of
 * DIALOG_IMPORT_GFX_SCH::TransferDataFromWindow (eeschema/import_gfx/dialog_import_gfx_sch.cpp
 * 175), then SCH_DRAWING_TOOLS::ImportGraphics' or SYMBOL_EDITOR_DRAWING_TOOLS::ImportGraphics'
 * placement as ONE commit "Import Graphic" at the given point, or staged on the pointer through
 * KiCad's move tool as each editor's Paste does (a click pushes it, Esc reverts it). "" for any
 * other shape of arguments, no ready frame or a file no plugin reads.
 */
std::string importGraphicsSch( std::string aArgsJson )
{
    using sch::json;

    SCH_BASE_FRAME* fr = sch::readySchFrame();

    if( !fr )
        return "";

    std::optional<sch::IMPORT_ARGS> parsed = sch::readImportArgs( aArgsJson );

    if( !parsed )
        return "";

    const sch::IMPORT_ARGS a = *parsed;
    const FRAME_T          type = fr->GetFrameType();

    GRAPHICS_IMPORT_MGR                     mgr;
    std::unique_ptr<GRAPHICS_IMPORT_PLUGIN> plugin =
            mgr.GetPluginByExt( wxFileName( a.path ).GetExt() );

    if( !plugin )
        return "";

    LIB_SYMBOL*                        symbol = nullptr;
    std::unique_ptr<GRAPHICS_IMPORTER> importer;

    if( type == FRAME_SCH_SYMBOL_EDITOR )
    {
        SYMBOL_EDIT_FRAME* symFrame = static_cast<SYMBOL_EDIT_FRAME*>( fr );
        symbol = symFrame->GetCurSymbol();
        importer = std::make_unique<GRAPHICS_IMPORTER_LIB_SYMBOL>( symbol, symFrame->GetUnit() );
    }
    else
    {
        importer = std::make_unique<GRAPHICS_IMPORTER_SCH>();
    }

    if( DXF_IMPORT_PLUGIN* dxf = dynamic_cast<DXF_IMPORT_PLUGIN*>( plugin.get() ) )
    {
        dxf->SetUnit( a.dxfUnits );

        if( a.lineWidthMm )
            importer->SetLineWidthMM( *a.lineWidthMm );
    }
    else
    {
        importer->SetLineWidthMM( 0.0 );
    }

    importer->SetPlugin( std::move( plugin ) );
    importer->SetImportOffsetMM( VECTOR2D( a.originMm.x / a.scale, a.originMm.y / a.scale ) );

    {
        LOCALE_IO dummy;    // Ensure floats can be read.

        if( importer->Load( a.path ) )
            importer->Import( VECTOR2D( a.scale, a.scale ) );
    }

    json out;
    out["warnings"] = sch::warningLines( importer->GetMessages() );

    auto staged = std::make_shared<sch::STAGED>();
    json uuids = json::array();

    for( std::unique_ptr<EDA_ITEM>& ptr : importer->GetItems() )
    {
        SCH_ITEM* item = dynamic_cast<SCH_ITEM*>( ptr.get() );

        if( !item )
            continue;

        ptr.release();
        staged->items.push_back( item );

        if( uuids.size() < sch::IMPORT_UUIDS_MAX )
            uuids.push_back( sch::utf8( item->m_Uuid.AsString() ) );
    }

    importer->ClearItems();

    out["items"] = staged->items.size();
    out["uuids"] = uuids;

    if( staged->items.empty() )
        return out.dump( -1, ' ', false, json::error_handler_t::replace );

    SCH_SCREEN* screen = fr->GetScreen();
    const bool  pointer = a.pointer;

    pcbjam_collab::runOnCoroutine( fr, [staged, type, symbol, screen, pointer]() {
        SCH_BASE_FRAME* frame = wxTheApp ? dynamic_cast<SCH_BASE_FRAME*>(
                                                   wxTheApp->GetTopWindow() )
                                         : nullptr;

        // The frame, its sheet or its symbol changed since the read: nothing lands.
        if( !frame || frame->GetFrameType() != type || frame->GetScreen() != screen )
            return;

        SYMBOL_EDIT_FRAME* symFrame = type == FRAME_SCH_SYMBOL_EDITOR
                                              ? static_cast<SYMBOL_EDIT_FRAME*>( frame )
                                              : nullptr;

        if( symFrame && symFrame->GetCurSymbol() != symbol )
            return;

        TOOL_MANAGER*         tm = frame->GetToolManager();
        SCH_SELECTION_TOOL*   selTool = tm->GetTool<SCH_SELECTION_TOOL>();
        KIGFX::VIEW*          view = frame->GetCanvas()->GetView();
        KIGFX::VIEW_CONTROLS* vc = frame->GetCanvas()->GetViewControls();
        SCH_COMMIT            commit( tm );

        tm->RunAction( ACTIONS::cancelInteractive );

        if( !pointer )
        {
            if( symFrame )
            {
                commit.Modify( symbol, screen );

                for( SCH_ITEM* item : staged->items )
                {
                    symbol->AddDrawItem( item );
                    item->ClearEditFlags();
                }
            }
            else
            {
                for( SCH_ITEM* item : staged->items )
                    commit.Add( item, screen );
            }

            staged->taken = true;
            commit.Push( _( "Import Graphic" ) );

            if( symFrame )
                symFrame->RebuildView();

            return;
        }

        tm->RunAction( ACTIONS::selectionClear );

        if( symFrame )
        {
            // SYMBOL_EDITOR_EDIT_TOOL::Paste's staging.
            commit.Modify( symbol, screen );

            for( SCH_ITEM& item : symbol->GetDrawItems() )
                item.ClearFlags( IS_NEW | IS_PASTED | SELECTED );

            for( SCH_ITEM* item : staged->items )
            {
                item->SetFlags( IS_NEW | IS_PASTED | SELECTED );
                symbol->AddDrawItem( item );
                view->Add( item );
            }

            staged->taken = true;
            selTool->RebuildSelection();
        }
        else
        {
            // SCH_EDITOR_CONTROL::Paste's staging.
            SCH_EDIT_FRAME* schEdit = static_cast<SCH_EDIT_FRAME*>( frame );

            for( SCH_ITEM* item : staged->items )
            {
                if( item->Type() == SCH_LINE_T )
                    item->SetFlags( STARTPOINT | ENDPOINT );

                item->SetFlags( IS_NEW | IS_PASTED | IS_MOVING );
                schEdit->AddToScreen( item, screen );
                commit.Added( item, screen );
                view->Hide( item, true );
            }

            staged->taken = true;

            EDA_ITEMS selItems( staged->items.begin(), staged->items.end() );
            tm->RunAction<EDA_ITEMS*>( ACTIONS::selectItems, &selItems );
        }

        SCH_SELECTION& selection = selTool->GetSelection();

        if( selection.Empty() )
        {
            commit.Revert();
            return;
        }

        if( SCH_ITEM* top = dynamic_cast<SCH_ITEM*>( selection.GetTopLeftItem() ) )
            selection.SetReferencePoint( top->GetPosition() );

        vc->SetCursorPosition( vc->GetMousePosition(), false );

        if( tm->RunSynchronousAction( SCH_ACTIONS::move, &commit ) )
            commit.Push( _( "Import Graphic" ) );
        else
            commit.Revert();
    } );

    return out.dump( -1, ' ', false, json::error_handler_t::replace );
}

} // namespace cc_e6

EMSCRIPTEN_BINDINGS( cc_e6_sch )
{
}
