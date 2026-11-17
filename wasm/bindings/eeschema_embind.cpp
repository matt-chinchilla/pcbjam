/*
 * Embind bindings for KiCad eeschema WASM.
 *
 * Picked up automatically by scripts/kicad/build-kicad-target.sh when building
 * the eeschema app (it compiles wasm/bindings/<app>_embind.cpp if present).
 */

// Modified by Chirichella Inc. on 2026-10-15: kicadImportSchematic, importing a non-KiCad
// schematic or an Altium project through SCH_EDIT_FRAME's own import path.
// Modified by Chirichella Inc. on 2026-11-07: kicadSymbolGet, kicadSymbolSet, kicadAnnotate
// and kicadCheckSheet, the Editing window's symbol properties, numbering and check.
// Modified by Chirichella Inc. on 2026-11-16: kicadSymbolForEditor and
// kicadSymbolPushToSchematic, a placed symbol to the Symbol Editor and back (engine run 14).
// Modified by Chirichella Inc. on 2026-11-16: kicadExportSchematicOutput, a schematic output
// through KiCad's own job or the symbol export beneath its dialog (engine run 14).
// Modified by Chirichella Inc. on 2026-11-16: kicadSymbolsGet, kicadSymbolsSet,
// kicadSymbolsExchange, kicadSchEdit and the schematic half of kicadSettingsGet and
// kicadSettingsSet (engine run 14, cc_rules_sch.inc).

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/bind.h>
#include <kiway_player.h>
#include <kiway.h>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <wx/app.h>
#include <wx/filename.h>
#include <wx/string.h>
#include <wx/window.h>
#include <nlohmann/json.hpp>
#include <kiid.h>
#include <layer_ids.h>
#include <pcbjam_read_only.h>
#include <project.h>
#include <schematic.h>
#include <sch_edit_frame.h>
#include <symbol_edit_frame.h>
#include <sch_io/kicad_sexpr/sch_io_kicad_sexpr.h>
#include <sch_io/sch_io_mgr.h>
#include <sch_sheet.h>
#include <richio.h>
#include <lib_symbol.h>
#include <tools/sch_selection.h>
#include <tools/sch_selection_tool.h>
#include <sch_commit.h>
#include <sch_draw_panel.h>
#include <geometry/eda_angle.h>
#include <math/util.h>
#include <tool/tool_manager.h>
#include <view/view.h>
#include <view/view_overlay.h>
#include <chrono>
#include <wx/event.h>
#include <sch_item.h>
#include <sch_line.h>
#include <sch_junction.h>
#include <sch_no_connect.h>
#include <sch_text.h>
#include <sch_label.h>
#include <sch_symbol.h>
#include <sch_field.h>
#include <sch_shape.h>
#include <eda_shape.h>
#include <stroke_params.h>
#include <sch_screen.h>
#include <sch_sheet_path.h>
#include <schematic_settings.h>
#include <tool/actions.h>
#include <tools/sch_actions.h>
#include <tool/coroutine.h>
#include <pcbjam_remote_lock.h>
#include "collab_common.h"
#include "open_gate.h"
#include "collab_presence_core.h"
#include "collab_presence_style.h"
#include "pcbjam_theme.h"
#include "pcbjam_libs_reload.h"
#include "pcbjam_async_policy.h"
#include "symedit_common.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <base_units.h>
#include <eeschema_settings.h>
#include <lib_id.h>
#include <reporter.h>
#include <sch_marker.h>
#include <sch_reference_list.h>
#include <sch_view.h>
#include <erc/erc.h>
#include <erc/erc_item.h>
#include <erc/erc_report.h>
#include <erc/erc_settings.h>
#include <mail_type.h>
// The simulator's exports (engine run 14, caps simModel and sim).
#include <advanced_config.h>
#include <deque>
#include <functional>
#include <project/project_file.h>
#include <string_utils.h>
#include <wildcards_and_files_ext.h>
#include <wx/arrstr.h>
#include <wx/regex.h>
#include <sim/sim_lib_mgr.h>
#include <sim/sim_library.h>
#include <sim/sim_library_ibis.h>
#include <sim/sim_model.h>
#include <sim/sim_model_ibis.h>
#include <sim/simulator_reporter.h>
#include <sim/spice_circuit_model.h>
#include <sim/spice_generator.h>
#include <sim/spice_settings.h>
#include <sim/spice_simulator.h>
#include <sim/spice_value.h>
#include <pgm_base.h>
#include <settings/settings_manager.h>
#include <ki_exception.h>
#include <jobs/job_export_sch_plot.h>
#include <wx/log.h>
#include "cc_output_common.h"

using namespace emscripten;
using json = nlohmann::json;

// Programmatically open a project file (schematic) in the running editor frame,
// without UI automation. Mirrors single_top.cpp's MacOpenFile path: the editor
// frame is the app's top window and is a KIWAY_PLAYER. Returns the result of
// OpenProjectFiles, or false if no frame is available — letting the JS caller
// fall back to driving File→Open.
//
// KICAD_MERGED_EMBIND (kicad_editor, editor-unification Part 2): pcbnew_embind.cpp
// defines the identical function and registers the same JS names — in the merged image
// the frame-agnostic duplicates (this + kicadCollabOnSave) and the shared-name
// registrations live once in kicad_editor_embind.cpp, which dispatches the per-editor
// entries (renamed schCollab*/pcbCollab* below; JS-facing names are unchanged).
#ifndef KICAD_MERGED_EMBIND
bool kicadOpenFile( std::string path )
{
    // Held across every suspension of the load; see open_gate.h.
    pcbjam_open::BusyGuard busy;

    if( pcbjam_open::testParkMs() > 0 )
        emscripten_sleep( pcbjam_open::testParkMs() );

    KIWAY_PLAYER* frame =
            wxTheApp ? static_cast<KIWAY_PLAYER*>( wxTheApp->GetTopWindow() ) : nullptr;

    if( !frame )
        return false;

    if( wxWindow* blocking = frame->Kiway().GetBlockingDialog() )
        blocking->Close( true );

    bool ok = frame->OpenProjectFiles(
            std::vector<wxString>( 1, wxString::FromUTF8( path.c_str() ) ) );

    // Test-only post-load park (open_gate.h): model fully loaded, gate still
    // closed — the deterministic window the collab-load-fuzz spec hammers.
    if( pcbjam_open::testParkMs() > 0 )
        emscripten_sleep( pcbjam_open::testParkMs() );

    return ok;
}

// JS-pollable open-in-flight probe (open_gate.h): the web shell defers the
// collab/presence attach until the open chain has truly completed.
bool kicadOpenFileBusy()
{
    return pcbjam_open::busy();
}

// Test-only (collab-load-fuzz): arm the deterministic open parks.
void kicadTestSetOpenPark( int aMs )
{
    pcbjam_open::testParkMs() = aMs;
}

// Read-only viewer lock (read-only-viewer): flips the process-global
// PCBJAM_READ_ONLY flag consumed by TOOL_MANAGER (view-only action allowlist)
// and the selection tools (nothing selectable), and mirrors it onto the
// project so the setup dialogs grey out. Returns false until the editor frame
// exists so JS polls; the shell fails CLOSED if it never applies.
bool kicadSetReadOnly( bool aReadOnly )
{
    KIWAY_PLAYER* frame =
            wxTheApp ? dynamic_cast<KIWAY_PLAYER*>( wxTheApp->GetTopWindow() ) : nullptr;

    if( !frame )
        return false;

    PCBJAM_READ_ONLY::Set( aReadOnly );
    frame->Prj().SetReadOnly( aReadOnly );
    return true;
}
#endif // !KICAD_MERGED_EMBIND

// Modified by Chirichella Inc. on 2026-10-15: import a non-KiCad schematic (or an
// Altium project's document list) through SCH_EDIT_FRAME's own import path, the one
// File > Import drives on the desktop. Same busy guard and coroutine rules as
// kicadOpenFile. aPropsJson is a flat JSON object of string to string.
// aTypeName is one of the eight importer types, spelled as SCH_IO_MGR::EnumFromStr reads
// them: "Altium", "CADSTAR Schematic Archive", "EAGLE", "EasyEDA (JLCEDA) Std",
// "EasyEDA (JLCEDA) Pro", "gEDA / Lepton EDA", "LTspice", "PADS Logic". Any other name
// (KiCad, Legacy, Database, HTTP, PCBJAM, unknown) returns false before the frame is
// touched. Resolves true only when a sheet was loaded (the item check at the end).
// Outside KICAD_MERGED_EMBIND on purpose: the JS name is eeschema-only, so this TU
// registers it in both the standalone and the merged image (like kicadSaveSchematic),
// and kicad_editor_embind.cpp stays free of eeschema headers.
static bool kicadImportSchematic( std::string aPath, std::string aTypeName, std::string aPropsJson )
{
    pcbjam_open::BusyGuard busy;

    SCH_IO_MGR::SCH_FILE_T type = SCH_IO_MGR::EnumFromStr( wxString::FromUTF8( aTypeName ) );

    // The cases importFile's own switch handles; its default branch imports nothing.
    switch( type )
    {
    case SCH_IO_MGR::SCH_ALTIUM:
    case SCH_IO_MGR::SCH_CADSTAR_ARCHIVE:
    case SCH_IO_MGR::SCH_EAGLE:
    case SCH_IO_MGR::SCH_LTSPICE:
    case SCH_IO_MGR::SCH_EASYEDA:
    case SCH_IO_MGR::SCH_EASYEDAPRO:
    case SCH_IO_MGR::SCH_PADS:
    case SCH_IO_MGR::SCH_GEDA:
        break;

    default:
        return false;
    }

    SCH_EDIT_FRAME* frame =
            wxTheApp ? dynamic_cast<SCH_EDIT_FRAME*>( wxTheApp->GetTopWindow() ) : nullptr;

    if( !frame )
        return false;

    // Malformed or non-object props are refused BEFORE any side effect (the
    // blocking-dialog close below, and importFile's default screens on failure).
    nlohmann::json parsed = nlohmann::json::parse( aPropsJson, nullptr, false );

    if( !parsed.is_object() )
        return false;

    if( wxWindow* blocking = frame->Kiway().GetBlockingDialog() )
        blocking->Close( true );

    std::map<std::string, UTF8> props;

    for( const auto& [key, value] : parsed.items() )
        if( value.is_string() )
            props[key] = UTF8( value.get<std::string>() );

    if( !frame->ImportSchematicFile( wxString::FromUTF8( aPath ), type, &props ) )
        return false;

    // importFile reports true on every path but a non-absolute aPath: a failed load (null
    // sheet, IO_ERROR, std::exception) calls CreateDefaultScreens() and falls through to its
    // one `return true`. That default schematic is one top-level sheet with an empty screen,
    // so the item check is what tells a loaded sheet from the default screen. Every
    // top-level sheet is read, not RootScreen() alone: an Altium project import makes each
    // document a top-level sheet, and the first one may be blank.
    for( SCH_SHEET* sheet : frame->Schematic().GetTopLevelSheets() )
    {
        if( sheet && sheet->GetScreen() && !sheet->GetScreen()->Items().empty() )
            return true;
    }

    return false;
}

// ───────────────────────────── Yjs collaborative bridge ─────────────────────────────
//
// eeschema's half of the unified bridge contract (features/yjs-bridge/0001, 0003).
// Unlike pl_editor it needs NO kicad-fork change: SCH_ITEM already carries a stable
// KIID, and eeschema has native change machinery, so the adapter is a thin re-use:
//   ChangeSource (emit) = a SCHEMATIC_LISTENER subclass (SCH_COMMIT::Push fires it)
//   apply              = SCH_COMMIT Modify/Remove + Push (drives connectivity recompute)
// The generic JS reconciler / transport / WasmTool wiring are reused unchanged.
//
// Scope of this first commit (0003 §"first PoC"): position-level sync of existing
// items — changed (move/edit) and removed. Decomposed field coverage via reflection,
// constructing arbitrary new item types on `added`, and symbol-instance / multi-sheet
// scoping are deferred (see TODOs). Items are resolved by globally-unique uuid, so
// changed/removed already work across the whole hierarchy without sheet scoping.
namespace {

// Guard so SCH_COMMIT::Push's listener callbacks during apply() aren't re-emitted.
bool s_applyingRemote = false;

using pcbjam_collab::toUtf8;

SCH_EDIT_FRAME* schFrame()
{
    return wxTheApp ? dynamic_cast<SCH_EDIT_FRAME*>( wxTheApp->GetTopWindow() ) : nullptr;
}

// The screen of the sheet the editor is currently showing. Per-sheet collab keys a room
// to each .kicad_sch, so the snapshot/diff that feeds a room must cover ONLY this screen,
// never the whole Hierarchy(). GetCurrentSheet().LastScreen() is the active sheet's screen
// (GetScreen() tracks the same screen and is the fallback before a sheet path exists).
SCH_SCREEN* currentScreen( SCH_EDIT_FRAME* aFrame )
{
    if( !aFrame )
        return nullptr;

    if( SCH_SCREEN* screen = aFrame->GetCurrentSheet().LastScreen() )
        return screen;

    return aFrame->GetScreen();
}

json itemToJson( SCH_ITEM* aItem )
{
    VECTOR2I p = aItem->GetPosition();
    json     j = {
        { "id", toUtf8( aItem->m_Uuid.AsString() ) },
        { "type", toUtf8( aItem->GetClass() ) },
        { "x", p.x },   // internal units; integral, so no quantization needed
        { "y", p.y },
    };

    // Per-type fields needed to reconstruct the item on `added` (0003 converters).
    // Hand-mapped for the wire-editing types; other types sync position-only (move) and
    // are skipped on `added` (logged) until their converters are added.
    if( aItem->Type() == SCH_LINE_T )
    {
        auto* line = static_cast<SCH_LINE*>( aItem );
        j["sx"]    = line->GetStartPoint().x;
        j["sy"]    = line->GetStartPoint().y;
        j["ex"]    = line->GetEndPoint().x;
        j["ey"]    = line->GetEndPoint().y;
        j["layer"] = (int) line->GetLayer();
    }

    if( EDA_TEXT* txt = dynamic_cast<EDA_TEXT*>( aItem ) )
        j["text"] = toUtf8( txt->GetText() );

    if( SCH_LABEL_BASE* lbl = dynamic_cast<SCH_LABEL_BASE*>( aItem ) )
        j["shape"] = (int) lbl->GetShape();

    // Graphic shapes (circle / rectangle / arc / line / bezier): geometry is start/end plus,
    // for an arc, the center, and for a bezier the two control points. Stroke + fill complete it.
    if( aItem->Type() == SCH_SHAPE_T )
    {
        auto* shp   = static_cast<SCH_SHAPE*>( aItem );
        j["stype"]  = (int) shp->GetShape();          // SHAPE_T
        j["sx"]     = shp->GetStart().x;
        j["sy"]     = shp->GetStart().y;
        j["ex"]     = shp->GetEnd().x;
        j["ey"]     = shp->GetEnd().y;
        j["layer"]  = (int) shp->GetLayer();
        j["width"]  = shp->GetStroke().GetWidth();
        j["fill"]   = (int) shp->GetFillMode();

        if( shp->GetShape() == SHAPE_T::ARC )
        {
            VECTOR2I c = shp->GetCenter();
            j["cx"]    = c.x;
            j["cy"]    = c.y;
        }
        else if( shp->GetShape() == SHAPE_T::BEZIER )
        {
            j["c1x"] = shp->GetBezierC1().x;
            j["c1y"] = shp->GetBezierC1().y;
            j["c2x"] = shp->GetBezierC2().x;
            j["c2y"] = shp->GetBezierC2().y;
        }
    }

    return j;
}

// Construct a new SCH_ITEM from a delta item (for `added`), with the delta's uuid
// (m_Uuid is const → const_cast, exactly as the s-expr parser does). Returns nullptr for
// types without a converter yet.
SCH_ITEM* makeItem( const json& j )
{
    std::string type = j.value( "type", "" );
    SCH_ITEM*   item = nullptr;

    if( type == "SCH_LINE" )
    {
        int   layer = j.value( "layer", (int) LAYER_NOTES );
        auto* line  = new SCH_LINE( VECTOR2I( j.value( "sx", 0 ), j.value( "sy", 0 ) ), layer );
        line->SetEndPoint( VECTOR2I( j.value( "ex", 0 ), j.value( "ey", 0 ) ) );
        item = line;
    }
    else if( type == "SCH_JUNCTION" )
    {
        item = new SCH_JUNCTION( VECTOR2I( j.value( "x", 0 ), j.value( "y", 0 ) ) );
    }
    else if( type == "SCH_NO_CONNECT" )
    {
        item = new SCH_NO_CONNECT( VECTOR2I( j.value( "x", 0 ), j.value( "y", 0 ) ) );
    }
    else if( type == "SCH_TEXT" )
    {
        auto* txt = new SCH_TEXT( VECTOR2I( j.value( "x", 0 ), j.value( "y", 0 ) ),
                                  wxString::FromUTF8( j.value( "text", "" ).c_str() ) );

        // Mirror the interactive text tool (sch_drawing_tools.cpp createNewText): parent the
        // item to the schematic and apply the project's default text size, so a remotely-added
        // text resolves variables / renders identically to a locally-placed one.
        if( SCH_EDIT_FRAME* fr = schFrame() )
        {
            txt->SetParent( &fr->Schematic() );
            int sz = fr->Schematic().Settings().m_DefaultTextSize;
            txt->SetTextSize( VECTOR2I( sz, sz ) );
        }

        item = txt;
    }
    else if( type == "SCH_LABEL" || type == "SCH_GLOBALLABEL" || type == "SCH_HIERLABEL" )
    {
        VECTOR2I pos( j.value( "x", 0 ), j.value( "y", 0 ) );
        wxString text = wxString::FromUTF8( j.value( "text", "" ).c_str() );

        SCH_LABEL_BASE* lbl = ( type == "SCH_LABEL" )         ? (SCH_LABEL_BASE*) new SCH_LABEL( pos, text )
                              : ( type == "SCH_GLOBALLABEL" ) ? (SCH_LABEL_BASE*) new SCH_GLOBALLABEL( pos, text )
                                                             : (SCH_LABEL_BASE*) new SCH_HIERLABEL( pos, text );

        if( j.contains( "shape" ) )
            lbl->SetShape( (LABEL_FLAG_SHAPE) j["shape"].get<int>() );

        item = lbl;
    }
    else if( type == "SCH_SHAPE" )
    {
        // Reconstruct from the geometry itemToJson emits. Committing a *new* SCH_SHAPE used to
        // trap in SCH_COMMIT::Push's CHT_ADD path under the retired asyncify runtime because
        // doApply ran off the coroutine context; doApply runs inside a COROUTINE
        // (kicadCollabApply), serialized with local edits, exactly as a native draw does.
        // (0006/0007.)  NB FILL_T::NO_FILL == 1, not 0.
        SHAPE_T st    = (SHAPE_T) j.value( "stype", (int) SHAPE_T::RECTANGLE );
        int     layer = j.value( "layer", (int) LAYER_NOTES );
        int     width = j.value( "width", 0 );
        FILL_T  fill  = (FILL_T) j.value( "fill", (int) FILL_T::NO_FILL );

        auto* shp = new SCH_SHAPE( st, (SCH_LAYER_ID) layer, width, fill );

        // Rectangle: two corners. Circle: start = center, end = a point on the radius. Both are
        // fully defined by start+end (what itemToJson emits via GetStart()/GetEnd()).
        shp->SetStart( VECTOR2I( j.value( "sx", 0 ), j.value( "sy", 0 ) ) );
        shp->SetEnd( VECTOR2I( j.value( "ex", 0 ), j.value( "ey", 0 ) ) );

        if( st == SHAPE_T::ARC && j.contains( "cx" ) )
        {
            shp->SetCenterX( j["cx"].get<int>() );
            shp->SetCenterY( j["cy"].get<int>() );
        }
        else if( st == SHAPE_T::BEZIER )
        {
            if( j.contains( "c1x" ) )
                shp->SetBezierC1( VECTOR2I( j["c1x"].get<int>(), j["c1y"].get<int>() ) );
            if( j.contains( "c2x" ) )
                shp->SetBezierC2( VECTOR2I( j["c2x"].get<int>(), j["c2y"].get<int>() ) );
        }

        if( SCH_EDIT_FRAME* fr = schFrame() )
            shp->SetParent( &fr->Schematic() );

        item = shp;
    }
    // SCH_SYMBOL `added` is still deferred (needs the s-expr clipboard-blob emit + LoadContent
    // reconstruction; symbol PLACEMENT is also natively blocked until symbol libraries are
    // bundled — see features/yjs-bridge tasks). Symbol move/position already syncs via `changed`.

    if( item )
        const_cast<KIID&>( item->m_Uuid ) = KIID( wxString::FromUTF8( j.value( "id", "" ).c_str() ) );

    return item;
}

// Current model of the ACTIVE screen as an array of item json. One collab room == one
// .kicad_sch screen, so we never fold in the rest of the hierarchy (uuids are unique
// within a screen, so no cross-sheet dedup is needed).
json snapshotItems( SCH_EDIT_FRAME* aFrame )
{
    json arr = json::array();

    if( SCH_SCREEN* screen = currentScreen( aFrame ) )
    {
        for( SCH_ITEM* item : screen->Items() )
            arr.push_back( itemToJson( item ) );
    }

    return arr;
}

// Wire emitters (legacy scalar delta + v2 items): shared, collab_common.h.
using pcbjam_collab::emitDelta;
using pcbjam_collab::emitItemsWire;

// Notify the standalone that the editor switched to a different sheet — a different
// .kicad_sch == a different collab room (ysync subschemas). The path is the active
// screen's load path: the same absolute MEMFS form kicadCollabOnSave emits, so the JS
// side strips the project prefix the same way (relativeProjectPath). No-op without a
// JS listener.
void emitSheetChanged()
{
    SCH_SCREEN* screen = currentScreen( schFrame() );

    if( !screen )
        return;

    std::string s = toUtf8( screen->GetFileName() );
    EM_ASM( {
        if( window.kicadCollab && window.kicadCollab.onSheetChanged )
        {
            // A throwing listener must never unwind the wasm frame that called it: under
            // JSPI that rejects the running coroutine's entry (findings P-1).
            try { window.kicadCollab.onSheetChanged( UTF8ToString( $0 ) ); }
            catch( e ) { console.error( '[pcbjam collab] onSheetChanged listener threw', e ); }
        }
    }, s.c_str() );
}

// ───────────────────────── sheet navigator bridge (sheet-panel) ─────────────────────────
//
// The React stand-in for the docked wx HIERARCHY_PANE that kicadSetChrome(false)
// hides: kicadSheetsGetTree() returns the loaded hierarchy (every SCH_SHEET_PATH
// instance — a sheet file placed twice is two rows, like the wx pane) plus the
// current path; kicadSheetsEnter(path) navigates. Same shape as pcbnew's layer
// bridge: validate synchronously, apply on the frame's coroutine, then push the
// fresh state to window.kicadCollab.onSheetsState so the panel is event-driven.
// Rows are page-number ordered (the wx pane's order); `parent` is the KIID path
// of the enclosing instance ("" for the root) so JS can rebuild the tree.
json sheetsStateJson( SCH_EDIT_FRAME* aFrame )
{
    SCH_SHEET_LIST list = aFrame->Schematic().Hierarchy();
    list.SortByPageNumbers( /* aUpdateVirtualPageNums */ false );

    json sheets = json::array();

    for( const SCH_SHEET_PATH& path : list )
    {
        SCH_SHEET_PATH parent = path;

        if( parent.size() > 1 )
            parent.pop_back();

        SCH_SCREEN* screen = path.LastScreen();
        SCH_SHEET*  sheet = path.Last();
        wxString    name = sheet ? sheet->GetName() : wxString();

        // The root sheet carries no Sheetname — show its file like the wx pane does.
        if( name.IsEmpty() && screen )
            name = wxFileName( screen->GetFileName() ).GetName();

        sheets.push_back( {
                { "path", toUtf8( path.PathAsString() ) },
                { "parent", path.size() > 1 ? toUtf8( parent.PathAsString() ) : std::string() },
                { "name", toUtf8( name ) },
                { "file", screen ? toUtf8( screen->GetFileName() ) : std::string() },
                { "page", toUtf8( path.GetPageNumber() ) },
                { "depth", static_cast<int>( path.size() ) - 1 },
        } );
    }

    return { { "current", toUtf8( aFrame->GetCurrentSheet().PathAsString() ) },
             { "sheets", sheets } };
}

void emitSheetsState( SCH_EDIT_FRAME* aFrame )
{
    if( !aFrame )
        return;

    std::string s = sheetsStateJson( aFrame ).dump();
    EM_ASM( {
        if( window.kicadCollab && window.kicadCollab.onSheetsState )
        {
            // Never let a throwing listener unwind the wasm frame (findings P-1).
            try { window.kicadCollab.onSheetsState( UTF8ToString( $0 ) ); }
            catch( e ) { console.error( '[pcbjam sheets] onSheetsState listener threw', e ); }
        }
    }, s.c_str() );
}

// Serialize one live schematic item to its native s-expr via a one-item
// SCH_SELECTION through SCH_IO_KICAD_SEXPR::Format. For a symbol the output also
// carries its (lib_symbols …) definition (that prelude is emitted for any symbol
// in the selection, independent of aForClipboard).
//
// aForClipboard MUST be false. Clipboard mode is a LOSSY, paste-oriented dialect:
// it rewrites `(instances (project … (path …)))` relative to aRelativePath — so a
// symbol on the current sheet collapses to `(path "")` — takes the REFERENCE field
// from the per-sheet instance instead of the ordinal one, and keeps orphaned
// instance data (sch_io_kicad_sexpr.cpp saveSymbol: ~758-766, ~791-806, ~903).
// The Y.Doc is the source of truth for the FILE, so a wire blob must be byte-equal
// to that item's subtree in a full file save; clipboard form would silently strip
// every symbol's sheet path and unit/reference on materialize.
//
// aRelativePath is still required (Format wxCHECKs it non-null) but is unread on
// the aForClipboard=false path.
std::string itemBlob( SCH_EDIT_FRAME* aFrame, SCH_ITEM* aItem )
{
    SCH_SELECTION sel;
    sel.SetScreen( aFrame->GetScreen() );
    sel.Add( aItem );

    STRING_FORMATTER   fmt;
    SCH_IO_KICAD_SEXPR plugin;
    plugin.Format( &sel, &aFrame->GetCurrentSheet(), aFrame->Schematic(), &fmt,
                   /*aForClipboard*/ false );
    return fmt.GetString();
}

// ── Emit via post-settle snapshot diff ───────────────────────────────────────────────────
//
// A local edit is a single SCH_COMMIT::Push that fires OnItemsAdded/Removed/Changed
// synchronously and THEN runs RecalculateConnections (sch_commit.cpp ~402-430). So the native
// listener only ever sees the *pre-cleanup* (raw) geometry, while the connectivity cleanup
// that follows — merging collinear wires, dropping redundant junctions, splitting at new
// crossings — is never reported. Broadcasting those raw per-category lists made the peer
// reconstruct the edit from the raw state and run ITS OWN cleanup, over a different "dirty"
// scope, so on a big connected drag the two peers cleaned up differently and the peer lost
// segments/junctions.
//
// Instead, treat the listener purely as a "something changed" trigger and broadcast a DIFF of
// the full model taken AFTER the edit settles — a CallAfter, which runs once Push (cleanup
// included) has fully returned. That captures tab A's FINAL, already-clean geometry; the peer
// applies it and re-cleaning already-clean geometry is idempotent, so the two converge. (This
// mirrors pl_editor's snapshot-differ.) g_baseline is the last-broadcast state.

// Diff baseline of the ACTIVE screen only (per-sheet collab room scope), keyed by uuid.
std::map<std::string, json> snapshotByUuid( SCH_EDIT_FRAME* aFrame )
{
    std::map<std::string, json> m;

    if( SCH_SCREEN* screen = currentScreen( aFrame ) )
    {
        for( SCH_ITEM* item : screen->Items() )
        {
            // ERC markers are screen items but not document content: the file
            // writer never saves them and the selection writer cannot format
            // one (empty blob → the JS side skips the entry with a warning).
            // Keep them out of the collab model entirely.
            if( item->Type() == SCH_MARKER_T )
                continue;

            m[toUtf8( item->m_Uuid.AsString() )] = itemToJson( item );
        }
    }

    return m;
}

std::map<std::string, json> g_baseline;
bool                        g_flushScheduled = false;

// Roots the listener saw change since the last flush (uuids, captured at callback
// time — removed items may be freed before the flush runs). The scalar snapshot
// diff is a LOSSY projection: rotations/mirrors (anchor unchanged), field text
// edits (the commit stages the SCH_FIELD, which is not a screen item), stroke
// properties etc. never move it (bug 04). Dirty roots emit their v2 blob
// unconditionally; the apply is an idempotent upsert and the TS layer drops
// no-op bodies, so a false positive costs one local serialization.
std::set<std::string> g_dirty;

// Lift a commit-staged item to the SCREEN item the differ tracks (a field/pin/cell
// lifts to its symbol/sheet/label/table — same promotion sch_commit's undo uses).
void noteDirty( SCH_ITEM* aItem )
{
    if( !aItem )
        return;

    while( EDA_ITEM* p = aItem->GetParent() )
    {
        if( !p->IsType( { SCH_SYMBOL_T, SCH_TABLE_T, SCH_SHEET_T, SCH_LABEL_LOCATE_ANY_T } ) )
            break;

        aItem = static_cast<SCH_ITEM*>( p );
    }

    g_dirty.insert( toUtf8( aItem->m_Uuid.AsString() ) );
}

// Re-seed the diff baseline to the current model — after handing out a seed snapshot, or after
// applying a remote delta (so those items aren't re-broadcast as a spurious local diff/echo).
// Declares "current model == broadcast state", so pending dirty marks are stale too — on a
// sheet switch they'd otherwise emit the OLD sheet's items into the new sheet's room.
void rebaseline()
{
    if( SCH_EDIT_FRAME* fr = schFrame() )
        g_baseline = snapshotByUuid( fr );

    g_dirty.clear();
}

// TARGETED rebaseline (bug 05): refresh baseline entries ONLY for the uuids a remote
// apply touched. A global rebaseline() would fold a concurrently-committed local edit
// (its flush is queued BEHIND the apply on the same pending-event list) into the
// baseline and silently swallow it; targeted, the edit still diffs and emits. The
// connectivity cleanup the apply's Push produced likewise stays diffable — the
// post-apply flush broadcasts it (idempotent on the original sender).
void rebaselineTouched( SCH_EDIT_FRAME* aFrame, const std::vector<std::string>& aIds )
{
    for( const std::string& id : aIds )
    {
        g_baseline.erase( id );

        KIID kid( wxString::FromUTF8( id.c_str() ) );

        if( SCH_ITEM* live = aFrame->Schematic().ResolveItem( kid, nullptr, /*allowNull*/ true ) )
            g_baseline[id] = itemToJson( live );
    }
}

// Diff the current (settled, post-cleanup) model against the baseline and broadcast the change.
void flushDiff()
{
    g_flushScheduled = false;

    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
        return;

    std::map<std::string, json> cur = snapshotByUuid( fr );

    json added = json::array(), changed = json::array(), removed = json::array();

    // v2 items wire (per-item s-expr blobs), built from the same diff. Screen items
    // are already root-level (fields live inside their symbols), so no lifting.
    json                  wAdded = json::array(), wChanged = json::array();
    std::set<std::string> wDone;

    auto blobFor = [&]( const std::string& id, json& aArr )
    {
        KIID kid( wxString::FromUTF8( id.c_str() ) );

        SCH_ITEM* item = fr->Schematic().ResolveItem( kid, nullptr, /*allowNull*/ true );

        if( !item )
            return;

        // A child that reached the dirty set unlifted (field/pin/sheet-pin —
        // ResolveItem walks direct children too) serializes to an EMPTY blob:
        // the selection writer has no case for it, so the entry used to reach
        // JS as envelope furniture and be skipped, silently losing the edit.
        // Lift to the screen item the differ tracks (same promotion as
        // noteDirty) so the parent's re-blob carries the child's content.
        while( EDA_ITEM* p = item->GetParent() )
        {
            if( !p->IsType( { SCH_SYMBOL_T, SCH_TABLE_T, SCH_SHEET_T, SCH_LABEL_LOCATE_ANY_T } ) )
                break;

            item = static_cast<SCH_ITEM*>( p );
        }

        std::string rootId = toUtf8( item->m_Uuid.AsString() );

        if( !wDone.insert( rootId ).second )
            return;

        std::string sexpr = itemBlob( fr, item );

        // Still empty after lifting (a marker that slipped in, an unformattable
        // type): never put a hollow envelope on the wire — mirror pcbnew's P-5
        // guard and leave a breadcrumb instead.
        if( sexpr.empty() )
        {
            std::string type = toUtf8( item->GetClass() );
            EM_ASM( {
                console.warn( '[pcbjam collab] eeschema: skipped un-serializable dirty root',
                              { uuid: UTF8ToString( $0 ), type: UTF8ToString( $1 ) } );
            }, rootId.c_str(), type.c_str() );
            return;
        }

        aArr.push_back( json{ { "sexpr", sexpr }, { "parent", nullptr } } );
    };

    for( const auto& [id, j] : cur )
    {
        auto it = g_baseline.find( id );

        if( it == g_baseline.end() )
        {
            added.push_back( j );
            blobFor( id, wAdded );
        }
        else if( it->second != j )
        {
            changed.push_back( j );
            blobFor( id, wChanged );
        }
    }

    for( const auto& [id, j] : g_baseline )
    {
        if( !cur.count( id ) )
            removed.push_back( id );
    }

    // Dirty roots (bug 04): whatever the listener saw commit emits its blob on the
    // v2 wire even when the scalar projection didn't move (rotation, field text,
    // stroke edits). wDone dedups against the scalar-diff emits; deleted ids
    // resolve null inside blobFor and skip (the removal loop covered them).
    for( const std::string& id : g_dirty )
        blobFor( id, wChanged );

    g_dirty.clear();

    g_baseline = std::move( cur );

    if( !added.empty() || !changed.empty() || !removed.empty() )
        emitDelta( json{ { "added", added }, { "changed", changed }, { "removed", removed } } );

    if( !wAdded.empty() || !wChanged.empty() || !removed.empty() )
        emitItemsWire( json{ { "added", wAdded }, { "changed", wChanged }, { "removed", removed } } );
}

// Coalesce all the listener callbacks of one commit (and any other edits in the same loop
// turn) into a single post-settle diff. flushDiff runs inside a COROUTINE via the apply
// queue: its v2 items emit serializes items via SCH_IO_KICAD_SEXPR::Format (itemBlob), and
// queueing it with the applies keeps emits from interleaving with a suspended apply body
// (under the retired asyncify runtime the bare CallAfter stack additionally trapped here —
// same lesson as doApply, 0007).
void scheduleFlush()
{
    if( g_flushScheduled )
        return;

    g_flushScheduled = true;

    if( SCH_EDIT_FRAME* fr = schFrame() )
        pcbjam_collab::runOnCoroutine( fr, []() { flushDiff(); } );
    else
        flushDiff();
}

// A hierarchical sheet was just created locally ("Add Sheet"): write its new child screen
// to the child .kicad_sch file and tell the standalone (window.kicadCollab.onSheetCreated),
// so the child is persisted/registered the moment it's created — without waiting for the
// user to enter it or save the project. Otherwise the parent's `(sheet … child)` reference
// dangles for peers / on reload. Deferred onto the apply coroutine (CallAfter + COROUTINE),
// same as flushDiff/doApply, so the save serializes with any in-flight apply.
// The sheet is re-resolved by uuid in the deferred body so a
// since-deleted sheet (e.g. an immediate undo) is a no-op rather than a dangling pointer.
void scheduleSheetSave( SCH_SHEET* aSheet )
{
    SCH_EDIT_FRAME* fr = schFrame();
    SCH_SCREEN*     parent = currentScreen( fr );

    if( !fr || !parent || !aSheet->GetScreen() )
        return;

    wxFileName childFn( aSheet->GetFileName() ); // relative "Sheetfile"
    childFn.MakeAbsolute( wxFileName( parent->GetFileName() ).GetPath() );

    std::string childAbs = toUtf8( childFn.GetFullPath() );
    std::string uuid = toUtf8( aSheet->m_Uuid.AsString() );

    pcbjam_collab::runOnCoroutine( fr, [fr, childAbs, uuid]() {
        KIID      kid( wxString::FromUTF8( uuid.c_str() ) );
        SCH_ITEM* item = fr->Schematic().ResolveItem( kid, nullptr, /*allowNull*/ true );

        if( !item || item->Type() != SCH_SHEET_T )
            return;

        try
        {
            SCH_IO_KICAD_SEXPR io;
            io.SaveSchematicFile( wxString::FromUTF8( childAbs.c_str() ),
                                  static_cast<SCH_SHEET*>( item ), &fr->Schematic() );
        }
        catch( ... )
        {
            return; // a write failure must not abort the runtime
        }

        EM_ASM( {
            if( window.kicadCollab && window.kicadCollab.onSheetCreated )
            {
                // A throwing listener must never unwind the wasm frame that called it: under
                // JSPI that rejects the running coroutine's entry (findings P-1).
                try { window.kicadCollab.onSheetCreated( UTF8ToString( $0 ) ); }
                catch( e ) { console.error( '[pcbjam collab] onSheetCreated listener threw', e ); }
            }
        }, childAbs.c_str() );
    } );
}

// ChangeSource: the native SCHEMATIC_LISTENER is just a trigger — the actual change set comes
// from the post-settle snapshot diff above. Skipped while applying a remote delta (no echo);
// doApply rebaselines instead.
//
// Presence (collab-presence 0003): schematic changes often change the selection
// too (delete, paste) with no closing canvas event — the trigger below also
// piggybacks a selection re-check. Defined in the presence section further down.
void schedulePresenceSelCheck();
// Findings Y-1/Y-3: ANY document change (local commit or remote apply) repaints
// the peers' overlay from the live document and re-checks the local selection,
// post-settle on the apply coroutine. Runs BEFORE the applying-remote early
// return below — the remote apply is exactly the case that left peers' boxes on
// deleted items.
void schedulePresenceDocChanged();

class COLLAB_LISTENER : public SCHEMATIC_LISTENER
{
public:
    void OnSchItemsAdded( SCHEMATIC&, std::vector<SCH_ITEM*>& aItems ) override
    {
        // A newly-added hierarchical sheet → persist + register its child file (above).
        if( !s_applyingRemote )
        {
            for( SCH_ITEM* item : aItems )
                if( item->Type() == SCH_SHEET_T )
                    scheduleSheetSave( static_cast<SCH_SHEET*>( item ) );
        }
        trigger( aItems );
    }
    void OnSchItemsChanged( SCHEMATIC&, std::vector<SCH_ITEM*>& v ) override { trigger( v ); }
    void OnSchItemsRemoved( SCHEMATIC&, std::vector<SCH_ITEM*>& v ) override { trigger( v ); }

    // The editor switched to a different sheet (a different .kicad_sch == a different
    // collab room). Re-baseline so the first edit on the new sheet diffs against ITS
    // screen, not the previous one, then tell the standalone to rebind its room to the
    // now-active sheet file. Fires from SCH_EDIT_FRAME::DisplayCurrentSheet, by which
    // point GetCurrentSheet()/GetScreen() already point at the new sheet.
    void OnSchSheetChanged( SCHEMATIC& ) override
    {
        rebaseline();
        emitSheetChanged();
        // The floating sheet panel follows wx-driven navigation too (double-
        // clicking a sheet symbol, Alt+Backspace, the toolbar arrows).
        emitSheetsState( schFrame() );
    }

private:
    // Capture the touched roots at callback time (fields/pins lift to their
    // symbol — noteDirty), then coalesce into one post-settle flush.
    void trigger( const std::vector<SCH_ITEM*>& aItems )
    {
        schedulePresenceDocChanged();

        if( s_applyingRemote )
            return;

        for( SCH_ITEM* item : aItems )
            noteDirty( item );

        scheduleFlush();
        schedulePresenceSelCheck();
    }
};

COLLAB_LISTENER* g_listener = nullptr;

// Get the live SCHEMATIC and ensure our listener is registered on it (idempotent).
SCHEMATIC* ensureBridge()
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
        return nullptr;

    SCHEMATIC& sch = fr->Schematic();

    if( !g_listener )
    {
        g_listener = new COLLAB_LISTENER();
        sch.AddListener( g_listener );
    }

    return &sch;
}

// ───────────────────────── collab presence (collab-presence 0003) ─────────────────────────
//
// eeschema port of pcbnew's presence layer (0002 — see pcbnew_embind.cpp for the full
// design rationale). The state + event/scheduling machinery live in the shared
// pcbjam_presence::CORE (collab_presence_core.h); this TU supplies only the
// eeschema-specific hooks: frame/tool lookup, KIID resolution via
// SCHEMATIC::ResolveItem, the bare-uuid selection payload, and the per-peer draw
// (cross-app ghosts gated to the CURRENT sheet). Zero kicad-fork changes: wx-layer
// Bind() triggers on the GAL canvas + the COLLAB_LISTENER piggyback, selection read
// post-settle from SCH_SELECTION_TOOL. Rooms are per-sheet (warm pool), so peers
// publishing cursor/selection in the bound room are BY CONSTRUCTION on this same
// sheet file — no sheet filtering is needed for same-room selections; the JS side
// rebinds the whole presence layer on sheet navigation (onSheetChanged) and clears
// the overlay in between.

// Cross-app selection (0006): the schematic items a peer's xsel (symbol uuids
// from a pcbnew peer) resolves to on the CURRENT sheet. Unlike same-room
// selections (per-sheet rooms scope those by construction) xsel arrives
// project-wide, so only items that resolve onto the sheet THIS canvas is
// showing count — a bbox from another sheet's screen would land at meaningless
// coordinates. ONE resolver shared by the ghost render and the test probe
// (kicadCollabTestGetCrossMapped), so the assertion can't drift from the pixels.
std::vector<SCH_ITEM*> resolveXsel( SCH_EDIT_FRAME* aFrame, const pcbjam_presence::PEER& aPeer )
{
    std::vector<SCH_ITEM*> items;

    for( const KIID& id : aPeer.xsel )
    {
        SCH_SHEET_PATH path;
        SCH_ITEM*      item = aFrame->Schematic().ResolveItem( id, &path, /*allowNull*/ true );

        if( item && path.LastScreen() == aFrame->GetScreen() )
            items.push_back( item );
    }

    return items;
}

pcbjam_presence::CORE& presenceCore()
{
    static pcbjam_presence::CORE core = []()
    {
        pcbjam_presence::CORE c;

        // eeschema ships its own defaults (hairline outline, subtler fill/cursor).
        c.style = pcbjam_presence::eeschemaDefaultStyle();

        c.frame = []() -> EDA_DRAW_FRAME* { return schFrame(); };

        c.selectionTool = []( EDA_DRAW_FRAME* fr ) -> SELECTION_TOOL*
        {
            return fr->GetToolManager()->GetTool<SCH_SELECTION_TOOL>();
        };

        // Selection emit = the bare uuid array (0006: eeschema uuids ARE the
        // symbol uuids; pcbnew's counterpart adds fpPaths).
        c.selectionEmitPayload = []( EDA_DRAW_FRAME* fr ) -> json
        {
            return presenceCore().selectionUuids( fr );
        };

        c.resolveItem = []( EDA_DRAW_FRAME* fr, const KIID& id ) -> EDA_ITEM*
        {
            return static_cast<SCH_EDIT_FRAME*>( fr )->Schematic()
                    .ResolveItem( id, nullptr, /*allowNull*/ true );
        };

        c.drawPeerShapes = []( pcbjam_presence::CORE& aCore, EDA_DRAW_FRAME* aFrame,
                               const pcbjam_presence::PEER& peer, const KIGFX::COLOR4D& color,
                               double px )
        {
            SCH_EDIT_FRAME* fr = static_cast<SCH_EDIT_FRAME*>( aFrame );

            // Reviewer (commenter) peers draw with the dashed/softer reviewer
            // style and a suffixed label (comments-ux 0003 §5.2).
            const pcbjam_presence::STYLE& base = peer.reviewer
                                                 ? pcbjam_presence::reviewerStyle( aCore.style )
                                                 : aCore.style;
            const std::string label = pcbjam_presence::peerLabel( aCore.style, peer.name,
                                                                  peer.reviewer );

            for( const KIID& id : peer.selection )
            {
                SCH_SHEET_PATH path;
                SCH_ITEM*      item = fr->Schematic().ResolveItem( id, &path, /*allowNull*/ true );

                if( !item )
                    continue;   // not in this schematic (yet) — skip silently

                pcbjam_presence::drawSelectionBox( aCore.overlay.get(), aCore.chipOverlay.get(),
                                                   aCore.textOverlay.get(),
                                                   item->ViewBBox(), label, color, px, base );
            }

            // Cross-app ghosts (0006) — see resolveXsel for the sheet gating.
            if( !peer.xsel.empty() )
            {
                pcbjam_presence::STYLE ghost = pcbjam_presence::ghostStyle( base );

                for( SCH_ITEM* item : resolveXsel( fr, peer ) )
                {
                    pcbjam_presence::drawSelectionBox( aCore.overlay.get(), aCore.chipOverlay.get(),
                                                       aCore.textOverlay.get(),
                                                       item->ViewBBox(), label, color, px,
                                                       ghost );
                }
            }
        };

        return c;
    }();

    return core;
}

void schedulePresenceSelCheck()
{
    presenceCore().scheduleSelCheck();
}

void schedulePresenceDocChanged()
{
    presenceCore().onDocChanged();
}

} // namespace


namespace {

// SCH_SYMBOL::Move() / SCH_LABEL_BASE::Move() move their child fields (reference, value, …) via
// an inner `field.Move()`. Under the retired asyncify runtime that inner call silently no-oped
// in the apply context — the field text was left behind at its old position while the body
// moved — so the fields are re-moved here with a devirtualized call. Kept as-is through the
// JSPI migration; the collab move/drift suites pin the peer's field positions.
void moveFields( std::vector<SCH_FIELD>& aFields, const VECTOR2I& aDelta )
{
    for( SCH_FIELD& field : aFields )
        field.SCH_FIELD::Move( aDelta );
}

// Move an item to an absolute position for the `changed` path. Under the retired asyncify
// runtime the virtual SCH_ITEM::Move(), dispatched from the apply/CallAfter context, silently
// NO-OPED (symbols/junctions/labels never moved on the peer — only SCH_LINE worked, via its
// direct SetStart/EndPoint path; see 0003 / eeschema_collab_asyncify_apply). Move() is
// therefore devirtualized with explicit class-qualified calls — statically bound plain wasm
// calls, correct on any runtime, so they stay. Composite items additionally need their child
// fields moved (see moveFields).
void moveItemTo( SCH_ITEM* aItem, const VECTOR2I& aNewPos )
{
    VECTOR2I delta = aNewPos - aItem->GetPosition();

    if( delta == VECTOR2I( 0, 0 ) )
        return;

    switch( aItem->Type() )
    {
    case SCH_SYMBOL_T:
    {
        auto* sym = static_cast<SCH_SYMBOL*>( aItem );
        sym->SCH_SYMBOL::Move( delta );
        moveFields( sym->GetFields(), delta );
        break;
    }
    case SCH_JUNCTION_T:   static_cast<SCH_JUNCTION*>( aItem )->SCH_JUNCTION::Move( delta ); break;
    case SCH_NO_CONNECT_T: static_cast<SCH_NO_CONNECT*>( aItem )->SCH_NO_CONNECT::Move( delta ); break;
    case SCH_TEXT_T:       static_cast<SCH_TEXT*>( aItem )->SCH_TEXT::Move( delta ); break;
    case SCH_LABEL_T:
    case SCH_GLOBAL_LABEL_T:
    case SCH_HIER_LABEL_T:
    {
        auto* lbl = static_cast<SCH_LABEL_BASE*>( aItem );
        lbl->SCH_LABEL_BASE::Move( delta );
        moveFields( lbl->GetFields(), delta );
        break;
    }
    default:
        aItem->Move( delta );   // virtual fallback (may no-op in the apply context)
        break;
    }
}

// The actual model mutation, via SCH_COMMIT so connectivity/ERC recompute as for a UI
// edit. (0003 decoded an asyncify-era dynCall trap on this path; the shim that healed
// it retired with that runtime.)
void doApply( SCH_EDIT_FRAME* aFrame, const json& aDelta )
{
    SCHEMATIC& sch = aFrame->Schematic();

    s_applyingRemote = true;

    SCH_COMMIT commit( aFrame );
    bool       staged = false;

    // With SKIP_UNDO no undo picker takes ownership of removed items; the commit
    // detaches them from the screen and we free them after Push. Fields are hidden
    // by CHT_REMOVE, not detached (still owned by their parent), so never freed.
    std::vector<SCH_ITEM*> removedItems;

    for( const json& rid : aDelta.value( "removed", json::array() ) )
    {
        SCH_SHEET_PATH path;
        KIID          id( wxString::FromUTF8( rid.get<std::string>().c_str() ) );

        if( SCH_ITEM* item = sch.ResolveItem( id, &path, /*allowNull*/ true ) )
        {
            commit.Remove( item, path.LastScreen() );

            if( item->Type() != SCH_FIELD_T )
                removedItems.push_back( item );

            staged = true;
        }
    }

    for( const json& j : aDelta.value( "changed", json::array() ) )
    {
        SCH_SHEET_PATH path;
        KIID          id( wxString::FromUTF8( j.value( "id", "" ).c_str() ) );

        if( SCH_ITEM* item = sch.ResolveItem( id, &path, /*allowNull*/ true ) )
        {
            commit.Modify( item, path.LastScreen() );

            if( item->Type() == SCH_LINE_T && j.contains( "sx" ) )
            {
                // Wires reshape (endpoints move independently), so set both points.
                auto* line = static_cast<SCH_LINE*>( item );
                line->SetStartPoint( VECTOR2I( j["sx"].get<int>(), j["sy"].get<int>() ) );
                line->SetEndPoint( VECTOR2I( j["ex"].get<int>(), j["ey"].get<int>() ) );
            }
            else if( j.contains( "x" ) && j.contains( "y" ) )
            {
                moveItemTo( item, VECTOR2I( j["x"].get<int>(), j["y"].get<int>() ) );
            }

            staged = true;
        }
    }

    for( const json& j : aDelta.value( "added", json::array() ) )
    {
        KIID id( wxString::FromUTF8( j.value( "id", "" ).c_str() ) );

        if( sch.ResolveItem( id, nullptr, /*allowNull*/ true ) )
            continue;                       // already present (our own echo)

        if( SCH_ITEM* item = makeItem( j ) )
        {
            commit.Add( item, aFrame->GetScreen() );
            staged = true;
        }
        else
        {
            EM_ASM( { console.log( "[collab] eeschema apply: no converter for added type " + UTF8ToString( $0 ) ); },
                    j.value( "type", "?" ).c_str() );
        }
    }

    // SKIP_UNDO: a peer's edit must never land on this editor's undo stack — Ctrl+Z
    // would revert (and re-broadcast) the peer's work. Undo is local-ops-only; stale
    // local undo entries are dropped/re-resolved by UUID at undo time (miss 09).
    if( staged )
        commit.Push( wxT( "Collaborative edit" ), SKIP_UNDO );

    for( SCH_ITEM* item : removedItems )
        delete item;

    // The applied remote changes (and any connectivity cleanup they triggered) are now the
    // shared state — fold them into the baseline so the post-apply listener flush doesn't
    // re-broadcast them as a local diff (echo).
    rebaseline();
    s_applyingRemote = false;
}

// v2 items apply: removed by uuid; added/changed are an idempotent per-item upsert.
// Each blob is parsed through the clipboard-paste path — LoadContent into a throwaway
// sheet (sch_editor_control.cpp Paste pattern) — then the loaded items are detached,
// matched by uuid against the live model (replace), lib-relinked for symbols, and
// committed. Runs inside the apply COROUTINE (see kicadCollabApplyItems).
// ysync bug 07 (UP side): an apply is queued for the room of ONE sheet but runs
// deferred on whatever screen is active when it runs. The JS binding stamps the
// envelope with its sheet's project-relative path; if that is not the screen
// shown now, adding the items here would put another sheet's items on this
// screen (8/28: the root's `(sheet …)` entries landed in a subsheet, which then
// referenced itself and failed to load). Drop it — the binding for the shown
// sheet reconciles on its own bind.
static bool applyTargetsShownSheet( SCH_EDIT_FRAME* aFrame, const json& aWire )
{
    auto it = aWire.find( "sheet" );

    if( it == aWire.end() || !it->is_string() )
        return true; // untagged (single-file tools, older clients): legacy behaviour

    const std::string want = it->get<std::string>();
    SCH_SCREEN*       cur = currentScreen( aFrame );
    const std::string have = cur ? toUtf8( cur->GetFileName() ) : std::string();

    if( have == want )
        return true;

    // `have` is absolute (MEMFS project dir + relative path); `want` is relative.
    if( have.size() > want.size() && have[have.size() - want.size() - 1] == '/'
        && have.compare( have.size() - want.size(), want.size(), want ) == 0 )
        return true;

    EM_ASM( { console.warn( "[collab] applyItems dropped: envelope sheet " + UTF8ToString( $0 )
                            + " != shown sheet " + UTF8ToString( $1 ) ); },
            want.c_str(), have.c_str() );
    return false;
}


void doApplyItems( SCH_EDIT_FRAME* aFrame, const json& aPayload )
{
    if( !applyTargetsShownSheet( aFrame, aPayload ) )
        return;

    // ysync 0012 #2: (a) pending LOCAL intent reaches the doc before a remote
    // root replaces the native item it lives on — a flush queued behind this
    // apply would otherwise diff against a baseline that already contains the
    // remote content and silently drop the user's edit; (b) the payload was
    // rendered when its doc event fired, so resolve it to the doc's latest
    // content now that it actually executes (see resolveItemsWire).
    if( g_flushScheduled )
        flushDiff();

    const json aWire = pcbjam_collab::resolveItemsWire( aPayload );

    SCHEMATIC& sch = aFrame->Schematic();

    s_applyingRemote = true;

    SCH_COMMIT commit( aFrame );
    bool       staged = false;

    std::vector<std::string> touched;   // uuids this apply acts on (targeted rebaseline)

    // Owned by nobody once the SKIP_UNDO commit detaches them — freed after Push
    // (fields are hidden, not detached, so excluded). See doApply.
    std::vector<SCH_ITEM*> removedItems;

    for( const json& rid : aWire.value( "removed", json::array() ) )
    {
        SCH_SHEET_PATH path;
        KIID           id( wxString::FromUTF8( rid.get<std::string>().c_str() ) );

        touched.push_back( rid.get<std::string>() );

        if( SCH_ITEM* item = sch.ResolveItem( id, &path, /*allowNull*/ true ) )
        {
            commit.Remove( item, path.LastScreen() );

            if( item->Type() != SCH_FIELD_T )
                removedItems.push_back( item );

            staged = true;
        }
    }

    auto upsert = [&]( const json& w )
    {
        std::string sexpr = w.value( "sexpr", "" );

        if( sexpr.find_first_not_of( " \t\r\n" ) == std::string::npos )
            return;

        // Parse into a throwaway sheet exactly like clipboard paste does. The screen
        // is heap-allocated and owned by the sheet (freed with it).
        SCH_SHEET   tempSheet;
        SCH_SCREEN* tempScreen = new SCH_SCREEN( &sch );
        tempSheet.SetScreen( tempScreen );

        STRING_LINE_READER reader( sexpr, wxT( "collab-items" ) );
        SCH_IO_KICAD_SEXPR plugin;

        try
        {
            plugin.LoadContent( reader, &tempSheet );
        }
        catch( ... )
        {
            EM_ASM( { console.log( "[collab] eeschema applyItems: blob parse failed" ); } );
            return;
        }

        // Resolve a symbol's LIB_SYMBOL: prefer the blob's own (lib_symbols …) cache
        // (a clipboard-style blob carries it), else the live screen's — peers share
        // the same document so the definition is normally already present.
        auto findLib = [&]( SCH_SYMBOL* aSym ) -> LIB_SYMBOL*
        {
            wxString lookup = aSym->GetLibId().Format().wx_str();

            if( !aSym->UseLibIdLookup() )
                lookup = aSym->GetSchSymbolLibraryName();

            auto& tlibs = tempScreen->GetLibSymbols();
            auto  ti    = tlibs.find( lookup );

            if( ti != tlibs.end() )
                return new LIB_SYMBOL( *ti->second );

            auto& libs = aFrame->GetScreen()->GetLibSymbols();
            auto  li   = libs.find( lookup );

            if( li != libs.end() )
                return new LIB_SYMBOL( *li->second );

            return nullptr;
        };

        std::vector<SCH_ITEM*> loaded;

        for( SCH_ITEM* item : tempScreen->Items() )
            loaded.push_back( item );

        for( SCH_ITEM* item : loaded )
        {
            tempScreen->Remove( item );     // detach: tempSheet's dtor must not free it

            SCH_SHEET_PATH path;

            if( SCH_ITEM* existing = sch.ResolveItem( item->m_Uuid, &path, /*allowNull*/ true ) )
            {
                commit.Remove( existing, path.LastScreen() );

                if( existing->Type() != SCH_FIELD_T )
                    removedItems.push_back( existing );
            }

            if( item->Type() == SCH_SYMBOL_T )
            {
                auto* sym = static_cast<SCH_SYMBOL*>( item );

                if( LIB_SYMBOL* lib = findLib( sym ) )
                    sym->SetLibSymbol( lib );
            }

            item->SetParent( &sch );
            touched.push_back( toUtf8( item->m_Uuid.AsString() ) );
            commit.Add( item, aFrame->GetScreen() );
            staged = true;
        }
    };

    for( const json& w : aWire.value( "added", json::array() ) )
        upsert( w );
    for( const json& w : aWire.value( "changed", json::array() ) )
        upsert( w );

    // SKIP_UNDO: remote applies never land on the local undo stack (see doApply).
    if( staged )
        commit.Push( wxT( "Collaborative edit (items)" ), SKIP_UNDO );

    for( SCH_ITEM* item : removedItems )
        delete item;

    // Fold ONLY the applied uuids into the baseline (echo suppression), then flush:
    // anything else that now differs — a concurrent local edit, the connectivity
    // cleanup this apply's Push produced — broadcasts as a normal local diff
    // instead of being swallowed (bug 05).
    rebaselineTouched( aFrame, touched );
    s_applyingRemote = false;
    scheduleFlush();
}

// Test/PoC move (the SCH_COMMIT body for kicadCollabTestMoveFirst, deferred via CallAfter).
void collabTestMove( SCH_EDIT_FRAME* aFrame, SCH_ITEM* aItem, SCH_SCREEN* aScreen, int aDx,
                     int aDy )
{
    SCH_COMMIT commit( aFrame );
    commit.Modify( aItem, aScreen );
    aItem->Move( VECTOR2I( aDx, aDy ) );
    commit.Push( wxT( "Collab test move" ) );
}

} // namespace


// JS → C++. Apply a remote per-item delta by uuid, through SCH_COMMIT so connectivity/
// ERC/hierarchy recompute the same way a UI edit would (0003 §apply).
//
// SCH_COMMIT must run on the editor's main loop, not on this embind ccall or an
// emscripten_async_call/setTimeout callback: wxEvtHandler::CallAfter queues onto the
// app's pending-event list, which the wasm main loop drains every frame via
// ProcessPendingEvents() (src/wasm/evtloop.cpp) — i.e. the exact context real UI edits
// run in. So defer the whole mutation there.
void schCollabApply( std::string aJson )
{
    // Open-in-flight guard (open_gate.h): never touch the model while a
    // kicadOpenFile chain is suspended mid-load — commits/virtuals would walk
    // a half-built schematic mid-mutation.
    // Callers gate on kicadOpenFileBusy; fuzzed by tests/kicad/collab-load-fuzz.spec.ts.
    if( pcbjam_open::busy() )
        return;

    json delta = json::parse( aJson, nullptr, /*allow_exceptions*/ false );

    if( delta.is_discarded() )
        return;

    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
        return;

    // Defer to the editor's main loop + apply coroutine (runOnCoroutine) so SCH_COMMIT runs
    // like a normal edit: a commit body that suspends (connectivity/GAL work) returns early
    // from COROUTINE::Call, so applies must serialize with each other and with local edits
    // or they interleave on shared commit/listener state. (0007, drift-trio #10.)
    pcbjam_collab::runOnCoroutine( fr, [fr, delta]() { doApply( fr, delta ); } );
}


// JS pull of the full current model as an all-"added" delta (seed/baseline). Also
// registers the change listener on first call.
std::string schCollabSnapshot()
{
    if( pcbjam_open::busy() ) // open in flight (open_gate.h) — see schCollabApply
        return json{ { "added", json::array() }, { "changed", json::array() },
                     { "removed", json::array() } }.dump();

    ensureBridge();
    json added = snapshotItems( schFrame() );

    // Seed the diff baseline to exactly the model we're handing out, so the first local edit
    // diffs against this snapshot (and we don't re-broadcast the whole model).
    rebaseline();

    return json{ { "added", added }, { "changed", json::array() },
                 { "removed", json::array() } }.dump();
}


// JS → C++, v2 items wire. Same CallAfter + COROUTINE context as kicadCollabApply
// (LoadContent + SCH_COMMIT must run where native edits run).
void schCollabApplyItems( std::string aJson )
{
    if( pcbjam_open::busy() ) // open in flight (open_gate.h) — see schCollabApply
        return;

    json wire = json::parse( aJson, nullptr, /*allow_exceptions*/ false );

    if( wire.is_discarded() )
        return;

    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
        return;

    pcbjam_collab::runOnCoroutine( fr, [fr, wire]() { doApplyItems( fr, wire ); } );
}


// JS pull of the ACTIVE screen's model as an all-"added" v2 items wire: one clipboard-
// style blob per item on the current sheet (one collab room == one .kicad_sch screen).
// Registers the listener + rebaselines exactly like kicadCollabSnapshot.
std::string schCollabSnapshotItems()
{
    if( pcbjam_open::busy() ) // open in flight (open_gate.h) — see schCollabApply
        return json{ { "added", json::array() }, { "changed", json::array() },
                     { "removed", json::array() } }.dump();

    SCH_EDIT_FRAME* fr = schFrame();

    json added = json::array();

    if( fr )
    {
        ensureBridge();

        if( SCH_SCREEN* screen = currentScreen( fr ) )
        {
            for( SCH_ITEM* item : screen->Items() )
            {
                // Same exclusions as snapshotByUuid/blobFor: markers are not
                // document content, and an empty blob (unformattable type)
                // must never seed a hollow envelope.
                if( item->Type() == SCH_MARKER_T )
                    continue;

                std::string sexpr = itemBlob( fr, item );

                if( sexpr.empty() )
                    continue;

                added.push_back( json{ { "sexpr", sexpr }, { "parent", nullptr } } );
            }
        }

        rebaseline();
    }

    return json{ { "added", added }, { "changed", json::array() },
                 { "removed", json::array() } }.dump();
}


// Test/PoC helper: move the first schematic item by (dx,dy) IU via a real SCH_COMMIT,
// firing the listener — a deterministic local edit for the two-tab demo / e2e.
// Returns the moved item's uuid.
std::string schCollabTestMoveFirst( int aDx, int aDy )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
        return "";

    SCHEMATIC& sch = fr->Schematic();

    for( const SCH_SHEET_PATH& path : sch.Hierarchy() )
    {
        SCH_SCREEN* screen = const_cast<SCH_SHEET_PATH&>( path ).LastScreen();

        if( !screen )
            continue;

        for( SCH_ITEM* item : screen->Items() )
        {
            fr->CallAfter( [fr, item, screen, aDx, aDy]() { collabTestMove( fr, item, screen, aDx, aDy ); } );
            return toUtf8( item->m_Uuid.AsString() );
        }
    }

    return "";
}


// Outcome of a deferred update-from-library run → window event
// `pcbjam:lib-update-done` {ok, updated, missing[]} (libs 0017 §2c).
static void emitLibUpdateDone( int aUpdated, const std::vector<std::string>& aMissing )
{
    nlohmann::json j;
    j["ok"] = true;
    j["updated"] = aUpdated;
    j["missing"] = aMissing;
    std::string s = j.dump();
    EM_ASM( {
        if( typeof window !== 'undefined' )
            window.dispatchEvent( new CustomEvent( 'pcbjam:lib-update-done',
                                                   { detail: JSON.parse( UTF8ToString( $0 ) ) } ) );
    }, s.c_str() );
}

// How many placed instances of a library symbol the open schematic holds —
// the JS lib-sync bridge asks after a remote lib update so the editor chrome
// can warn "a symbol you are using changed" (placed SCH_SYMBOLs keep their
// embedded copy across a lib reload, so the user must update explicitly).
// Counts across all unique screens of the hierarchy; 0 without a schematic
// frame (symbol editor / viewer sessions).
// Re-read the named library symbols into every placed instance — the headless
// core of Tools ▸ Update Symbols from Library… (DIALOG_CHANGE_SYMBOLS::
// processSymbols with the dialog's defaults: keep field text/positions, take
// the new body/pins/attributes), scoped to `aNamesJson` of `aLibNickname`.
// Runs as a normal SCH_COMMIT on the frame's coroutine so peers receive it as
// an ordinary edit. Returns {ok, updated, missing[]}. (libs 0017 §2c)
std::string schUpdateFromLibrary( std::string aLibNickname, std::string aNamesJson )
{
    nlohmann::json  out;
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
    {
        out["ok"] = false;
        out["error"] = "no schematic frame";
        return out.dump();
    }

    std::set<wxString> names;

    try
    {
        for( const auto& n : nlohmann::json::parse( aNamesJson ) )
            names.insert( wxString::FromUTF8( n.get<std::string>().c_str() ) );
    }
    catch( ... )
    {
        out["ok"] = false;
        out["error"] = "bad names";
        return out.dump();
    }

    const wxString           lib = wxString::FromUTF8( aLibNickname.c_str() );
    pcbjam_collab::runOnCoroutine( fr, [fr, lib, names]()
    {
        int                      updated = 0;
        std::vector<std::string> missing;
        SCH_COMMIT               commit( fr );
        SCH_SCREENS screens( fr->Schematic().Root() );

        for( SCH_SCREEN* screen = screens.GetFirst(); screen; screen = screens.GetNext() )
        {
            std::vector<SCH_SYMBOL*> targets;

            for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
            {
                SCH_SYMBOL*   sym = static_cast<SCH_SYMBOL*>( item );
                const LIB_ID& id = sym->GetLibId();

                if( wxString( id.GetLibNickname() ) == lib && names.count( wxString( id.GetLibItemName() ) ) )
                    targets.push_back( sym );
            }

            for( SCH_SYMBOL* sym : targets )
            {
                LIB_SYMBOL* libSymbol = fr->GetLibSymbol( sym->GetLibId() );

                if( !libSymbol )
                {
                    missing.push_back( std::string( sym->GetLibId().Format().c_str() ) );
                    continue;
                }

                std::unique_ptr<LIB_SYMBOL> flattened = libSymbol->Flatten();

                if( flattened->GetUnitCount() < sym->GetUnit() )
                {
                    missing.push_back( std::string( sym->GetLibId().Format().c_str() ) );
                    continue;
                }

                // Same order as the dialog: remove, record, swap the lib symbol,
                // re-append (the screen's RTree + connectivity re-index).
                screen->Remove( sym );
                commit.Modified( sym, static_cast<SCH_SYMBOL*>( sym->Clone() ), screen );
                sym->SetLibSymbol( flattened.release() );
                sym->SetExcludedFromSim( sym->GetLibSymbolRef()->GetExcludedFromSim() );
                sym->SetExcludedFromBOM( sym->GetLibSymbolRef()->GetExcludedFromBOM() );
                sym->SetExcludedFromBoard( sym->GetLibSymbolRef()->GetExcludedFromBoard() );
                sym->SetShowPinNames( sym->GetLibSymbolRef()->GetShowPinNames() );
                sym->SetShowPinNumbers( sym->GetLibSymbolRef()->GetShowPinNumbers() );
                sym->SetSchSymbolLibraryName( wxEmptyString );
                screen->Append( sym );
                updated++;
            }
        }

        commit.Push( wxT( "Update symbols from library" ) );
        fr->GetCanvas()->Refresh();

        // The Symbol Editor's own copy (libs 0019 F3): re-open it from the
        // fresh library when unmodified; report and leave it with local edits.
        if( SYMBOL_EDIT_FRAME* se = dynamic_cast<SYMBOL_EDIT_FRAME*>(
                    fr->Kiway().Player( FRAME_SCH_SYMBOL_EDITOR, false ) ) )
        {
            if( LIB_SYMBOL* cur = se->GetCurSymbol() )
            {
                const wxString curName = cur->GetName();

                if( se->GetCurLib() == lib && names.count( curName ) )
                {
                    const LIB_ID id( lib, curName );

                    if( se->IsContentModified() )
                        missing.push_back( std::string( id.Format().c_str() )
                                           + " (open in the Symbol Editor with unsaved edits — save or revert it first)" );
                    else
                        se->LoadSymbol( id, 1, 1 );
                }
            }
        }

        emitLibUpdateDone( updated, missing );
    } );

    // The body runs deferred on the frame's coroutine (runOnCoroutine =
    // CallAfter): the caller awaits the `pcbjam:lib-update-done` window event
    // for the outcome.
    out["ok"] = true;
    out["queued"] = true;
    return out.dump();
}

// Standalone-eeschema shape of kicadUpdateFromLibrary(kind, lib, namesJson).
static std::string schUpdateFromLibraryShim( std::string aKind, std::string aLib, std::string aNames )
{
    if( aKind != "symbol" )
        return "{\"ok\":false,\"error\":\"kind not handled by this editor\"}";

    return schUpdateFromLibrary( aLib, aNames );
}

int schLibsSymbolUsage( std::string aLibNickname, std::string aSymbolName )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
        return 0;

    const LIB_ID target( wxString::FromUTF8( aLibNickname.c_str() ),
                         wxString::FromUTF8( aSymbolName.c_str() ) );

    int         count = 0;
    SCH_SCREENS screens( fr->Schematic().Root() );

    for( SCH_SCREEN* screen = screens.GetFirst(); screen; screen = screens.GetNext() )
    {
        for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
        {
            if( static_cast<SCH_SYMBOL*>( item )->GetLibId() == target )
                count++;
        }
    }

    // The Symbol Editor's open copy counts as "used" too (libs 0019 F3).
    if( SYMBOL_EDIT_FRAME* se = dynamic_cast<SYMBOL_EDIT_FRAME*>(
                fr->Kiway().Player( FRAME_SCH_SYMBOL_EDITOR, false ) ) )
    {
        if( LIB_SYMBOL* cur = se->GetCurSymbol() )
        {
            if( se->GetCurLib() == wxString( target.GetLibNickname() )
                && cur->GetName() == wxString( target.GetLibItemName() ) )
                count++;
        }
    }

    return count;
}


// Test helper: read an item's position by uuid as "x,y" (internal units).
std::string schCollabGetPos( std::string aId )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
        return "";

    KIID id( wxString::FromUTF8( aId.c_str() ) );

    if( SCH_ITEM* item = fr->Schematic().ResolveItem( id, nullptr, /*allowNull*/ true ) )
    {
        VECTOR2I p = item->GetPosition();
        return std::to_string( p.x ) + "," + std::to_string( p.y );
    }

    return "";
}


// ── ysync-review repro hooks ─────────────────────────────────────────────────
// Local-edit test hooks for the ysync-review repro e2e (docs/features/
// ysync-review on the ysync-review branch): each drives a REAL SCH_COMMIT via
// CallAfter + COROUTINE (the doApply wrapping), so the SCHEMATIC_LISTENER →
// flushDiff emit path runs exactly as for a UI edit. Each returns false when
// the uuid doesn't resolve, letting the spec distinguish "hook missed the
// item" from "differ missed the edit" (bug 04).

// Delete an item by uuid via a real SCH_COMMIT.
bool schCollabTestRemoveItem( std::string aId )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
        return false;

    SCH_SHEET_PATH path;
    SCH_ITEM* item = fr->Schematic().ResolveItem( KIID( wxString::FromUTF8( aId.c_str() ) ),
                                                  &path, /*allowNull*/ true );

    if( !item )
        return false;

    SCH_SCREEN* screen = path.LastScreen();

    pcbjam_collab::runOnCoroutine( fr, [fr, item, screen]() {
        SCH_COMMIT commit( fr );
        commit.Remove( item, screen );
        commit.Push( wxT( "Collab test remove" ) );
    } );

    return true;
}

// Rotate an item in place (aDeg snapped to 90° CCW steps) — bug 04: a symbol's
// GetPosition() is unchanged by an in-place rotation and its json carries no
// orientation, so the rotation is invisible to the scalar differ.
bool schCollabTestRotateItem( std::string aId, double aDeg )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
        return false;

    SCH_SHEET_PATH path;
    SCH_ITEM* item = fr->Schematic().ResolveItem( KIID( wxString::FromUTF8( aId.c_str() ) ),
                                                  &path, /*allowNull*/ true );

    if( !item )
        return false;

    SCH_SCREEN* screen = path.LastScreen();
    int         steps = ( (int) ( aDeg / 90.0 + ( aDeg >= 0 ? 0.5 : -0.5 ) ) % 4 + 4 ) % 4;

    pcbjam_collab::runOnCoroutine( fr, [fr, item, screen, steps]() {
        SCH_COMMIT commit( fr );
        commit.Modify( item, screen );

        for( int i = 0; i < steps; ++i )
            item->Rotate( item->GetPosition(), /*aRotateCCW*/ true );

        commit.Push( wxT( "Collab test rotate" ) );
    } );

    return true;
}

// Run Edit>Undo / read the undo depth — miss 09; frame-generic, collab_common.h.
bool schCollabTestUndo()
{
    return pcbjam_collab::testUndo( schFrame() );
}

int schCollabTestUndoDepth()
{
    return pcbjam_collab::testUndoDepth( schFrame() );
}

// Set a symbol's Value field text — bug 04: fields live inside the symbol (not
// in screen->Items()) and the symbol json carries no field text, so the most
// common schematic edit after moving things never syncs.
bool schCollabTestSetFieldText( std::string aId, std::string aText )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
        return false;

    SCH_SHEET_PATH path;
    SCH_ITEM* item = fr->Schematic().ResolveItem( KIID( wxString::FromUTF8( aId.c_str() ) ),
                                                  &path, /*allowNull*/ true );

    if( !item || item->Type() != SCH_SYMBOL_T )
        return false;

    SCH_SYMBOL* sym = static_cast<SCH_SYMBOL*>( item );
    SCH_SCREEN* screen = path.LastScreen();
    wxString    text = wxString::FromUTF8( aText.c_str() );

    pcbjam_collab::runOnCoroutine( fr, [fr, sym, screen, text]() {
        SCH_COMMIT commit( fr );
        commit.Modify( sym, screen );
        sym->SetValueFieldText( text );
        commit.Push( wxT( "Collab test field text" ) );
    } );

    return true;
}


// ── drift-trio phase B action hooks (standalone-hardening 0008 §5) ───────────
// Creation/mutation primitives for the trio harness's action catalog. Each
// drives a REAL SCH_COMMIT on the apply coroutine, so the SCHEMATIC_LISTENER →
// flushDiff emit path runs exactly as for a UI edit. Names are tool-unique
// (registered outside the KICAD_MERGED_EMBIND guard — same convention as
// kicadCollabTestSetFieldText), so the merged image needs no dispatcher.

// Commit a freshly-built item onto the current screen; returns its uuid.
static std::string schCollabTestCommitAdd( SCH_ITEM* aItem, const wxChar* aMsg )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
    {
        delete aItem;
        return "";
    }

    SCH_SCREEN* screen = fr->GetScreen();

    // SCH_COMMIT::Push finds the SCHEMATIC via the ITEM's parent chain and
    // silently skips every listener notification (OnItemsAdded → our emit)
    // when it comes up null — a bare `new SCH_ITEM` has no parent yet, so
    // parent it like the drawing tools do before staging.
    aItem->SetParent( screen );

    std::string id = toUtf8( aItem->m_Uuid.AsString() );
    wxString    msg( aMsg );

    pcbjam_collab::runOnCoroutine( fr, [fr, aItem, screen, msg]() {
        SCH_COMMIT commit( fr );
        commit.Add( aItem, screen );
        commit.Push( msg );
    } );

    return id;
}

std::string schCollabTestAddWire( int aX1, int aY1, int aX2, int aY2 )
{
    SCH_LINE* line = new SCH_LINE( VECTOR2I( aX1, aY1 ), LAYER_WIRE );
    line->SetEndPoint( VECTOR2I( aX2, aY2 ) );
    return schCollabTestCommitAdd( line, wxT( "Collab test add wire" ) );
}

std::string schCollabTestAddJunction( int aX, int aY )
{
    return schCollabTestCommitAdd( new SCH_JUNCTION( VECTOR2I( aX, aY ) ),
                                   wxT( "Collab test add junction" ) );
}

std::string schCollabTestAddNoConnect( int aX, int aY )
{
    return schCollabTestCommitAdd( new SCH_NO_CONNECT( VECTOR2I( aX, aY ) ),
                                   wxT( "Collab test add no-connect" ) );
}

// aKind: "label" | "global" | "hier".
std::string schCollabTestAddLabel( std::string aKind, std::string aText, int aX, int aY )
{
    wxString        txt = wxString::FromUTF8( aText.c_str() );
    VECTOR2I        pos( aX, aY );
    SCH_LABEL_BASE* label;

    if( aKind == "global" )
        label = new SCH_GLOBALLABEL( pos, txt );
    else if( aKind == "hier" )
        label = new SCH_HIERLABEL( pos, txt );
    else
        label = new SCH_LABEL( pos, txt );

    return schCollabTestCommitAdd( label, wxT( "Collab test add label" ) );
}

// Place another instance of a symbol ALREADY in the schematic's lib_symbols
// cache (the harness fixtures embed their defs — no external lib needed).
std::string schCollabTestAddSymbol( std::string aLibId, int aX, int aY, std::string aRef )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
        return "";

    SCH_SCREEN* screen = fr->GetScreen();
    wxString    libIdStr = wxString::FromUTF8( aLibId.c_str() );
    LIB_SYMBOL* libSym = nullptr;

    for( const auto& [name, sym] : screen->GetLibSymbols() )
    {
        if( name == libIdStr )
        {
            libSym = sym;
            break;
        }
    }

    if( !libSym )
        return "";

    LIB_ID libId;

    if( libId.Parse( libIdStr ) >= 0 )
        return "";

    SCH_SHEET_PATH& sheet = fr->GetCurrentSheet();
    SCH_SYMBOL* sym = new SCH_SYMBOL( *libSym, libId, &sheet, 1, 1, VECTOR2I( aX, aY ) );
    sym->SetParent( screen );   // Push's SCHEMATIC lookup — see schCollabTestCommitAdd
    sym->SetRef( &sheet, wxString::FromUTF8( aRef.c_str() ) );

    std::string id = toUtf8( sym->m_Uuid.AsString() );

    pcbjam_collab::runOnCoroutine( fr, [fr, sym, screen]() {
        SCH_COMMIT commit( fr );
        commit.Add( sym, screen );
        commit.Push( wxT( "Collab test add symbol" ) );
    } );

    return id;
}

// By-uuid variant of MoveFirst (same CallAfter + devirtualized move path).
bool schCollabTestMoveSchItem( std::string aId, int aDx, int aDy )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
        return false;

    SCH_SHEET_PATH probe;
    if( !fr->Schematic().ResolveItem( KIID( wxString::FromUTF8( aId.c_str() ) ), &probe,
                                      /*allowNull*/ true ) )
        return false;

    // Re-resolve when the deferred body runs: a remote remove can apply in
    // between and the captured pointer would be dangling — the commit would
    // resurrect a deleted item (drift-trio S4 move-vs-delete). Vanished =>
    // the move loses, silently.
    fr->CallAfter( [fr, aId, aDx, aDy]() {
        SCH_SHEET_PATH path;
        SCH_ITEM* item = fr->Schematic().ResolveItem( KIID( wxString::FromUTF8( aId.c_str() ) ),
                                                      &path, /*allowNull*/ true );

        if( item )
            collabTestMove( fr, item, path.LastScreen(), aDx, aDy );
    } );
    return true;
}

bool schCollabTestMirrorSchItem( std::string aId, bool aHorizontal )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
        return false;

    SCH_SHEET_PATH path;
    SCH_ITEM* item = fr->Schematic().ResolveItem( KIID( wxString::FromUTF8( aId.c_str() ) ),
                                                  &path, /*allowNull*/ true );

    if( !item )
        return false;

    pcbjam_collab::runOnCoroutine( fr, [fr, aId, aHorizontal]() {   // re-resolve on the coroutine (S4)
        SCH_SHEET_PATH path;
        SCH_ITEM* live = fr->Schematic().ResolveItem( KIID( wxString::FromUTF8( aId.c_str() ) ),
                                                      &path, /*allowNull*/ true );

        if( !live )
            return;

        SCH_COMMIT commit( fr );
        commit.Modify( live, path.LastScreen() );

        if( aHorizontal )
            live->MirrorHorizontally( live->GetPosition().x );
        else
            live->MirrorVertically( live->GetPosition().y );

        commit.Push( wxT( "Collab test mirror" ) );
    } );

    return true;
}

// Duplicate (fresh uuids — exercises the Clone()/uuid-churn drift class).
std::string schCollabTestDuplicateSchItem( std::string aId, int aDx, int aDy )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
        return "";

    SCH_SHEET_PATH path;
    SCH_ITEM* item = fr->Schematic().ResolveItem( KIID( wxString::FromUTF8( aId.c_str() ) ),
                                                  &path, /*allowNull*/ true );

    if( !item )
        return "";

    SCH_SCREEN* screen = path.LastScreen();
    SCH_ITEM*   dup = item->Duplicate( /*addToParentGroup*/ false );
    dup->Move( VECTOR2I( aDx, aDy ) );

    std::string id = toUtf8( dup->m_Uuid.AsString() );

    pcbjam_collab::runOnCoroutine( fr, [fr, dup, screen]() {
        SCH_COMMIT commit( fr );
        commit.Add( dup, screen );
        commit.Push( wxT( "Collab test duplicate" ) );
    } );

    return id;
}


// Programmatically save the in-memory schematic to a .kicad_sch file, without
// driving the Save As dialog — eeschema's analogue of pl_editor's
// kicadSaveDrawingSheet. Serializes the root sheet via the same SCH_IO_KICAD_SEXPR
// writer eeschema uses, so a test can read the file back from MEMFS and assert the
// file ⇄ Y.Doc round trip (README §A; feature 0004). Single-sheet scope: the round-
// trip fixtures are flat schematics; saving the root sheet writes the whole model.
// C++ → JS save notification (standalone-hardening save routing). Called from the
// kicad fork's save chokepoint (SCH_EDIT_FRAME::saveSchematicFile) after a
// successful write to MEMFS, so the web app can route the saved bytes onward
// (API upload, local-disk write-back, download). No-op without a JS listener.
// KICAD_MERGED_EMBIND: identical definition in pcbnew_embind.cpp; the merged image
// gets the one in kicad_editor_embind.cpp (both fork save chokepoints call it).
#ifndef KICAD_MERGED_EMBIND
extern "C" void kicadCollabOnSave( const char* aPath )
{
    EM_ASM( {
        if( window.kicadCollab && window.kicadCollab.onSave )
        {
            // A throwing listener must never unwind the wasm frame that called it: under
            // JSPI that rejects the running coroutine's entry (findings P-1).
            try { window.kicadCollab.onSave( UTF8ToString( $0 ) ); }
            catch( e ) { console.error( '[pcbjam collab] onSave listener threw', e ); }
        }
    }, aPath );
}
#endif // !KICAD_MERGED_EMBIND

// ── presence entry points (collab-presence 0003 — eeschema port of the 0002 set) ────────────

// Install the presence input hooks on the GAL canvas + the fork's soft-lock
// query (idempotent) — shared core. The canvas is the same window across
// sheet navigation, so one install serves the whole session.
void schCollabPresenceStart()
{
    // Presence needs the document listener too (findings Y-1/Y-3: doc changes
    // repaint peers' shapes + re-check the local selection) — collab normally
    // registers it first via snapshot/apply, but presence must not depend on
    // that ordering (test harnesses, presence-only sessions).
    ensureBridge();
    presenceCore().start();
}

// JS → C++: full remote-peers snapshot (same wire as pcbnew's kicadCollabSetRemote).
// Rooms are per-sheet, so the JS rebind pushes a fresh (or empty) snapshot on every
// sheet switch — peers here are always this sheet's.
void schCollabSetRemote( std::string aJson )
{
    presenceCore().setRemote( aJson );
}

// JS → C++: cursor-only update for the peers of the last snapshot (findings Y-4).
void schCollabSetRemoteCursors( std::string aJson )
{
    presenceCore().setRemoteCursors( aJson );
}

// JS → C++ (collab-presence 0005): comment pin dots (same wire as pcbnew).
void schCollabSetPins( std::string aJson )
{
    presenceCore().setPins( aJson );
}

// JS → C++ (presence tuner): live-patch the overlay STYLE and repaint —
// see collab_presence_style.h + pcbnew's counterpart.
void schCollabSetStyle( std::string aJson )
{
    presenceCore().setStyle( aJson );
}

// JS → C++ (comments-ux 0002 F4): live color-theme switch (see pcbjam_theme.h).
void schSetColorTheme( std::string aTheme )
{
    pcbjam_theme::setColorTheme( schFrame(), aTheme );
}

// Pre-main chrome appearance seed (called at onRuntimeInitialized).
void schSetDarkChrome( bool aDark )
{
    pcbjam_theme::setDarkChromeFlag( aDark );
}

// Tuner helper: a VARIED demo-selection set for the current sheet — smallest +
// largest symbol and two bundles of wires (net-ish), mirroring pcbnew's
// pcbCollabTestDemoSet so the style preview shows the range of shapes.
std::string schCollabTestDemoSet()
{
    SCH_EDIT_FRAME* fr = schFrame();

    json groups = json::array();

    if( fr )
    {
        if( SCH_SCREEN* screen = currentScreen( fr ) )
        {
            SCH_SYMBOL* smallest = nullptr;
            SCH_SYMBOL* largest  = nullptr;
            double      minA = 0, maxA = 0;
            std::vector<std::string> wires;

            for( SCH_ITEM* item : screen->Items() )
            {
                if( item->Type() == SCH_SYMBOL_T )
                {
                    BOX2I  bb = item->GetBoundingBox();
                    double a  = (double) bb.GetWidth() * bb.GetHeight();

                    auto* sym = static_cast<SCH_SYMBOL*>( item );

                    if( !smallest || a < minA ) { smallest = sym; minA = a; }
                    if( !largest || a > maxA )  { largest = sym;  maxA = a; }
                }
                else if( item->Type() == SCH_LINE_T && wires.size() < 8 )
                {
                    wires.push_back( toUtf8( item->m_Uuid.AsString() ) );
                }
            }

            if( smallest )
                groups.push_back( { { "label", "symbol small" },
                                    { "ids", { toUtf8( smallest->m_Uuid.AsString() ) } } } );

            if( largest && largest != smallest )
                groups.push_back( { { "label", "symbol large" },
                                    { "ids", { toUtf8( largest->m_Uuid.AsString() ) } } } );

            if( wires.size() >= 2 )
            {
                size_t half = wires.size() / 2;
                groups.push_back( { { "label", "wires A" },
                                    { "ids", std::vector<std::string>( wires.begin(),
                                                                       wires.begin() + half ) } } );
                groups.push_back( { { "label", "wires B" },
                                    { "ids", std::vector<std::string>( wires.begin() + half,
                                                                       wires.end() ) } } );
            }
        }
    }

    return json{ { "groups", groups } }.dump();
}

// Test/tuner helper: the first N item uuids of the CURRENT sheet — real,
// resolvable KIIDs for synthetic remote-selection previews.
std::string schCollabTestListItems( int aCount )
{
    SCH_EDIT_FRAME* fr = schFrame();

    json out = json::array();

    if( fr )
    {
        if( SCH_SCREEN* screen = currentScreen( fr ) )
        {
            for( SCH_ITEM* item : screen->Items() )
            {
                if( (int) out.size() >= aCount )
                    break;

                out.push_back( toUtf8( item->m_Uuid.AsString() ) );
            }
        }
    }

    return out.dump();
}

// JS → C++ (0005): pan the view to a world position (comment panel "jump to pin").
void schCollabSetViewport( double aCx, double aCy )
{
    presenceCore().panTo( aCx, aCy );
}

// JS → C++ (0008 follow-user): fit a leader's world rect into this canvas.
void schCollabFitViewport( double aCx, double aCy, double aHalfW, double aHalfH )
{
    presenceCore().fitViewport( aCx, aCy, aHalfW, aHalfH );
}

// JS pull of the current viewport transform: `{cx,cy,scale,w,h}` with scale = px per
// IU via the GAL matrix (GetScale() is the zoom, not px/IU — pcbnew 0002 lesson).
std::string schCollabGetViewport()
{
    return presenceCore().viewportJson();
}

// JS pull of the CURRENT selection's uuids (presence seed + e2e no-leak probe).
std::string schCollabGetSelection()
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
        return "[]";

    return presenceCore().selectionUuids( fr ).dump();
}

// JS pull of the current selection in the 0006 payload shape. eeschema has no
// footprint paths — the uuids ARE the symbol uuids — but the export keeps the
// merged image's kicadCollabGetSelectionFull contract uniform across editors.
std::string schCollabGetSelectionFull()
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
        return "{\"uuids\":[],\"fpPaths\":[]}";

    json payload;
    payload["uuids"]   = presenceCore().selectionUuids( fr );
    payload["fpPaths"] = json::array();
    return payload.dump();
}

// Test probe (0006): the schematic-item uuids the current peers' cross-app
// selections resolve to ON THE CURRENT SHEET — same resolveXsel the render uses.
std::string schCollabTestGetCrossMapped()
{
    json arr = json::array();

    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
        return arr.dump();

    for( const pcbjam_presence::PEER& peer : presenceCore().peers )
    {
        for( SCH_ITEM* item : resolveXsel( fr, peer ) )
            arr.push_back( toUtf8( item->m_Uuid.AsString() ) );
    }

    return arr.dump();
}

// Test helper: REALLY select the current sheet's first item through the selection
// tool, then run the presence check (programmatic selects close no canvas event).
std::string schCollabTestSelectFirst()
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
        return "";

    SCH_SCREEN* screen = currentScreen( fr );

    if( !screen )
        return "";

    SCH_ITEM* target = nullptr;

    for( SCH_ITEM* item : screen->Items() )
    {
        target = item;
        break;
    }

    if( !target )
        return "";

    presenceCore().selectItem( target );

    return toUtf8( target->m_Uuid.AsString() );
}

// Test helper (0006): REALLY select the first SYMBOL on the current sheet —
// the deterministic cross-app subject (TestSelectFirst may pick a wire, which
// legitimately maps to nothing in pcbnew). Returns the uuid, "" without one.
std::string schCollabTestSelectComponent()
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
        return "";

    SCH_SCREEN* screen = currentScreen( fr );

    if( !screen )
        return "";

    SCH_ITEM* target = nullptr;

    for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
    {
        // Power symbols (PWR_FLAG, GND, …) legitimately have no footprint —
        // they'd make the cross-app subject map to nothing by construction.
        if( static_cast<SCH_SYMBOL*>( item )->IsPower() )
            continue;

        target = item;
        break;
    }

    if( !target )
        return "";

    presenceCore().selectItem( target );

    return toUtf8( target->m_Uuid.AsString() );
}

// Test helper (0007): select a SPECIFIC item by uuid — see pcbnew_embind.cpp
// for why the tiebreak specs must not rely on cross-tab iteration order.
bool schCollabTestSelectByUuid( std::string aUuid )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
        return false;

    SCH_SCREEN* screen = currentScreen( fr );

    if( !screen )
        return false;

    const wxString want = wxString::FromUTF8( aUuid.c_str() );

    for( SCH_ITEM* item : screen->Items() )
    {
        if( item->m_Uuid.AsString() == want )
        {
            presenceCore().selectItem( item );
            return true;
        }
    }

    return false;
}

// JS → C++ (0007): tiebreak release — see pcbnew_embind.cpp for the design
// (cancel-interactive-if-a-tool-holds-them → selective unselect → infobar →
// forced re-emit).
void schCollabReleaseSelection( std::string aUuidsJson, std::string aHolder )
{
    presenceCore().releaseSelection( aUuidsJson, aHolder );
}

// Test probe (0007): the current remote soft-lock set as `[{uuid, name}]`.
std::string schCollabTestGetLocked()
{
    return presenceCore().locksJson();
}

// Test helper: clear the selection through the tool + run the presence check.
bool schCollabTestClearSelection()
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
        return false;

    fr->CallAfter( [fr]() {
        // ClearSelection is not on the shared SELECTION_TOOL base — the one
        // presence hook that stays editor-typed.
        if( SCH_SELECTION_TOOL* st = fr->GetToolManager()->GetTool<SCH_SELECTION_TOOL>() )
        {
            st->ClearSelection();
            presenceCore().scheduleSelCheck();
        }
    } );

    return true;
}

// Merged-image dispatch probe (kicad_editor_embind.cpp): is the active top window the
// schematic editor? Counterpart of pcbnew_embind.cpp's pcbEditorActive().
bool schEditorActive()
{
    return schFrame() != nullptr;
}


void kicadSaveSchematic( std::string path )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
        return;

    SCHEMATIC& sch = fr->Schematic();

    // Save the CURRENT sheet, not Schematic().Root(): in the wasm open flow the
    // opened document is displayed as the current sheet but can sit under an
    // auto-created project root, so Root()'s own screen holds only a child-sheet
    // symbol (not the loaded items). GetCurrentSheet() is the screen the editor is
    // actually showing — the one whose items the snapshot/round-trip care about.
    SCH_SHEET* sheet = fr->GetCurrentSheet().Last();

    if( !sheet )
        sheet = &sch.Root();

    try
    {
        SCH_IO_KICAD_SEXPR io;
        io.SaveSchematicFile( wxString::FromUTF8( path.c_str() ), sheet, &sch );
    }
    catch( ... )
    {
        // Don't abort the wasm runtime on a save failure; the JS caller detects it
        // by the file being absent / empty.
    }
}


// Sheet navigator bridge (sheet-panel) — see sheetsStateJson. ensureBridge()
// so the OnSchSheetChanged push is wired even for sessions that never bind
// presence (read-only viewers).
std::string schSheetsGetTree()
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
        return "";

    ensureBridge();
    return sheetsStateJson( fr ).dump();
}

// Navigate to the sheet instance whose KIID path is aPath (as reported by
// kicadSheetsGetTree). Validated synchronously against the live hierarchy; the
// switch itself runs on the coroutine through the navigate tool's changeSheet
// action — the same route the wx hierarchy pane takes, so back/forward history
// stays consistent. DisplayCurrentSheet fires OnSchSheetChanged → the collab
// sheet-manager rebinds its room and the panel gets its onSheetsState push.
// The action is on the read-only allowlist (view-only), so viewers can navigate.
bool schSheetsEnter( const std::string& aPath )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
        return false;

    wxString       wanted = wxString::FromUTF8( aPath.c_str() );
    SCH_SHEET_LIST list = fr->Schematic().Hierarchy();
    auto           it = std::find_if( list.begin(), list.end(),
                                      [&]( const SCH_SHEET_PATH& p )
                                      {
                                          return p.PathAsString() == wanted;
                                      } );

    if( it == list.end() )
        return false;

    if( *it == fr->GetCurrentSheet() )
    {
        emitSheetsState( fr );
        return true;
    }

    SCH_SHEET_PATH target = *it;

    pcbjam_collab::runOnCoroutine( fr, [fr, target]() {
        SCH_SHEET_PATH path = target;
        fr->GetToolManager()->RunAction<SCH_SHEET_PATH*>( SCH_ACTIONS::changeSheet, &path );
        // changeSheet already pushed via OnSchSheetChanged; a no-op path
        // (already there / rejected) still leaves the panel consistent.
        emitSheetsState( fr );
    } );

    return true;
}

static bool kicadCollabBusyProbe()
{
    return pcbjam_collab::applyBusy() || !pcbjam_collab::applyQueue().empty();
}

// ── Interactive placement of an imported symbol (Import-from-file panel) ───
// `aSexpr` is the clipboard-dialect blob the panel already builds for
// kicadCollabApplyItems: `(lib_symbols …)` + one `(symbol …)`. Instead of
// committing it at a JS-computed point with SKIP_UNDO (invisible to peers,
// not undoable), hand the parsed symbol to the stock placement tool: it hangs
// off the pointer exactly like a chooser pick, R rotates it, Esc cancels (the
// tool deletes it) and the click commits "Place Symbol" — on the undo stack,
// and broadcast by the collab bridge like any local placement.
// Returns {"ok":true} when the placement was queued; parse problems are
// logged from the coroutine like applyItems does.
std::string schPlaceImportedItem( std::string aSexpr )
{
    json out;

    if( pcbjam_open::busy() ) // open in flight (open_gate.h) — see schCollabApply
    {
        out["ok"] = false;
        out["error"] = "open in flight";
        return out.dump();
    }

    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
    {
        out["ok"] = false;
        out["error"] = "no schematic frame";
        return out.dump();
    }

    // Same CallAfter + COROUTINE context as kicadCollabApplyItems: LoadContent
    // and the placement tool must run where native edits run.
    pcbjam_collab::runOnCoroutine( fr, [fr, aSexpr]()
    {
        SCHEMATIC&  sch = fr->Schematic();
        SCH_SHEET   tempSheet;
        SCH_SCREEN* tempScreen = new SCH_SCREEN( &sch );
        tempSheet.SetScreen( tempScreen );

        STRING_LINE_READER reader( aSexpr, wxT( "import-item" ) );
        SCH_IO_KICAD_SEXPR plugin;

        try
        {
            plugin.LoadContent( reader, &tempSheet );
        }
        catch( ... )
        {
            EM_ASM( { console.log( "[import-item] eeschema: blob parse failed" ); } );
            return;
        }

        SCH_SYMBOL* sym = nullptr;

        for( SCH_ITEM* item : tempScreen->Items().OfType( SCH_SYMBOL_T ) )
        {
            sym = static_cast<SCH_SYMBOL*>( item );
            break;
        }

        if( !sym )
        {
            EM_ASM( { console.log( "[import-item] eeschema: no symbol in blob" ); } );
            return;
        }

        // The LIB_SYMBOL, resolved BEFORE detaching: SCH_SCREEN::Remove drops a
        // definition from the screen cache with its last user. Same rule as
        // SCH_EDITOR_CONTROL::Paste — the current screen's copy wins (it may
        // have diverged from the library), else the blob's own (lib_symbols …).
        const wxString lookup = sym->GetSchSymbolLibraryName();
        LIB_SYMBOL*    lib = nullptr;
        auto&          tlibs = tempScreen->GetLibSymbols();
        auto&          libs = fr->GetScreen()->GetLibSymbols();
        auto           li = libs.find( lookup );
        auto           ti = tlibs.find( lookup );

        if( li != libs.end() )
            lib = new LIB_SYMBOL( *li->second );
        else if( ti != tlibs.end() )
            lib = new LIB_SYMBOL( *ti->second );

        tempScreen->Remove( sym ); // detach: tempSheet's dtor must not free it

        if( !lib )
        {
            EM_ASM( { console.log( "[import-item] eeschema: lib symbol not found for " + UTF8ToString( $0 ) ); },
                    toUtf8( lookup ).c_str() );
            delete sym;
            return;
        }

        sym->SetLibSymbol( lib ); // takes ownership; SCH_SCREEN::Append caches it on commit
        sym->SetParent( &sch );
        sym->ClearFlags();

        // Fresh identity per placement: the same blob may be placed repeatedly.
        const_cast<KIID&>( sym->m_Uuid ) = KIID();

        // PlaceSymbol takes ownership of `sym` (deleted on cancel, committed on click).
        // m_Reannotate = true: the blob carries an unannotated "R?" reference.
        fr->GetToolManager()->RunAction<SCH_ACTIONS::PLACE_SYMBOL_PARAMS>(
                SCH_ACTIONS::placeSymbol, SCH_ACTIONS::PLACE_SYMBOL_PARAMS{ sym, true } );
    } );

    out["ok"] = true;
    return out.dump();
}

// Modified by Chirichella Inc. on 2026-11-07: the Editing window's four exports (island
// cc15: symbol.get, symbol.set, sheet.annotate, sheet.check). One symbol's properties read
// and written by uuid, the parts numbered and the sheet checked, all through KiCad's own
// classes (SCH_COMMIT, AnnotateSymbols and its reference lists, ERC_TESTER and ERC_REPORT),
// with no KiCad dialog on any road. Each takes closed JSON args; a false answer queues
// nothing. Registered outside the KICAD_MERGED_EMBIND guard: the names are eeschema-only.
namespace
{
/** A lowercase 8-4-4-4-12 uuid (KIID's string constructor takes any text, so this gate comes first). */
bool isLowerUuid( const std::string& aText )
{
    if( aText.size() != 36 )
        return false;

    for( size_t i = 0; i < aText.size(); ++i )
    {
        const char c = aText[i];

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

bool hasControlChar( const wxString& aText )
{
    for( wxUniChar c : aText )
    {
        if( c < 0x20 || c == 0x7f )
            return true;
    }

    return false;
}

/**
 * A JSON string as UTF-8 text of aMin to aMax characters (code points), or nothing.
 * Invalid UTF-8 is refused (wxString::FromUTF8 answers empty for it).
 */
std::optional<wxString> boundedText( const json& aValue, size_t aMin, size_t aMax )
{
    if( !aValue.is_string() )
        return std::nullopt;

    const std::string& raw = aValue.get_ref<const std::string&>();

    if( raw.find( '\0' ) != std::string::npos )
        return std::nullopt;

    wxString text = wxString::FromUTF8( raw.c_str(), raw.size() );

    if( !raw.empty() && text.IsEmpty() )
        return std::nullopt;

    if( text.length() < aMin || text.length() > aMax )
        return std::nullopt;

    return text;
}

/** A folder under aRoot (the plot export's rule): no "..", no trailing slash, nothing else. */
bool isRunDir( const std::string& aDir, const std::string& aRoot )
{
    return aDir.size() > aRoot.size() && aDir.compare( 0, aRoot.size(), aRoot ) == 0
           && aDir.find( ".." ) == std::string::npos && aDir.back() != '/'
           && aDir.find( '\0' ) == std::string::npos;
}

/** "lib:name", both halves present, as LIB_ID reads it; or empty. */
bool isFootprintId( const wxString& aText )
{
    if( aText.IsEmpty() )
        return true;

    if( hasControlChar( aText ) )
        return false;

    LIB_ID id;

    return id.Parse( UTF8( aText ) ) == -1 && !id.GetLibNickname().empty()
           && !id.GetLibItemName().empty();
}

struct SYMBOL_HIT
{
    SCH_SYMBOL*    symbol = nullptr;
    SCH_SHEET_PATH path;
};

/**
 * The symbol whose uuid is aUuid, on aSheetPath when that path holds it, else (aStrict
 * false, or aSheetPath empty) on the sheet the editor shows, else on the first path of the
 * hierarchy holding it. A reused sheet shares one SCH_SYMBOL between its paths; the path
 * picks the instance (its reference, unit and flags).
 */
SYMBOL_HIT resolveSymbol( SCH_EDIT_FRAME* aFrame, const std::string& aUuid,
                          const std::string& aSheetPath, bool aStrict )
{
    SYMBOL_HIT hit;

    if( !aFrame || !isLowerUuid( aUuid ) )
        return hit;

    const KIID                  id( wxString::FromUTF8( aUuid.c_str() ) );
    std::vector<SCH_SHEET_PATH> holding;
    SCH_SYMBOL*                 symbol = nullptr;

    for( const SCH_SHEET_PATH& path : aFrame->Schematic().Hierarchy() )
    {
        SCH_SCREEN* screen = path.LastScreen();

        if( !screen )
            continue;

        for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
        {
            if( item->m_Uuid == id )
            {
                symbol = static_cast<SCH_SYMBOL*>( item );
                holding.push_back( path );
                break;
            }
        }
    }

    if( !symbol )
        return hit;

    if( !aSheetPath.empty() )
    {
        const wxString wanted = wxString::FromUTF8( aSheetPath.c_str() );

        for( const SCH_SHEET_PATH& path : holding )
        {
            if( path.PathAsString().CmpNoCase( wanted ) == 0 )
            {
                hit.symbol = symbol;
                hit.path = path;
                return hit;
            }
        }

        if( aStrict )
            return hit;
    }

    const SCH_SHEET_PATH& shown = aFrame->GetCurrentSheet();

    for( const SCH_SHEET_PATH& path : holding )
    {
        if( path == shown )
        {
            hit.symbol = symbol;
            hit.path = path;
            return hit;
        }
    }

    hit.symbol = symbol;
    hit.path = holding.front();
    return hit;
}

/** The sheet's name as the editor shows it; the root carries none, so its file's name (sheetsStateJson's rule). */
wxString sheetNameOf( const SCH_SHEET_PATH& aPath )
{
    SCH_SHEET*  sheet = aPath.Last();
    SCH_SCREEN* screen = aPath.LastScreen();
    wxString    name = sheet ? sheet->GetName() : wxString();

    if( name.IsEmpty() && screen )
        name = wxFileName( screen->GetFileName() ).GetName();

    return name;
}

const char* fieldKind( const SCH_FIELD& aField )
{
    switch( aField.GetId() )
    {
    case FIELD_T::REFERENCE:   return "reference";
    case FIELD_T::VALUE:       return "value";
    case FIELD_T::FOOTPRINT:   return "footprint";
    case FIELD_T::DATASHEET:   return "datasheet";
    case FIELD_T::DESCRIPTION: return "description";
    default:                   return "user";
    }
}

/** The symbol's field named aName (KiCad's canonical name: Reference, Value, ... or the user field's own). */
SCH_FIELD* fieldNamed( SCH_SYMBOL* aSymbol, const wxString& aName )
{
    for( SCH_FIELD& field : aSymbol->GetFields() )
    {
        if( field.GetCanonicalName() == aName )
            return &field;
    }

    return nullptr;
}

/** Is aName taken on this symbol in any casing (a mandatory field's name included)? */
bool fieldNameTaken( SCH_SYMBOL* aSymbol, const wxString& aName )
{
    for( const SCH_FIELD& field : aSymbol->GetFields() )
    {
        if( field.GetCanonicalName().CmpNoCase( aName ) == 0 || field.GetName().CmpNoCase( aName ) == 0 )
            return true;
    }

    static const char* const mandatory[] = { "Reference", "Value", "Footprint", "Datasheet",
                                             "Description" };

    for( const char* name : mandatory )
    {
        if( aName.CmpNoCase( wxString::FromUTF8( name ) ) == 0 )
            return true;
    }

    return false;
}

/** 0, 90, 180 or 270 and the mirror, as KiCad's own GetOrientation decomposes the transform. */
void decomposeOrientation( const SCH_SYMBOL* aSymbol, int& aDegrees, char& aMirror )
{
    const int orientation = aSymbol->GetOrientation();
    const int mirror = orientation & ( SYM_MIRROR_X | SYM_MIRROR_Y );

    switch( orientation & ~( SYM_MIRROR_X | SYM_MIRROR_Y ) )
    {
    case SYM_ORIENT_90:  aDegrees = 90;  break;
    case SYM_ORIENT_180: aDegrees = 180; break;
    case SYM_ORIENT_270: aDegrees = 270; break;
    default:             aDegrees = 0;   break;
    }

    aMirror = ( mirror & SYM_MIRROR_X ) ? 'x' : ( ( mirror & SYM_MIRROR_Y ) ? 'y' : 'n' );
}

int orientationConstant( int aDegrees, char aMirror )
{
    int orientation = SYM_ORIENT_0;

    switch( aDegrees )
    {
    case 90:  orientation = SYM_ORIENT_90;  break;
    case 180: orientation = SYM_ORIENT_180; break;
    case 270: orientation = SYM_ORIENT_270; break;
    default:  break;
    }

    if( aMirror == 'x' )
        orientation += SYM_MIRROR_X;
    else if( aMirror == 'y' )
        orientation += SYM_MIRROR_Y;

    return orientation;
}

/** 1 for a single drawing, 2 for a symbol with exactly one alternate drawing (KiCad's De Morgan pair). */
int bodyStylesOf( const SCH_SYMBOL* aSymbol )
{
    return aSymbol->GetBodyStyleCount() == 2 ? 2 : 1;
}

int unitsOf( const SCH_SYMBOL* aSymbol )
{
    return std::max( 1, aSymbol->GetUnitCount() );
}

double roundMm( int aIu )
{
    return std::round( schIUScale.IUTomm( aIu ) * 10000.0 ) / 10000.0;
}

/** The island's SymbolSheet (spec 6.2) for one instance. */
json symbolSheetJson( SCH_EDIT_FRAME* aFrame, SCH_SYMBOL* aSymbol, const SCH_SHEET_PATH& aPath )
{
    SCHEMATIC& sch = aFrame->Schematic();

    std::vector<SCH_FIELD*> ordered;
    aSymbol->GetFields( ordered, /*aVisibleOnly*/ false );

    json fields = json::array();

    for( SCH_FIELD* field : ordered )
    {
        wxString value;

        switch( field->GetId() )
        {
        case FIELD_T::REFERENCE:
            value = aSymbol->GetRef( &aPath, false );
            break;

        case FIELD_T::VALUE:
            value = aSymbol->GetValue( false, &aPath, false, wxEmptyString );
            break;

        case FIELD_T::FOOTPRINT:
            value = aSymbol->GetFootprintFieldText( false, &aPath, false, wxEmptyString );
            break;

        default:
            value = sch.ConvertKIIDsToRefs( field->GetText() );
            break;
        }

        json f = json::object();
        f["name"] = toUtf8( field->GetCanonicalName() );
        f["value"] = toUtf8( value );
        f["shown"] = field->IsVisible();
        f["kind"] = fieldKind( *field );
        fields.push_back( std::move( f ) );
    }

    int  degrees = 0;
    char mirror = 'n';
    decomposeOrientation( aSymbol, degrees, mirror );

    const int      bodyStyles = bodyStylesOf( aSymbol );
    const VECTOR2I pos = aSymbol->GetPosition();

    json out = json::object();
    out["uuid"] = toUtf8( aSymbol->m_Uuid.AsString() );
    out["sheetPath"] = toUtf8( aPath.PathAsString() );
    out["sheetName"] = toUtf8( sheetNameOf( aPath ) );
    out["ref"] = toUtf8( aSymbol->GetRef( &aPath, false ) );
    out["value"] = toUtf8( aSymbol->GetValue( false, &aPath, false, wxEmptyString ) );
    out["footprint"] = toUtf8( aSymbol->GetFootprintFieldText( false, &aPath, false, wxEmptyString ) );
    out["libId"] = std::string( aSymbol->GetLibId().Format().c_str() );
    out["fields"] = std::move( fields );
    out["inBom"] = !aSymbol->GetExcludedFromBOM( &aPath, wxEmptyString );
    out["onBoard"] = !aSymbol->GetExcludedFromBoard( &aPath, wxEmptyString );
    out["inSim"] = !aSymbol->GetExcludedFromSim( &aPath, wxEmptyString );
    out["dnp"] = aSymbol->GetDNP( &aPath, wxEmptyString );
    out["rotation"] = degrees;
    out["mirror"] = mirror == 'x' ? "x" : ( mirror == 'y' ? "y" : "none" );
    out["x"] = roundMm( pos.x );
    out["y"] = roundMm( pos.y );
    out["unit"] = std::max( 1, aSymbol->GetUnitSelection( &aPath ) );
    out["units"] = unitsOf( aSymbol );
    out["bodyStyle"] = bodyStyles == 2 ? std::clamp( aSymbol->GetBodyStyle(), 1, 2 ) : 1;
    out["bodyStyles"] = bodyStyles;

    // The footprint filters of the symbol's own definition (the copy the schematic holds,
    // so no library is read): KiCad's chooser ranks footprints matching these first.
    json filters = json::array();

    if( const std::unique_ptr<LIB_SYMBOL>& lib = aSymbol->GetLibSymbolRef() )
    {
        for( const wxString& filter : lib->GetFPFilters() )
            filters.push_back( toUtf8( filter ) );
    }

    out["filters"] = std::move( filters );
    return out;
}

struct FIELD_EDIT
{
    wxString                name;
    std::optional<wxString> value;
    std::optional<bool>     shown;
};

/** One symbol.set, read and checked whole before anything is queued. */
struct SYMBOL_SET
{
    std::optional<wxString>   ref;
    std::optional<wxString>   value;
    std::optional<wxString>   footprint;
    std::vector<FIELD_EDIT>   fields;
    std::optional<FIELD_EDIT> addField;
    std::optional<wxString>   removeField;
    std::optional<bool>       inBom, onBoard, inSim, dnp;
    std::optional<int>        rotation;
    std::optional<char>       mirror;
    std::optional<double>     x, y;
    std::optional<int>        unit;
    std::optional<int>        bodyStyle;
};

constexpr size_t FIELD_TEXT_MAX = 1024;
constexpr double POSITION_MAX_MM = 50000.0;

/**
 * Reads aSet against aSymbol (whose fields, units and drawings bound what may be asked).
 * Answers false for any key, type or bound outside spec 11.2.
 */
bool readSymbolSet( const json& aSet, SCH_SYMBOL* aSymbol, SYMBOL_SET& aOut )
{
    if( !aSet.is_object() || aSet.empty() )
        return false;

    std::set<wxString> named;   // field names a fields[] entry or removeField touches

    for( const auto& [key, value] : aSet.items() )
    {
        if( key == "ref" )
        {
            aOut.ref = boundedText( value, 1, 64 );

            if( !aOut.ref || hasControlChar( *aOut.ref ) )
                return false;
        }
        else if( key == "value" )
        {
            if( !( aOut.value = boundedText( value, 0, FIELD_TEXT_MAX ) ) )
                return false;
        }
        else if( key == "footprint" )
        {
            aOut.footprint = boundedText( value, 0, FIELD_TEXT_MAX );

            if( !aOut.footprint || !isFootprintId( *aOut.footprint ) )
                return false;
        }
        else if( key == "fields" )
        {
            if( !value.is_array() || value.empty() || value.size() > 64 )
                return false;

            for( const json& entry : value )
            {
                if( !entry.is_object() || !entry.contains( "name" ) || entry.size() < 2 )
                    return false;

                FIELD_EDIT edit;

                for( const auto& [k, v] : entry.items() )
                {
                    if( k == "name" )
                    {
                        std::optional<wxString> name = boundedText( v, 1, 128 );

                        if( !name )
                            return false;

                        edit.name = *name;
                    }
                    else if( k == "value" )
                    {
                        if( !( edit.value = boundedText( v, 0, FIELD_TEXT_MAX ) ) )
                            return false;
                    }
                    else if( k == "shown" )
                    {
                        if( !v.is_boolean() )
                            return false;

                        edit.shown = v.get<bool>();
                    }
                    else
                    {
                        return false;
                    }
                }

                SCH_FIELD* field = fieldNamed( aSymbol, edit.name );

                if( !field || !named.insert( edit.name ).second )
                    return false;

                if( edit.value )
                {
                    if( field->GetId() == FIELD_T::REFERENCE
                            && ( edit.value->IsEmpty() || edit.value->length() > 64
                                 || hasControlChar( *edit.value ) ) )
                    {
                        return false;
                    }

                    if( field->GetId() == FIELD_T::FOOTPRINT && !isFootprintId( *edit.value ) )
                        return false;
                }

                aOut.fields.push_back( std::move( edit ) );
            }
        }
        else if( key == "addField" )
        {
            if( !value.is_object() || value.size() != 3 || !value.contains( "name" )
                    || !value.contains( "value" ) || !value.contains( "shown" )
                    || !value["shown"].is_boolean() )
            {
                return false;
            }

            std::optional<wxString> name = boundedText( value["name"], 1, 128 );
            std::optional<wxString> text = boundedText( value["value"], 0, FIELD_TEXT_MAX );

            if( !name || !text || hasControlChar( *name ) || fieldNameTaken( aSymbol, *name ) )
                return false;

            aOut.addField = FIELD_EDIT{ *name, *text, value["shown"].get<bool>() };
        }
        else if( key == "removeField" )
        {
            std::optional<wxString> name = boundedText( value, 1, 128 );

            if( !name )
                return false;

            SCH_FIELD* field = fieldNamed( aSymbol, *name );

            if( !field || field->IsMandatory() )
                return false;

            aOut.removeField = *name;
        }
        else if( key == "inBom" || key == "onBoard" || key == "inSim" || key == "dnp" )
        {
            if( !value.is_boolean() )
                return false;

            const bool on = value.get<bool>();

            if( key == "inBom" )
                aOut.inBom = on;
            else if( key == "onBoard" )
                aOut.onBoard = on;
            else if( key == "inSim" )
                aOut.inSim = on;
            else
                aOut.dnp = on;
        }
        else if( key == "rotation" )
        {
            if( !value.is_number_integer() )
                return false;

            const long long degrees = value.get<long long>();

            if( degrees != 0 && degrees != 90 && degrees != 180 && degrees != 270 )
                return false;

            aOut.rotation = static_cast<int>( degrees );
        }
        else if( key == "mirror" )
        {
            if( !value.is_string() )
                return false;

            const std::string& m = value.get_ref<const std::string&>();

            if( m == "none" )
                aOut.mirror = 'n';
            else if( m == "x" )
                aOut.mirror = 'x';
            else if( m == "y" )
                aOut.mirror = 'y';
            else
                return false;
        }
        else if( key == "x" || key == "y" )
        {
            if( !value.is_number() )
                return false;

            const double mm = value.get<double>();

            if( !std::isfinite( mm ) || std::fabs( mm ) > POSITION_MAX_MM )
                return false;

            ( key == "x" ? aOut.x : aOut.y ) = mm;
        }
        else if( key == "unit" )
        {
            if( !value.is_number_integer() )
                return false;

            const long long unit = value.get<long long>();

            if( unit < 1 || unit > unitsOf( aSymbol ) )
                return false;

            aOut.unit = static_cast<int>( unit );
        }
        else if( key == "bodyStyle" )
        {
            if( !value.is_number_integer() )
                return false;

            const long long style = value.get<long long>();

            if( style < 1 || style > bodyStylesOf( aSymbol ) )
                return false;

            aOut.bodyStyle = static_cast<int>( style );
        }
        else
        {
            return false;
        }
    }

    // One change per thing: a field named twice (by its own key and in fields[], or kept
    // and removed, or removed and added again) is ambiguous and refused.
    if( aOut.ref && named.count( wxS( "Reference" ) ) )
        return false;

    if( aOut.value && named.count( wxS( "Value" ) ) )
        return false;

    if( aOut.footprint && named.count( wxS( "Footprint" ) ) )
        return false;

    if( aOut.removeField && named.count( *aOut.removeField ) )
        return false;

    if( aOut.removeField && aOut.addField && aOut.removeField->CmpNoCase( aOut.addField->name ) == 0 )
        return false;

    return true;
}

/** Applies a read set on the apply coroutine, as ONE commit (one undo step). */
void applySymbolSet( SCH_EDIT_FRAME* aFrame, SCH_SYMBOL* aSymbol, const SCH_SHEET_PATH& aPath,
                     const SYMBOL_SET& aSet )
{
    SCHEMATIC&  sch = aFrame->Schematic();
    SCH_SCREEN* screen = aPath.LastScreen();
    SCH_COMMIT  commit( aFrame );

    commit.Modify( aSymbol, screen );

    bool shared = false;     // a change the other units of a multi-unit part must share
    bool reshaped = false;   // orientation, unit or drawing: the fields may need placing again

    auto setFieldValue =
            [&]( SCH_FIELD* aField, const wxString& aText )
            {
                switch( aField->GetId() )
                {
                case FIELD_T::REFERENCE:
                    aSymbol->SetRef( &aPath, aText );
                    break;

                case FIELD_T::VALUE:
                    aSymbol->SetValueFieldText( aText, &aPath, wxEmptyString );
                    shared = true;
                    break;

                case FIELD_T::FOOTPRINT:
                    aSymbol->SetFootprintFieldText( aText );
                    shared = true;
                    break;

                default:
                    aField->SetText( sch.ConvertRefsToKIIDs( aText ) );
                    shared = true;
                    break;
                }
            };

    if( aSet.ref )
        aSymbol->SetRef( &aPath, *aSet.ref );

    if( aSet.value )
        setFieldValue( aSymbol->GetField( FIELD_T::VALUE ), *aSet.value );

    if( aSet.footprint )
        setFieldValue( aSymbol->GetField( FIELD_T::FOOTPRINT ), *aSet.footprint );

    for( const FIELD_EDIT& edit : aSet.fields )
    {
        SCH_FIELD* field = fieldNamed( aSymbol, edit.name );

        if( !field )
            continue;

        if( edit.value )
            setFieldValue( field, *edit.value );

        if( edit.shown )
        {
            field->SetVisible( *edit.shown );
            shared = shared || field->GetId() != FIELD_T::REFERENCE;
        }
    }

    if( aSet.removeField )
    {
        aSymbol->RemoveField( *aSet.removeField );
        shared = true;
    }

    if( aSet.addField && !fieldNameTaken( aSymbol, aSet.addField->name ) )
    {
        // As the symbol properties dialog adds a row: at the symbol's anchor, the
        // reference's angle, the schematic's default text size, the next user ordinal.
        SCH_FIELD    added( aSymbol, FIELD_T::USER, aSet.addField->name );
        const int    size = sch.Settings().m_DefaultTextSize;
        const SCH_FIELD* reference = aSymbol->GetField( FIELD_T::REFERENCE );

        added.SetText( sch.ConvertRefsToKIIDs( *aSet.addField->value ) );
        added.SetVisible( *aSet.addField->shown );
        added.SetTextSize( VECTOR2I( size, size ) );

        if( reference )
            added.SetTextAngle( reference->GetTextAngle() );

        added.SetPosition( aSymbol->GetPosition() );
        added.SetOrdinal( aSymbol->GetNextFieldOrdinal() );

        if( SCH_FIELD* placed = aSymbol->AddField( added ) )
            placed->SetParent( aSymbol );

        shared = true;
    }

    if( aSet.inBom )
    {
        aSymbol->SetExcludedFromBOM( !*aSet.inBom, &aPath, wxEmptyString );
        shared = true;
    }

    if( aSet.onBoard )
    {
        aSymbol->SetExcludedFromBoard( !*aSet.onBoard, &aPath, wxEmptyString );
        shared = true;
    }

    if( aSet.inSim )
    {
        aSymbol->SetExcludedFromSim( !*aSet.inSim, &aPath, wxEmptyString );
        shared = true;
    }

    if( aSet.dnp )
    {
        aSymbol->SetDNP( *aSet.dnp, &aPath, wxEmptyString );
        shared = true;
    }

    if( aSet.unit )
    {
        aSymbol->SetUnitSelection( &aPath, *aSet.unit );
        aSymbol->SetUnit( *aSet.unit );
        reshaped = true;
    }

    if( aSet.bodyStyle )
    {
        aSymbol->SetBodyStyle( *aSet.bodyStyle );
        reshaped = true;
    }

    if( aSet.rotation || aSet.mirror )
    {
        // The properties dialog's way: the absolute rotation, then the mirror, from one
        // constant; the half not asked for keeps its current reading.
        int  degrees = 0;
        char mirror = 'n';
        decomposeOrientation( aSymbol, degrees, mirror );

        aSymbol->SetOrientation( orientationConstant( aSet.rotation.value_or( degrees ),
                                                      aSet.mirror.value_or( mirror ) ) );
        reshaped = true;
    }

    if( aSet.x || aSet.y )
    {
        VECTOR2I pos = aSymbol->GetPosition();

        if( aSet.x )
            pos.x = KiROUND( schIUScale.mmToIU( *aSet.x ) );

        if( aSet.y )
            pos.y = KiROUND( schIUScale.mmToIU( *aSet.y ) );

        aSymbol->SetPosition( pos );   // moves the fields with the body
    }

    // The edit tool places the fields again after the dialog when autoplacement is on;
    // here inside the same commit, so the one undo step takes it back too.
    if( reshaped && aFrame->eeconfig() && aFrame->eeconfig()->m_AutoplaceFields.enable )
    {
        AUTOPLACE_ALGO placed = aSymbol->GetFieldsAutoplaced();

        if( placed == AUTOPLACE_AUTO || placed == AUTOPLACE_MANUAL )
            aSymbol->AutoplaceFields( screen, placed );
    }

    // The dialog keeps the fields (all but the reference), the flags and the alternate
    // pins of every unit of a multi-unit part in step; same commit.
    if( shared && aSymbol->IsMultiUnit() )
        aSymbol->SyncOtherUnits( aPath, commit, nullptr, wxEmptyString );

    commit.Push( wxT( "Edit symbol" ) );
}

/** Every line a run reports, with its severity, in order. */
class RUN_LINES_REPORTER : public REPORTER
{
public:
    using REPORTER::Report;

    REPORTER& Report( const wxString& aText, SEVERITY aSeverity = RPT_SEVERITY_UNDEFINED ) override
    {
        REPORTER::Report( aText, aSeverity );
        wxString text = aText;
        text.Trim( true ).Trim( false );

        if( !text.IsEmpty() )
            m_lines.emplace_back( aSeverity, text );

        return *this;
    }

    json Lines() const
    {
        json lines = json::array();

        for( const auto& [severity, text] : m_lines )
        {
            json line = json::object();

            switch( severity )
            {
            case RPT_SEVERITY_ERROR:   line["severity"] = "error";   break;
            case RPT_SEVERITY_WARNING: line["severity"] = "warning"; break;
            case RPT_SEVERITY_ACTION:  line["severity"] = "action";  break;
            default:                   line["severity"] = "info";    break;
            }

            line["text"] = toUtf8( text );
            lines.push_back( std::move( line ) );
        }

        return lines;
    }

    std::vector<std::pair<SEVERITY, wxString>> m_lines;
};

/** <dir>/<name>, written LAST and whole (a part file renamed into place), as the plot's done file. */
void writeRunDone( const std::string& aDir, const char* aName, const json& aBody )
{
    const std::string body = aBody.dump( -1, ' ', false, json::error_handler_t::replace );
    const std::string fin = aDir + "/" + aName;
    const std::string part = fin + ".part";

    if( FILE* f = std::fopen( part.c_str(), "wb" ) )
    {
        std::fwrite( body.data(), 1, body.size(), f );
        std::fclose( f );
        std::rename( part.c_str(), fin.c_str() );
    }
}

void ensureRunDir( const std::string& aDir )
{
    const wxString dir = wxString::FromUTF8( aDir.c_str() );

    if( !wxFileName::DirExists( dir ) )
        wxFileName::Mkdir( dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL );
}

struct INSTANCE_REF
{
    std::string uuid;
    std::string sheetPath;
    wxString    ref;
    bool        power = false;
};

/** Every symbol instance's designator, keyed by sheet path then uuid. */
std::map<std::string, INSTANCE_REF> instanceRefs( SCH_EDIT_FRAME* aFrame )
{
    std::map<std::string, INSTANCE_REF> refs;

    for( const SCH_SHEET_PATH& path : aFrame->Schematic().Hierarchy() )
    {
        SCH_SCREEN* screen = path.LastScreen();

        if( !screen )
            continue;

        const std::string sheetPath = toUtf8( path.PathAsString() );

        for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
        {
            SCH_SYMBOL*  symbol = static_cast<SCH_SYMBOL*>( item );
            INSTANCE_REF entry;
            entry.uuid = toUtf8( symbol->m_Uuid.AsString() );
            entry.sheetPath = sheetPath;
            entry.ref = symbol->GetRef( &path, false );
            entry.power = symbol->IsPower();
            refs[sheetPath + entry.uuid] = std::move( entry );
        }
    }

    return refs;
}

/**
 * AnnotateSymbols' own steps (annotate.cpp) with the kept designators taken out of the
 * numbering and handed to it as additional references, so they stay and stay taken.
 * KiCad's ANNOTATE_SELECTION cannot carry this: it reads the selection on the CURRENT
 * sheet path only (AppendSymbol on GetCurrentSheet), so it never reaches a part on
 * another sheet of the design. A kept uuid keeps its DESIGNATOR: every unit (and every
 * instance) sharing it is kept with it.
 */
void annotateKeeping( SCH_EDIT_FRAME* aFrame, SCH_COMMIT* aCommit, bool aWholeDesign,
                      ANNOTATE_ORDER_T aOrder, bool aReset, const std::set<std::string>& aKeep,
                      REPORTER& aReporter )
{
    SCHEMATIC&     sch = aFrame->Schematic();
    SCH_SCREENS    screens( sch.Root() );
    SCH_SHEET_LIST sheets = sch.Hierarchy();
    SCH_SHEET_PATH currentSheet = aFrame->GetCurrentSheet();

    std::set<wxString> keptRefs;

    for( const SCH_SHEET_PATH& path : sheets )
    {
        for( SCH_ITEM* item : path.LastScreen()->Items().OfType( SCH_SYMBOL_T ) )
        {
            SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( item );

            if( aKeep.count( toUtf8( symbol->m_Uuid.AsString() ) ) && symbol->IsAnnotated( &path ) )
                keptRefs.insert( symbol->GetRef( &path, false ) );
        }
    }

    auto isKept =
            [&]( SCH_REFERENCE& aRef )
            {
                return keptRefs.count( aRef.GetSymbol()->GetRef( &aRef.GetSheetPath(), false ) ) > 0;
            };

    if( int count = screens.ReplaceDuplicateTimeStamps() )
    {
        aReporter.ReportTail( wxString::Format( wxS( "%d duplicate time stamps were found and replaced." ),
                                                count ),
                              RPT_SEVERITY_WARNING );
    }

    SCH_MULTI_UNIT_REFERENCE_MAP lockedSymbols;

    if( aWholeDesign )
        sheets.GetMultiUnitSymbols( lockedSymbols, SYMBOL_FILTER_ALL );
    else
        currentSheet.GetMultiUnitSymbols( lockedSymbols, SYMBOL_FILTER_ALL );

    if( aReset )
    {
        std::vector<wxString> conflictingRefs;

        for( auto& [refBase, refList] : lockedSymbols )
        {
            std::set<int> seenUnits;

            for( size_t i = 0; i < refList.GetCount(); ++i )
            {
                if( !seenUnits.insert( refList[i].GetUnit() ).second )
                {
                    conflictingRefs.push_back( refBase );
                    break;
                }
            }
        }

        for( const wxString& ref : conflictingRefs )
            lockedSymbols.erase( ref );
    }

    for( const wxString& ref : keptRefs )
        lockedSymbols.erase( ref );

    aFrame->SetSheetNumberAndCount();

    SCH_REFERENCE_LIST scoped;

    if( aWholeDesign )
        sheets.GetSymbols( scoped, SYMBOL_FILTER_ALL );
    else
        currentSheet.GetSymbols( scoped, SYMBOL_FILTER_ALL );

    SCH_REFERENCE_LIST references;

    for( size_t i = 0; i < scoped.GetCount(); ++i )
    {
        if( !isKept( scoped[i] ) )
            references.AddItem( scoped[i] );
    }

    if( aReset )
        references.RemoveAnnotation();

    SCH_REFERENCE_LIST allRefs;
    SCH_REFERENCE_LIST additionalRefs;
    sheets.GetSymbols( allRefs, SYMBOL_FILTER_ALL );

    for( size_t i = 0; i < allRefs.GetCount(); ++i )
    {
        if( !references.Contains( allRefs[i] ) )
            additionalRefs.AddItem( allRefs[i] );
    }

    references.SetRefDesTracker( sch.Settings().m_refDesTracker );
    references.SplitReferences();
    references.AnnotateByOptions( aOrder, INCREMENTAL_BY_REF, 0, lockedSymbols, additionalRefs, false );

    for( size_t i = 0; i < references.GetCount(); ++i )
    {
        SCH_REFERENCE& ref = references[i];
        aCommit->Modify( ref.GetSymbol(), ref.GetSheetPath().LastScreen() );
        ref.Annotate();
    }

    aFrame->CheckAnnotate(
            [&aReporter]( ERCE_T, const wxString& aMsg, SCH_REFERENCE*, SCH_REFERENCE* )
            {
                aReporter.Report( aMsg, RPT_SEVERITY_ERROR );
            },
            aWholeDesign ? ANNOTATE_ALL : ANNOTATE_CURRENT_SHEET, false, SYMBOL_FILTER_ALL );

    aFrame->GetCurrentSheet().UpdateAllScreenReferences();
    aFrame->SetSheetNumberAndCount();
    aFrame->SyncView();
    aFrame->GetCanvas()->Refresh();
    aFrame->OnModify();
    aFrame->UpdateNetHighlightStatus();
}

/** Number the parts, on the apply coroutine; the done file names every designator that moved. */
void runAnnotate( const std::string& aDir, bool aWholeDesign, ANNOTATE_ORDER_T aOrder, bool aReset,
                  const std::set<std::string>& aKeep )
{
    RUN_LINES_REPORTER reporter;
    json               changes = json::array();

    ensureRunDir( aDir );

    SCH_EDIT_FRAME* fr = schFrame();   // re-resolved on the coroutine (S4)

    if( !fr )
    {
        reporter.Report( wxS( "no schematic editor to number" ), RPT_SEVERITY_ERROR );
    }
    else
    {
        try
        {
            const std::map<std::string, INSTANCE_REF> before = instanceRefs( fr );
            SCH_COMMIT                                commit( fr );

            if( aKeep.empty() )
            {
                fr->AnnotateSymbols( &commit, aWholeDesign ? ANNOTATE_ALL : ANNOTATE_CURRENT_SHEET,
                                     aOrder, INCREMENTAL_BY_REF, /*aRecursive*/ aWholeDesign,
                                     /*aStartNumber*/ 0, aReset, /*aRegroupUnits*/ false,
                                     /*aRepairTimestamps*/ true, reporter, SYMBOL_FILTER_ALL );
            }
            else
            {
                annotateKeeping( fr, &commit, aWholeDesign, aOrder, aReset, aKeep, reporter );
            }

            const std::map<std::string, INSTANCE_REF> after = instanceRefs( fr );
            bool                                      moved = false;

            for( const auto& [key, now] : after )
            {
                auto was = before.find( key );

                if( was == before.end() || was->second.ref == now.ref )
                    continue;

                moved = true;

                if( now.power )
                    continue;   // power symbols are numbered too, but they are not parts

                json change = json::object();
                change["uuid"] = now.uuid;
                change["sheetPath"] = now.sheetPath;
                change["from"] = toUtf8( was->second.ref );
                change["to"] = toUtf8( now.ref );
                changes.push_back( std::move( change ) );
            }

            if( moved )
                commit.Push( wxT( "Number the parts" ) );
            else
                commit.Revert();   // nothing changed: no undo step
        }
        catch( const std::exception& e )
        {
            reporter.Report( wxString::Format( wxS( "the numbering failed: %s" ),
                                               wxString::FromUTF8( e.what() ) ),
                             RPT_SEVERITY_ERROR );
        }
        catch( ... )
        {
            reporter.Report( wxS( "the numbering failed" ), RPT_SEVERITY_ERROR );
        }
    }

    // Only the warnings and errors: AnnotateSymbols reports an action line per symbol.
    json lines = json::array();

    for( const json& line : reporter.Lines() )
    {
        if( line["severity"] == "warning" || line["severity"] == "error" )
            lines.push_back( line );
    }

    json done = json::object();
    done["changes"] = std::move( changes );
    done["lines"] = std::move( lines );
    writeRunDone( aDir, ".cc-annotate.json", done );
}

/**
 * Check the sheet, on the apply coroutine: KiCad's ERC dialog preparation (record the
 * exclusions, clear every ERC marker, mark what is not numbered or numbered twice), then
 * the ERC job's body (ERC_TESTER::RunTests, then ERC_REPORT::WriteJsonReport in mm at the
 * default severities) WITHOUT the job's AsyncLoad and BlockUntilLoaded: in this image the
 * symbol library adapter's AsyncLoad queues a load of every row it has not loaded (the
 * island serves KiCad's whole set, 222 libraries), and BlockUntilLoaded would wait on
 * them all on this coroutine. No commit (the undo depth stays); the markers are not saved
 * content, and any screen the run marks modified is put back as it was.
 */
void runCheck( const std::string& aDir )
{
    RUN_LINES_REPORTER reporter;
    int                code = -1;

    ensureRunDir( aDir );

    SCH_EDIT_FRAME* fr = schFrame();   // re-resolved on the coroutine (S4)

    if( !fr )
    {
        reporter.Report( wxS( "no schematic editor to check" ), RPT_SEVERITY_ERROR );
    }
    else
    {
        try
        {
            SCHEMATIC*   sch = &fr->Schematic();
            SCH_SCREENS  screens( sch->Root() );

            std::map<SCH_SCREEN*, bool> modified;

            for( SCH_SCREEN* screen = screens.GetFirst(); screen; screen = screens.GetNext() )
                modified[screen] = screen->IsContentModified();

            sch->RecordERCExclusions();

            // The dialog clears the selection before it deletes the markers; only a selected
            // marker would be left dangling, so the reader's selection is kept otherwise.
            if( SCH_SELECTION_TOOL* selTool = fr->GetToolManager()->GetTool<SCH_SELECTION_TOOL>() )
            {
                bool markerSelected = false;

                for( EDA_ITEM* item : selTool->GetSelection() )
                    markerSelected = markerSelected || item->Type() == SCH_MARKER_T;

                if( markerSelected )
                    selTool->ClearSelection();
            }

            screens.DeleteAllMarkers( MARKER_BASE::MARKER_ERC, /*aIncludeExclusions*/ true );

            fr->CheckAnnotate(
                    []( ERCE_T aType, const wxString& aMsg, SCH_REFERENCE* aItemA, SCH_REFERENCE* aItemB )
                    {
                        std::shared_ptr<ERC_ITEM> ercItem = ERC_ITEM::Create( aType );
                        ercItem->SetErrorMessage( aMsg );

                        if( aItemB )
                            ercItem->SetItems( aItemA->GetSymbol(), aItemB->GetSymbol() );
                        else
                            ercItem->SetItems( aItemA->GetSymbol() );

                        SCH_MARKER* marker = new SCH_MARKER( std::move( ercItem ),
                                                             aItemA->GetSymbol()->GetPosition() );
                        aItemA->GetSheetPath().LastScreen()->Append( marker );
                    },
                    ANNOTATE_ALL, true, SYMBOL_FILTER_NON_POWER );

            ERC_TESTER tester( sch );
            tester.RunTests( fr->GetCanvas()->GetView()->GetDrawingSheet(), /*aEditFrame*/ nullptr,
                             fr->Kiway().KiFACE( KIWAY::FACE_CVPCB, false ), &fr->Prj(),
                             /*aProgressReporter*/ nullptr );

            auto provider = std::make_shared<SHEETLIST_ERC_ITEMS_PROVIDER>( sch );
            provider->SetSeverities( RPT_SEVERITY_ERROR | RPT_SEVERITY_WARNING );

            reporter.Report( wxString::Format( wxS( "Found %d violations" ), provider->GetCount() ),
                             RPT_SEVERITY_INFO );

            ERC_REPORT report( sch, EDA_UNITS::MM, provider );
            const wxString out = wxString::FromUTF8( ( aDir + "/erc.json" ).c_str() );

            if( report.WriteJsonReport( out ) )
            {
                code = 0;
            }
            else
            {
                code = 1;
                reporter.Report( wxS( "the report was not written" ), RPT_SEVERITY_ERROR );
            }

            // The dialog's testErc: the shown sheet's new marks into the view (the other
            // sheets add theirs when they are shown).
            for( SCH_ITEM* marker : fr->GetScreen()->Items().OfType( SCH_MARKER_T ) )
            {
                fr->GetCanvas()->GetView()->Remove( marker );
                fr->GetCanvas()->GetView()->Add( marker );
            }

            fr->GetCanvas()->Refresh();

            for( SCH_SCREEN* screen = screens.GetFirst(); screen; screen = screens.GetNext() )
            {
                auto was = modified.find( screen );

                if( was != modified.end() && !was->second && screen->IsContentModified() )
                    screen->SetContentModified( false );
            }
        }
        catch( const std::exception& e )
        {
            reporter.Report( wxString::Format( wxS( "the check failed: %s" ),
                                               wxString::FromUTF8( e.what() ) ),
                             RPT_SEVERITY_ERROR );
        }
        catch( ... )
        {
            reporter.Report( wxS( "the check failed" ), RPT_SEVERITY_ERROR );
        }
    }

    json done = json::object();
    done["code"] = code;
    done["lines"] = reporter.Lines();
    writeRunDone( aDir, ".cc-erc.json", done );
}
} // namespace

// symbol.get: the SymbolSheet of the symbol whose uuid is aUuid (lowercase 8-4-4-4-12), on
// aSheetPath when it holds it, else on the sheet the editor shows, else on the first path
// holding it; "" for no such symbol (or no schematic frame, or an open in flight). A read:
// synchronous, no coroutine. Unsaved edits included.
static std::string kicadSymbolGet( std::string aUuid, std::string aSheetPath )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return "";

    SYMBOL_HIT hit = resolveSymbol( fr, aUuid, aSheetPath, /*aStrict*/ false );

    if( !hit.symbol )
        return "";

    return symbolSheetJson( fr, hit.symbol, hit.path )
            .dump( -1, ' ', false, json::error_handler_t::replace );
}

// symbol.set: {"uuid", "sheetPath", "set": {...}}, those three keys only; set as spec 11.2
// (ref, value, footprint, fields, addField, removeField, inBom, onBoard, inSim, dnp,
// rotation, mirror, x, y, unit, bodyStyle; at least one). A non-empty sheetPath must hold
// the symbol; an empty one resolves as symbol.get does. False (nothing queued) for any
// other JSON; true once ONE commit is queued on the apply coroutine (one undo step, pushed
// as "Edit symbol"). The host waits for kicadCollabBusy to clear, then reads back.
static bool kicadSymbolSet( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 3 )
        return false;

    auto uuid = args.find( "uuid" );
    auto sheetPath = args.find( "sheetPath" );
    auto set = args.find( "set" );

    if( uuid == args.end() || !uuid->is_string() || sheetPath == args.end()
            || !sheetPath->is_string() || set == args.end() )
    {
        return false;
    }

    const std::string id = uuid->get<std::string>();
    SYMBOL_HIT        hit = resolveSymbol( fr, id, sheetPath->get<std::string>(), /*aStrict*/ true );

    if( !hit.symbol )
        return false;

    SYMBOL_SET edits;

    try
    {
        if( !readSymbolSet( *set, hit.symbol, edits ) )
            return false;
    }
    catch( ... )
    {
        return false;
    }

    const std::string path = toUtf8( hit.path.PathAsString() );

    pcbjam_collab::runOnCoroutine( fr, [id, path, edits]() {   // re-resolve on the coroutine (S4)
        SCH_EDIT_FRAME* frame = schFrame();
        SYMBOL_HIT      live = resolveSymbol( frame, id, path, /*aStrict*/ true );

        if( !live.symbol )
            return;

        applySymbolSet( frame, live.symbol, live.path, edits );
    } );

    return true;
}

// sheet.annotate: {"run": int, "dir": "/tmp/cc-annotate/<n>", "scope": "all"|"sheet",
// "order": "x"|"y", "reset": bool, "keep": [uuid...]} (at most 4,096 lowercase uuids),
// those six keys only. Queues the numbering on the apply coroutine (one commit pushed as
// "Number the parts", none when nothing changed) and then writes <dir>/.cc-annotate.json
// {"changes": [{uuid, sheetPath, from, to}], "lines": [{severity, text}]} last.
static bool kicadAnnotate( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 6 )
        return false;

    auto run = args.find( "run" );
    auto dir = args.find( "dir" );
    auto scope = args.find( "scope" );
    auto order = args.find( "order" );
    auto reset = args.find( "reset" );
    auto keep = args.find( "keep" );

    if( run == args.end() || !run->is_number_integer() || run->get<long long>() < 0
            || dir == args.end() || !dir->is_string() || scope == args.end()
            || !scope->is_string() || order == args.end() || !order->is_string()
            || reset == args.end() || !reset->is_boolean() || keep == args.end()
            || !keep->is_array() || keep->size() > 4096 )
    {
        return false;
    }

    const std::string d = dir->get<std::string>();
    const std::string s = scope->get<std::string>();
    const std::string o = order->get<std::string>();

    if( !isRunDir( d, "/tmp/cc-annotate/" ) || ( s != "all" && s != "sheet" )
            || ( o != "x" && o != "y" ) )
    {
        return false;
    }

    std::set<std::string> kept;

    for( const json& k : *keep )
    {
        if( !k.is_string() || !isLowerUuid( k.get<std::string>() ) )
            return false;

        kept.insert( k.get<std::string>() );
    }

    const bool             wholeDesign = s == "all";
    const ANNOTATE_ORDER_T sort = o == "y" ? SORT_BY_Y_POSITION : SORT_BY_X_POSITION;
    const bool             resetAll = reset->get<bool>();

    pcbjam_collab::runOnCoroutine( fr, [d, wholeDesign, sort, resetAll, kept]() {
        runAnnotate( d, wholeDesign, sort, resetAll, kept );
    } );

    return true;
}

// sheet.check: {"run": int, "dir": "/tmp/cc-erc/<n>"}, those two keys only. Queues the
// check on the apply coroutine; it writes KiCad's JSON report to <dir>/erc.json and then
// <dir>/.cc-erc.json {"code": int, "lines": [{severity, text}]} last (code 0: the report
// was written). The marks it adds stay on the drawing; no commit.
static bool kicadCheckSheet( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 2 )
        return false;

    auto run = args.find( "run" );
    auto dir = args.find( "dir" );

    if( run == args.end() || !run->is_number_integer() || run->get<long long>() < 0
            || dir == args.end() || !dir->is_string() )
    {
        return false;
    }

    const std::string d = dir->get<std::string>();

    if( !isRunDir( d, "/tmp/cc-erc/" ) )
        return false;

    pcbjam_collab::runOnCoroutine( fr, [d]() { runCheck( d ); } );

    return true;
}

namespace
{
/**
 * SCH_EDIT_FRAME::SaveSymbolToSchematic's body for the placed symbol aPrincipal on aPath:
 * every unit of the part (the principal and every unit sharing its designator) takes a
 * flattened copy of aSymbol, and with aRelink each unit's lib id becomes aLibId (the part now
 * names the design's own copy), all as ONE commit pushed as "Edit symbol" (one undo step).
 */
void pushSymbolToSchematic( SCH_EDIT_FRAME* aFrame, SCH_SYMBOL* aPrincipal,
                            const SCH_SHEET_PATH& aPath, const LIB_SYMBOL& aSymbol,
                            bool aRelink, const LIB_ID& aLibId )
{
    SCH_SHEET_LIST sheets = aFrame->Schematic().Hierarchy();
    SCH_COMMIT     commit( aFrame );
    wxString       principalRef;

    if( aPrincipal->IsAnnotated( &aPath ) )
        principalRef = aPrincipal->GetRef( &aPath, false );

    std::vector<std::pair<SCH_SYMBOL*, SCH_SHEET_PATH>> allUnits;

    for( const SCH_SHEET_PATH& path : sheets )
    {
        for( SCH_ITEM* candidate : path.LastScreen()->Items().OfType( SCH_SYMBOL_T ) )
        {
            SCH_SYMBOL* candidateSymbol = static_cast<SCH_SYMBOL*>( candidate );

            if( candidateSymbol == aPrincipal
                    || ( !principalRef.IsEmpty() && candidateSymbol->IsAnnotated( &path )
                         && candidateSymbol->GetRef( &path, false ) == principalRef ) )
            {
                allUnits.emplace_back( candidateSymbol, path );
            }
        }
    }

    for( auto& [unit, path] : allUnits )
    {
        // Removed first so the screen's cached library symbols never keep a stale copy.
        path.LastScreen()->Remove( unit );

        if( !unit->IsNew() )
            commit.Modify( unit, path.LastScreen() );

        if( aRelink )
        {
            unit->SetLibId( aLibId );
            unit->SetSchSymbolLibraryName( wxEmptyString );
        }

        std::unique_ptr<LIB_SYMBOL> copy = aSymbol.Flatten();

        if( copy )
        {
            copy->SetLibId( unit->GetLibId() );
            unit->SetLibSymbol( copy.release() );
        }

        unit->UpdateFields( &aFrame->GetCurrentSheet(),
                            true,  /* update style */
                            true,  /* update ref */
                            true,  /* update other fields */
                            false, /* reset ref */
                            false  /* reset other fields */ );

        path.LastScreen()->Append( unit );
        aFrame->GetCanvas()->GetView()->Update( unit );
    }

    // Clear any orphaned alternate pins.
    for( SCH_PIN* pin : aPrincipal->GetPins() )
    {
        wxString altName = pin->GetAlt();

        if( altName.IsEmpty() )
            continue;

        if( pin->GetAlternates().count( altName ) == 0 )
            pin->SetAlt( wxEmptyString );
    }

    if( !commit.Empty() )
        commit.Push( wxS( "Edit symbol" ) );
}
} // namespace

// symbol.forEditor: {uuid, sheetPath}, those two keys only (symbol.get's rules). The placed
// symbol exactly as SYMBOL_EDIT_FRAME::LoadSymbolFromSchematic builds it (its library copy
// flattened, the instance's fields carried back through its rotation and mirror), formatted
// as a kicad_symbol_lib document: {name, libId, text}. A read; "" for any other shape or no
// such symbol.
static std::string kicadSymbolForEditor( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return "";

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 2 || !args.contains( "uuid" )
            || !args.contains( "sheetPath" ) || !args["uuid"].is_string()
            || !args["sheetPath"].is_string() )
    {
        return "";
    }

    SYMBOL_HIT hit = resolveSymbol( fr, args["uuid"].get<std::string>(),
                                    args["sheetPath"].get<std::string>(), /*aStrict*/ false );

    if( !hit.symbol || !hit.symbol->GetLibSymbolRef() )
        return "";

    std::unique_ptr<LIB_SYMBOL> symbol = hit.symbol->GetLibSymbolRef()->Flatten();

    if( !symbol )
        return "";

    symbol->SetLibId( hit.symbol->GetLibId() );

    // Take in account the symbol orientation and mirroring to calculate the field positions
    // in the symbol editor (i.e. no rotation, no mirroring): LoadSymbolFromSchematic's lines.
    int orientation = hit.symbol->GetOrientation() & ~( SYM_MIRROR_X | SYM_MIRROR_Y );
    int mirror = hit.symbol->GetOrientation() & ( SYM_MIRROR_X | SYM_MIRROR_Y );

    std::vector<SCH_FIELD> fullSetOfFields;

    for( const SCH_FIELD& field : hit.symbol->GetFields() )
    {
        VECTOR2I  pos = field.GetPosition() - hit.symbol->GetPosition();
        SCH_FIELD libField( symbol.get(), field.GetId() );

        libField = field;

        // The inverse transform is mirroring before, rotate after
        switch( mirror )
        {
        case SYM_MIRROR_X: pos.y = -pos.y; break;
        case SYM_MIRROR_Y: pos.x = -pos.x; break;
        default:                           break;
        }

        switch( orientation )
        {
        case SYM_ORIENT_90:
            std::swap( pos.x, pos.y );
            pos.x = - pos.x;
            break;
        case SYM_ORIENT_270:
            std::swap( pos.x, pos.y );
            pos.y = - pos.y;
            break;
        case SYM_ORIENT_180:
            pos.x = - pos.x;
            pos.y = - pos.y;
            break;
        default:
            break;
        }

        libField.SetPosition( pos );

        fullSetOfFields.emplace_back( std::move( libField ) );
    }

    symbol->SetFields( fullSetOfFields );

    const std::string text = cc_symedit::formatSymbol( symbol.get() );

    if( text.empty() )
        return "";

    json out = json::object();
    out["name"] = std::string( symbol->GetLibId().GetLibItemName().c_str() );
    out["libId"] = std::string( hit.symbol->GetLibId().Format().c_str() );
    out["text"] = text;
    return out.dump( -1, ' ', false, json::error_handler_t::replace );
}

// symbol.push: {uuid, sheetPath, text, name, libId, token}, those six keys only. text a
// kicad_symbol_lib document (at most 4 MB) holding name, read by KiCad's own reader and
// flattened; SaveSymbolToSchematic's body on the placed symbol (every unit of the part by its
// designator) as ONE commit "Edit symbol"; libId "" keeps each unit's lib id, else (lib:name)
// sets it in the same commit. False (nothing queued) for any other shape.
static bool kicadSymbolPushToSchematic( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 6 )
        return false;

    for( const char* key : { "uuid", "sheetPath", "text", "name", "libId", "token" } )
    {
        if( !args.contains( key ) || !args[key].is_string() )
            return false;
    }

    const std::string id = args["uuid"].get<std::string>();
    const std::string path = args["sheetPath"].get<std::string>();
    const std::string& text = args["text"].get_ref<const std::string&>();
    const std::string& token = args["token"].get_ref<const std::string&>();

    if( !isLowerUuid( id ) || text.empty() || text.size() > cc_symedit::DOC_MAX_BYTES
            || token.empty() || token.size() > 64 )
    {
        return false;
    }

    for( char c : token )
    {
        if( c < 0x20 || c > 0x7e )
            return false;
    }

    std::optional<wxString> name = boundedText( args["name"], 1, 1024 );
    std::optional<wxString> libIdText = boundedText( args["libId"], 0, 1024 );

    if( !name || hasControlChar( *name ) || !libIdText || !isFootprintId( *libIdText ) )
        return false;

    LIB_ID     libId;
    const bool relink = !libIdText->IsEmpty();

    if( relink )
        libId.Parse( UTF8( *libIdText ) );

    if( !resolveSymbol( fr, id, path, /*aStrict*/ true ).symbol )
        return false;

    cc_symedit::SYMBOL_CHAIN chain = cc_symedit::readSymbol( text, *name );

    if( !chain.symbol() )
        return false;

    std::shared_ptr<LIB_SYMBOL> flat( chain.symbol()->Flatten().release() );

    if( !flat )
        return false;

    pcbjam_collab::runOnCoroutine( fr, [id, path, flat, relink, libId]() {   // re-resolve (S4)
        SCH_EDIT_FRAME* frame = schFrame();
        SYMBOL_HIT      live = resolveSymbol( frame, id, path, /*aStrict*/ true );

        if( !live.symbol )
            return;

        pushSymbolToSchematic( frame, live.symbol, live.path, *flat, relink, libId );
    } );

    return true;
}

// Modified by Chirichella Inc. on 2026-11-16: the simulator's exports (engine run 14, island
// cc23, caps simModel and sim): the model catalog, one symbol's model read and written by
// uuid, a model file read, the SPICE netlist, a run, Stop, and KiCad's operating point
// labels on the drawing. All through KiCad's own classes (SIM_MODEL and its tables,
// SIM_LIB_MGR, SIM_LIBRARY, SPICE_CIRCUIT_MODEL, the NGSPICE client bound to the
// ngspice_service worker by wasm/stubs/sharedspice_client.cpp), with no KiCad window,
// dialog or menu on any road: SIMULATOR_FRAME and DIALOG_SIM_MODEL are never made. Each
// takes closed JSON args; a refusal ("" or false) queues nothing. Registered outside the
// KICAD_MERGED_EMBIND guard: the names are eeschema-only.

// The ngspice client's two plot lists (wasm/stubs/sharedspice_client.cpp, declared in its
// sharedspice.h; NGSPICE keeps its pointers private). Each is an RPC to the service:
// callers run on the apply coroutine. At global scope: C linkage, the client's symbols.
extern "C" char** pcbjam_ngSpice_AllPlots( void );
extern "C" char** pcbjam_ngSpice_AllVecs( char* plotname );

namespace
{
/** The Sim.* fields a model write owns (Sim.Enable, KiCad 7's exclusion flag, is not one). */
const char* const SIM_OWNED_FIELDS[] = { "Sim.Device",    "Sim.Type",      "Sim.Pins",
                                         "Sim.Params",    "Sim.Library",   "Sim.Name",
                                         "Sim.Ibis.Pin",  "Sim.Ibis.Model", "Sim.Ibis.Diff",
                                         "Sim.NodesFormat" };

bool isSimOwnedField( const wxString& aName )
{
    for( const char* name : SIM_OWNED_FIELDS )
    {
        if( aName == wxString::FromUTF8( name ) )
            return true;
    }

    return false;
}

/**
 * A model type's key: the Sim.Device word, then "." and the Sim.Type word when the type
 * has one ("R", "R.POT", "V.PULSE", "NMOS.BSIM4"), the pair KiCad itself reads the type
 * from (SIM_MODEL::ReadTypeFromFields), so each key names one type.
 */
std::string simTypeKey( SIM_MODEL::TYPE aType )
{
    const SIM_MODEL::INFO info = SIM_MODEL::TypeInfo( aType );
    const std::string     device = SIM_MODEL::DeviceInfo( info.deviceType ).fieldValue;

    return info.fieldValue.empty() ? device : device + "." + info.fieldValue;
}

std::optional<SIM_MODEL::TYPE> simTypeOfKey( const std::string& aKey )
{
    if( aKey.empty() )
        return std::nullopt;

    for( SIM_MODEL::TYPE type : SIM_MODEL::TYPE_ITERATOR() )
    {
        if( type != SIM_MODEL::TYPE::NONE && simTypeKey( type ) == aKey )
            return type;
    }

    return std::nullopt;
}

const char* simCategoryName( SIM_MODEL::PARAM::CATEGORY aCategory )
{
    using CATEGORY = SIM_MODEL::PARAM::CATEGORY;

    switch( aCategory )
    {
    case CATEGORY::PRINCIPAL:              return "principal";
    case CATEGORY::GEOMETRY:               return "geometry";
    case CATEGORY::AC:                     return "ac";
    case CATEGORY::DC:                     return "dc";
    case CATEGORY::CAPACITANCE:            return "capacitance";
    case CATEGORY::TEMPERATURE:            return "temperature";
    case CATEGORY::NOISE:                  return "noise";
    case CATEGORY::DISTRIBUTED_QUANTITIES: return "distributed_quantities";
    case CATEGORY::LIMITING_VALUES:        return "limiting_values";
    case CATEGORY::S_PARAM:                return "s_param";
    case CATEGORY::ADVANCED:               return "advanced";
    case CATEGORY::FLAGS:                  return "flags";
    case CATEGORY::WAVEFORM:               return "waveform";
    case CATEGORY::INITIAL_CONDITIONS:     return "initial_conditions";
    case CATEGORY::SUPERFLUOUS:            return "superfluous";
    }

    return "advanced";
}

const char* simKindName( SIM_TYPE aType )
{
    switch( aType )
    {
    case ST_OP:    return "op";
    case ST_DC:    return "dc";
    case ST_AC:    return "ac";
    case ST_TRAN:  return "tran";
    case ST_NOISE: return "noise";
    case ST_PZ:    return "pz";
    case ST_SP:    return "sp";
    case ST_FFT:   return "fft";
    default:       return "other";
    }
}

/** The symbol's library pins sorted by number, the order SIM_LIB_MGR::CreateModel gives a model. */
std::vector<SCH_PIN*> sortedSimPins( SCH_SYMBOL* aSymbol )
{
    std::vector<SCH_PIN*> pins = aSymbol->GetAllLibPins();

    std::sort( pins.begin(), pins.end(),
               []( const SCH_PIN* lhs, const SCH_PIN* rhs )
               {
                   return StrNumCmp( lhs->GetNumber(), rhs->GetNumber(), true ) < 0;
               } );

    return pins;
}

/**
 * The embedded files a model may name (kicad-embed://), as SCH_EDIT_FRAME::
 * RefreshOperatingPointDisplay stacks them: the schematic's, then the symbol's own and its
 * library parents'.
 */
std::vector<EMBEDDED_FILES*> simFilesStack( SCHEMATIC& aSch, SCH_SYMBOL* aSymbol )
{
    std::vector<EMBEDDED_FILES*> stack;
    stack.push_back( aSch.GetEmbeddedFiles() );

    if( aSymbol )
    {
        if( EMBEDDED_FILES* own = aSymbol->GetEmbeddedFiles() )
        {
            stack.push_back( own );

            if( const std::unique_ptr<LIB_SYMBOL>& lib = aSymbol->GetLibSymbolRef() )
                lib->AppendParentEmbeddedFiles( stack );
        }
    }

    return stack;
}

bool isErrorSeverity( SEVERITY aSeverity )
{
    return aSeverity == RPT_SEVERITY_ERROR || aSeverity == RPT_SEVERITY_UNDEFINED;
}

/** A Problem record (spec 8.2); text is KiCad's own English. */
json simProblem( const char* aCode, SEVERITY aSeverity, const wxString& aText )
{
    json problem = json::object();
    problem["code"] = aCode;
    problem["severity"] = isErrorSeverity( aSeverity ) ? "error" : "warning";
    problem["text"] = toUtf8( aText );
    return problem;
}

bool mentionsNotFound( const wxString& aText )
{
    return aText.Lower().Contains( wxS( "not found" ) );
}

/** Error and warning lines of aReporter as problems; aErrorCode for errors, a miss as file_missing. */
void appendProblems( const RUN_LINES_REPORTER& aReporter, const char* aErrorCode, json& aOut,
                     const std::function<void( json& )>& aStamp = nullptr )
{
    for( const auto& [severity, text] : aReporter.m_lines )
    {
        if( !isErrorSeverity( severity ) && severity != RPT_SEVERITY_WARNING )
            continue;

        const char* code = "other";

        if( mentionsNotFound( text ) )
            code = "file_missing";
        else if( isErrorSeverity( severity ) )
            code = aErrorCode;

        json problem = simProblem( code, severity, text );

        if( aStamp )
            aStamp( problem );

        aOut.push_back( std::move( problem ) );
    }
}

/**
 * A model file's path as the island names it: relative to the project folder (no "..", no
 * absolute path, no backslash, no variable), or kicad-embed://<name>. Answers the text
 * KiCad stores in Sim.Library and resolves (SIM_LIB_MGR::ResolveLibraryPath), or nothing.
 */
std::optional<wxString> simModelPath( const json& aValue )
{
    std::optional<wxString> path = boundedText( aValue, 1, 512 );

    if( !path || hasControlChar( *path ) || path->Contains( wxS( "\\" ) )
            || path->Contains( wxS( "$" ) ) )
    {
        return std::nullopt;
    }

    const wxString embedPrefix = wxString::FromUTF8( FILEEXT::KiCadUriPrefix.c_str() ) + wxS( "://" );

    if( path->StartsWith( embedPrefix ) )
    {
        const wxString name = path->Mid( embedPrefix.length() );

        if( name.IsEmpty() || name.Contains( wxS( "/" ) ) || name.Contains( wxS( ".." ) ) )
            return std::nullopt;

        return path;
    }

    if( path->StartsWith( wxS( "/" ) ) || path->StartsWith( wxS( "~" ) ) || path->Contains( wxS( ":" ) ) )
        return std::nullopt;

    wxArrayString parts = wxSplit( *path, '/' );

    for( const wxString& part : parts )
    {
        if( part.IsEmpty() || part == wxS( "." ) || part == wxS( ".." ) )
            return std::nullopt;
    }

    return path;
}

wxString symbolFieldText( SCH_SYMBOL* aSymbol, const wxString& aName )
{
    SCH_FIELD* field = fieldNamed( aSymbol, aName );
    return field ? field->GetText() : wxString();
}

/** The island's SimModelSheet (spec 8.2) for one instance. */
json simModelSheetJson( SCH_EDIT_FRAME* aFrame, SCH_SYMBOL* aSymbol, const SCH_SHEET_PATH& aPath )
{
    SCHEMATIC&         sch = aFrame->Schematic();
    RUN_LINES_REPORTER reporter;
    SIM_LIB_MGR        mgr( &aFrame->Prj() );

    mgr.SetFilesStack( simFilesStack( sch, aSymbol ) );

    const wxString library = symbolFieldText( aSymbol, SIM_LIBRARY_FIELD );
    const wxString device = symbolFieldText( aSymbol, SIM_DEVICE_FIELD );

    json out = json::object();
    out["uuid"] = toUtf8( aSymbol->m_Uuid.AsString() );
    out["sheetPath"] = toUtf8( aPath.PathAsString() );
    out["ref"] = toUtf8( aSymbol->GetRef( &aPath, false ) );
    out["enabled"] = !aSymbol->GetExcludedFromSim( &aPath, wxEmptyString );
    out["source"] = "none";
    out["device"] = "";
    out["type"] = "";
    out["typeName"] = "";
    out["file"] = nullptr;
    out["pins"] = json::array();
    out["params"] = json::array();
    out["problems"] = json::array();

    try
    {
        SIM_LIBRARY::MODEL libModel = mgr.CreateModel( &aPath, *aSymbol, /*aResolve*/ true, 0,
                                                       wxEmptyString, reporter );
        SIM_MODEL&         model = libModel.model;
        const SIM_MODEL::TYPE type = model.GetType();

        if( !library.IsEmpty() )
            out["source"] = "file";
        else if( !device.IsEmpty() )
            out["source"] = "builtin";
        else if( type != SIM_MODEL::TYPE::NONE )
            out["source"] = "inferred";

        if( type != SIM_MODEL::TYPE::NONE )
        {
            out["device"] = SIM_MODEL::DeviceInfo( model.GetDeviceType() ).fieldValue;
            out["type"] = simTypeKey( type );
            out["typeName"] = SIM_MODEL::TypeInfo( type ).description;
        }

        if( !library.IsEmpty() )
        {
            json file = json::object();
            file["path"] = toUtf8( library );
            file["model"] = toUtf8( symbolFieldText( aSymbol, SIM_NAME_FIELD ) );
            out["file"] = std::move( file );
        }

        for( const SIM_MODEL_PIN& pin : model.GetPins() )
        {
            json p = json::object();
            p["symbolPin"] = toUtf8( pin.symbolPinNumber );
            p["modelPin"] = pin.modelPinName;
            out["pins"].push_back( std::move( p ) );
        }

        for( int i = 0; i < model.GetParamCount(); ++i )
        {
            const SIM_MODEL::PARAM& param = model.GetParam( i );

            if( param.info.dir == SIM_MODEL::PARAM::DIR_OUT )
                continue;   // an output of the simulator, never set

            json p = json::object();
            p["name"] = param.info.name;
            p["value"] = param.value;
            p["default"] = param.info.defaultValue;
            p["unit"] = param.info.unit;
            p["given"] = !model.GetParamOverride( i ).value.empty();
            out["params"].push_back( std::move( p ) );
        }

        if( model.GetDeviceType() == SIM_MODEL::DEVICE_T::KIBIS )
        {
            json ibis = json::object();
            ibis["pin"] = toUtf8( symbolFieldText( aSymbol, SIM_LIBRARY_IBIS::PIN_FIELD ) );
            ibis["model"] = toUtf8( symbolFieldText( aSymbol, SIM_LIBRARY_IBIS::MODEL_FIELD ) );
            ibis["diff"] = symbolFieldText( aSymbol, SIM_LIBRARY_IBIS::DIFF_FIELD ) == wxS( "1" );
            ibis["type"] = SIM_MODEL::TypeInfo( type ).fieldValue;
            out["ibis"] = std::move( ibis );
        }
    }
    catch( const IO_ERROR& e )
    {
        reporter.Report( e.What(), RPT_SEVERITY_ERROR );
    }
    catch( const std::exception& e )
    {
        reporter.Report( wxString::FromUTF8( e.what() ), RPT_SEVERITY_ERROR );
    }

    const std::string uuid = out["uuid"].get<std::string>();
    const std::string path = out["sheetPath"].get<std::string>();
    const std::string ref = out["ref"].get<std::string>();

    appendProblems( reporter, "model_error", out["problems"],
                    [&]( json& aProblem )
                    {
                        aProblem["ref"] = ref;
                        aProblem["uuid"] = uuid;
                        aProblem["sheetPath"] = path;
                    } );

    return out;
}

/** One read sim.model.set, ready for the apply coroutine. */
struct SIM_MODEL_SET
{
    bool                                     remove = false;
    std::vector<std::pair<wxString, wxString>> fields;   // the owned Sim.* fields to hold
};

/** Reads the pins array of a set against the symbol's pins and the model's. */
bool readSimPins( const json& aPins, const std::vector<SCH_PIN*>& aSymbolPins, SIM_MODEL& aModel )
{
    if( !aPins.is_array() || aPins.size() > 1024 )
        return false;

    if( aPins.empty() )
        return true;   // the model's own default map (model pin n on symbol pin n)

    std::set<wxString>    symbolPins;
    std::set<std::string> modelPins;

    for( SCH_PIN* pin : aSymbolPins )
        symbolPins.insert( pin->GetNumber() );

    for( int i = 0; i < aModel.GetPinCount(); ++i )
        modelPins.insert( aModel.GetPin( i ).modelPinName );

    std::set<wxString>    seenSymbol;
    std::set<std::string> seenModel;

    std::vector<std::pair<std::string, wxString>> pairs;

    for( const json& entry : aPins )
    {
        if( !entry.is_object() || entry.size() != 2 )
            return false;

        auto symbolPin = entry.find( "symbolPin" );
        auto modelPin = entry.find( "modelPin" );

        if( symbolPin == entry.end() || modelPin == entry.end() )
            return false;

        std::optional<wxString> s = boundedText( *symbolPin, 1, 64 );
        std::optional<wxString> m = boundedText( *modelPin, 1, 64 );

        if( !s || !m )
            return false;

        const std::string model = toUtf8( *m );

        if( !symbolPins.count( *s ) || !modelPins.count( model ) || seenSymbol.count( *s )
                || seenModel.count( model ) )
        {
            return false;
        }

        seenSymbol.insert( *s );
        seenModel.insert( model );
        pairs.emplace_back( model, *s );
    }

    // The dialog's way: every model pin starts unconnected, then each row is assigned.
    for( int i = 0; i < aModel.GetPinCount(); ++i )
        aModel.AssignSymbolPinNumberToModelPin( i, wxEmptyString );

    for( const auto& [model, symbol] : pairs )
        aModel.AssignSymbolPinNumberToModelPin( model, symbol );

    return true;
}

/** Sets each param of a set on aModel; false for an unknown name or a refused value. */
bool readSimParams( const json& aParams, SIM_MODEL& aModel, const std::set<std::string>& aReserved )
{
    if( !aParams.is_object() || aParams.size() > 1024 )
        return false;

    for( auto it = aParams.begin(); it != aParams.end(); ++it )
    {
        const std::string& name = it.key();

        if( aReserved.count( name ) )
            continue;

        std::optional<wxString> value = boundedText( it.value(), 0, 1024 );

        if( name.empty() || name.size() > 64 || !value || hasControlChar( *value )
                || !aModel.FindParam( name ) )
        {
            return false;
        }

        const SIM_MODEL::PARAM* param = aModel.FindParam( name );

        if( param->info.dir == SIM_MODEL::PARAM::DIR_OUT )
            return false;

        aModel.SetParamValue( name, toUtf8( *value ) );   // throws IO_ERROR on refusal
    }

    return true;
}

/**
 * Reads a sim.model.set's model whole on a scratch model (spec 6.2), and answers the owned
 * Sim.* fields SIM_MODEL::WriteFields gives it. False for any key, type, name or value
 * outside the contract; nothing is touched.
 */
bool readSimModelSet( SCH_EDIT_FRAME* aFrame, SCH_SYMBOL* aSymbol, const json& aModel,
                      SIM_MODEL_SET& aOut )
{
    if( aModel.is_null() )
    {
        aOut.remove = true;
        return true;
    }

    if( !aModel.is_object() )
        return false;

    auto source = aModel.find( "source" );

    if( source == aModel.end() || !source->is_string() )
        return false;

    const std::string     kind = source->get<std::string>();
    std::vector<SCH_PIN*> pins = sortedSimPins( aSymbol );

    // The symbol's fields less the ones a model write owns: the copy DIALOG_SIM_MODEL edits.
    std::vector<SCH_FIELD> fields;

    for( const SCH_FIELD& field : aSymbol->GetFields() )
    {
        if( !isSimOwnedField( field.GetName() ) )
            fields.push_back( field );
    }

    if( fields.empty() )
        return false;   // never: a symbol carries its mandatory fields

    RUN_LINES_REPORTER reporter;

    if( kind == "builtin" )
    {
        if( aModel.size() != 4 || !aModel.contains( "type" ) || !aModel.contains( "params" )
                || !aModel.contains( "pins" ) || !aModel["type"].is_string() )
        {
            return false;
        }

        std::optional<SIM_MODEL::TYPE> type = simTypeOfKey( aModel["type"].get<std::string>() );

        if( !type )
            return false;

        const SIM_MODEL::DEVICE_T device = SIM_MODEL::TypeInfo( *type ).deviceType;

        // Only what the model dialog's Built-in tab offers: a file's subcircuit, an IBIS part
        // and a device the dialog never lists are not built in.
        if( !SIM_MODEL::DeviceInfo( device ).showInMenu || device == SIM_MODEL::DEVICE_T::SUBCKT
                || device == SIM_MODEL::DEVICE_T::KIBIS )
        {
            return false;
        }

        std::unique_ptr<SIM_MODEL> model = SIM_MODEL::Create( *type, pins, reporter );

        if( !model || reporter.HasMessageOfSeverity( RPT_SEVERITY_UNDEFINED | RPT_SEVERITY_ERROR ) )
            return false;

        if( !readSimParams( aModel["params"], *model, {} )
                || !readSimPins( aModel["pins"], pins, *model ) )
        {
            return false;
        }

        model->SetIsStoredInValue( false );
        model->WriteFields( fields );
    }
    else if( kind == "file" )
    {
        if( aModel.size() != 5 || !aModel.contains( "path" ) || !aModel.contains( "model" )
                || !aModel.contains( "params" ) || !aModel.contains( "pins" ) )
        {
            return false;
        }

        std::optional<wxString> path = simModelPath( aModel["path"] );
        std::optional<wxString> name = boundedText( aModel["model"], 1, 256 );
        const json&             params = aModel["params"];

        if( !path || !name || hasControlChar( *name ) || !params.is_object() )
            return false;

        SetFieldValue( fields, SIM_LIBRARY_FIELD, toUtf8( *path ), false );
        SetFieldValue( fields, SIM_NAME_FIELD, toUtf8( *name ), false );

        // An IBIS file's part takes its pin, its pin model, the differential flag and the
        // driver kind as fields (DIALOG_SIM_MODEL's IBIS rows), carried in params under
        // four reserved keys.
        const bool ibisFile = path->Lower().EndsWith( wxS( ".ibs" ) );
        const std::set<std::string> reserved = { "ibis.pin", "ibis.model", "ibis.diff", "ibis.type" };

        if( ibisFile )
        {
            std::string ibisType = "DEVICE";

            for( const std::string& key : reserved )
            {
                auto it = params.find( key );

                if( it == params.end() )
                    continue;

                std::optional<wxString> text = boundedText( *it, 0, 256 );

                if( !text || hasControlChar( *text ) )
                    return false;

                const std::string value = toUtf8( *text );

                if( key == "ibis.pin" )
                    SetFieldValue( fields, SIM_LIBRARY_IBIS::PIN_FIELD, value, false );
                else if( key == "ibis.model" )
                    SetFieldValue( fields, SIM_LIBRARY_IBIS::MODEL_FIELD, value, false );
                else if( key == "ibis.diff" )
                {
                    if( value != "" && value != "1" )
                        return false;

                    SetFieldValue( fields, SIM_LIBRARY_IBIS::DIFF_FIELD, value, false );
                }
                else if( key == "ibis.type" )
                {
                    if( value != "DEVICE" && value != "DCDRIVER" && value != "RECTDRIVER"
                            && value != "PRBSDRIVER" )
                    {
                        return false;
                    }

                    ibisType = value;
                }
            }

            SetFieldValue( fields, SIM_DEVICE_FIELD, "IBIS", false );
            SetFieldValue( fields, SIM_DEVICE_SUBTYPE_FIELD, ibisType, false );
        }
        else
        {
            for( const std::string& key : reserved )
            {
                if( params.contains( key ) )
                    return false;
            }
        }

        SIM_LIB_MGR mgr( &aFrame->Prj() );
        mgr.SetForceFullParse();
        mgr.SetFilesStack( simFilesStack( aFrame->Schematic(), aSymbol ) );

        SIM_LIBRARY::MODEL libModel = mgr.CreateModel( fields, /*aResolve*/ false, 0, pins, reporter );

        if( reporter.HasMessageOfSeverity( RPT_SEVERITY_UNDEFINED | RPT_SEVERITY_ERROR ) )
            return false;

        // The file was read and holds the named model (else CreateModel made a fallback).
        bool found = false;

        for( const auto& [libPath, library] : mgr.GetLibraries() )
        {
            if( library.get().FindModel( toUtf8( *name ) ) )
                found = true;
        }

        if( !found )
            return false;

        if( !readSimParams( params, libModel.model, ibisFile ? reserved : std::set<std::string>() )
                || !readSimPins( aModel["pins"], pins, libModel.model ) )
        {
            return false;
        }

        libModel.model.SetIsStoredInValue( false );
        libModel.model.WriteFields( fields );
    }
    else
    {
        return false;
    }

    for( const SCH_FIELD& field : fields )
    {
        if( isSimOwnedField( field.GetName() ) && !field.GetText().IsEmpty() )
            aOut.fields.emplace_back( field.GetName(), field.GetText() );
    }

    return true;
}

/** Applies a read set on the apply coroutine, as ONE commit (one undo step). */
void applySimModelSet( SCH_EDIT_FRAME* aFrame, SCH_SYMBOL* aSymbol, const SCH_SHEET_PATH& aPath,
                       const SIM_MODEL_SET& aSet )
{
    SCHEMATIC&  sch = aFrame->Schematic();
    SCH_SCREEN* screen = aPath.LastScreen();
    SCH_COMMIT  commit( aFrame );

    commit.Modify( aSymbol, screen );

    std::set<wxString> kept;

    for( const auto& [name, text] : aSet.fields )
        kept.insert( name );

    for( const char* owned : SIM_OWNED_FIELDS )
    {
        const wxString name = wxString::FromUTF8( owned );

        if( !kept.count( name ) && fieldNamed( aSymbol, name ) )
            aSymbol->RemoveField( name );
    }

    for( const auto& [name, text] : aSet.fields )
    {
        if( SCH_FIELD* field = fieldNamed( aSymbol, name ) )
        {
            field->SetText( text );
            continue;
        }

        // As the symbol properties dialog adds a row, hidden (KiCad writes Sim.* fields
        // hidden): the symbol's anchor, the reference's angle, the default text size.
        SCH_FIELD        added( aSymbol, FIELD_T::USER, name );
        const int        size = sch.Settings().m_DefaultTextSize;
        const SCH_FIELD* reference = aSymbol->GetField( FIELD_T::REFERENCE );

        added.SetText( text );
        added.SetVisible( false );
        added.SetTextSize( VECTOR2I( size, size ) );

        if( reference )
            added.SetTextAngle( reference->GetTextAngle() );

        added.SetPosition( aSymbol->GetPosition() );
        added.SetOrdinal( aSymbol->GetNextFieldOrdinal() );

        if( SCH_FIELD* placed = aSymbol->AddField( added ) )
            placed->SetParent( aSymbol );
    }

    if( aSymbol->IsMultiUnit() )
        aSymbol->SyncOtherUnits( aPath, commit, nullptr, wxEmptyString );

    commit.Push( wxT( "Edit simulation model" ) );
}

/** A SIM_LIBRARY's models (spec 6.2 kicadSimFileRead). */
json simFileJson( SCH_EDIT_FRAME* aFrame, const wxString& aPath )
{
    RUN_LINES_REPORTER reporter;
    SIM_LIB_MGR        mgr( &aFrame->Prj() );

    mgr.SetForceFullParse();
    mgr.SetFilesStack( simFilesStack( aFrame->Schematic(), nullptr ) );

    json out = json::object();
    out["kind"] = aPath.Lower().EndsWith( wxS( ".ibs" ) ) ? "ibis" : "spice";
    out["models"] = json::array();
    out["problems"] = json::array();

    try
    {
        mgr.SetLibrary( aPath, reporter );

        for( const auto& [libPath, library] : mgr.GetLibraries() )
        {
            const bool ibis = dynamic_cast<const SIM_LIBRARY_IBIS*>( &library.get() ) != nullptr;
            out["kind"] = ibis ? "ibis" : "spice";

            for( const SIM_LIBRARY::MODEL& entry : library.get().GetModels() )
            {
                const SIM_MODEL& model = entry.model;
                json             m = json::object();
                json             pinNames = json::array();

                m["name"] = entry.name;
                m["type"] = simTypeKey( model.GetType() );

                if( ibis )
                {
                    m["kind"] = "ibis";

                    if( const SIM_MODEL_IBIS* part = dynamic_cast<const SIM_MODEL_IBIS*>( &model ) )
                    {
                        for( const auto& [pin, pinName] : part->GetIbisPins() )
                            pinNames.push_back( pin );
                    }
                }
                else
                {
                    m["kind"] = model.GetType() == SIM_MODEL::TYPE::SUBCKT ? "subckt" : "model";

                    for( const std::string& pin : model.GetPinNames() )
                        pinNames.push_back( pin );
                }

                m["pins"] = std::move( pinNames );
                out["models"].push_back( std::move( m ) );
            }
        }
    }
    catch( const IO_ERROR& e )
    {
        reporter.Report( e.What(), RPT_SEVERITY_ERROR );
    }
    catch( const std::exception& e )
    {
        reporter.Report( wxString::FromUTF8( e.what() ), RPT_SEVERITY_ERROR );
    }

    const std::string path = toUtf8( aPath );

    appendProblems( reporter, "file_error", out["problems"],
                    [&]( json& aProblem ) { aProblem["path"] = path; } );

    return out;
}

// Runs

std::vector<std::string> simAllPlots()
{
    std::vector<std::string> plots;

    if( char** names = pcbjam_ngSpice_AllPlots() )
    {
        for( char** name = names; *name; ++name )
            plots.emplace_back( *name );
    }

    return plots;
}

std::vector<std::string> simAllVecs( const std::string& aPlot )
{
    std::vector<std::string> vecs;
    std::string              plot = aPlot;

    if( char** names = pcbjam_ngSpice_AllVecs( plot.data() ) )
    {
        for( char** name = names; *name; ++name )
            vecs.emplace_back( *name );
    }

    return vecs;
}

/** The run in flight: one at a time per engine. */
struct SIM_LIVE_RUN
{
    long long                run = -1;
    std::string              dir;
    SIM_TYPE                 kind = ST_UNKNOWN;
    std::vector<std::string> plotsBefore;
    int                      depth = 0;
    bool                     started = false;         // bg_run asked: an idle event now ends it
    bool                     stopRequested = false;
    bool                     finishQueued = false;
    json                     problems = json::array();
    json                     lines = json::array();
};

std::optional<SIM_LIVE_RUN>& simLive()
{
    static std::optional<SIM_LIVE_RUN> live;
    return live;
}

/** The circuit model of the last run: its signal map names the vectors until the next run. */
std::shared_ptr<SPICE_CIRCUIT_MODEL>& simCircuit()
{
    static std::shared_ptr<SPICE_CIRCUIT_MODEL> circuit;
    return circuit;
}

/** The plots each finished run made, by run number (an FFT names its transient's). */
std::map<long long, std::vector<std::string>>& simRunPlots()
{
    static std::map<long long, std::vector<std::string>> plots;
    return plots;
}

/** The plots the last run of each kind made: destroyed when that kind runs again. */
std::map<int, std::vector<std::string>>& simKindPlots()
{
    static std::map<int, std::vector<std::string>> plots;
    return plots;
}

void finishSimRun();

/**
 * NGSPICE's reporter: the console lines (the last 2,000), and the finish queued on the
 * apply coroutine when the simulator goes idle. Report and OnSimStateChange are called
 * from pcbjam_ngspice_event, a fresh wasm entry that must never suspend: they only store
 * and queue (no vector is read here).
 */
class SIM_RUN_REPORTER : public SIMULATOR_REPORTER
{
public:
    using REPORTER::Report;

    REPORTER& Report( const wxString& aText, SEVERITY aSeverity = RPT_SEVERITY_UNDEFINED ) override
    {
        REPORTER::Report( aText, aSeverity );

        for( wxString line : wxSplit( aText, '\n', '\0' ) )
        {
            line.Trim( true );

            if( line.IsEmpty() )
                continue;

            m_console.push_back( toUtf8( line ) );

            while( m_console.size() > 2000 )
                m_console.pop_front();
        }

        return *this;
    }

    void OnSimStateChange( SIMULATOR* aObject, SIM_STATE aNewState ) override
    {
        std::optional<SIM_LIVE_RUN>& live = simLive();

        if( aNewState != SIM_IDLE || !live || !live->started || live->finishQueued )
            return;

        SCH_EDIT_FRAME* fr = schFrame();

        if( !fr )
            return;

        live->finishQueued = true;
        pcbjam_collab::runOnCoroutine( fr, []() { finishSimRun(); } );
    }

    std::deque<std::string> m_console;
};

SIM_RUN_REPORTER& simReporter()
{
    static SIM_RUN_REPORTER reporter;
    return reporter;
}

/** The one simulator (SIMULATOR::CreateInstance answers a process-wide NGSPICE). */
std::shared_ptr<SPICE_SIMULATOR> simEngine( SCH_EDIT_FRAME* aFrame )
{
    static std::shared_ptr<SPICE_SIMULATOR> engine;

    if( !engine )
    {
        // NGSPICE's constructor binds the client and asks the service to init: the worker
        // boots here, on the first run of the engine's life.
        engine = SIMULATOR::CreateInstance( "ngspice" );

        if( !engine )
            return nullptr;

        engine->SetReporter( &simReporter() );
        engine->Settings() = aFrame->Prj().GetProjectFile().m_SchematicSettings->m_NgspiceSettings;
        engine->Init();
    }
    else
    {
        // A design opened since: its own settings (SIMULATOR_FRAME::LoadSettings).
        engine->Settings() = aFrame->Prj().GetProjectFile().m_SchematicSettings->m_NgspiceSettings;
    }

    return engine;
}

/**
 * Does any symbol name an IBIS model? Its netlist line is made by KIBIS through the shared
 * simulator (SIMULATOR::CreateInstance's one NGSPICE), whose Init reads the simulator's
 * settings: those must be set first (simEngine), as SIMULATOR_FRAME sets them.
 */
bool designHasIbis( SCH_EDIT_FRAME* aFrame )
{
    for( const SCH_SHEET_PATH& path : aFrame->Schematic().Hierarchy() )
    {
        SCH_SCREEN* screen = path.LastScreen();

        if( !screen )
            continue;

        for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
        {
            SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( item );

            if( symbolFieldText( symbol, SIM_DEVICE_FIELD ).Upper() == wxS( "IBIS" )
                    || symbolFieldText( symbol, SIM_LIBRARY_FIELD ).Lower().EndsWith( wxS( ".ibs" ) ) )
            {
                return true;
            }
        }
    }

    return false;
}

/** The netlist options (spec 8.1 SimOptions) as NETLIST_EXPORTER_SPICE flags, SIM_TAB's base. */
std::optional<unsigned> simOptionFlags( const json& aOptions )
{
    if( !aOptions.is_object() || aOptions.size() != 4 )
        return std::nullopt;

    static const char* const keys[] = { "voltages", "currents", "dissipations", "events" };

    for( const char* key : keys )
    {
        auto it = aOptions.find( key );

        if( it == aOptions.end() || !it->is_boolean() )
            return std::nullopt;
    }

    unsigned flags = NETLIST_EXPORTER_SPICE::OPTION_ADJUST_INCLUDE_PATHS
                     | NETLIST_EXPORTER_SPICE::OPTION_ADJUST_PASSIVE_VALS;

    if( aOptions["voltages"].get<bool>() )
        flags |= NETLIST_EXPORTER_SPICE::OPTION_SAVE_ALL_VOLTAGES;

    if( aOptions["currents"].get<bool>() )
        flags |= NETLIST_EXPORTER_SPICE::OPTION_SAVE_ALL_CURRENTS;

    if( aOptions["dissipations"].get<bool>() )
        flags |= NETLIST_EXPORTER_SPICE::OPTION_SAVE_ALL_DISSIPATIONS;

    if( aOptions["events"].get<bool>() )
        flags |= NETLIST_EXPORTER_SPICE::OPTION_SAVE_ALL_EVENTS;

    return flags;
}

/** Printable ASCII with no ';', quote or backslash: nothing of SPICE beyond the analysis words. */
bool isPlainCommandText( const std::string& aText )
{
    for( char c : aText )
    {
        const unsigned char u = static_cast<unsigned char>( c );

        if( u < 0x20 || u > 0x7e || c == ';' || c == '"' || c == '\'' || c == '\\' || c == '`' )
            return false;
    }

    return true;
}

/**
 * An analysis line the island wrote (bridge/sim.ts commandOf): one line, one of the seven
 * analyses KiCad runs with bg_run; "" for the sheet's own directive.
 */
bool isAnalysisCommand( const std::string& aCommand )
{
    if( aCommand.empty() )
        return true;

    if( aCommand.size() > 4096 || !isPlainCommandText( aCommand ) )
        return false;

    switch( SPICE_CIRCUIT_MODEL::CommandToSimType( wxString::FromUTF8( aCommand.c_str() ) ) )
    {
    case ST_OP: case ST_DC: case ST_AC: case ST_TRAN: case ST_NOISE: case ST_PZ: case ST_SP:
        return aCommand[0] == '.';
    default:
        return false;
    }
}

/** An FFT's lines (DIALOG_SIM_COMMAND's: "linearize <vectors>", "fft <vectors>", the window). */
bool readFftLines( const std::string& aCommand, std::vector<std::string>& aLines )
{
    if( aCommand.empty() || aCommand.size() > 16384 )
        return false;

    bool hasFft = false;

    for( const wxString& raw : wxSplit( wxString::FromUTF8( aCommand.c_str() ), '\n', '\0' ) )
    {
        std::string line = toUtf8( raw );

        if( line.empty() )
            continue;

        if( !isPlainCommandText( line ) || aLines.size() >= 16 )
            return false;

        if( line.rfind( "fft ", 0 ) == 0 )
            hasFft = true;
        else if( line.rfind( "linearize ", 0 ) != 0 && line.rfind( "set specwindow=", 0 ) != 0
                 && line.rfind( "set specwindoworder=", 0 ) != 0 )
            return false;

        aLines.push_back( line );
    }

    return hasFft;
}

/** A SPICE number (spec 7.2's grammar), the tuner overrides' values. */
bool isSpiceNumber( const std::string& aText )
{
    static const wxRegEx number( wxS( "^[+-]?([0-9]+\\.?[0-9]*|\\.[0-9]+)([eE][+-]?[0-9]+)?"
                                      "(f|p|n|u|m|k|meg|g|t)?$" ),
                                 wxRE_ADVANCED | wxRE_ICASE );

    return !aText.empty() && aText.size() <= 64 && number.Matches( wxString::FromUTF8( aText.c_str() ) );
}

struct SIM_OVERRIDE
{
    wxString ref;
    double   value = 0.0;
};

/**
 * The annotation check every netlist needs (SCH_EDIT_FRAME::ReadyToNetlist with a
 * COLLECTING handler, never the Annotate dialog): a part with no number or a designator
 * used twice is an error problem.
 */
void collectAnnotationProblems( SCH_EDIT_FRAME* aFrame, json& aOut )
{
    aFrame->CheckAnnotate(
            [&]( ERCE_T aType, const wxString& aMsg, SCH_REFERENCE* aItemA, SCH_REFERENCE* )
            {
                json problem = simProblem( "other", RPT_SEVERITY_ERROR, aMsg );

                if( aItemA )
                {
                    problem["ref"] = toUtf8( aItemA->GetSymbol()->GetRef( &aItemA->GetSheetPath(), true ) );
                    problem["uuid"] = toUtf8( aItemA->GetSymbol()->m_Uuid.AsString() );
                    problem["sheetPath"] = toUtf8( aItemA->GetSheetPath().PathAsString() );
                }

                aOut.push_back( std::move( problem ) );
            },
            ANNOTATE_ALL, true, SYMBOL_FILTER_NON_POWER );
}

/**
 * Each simulated symbol's model made once (one SIM_LIB_MGR, so a file is read once), its
 * failures as problems carrying the symbol: model_error, a missing file as file_missing.
 */
void collectModelProblems( SCH_EDIT_FRAME* aFrame, json& aOut )
{
    SCHEMATIC&         sch = aFrame->Schematic();
    SIM_LIB_MGR        mgr( &aFrame->Prj() );
    std::set<wxString> seenRefs;

    for( const SCH_SHEET_PATH& path : sch.Hierarchy() )
    {
        SCH_SCREEN* screen = path.LastScreen();

        if( !screen )
            continue;

        for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
        {
            SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( item );

            if( symbol->ResolveExcludedFromSim( &path, wxEmptyString ) )
                continue;

            const wxString ref = symbol->GetRef( &path, true );

            if( !seenRefs.insert( ref ).second )
                continue;   // a later unit of a part already made

            RUN_LINES_REPORTER reporter;

            try
            {
                mgr.SetFilesStack( simFilesStack( sch, symbol ) );
                mgr.CreateModel( &path, *symbol, /*aResolve*/ true, 0, wxEmptyString, reporter );
            }
            catch( const IO_ERROR& e )
            {
                reporter.Report( e.What(), RPT_SEVERITY_ERROR );
            }
            catch( const std::exception& e )
            {
                reporter.Report( wxString::FromUTF8( e.what() ), RPT_SEVERITY_ERROR );
            }

            const std::string r = toUtf8( ref );
            const std::string uuid = toUtf8( symbol->m_Uuid.AsString() );
            const std::string sheetPath = toUtf8( path.PathAsString() );

            appendProblems( reporter, "model_error", aOut,
                            [&]( json& aProblem )
                            {
                                aProblem["ref"] = r;
                                aProblem["uuid"] = uuid;
                                aProblem["sheetPath"] = sheetPath;
                            } );
        }
    }
}

/** The circuit read's own lines not already said per symbol (same text), as problems. */
void appendCircuitProblems( const RUN_LINES_REPORTER& aReporter, json& aOut )
{
    std::set<std::string> said;

    for( const json& problem : aOut )
        said.insert( problem.value( "text", "" ) );

    json more = json::array();
    appendProblems( aReporter, "other", more );

    for( json& problem : more )
    {
        if( !said.count( problem.value( "text", "" ) ) )
            aOut.push_back( std::move( problem ) );
    }
}

bool hasErrorProblem( const json& aProblems )
{
    for( const json& problem : aProblems )
    {
        if( problem.value( "severity", "" ) == "error" )
            return true;
    }

    return false;
}

json consoleJson()
{
    json lines = json::array();

    for( const std::string& line : simReporter().m_console )
        lines.push_back( line );

    return lines;
}

/** The record .cc-sim.json, written LAST, after which the run is over. */
void writeSimRecord( const std::string& aDir, long long aRun, const char* aError,
                     const std::string& aMessage, const json& aLines, const json& aProblems )
{
    json record = json::object();
    record["run"] = aRun;
    record["error"] = aError;
    record["message"] = aMessage;
    record["lines"] = aLines;
    record["problems"] = aProblems;
    writeRunDone( aDir, ".cc-sim.json", record );
}

/** A plot's x vector: KiCad's own reading (NGSPICE::GetXAxis), or none (op, pz). */
std::optional<std::string> simScaleOf( const std::vector<std::string>& aVecs )
{
    for( const std::string& vec : aVecs )
    {
        if( vec == "time" || vec == "frequency" )
            return vec;
    }

    for( const std::string& vec : aVecs )
    {
        if( wxString::FromUTF8( vec.c_str() ).Lower().EndsWith( wxS( "-sweep" ) ) )
            return vec;
    }

    for( const std::string& vec : aVecs )
    {
        if( vec == "sweep" )
            return vec;
    }

    return std::nullopt;
}

const char* simTraceName( SIM_TRACE_TYPE aType )
{
    switch( aType & SPT_Y_AXIS_MASK )
    {
    case SPT_VOLTAGE: return "voltage";
    case SPT_CURRENT: return "current";
    case SPT_POWER:   return "power";
    default:          return "other";
    }
}

/**
 * The finish of a run, on the apply coroutine: every plot the run made with its vectors'
 * signal map, the operating point's values handed to the schematic as KiCad's frame hands
 * them (SCHEMATIC::SetOperatingPoint), then result.json, then the record last.
 */
void finishSimRun()
{
    std::optional<SIM_LIVE_RUN>& live = simLive();

    if( !live )
        return;

    // A copy: the run stays live (a netlist or a second run refused) until its record.
    SIM_LIVE_RUN run = *live;

    SCH_EDIT_FRAME*                       fr = schFrame();
    std::shared_ptr<SPICE_CIRCUIT_MODEL>& circuit = simCircuit();
    std::shared_ptr<SPICE_SIMULATOR>      engine = fr ? simEngine( fr ) : nullptr;

    json        result = json::object();
    json        plotsOut = json::array();
    json        op = nullptr;
    const char* error = "";
    std::string message;

    std::vector<std::string> made;

    try
    {
        if( engine )
        {
            // SIMULATOR_FRAME::onSimFinished's grace: the simulator may report idle while
            // it still writes; IsRunning is the client's own mirror, never an RPC.
            const std::set<std::string> before( run.plotsBefore.begin(), run.plotsBefore.end() );

            for( const std::string& plot : simAllPlots() )
            {
                if( !before.count( plot ) && plot != "const" )
                    made.push_back( plot );
            }

            for( const std::string& plot : made )
            {
                std::vector<std::string>   vecs = simAllVecs( plot );
                std::optional<std::string> scale = simScaleOf( vecs );
                const bool                 complex = run.kind == ST_AC || run.kind == ST_SP
                                                     || run.kind == ST_PZ || run.kind == ST_FFT;

                engine->Command( "setplot " + plot );

                // One vector's length stands for the plot's (ngspice keeps every vector of
                // an analysis plot the same length); read through the client's arena, then
                // freed by the next read.
                int length = 0;

                if( !vecs.empty() )
                {
                    const std::string& probe = scale ? *scale : vecs.front();
                    length = static_cast<int>( engine->GetRealVector( probe ).size() );
                }

                json vectors = json::array();

                for( const std::string& vec : vecs )
                {
                    wxString       signal;
                    SIM_TRACE_TYPE type = SPT_UNKNOWN;

                    if( circuit && ( !scale || vec != *scale ) )
                        type = circuit->VectorToSignal( vec, signal );

                    json v = json::object();
                    v["name"] = vec;
                    v["signal"] = signal.IsEmpty() ? vec : toUtf8( signal );

                    if( scale && vec == *scale )
                        v["trace"] = "other";
                    else if( run.kind == ST_NOISE )
                        v["trace"] = "noise";
                    else
                        v["trace"] = simTraceName( type );

                    v["complex"] = complex;
                    v["length"] = length;
                    vectors.push_back( std::move( v ) );
                }

                json p = json::object();
                p["name"] = plot;
                p["x"] = scale ? json( *scale ) : json( nullptr );
                p["vectors"] = std::move( vectors );
                plotsOut.push_back( std::move( p ) );

                if( run.kind == ST_OP && op.is_null() && circuit && fr )
                {
                    // SIMULATOR_FRAME_UI::OnSimRefresh's operating point, value by value.
                    SCHEMATIC& schematic = fr->Schematic();
                    schematic.ClearOperatingPoints();
                    op = json::array();

                    for( const std::string& vec : vecs )
                    {
                        std::vector<double> values = engine->GetRealVector( vec, 1 );

                        if( values.empty() )
                            continue;

                        wxString       signal;
                        SIM_TRACE_TYPE type = circuit->VectorToSignal( vec, signal );

                        if( type != SPT_VOLTAGE && type != SPT_CURRENT && type != SPT_POWER )
                            continue;

                        json entry = json::object();
                        entry["signal"] = toUtf8( signal );
                        entry["trace"] = simTraceName( type );
                        entry["value"] = values[0];
                        op.push_back( std::move( entry ) );

                        wxString key = signal.SubString( 2, signal.Length() - 2 );

                        if( type == SPT_POWER )
                            key += wxS( ":power" );

                        schematic.SetOperatingPoint( key, values[0] );
                    }

                    fr->RefreshOperatingPointDisplay();
                    fr->GetCanvas()->Refresh();
                }
            }
        }
    }
    catch( const std::exception& e )
    {
        error = "sim_failed";
        message = e.what();
    }
    catch( ... )
    {
        error = "sim_failed";
        message = "the run's results could not be read";
    }

    std::string ended = "done";

    if( run.stopRequested )
    {
        ended = "stopped";
        error = "stopped";
    }
    else if( *error == '\0' && made.empty() )
    {
        // ngspice made no plot: the run failed. Its first error line says why.
        error = "sim_failed";

        for( const std::string& line : simReporter().m_console )
        {
            if( wxString::FromUTF8( line.c_str() ).Lower().Contains( wxS( "error" ) ) )
            {
                message = line;
                break;
            }
        }
    }

    if( *error && std::string( error ) == "sim_failed" )
        ended = "failed";

    simRunPlots()[run.run] = made;
    simKindPlots()[run.kind] = made;

    result["run"] = run.run;
    result["kind"] = simKindName( run.kind );
    result["ended"] = ended;
    result["depth"] = run.depth;
    result["plots"] = std::move( plotsOut );
    result["op"] = std::move( op );
    result["console"] = consoleJson();
    result["problems"] = run.problems;
    writeRunDone( run.dir, "result.json", result );

    writeSimRecord( run.dir, run.run, error, message, run.lines, run.problems );
    simLive().reset();
}

/** The run's body on the apply coroutine (spec 6.3 steps 1 to 7). */
void startSimRun( long long aRun, const std::string& aDir, const std::string& aCommand,
                  unsigned aFlags, const std::vector<std::pair<wxString, std::string>>& aOverrides,
                  std::optional<long long> aFftOf, const std::vector<std::string>& aFftLines )
{
    ensureRunDir( aDir );

    RUN_LINES_REPORTER reporter;
    json               problems = json::array();
    SCH_EDIT_FRAME*    fr = schFrame();   // re-resolved on the coroutine (S4)

    auto refuse =
            [&]( const char* aError, const std::string& aMessage )
            {
                simLive().reset();
                writeSimRecord( aDir, aRun, aError, aMessage, reporter.Lines(), problems );
            };

    if( !fr )
    {
        refuse( "circuit", "no schematic editor" );
        return;
    }

    try
    {
        simReporter().m_console.clear();

        std::shared_ptr<SPICE_SIMULATOR> engine = simEngine( fr );

        if( !engine )
        {
            refuse( "service_failed", "the simulator could not be made" );
            return;
        }

        const bool fft = !aFftLines.empty();
        SIM_TYPE   kind = ST_FFT;

        // kicadSimRun made the run live; a Stop asked before this body ran is kept.
        SIM_LIVE_RUN& live = simLive() ? *simLive() : simLive().emplace();
        live.run = aRun;
        live.dir = aDir;
        live.depth = pcbjam_collab::testUndoDepth( fr );

        if( fft )
        {
            auto source = simRunPlots().find( aFftOf.value_or( -1 ) );
            std::vector<std::string> plots = simAllPlots();

            if( source == simRunPlots().end() || source->second.empty()
                    || std::find( plots.begin(), plots.end(), source->second.front() ) == plots.end() )
            {
                refuse( "no_simulation", "the transient this transform reads is gone" );
                return;
            }

            for( const std::string& plot : simKindPlots()[ST_FFT] )
                engine->Command( "destroy " + plot );

            live.kind = ST_FFT;
            live.plotsBefore = simAllPlots();

            // SIMULATOR_FRAME::StartSimulation's FFT: setplot the transient, then each line;
            // ngspice answers synchronously, so the finish follows at once.
            engine->Command( "setplot " + source->second.front() );

            for( const std::string& line : aFftLines )
                engine->Command( line );

            json accepted = json::object();
            accepted["run"] = aRun;
            accepted["plot"] = toUtf8( engine->CurrentPlotName() );
            accepted["problems"] = problems;
            writeRunDone( aDir, "accepted.json", accepted );

            simLive()->finishQueued = true;
            finishSimRun();
            return;
        }

        // SIMULATOR_FRAME::LoadSimulator without its dialogs: the annotation as problems,
        // the connections rebuilt as KiCad does before a simulation, each model made.
        collectAnnotationProblems( fr, problems );

        if( hasErrorProblem( problems ) )
        {
            refuse( "circuit", "the schematic is not fully numbered" );
            return;
        }

        if( ADVANCED_CFG::GetCfg().m_IncrementalConnectivity )
            fr->RecalculateConnections( nullptr, GLOBAL_CLEANUP );

        collectModelProblems( fr, problems );

        std::shared_ptr<SPICE_CIRCUIT_MODEL> circuit =
                std::make_shared<SPICE_CIRCUIT_MODEL>( &fr->Schematic() );

        wxString command = wxString::FromUTF8( aCommand.c_str() );

        if( command.IsEmpty() )
        {
            command = circuit->GetSchTextSimCommand();

            if( command.IsEmpty() )
            {
                refuse( "no_directive", "the schematic carries no simulation directive" );
                return;
            }

            // The sheet's own directives may hold several analyses: KiCad runs the first.
            command = command.BeforeFirst( '\n' ).Trim( true ).Trim( false );
        }

        kind = SPICE_CIRCUIT_MODEL::CommandToSimType( command );

        if( kind == ST_UNKNOWN || kind == ST_FFT || kind == ST_DISTO || kind == ST_SENS
                || kind == ST_TF )
        {
            refuse( "circuit", "the directive is not an analysis this simulator runs" );
            return;
        }

        live.kind = kind;

        // One plot per analysis kind lives in the service: the kind's last plots go first
        // (a transient an FFT reads stays until the next transient).
        for( const std::string& plot : simKindPlots()[kind] )
            engine->Command( "destroy " + plot );

        simKindPlots().erase( kind );
        live.plotsBefore = simAllPlots();

        // SIMULATOR_FRAME's ngspice settings: an LTspice and PSpice reading when the project
        // has no workbook, put back after the attach (the project file is not changed).
        std::shared_ptr<NGSPICE_SETTINGS> settings =
                fr->Prj().GetProjectFile().m_SchematicSettings->m_NgspiceSettings;
        const NGSPICE_COMPATIBILITY_MODE mode = settings->GetCompatibilityMode();

        if( settings->GetWorkbookFilename().IsEmpty() )
            settings->SetCompatibilityMode( NGSPICE_COMPATIBILITY_MODE::LT_PSPICE );

        const bool attached = engine->Attach( circuit, command, aFlags, fr->Prj().GetProjectPath(),
                                              reporter );

        settings->SetCompatibilityMode( mode );

        appendCircuitProblems( reporter, problems );
        simCircuit() = circuit;

        if( !attached || hasErrorProblem( problems ) )
        {
            refuse( "circuit", "the netlist could not be made" );
            return;
        }

        // The tuner overrides, as SIMULATOR_FRAME_UI::applyTuners sends each one.
        for( const auto& [ref, value] : aOverrides )
        {
            const SPICE_ITEM* item = circuit->FindItem( ref );

            if( !item || !item->model || !item->model->GetTunerParam() )
            {
                json problem = simProblem( "other", RPT_SEVERITY_WARNING,
                                           wxString::Format( wxS( "%s is not tunable" ), ref ) );
                problem["ref"] = toUtf8( ref );
                problems.push_back( std::move( problem ) );
                continue;
            }

            const double number = SPICE_VALUE( wxString::FromUTF8( value.c_str() ) ).ToDouble();
            engine->Command( item->model->SpiceGenerator().TunerCommand( *item, number ) );
        }

        live.problems = problems;
        live.lines = reporter.Lines();
        live.started = true;

        if( !engine->Run() )
        {
            refuse( "sim_failed", "the simulator did not start the run" );
            return;
        }

        // The run is live in the service. Its idle event may already have queued the finish
        // (it runs after this body): the acceptance is written first either way.
        json accepted = json::object();
        accepted["run"] = aRun;
        accepted["plot"] = toUtf8( engine->CurrentPlotName() );
        accepted["problems"] = problems;
        writeRunDone( aDir, "accepted.json", accepted );
    }
    catch( const IO_ERROR& e )
    {
        refuse( "circuit", toUtf8( e.What() ) );
    }
    catch( const std::exception& e )
    {
        refuse( "sim_failed", e.what() );
    }
    catch( ... )
    {
        refuse( "sim_failed", "the run failed" );
    }
}

/** Reads {command, options}: the analysis line ("" for the sheet's own) and the flags. */
bool readSimCommand( const json& aArgs, std::string& aCommand, unsigned& aFlags )
{
    auto command = aArgs.find( "command" );
    auto options = aArgs.find( "options" );

    if( command == aArgs.end() || !command->is_string() || options == aArgs.end() )
        return false;

    std::optional<unsigned> flags = simOptionFlags( *options );

    if( !flags )
        return false;

    aCommand = command->get<std::string>();
    aFlags = *flags;
    return true;
}
} // namespace

// sim.catalog: "{}" answers {"devices": [{device, description, types: [{type, name,
// description}]}]}, every device the model dialog's Built-in tab lists (DeviceInfo's
// showInMenu) and each of its types from KiCad's compiled tables; {"type": "<key>"} (a key
// as simTypeKey writes it: "R", "V.PULSE", "NMOS.BSIM4") answers that type's {type, device,
// name, description, pins: [{name, symbolPinHint}], params: [{name, unit, default,
// category, description, kind, tuner}]}. A read; "" for any other args.
static std::string kicadSimModelCatalog( std::string aArgsJson )
{
    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() )
        return "";

    try
    {
        if( args.empty() )
        {
            json devices = json::array();

            for( SIM_MODEL::DEVICE_T device : SIM_MODEL::DEVICE_T_ITERATOR() )
            {
                if( device == SIM_MODEL::DEVICE_T::NONE )
                    continue;

                const SIM_MODEL::DEVICE_INFO info = SIM_MODEL::DeviceInfo( device );

                if( !info.showInMenu )
                    continue;

                json types = json::array();

                for( SIM_MODEL::TYPE type : SIM_MODEL::TYPE_ITERATOR() )
                {
                    if( type == SIM_MODEL::TYPE::NONE || SIM_MODEL::TypeInfo( type ).deviceType != device )
                        continue;

                    json t = json::object();
                    t["type"] = simTypeKey( type );
                    t["name"] = SIM_MODEL::TypeInfo( type ).fieldValue;
                    t["description"] = SIM_MODEL::TypeInfo( type ).description;
                    types.push_back( std::move( t ) );
                }

                if( types.empty() )
                    continue;

                json d = json::object();
                d["device"] = info.fieldValue;
                d["description"] = info.description;
                d["types"] = std::move( types );
                devices.push_back( std::move( d ) );
            }

            json out = json::object();
            out["devices"] = std::move( devices );
            return out.dump( -1, ' ', false, json::error_handler_t::replace );
        }

        if( args.size() != 1 || !args.contains( "type" ) || !args["type"].is_string() )
            return "";

        std::optional<SIM_MODEL::TYPE> type = simTypeOfKey( args["type"].get<std::string>() );

        if( !type )
            return "";

        NULL_REPORTER              devnull;
        std::unique_ptr<SIM_MODEL> model = SIM_MODEL::Create( *type, std::vector<SCH_PIN*>(),
                                                                devnull );

        if( !model )
            return "";

        const SIM_MODEL::PARAM* tuner = model->GetTunerParam();

        json pins = json::array();

        for( int i = 0; i < model->GetPinCount(); ++i )
        {
            json p = json::object();
            p["name"] = model->GetPin( i ).modelPinName;
            p["symbolPinHint"] = std::to_string( i + 1 );   // the default map: pin n on symbol pin n
            pins.push_back( std::move( p ) );
        }

        json params = json::array();

        for( int i = 0; i < model->GetParamCount(); ++i )
        {
            const SIM_MODEL::PARAM& param = model->GetParam( i );
            json                    p = json::object();

            p["name"] = param.info.name;
            p["unit"] = param.info.unit;
            p["default"] = param.info.defaultValue;
            p["category"] = simCategoryName( param.info.category );
            p["description"] = param.info.description;

            if( param.info.dir == SIM_MODEL::PARAM::DIR_OUT )
                p["kind"] = "other";
            else if( param.info.isInstanceParam )
                p["kind"] = "instance";
            else
                p["kind"] = "model";

            p["tuner"] = tuner && tuner->info.name == param.info.name;
            params.push_back( std::move( p ) );
        }

        json out = json::object();
        out["type"] = simTypeKey( *type );
        out["device"] = SIM_MODEL::DeviceInfo( SIM_MODEL::TypeInfo( *type ).deviceType ).fieldValue;
        out["name"] = SIM_MODEL::TypeInfo( *type ).fieldValue;
        out["description"] = SIM_MODEL::TypeInfo( *type ).description;
        out["pins"] = std::move( pins );
        out["params"] = std::move( params );
        return out.dump( -1, ' ', false, json::error_handler_t::replace );
    }
    catch( ... )
    {
        return "";
    }
}

// sim.model.get: {"uuid", "sheetPath"}, those two keys only (symbol.get's grammar: a path
// that does not hold the symbol falls back to the shown sheet, then the first holding it).
// The symbol's model as SIM_LIB_MGR::CreateModel resolves it for the netlist, with the
// schematic's and the symbol's embedded files: SimModelSheet (spec 8.2), plus "ibis":
// {pin, model, diff, type} for an IBIS part. "" for no such symbol (or no schematic frame,
// or an open in flight). A read: synchronous.
static std::string kicadSimModelGet( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return "";

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 2 || !args.contains( "uuid" )
            || !args["uuid"].is_string() || !args.contains( "sheetPath" )
            || !args["sheetPath"].is_string() )
    {
        return "";
    }

    SYMBOL_HIT hit = resolveSymbol( fr, args["uuid"].get<std::string>(),
                                    args["sheetPath"].get<std::string>(), /*aStrict*/ false );

    if( !hit.symbol )
        return "";

    return simModelSheetJson( fr, hit.symbol, hit.path )
            .dump( -1, ' ', false, json::error_handler_t::replace );
}

// sim.model.set: {"uuid", "sheetPath", "model": SimModelSet | null}, those three keys only;
// a non-empty sheetPath must hold the symbol. The model is read whole on a scratch model
// first: the type known, every param the type's (an IBIS file's part also takes ibis.pin,
// ibis.model, ibis.diff and ibis.type), every value taken by SetParamValue, the pin map
// naming the symbol's pins and the model's, the file inside the project folder (or
// kicad-embed://) and holding the named model. False (nothing queued) on any failure;
// true once ONE commit is queued (one undo step, "Edit simulation model"): the symbol's
// Sim.* fields as SIM_MODEL::WriteFields writes them, or, for null, removed (the model is
// inferred again). The host waits for kicadCollabBusy to clear, then reads back.
static bool kicadSimModelSet( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 3 || !args.contains( "uuid" )
            || !args["uuid"].is_string() || !args.contains( "sheetPath" )
            || !args["sheetPath"].is_string() || !args.contains( "model" ) )
    {
        return false;
    }

    const std::string id = args["uuid"].get<std::string>();
    SYMBOL_HIT        hit = resolveSymbol( fr, id, args["sheetPath"].get<std::string>(),
                                           /*aStrict*/ true );

    if( !hit.symbol )
        return false;

    SIM_MODEL_SET set;

    try
    {
        if( !readSimModelSet( fr, hit.symbol, args["model"], set ) )
            return false;
    }
    catch( ... )
    {
        return false;   // SetParamValue's IO_ERROR among them: a refused value
    }

    const std::string path = toUtf8( hit.path.PathAsString() );

    pcbjam_collab::runOnCoroutine( fr, [id, path, set]() {   // re-resolve on the coroutine (S4)
        SCH_EDIT_FRAME* frame = schFrame();
        SYMBOL_HIT      live = resolveSymbol( frame, id, path, /*aStrict*/ true );

        if( !live.symbol )
            return;

        applySimModelSet( frame, live.symbol, live.path, set );
    } );

    return true;
}

// sim.file: {"path"}, that key only: a path relative to the project folder or
// kicad-embed://<name>. Answers {kind: "spice" | "ibis", models: [{name, kind: "model" |
// "subckt" | "ibis", type, pins: [string]}], problems: [Problem]} through SIM_LIBRARY (a
// missing file is a file_missing problem and no models). "" for a path outside the folder
// (or any other args). A read: synchronous.
static std::string kicadSimFileRead( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return "";

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 1 || !args.contains( "path" ) )
        return "";

    std::optional<wxString> path = simModelPath( args["path"] );

    if( !path )
        return "";

    return simFileJson( fr, *path ).dump( -1, ' ', false, json::error_handler_t::replace );
}

// sim.netlist: {"command", "options": {voltages, currents, dissipations, events}}, those
// two keys only; command is the analysis line ("" writes the sheet's own directives, as
// KiCad does). A fresh SPICE_CIRCUIT_MODEL over the schematic: answers {netlist, problems,
// directive, truncated?} (netlist at most 4 MiB; directive is the sheet's own analysis
// line, "" when it carries none). Registered with the PARKER policy (it answers a
// Promise): an IBIS part's model is made by KIBIS through the simulator, which waits on the
// service. "" while a run is live (KIBIS resets the simulator), or for any other args.
static std::string kicadSimNetlist( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() || simLive() )
        return "";

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 2 )
        return "";

    std::string command;
    unsigned    flags = 0;

    if( !readSimCommand( args, command, flags ) || !isAnalysisCommand( command ) )
        return "";

    json out = json::object();
    out["netlist"] = "";
    out["problems"] = json::array();
    out["directive"] = "";

    try
    {
        RUN_LINES_REPORTER  reporter;
        SPICE_CIRCUIT_MODEL circuit( &fr->Schematic() );
        STRING_FORMATTER    formatter;

        collectAnnotationProblems( fr, out["problems"] );
        collectModelProblems( fr, out["problems"] );

        if( designHasIbis( fr ) && !simEngine( fr ) )
        {
            out["problems"].push_back( simProblem( "other", RPT_SEVERITY_ERROR,
                                                   wxS( "the simulator could not be made" ) ) );
        }

        circuit.GetNetlist( wxString::FromUTF8( command.c_str() ), flags, &formatter, reporter );
        appendCircuitProblems( reporter, out["problems"] );

        out["directive"] = toUtf8( circuit.GetSchTextSimCommand() );

        std::string text = formatter.GetString();
        constexpr size_t NETLIST_MAX = 4u << 20;

        if( text.size() > NETLIST_MAX )
        {
            text.resize( NETLIST_MAX );
            out["truncated"] = true;
        }

        out["netlist"] = std::move( text );
    }
    catch( const IO_ERROR& e )
    {
        out["problems"].push_back( simProblem( "other", RPT_SEVERITY_ERROR, e.What() ) );
    }
    catch( const std::exception& e )
    {
        out["problems"].push_back( simProblem( "other", RPT_SEVERITY_ERROR,
                                               wxString::FromUTF8( e.what() ) ) );
    }

    return out.dump( -1, ' ', false, json::error_handler_t::replace );
}

// sim.run: {"run": int, "dir": "/tmp/cc-sim/<n>", "command", "options", "overrides":
// [{ref, value}] (at most 64; value a SPICE number), "fftOf": int | null}, those six keys
// only. command is the island's analysis line ("" for the sheet's own directive); with
// fftOf set it is the FFT's lines instead and fftOf names the transient run it reads.
// Queued on the apply coroutine: the annotation and every model checked, the circuit read
// (an error problem ends the run, error "circuit"), the simulator made on the first run of
// the engine's life (the service boots there), the kind's last plots destroyed, the attach,
// each override as KiCad's tuner sends it, then bg_run; <dir>/accepted.json {run, plot,
// problems} once the run is accepted. On idle the finish is QUEUED (never read in the
// callback): <dir>/result.json (SimResult, spec 8.3) and then <dir>/.cc-sim.json {run,
// error: "" | "circuit" | "no_directive" | "no_simulation" | "sim_failed" | "stopped" |
// "service_failed", message, lines, problems} last. False (nothing queued) while a run is
// live, or for any other JSON.
static bool kicadSimRun( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() || simLive() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 6 )
        return false;

    auto run = args.find( "run" );
    auto dir = args.find( "dir" );
    auto overrides = args.find( "overrides" );
    auto fftOf = args.find( "fftOf" );

    if( run == args.end() || !run->is_number_integer() || run->get<long long>() < 0
            || dir == args.end() || !dir->is_string() || overrides == args.end()
            || !overrides->is_array() || overrides->size() > 64 || fftOf == args.end()
            || !( fftOf->is_null() || ( fftOf->is_number_integer() && fftOf->get<long long>() >= 0 ) ) )
    {
        return false;
    }

    const std::string d = dir->get<std::string>();

    if( !isRunDir( d, "/tmp/cc-sim/" ) )
        return false;

    std::string command;
    unsigned    flags = 0;

    if( !readSimCommand( args, command, flags ) )
        return false;

    std::optional<long long> source;
    std::vector<std::string> fftLines;

    if( !fftOf->is_null() )
    {
        source = fftOf->get<long long>();

        if( !readFftLines( command, fftLines ) || !overrides->empty() )
            return false;
    }
    else if( !isAnalysisCommand( command ) )
    {
        return false;
    }

    std::vector<std::pair<wxString, std::string>> tuned;

    for( const json& entry : *overrides )
    {
        if( !entry.is_object() || entry.size() != 2 || !entry.contains( "ref" )
                || !entry.contains( "value" ) || !entry["value"].is_string() )
        {
            return false;
        }

        std::optional<wxString> ref = boundedText( entry["ref"], 1, 64 );
        const std::string       value = entry["value"].get<std::string>();

        if( !ref || hasControlChar( *ref ) || !isSpiceNumber( value ) )
            return false;

        tuned.emplace_back( *ref, value );
    }

    // Live from here: a second run is refused until this one's record is written.
    SIM_LIVE_RUN& live = simLive().emplace();
    live.run = run->get<long long>();
    live.dir = d;

    const long long n = run->get<long long>();

    pcbjam_collab::runOnCoroutine( fr, [n, d, command, flags, tuned, source, fftLines]() {
        startSimRun( n, d, command, flags, tuned, source, fftLines );
    } );

    return true;
}

// sim.stop: halts the live run (NGSPICE::Stop, bg_halt), queued on the apply coroutine; the
// finish then records "stopped". When the service has no run to halt (its worker died), the
// run is finished at once, so a dead service never leaves the engine holding a live run.
// False when no run is live (or its finish is already queued).
static bool kicadSimStop()
{
    SCH_EDIT_FRAME*              fr = schFrame();
    std::optional<SIM_LIVE_RUN>& live = simLive();

    if( !fr || !live || live->finishQueued )
        return false;

    live->stopRequested = true;

    pcbjam_collab::runOnCoroutine( fr, []() {
        SCH_EDIT_FRAME* frame = schFrame();

        if( !frame || !simLive() || simLive()->finishQueued )
            return;

        std::shared_ptr<SPICE_SIMULATOR> engine = simEngine( frame );

        // bg_halt accepted: ngspice's thread ends and its idle event queues the finish.
        // Refused (no run in the service: the worker died, and the next request booted a
        // fresh one): no idle event will come, so the run is finished here.
        if( !engine || !engine->Stop() )
        {
            simLive()->finishQueued = true;
            finishSimRun();
        }
    } );

    return true;
}

// sim.opOnSheet: {"voltages": bool, "currents": bool}, those two keys only. KiCad's own
// operating point labels on the drawing, from the last operating point's values (the run's
// finish handed them to the schematic): the two eeschema appearance settings set and the
// display refreshed, as SCH_EDITOR_CONTROL::ToggleOPVoltages and ToggleOPCurrents do. No
// commit, no undo step, nothing modified.
static bool kicadSimOpOnSheet( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() || !fr->eeconfig() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 2 || !args.contains( "voltages" )
            || !args["voltages"].is_boolean() || !args.contains( "currents" )
            || !args["currents"].is_boolean() )
    {
        return false;
    }

    const bool voltages = args["voltages"].get<bool>();
    const bool currents = args["currents"].get<bool>();

    pcbjam_collab::runOnCoroutine( fr, [voltages, currents]() {
        SCH_EDIT_FRAME* frame = schFrame();

        if( !frame || !frame->eeconfig() )
            return;

        EESCHEMA_SETTINGS* cfg = frame->eeconfig();
        cfg->m_Appearance.show_op_voltages = voltages;
        cfg->m_Appearance.show_op_currents = currents;

        auto* view = frame->GetCanvas()->GetView();
        view->SetLayerVisible( LAYER_OP_VOLTAGES, voltages );
        view->SetLayerVisible( LAYER_OP_CURRENTS, currents );

        frame->RefreshOperatingPointDisplay();
        frame->GetCanvas()->Refresh();
    } );

    return true;
}

// Circuit Center (engine run 14): properties, numbering and rules (the fields table, the
// symbol exchange, the global text edit and the settings sections), in their own file.
#include "cc_rules_sch.inc"

#ifdef KICAD_MERGED_EMBIND
// Circuit Center (engine run 12): the eeschema half of the board engine's schematic
// netlist, shared by Update the board (kicadUpdateBoardFromSchematic) and the board
// check's schematic parity (kicadCheckBoard), both in pcbnew_embind.cpp. aPlayer is the
// HIDDEN schematic editor the board engine opened (PCB_EDIT_FRAME::TestStandalone's road,
// minus every dialog). The steps of MAIL_SCH_GET_NETLIST and ReadyToNetlist with nothing
// asked: the power symbols numbered; CheckAnnotate with a COLLECTING handler (never the
// Annotate dialog): a part with no number is an "unannotated" problem, a designator used
// twice (or units that disagree) a "duplicate_designator" problem, and with any of them no
// netlist is made; duplicate sheet names a "duplicate_sheet_names" warning (never IsOK);
// then KiCad's own MAIL_SCH_GET_NETLIST handler with an empty payload (the connections
// recalculated when incremental connectivity is on, NETLIST_EXPORTER_KICAD with GNL_ALL |
// GNL_OPT_KICAD). Last, every screen is marked
// unmodified, so the next run's re-open raises no Save Changes prompt.
// Answers JSON (the two include roots stay apart, so plain strings cross):
// {"text", "blocked": [{code, ref, message}], "warnings": [{code, message}],
//  "sheets": [absolute top-level sheet files, in KiCad's order],
//  "files": [every sheet file the open read, absolute], "error", "message"}.
std::string ccSchNetlistForBoard( KIWAY_PLAYER* aPlayer )
{
    json out = json::object();
    out["text"] = "";
    out["blocked"] = json::array();
    out["warnings"] = json::array();
    out["sheets"] = json::array();
    out["files"] = json::array();
    out["error"] = "";
    out["message"] = "";

    SCH_EDIT_FRAME* sf = dynamic_cast<SCH_EDIT_FRAME*>( aPlayer );

    if( !sf )
    {
        out["error"] = "schematic_unreadable";
        out["message"] = "no schematic editor";
        return out.dump( -1, ' ', false, json::error_handler_t::replace );
    }

    try
    {
        SCHEMATIC& sch = sf->Schematic();

        for( SCH_SHEET* top : sch.GetTopLevelSheets() )
        {
            if( top && top->GetScreen() )
                out["sheets"].push_back( toUtf8( top->GetScreen()->GetFileName() ) );
        }

        sch.Hierarchy().AnnotatePowerSymbols();

        json blocked = json::array();

        sf->CheckAnnotate(
                [&]( ERCE_T aType, const wxString& aMsg, SCH_REFERENCE* aItemA, SCH_REFERENCE* )
                {
                    json problem = json::object();
                    problem["code"] = aType == ERCE_UNANNOTATED ? "unannotated"
                                                                : "duplicate_designator";
                    problem["ref"] = aItemA ? toUtf8( aItemA->GetSymbol()->GetRef(
                                                      &aItemA->GetSheetPath(), true ) )
                                            : std::string();
                    problem["message"] = toUtf8( aMsg );
                    blocked.push_back( std::move( problem ) );
                },
                ANNOTATE_ALL, true, SYMBOL_FILTER_NON_POWER );

        ERC_TESTER erc( &sch );

        if( erc.TestDuplicateSheetNames( false ) > 0 )
        {
            json warning = json::object();
            warning["code"] = "duplicate_sheet_names";
            warning["message"] = "Two sheets of the schematic share a name.";
            out["warnings"].push_back( std::move( warning ) );
        }

        if( blocked.empty() )
        {
            // KiCad's own handler with an empty payload (no ReadyToNetlist, checked above):
            // the connections recalculated when incremental connectivity is on, then
            // NETLIST_EXPORTER_KICAD with GNL_ALL | GNL_OPT_KICAD into the payload.
            std::string payload;
            sf->Kiway().ExpressMail( FRAME_SCH, MAIL_SCH_GET_NETLIST, payload, nullptr );
            out["text"] = payload;
        }

        out["blocked"] = std::move( blocked );

        SCH_SCREENS screens( sch.Root() );

        for( SCH_SCREEN* screen = screens.GetFirst(); screen; screen = screens.GetNext() )
        {
            screen->SetContentModified( false );

            if( !screen->GetFileName().IsEmpty() )
                out["files"].push_back( toUtf8( screen->GetFileName() ) );
        }
    }
    catch( const std::exception& e )
    {
        out["error"] = "schematic_unreadable";
        out["message"] = e.what();
    }
    catch( ... )
    {
        out["error"] = "schematic_unreadable";
        out["message"] = "the schematic could not be read";
    }

    return out.dump( -1, ' ', false, json::error_handler_t::replace );
}
#endif // KICAD_MERGED_EMBIND

// ════════════════════════ Circuit Center, engine run 14 (outputs) ════════════════════════
//
// The schematic half of the island's project.export (cap export), from the outputs spec
// (2026-11-16): kicadExportSchematicOutput runs ONE schematic output, a KiCad job by its registry
// name with KiCad's own settings JSON (6.2's merge, cc_output_common.h) or our cc_symbols, into
// /tmp/cc-out/<n>, its record <dir>/.cc-export.json {code, lines, dropped} written LAST. In this
// GUI build EESCHEMA_JOBS_HANDLER::getSchematic answers the editor's own schematic, unsaved edits
// included. Nothing in the design changes: no commit, no undo step, every sheet's modified flag
// as it was; no KiCad window opens (wxLogNull keeps wx's log window shut too).
namespace
{
/**
 * The four schematic plot jobs and their REGISTER_JOB lines live in
 * common/jobs/job_export_sch_plot.cpp, whose object nothing else in this image references (the
 * jobs handler only casts to the base class), so the linker left it out of the archive and the
 * registry never held sch_export_plot_* (measured on the first run 14 build: refused, and absent
 * from kicadOutputJobs). These references to its constructors pull the object in.
 */
__attribute__( ( used ) ) JOB* ( *const ccSchPlotJobsLinked[] )() = {
    []() -> JOB* { return new JOB_EXPORT_SCH_PLOT_PDF(); },
    []() -> JOB* { return new JOB_EXPORT_SCH_PLOT_SVG(); },
    []() -> JOB* { return new JOB_EXPORT_SCH_PLOT_DXF(); },
    []() -> JOB* { return new JOB_EXPORT_SCH_PLOT_PS(); },
};

/** One schematic output, read and checked before anything is queued. */
struct CC_SCH_OUTPUT
{
    std::string              dir;
    std::string              job;
    std::shared_ptr<JOB>     kicadJob;   // null for ours
    std::vector<wxString>    pages;      // the plot jobs only; empty: every sheet
    cc_output::MERGE_RESULT  merge;
};

/**
 * cc_symbols: SCH_EDITOR_CONTROL::ExportSymbolsToLibrary without its library chooser and table,
 * at its own defaults (power symbols left out, the schematic left linked as it is): each symbol
 * the design places, by its library id, flattened and written by the KiCad S-expression writer
 * into <root sheet's name>.kicad_sym. The file is made first.
 */
int ccRunSymbolsOutput( SCH_EDIT_FRAME* aFrame, const std::string& aDir,
                        cc_output::LINES_REPORTER& aLines )
{
    SCHEMATIC&       sch = aFrame->Schematic();
    const wxString   stem = wxFileName( sch.GetFileName() ).GetName();
    const wxString   path = wxFileName( wxString::FromUTF8( aDir.c_str() ), stem,
                                        wxS( "kicad_sym" ) ).GetFullPath();
    SCH_SHEET_LIST     sheets = sch.BuildSheetListSortedByPageNumbers();
    SCH_REFERENCE_LIST symbols;

    sheets.GetSymbols( symbols, SYMBOL_FILTER_NON_POWER );

    std::map<LIB_ID, LIB_SYMBOL*> libSymbols;

    for( size_t i = 0; i < symbols.GetCount(); ++i )
    {
        SCH_SYMBOL* symbol = symbols[i].GetSymbol();
        LIB_SYMBOL* libSymbol = symbol ? symbol->GetLibSymbolRef().get() : nullptr;

        if( libSymbol )
            libSymbols.emplace( libSymbol->GetLibId(), libSymbol );   // the first of an id kept
    }

    SCH_IO_KICAD_SEXPR io;
    io.CreateLibrary( path );   // throws IO_ERROR, an error line in the caller

    for( const auto& [id, origSym] : libSymbols )
    {
        std::unique_ptr<LIB_SYMBOL> flat = origSym->Flatten();
        io.SaveSymbol( path, flat.release() );   // the library cache owns it from here
    }

    if( libSymbols.empty() )
        aLines.Report( _( "No symbols to export." ), RPT_SEVERITY_WARNING );

    return 0;
}

/** One schematic output, on the apply coroutine; the record last. */
void ccRunSchematicOutput( const CC_SCH_OUTPUT& aArgs )
{
    cc_output::LINES_REPORTER lines;
    int                       code = -1;

    cc_output::EnsureDir( aArgs.dir );

    for( const std::string& warning : aArgs.merge.warnings )
        lines.Report( wxString::FromUTF8( warning.c_str() ), RPT_SEVERITY_WARNING );

    auto finish = [&]()
                  {
                      json done = json::object();
                      done["code"] = code;
                      done["lines"] = cc_output::LinesJson( lines );
                      done["dropped"] = aArgs.merge.dropped;
                      cc_output::WriteRecord( aArgs.dir, cc_output::RecordName(), done );
                  };

    SCH_EDIT_FRAME* fr = schFrame();   // re-resolved on the coroutine (S4)

    if( !fr || !fr->Kiway().KiFACE( KIWAY::FACE_SCH, false ) )
    {
        lines.Report( wxS( "no schematic editor to export" ), RPT_SEVERITY_ERROR );
        finish();
        return;
    }

    if( aArgs.kicadJob && ( !Pgm().IsGUI() || !Pgm().GetSettingsManager().IsProjectOpen() ) )
    {
        // getSchematic would load the saved files instead of the editor's schematic.
        lines.Report( wxS( "the job handler would not export the editor's schematic" ),
                      RPT_SEVERITY_ERROR );
        finish();
        return;
    }

    if( !aArgs.merge.refusal.empty() )
    {
        lines.Report( wxString::FromUTF8( aArgs.merge.refusal.c_str() ), RPT_SEVERITY_ERROR );
        finish();
        return;
    }

    wxLogNull   noLog;
    SCH_SCREENS screens( fr->Schematic().Root() );

    std::map<SCH_SCREEN*, bool> modified;

    for( SCH_SCREEN* screen = screens.GetFirst(); screen; screen = screens.GetNext() )
        modified[screen] = screen->IsContentModified();

    try
    {
        if( aArgs.kicadJob )
        {
            if( !aArgs.pages.empty() )
            {
                if( JOB_EXPORT_SCH_PLOT* plot = dynamic_cast<JOB_EXPORT_SCH_PLOT*>( aArgs.kicadJob.get() ) )
                {
                    plot->m_plotAll = false;
                    plot->m_plotPages = aArgs.pages;
                }
            }

            // As KiCad's jobset runner (kicad/jobs_runner.cpp): the run folder is the job's
            // temporary output directory and its own output path stays empty, so the job's
            // handler names the files as kicad-cli jobset run names them.
            aArgs.kicadJob->SetTempOutputDirectory( wxString::FromUTF8( aArgs.dir.c_str() ) );
            code = fr->Kiway().ProcessJob( KIWAY::FACE_SCH, aArgs.kicadJob.get(), &lines );
        }
        else if( aArgs.job == "cc_symbols" )
        {
            code = ccRunSymbolsOutput( fr, aArgs.dir, lines );
        }
        else
        {
            lines.Report( wxS( "this engine does not run that output" ), RPT_SEVERITY_ERROR );
        }
    }
    catch( const IO_ERROR& ioe )
    {
        lines.Report( ioe.What(), RPT_SEVERITY_ERROR );
        code = -1;
    }
    catch( const std::exception& e )
    {
        lines.Report( wxString::Format( wxS( "the %s output failed: %s" ),
                                        wxString::FromUTF8( aArgs.job.c_str() ),
                                        wxString::FromUTF8( e.what() ) ),
                      RPT_SEVERITY_ERROR );
        code = -1;
    }
    catch( ... )
    {
        lines.Report( wxString::Format( wxS( "the %s output failed" ),
                                        wxString::FromUTF8( aArgs.job.c_str() ) ),
                      RPT_SEVERITY_ERROR );
        code = -1;
    }

    // An output never marks a sheet changed.
    for( SCH_SCREEN* screen = screens.GetFirst(); screen; screen = screens.GetNext() )
    {
        auto was = modified.find( screen );

        if( was != modified.end() && !was->second && screen->IsContentModified() )
            screen->SetContentModified( false );
    }

    // A plot walks every sheet and puts the shown one back (SCH_PLOTTER's restore); the view
    // is asked to draw again, as the plot dialog's closing does.
    if( fr->GetCanvas() )
        fr->GetCanvas()->Refresh();

    finish();
}
} // namespace

// project.export, a schematic output (cap export). aArgsJson exactly {"run": int >= 0, "dir":
// "/tmp/cc-out/<n>", "job": one of cc_output::SchematicJobs(), "settings": object} and,
// optionally, "pages": [string] (0 to 1,024 page numbers as the sheets print them, each 1 to 32
// characters; empty means every sheet; non-empty only for a plot job, which then plots those
// pages). A KiCad job's settings are KiCad's own job JSON (a jobset's), merged by the outputs
// spec's 6.2; cc_symbols takes no settings. False (nothing queued) for anything else, with no
// schematic frame, or while an open is in flight; true once the run is queued (the host waits
// for the apply queue to empty and <dir>/.cc-export.json {"code", "lines", "dropped"}).
static bool kicadExportSchematicOutput( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || ( args.size() != 4 && args.size() != 5 ) )
        return false;

    auto run = args.find( "run" );
    auto dir = args.find( "dir" );
    auto job = args.find( "job" );
    auto settings = args.find( "settings" );
    auto pages = args.find( "pages" );

    if( run == args.end() || !run->is_number_integer() || run->get<long long>() < 0
            || dir == args.end() || !dir->is_string() || job == args.end() || !job->is_string()
            || settings == args.end() || !settings->is_object()
            || ( args.size() == 5 && pages == args.end() ) )
    {
        return false;
    }

    CC_SCH_OUTPUT a;
    a.dir = dir->get<std::string>();
    a.job = job->get<std::string>();

    if( !cc_output::IsRunDir( a.dir, cc_output::OutputRoot() )
            || !cc_output::Lists( cc_output::SchematicJobs(), a.job ) )
    {
        return false;
    }

    if( pages != args.end() )
    {
        if( !pages->is_array() || pages->size() > 1024 )
            return false;

        for( const json& page : *pages )
        {
            if( !page.is_string() )
                return false;

            const std::string number = page.get<std::string>();

            if( number.empty() || number.size() > 32 )
                return false;

            a.pages.push_back( wxString::FromUTF8( number.c_str() ) );
        }

        if( !a.pages.empty() && !cc_output::IsSchematicPlot( a.job ) )
            return false;
    }

    if( cc_output::IsOurs( a.job ) )
    {
        cc_output::OURS_SETTINGS ours;

        if( !cc_output::ReadOurs( a.job, *settings, ours ) )
            return false;
    }
    else
    {
        const wxString name = wxString::FromUTF8( a.job.c_str() );

        if( JOB_REGISTRY::GetKifaceType( name ) != KIWAY::FACE_SCH )
            return false;

        a.kicadJob.reset( JOB_REGISTRY::CreateInstance<JOB>( name ) );

        if( !a.kicadJob || !cc_output::MergeSettings( *a.kicadJob, a.job, *settings, a.merge ) )
            return false;
    }

    pcbjam_collab::runOnCoroutine( fr, [a]() { ccRunSchematicOutput( a ); } );
    return true;
}

EMSCRIPTEN_BINDINGS(eeschema) {
    // Programmatic save of the in-memory schematic (round-trip tests, README §A).
    function("kicadSaveSchematic", &kicadSaveSchematic);
    // eeschema-only ysync-review repro hook (name not shared with pcbnew).
    function("kicadCollabTestSetFieldText", &schCollabTestSetFieldText);
    // drift-trio phase B action hooks (tool-unique names, merged-image safe).
    function("kicadCollabTestAddWire", &schCollabTestAddWire);
    function("kicadCollabTestAddJunction", &schCollabTestAddJunction);
    function("kicadCollabTestAddNoConnect", &schCollabTestAddNoConnect);
    function("kicadCollabTestAddLabel", &schCollabTestAddLabel);
    function("kicadCollabTestAddSymbol", &schCollabTestAddSymbol);
    function("kicadCollabTestMoveSchItem", &schCollabTestMoveSchItem);
    function("kicadCollabTestMirrorSchItem", &schCollabTestMirrorSchItem);
    function("kicadCollabTestDuplicateSchItem", &schCollabTestDuplicateSchItem);
    // Sheet navigator bridge (sheet-panel): hierarchy tree + navigate.
    function("kicadSheetsGetTree", &schSheetsGetTree);
    function("kicadSheetsEnter", &schSheetsEnter);
    // Non-KiCad schematic / Altium project import (eeschema-only name, so registered
    // here in both images; suspends like kicadOpenFile, hence the PARKER policy).
    function("kicadImportSchematic", &kicadImportSchematic PCBJAM_PARKER_POLICY);
    // The Editing window (island cc15): one symbol's properties read and written by uuid,
    // the parts numbered and the sheet checked (eeschema-only names, so registered here in
    // both images).
    function("kicadSymbolGet", &kicadSymbolGet);
    function("kicadSymbolSet", &kicadSymbolSet);
    function("kicadAnnotate", &kicadAnnotate);
    function("kicadCheckSheet", &kicadCheckSheet);
    // A placed symbol to the Symbol Editor and back (engine run 14, cap symbolEdit).
    function("kicadSymbolForEditor", &kicadSymbolForEditor);
    function("kicadSymbolPushToSchematic", &kicadSymbolPushToSchematic);
    // The simulator (engine run 14, island cc23): caps simModel (the catalog, a symbol's
    // model read and written, a model file read) and sim (the netlist, a run, Stop, the
    // operating point labels). The netlist answers a Promise: an IBIS part's model waits on
    // the simulator service (KIBIS), hence the PARKER policy.
    function("kicadSimModelCatalog", &kicadSimModelCatalog);
    function("kicadSimModelGet", &kicadSimModelGet);
    function("kicadSimModelSet", &kicadSimModelSet);
    function("kicadSimFileRead", &kicadSimFileRead);
    function("kicadSimNetlist", &kicadSimNetlist PCBJAM_PARKER_POLICY);
    function("kicadSimRun", &kicadSimRun);
    function("kicadSimStop", &kicadSimStop);
    function("kicadSimOpOnSheet", &kicadSimOpOnSheet);
    // Circuit Center (island cc23, engine run 14): one schematic output through KiCad's own job
    // or our symbol export, no dialog (project.export).
    function("kicadExportSchematicOutput", &kicadExportSchematicOutput);
    // Properties, numbering and rules (island cc23, engine run 14): every part's fields under
    // a variant, Change and Update Symbols, the global text and graphics edit.
    function("kicadSymbolsGet", &kicadSymbolsGet);
    function("kicadSymbolsSet", &kicadSymbolsSet);
    function("kicadSymbolsExchange", &kicadSymbolsExchange);
    function("kicadSchEdit", &kicadSchEdit);

#ifndef KICAD_MERGED_EMBIND
    // JS names ALSO registered by pcbnew_embind.cpp — in the merged image these are
    // registered once by kicad_editor_embind.cpp, dispatching on the active frame.
    // Programmatic file open (preferred over UI automation from the web app).
    function("kicadOpenFile", &kicadOpenFile PCBJAM_PARKER_POLICY);
    function("kicadOpenFileBusy", &kicadOpenFileBusy);
    function("kicadTestSetOpenPark", &kicadTestSetOpenPark);
    function("kicadCollabBusy", &kicadCollabBusyProbe);
    // Read-only viewer lock (read-only-viewer).
    function("kicadSetReadOnly", &kicadSetReadOnly);
    // Yjs collaborative bridge entry points (same contract as pl_editor).
    function("kicadCollabApply", &schCollabApply);
    function("kicadCollabSnapshot", &schCollabSnapshot);
    // v2 items bridge: per-item s-expr payloads (ysync 0008).
    function("kicadCollabApplyItems", &schCollabApplyItems);
    function("kicadPlaceImportedItem", &schPlaceImportedItem);
    function("kicadCollabSnapshotItems", &schCollabSnapshotItems);
    function("kicadCollabTestMoveFirst", &schCollabTestMoveFirst);
    function("kicadCollabGetPos", &schCollabGetPos);
    // ysync-review repro hooks shared with pcbnew (dispatched when merged).
    function("kicadCollabTestRemoveItem", &schCollabTestRemoveItem);
    function("kicadCollabTestRotateItem", &schCollabTestRotateItem);
    function("kicadCollabTestUndo", &schCollabTestUndo);
    function("kicadCollabTestUndoDepth", &schCollabTestUndoDepth);
    // Presence (collab-presence 0003) — shared names with pcbnew's 0002 set.
    function("kicadCollabPresenceStart", &schCollabPresenceStart);
    function("kicadCollabSetRemote", &schCollabSetRemote);
    function("kicadCollabSetRemoteCursors", &schCollabSetRemoteCursors);
    function("kicadCollabSetPins", &schCollabSetPins);
    function("kicadCollabSetViewport", &schCollabSetViewport);
    // Follow-user (collab-presence 0008).
    function("kicadCollabFitViewport", &schCollabFitViewport);
    function("kicadCollabSetStyle", &schCollabSetStyle);
    // Live color-theme switch (comments-ux 0002 F4).
    function("kicadSetColorTheme", &schSetColorTheme);
    function("kicadSetDarkChrome", &schSetDarkChrome);
    function("kicadCollabTestListItems", &schCollabTestListItems);
    function("kicadCollabTestDemoSet", &schCollabTestDemoSet);
    function("kicadCollabGetViewport", &schCollabGetViewport);
    function("kicadCollabGetSelection", &schCollabGetSelection);
    // Cross-app selection (0006).
    function("kicadCollabGetSelectionFull", &schCollabGetSelectionFull);
    function("kicadCollabTestGetCrossMapped", &schCollabTestGetCrossMapped);
    // Selection soft-locks (0007).
    function("kicadCollabReleaseSelection", &schCollabReleaseSelection);
    function("kicadCollabTestGetLocked", &schCollabTestGetLocked);
    function("kicadCollabTestSelectFirst", &schCollabTestSelectFirst);
    function("kicadCollabTestSelectComponent", &schCollabTestSelectComponent);
    function("kicadCollabTestSelectByUuid", &schCollabTestSelectByUuid);
    function("kicadCollabTestClearSelection", &schCollabTestClearSelection);
    // Library reload after a remote (synced) lib edit — r2-idb-sync realtime.
    function("kicadLibsReload", &pcbjam_libs::reloadLibrary PCBJAM_PARKER_POLICY);
    // Runtime lib-table row insert + load (a new team library appeared
    // mid-session; the lib set is otherwise frozen at boot).
    function("kicadLibsAddEntry", &pcbjam_libs::addLibraryEntry PCBJAM_PARKER_POLICY);
    // Placed-instance count for a library symbol (drives the "a symbol you are
    // using was updated" toast after a remote lib edit).
    function("kicadLibsSymbolUsage", &schLibsSymbolUsage);
    // Update placed symbols from the library (libs 0017 §2c).
    function("kicadUpdateFromLibrary", &schUpdateFromLibraryShim);
    // Settings sections (engine run 14); the merged image dispatches these two names.
    function("kicadSettingsGet", &schSettingsGet);
    function("kicadSettingsSet", &schSettingsSet);
#endif // !KICAD_MERGED_EMBIND
}
#endif
