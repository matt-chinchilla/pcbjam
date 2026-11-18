/*
 * Embind bindings for KiCad WASM
 * Exposes core PCBnew objects to JavaScript
 *
 * Modified by Circuit Center Inc on 2026-11-18: engine run 15's seams, cc_fp_items.h (the
 * footprint item kinds, declarations) and one include per author lane's board face
 * (cc_eN_pcb.inc) after cc_rules_pcb.inc, each with its own namespace and bindings block.
 *
 * This provides a foundation for future Pyodide integration.
 *
 * Note: GetBoard() is not available when KICAD_SCRIPTING=OFF.
 * These bindings expose the classes for use when a board reference
 * is obtained through other means (e.g., from the UI).
 */

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/bind.h>
#include <board.h>
#include <board_commit.h>
#include <pcb_io/kicad_sexpr/pcb_io_kicad_sexpr.h>
#include <board_item.h>
#include <footprint.h>
#include <pad.h>
#include <pcb_track.h>
#include <pcb_field.h>
#include <pcb_shape.h>
#include <pcb_text.h>
#include <pcb_group.h>
#include <zone.h>
#include <eda_text.h>
#include <pcb_edit_frame.h>
#include <footprint_edit_frame.h>
#include <footprint_library_adapter.h>
#include <project_pcb.h>
#include <lib_id.h>
#include <kicad_clipboard.h>
#include <io/kicad/kicad_io_utils.h>
#include <richio.h>
#include <tools/pcb_selection.h>
#include <tools/pcb_selection_tool.h>
#include <settings/color_settings.h>
#include <widgets/appearance_controls.h>
#include <geometry/shape_poly_set.h>
#include <geometry/shape_line_chain.h>
#include <geometry/eda_angle.h>
#include <kiway_player.h>
#include <kiway.h>
#include <kiid.h>
#include <layer_ids.h>
#include <lset.h>
#include <math/util.h>
#include <tool/actions.h>
#include <tool/coroutine.h>
#include <tool/tool_manager.h>
#include <tool/tool_event.h>
#include <tools/pcb_actions.h>
#include <pcbnew_settings.h>
#include <view/view_controls.h>
#include <pcbjam_remote_lock.h>
#include <pcbjam_read_only.h>
#include <project.h>
#include <view/view.h>
#include <view/view_overlay.h>
#include <pcb_draw_panel_gal.h>
#include <exporters/place_file_exporter.h>
#include <exporters/export_hyperlynx.h>
#include <ki_exception.h>
#include <zones.h>
#include <eda_item_flags.h>
#include <functional>
#include <jobs/job_export_pcb_gerbers.h>
#include <jobs/job_export_pcb_drill.h>
#include <netlist_reader/pcb_netlist.h>
#include <netlist_reader/netlist_reader.h>
#include <netlist_reader/board_netlist_updater.h>
#include <drc/drc_engine.h>
#include <drc/drc_item.h>
#include <drc/drc_rule.h>
#include <drc/drc_rule_condition.h>
#include <tools/drc_tool.h>
#include <tools/zone_filler_tool.h>
#include <pcb_marker.h>
#include <pcb_textbox.h>
#include <pcb_table.h>
#include <pcb_tablecell.h>
#include <pcb_dimension.h>
#include <pcb_reference_image.h>
#include <pcb_barcode.h>
#include <board_design_settings.h>
#include <netclass.h>
#include <netinfo.h>
#include <project/project_file.h>
#include <string_utils.h>
#include <units_provider.h>
#include <wildcards_and_files_ext.h>
#include <wx/log.h>
#include <cmath>
#include <cstring>
#include <pgm_base.h>
#include <reporter.h>
#include <settings/settings_manager.h>
#include <nlohmann/json.hpp>
#include "collab_common.h"
#include "collab_presence_core.h"
#include "open_gate.h"
#include "pcbjam_async_policy.h"
#include "timer_park.h"
#include "collab_presence_style.h"
#include "pcbjam_theme.h"
#include "pcbjam_libs_reload.h"
#include "cc_output_common.h"
#include "cc_fp_items.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <wx/app.h>
#include <wx/event.h>
#include <wx/filename.h>
#include <wx/string.h>
#include <wx/window.h>

using namespace emscripten;
using json = nlohmann::json;

// Programmatically open a project file (board/schematic) in the running editor
// frame, without UI automation. Mirrors single_top.cpp's MacOpenFile path:
// the editor frame is the app's top window and is a KIWAY_PLAYER. Returns the
// result of OpenProjectFiles, or false if no frame is available — letting the
// JS caller fall back to driving File→Open.
//
// KICAD_MERGED_EMBIND (kicad_editor, editor-unification Part 2): eeschema_embind.cpp
// defines the identical function and registers the same JS names — in the merged image
// the frame-agnostic duplicates (this + kicadCollabOnSave) and the shared-name
// registrations live once in kicad_editor_embind.cpp, which dispatches the per-editor
// entries (renamed pcbCollab*/schCollab* below; JS-facing names are unchanged).
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

// Test-only (timer-park repro, timer_park.h): a one-shot wx timer whose
// Notify() suspends — the deterministic concurrent-suspension window.
bool kicadTestArmTimerPark( int aDelayMs, int aParkMs )
{
    return pcbjam_timer_park::arm( aDelayMs, aParkMs );
}

std::string kicadTestTimerParkState()
{
    return pcbjam_timer_park::stateJson();
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

// ───────────────────────────── Yjs collaborative bridge ─────────────────────────────
//
// pcbnew's half of the unified bridge contract (features/yjs-bridge 0001, 0004). Like
// eeschema it needs NO kicad-fork change: BOARD_ITEM already carries a stable KIID, and
// pcbnew has native change machinery, so the adapter is a thin re-use of public API:
//   ChangeSource (emit) = a BOARD_LISTENER subclass (BOARD_COMMIT::Push fires it)
//   apply              = BOARD_COMMIT Add/Modify/Remove + Push (drives connectivity +
//                        ratsnest recompute — mandatory on pcbnew, 0004 §apply)
// The generic JS reconciler / transport / WasmTool wiring are reused unchanged, and the
// emit/apply structure mirrors the (battle-tested) eeschema bridge:
//   - emit  = a POST-SETTLE snapshot DIFF (the listener is just a "something changed"
//             trigger; the real change set is a diff of the full model taken after the
//             edit's BOARD_COMMIT::Push — connectivity cleanup included — has returned,
//             so peers converge by re-applying already-clean geometry). See eeschema 0007.
//   - apply = BOARD_COMMIT run inside a CallAfter + COROUTINE, the exact context native
//             tool edits run in, serialized with every other apply/local edit through the
//             collab_common.h apply queue (eeschema 0007).
//
// Scope of this first commit (0004 §"first PoC", matching eeschema commit-3's first cut):
// position/geometry sync of existing items — changed (move/reshape) and removed work for
// ANY top-level item by uuid; `added` reconstructs PCB_TRACK segments natively. Footprint/
// via/zone `added` (which need a library or the s-expr clipboard blob, same class as the
// deferred SCH_SYMBOL add) are logged + skipped until a later commit. Net assignment +
// ratsnest are recomputed by BOARD_COMMIT::Push regardless.
namespace {

// Guard so BOARD_COMMIT::Push's listener callbacks during apply() aren't re-emitted.
bool s_applyingRemote = false;

using pcbjam_collab::toUtf8;

PCB_EDIT_FRAME* pcbFrame()
{
    return wxTheApp ? dynamic_cast<PCB_EDIT_FRAME*>( wxTheApp->GetTopWindow() ) : nullptr;
}

bool isTrackType( KICAD_T t )
{
    return t == PCB_TRACE_T || t == PCB_ARC_T || t == PCB_VIA_T;
}

// Read an item's layer WITHOUT the virtual GetLayer(). Under the retired asyncify runtime
// that virtual mis-dispatched in the non-coroutine emit/snapshot context — it returned 0
// (F_Cu) for EVERY item, silently putting every collab-added item/track/text on the top
// copper layer on the peer. The class-qualified `BOARD_ITEM::GetLayer()` is a statically-
// bound (direct) call that just reads m_layer — correct on any runtime, so it stays. Zones
// keep their layer in m_layerSet (not m_layer), so use their non-virtual GetFirstLayer().
int itemLayer( BOARD_ITEM* aItem )
{
    if( aItem->Type() == PCB_ZONE_T )
        return (int) static_cast<ZONE*>( aItem )->GetFirstLayer();

    return (int) aItem->BOARD_ITEM::GetLayer();
}

// Iterate every board item the bridge syncs: the top-level items (tracks, footprints, drawings,
// zones, groups) PLUS each footprint's TEXT children (fields = reference/value/user, and graphic
// PCB_TEXT). The text children are visited by their OWN uuid because a silkscreen reference/value
// can be moved *independently* of its footprint (the footprint origin doesn't change, so syncing
// the footprint as a unit would miss it). Pads and footprint graphic shapes are NOT visited — they
// move only with the footprint. All these uuids live in BOARD::m_itemByIdCache, so apply resolves
// them directly. On a whole-footprint move the children also re-appear in the diff (their absolute
// positions changed); that's redundant but convergent, since apply uses absolute SetPosition.
template <typename Fn>
void forEachTopItem( BOARD& aBoard, Fn&& aFn )
{
    for( PCB_TRACK* t : aBoard.Tracks() )       aFn( static_cast<BOARD_ITEM*>( t ) );

    for( FOOTPRINT* f : aBoard.Footprints() )
    {
        aFn( static_cast<BOARD_ITEM*>( f ) );

        for( PCB_FIELD* fld : f->GetFields() )
        {
            if( fld )
                aFn( static_cast<BOARD_ITEM*>( fld ) );
        }

        for( BOARD_ITEM* g : f->GraphicalItems() )
        {
            if( g->Type() == PCB_TEXT_T )
                aFn( g );
        }
    }

    for( BOARD_ITEM* d : aBoard.Drawings() )    aFn( d );
    for( ZONE* z : aBoard.Zones() )             aFn( static_cast<BOARD_ITEM*>( z ) );
    for( PCB_GROUP* g : aBoard.Groups() )       aFn( static_cast<BOARD_ITEM*>( g ) );
}

// The diff/wire unit for one board item: the fields apply() can act on. Tracks carry their two
// endpoints + width (they reshape, like an eeschema SCH_LINE); everything else syncs position.
// Deliberately NO opaque s-expr blob here — keeping the diff unit to the applicable fields
// avoids broadcasting `changed` entries the peer can only partially apply (which would diverge
// then loop). Added-item reconstruction is handled type-by-type in makeItem instead.
json itemToJson( BOARD_ITEM* aItem )
{
    VECTOR2I p = aItem->GetPosition();
    json     j = {
        { "id", toUtf8( aItem->m_Uuid.AsString() ) },
        { "type", toUtf8( aItem->GetClass() ) },
        { "x", p.x },   // internal units (nm); integral, no quantization needed
        { "y", p.y },
        { "layer", itemLayer( aItem ) },   // devirtualized — aItem->GetLayer() mis-dispatches here
    };

    // Parent footprint uuid (or absent for roots). Carried in the baseline so a
    // REMOVED child can still be attributed to its parent after the live item is
    // gone — flushDiff lifts such removals to a parent re-blob on the v2 wire.
    if( FOOTPRINT* fp = aItem->GetParentFootprint() )
        j["parent"] = toUtf8( fp->m_Uuid.AsString() );

    if( isTrackType( aItem->Type() ) )
    {
        auto* tr   = static_cast<PCB_TRACK*>( aItem );
        j["sx"]    = tr->GetStart().x;
        j["sy"]    = tr->GetStart().y;
        j["ex"]    = tr->GetEnd().x;
        j["ey"]    = tr->GetEnd().y;

        // The layerless PCB_VIA::GetWidth() is an assert trap (padstack refactor); pass the
        // whole-stack slot instead. applyChanged's layerless SetWidth writes the same slot.
        if( aItem->Type() == PCB_VIA_T )
            j["width"] = static_cast<PCB_VIA*>( aItem )->GetWidth( PADSTACK::ALL_LAYERS );
        else
            j["width"] = tr->GetWidth();
    }

    // Vias and zones reconstruct NATIVELY on `added` (the s-expr clipboard blob's `(kicad_pcb …)`
    // envelope parse — used for footprints — proved fragile for these under the retired asyncify
    // runtime, the same wall that deferred the eeschema symbol blob; the native path stays as the
    // simpler, pinned-by-tests route). So emit the geometry their makeItem needs.
    if( aItem->Type() == PCB_VIA_T )
    {
        auto* via  = static_cast<PCB_VIA*>( aItem );
        j["drill"] = via->GetDrillValue();
        j["ltop"]  = (int) via->TopLayer();
        j["lbot"]  = (int) via->BottomLayer();
    }
    else if( aItem->Type() == PCB_ZONE_T )
    {
        auto*                 zone = static_cast<ZONE*>( aItem );
        const SHAPE_POLY_SET* poly = zone->Outline();
        json                  pts  = json::array();

        if( poly && poly->OutlineCount() > 0 )
        {
            const SHAPE_LINE_CHAIN& chain = poly->COutline( 0 );

            for( int i = 0; i < chain.PointCount(); ++i )
            {
                const VECTOR2I& p = chain.CPoint( i );
                pts.push_back( { p.x, p.y } );
            }
        }

        j["poly"] = pts;
    }
    // A board-level graphic text (Place→Text) also reconstructs NATIVELY (same reason as
    // via/zone): emit its size / stroke / angle so makeItem can rebuild it. Footprint child text
    // is synced by move, not `added`, so this is only the board PCB_TEXT case.
    else if( aItem->Type() == PCB_TEXT_T )
    {
        auto* txt    = static_cast<PCB_TEXT*>( aItem );
        j["tw"]      = txt->GetTextSize().x;
        j["th"]      = txt->GetTextSize().y;
        j["thick"]   = txt->GetTextThickness();
        j["angle"]   = txt->GetTextAngle().AsTenthsOfADegree();
        // Justification + mirror anchor the glyphs relative to the text POSITION; without them a
        // left-justified text reconstructs centered and renders visibly offset from the same
        // GetPosition() (peers diverge visually though GetPosition matches). Bold/italic for looks.
        j["hjust"]   = (int) txt->GetHorizJustify();
        j["vjust"]   = (int) txt->GetVertJustify();
        j["mirror"]  = txt->IsMirrored();
        j["bold"]    = txt->IsBold();
        j["italic"]  = txt->IsItalic();
    }

    // Text items (incl. footprint fields / graphic text): carry the string so a move diff is
    // legible and a text `added` can reconstruct. Position-only sync uses x/y above.
    if( EDA_TEXT* txt = dynamic_cast<EDA_TEXT*>( aItem ) )
        j["text"] = toUtf8( txt->GetText() );

    return j;
}

// ── s-expr item blob (the generic `added` mechanism) ─────────────────────────────────────────
//
// For added items beyond the natively-reconstructed PCB_TRACK (footprints, vias, zones, graphic
// shapes/text…), serialize one item with the BOARD writer's control set: a bare `(footprint …)`
// for a footprint, or a fake `(kicad_pcb … <layers> <item>)` envelope for everything else (the
// bare item tokens like `(segment`/`(via`/`(zone` are NOT accepted by the parser top-level, so
// the envelope is required).
//
// The envelope still comes from CLIPBOARD_IO::SaveSelection, which is also what supplies the
// `(layers …)` block the parser needs; CLIPBOARD_IO normally talks to the system clipboard, so
// SetWriter/SetReader redirect it to a string to work headless / in wasm. Footprints take the
// dedicated path below instead — see blobForItem for why the clipboard dialect is wrong here.

// A board writer we can point at a BOARD. PCB_IO_KICAD_SEXPR's default control set is
// CTL_FOR_BOARD — exactly what a .kicad_pcb save uses — but only CLIPBOARD_IO exposes a
// public SetBoard(); m_board is protected on PCB_IO, so a two-line subclass gets us the
// file writer without a fork change.
class WIRE_BOARD_IO : public PCB_IO_KICAD_SEXPR
{
public:
    explicit WIRE_BOARD_IO( BOARD* aBoard ) : PCB_IO_KICAD_SEXPR( CTL_FOR_BOARD )
    {
        m_board = aBoard;
    }
};

// Serialize one live board item to a wire blob (used only for `added` payloads — NOT the
// diff unit, so `changed`/`removed` stay light and the blob never drives change detection).
//
// The blob MUST be byte-equal to that item's subtree in a full .kicad_pcb save: the Y.Doc is
// the source of truth for the FILE, so any writer difference is a permanent, unfixable drift
// and corrupts what a server-side materialize writes.
//
// That is why footprints use CTL_FOR_BOARD and not CLIPBOARD_IO's CTL_FOR_CLIPBOARD. The two
// differ by exactly CTL_OMIT_FOOTPRINT_VERSION (pcb_io_kicad_sexpr.h), so clipboard form emits
//     (footprint "Lib:C1206" (version 20260206) (generator "pcbnew") (generator_version "10.0") …)
// — three tokens a board-embedded footprint never has (pcb_io_kicad_sexpr.cpp ~1201). Every
// footprint of every board drifted on those three lines.
//
// Non-footprints DON'T use SaveSelection either (drift-trio finding, standalone-hardening
// 0008 §10): its "make safe to transfer" step clears the locked flag on the copy
// (kicad_clipboard.cpp ~416) — clipboard semantics; pastes are unlocked — so `(locked yes)`,
// real file content, vanished from every track/via/zone/text blob and a peer's lock never
// reached the doc. Instead the live item is Formatted directly with the FILE writer and
// wrapped in the same synthetic `(kicad_pcb … <layers> <item>)` envelope the apply path
// already parses (the parser rejects bare `(segment`/`(via`/`(zone` at top level).
//
// Footprints also DON'T go through SaveSelection: its "make safe to transfer" step copies the
// footprint, and FOOTPRINT's copy ctor ASSIGNS the mandatory fields into the new footprint's
// freshly-constructed ones (`*existingField = *field`; EDA_ITEM::operator= keeps the target's
// uuid) — so Reference/Value/Datasheet/Description would carry NEW uuids in every blob,
// breaking the wire's identity-by-uuid (every emit would read as field remove+add, and round
// trips lose the field uuids). The copy ctor now restores those uuids itself (footprint.cpp),
// but we keep making the safety copy here so the live item is never mutated.
std::string wrapInBoardEnvelope( BOARD& aBoard, const std::string& aItemSexpr );

std::string blobForItem( BOARD* aBoard, BOARD_ITEM* aItem )
{
    // P-5 (findings group P): the FILE writer cannot format a footprint FIELD standalone —
    // PCB_IO_KICAD_SEXPR::Format has an explicit `case PCB_FIELD_T: break;` (fields are
    // written by the footprint formatter), so a field reaching here produced a bare
    // `(kicad_pcb … (layers …))` envelope with no uuid-bearing item, which the JS wire
    // (unwrapWireItem) could not resolve. Fields only ever travel inside their footprint's
    // blob; refuse to blob one on its own — callers skip an empty blob.
    if( aItem->Type() == PCB_FIELD_T )
        return "";

    if( aItem->Type() == PCB_FOOTPRINT_T )
    {
        const FOOTPRINT* src = static_cast<const FOOTPRINT*>( aItem );
        FOOTPRINT        copy( *src );

        // The rest of SaveSelection's footprint safety steps, minus the refPoint move
        // (the wire carries absolute positions) and minus SetNetCode(0): zeroing pad
        // nets is a paste-into-FOREIGN-board safety, but collab peers edit the SAME
        // board — nets must survive the wire. KiCad 10 formats pad nets by NAME and
        // makeFromBlob parses them in a temporary board, then maps them by name
        // onto the receiver's board (creating any missing nets).
        //
        // NOTE: unlike SaveSelection we do NOT SetLocked( false ) — `(locked yes)` is
        // real file content and dropping it would drift against the save.
        WIRE_BOARD_IO    io( aBoard );
        STRING_FORMATTER fmt;
        io.SetOutputFormatter( &fmt );
        io.Format( &copy );

        copy.SetParent( nullptr );
        copy.SetParentGroup( nullptr );

        std::string out = fmt.GetString();
        KICAD_FORMAT::Prettify( out, KICAD_FORMAT::FORMAT_MODE::COMPACT_TEXT_PROPERTIES );
        return out;
    }

    // Direct file-writer Format of the LIVE item (no mutation, unlike
    // SaveSelection's transfer copy) + the apply path's own envelope.
    WIRE_BOARD_IO    io( aBoard );
    STRING_FORMATTER fmt;
    io.SetOutputFormatter( &fmt );
    io.Format( aItem );

    std::string body = fmt.GetString();
    KICAD_FORMAT::Prettify( body, KICAD_FORMAT::FORMAT_MODE::COMPACT_TEXT_PROPERTIES );
    return wrapInBoardEnvelope( *aBoard, body );
}

// Reconstruct a board item in a temporary BOARD, remap its nets, and detach it.
// Returns nullptr on a parse failure or if no item is found. Runs inside the apply COROUTINE.
BOARD_ITEM* makeFromBlob( BOARD& aBoard, const std::string& aBlobIn )
{
    if( aBlobIn.empty() )
        return nullptr;

    // PCB_IO_KICAD_SEXPR::Parse constructs its parser with a NULL board, regardless
    // of io.SetBoard(). A bare footprint is therefore parsed as a library item:
    // pad net names are ignored and the footprint is unlocked. Every remote move
    // replaces the footprint, so this used to erase its connections on the peer.
    // Use the board envelope for footprints too. It supplies the format version
    // (including field-visibility rules) and a net table, then MapNets below binds
    // the pads to the live board by name. The wire/Y.Doc stays in board-file form.
    const size_t first = aBlobIn.find_first_not_of( " \t\r\n" );
    const std::string aBlob = first != std::string::npos
                                     && aBlobIn.compare( first, 10, "(footprint" ) == 0
                             ? wrapInBoardEnvelope( aBoard, aBlobIn )
                             : aBlobIn;

    CLIPBOARD_IO io;
    io.SetBoard( &aBoard );

    // Parse directly (not io.Parse(), whose catch(...) swallows the error): a
    // failed apply must say WHY, or wire bugs surface as silent non-convergence.
    BOARD_ITEM* parsed = nullptr;           // FOOTPRINT* (bare) | BOARD* (envelope) | nullptr

    try
    {
        parsed = io.PCB_IO_KICAD_SEXPR::Parse( wxString::FromUTF8( aBlob.c_str() ) );
    }
    catch( const IO_ERROR& e )
    {
        EM_ASM( { console.log( "[collab] pcbnew blob parse error: " + UTF8ToString( $0 ) ); },
                std::string( e.What().utf8_str() ).c_str() );
        return nullptr;
    }
    catch( ... )
    {
        EM_ASM( { console.log( "[collab] pcbnew blob parse error: unknown exception" ); } );
        return nullptr;
    }

    if( !parsed )
        return nullptr;

    if( parsed->Type() != PCB_T )
        return parsed;                      // bare footprint — ready to commit.Add

    // Envelope board: remap its net codes onto ours, then lift out the single item it carries.
    BOARD* clip = static_cast<BOARD*>( parsed );
    clip->MapNets( &aBoard );

    BOARD_ITEM* found = nullptr;

    if( !clip->Tracks().empty() )           found = clip->Tracks().front();      // track / via / arc
    else if( !clip->Zones().empty() )       found = clip->Zones().front();
    else if( !clip->Drawings().empty() )    found = clip->Drawings().front();    // shape / text / …
    else if( !clip->Footprints().empty() )  found = clip->Footprints().front();
    else if( !clip->Groups().empty() )      found = clip->Groups().front();

    if( found )
    {
        clip->Remove( found );              // detach so clip's dtor doesn't delete it
        // Reparent onto the REAL board before clip is freed: the item's m_parent still points at
        // clip, and commit.Push/saveCopyInUndoList dereferences GetParent() — a dangling pointer
        // here is what trapped via add ("index out of bounds") and tripped the zone undo assert.
        found->SetParent( &aBoard );
        found->SetParentGroup( nullptr );

        if( FOOTPRINT* fp = dynamic_cast<FOOTPRINT*>( found ) )
        {
            // Component classes also belong to the temporary board. Resolve the
            // parsed names against the destination before that board is freed,
            // just as PCB_CONTROL's native clipboard import does.
            fp->ResolveComponentClassNames( &aBoard, fp->GetTransientComponentClassNames() );
            fp->ClearTransientComponentClassNames();
        }
    }

    delete clip;
    return found;
}

// Wrap a BARE item s-expr (e.g. one rendered from the Y.Doc Slot body) in the fake
// `(kicad_pcb …)` envelope CLIPBOARD_IO's parser requires for non-footprint items. The
// envelope carries the LIVE board's layer table so the item's layer names resolve — peers
// in a collab session share the same board, so names map 1:1. (Peer-emitted blobs already
// arrive enveloped by blobForItem; this is only for bare payloads.)
std::string wrapInBoardEnvelope( BOARD& aBoard, const std::string& aItemSexpr )
{
    std::string s = "(kicad_pcb (version " + std::to_string( SEXPR_BOARD_FILE_VERSION )
                    + ") (generator \"pcbnew\") (layers";

    auto emit = [&]( PCB_LAYER_ID id )
    {
        const char* type = IsCopperLayer( id ) ? LAYER::ShowType( aBoard.GetLayerType( id ) )
                                               : "user";

        // CANONICAL name (LSET::Name), NOT GetLayerName(): the parser validates
        // position 2 against the fixed layer hash, and user-visible names differ
        // from canonical ones (e.g. "B.Courtyard" vs "B.CrtYd") — the envelope
        // parse threw "not in fixed layer hash" for any board with such layers.
        s += " (" + std::to_string( (int) id ) + " \""
             + std::string( LSET::Name( id ).utf8_str() ) + "\" " + type + ")";
    };

    // Copper first (front→back), then tech/user — the formatBoardLayers order.
    // parseLayers stops counting copper at the first non-copper entry and then
    // rejects the count (<2), so raw Seq() order (which interleaves copper and
    // non-copper ids in the v9 numbering) fails on EVERY bare payload.
    for( PCB_LAYER_ID id : aBoard.GetEnabledLayers().CuStack() )
        emit( id );

    for( PCB_LAYER_ID id : aBoard.GetEnabledLayers().TechAndUserUIOrder() )
        emit( id );

    s += ") " + aItemSexpr + ")";
    return s;
}

// Construct a new BOARD_ITEM from a delta item (for `added`), with the delta's uuid (m_Uuid is
// const → const_cast, exactly as the s-expr parser does). PCB_TRACK segments reconstruct natively
// from their fields (cheap, trap-free); every other type goes through the s-expr clipboard blob
// (`sexpr`, attached to added payloads by the emit side). Returns nullptr if neither applies.
BOARD_ITEM* makeItem( BOARD& aBoard, const json& j )
{
    std::string type = j.value( "type", "" );
    BOARD_ITEM* item = nullptr;

    if( type == "PCB_TRACK" && j.contains( "sx" ) )
    {
        auto* tr = new PCB_TRACK( &aBoard );
        tr->SetStart( VECTOR2I( j.value( "sx", 0 ), j.value( "sy", 0 ) ) );
        tr->SetEnd( VECTOR2I( j.value( "ex", 0 ), j.value( "ey", 0 ) ) );
        tr->SetWidth( j.value( "width", 0 ) );
        // SetLayer is virtual and (like GetLayer) mis-dispatches in this apply context → it no-ops,
        // leaving the item on the default layer. Class-qualify to a direct m_layer write.
        tr->BOARD_ITEM::SetLayer( (PCB_LAYER_ID) j.value( "layer", (int) F_Cu ) );
        item = tr;
    }
    // Via / zone: reconstruct natively from emitted geometry (the envelope-blob parse is
    // skipped for these — see itemToJson). The blob is still emitted as a fallback.
    else if( type == "PCB_VIA" && j.contains( "drill" ) )
    {
        auto*    via = new PCB_VIA( &aBoard );
        VECTOR2I c( j.value( "x", 0 ), j.value( "y", 0 ) );
        via->SetPosition( c );
        via->SetWidth( j.value( "width", 0 ) );
        via->SetDrill( j.value( "drill", 0 ) );
        via->SetLayerPair( (PCB_LAYER_ID) j.value( "ltop", (int) F_Cu ),
                           (PCB_LAYER_ID) j.value( "lbot", (int) B_Cu ) );
        item = via;
    }
    else if( type == "ZONE" && j.contains( "poly" ) )
    {
        auto*                 zone = new ZONE( &aBoard );
        std::vector<VECTOR2I> outline;

        for( const json& p : j["poly"] )
        {
            if( p.is_array() && p.size() == 2 )
                outline.emplace_back( p[0].get<int>(), p[1].get<int>() );
        }

        // Zones keep their layer in m_layerSet via SetLayer→SetLayerSet (both virtual, both no-op
        // here). Class-qualify ZONE::SetLayerSet to set it directly (else GetFirstLayer == -1).
        zone->ZONE::SetLayerSet( LSET( { (PCB_LAYER_ID) j.value( "layer", (int) F_Cu ) } ) );

        if( outline.size() >= 3 )
            zone->AddPolygon( outline );

        item = zone;
    }
    else if( type == "PCB_TEXT" && j.contains( "text" ) )
    {
        auto* txt = new PCB_TEXT( &aBoard );
        txt->SetText( wxString::FromUTF8( j.value( "text", "" ).c_str() ) );
        txt->SetPosition( VECTOR2I( j.value( "x", 0 ), j.value( "y", 0 ) ) );
        txt->BOARD_ITEM::SetLayer( (PCB_LAYER_ID) j.value( "layer", (int) F_SilkS ) ); // devirt

        if( j.contains( "tw" ) )
            txt->SetTextSize( VECTOR2I( j.value( "tw", 0 ), j.value( "th", 0 ) ) );

        if( j.contains( "thick" ) )
            txt->SetTextThickness( j.value( "thick", 0 ) );

        if( j.contains( "angle" ) )
            txt->SetTextAngle( EDA_ANGLE( j.value( "angle", 0 ), TENTHS_OF_A_DEGREE_T ) );

        // Restore justification/mirror so the glyphs sit at the same place relative to position.
        if( j.contains( "hjust" ) )
            txt->SetHorizJustify( (GR_TEXT_H_ALIGN_T) j.value( "hjust", (int) GR_TEXT_H_ALIGN_LEFT ) );

        if( j.contains( "vjust" ) )
            txt->SetVertJustify( (GR_TEXT_V_ALIGN_T) j.value( "vjust", (int) GR_TEXT_V_ALIGN_CENTER ) );

        if( j.contains( "mirror" ) )
            txt->SetMirrored( j.value( "mirror", false ) );

        if( j.contains( "bold" ) )
            txt->SetBold( j.value( "bold", false ) );

        if( j.contains( "italic" ) )
            txt->SetItalic( j.value( "italic", false ) );

        item = txt;
    }
    else if( j.contains( "sexpr" ) )
    {
        item = makeFromBlob( aBoard, j.value( "sexpr", "" ) );
    }

    // Force the delta's uuid (the blob already carries the sender's uuid for the item and any
    // children, but set the top-level one explicitly to be certain peers agree on identity).
    if( item )
        const_cast<KIID&>( item->m_Uuid ) = KIID( wxString::FromUTF8( j.value( "id", "" ).c_str() ) );

    return item;
}

// Set an existing item's geometry from a `changed` delta. Tracks reshape via their endpoints
// (independent — like an eeschema wire); everything else moves to an absolute position.
// SetStart/SetEnd/SetPosition run inside the apply COROUTINE (see kicadCollabApply), the same
// context native edits use, serialized through the apply queue.
void applyChanged( BOARD_ITEM* aItem, const json& j )
{
    if( isTrackType( aItem->Type() ) && j.contains( "sx" ) )
    {
        auto* tr = static_cast<PCB_TRACK*>( aItem );
        tr->SetStart( VECTOR2I( j["sx"].get<int>(), j["sy"].get<int>() ) );
        tr->SetEnd( VECTOR2I( j["ex"].get<int>(), j["ey"].get<int>() ) );

        if( j.contains( "width" ) )
            tr->SetWidth( j["width"].get<int>() );
    }
    else if( j.contains( "x" ) && j.contains( "y" ) )
    {
        aItem->SetPosition( VECTOR2I( j["x"].get<int>(), j["y"].get<int>() ) );
    }
}

// Wire emitters (legacy scalar delta + v2 items): shared, collab_common.h.
using pcbjam_collab::emitDelta;
using pcbjam_collab::emitItemsWire;

// ── Emit via post-settle snapshot diff (mirrors eeschema 0007) ───────────────────────────────
//
// A local edit is one BOARD_COMMIT::Push that fires the listener callbacks synchronously and
// THEN recomputes connectivity/ratsnest. The native listener therefore only ever sees the
// pre-cleanup geometry. So treat the listener purely as a "something changed" trigger and
// broadcast a DIFF of the full model taken AFTER the edit settles (a CallAfter, which runs once
// Push has fully returned) — capturing this tab's FINAL geometry. The peer applies that and
// re-applying already-settled geometry is idempotent, so the two converge. g_baseline holds the
// last-broadcast state.

std::map<std::string, json> snapshotByUuid( BOARD& aBoard )
{
    std::map<std::string, json> m;

    forEachTopItem( aBoard, [&]( BOARD_ITEM* item )
                    {
                        std::string id = toUtf8( item->m_Uuid.AsString() );

                        if( !m.count( id ) )
                            m[id] = itemToJson( item );
                    } );

    return m;
}

std::map<std::string, json> g_baseline;
bool                        g_flushScheduled = false;

// Roots the listener saw change since the last flush (uuids, children lifted to
// their footprint at capture time). The scalar snapshot diff below is a LOSSY
// projection (id/type/x/y/layer + a few extras) — edits that don't move the
// projection (pad/zone property edits, anchor-centred rotations, endpoint drags)
// would otherwise never emit (bug 04). Dirty roots emit their v2 blob
// unconditionally; the wire apply is an idempotent upsert, so a false positive
// (a commit that changed nothing) costs one no-op echo.
std::set<std::string> g_dirty;

void noteDirty( BOARD_ITEM* aItem )
{
    if( !aItem )
        return;

    if( FOOTPRINT* fp = aItem->GetParentFootprint() )
        aItem = fp;

    g_dirty.insert( toUtf8( aItem->m_Uuid.AsString() ) );
}

// Re-seed the diff baseline to the current model — after handing out a seed snapshot, or after
// applying a remote delta (so those items aren't re-broadcast as a spurious local diff/echo).
// Declares "current model == broadcast state", so pending dirty marks are stale too.
void rebaseline()
{
    if( PCB_EDIT_FRAME* fr = pcbFrame() )
        g_baseline = snapshotByUuid( *fr->GetBoard() );

    g_dirty.clear();
}

// TARGETED rebaseline (bug 05): refresh baseline entries ONLY for the uuids a remote
// apply touched. A global rebaseline() here would fold a concurrently-committed local
// edit (its flush is queued BEHIND the apply on the same pending-event list) into the
// baseline and silently swallow it; with the targeted update the edit's uuids keep
// their pre-edit entries and the queued flush still emits it. Receiver-side cleanup
// the apply's Push produced likewise stays diffable — the post-apply flush broadcasts
// it, and re-application on the original sender is idempotent.
void rebaselineTouched( BOARD* aBoard, const std::vector<std::string>& aIds )
{
    for( const std::string& id : aIds )
    {
        // Drop the stale entry — and any child entries it owned (their parent
        // field carries the root uuid) — then re-snapshot whatever is live now.
        g_baseline.erase( id );

        for( auto it = g_baseline.begin(); it != g_baseline.end(); )
        {
            if( it->second.value( "parent", std::string() ) == id )
                it = g_baseline.erase( it );
            else
                ++it;
        }

        BOARD_ITEM* live = aBoard->ResolveItem( KIID( wxString::FromUTF8( id.c_str() ) ),
                                                /*allowNull*/ true );

        if( !live )
            continue;

        g_baseline[id] = itemToJson( live );

        if( live->Type() == PCB_FOOTPRINT_T )
        {
            FOOTPRINT* f = static_cast<FOOTPRINT*>( live );

            for( PCB_FIELD* fld : f->GetFields() )
            {
                if( fld )
                    g_baseline[toUtf8( fld->m_Uuid.AsString() )] = itemToJson( fld );
            }

            for( BOARD_ITEM* g : f->GraphicalItems() )
            {
                if( g->Type() == PCB_TEXT_T )
                    g_baseline[toUtf8( g->m_Uuid.AsString() )] = itemToJson( g );
            }
        }
    }
}

// Diff the current (settled, post-cleanup) model against the baseline and broadcast the change.
void flushDiff()
{
    g_flushScheduled = false;

    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr )
        return;

    BOARD*                      board = fr->GetBoard();
    std::map<std::string, json> cur   = snapshotByUuid( *board );

    json added = json::array(), changed = json::array(), removed = json::array();

    // v2 items wire (per-item s-expr blobs): each touched id LIFTS to its root live
    // item (footprint children → the footprint), deduped, and the root is blobbed
    // whole — so containment travels and a child edit re-sends its parent subtree.
    json                  wAdded = json::array(), wChanged = json::array();
    std::set<std::string> wDone;

    // Roots that reached the dirty set already STRUCT_DELETED. The baseline
    // diff can miss such a deletion (observed 2026-08-31: 8 footprints deleted
    // by a netlist-update/undo churn were P-5-skipped with removed:0 and
    // survived in the board room forever) — collect them here and emit the
    // removals explicitly below instead of dropping the event.
    std::set<std::string> deletedDirtyRoots;

    auto liftBlob = [&]( const std::string& id, json& aArr )
    {
        BOARD_ITEM* live =
                board->ResolveItem( KIID( wxString::FromUTF8( id.c_str() ) ), /*allowNull*/ true );

        if( !live )
            return;

        bool lifted = false;

        if( FOOTPRINT* fp = live->GetParentFootprint() )
        {
            live = fp;
            lifted = true;
        }

        std::string rootId = toUtf8( live->m_Uuid.AsString() );

        if( !wDone.insert( rootId ).second )
            return;

        std::string sexpr = blobForItem( board, live );

        // P-5: a root the writer cannot serialize standalone (a footprint FIELD whose parent
        // is not a footprint, or a deleted item still resolvable through the id cache) must
        // not reach the wire as a hollow envelope. Skip it and leave a breadcrumb — the
        // trigger that lets a field arrive here unlifted is still unknown
        // (memory: items-wire-batch-loss); this line is the hunt.
        if( sexpr.empty() || ( live->GetFlags() & STRUCT_DELETED ) )
        {
            std::string why = sexpr.empty() ? "empty blob" : "STRUCT_DELETED";
            std::string type = toUtf8( live->GetClass() );
            int parentNull = live->GetParent() == nullptr ? 1 : 0;
            EM_ASM( {
                console.warn( '[pcbjam collab] P-5: skipped un-serializable dirty root',
                              { uuid: UTF8ToString( $0 ), type: UTF8ToString( $1 ),
                                why: UTF8ToString( $2 ), parentNull: !!$3 } );
            }, rootId.c_str(), type.c_str(), why.c_str(), parentNull );

            // A DELETED root must still leave the shared doc: skipping it here
            // while the baseline diff misses it strands the item in the room.
            if( live->GetFlags() & STRUCT_DELETED )
                deletedDirtyRoots.insert( rootId );

            return;
        }

        json w = json{ { "sexpr", sexpr }, { "parent", nullptr } };

        // A lifted child means its (pre-existing) parent's CONTENT changed.
        ( lifted ? wChanged : aArr ).push_back( w );
    };

    for( const auto& [id, j] : cur )
    {
        auto it = g_baseline.find( id );

        if( it == g_baseline.end() )
        {
            liftBlob( id, wAdded );

            // Skip a newly-added footprint's text CHILDREN: the footprint's own add carries them,
            // and emitting a lone child would (for a field) wrap it in a spurious footprint. (A
            // child-only add onto an existing footprint is therefore not synced yet — rare.)
            BOARD_ITEM* live =
                    board->ResolveItem( KIID( wxString::FromUTF8( id.c_str() ) ), /*allowNull*/ true );

            if( live && live->GetParentFootprint() )
                continue;

            json withBlob = j;

            // Attach an s-expr clipboard blob ONLY for types makeItem reconstructs from it
            // (footprints, board graphics, …). Tracks/vias/zones/text rebuild NATIVELY from the
            // fields itemToJson already emitted, so they need no blob — and skipping it avoids a
            // wasted SaveSelection plus the envelope parse for those.
            if( live && !isTrackType( live->Type() ) && live->Type() != PCB_ZONE_T
                && live->Type() != PCB_TEXT_T )
            {
                std::string sexpr = blobForItem( board, live );

                if( !sexpr.empty() )    // P-5: never attach a hollow envelope
                    withBlob["sexpr"] = sexpr;
            }

            added.push_back( withBlob );
        }
        else if( it->second != j )
        {
            liftBlob( id, wChanged );
            changed.push_back( j );
        }
    }

    // v2 removals diverge from the legacy wire: a removed footprint CHILD whose
    // parent survives lifts to the parent's re-blob (wChanged) — the new body
    // carries the post-delete child set, and the receiver's parent-replace covers
    // the deletion. A bare child removal would strand a dangling {item} slot in
    // the Y-side parent body (bug 03). The legacy wire keeps the raw uuid list
    // (its receiver skips footprint children anyway).
    json wRemoved = json::array();

    for( const auto& [id, j] : g_baseline )
    {
        if( cur.count( id ) )
            continue;

        removed.push_back( id );

        std::string parentId = j.value( "parent", std::string() );

        if( !parentId.empty() && cur.count( parentId ) )
            liftBlob( parentId, wChanged );
        else
            wRemoved.push_back( id );
    }

    // Dirty roots (bug 04): whatever the listener saw commit emits its blob on
    // the v2 wire even when the scalar projection didn't move. wDone dedups
    // against the scalar-diff emits above; deleted ids resolve null and skip.
    for( const std::string& id : g_dirty )
        liftBlob( id, wChanged );

    g_dirty.clear();

    // Deleted dirty roots the baseline diff did not catch (the id cache still
    // resolves them, and an apply/boot rebaseline can race the commit): emit
    // their removal on both wires, and keep them out of the next baseline so
    // a redo re-emits them as adds. Duplicate removals are no-ops downstream.
    for( const std::string& id : deletedDirtyRoots )
    {
        bool alreadyDiffed = g_baseline.count( id ) && !cur.count( id );

        cur.erase( id );

        if( alreadyDiffed )
            continue;

        removed.push_back( id );
        wRemoved.push_back( id );
    }

    g_baseline = std::move( cur );

    if( !added.empty() || !changed.empty() || !removed.empty() )
        emitDelta( json{ { "added", added }, { "changed", changed }, { "removed", removed } } );

    if( !wAdded.empty() || !wChanged.empty() || !wRemoved.empty() )
        emitItemsWire( json{ { "added", wAdded }, { "changed", wChanged }, { "removed", wRemoved } } );
}

// Coalesce all the listener callbacks of one commit (and any other edits in the same loop
// turn) into a single post-settle diff.
// flushDiff runs inside a COROUTINE via the apply queue: the v2 items emit serializes
// ROOT items via CLIPBOARD_IO Format (blobForItem), and running it in the same queue
// as the applies keeps emits from interleaving with a suspended apply body (under the
// retired asyncify runtime the bare CallAfter stack additionally trapped here — same
// lesson as doApply / eeschema 0007).
void scheduleFlush()
{
    if( g_flushScheduled )
        return;

    g_flushScheduled = true;

    if( PCB_EDIT_FRAME* fr = pcbFrame() )
        pcbjam_collab::runOnCoroutine( fr, []() { flushDiff(); } );
    else
        flushDiff();
}

// Presence (collab-presence 0002): board changes often change the selection too
// (delete, paste) with no closing canvas event — piggyback a selection re-check
// on the collab listener trigger. Defined in the presence section below.
void schedulePresenceSelCheck();
// Findings Y-1/Y-3: ANY document change (local commit or remote apply) repaints
// the peers' overlay from the live board and re-checks the local selection —
// runs BEFORE the applying-remote early return in trigger().
void schedulePresenceDocChanged();

// ChangeSource: the native BOARD_LISTENER is just a trigger — the actual change set comes from
// the post-settle snapshot diff above. Skipped while applying a remote delta (no echo); doApply
// rebaselines instead. OnBoardCompositeUpdate (the single combined add/remove/change event,
// 0004) plus the bulk + singular callbacks all funnel into one trigger.
class COLLAB_LISTENER : public BOARD_LISTENER
{
public:
    void OnBoardItemAdded( BOARD&, BOARD_ITEM* i ) override                     { trigger( { i } ); }
    void OnBoardItemsAdded( BOARD&, std::vector<BOARD_ITEM*>& v ) override      { trigger( v ); }
    void OnBoardItemRemoved( BOARD&, BOARD_ITEM* i ) override                   { trigger( { i } ); }
    void OnBoardItemsRemoved( BOARD&, std::vector<BOARD_ITEM*>& v ) override    { trigger( v ); }
    void OnBoardItemChanged( BOARD&, BOARD_ITEM* i ) override                   { trigger( { i } ); }
    void OnBoardItemsChanged( BOARD&, std::vector<BOARD_ITEM*>& v ) override    { trigger( v ); }
    void OnBoardCompositeUpdate( BOARD&, std::vector<BOARD_ITEM*>& a,
                                 std::vector<BOARD_ITEM*>& r,
                                 std::vector<BOARD_ITEM*>& c ) override
    {
        trigger( a );
        trigger( r );
        trigger( c );
    }

private:
    // Capture the touched roots at callback time (uuid strings — removed items
    // may be freed before the flush runs), then coalesce into one flush.
    void trigger( const std::vector<BOARD_ITEM*>& aItems )
    {
        schedulePresenceDocChanged();

        if( s_applyingRemote )
            return;

        for( BOARD_ITEM* item : aItems )
            noteDirty( item );

        scheduleFlush();
        schedulePresenceSelCheck();
    }
};

COLLAB_LISTENER* g_listener = nullptr;

// Get the live BOARD and ensure our listener is registered on it (idempotent).
BOARD* ensureBridge()
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr )
        return nullptr;

    BOARD* board = fr->GetBoard();

    if( !g_listener )
    {
        g_listener = new COLLAB_LISTENER();
        board->AddListener( g_listener );
    }

    return board;
}

// The actual model mutation, via BOARD_COMMIT so connectivity + ratsnest recompute exactly as
// for a UI edit (0004 §apply: never bypass the commit for remote ops). Runs inside the apply
// COROUTINE (see kicadCollabApply).
void doApply( PCB_EDIT_FRAME* aFrame, const json& aDelta )
{
    BOARD* board = aFrame->GetBoard();

    s_applyingRemote = true;

    BOARD_COMMIT commit( aFrame );
    bool         staged = false;

    // With SKIP_UNDO no undo picker takes ownership of removed items; the commit
    // detaches them from the board and we free them after Push.
    std::vector<BOARD_ITEM*> removedItems;

    for( const json& rid : aDelta.value( "removed", json::array() ) )
    {
        KIID id( wxString::FromUTF8( rid.get<std::string>().c_str() ) );

        if( BOARD_ITEM* item = board->ResolveItem( id, /*allowNullptr*/ true ) )
        {
            // A footprint text child appears in `removed` when its whole footprint was deleted
            // (it vanished from the sender's snapshot). Removing the footprint cascades to its
            // children, so don't also remove the child here — that would double-remove. (A rare
            // child-only delete with the footprint kept is therefore not synced; acceptable.)
            if( item->GetParentFootprint() )
                continue;

            commit.Remove( item );
            removedItems.push_back( item );
            staged = true;
        }
    }

    for( const json& j : aDelta.value( "changed", json::array() ) )
    {
        KIID id( wxString::FromUTF8( j.value( "id", "" ).c_str() ) );

        if( BOARD_ITEM* item = board->ResolveItem( id, /*allowNullptr*/ true ) )
        {
            commit.Modify( item );
            applyChanged( item, j );
            staged = true;
        }
    }

    for( const json& j : aDelta.value( "added", json::array() ) )
    {
        KIID id( wxString::FromUTF8( j.value( "id", "" ).c_str() ) );

        if( board->ResolveItem( id, /*allowNullptr*/ true ) )
            continue;                       // already present (our own echo)

        if( BOARD_ITEM* item = makeItem( *board, j ) )
        {
            commit.Add( item );
            staged = true;
        }
        else
        {
            EM_ASM( { console.log( "[collab] pcbnew apply: no converter for added type " + UTF8ToString( $0 ) ); },
                    j.value( "type", "?" ).c_str() );
        }
    }

    // SKIP_UNDO: a peer's edit must never land on this editor's undo stack — Ctrl+Z
    // would revert (and re-broadcast) the peer's work. Undo is local-ops-only; stale
    // local undo entries are dropped/re-resolved by UUID at undo time (miss 09).
    if( staged )
        commit.Push( wxT( "Collaborative edit" ), SKIP_UNDO );

    for( BOARD_ITEM* item : removedItems )
        delete item;

    // The applied remote changes (and any connectivity cleanup they triggered) are now the
    // shared state — fold them into the baseline so the post-apply listener flush doesn't
    // re-broadcast them as a local diff (echo).
    rebaseline();
    s_applyingRemote = false;
}

// v2 items apply: removed by uuid; added/changed are an idempotent per-item upsert —
// parse the blob (wrapping bare non-footprint payloads in a live-board envelope),
// then replace any existing item sharing the parsed uuid. Runs inside the apply
// COROUTINE (see kicadCollabApplyItems), via BOARD_COMMIT like every remote op.
void doApplyItems( PCB_EDIT_FRAME* aFrame, const json& aPayload )
{
    // ysync 0012 #2: flush pending local intent first, then resolve the queued
    // payload to the doc's latest content at execution time (see the eeschema
    // twin and resolveItemsWire).
    if( g_flushScheduled )
        flushDiff();

    const json aWire = pcbjam_collab::resolveItemsWire( aPayload );

    BOARD* board = aFrame->GetBoard();

    s_applyingRemote = true;

    BOARD_COMMIT commit( aFrame );
    bool         staged = false;

    std::vector<std::string> touched;   // root uuids this apply acts on (targeted rebaseline)

    // Owned by nobody once the SKIP_UNDO commit detaches them — freed after Push.
    std::vector<BOARD_ITEM*> removedItems;

    std::set<std::string> removedIds;

    for( const json& rid : aWire.value( "removed", json::array() ) )
        removedIds.insert( rid.get<std::string>() );

    for( const std::string& rid : removedIds )
    {
        KIID id( wxString::FromUTF8( rid.c_str() ) );

        touched.push_back( rid );

        if( BOARD_ITEM* item = board->ResolveItem( id, /*allowNullptr*/ true ) )
        {
            if( FOOTPRINT* pfp = item->GetParentFootprint() )
            {
                // Covered by the parent's own removal when the whole footprint
                // goes. A BARE child removal must remove the child itself
                // (bug 03 receiving half) — the sender now lifts these to a
                // parent re-blob, but Y-rendered wires can still carry them.
                if( removedIds.count( toUtf8( pfp->m_Uuid.AsString() ) ) )
                    continue;
            }

            commit.Remove( item );

            // A bare child "removal" of a PCB_FIELD_T is a hide, not a detach —
            // the field stays owned by its parent footprint.
            if( item->Type() != PCB_FIELD_T )
                removedItems.push_back( item );

            staged = true;
        }
    }

    auto upsert = [&]( const json& w )
    {
        std::string sexpr = w.value( "sexpr", "" );
        size_t      p     = sexpr.find_first_not_of( " \t\r\n" );

        if( p == std::string::npos )
            return;

        std::string trimmed = sexpr.substr( p );

        // Peer-emitted blobs are already enveloped (or a bare footprint, which the
        // parser accepts top-level); bare Y.Doc-rendered items need the envelope.
        if( trimmed.rfind( "(kicad_pcb", 0 ) != 0 && trimmed.rfind( "(footprint", 0 ) != 0 )
            trimmed = wrapInBoardEnvelope( *board, trimmed );

        BOARD_ITEM* parsed = makeFromBlob( *board, trimmed );

        if( !parsed )
        {
            EM_ASM( { console.log( "[collab] pcbnew applyItems: blob parse failed" ); } );
            return;
        }

        if( BOARD_ITEM* existing = board->ResolveItem( parsed->m_Uuid, /*allowNullptr*/ true ) )
        {
            // Replacing by uuid; a (shouldn't-happen) child match replaces its parent.
            if( FOOTPRINT* fp = existing->GetParentFootprint() )
                existing = fp;

            commit.Remove( existing );
            removedItems.push_back( existing );
        }

        touched.push_back( toUtf8( parsed->m_Uuid.AsString() ) );

        commit.Add( parsed );
        staged = true;
    };

    for( const json& w : aWire.value( "added", json::array() ) )
        upsert( w );
    for( const json& w : aWire.value( "changed", json::array() ) )
        upsert( w );

    // SKIP_UNDO: remote applies never land on the local undo stack (see doApply).
    if( staged )
        commit.Push( wxT( "Collaborative edit (items)" ), SKIP_UNDO );

    for( BOARD_ITEM* item : removedItems )
        delete item;

    // Fold ONLY the applied uuids into the baseline (echo suppression), then flush:
    // anything else that now differs — a concurrent local edit, cleanup this apply's
    // Push produced — broadcasts as a normal local diff instead of being swallowed.
    rebaselineTouched( board, touched );
    s_applyingRemote = false;
    scheduleFlush();
}

// Test/PoC move (the BOARD_COMMIT body for kicadCollabTestMoveFirst). Run inside a COROUTINE by
// the caller — the context native tool edits and doApply run in, serialized through the apply
// queue. (Under the retired asyncify runtime the virtual `BOARD_ITEM::Move` additionally
// mis-dispatched off that context; same lesson as eeschema's devirtualized move.)
void collabTestMove( PCB_EDIT_FRAME* aFrame, BOARD_ITEM* aItem, int aDx, int aDy )
{
    BOARD_COMMIT commit( aFrame );
    commit.Modify( aItem );
    aItem->Move( VECTOR2I( aDx, aDy ) );
    commit.Push( wxT( "Collab test move" ) );
}

// ───────────────────────── collab presence (collab-presence 0002) ─────────────────────────
//
// Ephemeral presence: emit THIS tab's selection + cursor to JS (they ride Yjs awareness),
// and render REMOTE peers' cursors + selection outlines into a per-user-colored
// KIGFX::VIEW_OVERLAY. Never via the SELECTED/BRIGHTENED item flags — those have one
// global color and mutate real selection state (events, serialization, races with the
// local user); the overlay's command list takes a COLOR4D per command, never enters
// m_selection, and never serializes.
//
// Zero kicad-fork changes: the input triggers are wx-layer Bind() handlers on the GAL
// canvas (bound after WX_VIEW_CONTROLS' own, so they run FIRST and Skip() onward), the
// selection is read from PCB_SELECTION_TOOL after the event settles (CallAfter), and the
// repaint is the MakeOverlay()/VIEW::Update()/ForceRefresh() combo the cross-probe flash
// already uses. Selection changes with no closing canvas event (Edit→Select All from the
// menu) are missed until the next input or board change — acceptable for an ephemeral
// layer that fully resyncs on every emit.
//
// The state + event/scheduling machinery live in the shared pcbjam_presence::CORE
// (collab_presence_core.h); this TU supplies only the pcbnew-specific hooks:
// frame/tool lookup, KIID resolution via BOARD::ResolveItem, the {uuids,fpPaths}
// selection payload (0006), and the per-peer draw (exact-outline highlight + xsel
// ghosts matched against footprint GetPath() tails).

// The full selection emit payload (0006): the uuids plus, for every selected
// FOOTPRINT, its schematic link — the GetPath() KIID_PATH string ending in the
// symbol uuid. eeschema peers highlight the corresponding symbols from it.
json selectionPayload( PCB_EDIT_FRAME* aFrame );

// Cross-app selection (0006): the footprints a peer's xsel (eeschema symbol
// uuids) resolves to on this board — every footprint whose GetPath() ends in
// the symbol uuid (a reused sheet legitimately maps one symbol to N
// footprints). The per-call linear scan reflects the live board with no index
// to invalidate; boards are small relative to the redraw's own draw cost.
// ONE resolver shared by the ghost render and the test probe
// (kicadCollabTestGetCrossMapped), so the assertion can't drift from the pixels.
std::vector<FOOTPRINT*> resolveXsel( PCB_EDIT_FRAME* aFrame, const pcbjam_presence::PEER& aPeer )
{
    std::vector<FOOTPRINT*> fps;

    for( const KIID& symId : aPeer.xsel )
    {
        for( FOOTPRINT* fp : aFrame->GetBoard()->Footprints() )
        {
            const KIID_PATH& path = fp->GetPath();

            if( !path.empty() && path.back() == symId )
                fps.push_back( fp );
        }
    }

    return fps;
}

pcbjam_presence::CORE& presenceCore()
{
    static pcbjam_presence::CORE core = []()
    {
        pcbjam_presence::CORE c;

        c.frame = []() -> EDA_DRAW_FRAME* { return pcbFrame(); };

        c.selectionTool = []( EDA_DRAW_FRAME* fr ) -> SELECTION_TOOL*
        {
            return fr->GetToolManager()->GetTool<PCB_SELECTION_TOOL>();
        };

        c.selectionEmitPayload = []( EDA_DRAW_FRAME* fr ) -> json
        {
            return selectionPayload( static_cast<PCB_EDIT_FRAME*>( fr ) );
        };

        c.resolveItem = []( EDA_DRAW_FRAME* fr, const KIID& id ) -> EDA_ITEM*
        {
            return static_cast<PCB_EDIT_FRAME*>( fr )->GetBoard()
                    ->ResolveItem( id, /*aAllowNullptrReturn*/ true );
        };

        c.drawPeerShapes = []( pcbjam_presence::CORE& aCore, EDA_DRAW_FRAME* aFrame,
                               const pcbjam_presence::PEER& peer, const KIGFX::COLOR4D& color,
                               double px )
        {
            PCB_EDIT_FRAME* fr    = static_cast<PCB_EDIT_FRAME*>( aFrame );
            BOARD*          board = fr->GetBoard();

            // Draw ONE item's selection box under the given style. Exact-geometry
            // outline (style shape 5): footprints hug their bounding hull,
            // everything else its transformed shape. Runs on the apply
            // coroutine, falls back to the bbox on anything that can't
            // produce a polygon.
            auto drawItem = [&]( BOARD_ITEM* item, const std::string& name,
                                 const KIGFX::COLOR4D& itemColor,
                                 const pcbjam_presence::STYLE& style )
            {
                SHAPE_POLY_SET outline;

                if( style.selShape == 5 )
                {
                    try
                    {
                        int pad = KiROUND( style.selPaddingPx * px );
                        int err = KiROUND( px ) + 1;

                        if( item->Type() == PCB_FOOTPRINT_T )
                        {
                            outline = static_cast<FOOTPRINT*>( item )->GetBoundingHull();

                            if( pad > 0 )
                                outline.Inflate( pad, CORNER_STRATEGY::ROUND_ALL_CORNERS, err );
                        }
                        else
                        {
                            item->TransformShapeToPolygon( outline, (PCB_LAYER_ID) itemLayer( item ),
                                                           pad, err, ERROR_OUTSIDE );
                        }
                    }
                    catch( ... )
                    {
                        outline.RemoveAllContours();
                    }
                }

                pcbjam_presence::drawSelectionBox( aCore.overlay.get(), aCore.chipOverlay.get(),
                                                   aCore.textOverlay.get(),
                                                   item->ViewBBox(), name, itemColor, px,
                                                   style, &outline );
            };

            // Reviewer (commenter) peers draw with the dashed/softer reviewer
            // style and a suffixed label (comments-ux 0003 §5.2).
            const pcbjam_presence::STYLE& base = peer.reviewer
                                                 ? pcbjam_presence::reviewerStyle( aCore.style )
                                                 : aCore.style;
            const std::string label = pcbjam_presence::peerLabel( aCore.style, peer.name,
                                                                  peer.reviewer );

            for( const KIID& id : peer.selection )
            {
                BOARD_ITEM* item = board->ResolveItem( id, /*aAllowNullptrReturn*/ true );

                if( !item )
                    continue;   // not on this board (yet) — skip silently

                drawItem( item, label, color, base );
            }

            // Cross-app ghosts (0006) — see resolveXsel for the path-tail matching.
            if( !peer.xsel.empty() )
            {
                pcbjam_presence::STYLE ghost = pcbjam_presence::ghostStyle( base );

                for( FOOTPRINT* fp : resolveXsel( fr, peer ) )
                    drawItem( fp, label, color, ghost );
            }
        };

        return c;
    }();

    return core;
}

json selectionPayload( PCB_EDIT_FRAME* aFrame )
{
    json payload;
    payload["uuids"]   = presenceCore().selectionUuids( aFrame );
    payload["fpPaths"] = json::array();

    PCB_SELECTION_TOOL* selTool = aFrame->GetToolManager()->GetTool<PCB_SELECTION_TOOL>();

    if( !selTool )
        return payload;

    for( EDA_ITEM* item : selTool->GetSelection() )
    {
        if( item->Type() != PCB_FOOTPRINT_T )
            continue;

        const KIID_PATH& path = static_cast<FOOTPRINT*>( item )->GetPath();

        if( !path.empty() )
            payload["fpPaths"].push_back( toUtf8( path.AsString() ) );
    }

    return payload;
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


// JS → C++. Apply a remote per-item delta by uuid, through BOARD_COMMIT so connectivity/ratsnest
// recompute the same way a UI edit would (0004 §apply).
//
// BOARD_COMMIT must run on the editor's main loop, not on this embind ccall or a setTimeout
// callback: wxEvtHandler::CallAfter queues onto the app's pending-event list, drained every
// frame by the wasm main loop (src/wasm/evtloop.cpp) — the exact context real UI edits run in.
// Additionally run the mutation inside a COROUTINE via the collab_common.h apply queue: a
// commit body that suspends (connectivity/GAL work) returns early from COROUTINE::Call, so
// applies must serialize with each other and with local edits or they interleave on shared
// commit/listener state (eeschema 0007, drift-trio #10).
void pcbCollabApply( std::string aJson )
{
    // Open-in-flight guard (open_gate.h): never touch the model while a
    // kicadOpenFile chain is suspended mid-load — commits/virtuals would walk
    // a half-built board mid-mutation.
    // Callers gate on kicadOpenFileBusy; fuzzed by tests/kicad/collab-load-fuzz.spec.ts.
    if( pcbjam_open::busy() )
        return;

    json delta = json::parse( aJson, nullptr, /*allow_exceptions*/ false );

    if( delta.is_discarded() )
        return;

    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr )
        return;

    pcbjam_collab::runOnCoroutine( fr, [fr, delta]() { doApply( fr, delta ); } );
}


// JS → C++, v2 items wire. Same CallAfter + COROUTINE context as kicadCollabApply
// (the blob parse + commit must run where native edits run — see above).
void pcbCollabApplyItems( std::string aJson )
{
    if( pcbjam_open::busy() ) // open in flight (open_gate.h) — see pcbCollabApply
        return;

    json wire = json::parse( aJson, nullptr, /*allow_exceptions*/ false );

    if( wire.is_discarded() )
        return;

    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr )
        return;

    pcbjam_collab::runOnCoroutine( fr, [fr, wire]() { doApplyItems( fr, wire ); } );
}


// JS pull of the full current model as an all-"added" delta (seed/baseline). Also registers the
// change listener on first call.
std::string pcbCollabSnapshot()
{
    if( pcbjam_open::busy() ) // open in flight (open_gate.h) — see pcbCollabApply
        return json{ { "added", json::array() }, { "changed", json::array() },
                     { "removed", json::array() } }.dump();

    BOARD* board = ensureBridge();

    json added = json::array();

    if( board )
    {
        forEachTopItem( *board, [&]( BOARD_ITEM* item ) { added.push_back( itemToJson( item ) ); } );
    }

    // Seed the diff baseline to exactly the model we're handing out, so the first local edit
    // diffs against this snapshot (and we don't re-broadcast the whole model).
    rebaseline();

    return json{ { "added", added }, { "changed", json::array() },
                 { "removed", json::array() } }.dump();
}


// JS pull of the full current model as an all-"added" v2 items wire: one blob per ROOT
// item (a footprint's blob embeds its children — the TS side flattens). Registers the
// listener + rebaselines exactly like kicadCollabSnapshot.
std::string pcbCollabSnapshotItems()
{
    if( pcbjam_open::busy() ) // open in flight (open_gate.h) — see pcbCollabApply
        return json{ { "added", json::array() }, { "changed", json::array() },
                     { "removed", json::array() } }.dump();

    BOARD* board = ensureBridge();

    json added = json::array();

    if( board )
    {
        auto push = [&]( BOARD_ITEM* item )
        {
            std::string sexpr = blobForItem( board, item );

            if( !sexpr.empty() )    // P-5: never seed a hollow envelope
                added.push_back( json{ { "sexpr", sexpr }, { "parent", nullptr } } );
        };

        for( FOOTPRINT* fp : board->Footprints() )  push( fp );
        for( PCB_TRACK* t : board->Tracks() )       push( t );
        for( ZONE* z : board->Zones() )             push( z );
        for( BOARD_ITEM* d : board->Drawings() )    push( d );
    }

    rebaseline();

    return json{ { "added", added }, { "changed", json::array() },
                 { "removed", json::array() } }.dump();
}


// Programmatically save the in-memory board to a .kicad_pcb file, without driving
// the Save As dialog — pcbnew's analogue of pl_editor's kicadSaveDrawingSheet.
// Serializes exactly what the editor has loaded via the same writer eeschema/pcbnew
// use, so a test can read the file back from MEMFS and assert the file ⇄ Y.Doc
// round trip (README §A; feature 0004). Uses only public PCB_IO_KICAD_SEXPR API.
// C++ → JS save notification (standalone-hardening save routing). Called from the
// kicad fork's save chokepoint (PCB_EDIT_FRAME::SavePcbFile) after a successful
// write to MEMFS, so the web app can route the saved bytes onward (API upload,
// local-disk write-back, download). No-op without a JS listener.
// KICAD_MERGED_EMBIND: identical definition in eeschema_embind.cpp; the merged image
// gets the one in kicad_editor_embind.cpp (both fork save chokepoints call it).
#ifndef KICAD_MERGED_EMBIND
extern "C" void kicadCollabOnSave( const char* aPath )
{
    EM_ASM( {
        if( window.kicadCollab && window.kicadCollab.onSave )
            window.kicadCollab.onSave( UTF8ToString( $0 ) );
    }, aPath );
}
#endif // !KICAD_MERGED_EMBIND

// Merged-image dispatch probe (kicad_editor_embind.cpp): is the active top window the
// PCB editor? Each shared JS entry routes to the pcb*/sch* implementation whose frame
// is live — exactly the null-check its body starts with anyway.
bool pcbEditorActive()
{
    return pcbFrame() != nullptr;
}


void kicadSaveBoard( std::string path )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr )
        return;

    BOARD* board = fr->GetBoard();

    if( !board )
        return;

    try
    {
        PCB_IO_KICAD_SEXPR io;
        io.SaveBoard( wxString::FromUTF8( path.c_str() ), board );
    }
    catch( ... )
    {
        // Don't abort the wasm runtime on a save failure; the JS caller detects it
        // by the file being absent / empty.
    }
}


// Test/PoC helper: move the first top-level board item by (dx,dy) IU via a real BOARD_COMMIT,
// firing the listener — a deterministic local edit for the two-tab demo / e2e. Returns the
// moved item's uuid.
std::string pcbCollabTestMoveFirst( int aDx, int aDy )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr )
        return "";

    std::string movedId;

    forEachTopItem( *fr->GetBoard(), [&]( BOARD_ITEM* item )
                    {
                        if( !movedId.empty() )
                            return;

                        movedId = toUtf8( item->m_Uuid.AsString() );
                        // Main loop + apply coroutine (runOnCoroutine) — same
                        // wrapping as doApply.
                        pcbjam_collab::runOnCoroutine( fr, [fr, item, aDx, aDy]() {
                            collabTestMove( fr, item, aDx, aDy );
                        } );
                    } );

    return movedId;
}


// Test helper: read an item's position by uuid as "x,y" (internal units).
std::string pcbCollabGetPos( std::string aId )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr )
        return "";

    KIID id( wxString::FromUTF8( aId.c_str() ) );

    if( BOARD_ITEM* item = fr->GetBoard()->ResolveItem( id, /*allowNullptr*/ true ) )
    {
        VECTOR2I p = item->GetPosition();
        return std::to_string( p.x ) + "," + std::to_string( p.y );
    }

    return "";
}


// ── presence entry points (collab-presence 0002) ────────────────────────────────────────────

// Install the presence input hooks on the GAL canvas (idempotent). Called by the JS
// presence binding at collab attach; also implied by the first kicadCollabSetRemote.
// Handlers Skip() so WX_VIEW_CONTROLS' own processing is untouched; selection checks
// run POST-event via CallAfter (the selection tool acts on the same event after us).
void pcbCollabPresenceStart()
{
    // Presence needs the board listener too (findings Y-1/Y-3) — see eeschema.
    ensureBridge();
    presenceCore().start();
}

// JS → C++: full remote-peers snapshot — `{peers:[{id,name,color,cursor:{x,y}|null,
// selection:[uuid]}]}`, trivially derived from awareness.getStates() and idempotent
// (the overlay is cleared + fully redrawn). An empty peers list clears the overlay.
void pcbCollabSetRemote( std::string aJson )
{
    presenceCore().setRemote( aJson );
}

// JS → C++: cursor-only update for the peers of the last snapshot (findings Y-4).
void pcbCollabSetRemoteCursors( std::string aJson )
{
    presenceCore().setRemoteCursors( aJson );
}

// JS → C++ (collab-presence 0005): comment pin dots — `{pins:[{id,x,y,color,
// resolved}]}`, world IU coords resolved by the TS side from the ydoc anchors.
// Snapshot semantics like SetRemote: cleared + fully redrawn each push.
void pcbCollabSetPins( std::string aJson )
{
    presenceCore().setPins( aJson );
}

// JS → C++ (presence tuner): live-patch the overlay STYLE (partial JSON —
// see collab_presence_style.h) and repaint. Dev-time only in practice, but
// harmless in production (nothing calls it without VITE_PRESENCE_TUNER).
void pcbCollabSetStyle( std::string aJson )
{
    presenceCore().setStyle( aJson );
}

// JS → C++ (comments-ux 0002 F4): live color-theme switch (see pcbjam_theme.h).
void pcbSetColorTheme( std::string aTheme )
{
    pcbjam_theme::setColorTheme( pcbFrame(), aTheme );
}

// Pre-main chrome appearance seed (called at onRuntimeInitialized).
void pcbSetDarkChrome( bool aDark )
{
    pcbjam_theme::setDarkChromeFlag( aDark );
}

// ── Layer bridge (viewer-panels) ─────────────────────────────────────────────
// The React layer panel's read/toggle surface for canvas-only sessions (the wx
// Appearance pane is chrome-hidden there). Per-layer visibility has no
// TOOL_ACTION, so the setter bodies mirror KiCad's IPC handlers
// (pcbnew/api/api_handler_pcb.cpp handleSetVisibleLayers/handleSetActiveLayer —
// compiled into this image but unreachable, nng transport stubbed). Both
// setters are view-only state: nothing touches the document or the save path
// (visibility persists to the .kicad_prl on native, nowhere here).

// Full layer state as one JSON payload:
//   { "active": int, "layers": [{ id, name, canonical, copper, visible, color }] }
// in UI order (the wx Appearance panel's), colors from the live COLOR_SETTINGS
// so the panel's swatches match the canvas theme.
static json layersStateJson( PCB_EDIT_FRAME* aFrame )
{
    BOARD*          board  = aFrame->GetBoard();
    COLOR_SETTINGS* colors = aFrame->GetColorSettings();

    json layers = json::array();

    for( PCB_LAYER_ID layer : board->GetEnabledLayers().UIOrder() )
    {
        layers.push_back( json{
                { "id", static_cast<int>( layer ) },
                { "name", pcbjam_collab::toUtf8( board->GetLayerName( layer ) ) },
                { "canonical", pcbjam_collab::toUtf8( BOARD::GetStandardLayerName( layer ) ) },
                { "copper", IsCopperLayer( layer ) },
                { "visible", board->IsLayerVisible( layer ) },
                { "color", colors ? pcbjam_collab::toUtf8( colors->GetColor( layer ).ToCSSString() )
                                  : std::string() } } );
    }

    return json{ { "active", static_cast<int>( aFrame->GetActiveLayer() ) },
                 { "layers", layers } };
}

// Post-apply push: window.kicadCollab.onLayersState — the panel updates
// event-driven instead of polling (both setters apply on the coroutine, so a
// synchronous re-read right after the embind call would still see old state).
static void emitLayersState( PCB_EDIT_FRAME* aFrame )
{
    std::string s = layersStateJson( aFrame ).dump();
    EM_ASM( {
        if( window.kicadCollab && window.kicadCollab.onLayersState )
            window.kicadCollab.onLayersState( UTF8ToString( $0 ) );
    }, s.c_str() );
}

std::string pcbLayersGetState()
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr )
        return "";

    return layersStateJson( fr ).dump();
}

// Show/hide ONE layer. Validated synchronously (frame up, layer enabled);
// the apply itself runs on the coroutine like every other view mutation
// from JS, then pushes the fresh state to onLayersState.
bool pcbLayersSetVisible( int aLayer, bool aVisible )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr )
        return false;

    PCB_LAYER_ID layer = static_cast<PCB_LAYER_ID>( aLayer );

    if( !fr->GetBoard()->GetEnabledLayers().Contains( layer ) )
        return false;

    pcbjam_collab::runOnCoroutine( fr, [fr, layer, aVisible]() {
        BOARD* board = fr->GetBoard();
        LSET   visible = board->GetVisibleLayers();

        visible.set( layer, aVisible );
        board->SetVisibleLayers( visible );

        // Keep the (chrome-hidden but alive) wx Appearance pane in sync —
        // same follow-ups as the IPC handler.
        if( APPEARANCE_CONTROLS* panel = fr->GetAppearancePanel() )
            panel->OnBoardChanged();

        fr->GetCanvas()->SyncLayersVisibility( board );
        fr->Refresh();
        emitLayersState( fr );
    } );

    return true;
}

bool pcbLayersSetActive( int aLayer )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr )
        return false;

    PCB_LAYER_ID layer = static_cast<PCB_LAYER_ID>( aLayer );

    if( !fr->GetBoard()->GetEnabledLayers().Contains( layer ) )
        return false;

    pcbjam_collab::runOnCoroutine( fr, [fr, layer]() {
        fr->SetActiveLayer( layer, /* aForceRedraw */ true );
        emitLayersState( fr );
    } );

    return true;
}

// Tuner helper: a VARIED demo-selection set — labeled uuid groups (smallest +
// largest footprint, the two busiest nets' track segments) so the style
// preview shows the real range of shapes instead of two overlapping items.
std::string pcbCollabTestDemoSet()
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    json groups = json::array();

    if( fr )
    {
        BOARD* board = fr->GetBoard();

        // Footprints by bbox area → smallest and largest.
        FOOTPRINT* smallest = nullptr;
        FOOTPRINT* largest  = nullptr;
        double     minA = 0, maxA = 0;

        for( FOOTPRINT* f : board->Footprints() )
        {
            BOX2I  bb = f->GetBoundingBox();
            double a  = (double) bb.GetWidth() * bb.GetHeight();

            if( !smallest || a < minA ) { smallest = f; minA = a; }
            if( !largest || a > maxA )  { largest = f;  maxA = a; }
        }

        if( smallest )
            groups.push_back( { { "label", "fp small (" + toUtf8( smallest->GetReference() ) + ")" },
                                { "ids", { toUtf8( smallest->m_Uuid.AsString() ) } } } );

        if( largest && largest != smallest )
            groups.push_back( { { "label", "fp large (" + toUtf8( largest->GetReference() ) + ")" },
                                { "ids", { toUtf8( largest->m_Uuid.AsString() ) } } } );

        // Tracks grouped by net → the two busiest nets (capped segment lists).
        std::map<std::string, std::vector<std::string>> nets;

        for( PCB_TRACK* t : board->Tracks() )
        {
            std::string net = toUtf8( t->GetNetname() );

            if( !net.empty() )
                nets[net].push_back( toUtf8( t->m_Uuid.AsString() ) );
        }

        std::vector<std::pair<std::string, std::vector<std::string>>> byCount( nets.begin(),
                                                                               nets.end() );
        std::sort( byCount.begin(), byCount.end(),
                   []( const auto& a, const auto& b ) { return a.second.size() > b.second.size(); } );

        for( size_t i = 0; i < byCount.size() && i < 2; ++i )
        {
            auto ids = byCount[i].second;

            if( ids.size() > 12 )
                ids.resize( 12 );

            groups.push_back( { { "label", "net " + byCount[i].first }, { "ids", ids } } );
        }
    }

    return json{ { "groups", groups } }.dump();
}

// Test/tuner helper: the first N top-level item uuids — real, resolvable KIIDs
// for synthetic remote-selection previews (a solo tab has no peer to borrow from).
std::string pcbCollabTestListItems( int aCount )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    json out = json::array();

    if( fr )
    {
        forEachTopItem( *fr->GetBoard(), [&]( BOARD_ITEM* item )
                        {
                            if( (int) out.size() < aCount && !item->GetParentFootprint() )
                                out.push_back( toUtf8( item->m_Uuid.AsString() ) );
                        } );
    }

    return out.dump();
}

// JS → C++ (0005): pan the view to a world position (comment panel "jump to
// pin"). CallAfter + COROUTINE like every other view mutation from JS.
void pcbCollabSetViewport( double aCx, double aCy )
{
    presenceCore().panTo( aCx, aCy );
}

// JS → C++ (0008 follow-user): fit a leader's world rect into this canvas.
void pcbCollabFitViewport( double aCx, double aCy, double aHalfW, double aHalfH )
{
    presenceCore().fitViewport( aCx, aCy, aHalfW, aHalfH );
}

// JS → C++: open the board's 3D viewer (read-only-viewer / hide-UI sessions have
// no wx View menu). Dispatched like a native edit — CallAfter + COROUTINE via
// runOnCoroutine — so the tool runs on its own stack, where the 3D frame's
// RunMainStack bounce (the 8/12 sleep-park fix) is valid; never from inside a
// parked bridge crossing. Returns false when no board frame is up.
bool pcbShow3DViewer()
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr )
        return false;

    pcbjam_collab::runOnCoroutine(
            fr, [fr]() { fr->GetToolManager()->RunAction( ACTIONS::show3DViewer ); } );
    return true;
}

// JS pull of the current viewport transform (world↔screen mapping for the DOM layer):
// `{cx,cy,scale,w,h}` — world center, pixels-per-IU scale, canvas size in px.
std::string pcbCollabGetViewport()
{
    return presenceCore().viewportJson();
}

// JS pull of the CURRENT selection's uuids (presence seed at attach + the e2e's
// no-state-leak probe: a remote render must leave this empty).
std::string pcbCollabGetSelection()
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr )
        return "[]";

    return presenceCore().selectionUuids( fr ).dump();
}

// JS pull of the current selection WITH the footprints' schematic paths (0006):
// `{uuids:[…], fpPaths:[…]}` — the same payload checkSelection emits. Seeds the
// cross-app presence at attach; GetSelection above keeps its bare-array shape
// for the pre-0006 callers and the e2e no-leak probe.
std::string pcbCollabGetSelectionFull()
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr )
        return "{\"uuids\":[],\"fpPaths\":[]}";

    return selectionPayload( fr ).dump();
}

// Test probe (0006): the board-item uuids the current peers' cross-app
// selections (xsel symbol uuids) resolve to on THIS board — same resolveXsel
// the render uses, so the assertion target can't drift from the pixels.
std::string pcbCollabTestGetCrossMapped()
{
    json arr = json::array();

    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr )
        return arr.dump();

    for( const pcbjam_presence::PEER& peer : presenceCore().peers )
    {
        for( FOOTPRINT* fp : resolveXsel( fr, peer ) )
            arr.push_back( toUtf8( fp->m_Uuid.AsString() ) );
    }

    return arr.dump();
}

// Test helper: REALLY select the first top-level item through the selection tool (the
// same call the cross-probe flash uses), then run the presence check — a programmatic
// select has no closing canvas event. Returns the selected uuid.
std::string pcbCollabTestSelectFirst()
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr )
        return "";

    BOARD_ITEM* target = nullptr;

    forEachTopItem( *fr->GetBoard(), [&]( BOARD_ITEM* item )
                    {
                        if( !target )
                            target = item;
                    } );

    if( !target )
        return "";

    presenceCore().selectItem( target );

    return toUtf8( target->m_Uuid.AsString() );
}

// Test helper (0006): REALLY select the first FOOTPRINT through the selection
// tool — the deterministic cross-app subject (TestSelectFirst may pick a track,
// which legitimately maps to nothing in eeschema). Returns the uuid, "" if the
// board has no footprints.
std::string pcbCollabTestSelectComponent()
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr )
        return "";

    FOOTPRINT* target = nullptr;

    for( FOOTPRINT* fp : fr->GetBoard()->Footprints() )
    {
        target = fp;
        break;
    }

    if( !target )
        return "";

    presenceCore().selectItem( target );

    return toUtf8( target->m_Uuid.AsString() );
}

// Test helper (0007): select a SPECIFIC item by uuid through the selection
// tool. The tiebreak specs need both tabs holding the SAME item, and
// cross-tab "first footprint" iteration order is not guaranteed — a
// ysync-materialized board need not match the seeding tab's parse order.
bool pcbCollabTestSelectByUuid( std::string aUuid )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr )
        return false;

    BOARD_ITEM* item = fr->GetBoard()->ResolveItem(
            KIID( wxString::FromUTF8( aUuid.c_str() ) ), /*allowNull*/ true );

    if( !item )
        return false;

    presenceCore().selectItem( item );

    return true;
}

// JS → C++ (0007): the local client LOST the selection tiebreak — release the
// contested items (only those) from the live selection. If an interactive
// tool (move/drag) holds them, cancel it first (ESC semantics — the preview
// reverts); a bare cancel is NOT sent when idle, since ESC on the base
// selection tool would clear the whole selection. Ends with a forced
// selection re-emit (programmatic changes close no canvas event).
void pcbCollabReleaseSelection( std::string aUuidsJson, std::string aHolder )
{
    presenceCore().releaseSelection( aUuidsJson, aHolder );
}

// Test probe (0007): the current remote soft-lock set as `[{uuid, name}]`.
std::string pcbCollabTestGetLocked()
{
    return presenceCore().locksJson();
}

// Test helper: clear the selection through the tool + run the presence check.
bool pcbCollabTestClearSelection()
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr )
        return false;

    fr->CallAfter( [fr]() {
        // ClearSelection is not on the shared SELECTION_TOOL base — the one
        // presence hook that stays editor-typed.
        if( PCB_SELECTION_TOOL* st = fr->GetToolManager()->GetTool<PCB_SELECTION_TOOL>() )
        {
            st->ClearSelection();
            presenceCore().scheduleSelCheck();
        }
    } );

    return true;
}

// Test helper: the s-expr clipboard blob for an item by uuid (what the emit side attaches to an
// `added` payload). Lets the e2e round-trip the blob add path without a real draw.
std::string kicadCollabTestItemBlob( std::string aId )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr )
        return "";

    BOARD* board = fr->GetBoard();

    if( BOARD_ITEM* item = board->ResolveItem( KIID( wxString::FromUTF8( aId.c_str() ) ),
                                               /*allowNullptr*/ true ) )
    {
        return blobForItem( board, item );
    }

    return "";
}

// ── ysync-review repro hooks ─────────────────────────────────────────────────
// Local-edit test hooks for the ysync-review repro e2e (docs/features/
// ysync-review on the ysync-review branch): each drives a REAL BOARD_COMMIT on
// the app main loop inside the apply COROUTINE (the collabTestMove wrapping),
// so the COLLAB_LISTENER → flushDiff emit path runs exactly as for a UI edit. Each
// returns false when the uuid doesn't resolve, letting the spec distinguish
// "hook missed the item" from "differ missed the edit" (bug 04).

// Resolve a live board item by uuid, or null (shared by the hooks below).
static BOARD_ITEM* testResolve( PCB_EDIT_FRAME* aFrame, const std::string& aId )
{
    if( !aFrame )
        return nullptr;

    return aFrame->GetBoard()->ResolveItem( KIID( wxString::FromUTF8( aId.c_str() ) ),
                                            /*allowNullptr*/ true );
}

// Delete an item by uuid. With a footprint CHILD uuid this is the bug-03
// sending half (the UI's fp-text delete): the emit must lift to a parent
// re-blob; today it goes out as a bare child removal.
// Run Edit>Undo / read the undo depth — miss 09; frame-generic, collab_common.h.
bool pcbCollabTestUndo()
{
    return pcbjam_collab::testUndo( pcbFrame() );
}

int pcbCollabTestUndoDepth()
{
    return pcbjam_collab::testUndoDepth( pcbFrame() );
}

bool pcbCollabTestRemoveItem( std::string aId )
{
    PCB_EDIT_FRAME* fr = pcbFrame();
    BOARD_ITEM*     item = testResolve( fr, aId );

    if( !item )
        return false;

    pcbjam_collab::runOnCoroutine( fr, [fr, item]() {
        BOARD_COMMIT commit( fr );
        commit.Remove( item );
        commit.Push( wxT( "Collab test remove" ) );
    } );

    return true;
}

// Rotate an item about its OWN anchor — bug 04: the scalar json carries no
// orientation for footprints, so an anchor-centred rotation is invisible to
// the differ unless a child's absolute position happens to move.
bool pcbCollabTestRotateItem( std::string aId, double aDeg )
{
    PCB_EDIT_FRAME* fr = pcbFrame();
    BOARD_ITEM*     item = testResolve( fr, aId );

    if( !item )
        return false;

    pcbjam_collab::runOnCoroutine( fr, [fr, item, aDeg]() {
        BOARD_COMMIT commit( fr );
        commit.Modify( item );
        item->Rotate( item->GetPosition(), EDA_ANGLE( aDeg, DEGREES_T ) );
        commit.Push( wxT( "Collab test rotate" ) );
    } );

    return true;
}

// Resize a pad (the pad-properties dialog edit) — bug 04: pads are not visited
// by forEachTopItem at all, so the edit never reaches either wire.
bool pcbCollabTestSetPadSize( std::string aId, int aW, int aH )
{
    PCB_EDIT_FRAME* fr = pcbFrame();
    BOARD_ITEM*     item = testResolve( fr, aId );

    if( !item || item->Type() != PCB_PAD_T )
        return false;

    PAD* pad = static_cast<PAD*>( item );

    pcbjam_collab::runOnCoroutine( fr, [fr, pad, aW, aH]() {
        BOARD_COMMIT commit( fr );
        commit.Modify( pad );
        pad->SetSize( PADSTACK::ALL_LAYERS, VECTOR2I( aW, aH ) );
        commit.Push( wxT( "Collab test pad size" ) );
    } );

    return true;
}

// Drag a track/shape END point only — bug 04: Drawings' json is position-only
// and GetPosition() is the START, so an end-point reshape of a graphic shape
// is invisible (tracks DO carry endpoints — the visible control case).
bool pcbCollabTestMoveEndpoint( std::string aId, int aDx, int aDy )
{
    PCB_EDIT_FRAME* fr = pcbFrame();
    BOARD_ITEM*     item = testResolve( fr, aId );

    if( !item || ( !isTrackType( item->Type() ) && item->Type() != PCB_SHAPE_T ) )
        return false;

    pcbjam_collab::runOnCoroutine( fr, [fr, item, aDx, aDy]() {
        BOARD_COMMIT commit( fr );
        commit.Modify( item );

        if( isTrackType( item->Type() ) )
        {
            auto* t = static_cast<PCB_TRACK*>( item );
            t->SetEnd( t->GetEnd() + VECTOR2I( aDx, aDy ) );
        }
        else
        {
            auto* s = static_cast<PCB_SHAPE*>( item );
            s->SetEnd( s->GetEnd() + VECTOR2I( aDx, aDy ) );
        }

        commit.Push( wxT( "Collab test endpoint" ) );
    } );

    return true;
}

// ── drift-trio phase B action hooks (standalone-hardening 0008 §5) ───────────
// Creation/mutation primitives for the trio harness's action catalog. Each
// drives a REAL BOARD_COMMIT on the apply coroutine, so the BOARD_LISTENER →
// flushDiff emit path runs exactly as for a UI edit. Names are tool-unique
// (registered outside the KICAD_MERGED_EMBIND guard — same convention as
// kicadCollabTestSetPadSize), so the merged image needs no dispatcher.

// Commit a freshly-built board item; returns its uuid.
static std::string pcbCollabTestCommitAdd( BOARD_ITEM* aItem, const wxChar* aMsg )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr )
    {
        delete aItem;
        return "";
    }

    std::string id = toUtf8( aItem->m_Uuid.AsString() );
    wxString    msg( aMsg );

    pcbjam_collab::runOnCoroutine( fr, [fr, aItem, msg]() {
        BOARD_COMMIT commit( fr );
        commit.Add( aItem );
        commit.Push( msg );
    } );

    return id;
}

std::string pcbCollabTestAddTrack( int aX1, int aY1, int aX2, int aY2, int aWidth,
                                   std::string aLayer )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr )
        return "";

    BOARD*     board = fr->GetBoard();
    PCB_TRACK* track = new PCB_TRACK( board );
    track->SetStart( VECTOR2I( aX1, aY1 ) );
    track->SetEnd( VECTOR2I( aX2, aY2 ) );
    track->SetWidth( aWidth );
    track->SetLayer( board->GetLayerID( wxString::FromUTF8( aLayer.c_str() ) ) );
    return pcbCollabTestCommitAdd( track, wxT( "Collab test add track" ) );
}

std::string pcbCollabTestAddVia( int aX, int aY, int aSize, int aDrill )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr )
        return "";

    PCB_VIA* via = new PCB_VIA( fr->GetBoard() );
    via->SetPosition( VECTOR2I( aX, aY ) );
    via->SetWidth( aSize );
    via->SetDrill( aDrill );
    return pcbCollabTestCommitAdd( via, wxT( "Collab test add via" ) );
}

std::string pcbCollabTestAddBoardText( std::string aText, int aX, int aY, std::string aLayer )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr )
        return "";

    BOARD*    board = fr->GetBoard();
    PCB_TEXT* text = new PCB_TEXT( board );
    text->SetText( wxString::FromUTF8( aText.c_str() ) );
    text->SetTextPos( VECTOR2I( aX, aY ) );
    text->SetLayer( board->GetLayerID( wxString::FromUTF8( aLayer.c_str() ) ) );
    return pcbCollabTestCommitAdd( text, wxT( "Collab test add text" ) );
}

// Rectangular unfilled zone outline on one layer.
std::string pcbCollabTestAddZone( int aX1, int aY1, int aX2, int aY2, std::string aLayer )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr )
        return "";

    BOARD* board = fr->GetBoard();
    ZONE*  zone = new ZONE( board );
    zone->SetLayer( board->GetLayerID( wxString::FromUTF8( aLayer.c_str() ) ) );
    zone->Outline()->NewOutline();
    zone->Outline()->Append( aX1, aY1 );
    zone->Outline()->Append( aX2, aY1 );
    zone->Outline()->Append( aX2, aY2 );
    zone->Outline()->Append( aX1, aY2 );
    return pcbCollabTestCommitAdd( zone, wxT( "Collab test add zone" ) );
}

bool pcbCollabTestFlipBoardItem( std::string aId )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !testResolve( fr, aId ) )
        return false;

    pcbjam_collab::runOnCoroutine( fr, [fr, aId]() {   // re-resolve on the coroutine (S4)
        BOARD_ITEM* item = testResolve( fr, aId );

        if( !item )
            return;

        BOARD_COMMIT commit( fr );
        commit.Modify( item );
        item->Flip( item->GetPosition(), FLIP_DIRECTION::LEFT_RIGHT );
        commit.Push( wxT( "Collab test flip" ) );
    } );

    return true;
}

// aField: "Reference" | "Value" (the two mandatory text fields).
bool pcbCollabTestSetFootprintField( std::string aId, std::string aField, std::string aText )
{
    PCB_EDIT_FRAME* fr = pcbFrame();
    BOARD_ITEM*     item = testResolve( fr, aId );

    if( !item || item->Type() != PCB_FOOTPRINT_T )
        return false;

    wxString text = wxString::FromUTF8( aText.c_str() );
    bool     isRef = ( aField == "Reference" );

    if( !isRef && aField != "Value" )
        return false;

    pcbjam_collab::runOnCoroutine( fr, [fr, aId, text, isRef]() {   // re-resolve on the coroutine (S4)
        BOARD_ITEM* live = testResolve( fr, aId );

        if( !live || live->Type() != PCB_FOOTPRINT_T )
            return;

        FOOTPRINT*   fp = static_cast<FOOTPRINT*>( live );
        BOARD_COMMIT commit( fr );
        commit.Modify( fp );

        if( isRef )
            fp->SetReference( text );
        else
            fp->SetValue( text );

        commit.Push( wxT( "Collab test footprint field" ) );
    } );

    return true;
}

// Circuit Center (island cc12, footprint.models): replace one footprint's 3D
// model list as ONE undo step. The collab apply (kicadCollabApplyItems) replaces
// a footprint by its uuid but pushes SKIP_UNDO by design (a peer's edit never
// lands on the local undo stack), so the reader's Ctrl+Z could not take a model
// change back; this is a REAL BOARD_COMMIT, as a model edit in KiCad's own
// footprint properties dialog is: Modify, the new list, Push (undo, OnModify).
// aModelsJson is a JSON array of { path, offset: [x,y,z] mm, rotate: [x,y,z]
// degrees, scale: [x,y,z] }, each model shown at full opacity. Answers false
// (nothing queued) when the uuid is not a footprint of the board or the JSON is
// not that shape; true once the commit is queued on the apply coroutine, which
// re-resolves the footprint there (the host waits for kicadCollabBusy to clear).
bool pcbFootprintSetModels( std::string aId, std::string aModelsJson )
{
    PCB_EDIT_FRAME* fr = pcbFrame();
    BOARD_ITEM*     item = testResolve( fr, aId );

    if( !item || item->Type() != PCB_FOOTPRINT_T )
        return false;

    json parsed = json::parse( aModelsJson, nullptr, /*allow_exceptions*/ false );

    if( parsed.is_discarded() || !parsed.is_array() )
        return false;

    auto vec3 = []( const json& aObj, const char* aKey, VECTOR3D& aOut ) -> bool
    {
        auto it = aObj.find( aKey );

        if( it == aObj.end() )
            return false;

        const json& v = *it;

        if( !v.is_array() || v.size() != 3 )
            return false;

        for( const json& n : v )
        {
            if( !n.is_number() )
                return false;
        }

        aOut = VECTOR3D( v[0].get<double>(), v[1].get<double>(), v[2].get<double>() );
        return true;
    };

    std::vector<FP_3DMODEL> models;

    for( const json& m : parsed )
    {
        if( !m.is_object() )
            return false;

        auto path = m.find( "path" );

        if( path == m.end() || !path->is_string() )
            return false;

        FP_3DMODEL model;
        model.m_Filename = wxString::FromUTF8( path->get<std::string>().c_str() );

        if( !vec3( m, "offset", model.m_Offset ) || !vec3( m, "rotate", model.m_Rotation )
                || !vec3( m, "scale", model.m_Scale ) )
            return false;

        models.push_back( model );
    }

    pcbjam_collab::runOnCoroutine( fr, [fr, aId, models]() {   // re-resolve on the coroutine (S4)
        BOARD_ITEM* live = testResolve( fr, aId );

        if( !live || live->Type() != PCB_FOOTPRINT_T )
            return;

        FOOTPRINT*   fp = static_cast<FOOTPRINT*>( live );
        BOARD_COMMIT commit( fr );
        commit.Modify( fp );
        fp->Models() = models;
        commit.Push( wxT( "Change 3D Models" ) );
    } );

    return true;
}

// Circuit Center (island cc13, project.plot): the board's Gerbers and drill
// files plotted with NO KiCad dialog. KiCad's own jobs (JOB_EXPORT_PCB_GERBERS,
// JOB_EXPORT_PCB_DRILL) run through the pcbnew kiface's job handler
// (KIWAY::ProcessJob, the path kicad-cli and jobsets take). In a GUI build with
// a project open, PCBNEW_JOBS_HANDLER::getBoard answers the board the editor
// holds, unsaved edits included; it never reads the file. Neither job reaches a
// dialog on this path (the zone refill that asks is off: a refill is an edit,
// so pours plot as the board last filled them; EnsurePathExists only reports).
namespace
{
// The lines reporter, its severity words and the record writer live in cc_output_common.h
// (engine run 14), shared with the board and schematic output exports; the plot is unchanged.
using PLOT_LINES_REPORTER = cc_output::LINES_REPORTER;

const char* plotSeverityWord( SEVERITY aSeverity )
{
    return cc_output::SeverityWord( aSeverity );
}

/**
 * <dir>/.cc-plot.json, written LAST and whole (a part file renamed into place):
 * {"gerbers": exit code, "drill": exit code or null, "lines": [{severity, text}]}.
 * The host reads the folder once it is there and the apply queue is empty.
 */
void writePlotDone( const std::string& aDir, int aGerbers, const std::optional<int>& aDrill,
                    const PLOT_LINES_REPORTER& aReporter )
{
    json done = json::object();
    done["gerbers"] = aGerbers;
    done["drill"] = aDrill ? json( *aDrill ) : json( nullptr );
    done["lines"] = cc_output::LinesJson( aReporter );

    cc_output::WriteRecord( aDir, ".cc-plot.json", done );
}

/** One job through the pcbnew kiface's handler; a throw is an error line and exit code -1. */
int runPlotJob( PCB_EDIT_FRAME* aFrame, JOB* aJob, PLOT_LINES_REPORTER& aReporter, const char* aWhat )
{
    try
    {
        return aFrame->Kiway().ProcessJob( KIWAY::FACE_PCB, aJob, &aReporter );
    }
    catch( const std::exception& e )
    {
        aReporter.Report( wxString::Format( wxS( "the %s job failed: %s" ), aWhat,
                                            wxString::FromUTF8( e.what() ) ),
                          RPT_SEVERITY_ERROR );
    }
    catch( ... )
    {
        aReporter.Report( wxString::Format( wxS( "the %s job failed" ), aWhat ),
                          RPT_SEVERITY_ERROR );
    }

    return -1;
}

/** The plot itself, on the apply coroutine: both jobs, then the done file. */
void runPlot( const std::string& aDir, bool aBoardPreset, bool aDrill, bool aJobFile )
{
    PLOT_LINES_REPORTER reporter;
    int                 gerbers = -1;
    std::optional<int>  drill;

    if( aDrill )
        drill = -1;

    const wxString dir = wxString::FromUTF8( aDir.c_str() );

    if( !wxFileName::DirExists( dir ) )
        wxFileName::Mkdir( dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL );

    PCB_EDIT_FRAME* fr = pcbFrame();   // re-resolved on the coroutine (S4)
    BOARD*          brd = fr ? fr->GetBoard() : nullptr;

    if( !brd || !fr->Kiway().KiFACE( KIWAY::FACE_PCB, false ) )
    {
        reporter.Report( wxS( "no board editor to plot" ), RPT_SEVERITY_ERROR );
    }
    else if( !Pgm().IsGUI() || !Pgm().GetSettingsManager().IsProjectOpen() )
    {
        // getBoard would load the saved file instead of the editor's board (and keep it).
        reporter.Report( wxS( "the job handler would not plot the editor's board" ),
                         RPT_SEVERITY_ERROR );
    }
    else
    {
        JOB_EXPORT_PCB_GERBERS gerberJob;
        gerberJob.SetConfiguredOutputPath( dir );
        gerberJob.m_useBoardPlotParams = aBoardPreset;
        gerberJob.m_createJobsFile = aJobFile;
        gerberJob.m_checkZonesBeforePlot = false;
        gerberJob.m_variant = wxEmptyString;

        if( !aBoardPreset )
        {
            // A board house set (kicad-cli's defaults otherwise): every enabled copper
            // layer, both masks, pastes and silkscreens and the outline, X2 with the
            // netlist attributes, .gbr names, KiCad's dialog precision (4.6 in mm).
            const LSET layers = ( brd->GetEnabledLayers() & LSET::AllCuMask() )
                                | LSET( { F_Mask, B_Mask, F_Paste, B_Paste, F_SilkS, B_SilkS,
                                          Edge_Cuts } );
            gerberJob.m_plotLayerSequence = layers.SeqStackupForPlotting();
            gerberJob.m_plotOnAllLayersSequence.clear();
            gerberJob.m_useX2Format = true;
            gerberJob.m_includeNetlistAttributes = true;
            gerberJob.m_useProtelFileExtension = false;
            gerberJob.m_precision = 6;
            gerberJob.m_disableApertureMacros = false;
            gerberJob.m_useDrillOrigin = false;
        }

        gerbers = runPlotJob( fr, &gerberJob, reporter, "gerbers" );

        if( aDrill )
        {
            // Both presets (kicad-cli's pcb export drill has no board option):
            // separate PTH and NPTH Excellon files in mm, decimal, absolute origin.
            JOB_EXPORT_PCB_DRILL drillJob;
            drillJob.SetConfiguredOutputPath( dir );
            drillJob.m_format = JOB_EXPORT_PCB_DRILL::DRILL_FORMAT::EXCELLON;
            drillJob.m_drillUnits = JOB_EXPORT_PCB_DRILL::DRILL_UNITS::MM;
            drillJob.m_zeroFormat = JOB_EXPORT_PCB_DRILL::ZEROS_FORMAT::DECIMAL;
            drillJob.m_drillOrigin = JOB_EXPORT_PCB_DRILL::DRILL_ORIGIN::ABS;
            drillJob.m_excellonCombinePTHNPTH = false;
            drillJob.m_excellonMirrorY = false;
            drillJob.m_excellonMinimalHeader = false;
            drillJob.m_excellonOvalDrillRoute = false;
            drillJob.m_generateMap = false;
            drillJob.m_generateTenting = false;
            drillJob.m_generateReport = false;
            drill = runPlotJob( fr, &drillJob, reporter, "drill" );
        }
    }

    writePlotDone( aDir, gerbers, drill, reporter );
}
} // namespace

// aArgsJson: {"dir": "/tmp/cc-plot/<n>", "preset": "fab"|"board", "drill": bool, "job": bool},
// those four keys only. dir is a folder under /tmp/cc-plot/, outside the project folder;
// "fab" is a board house set, "board" the board's own plot settings (kicad-cli's
// --board-plot-params); drill adds the PTH and NPTH Excellon files, job the .gbrjob file.
// Answers false (nothing queued) for any other JSON or with no board frame; true once the
// plot is queued on the apply coroutine (the host waits for kicadCollabBusy to clear and
// for <dir>/.cc-plot.json).
bool pcbPlotBoard( std::string aArgsJson )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 4 )
        return false;

    auto dir = args.find( "dir" );
    auto preset = args.find( "preset" );
    auto drill = args.find( "drill" );
    auto job = args.find( "job" );

    if( dir == args.end() || !dir->is_string() || preset == args.end() || !preset->is_string()
            || drill == args.end() || !drill->is_boolean() || job == args.end()
            || !job->is_boolean() )
    {
        return false;
    }

    const std::string d = dir->get<std::string>();

    if( !cc_output::IsRunDir( d, "/tmp/cc-plot/" ) )
        return false;

    const std::string p = preset->get<std::string>();

    if( p != "fab" && p != "board" )
        return false;

    const bool boardPreset = p == "board";
    const bool withDrill = drill->get<bool>();
    const bool withJob = job->get<bool>();

    pcbjam_collab::runOnCoroutine( fr, [d, boardPreset, withDrill, withJob]() {
        runPlot( d, boardPreset, withDrill, withJob );
    } );

    return true;
}

bool pcbCollabTestSetBoardItemLocked( std::string aId, bool aLocked )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !testResolve( fr, aId ) )
        return false;

    pcbjam_collab::runOnCoroutine( fr, [fr, aId, aLocked]() {   // re-resolve on the coroutine (S4)
        BOARD_ITEM* item = testResolve( fr, aId );

        if( !item )
            return;

        BOARD_COMMIT commit( fr );
        commit.Modify( item );
        item->SetLocked( aLocked );
        commit.Push( wxT( "Collab test lock" ) );
    } );

    return true;
}

// By-uuid variant of MoveFirst (same coroutine + BOARD_COMMIT body).
bool pcbCollabTestMoveBoardItem( std::string aId, int aDx, int aDy )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !testResolve( fr, aId ) )
        return false;

    // Re-resolve ON the coroutine: a remote remove can apply between scheduling
    // and running, and doApplyItems FREES removed items — a captured pointer
    // would be dangling and the commit would resurrect a deleted item
    // (drift-trio S4 move-vs-delete). Vanished => the move loses, silently.
    pcbjam_collab::runOnCoroutine( fr, [fr, aId, aDx, aDy]() {
        if( BOARD_ITEM* item = testResolve( fr, aId ) )
            collabTestMove( fr, item, aDx, aDy );
    } );
    return true;
}

// Duplicate (fresh uuids — exercises the FOOTPRINT copy-ctor uuid class).
std::string pcbCollabTestDuplicateBoardItem( std::string aId, int aDx, int aDy )
{
    PCB_EDIT_FRAME* fr = pcbFrame();
    BOARD_ITEM*     item = testResolve( fr, aId );

    if( !item )
        return "";

    if( item->GetParentFootprint() )
        return "";                          // duplicate roots only

    BOARD_ITEM* dup = item->Duplicate( /*addToParentGroup*/ false );
    dup->Move( VECTOR2I( aDx, aDy ) );

    std::string id = toUtf8( dup->m_Uuid.AsString() );

    pcbjam_collab::runOnCoroutine( fr, [fr, dup]() {
        BOARD_COMMIT commit( fr );
        commit.Add( dup );
        commit.Push( wxT( "Collab test duplicate" ) );
    } );

    return id;
}

// Wrapper to return footprints as vector for JS iteration
std::vector<FOOTPRINT*> Board_GetFootprints(BOARD* board) {
    if (!board) return {};
    std::vector<FOOTPRINT*> result;
    for (FOOTPRINT* fp : board->Footprints()) {
        result.push_back(fp);
    }
    return result;
}

// Wrapper to return pads as vector
std::vector<PAD*> Footprint_GetPads(FOOTPRINT* fp) {
    if (!fp) return {};
    std::vector<PAD*> result;
    for (PAD* pad : fp->Pads()) {
        result.push_back(pad);
    }
    return result;
}

// Wrapper for GetFileName since it returns wxString
std::string Board_GetFileName(BOARD* board) {
    if (!board) return "";
    return board->GetFileName().ToStdString();
}

// Wrapper for footprint reference
std::string Footprint_GetReference(FOOTPRINT* fp) {
    if (!fp) return "";
    return fp->GetReference().ToStdString();
}

// Wrapper for footprint value
std::string Footprint_GetValue(FOOTPRINT* fp) {
    if (!fp) return "";
    return fp->GetValue().ToStdString();
}

// Wrapper for pad number
std::string Pad_GetNumber(PAD* pad) {
    if (!pad) return "";
    return pad->GetNumber().ToStdString();
}

// Wrapper for pad pin function
std::string Pad_GetPinFunction(PAD* pad) {
    if (!pad) return "";
    return pad->GetPinFunction().ToStdString();
}

static bool kicadCollabBusyProbe()
{
    return pcbjam_collab::applyBusy() || !pcbjam_collab::applyQueue().empty();
}


// ── libs 0017 §2d/§2c: placed-footprint usage + update-from-library ──────────

// The Footprint Editor frame opened from this board session (nullptr when
// none exists — Player(…, false) never creates one).
static FOOTPRINT_EDIT_FRAME* fpEditorFrame( PCB_EDIT_FRAME* aFrame )
{
    if( !aFrame )
        return nullptr;

    return dynamic_cast<FOOTPRINT_EDIT_FRAME*>(
            aFrame->Kiway().Player( FRAME_FOOTPRINT_EDITOR, false ) );
}

// libs 0019 F1: drop pcbnew's SECOND footprint cache for one library.
// FOOTPRINT_LIBRARY_ADAPTER::PreloadedFootprints is read before the plugin by
// the tree, the preview and LoadFootprint, and its only invalidation is
// timestamp-gated (PCB_IO_PCBJAM_FP pins the timestamp), so a remote edit
// never reached it — the tree/preview/update kept serving the old body.
void pcbLibsInvalidatePreloaded( std::string aLibNickname )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr )
        return;

    PROJECT_PCB::FootprintLibAdapter( &fr->Prj() )
            ->InvalidatePreloaded( wxString::FromUTF8( aLibNickname.c_str() ) );
}

// Test probes (libs 0019 smoke): warm the adapter's preloaded cache the way the
// footprint editor's tree does, and read a footprint's pad sizes through the
// SAME LoadFootprint path the preview / update-from-library use.
int pcbLibsTestPreload( std::string aLibNickname )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr )
        return -1;

    FOOTPRINT_LIBRARY_ADAPTER* adapter = PROJECT_PCB::FootprintLibAdapter( &fr->Prj() );
    adapter->AsyncLoad();
    adapter->BlockUntilLoaded();
    return (int) adapter->GetFootprints( wxString::FromUTF8( aLibNickname.c_str() ), true ).size();
}

std::string pcbLibsTestLoadFootprint( std::string aLibNickname, std::string aFootprintName )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr )
        return "";

    FOOTPRINT_LIBRARY_ADAPTER* adapter = PROJECT_PCB::FootprintLibAdapter( &fr->Prj() );
    std::unique_ptr<FOOTPRINT> fp;

    try
    {
        fp.reset( adapter->LoadFootprint( wxString::FromUTF8( aLibNickname.c_str() ),
                                          wxString::FromUTF8( aFootprintName.c_str() ), false ) );
    }
    catch( ... )
    {
        return "";
    }

    if( !fp )
        return "";

    std::string out;

    for( PAD* pad : fp->Pads() )
    {
        out += std::string( pad->GetNumber().utf8_str() ) + ":"
               + std::to_string( pad->GetSize( PADSTACK::ALL_LAYERS ).x ) + "x"
               + std::to_string( pad->GetSize( PADSTACK::ALL_LAYERS ).y ) + ";";
    }

    return out;
}

// Smoke plumbing: load a library footprint into the (already open) Footprint
// Editor frame — what a tree double-click does, without tree geometry.
bool pcbLibsTestEditorLoad( std::string aLibNickname, std::string aFootprintName )
{
    FOOTPRINT_EDIT_FRAME* fe = fpEditorFrame( pcbFrame() );

    if( !fe )
        return false;

    const LIB_ID id( wxString::FromUTF8( aLibNickname.c_str() ),
                     wxString::FromUTF8( aFootprintName.c_str() ) );
    pcbjam_collab::runOnCoroutine( fe, [fe, id]() { fe->LoadFootprintFromLibrary( id ); } );
    return true;
}

// Smoke probe: the Footprint Editor's OPEN copy — "<lib>:<name>|<pads>" or "".
std::string pcbLibsTestEditorFootprint()
{
    FOOTPRINT_EDIT_FRAME* fe = fpEditorFrame( pcbFrame() );

    if( !fe || !fe->GetBoard() )
        return "";

    FOOTPRINT* fp = fe->GetBoard()->GetFirstFootprint();

    if( !fp )
        return "";

    std::string out = std::string( fe->GetLoadedFPID().Format().c_str() ) + "|";

    for( PAD* pad : fp->Pads() )
    {
        out += std::string( pad->GetNumber().utf8_str() ) + ":"
               + std::to_string( pad->GetSize( PADSTACK::ALL_LAYERS ).x ) + "x"
               + std::to_string( pad->GetSize( PADSTACK::ALL_LAYERS ).y ) + ";";
    }

    return out;
}

// Placed-instance count for a library footprint (mirror of schLibsSymbolUsage):
// drives the "a footprint you placed was updated" notice after a remote lib
// edit. Counts the board's placements PLUS the copy open in the Footprint
// Editor (libs 0019 F3 — an open-but-unplaced footprint is "used" too).
// 0 without a board frame.
int pcbLibsFootprintUsage( std::string aLibNickname, std::string aFootprintName )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() )
        return 0;

    const LIB_ID target( wxString::FromUTF8( aLibNickname.c_str() ),
                         wxString::FromUTF8( aFootprintName.c_str() ) );
    int count = 0;

    for( FOOTPRINT* fp : fr->GetBoard()->Footprints() )
    {
        if( fp->GetFPID() == target )
            count++;
    }

    if( FOOTPRINT_EDIT_FRAME* fe = fpEditorFrame( fr ) )
    {
        if( fe->GetLoadedFPID() == target )
            count++;
    }

    return count;
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

// Re-read the named library footprints into every placed instance — the
// headless core of Tools ▸ Update Footprints from Library… (DIALOG_EXCHANGE_
// FOOTPRINTS::processFootprint with the dialog's defaults), scoped to
// `aNamesJson` (a JSON array of footprint names) of `aLibNickname`. Runs as a
// normal BOARD_COMMIT on the frame's coroutine so peers receive it as an
// ordinary edit. Returns {ok, updated, missing[]}.
std::string pcbUpdateFromLibrary( std::string aLibNickname, std::string aNamesJson )
{
    nlohmann::json out;
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() )
    {
        out["ok"] = false;
        out["error"] = "no board frame";
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

    const wxString lib = wxString::FromUTF8( aLibNickname.c_str() );
    pcbjam_collab::runOnCoroutine( fr, [fr, lib, names]()
    {
        int                      updated = 0;
        std::vector<std::string> missing;
        BOARD_COMMIT             commit( fr );
        // Reverse: ExchangeFootprint appends the replacement at the end of the list.
        std::vector<FOOTPRINT*> targets;

        for( FOOTPRINT* fp : fr->GetBoard()->Footprints() )
        {
            const LIB_ID& id = fp->GetFPID();

            if( wxString( id.GetLibNickname() ) == lib && names.count( wxString( id.GetLibItemName() ) ) )
                targets.push_back( fp );
        }

        for( auto it = targets.rbegin(); it != targets.rend(); ++it )
        {
            FOOTPRINT* fp = *it;
            FOOTPRINT* fresh = fr->LoadFootprint( fp->GetFPID() );

            if( !fresh )
            {
                missing.push_back( std::string( fp->GetFPID().Format().c_str() ) );
                continue;
            }

            bool changed = fp->FootprintNeedsUpdate( fresh );
            fr->ExchangeFootprint( fp, fresh, commit, /*deleteExtraTexts*/ true,
                                   /*resetTextLayers*/ true, /*resetTextEffects*/ true,
                                   /*resetTextPositions*/ true, /*resetTextContent*/ true,
                                   /*resetFabricationAttrs*/ true,
                                   /*resetClearanceOverrides*/ true, /*reset3DModels*/ true,
                                   &changed );
            updated++;
        }

        commit.Push( wxT( "Update footprints from library" ) );
        fr->GetCanvas()->Refresh();

        // The Footprint Editor's own copy (libs 0019 F3): re-open it from the
        // (fresh) library when it is one of the named items and has no local
        // edits; with edits pending it is left alone and reported, never
        // clobbered.
        if( FOOTPRINT_EDIT_FRAME* fe = fpEditorFrame( fr ) )
        {
            const LIB_ID loaded = fe->GetLoadedFPID();

            if( wxString( loaded.GetLibNickname() ) == lib
                && names.count( wxString( loaded.GetLibItemName() ) ) )
            {
                if( fe->IsContentModified() )
                    missing.push_back( std::string( loaded.Format().c_str() )
                                       + " (open in the Footprint Editor with unsaved edits — save or revert it first)" );
                else
                    fe->LoadFootprintFromLibrary( loaded );
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

// Standalone-pcbnew shape of kicadUpdateFromLibrary(kind, lib, namesJson).
static std::string pcbUpdateFromLibraryShim( std::string aKind, std::string aLib, std::string aNames )
{
    if( aKind != "footprint" )
        return "{\"ok\":false,\"error\":\"kind not handled by this editor\"}";

    return pcbUpdateFromLibrary( aLib, aNames );
}

// ── Interactive placement of an imported footprint (Import-from-file panel) ─
// `aSexpr` is the bare `(footprint …)` blob the panel builds for
// kicadCollabApplyItems. Instead of committing it at a JS-computed point with
// SKIP_UNDO (invisible to peers, not undoable), mirror the paste path
// (PCB_CONTROL::placeBoardItems with aIsNew): stage the footprint in a
// BOARD_COMMIT, select it, and drive the stock move tool synchronously — it
// hangs off the pointer, R rotates it, Esc cancels (the commit reverts, the
// footprint is gone) and the click pushes "Place Footprint" — on the undo
// stack, and broadcast by the collab bridge like any local placement.
// PCB_ACTIONS::placeFootprint is NOT used for a pre-built footprint: it
// expects the caller to have already committed the footprint at the origin,
// which would broadcast that origin position to peers.
std::string pcbPlaceImportedItem( std::string aSexpr )
{
    json out;

    if( pcbjam_open::busy() ) // open in flight (open_gate.h) — see pcbCollabApply
    {
        out["ok"] = false;
        out["error"] = "open in flight";
        return out.dump();
    }

    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr )
    {
        out["ok"] = false;
        out["error"] = "no board frame";
        return out.dump();
    }

    // Same CallAfter + COROUTINE context as kicadCollabApplyItems. The body
    // stays on the apply slot for the whole placement (RunSynchronousAction
    // suspends inside its wxMilliSleep); remote applies queue behind it.
    pcbjam_collab::runOnCoroutine( fr, [fr, aSexpr]()
    {
        BOARD*      board = fr->GetBoard();
        BOARD_ITEM* item = makeFromBlob( *board, aSexpr );

        if( !item )
        {
            EM_ASM( { console.log( "[import-item] pcbnew: blob parse failed" ); } );
            return;
        }

        if( item->Type() != PCB_FOOTPRINT_T )
        {
            EM_ASM( { console.log( "[import-item] pcbnew: blob is not a footprint" ); } );
            delete item;
            return;
        }

        FOOTPRINT*            fp = static_cast<FOOTPRINT*>( item );
        TOOL_MANAGER*         tm = fr->GetToolManager();
        KIGFX::VIEW_CONTROLS* vc = fr->GetCanvas()->GetViewControls();

        tm->RunAction( ACTIONS::selectionClear );

        // New-item preparation, as placeBoardItems( aIsNew ) and the footprint
        // viewer's "add to board" do.
        const_cast<KIID&>( fp->m_Uuid ) = KIID();
        fp->RunOnChildren( []( BOARD_ITEM* aChild ) { const_cast<KIID&>( aChild->m_Uuid ) = KIID(); },
                           RECURSE_MODE::RECURSE );
        fp->SetParent( board );
        fp->SetPath( KIID_PATH() );

        for( PAD* pad : fp->Pads() )
            pad->SetNetCode( 0 ); // library pads carry orphaned nets

        if( fp->IsFlipped() )
            fp->Flip( fp->GetPosition(), fr->GetPcbNewSettings()->m_FlipDirection );

        // Start under the pointer; the move tool anchors the selection there.
        fp->SetPosition( vc->GetCursorPosition() );

        BOARD_COMMIT commit( fr );
        EDA_ITEMS    toSel{ fp };
        tm->RunAction<EDA_ITEMS*>( ACTIONS::selectItems, &toSel );
        commit.Add( fp );

        PCB_SELECTION& selection = tm->GetTool<PCB_SELECTION_TOOL>()->GetSelection();
        selection.SetReferencePoint( fp->GetPosition() );
        vc->SetCursorPosition( vc->GetMousePosition(), false );
        tm->ProcessEvent( EVENTS::SelectedEvent );

        if( tm->RunSynchronousAction( PCB_ACTIONS::move, &commit ) )
            commit.Push( wxT( "Place Footprint" ) );
        else
            commit.Revert();
    } );

    out["ok"] = true;
    return out.dump();
}

// ════════════════════════ Circuit Center, engine run 12 (engine run A) ════════════════════════
//
// The board half of four island ops (cc18): Update the board (board.preview, board.update; cap
// boardUpdate), the board check (board.check, cap drc), why a rule applies (board.explain, cap
// explain) and the selected board item's properties (board.item.get and board.item.set, cap
// boardItem). Every export refuses at once (false or "") with no board frame or while an open
// is in flight (pcbjam_open::busy()); the runs queue on the apply coroutine and write their
// record file LAST (a part file renamed into place), as kicadPlotBoard does; no KiCad dialog
// opens on any of these roads (wxLogNull keeps wx's log window shut too).

#ifdef KICAD_MERGED_EMBIND
// eeschema_embind.cpp: the hidden schematic editor's checks and KiCad netlist, as JSON.
std::string ccSchNetlistForBoard( KIWAY_PLAYER* aPlayer );
#endif

namespace
{
using pcbjam_collab::toUtf8;

/** A run folder: under aRoot, longer than it, no "..", no trailing slash, no NUL (the plot's rule). */
bool ccIsRunDir( const std::string& aDir, const std::string& aRoot )
{
    return cc_output::IsRunDir( aDir, aRoot );
}

void ccEnsureDir( const std::string& aDir )
{
    const wxString dir = wxString::FromUTF8( aDir.c_str() );

    if( !wxFileName::DirExists( dir ) )
        wxFileName::Mkdir( dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL );
}

/** <dir>/<name>, written whole: a part file renamed into place. */
void ccWriteJson( const std::string& aDir, const char* aName, const json& aBody )
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

json ccLines( const PLOT_LINES_REPORTER& aReporter )
{
    json lines = json::array();

    for( const auto& [severity, text] : aReporter.m_lines )
    {
        json line = json::object();
        line["severity"] = plotSeverityWord( severity );
        line["text"] = toUtf8( text );
        lines.push_back( std::move( line ) );
    }

    return lines;
}

/** A string of the records: KiCad's text, cut to aMax bytes. */
std::string ccText( const wxString& aText, size_t aMax = 1024 )
{
    std::string s = toUtf8( aText );

    if( s.size() > aMax )
    {
        s.resize( aMax );

        // Never end inside a UTF-8 sequence.
        while( !s.empty() && ( static_cast<unsigned char>( s.back() ) & 0xC0 ) == 0x80 )
            s.pop_back();

        if( !s.empty() && ( static_cast<unsigned char>( s.back() ) & 0x80 ) )
            s.pop_back();
    }

    return s;
}

/** KiCad's net name as the records carry it: unescaped. */
std::string ccNet( const wxString& aNetname )
{
    return ccText( UnescapeString( aNetname ), 512 );
}

/** Millimeters at 4 decimals. */
double ccMm( long long aIU )
{
    return std::round( pcbIUScale.IUTomm( (int) aIU ) * 10000.0 ) / 10000.0;
}

double ccRound( double aValue, double aScale )
{
    return std::round( aValue * aScale ) / aScale;
}

/** A layer's canonical KiCad name ("F.Cu", "In1.Cu", "Edge.Cuts"); "" for none. */
std::string ccLayer( PCB_LAYER_ID aLayer )
{
    if( aLayer < 0 || aLayer >= PCB_LAYER_ID_COUNT )
        return "";

    return toUtf8( LSET::Name( aLayer ) );   // the file format's name, never the translated one
}

/** The layer a canonical name names, among the layers the board enables; UNDEFINED_LAYER else. */
PCB_LAYER_ID ccLayerByName( const BOARD* aBoard, const std::string& aName )
{
    for( PCB_LAYER_ID layer : aBoard->GetEnabledLayers().Seq() )
    {
        if( toUtf8( LSET::Name( layer ) ) == aName )
            return layer;
    }

    return UNDEFINED_LAYER;
}

bool ccIsLowerUuid( const std::string& aText )
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

BOARD_ITEM* ccResolve( BOARD* aBoard, const std::string& aUuid )
{
    if( !aBoard )
        return nullptr;

    return aBoard->ResolveItem( KIID( wxString::FromUTF8( aUuid.c_str() ) ), /*allowNull*/ true );
}

/** Every line a run's KiCad code reports, counted by severity (empty lines left out). */
class CC_COUNTING_REPORTER : public REPORTER
{
public:
    using REPORTER::Report;

    REPORTER& Report( const wxString& aText, SEVERITY aSeverity = RPT_SEVERITY_UNDEFINED ) override
    {
        REPORTER::Report( aText, aSeverity );

        if( aText.IsEmpty() )
            return *this;

        switch( aSeverity )
        {
        case RPT_SEVERITY_ACTION:  m_actions++;  break;
        case RPT_SEVERITY_WARNING: m_warnings++; break;
        case RPT_SEVERITY_ERROR:   m_errors++;   break;
        default:                                 break;
        }

        return *this;
    }

    int m_actions = 0;
    int m_warnings = 0;
    int m_errors = 0;
};

// ───────────────────────────── the board's schematic netlist ─────────────────────────────

/** What the hidden schematic editor answered: the problems, the warnings and the sheets read. */
struct CC_SCH_NETLIST
{
    std::string              error;     // "", "no_schematic", "schematic_unreadable", "not_annotated"
    std::string              message;
    json                     blocked = json::array();    // [{code, ref, message}]
    json                     warnings = json::array();   // [{code, message}]
    std::vector<std::string> sheets;                     // absolute engine paths, KiCad's order
};

/**
 * The board's schematic as a NETLIST, read IN THIS ENGINE as KiCad's own F8 does under
 * __EMSCRIPTEN__ (PCB_EDIT_FRAME::TestStandalone, then MAIL_SCH_GET_NETLIST), minus every
 * dialog: the root file is <project path>/<project name>.kicad_sch, each top-level sheet the
 * project lists must be in the folder (a short schematic is never read in silence), the
 * schematic editor is created HIDDEN and re-opens the files every run (the folder's current
 * sheets), the open runs on the main stack as KiCad's UpdatePCBFromSchematic runs it
 * (RunMainStack) under the open gate's BusyGuard with no lock prompt, and the checks and the
 * netlist are eeschema's (ccSchNetlistForBoard). False with aResult.error set: no_schematic,
 * schematic_unreadable, or not_annotated (aResult.blocked names the parts). Runs on the apply
 * coroutine only. Shared by Update the board and the board check's schematic parity.
 */
bool boardSchematicNetlist( PCB_EDIT_FRAME* aFrame, NETLIST& aOut, CC_SCH_NETLIST& aResult )
{
#ifndef KICAD_MERGED_EMBIND
    aResult.error = "schematic_unreadable";
    aResult.message = "this engine has no schematic editor";
    return false;
#else
    PROJECT&   prj = aFrame->Prj();
    wxFileName root( prj.GetProjectPath(), prj.GetProjectName(),
                     FILEEXT::KiCadSchematicFileExtension );

    if( !root.FileExists() )
    {
        aResult.error = "no_schematic";
        aResult.message = toUtf8( root.GetFullPath() );
        return false;
    }

    for( const TOP_LEVEL_SHEET_INFO& info : prj.GetProjectFile().GetTopLevelSheets() )
    {
        wxFileName sheet( prj.GetProjectPath(), info.filename );

        if( !sheet.FileExists() )
        {
            aResult.error = "schematic_unreadable";
            aResult.message = "a top-level sheet is missing: " + toUtf8( info.filename );
            return false;
        }
    }

    // What the hidden editor last opened, and every sheet file it read with its size and
    // modification time: while none changed (and the root is the same), the open is kept
    // and only the checks and the netlist run again. Each re-open of a design holds more
    // of the engine's memory, which never shrinks (about 64 MB a re-open of Glasgow).
    static std::vector<std::string> s_files;
    static std::string              s_stamp;

    auto stampOf = [&]( const std::vector<std::string>& aFiles ) -> std::string
                   {
                       std::string stamp = toUtf8( root.GetFullPath() );

                       for( const TOP_LEVEL_SHEET_INFO& info : prj.GetProjectFile().GetTopLevelSheets() )
                           stamp += "|top:" + toUtf8( info.filename );

                       for( const std::string& file : aFiles )
                       {
                           wxFileName fn( wxString::FromUTF8( file.c_str() ) );
                           stamp += "|" + file;

                           if( !fn.FileExists() )
                           {
                               stamp += ":gone";
                               continue;
                           }

                           stamp += ":" + std::to_string( fn.GetSize().GetValue() ) + ":"
                                    + std::to_string( fn.GetModificationTime().GetValue().GetValue() );
                       }

                       return stamp;
                   };

    bool        opened = false;
    std::string answer;

    auto readSchematic =
            [&]()
            {
                pcbjam_open::BusyGuard busy;
                wxLogNull              noLog;
                KIWAY_PLAYER*          player = aFrame->Kiway().Player( FRAME_SCH, false );

                if( player && !s_stamp.empty() && stampOf( s_files ) == s_stamp )
                {
                    opened = true;
                }
                else
                {
                    if( !player )
                        player = aFrame->Kiway().Player( FRAME_SCH, true );

                    if( !player )
                        return;

                    // The lock is the hidden editor's own; never ask about it.
                    prj.SetLockOverrideGranted( true );

                    wxEventBlocker blocker( aFrame );
                    opened = player->OpenProjectFiles(
                            std::vector<wxString>( 1, root.GetFullPath() ) );
                }

                if( opened )
                    answer = ccSchNetlistForBoard( player );
            };

    if( COROUTINE<int, int>* cor = pcbjam_collab::activeApplySlot().cor )
        cor->RunMainStack( readSchematic );
    else
        readSchematic();

    json parsed = json::parse( answer, nullptr, /*allow_exceptions*/ false );

    s_files.clear();
    s_stamp.clear();

    if( opened && !parsed.is_discarded() && parsed.is_object() && parsed.contains( "files" )
            && parsed.value( "error", std::string() ).empty() )
    {
        for( const json& f : parsed["files"] )
        {
            if( f.is_string() )
                s_files.push_back( f.get<std::string>() );
        }

        s_stamp = stampOf( s_files );
    }

    if( !opened || parsed.is_discarded() || !parsed.is_object() )
    {
        aResult.error = "schematic_unreadable";
        aResult.message = "the schematic could not be opened";
        return false;
    }

    if( parsed.value( "error", std::string() ) != "" )
    {
        aResult.error = "schematic_unreadable";
        aResult.message = parsed.value( "message", std::string() );
        return false;
    }

    for( const json& s : parsed["sheets"] )
    {
        if( s.is_string() )
            aResult.sheets.push_back( s.get<std::string>() );
    }

    if( aResult.sheets.empty() )
        aResult.sheets.push_back( toUtf8( root.GetFullPath() ) );

    aResult.blocked = parsed["blocked"];
    aResult.warnings = parsed["warnings"];

    if( !aResult.blocked.empty() )
    {
        aResult.error = "not_annotated";
        return false;
    }

    try
    {
        const std::string text = parsed.value( "text", std::string() );
        auto*             lineReader = new STRING_LINE_READER( text, wxS( "Eeschema netlist" ) );
        KICAD_NETLIST_READER netlistReader( lineReader, &aOut );   // owns lineReader

        netlistReader.LoadNetlist();
    }
    catch( const IO_ERROR& e )
    {
        aResult.error = "schematic_unreadable";
        aResult.message = toUtf8( e.What() );
        return false;
    }

    return true;
#endif
}

// ───────────────────────────── Update the board ─────────────────────────────

/** 16 hex digits, FNV-1a 64 over the text: stable for the life of the engine and across runs. */
std::string ccDigest( const std::string& aText )
{
    uint64_t hash = 1469598103934665603ULL;

    for( unsigned char c : aText )
    {
        hash ^= c;
        hash *= 1099511628211ULL;
    }

    char buf[17];
    std::snprintf( buf, sizeof( buf ), "%016llx", static_cast<unsigned long long>( hash ) );
    return buf;
}

/**
 * The preview's token: the parsed netlist as pcbnew formats it (no date in it), the three
 * options and the board's change count. An apply whose token differs changes nothing (stale).
 */
std::string ccUpdateToken( NETLIST& aNetlist, bool aReplace, bool aDelete, bool aRelink,
                           int aBoardStamp )
{
    STRING_FORMATTER formatter;
    aNetlist.Format( "pcb_netlist", &formatter, 0, 0 );

    std::string text = formatter.GetString();
    text += aReplace ? "|r1" : "|r0";
    text += aDelete ? "|d1" : "|d0";
    text += aRelink ? "|l1" : "|l0";
    text += "|t" + std::to_string( aBoardStamp );

    return ccDigest( text );
}

/** One change log record in the island's closed shape (each kind its own keys). */
json ccChangeRecord( const NETLIST_CHANGE& aChange )
{
    const std::string kind = toUtf8( aChange.kind );
    json              r = json::object();

    r["kind"] = kind;

    auto put = [&]( const char* aKey, const wxString& aValue )
               {
                   r[aKey] = ccText( aValue );
               };

    if( kind == "add" )
    {
        put( "ref", aChange.ref );
        put( "footprint", aChange.footprint );
        r["pins"] = json::array();
        r["extraPads"] = json::array();
        r["sameName"] = false;
    }
    else if( kind == "remove" )
    {
        put( "ref", aChange.ref );
        put( "footprint", aChange.footprint );
    }
    else if( kind == "footprint" || kind == "value" )
    {
        put( "ref", aChange.ref );
        put( "from", aChange.from );
        put( "to", aChange.to );
    }
    else if( kind == "reference" )
    {
        put( "ref", aChange.ref );
        put( "to", aChange.to );
    }
    else if( kind == "detail" )
    {
        put( "ref", aChange.ref );
        put( "topic", aChange.topic );
        put( "name", aChange.name );
        put( "from", aChange.from );
        put( "to", aChange.to );
    }
    else if( kind == "connect" )
    {
        put( "ref", aChange.ref );
        put( "pin", aChange.pin );
        put( "to", aChange.to );
    }
    else if( kind == "reconnect" )
    {
        put( "ref", aChange.ref );
        put( "pin", aChange.pin );
        put( "from", aChange.from );
        put( "to", aChange.to );
    }
    else if( kind == "disconnect" )
    {
        put( "ref", aChange.ref );
        put( "pin", aChange.pin );
        put( "from", aChange.from );
    }
    else if( kind == "net" || kind == "unusedNet" )
    {
        put( "net", aChange.net );
    }
    else if( kind == "copper" )
    {
        put( "item", aChange.topic );
        put( "name", aChange.name );
        put( "from", aChange.from );
        put( "to", aChange.to );
    }
    else   // problem
    {
        r["kind"] = "problem";
        put( "code", aChange.code );
        r["severity"] = toUtf8( aChange.severity ) == "error" ? "error" : "warning";
        put( "ref", aChange.ref );
        put( "pin", aChange.pin );
        put( "footprint", aChange.footprint );
        put( "net", aChange.net );
        put( "name", aChange.name );
    }

    put( "message", aChange.message );
    return r;
}

/** A problem record the export adds itself (the annotation check, duplicate sheet names). */
json ccProblemRecord( const std::string& aCode, const std::string& aSeverity,
                      const std::string& aRef, const std::string& aMessage )
{
    json r = json::object();
    r["kind"] = "problem";
    r["code"] = aCode;
    r["severity"] = aSeverity;
    r["ref"] = aRef;
    r["pin"] = "";
    r["footprint"] = "";
    r["net"] = "";
    r["name"] = "";
    r["message"] = aMessage;
    return r;
}

/**
 * A new part's pins, read from its footprint before anything is placed (KiCad's dry run
 * says nothing of a new part's pads): each pin of the symbol with its net and whether the
 * footprint has a pad of that number, the numbered pads no pin names, and whether a part of
 * that designator is already on the board (the footprints not linked to the symbols).
 */
void ccFillAddRecord( PCB_EDIT_FRAME* aFrame, NETLIST& aNetlist, json& aRecord )
{
    const wxString ref = wxString::FromUTF8( aRecord["ref"].get<std::string>().c_str() );
    COMPONENT*     component = aNetlist.GetComponentByReference( ref );

    aRecord["sameName"] = aFrame->GetBoard()->FindFootprintByReference( ref ) != nullptr;

    if( !component )
        return;

    FOOTPRINT* fp = aFrame->LoadFootprint( component->GetFPID() );
    json       pins = json::array();
    std::set<wxString> named;

    for( unsigned ii = 0; ii < component->GetNetCount() && pins.size() < 4096; ++ii )
    {
        const COMPONENT_NET& net = component->GetNet( ii );
        json                 pin = json::object();

        pin["pin"] = ccText( net.GetPinName() );
        pin["net"] = ccNet( net.GetNetName() );
        pin["pad"] = fp && fp->FindPadByNumber( net.GetPinName() ) != nullptr;
        named.insert( net.GetPinName() );
        pins.push_back( std::move( pin ) );
    }

    json extra = json::array();

    if( fp )
    {
        std::set<wxString> seen;

        for( PAD* pad : fp->Pads() )
        {
            const wxString number = pad->GetNumber();

            if( number.IsEmpty() || named.count( number ) || !seen.insert( number ).second )
                continue;

            if( extra.size() < 4096 )
                extra.push_back( ccText( number ) );
        }

        delete fp;
    }

    aRecord["pins"] = std::move( pins );
    aRecord["extraPads"] = std::move( extra );
}

/**
 * The new footprints KiCad spread at {0,0} moved together beside the board: the pile's upper
 * left 5 mm right of the board's outline at its top, else right of every other item, else
 * (an empty board) centered on the page, KiCad's own point for an empty board. FP_JUST_ADDED
 * is cleared (else their pads stay out of connectivity until a drag, so no unrouted lines),
 * the move joins the update's undo step (APPEND_UNDO: one Undo takes all of it back), and
 * they stay selected; the view fits the board and the new parts (Zoom to Objects: KiCad's
 * Zoom to Fit fits the board's outline when it has one, leaving the new parts off screen).
 */
void ccPlaceNewFootprints( PCB_EDIT_FRAME* aFrame, const std::vector<FOOTPRINT*>& aNew )
{
    if( aNew.empty() )
        return;

    BOARD*               board = aFrame->GetBoard();
    std::set<FOOTPRINT*> fresh( aNew.begin(), aNew.end() );
    BOX2I                pile = aNew.front()->GetBoundingBox( false );

    for( FOOTPRINT* fp : aNew )
        pile.Merge( fp->GetBoundingBox( false ) );

    const int gap = pcbIUScale.mmToIU( 5 );
    BOX2I     edges = board->GetBoardEdgesBoundingBox();
    VECTOR2I  delta;

    if( edges.GetWidth() > 0 || edges.GetHeight() > 0 )
    {
        delta = VECTOR2I( edges.GetRight() + gap, edges.GetTop() ) - pile.GetOrigin();
    }
    else
    {
        BOX2I items;
        bool  any = false;

        auto add = [&]( const BOX2I& aBox )
                   {
                       if( !any )
                           items = aBox;
                       else
                           items.Merge( aBox );

                       any = true;
                   };

        for( FOOTPRINT* fp : board->Footprints() )
        {
            if( !fresh.count( fp ) )
                add( fp->GetBoundingBox( false ) );
        }

        for( PCB_TRACK* track : board->Tracks() )
            add( track->GetBoundingBox() );

        for( BOARD_ITEM* drawing : board->Drawings() )
            add( drawing->GetBoundingBox() );

        for( ZONE* zone : board->Zones() )
            add( zone->GetBoundingBox() );

        if( any )
        {
            delta = VECTOR2I( items.GetRight() + gap, items.GetTop() ) - pile.GetOrigin();
        }
        else
        {
            VECTOR2I page = board->GetPageSettings().GetSizeIU( pcbIUScale.IU_PER_MILS );
            delta = VECTOR2I( page.x / 2, page.y / 2 ) - pile.Centre();
        }
    }

    BOARD_COMMIT commit( aFrame );

    for( FOOTPRINT* fp : aNew )
    {
        commit.Modify( fp );
        fp->Move( delta );
        fp->SetAttributes( fp->GetAttributes() & ~FP_JUST_ADDED );
    }

    commit.Push( _( "Update Netlist" ), APPEND_UNDO );

    board->BuildConnectivity();
    aFrame->Compile_Ratsnest( true );

    TOOL_MANAGER* toolMgr = aFrame->GetToolManager();
    EDA_ITEMS     items( aNew.begin(), aNew.end() );

    toolMgr->RunAction( ACTIONS::selectionClear );
    toolMgr->RunAction<EDA_ITEMS*>( ACTIONS::selectItems, &items );
    toolMgr->RunAction( ACTIONS::zoomFitObjects );   // Zoom to Fit would fit the outline alone
    aFrame->GetCanvas()->Refresh();
}

struct CC_UPDATE_ARGS
{
    long long   run = 0;
    std::string dir;
    bool        apply = false;
    bool        replaceFootprints = true;
    bool        deleteExtra = false;
    bool        relinkByReference = false;
    std::string token;
};

/**
 * Update the board, on the apply coroutine: the Update spec's steps 1 to 8. The schematic is
 * read in this engine (boardSchematicNetlist); a preview runs KiCad's updater as a dry run,
 * an apply checks the token, runs it for real (one BOARD_COMMIT "Update Netlist"), then
 * OnNetlistChanged (nets, classes, rules) and ccPlaceNewFootprints. Writes <dir>/report.json
 * (the engine report) when the updater ran (and for a preview stopped by parts with no number,
 * so it can list them), then <dir>/.cc-update.json LAST: {error, message, lines}.
 */
void ccRunBoardUpdate( const CC_UPDATE_ARGS& aArgs )
{
    PLOT_LINES_REPORTER lines;
    std::string         error;
    std::string         message;

    ccEnsureDir( aArgs.dir );

    auto finish = [&]()
                  {
                      json done = json::object();
                      done["error"] = error;
                      done["message"] = message;
                      done["lines"] = ccLines( lines );
                      ccWriteJson( aArgs.dir, ".cc-update.json", done );
                  };

    PCB_EDIT_FRAME* fr = pcbFrame();   // re-resolved on the coroutine (S4)

    if( !fr || !fr->GetBoard() )
    {
        error = "island_error";
        message = "no board editor";
        finish();
        return;
    }

    wxLogNull noLog;

    try
    {
        NETLIST        netlist;
        CC_SCH_NETLIST sch;
        const bool     read = boardSchematicNetlist( fr, netlist, sch );
        json           problems = json::array();
        int            problemWarnings = 0;
        int            problemErrors = 0;

        for( const json& b : sch.blocked )
        {
            problems.push_back( ccProblemRecord( b.value( "code", std::string() ), "error",
                                                 b.value( "ref", std::string() ),
                                                 b.value( "message", std::string() ) ) );
            problemErrors++;
        }

        for( const json& w : sch.warnings )
        {
            problems.push_back( ccProblemRecord( w.value( "code", std::string() ), "warning", "",
                                                 w.value( "message", std::string() ) ) );
            lines.Report( wxString::FromUTF8( w.value( "message", std::string() ).c_str() ),
                          RPT_SEVERITY_WARNING );
            problemWarnings++;
        }

        json report = json::object();
        report["run"] = aArgs.run;
        report["ok"] = true;
        report["token"] = "";
        report["blocked"] = "";
        report["sheets"] = sch.sheets;
        report["added"] = json::array();
        report["truncated"] = false;
        report["unmapped"] = 0;

        if( !read && sch.error != "not_annotated" )
        {
            error = "schematic_unreadable";
            message = sch.error == "no_schematic" ? "no schematic beside the board: " + sch.message
                                                  : sch.message;
            finish();
            return;
        }

        if( !read )
        {
            if( aArgs.apply )
            {
                error = "not_annotated";
                message = "parts on the schematic have no number";
                finish();
                return;
            }

            report["blocked"] = "not_annotated";
            report["changes"] = problems;
            report["warnings"] = problemWarnings;
            report["errors"] = problemErrors;
            ccWriteJson( aArgs.dir, "report.json", report );
            finish();
            return;
        }

        netlist.SortByReference();
        netlist.SetFindByTimeStamp( !aArgs.relinkByReference );
        netlist.SetReplaceFootprints( aArgs.replaceFootprints );

        const std::string token = ccUpdateToken( netlist, aArgs.replaceFootprints,
                                                 aArgs.deleteExtra, aArgs.relinkByReference,
                                                 fr->GetBoard()->GetTimeStamp() );

        if( aArgs.apply && token != aArgs.token )
        {
            error = "stale";
            message = "the board or the schematic changed since the preview";
            finish();
            return;
        }

        TOOL_MANAGER* toolMgr = fr->GetToolManager();

        if( aArgs.apply )
        {
            toolMgr->DeactivateTool();
            toolMgr->RunAction( ACTIONS::selectionClear );
        }

        CC_COUNTING_REPORTER        counter;
        std::vector<NETLIST_CHANGE> log;
        BOARD_NETLIST_UPDATER       updater( fr, fr->GetBoard() );

        updater.SetReporter( &counter );
        updater.SetChangeLog( &log );
        updater.SetIsDryRun( !aArgs.apply );
        updater.SetReplaceFootprints( aArgs.replaceFootprints );
        updater.SetDeleteUnusedFootprints( aArgs.deleteExtra );
        updater.SetLookupByTimestamp( !aArgs.relinkByReference );
        updater.SetTransferGroups( false );
        updater.SetApplyDesignBlockLayouts( false );
        updater.SetOverrideLocks( false );
        updater.SetUpdateFields( true );
        updater.SetRemoveExtraFields( false );
        updater.UpdateNetlist( netlist );

        json added = json::array();

        if( aArgs.apply )
        {
            bool drag = false;   // never started: the parts are placed beside the board instead
            fr->OnNetlistChanged( updater, &drag );

            std::vector<FOOTPRINT*> fresh = updater.GetAddedFootprints();
            ccPlaceNewFootprints( fr, fresh );

            for( FOOTPRINT* fp : fresh )
                added.push_back( ccText( fp->GetReference() ) );
        }

        json changes = problems;
        bool truncated = false;

        for( const NETLIST_CHANGE& change : log )
        {
            if( changes.size() >= 4096 )
            {
                truncated = true;
                break;
            }

            json record = ccChangeRecord( change );

            if( !aArgs.apply && record["kind"] == "add" )
                ccFillAddRecord( fr, netlist, record );

            changes.push_back( std::move( record ) );
        }

        report["token"] = aArgs.apply ? "" : token;
        report["changes"] = std::move( changes );
        report["added"] = std::move( added );
        report["warnings"] = counter.m_warnings + problemWarnings;
        report["errors"] = counter.m_errors + problemErrors;
        report["truncated"] = truncated;
        report["unmapped"] =
                counter.m_actions + counter.m_warnings + counter.m_errors - (int) log.size();

        ccWriteJson( aArgs.dir, "report.json", report );
    }
    catch( const std::exception& e )
    {
        error = "island_error";
        message = e.what();
    }
    catch( ... )
    {
        error = "island_error";
        message = "the update failed";
    }

    finish();
}

// ───────────────────────────── the board check ─────────────────────────────

/** Where a rule came from, and its name as the page shows it (a net class's own name). */
std::pair<std::string, std::string> ccRuleSource( const DRC_RULE* aRule )
{
    if( !aRule )
        return { "other", "" };

    const wxString name = aRule->m_Name;

    // The implicit rules are named "netclass 'Power'", "keepout area 'X'", "tuning profile 'Y'".
    auto quoted = [&]() -> std::string
                  {
                      int first = name.Find( '\'' );
                      int last = name.Find( '\'', true );

                      if( first != wxNOT_FOUND && last > first )
                          return ccText( name.Mid( first + 1, last - first - 1 ) );

                      return "";
                  };

    switch( aRule->GetImplicitSource() )
    {
    case DRC_IMPLICIT_SOURCE::NONE:                   return { "custom", ccText( name ) };
    case DRC_IMPLICIT_SOURCE::BOARD_SETUP_CONSTRAINT: return { "board", "" };
    case DRC_IMPLICIT_SOURCE::NET_CLASS:              return { "netclass", quoted() };
    case DRC_IMPLICIT_SOURCE::KEEPOUT:                return { "keepout", quoted() };
    case DRC_IMPLICIT_SOURCE::TUNING_PROFILE:         return { "tuning", quoted() };
    default:                                          return { "other", ccText( name ) };
    }
}

/** One item a finding names, in the island's closed shape. */
json ccDrcItem( EDA_ITEM* aItem, UNITS_PROVIDER& aUnits )
{
    json r = json::object();
    r["uuid"] = toUtf8( aItem->m_Uuid.AsString() );
    r["kind"] = "other";
    r["ref"] = "";
    r["pad"] = "";
    r["net"] = "";
    r["layer"] = "";
    r["parent"] = "";
    r["description"] = ccText( aItem->GetItemDescription( &aUnits, true ) );

    BOARD_ITEM* item = aItem->IsBOARD_ITEM() ? static_cast<BOARD_ITEM*>( aItem ) : nullptr;

    if( !item )
        return r;

    FOOTPRINT* parent = item->GetParentFootprint();

    if( parent && item->Type() != PCB_FOOTPRINT_T )
    {
        r["ref"] = ccText( parent->GetReference() );
        r["parent"] = toUtf8( parent->m_Uuid.AsString() );
    }

    if( BOARD_CONNECTED_ITEM* connected = dynamic_cast<BOARD_CONNECTED_ITEM*>( item ) )
        r["net"] = ccNet( connected->GetNetname() );

    switch( item->Type() )
    {
    case PCB_PAD_T:
    {
        PAD* pad = static_cast<PAD*>( item );
        r["kind"] = pad->GetAttribute() == PAD_ATTRIB::NPTH && pad->GetNumber().IsEmpty()
                            ? "hole" : "pad";
        r["pad"] = ccText( pad->GetNumber() );
        break;
    }

    case PCB_TRACE_T:
        r["kind"] = "track";
        r["layer"] = ccLayer( item->GetLayer() );
        break;

    case PCB_ARC_T:
        r["kind"] = "arc";
        r["layer"] = ccLayer( item->GetLayer() );
        break;

    case PCB_VIA_T:
        r["kind"] = "via";
        break;

    case PCB_ZONE_T:
        r["kind"] = "zone";
        r["layer"] = ccLayer( static_cast<ZONE*>( item )->GetFirstLayer() );
        break;

    case PCB_FOOTPRINT_T:
        r["kind"] = "footprint";
        r["ref"] = ccText( static_cast<FOOTPRINT*>( item )->GetReference() );
        r["layer"] = ccLayer( item->GetLayer() );
        break;

    case PCB_FIELD_T:
        r["kind"] = "field";
        r["layer"] = ccLayer( item->GetLayer() );
        break;

    case PCB_TEXT_T:
        r["kind"] = "text";
        r["layer"] = ccLayer( item->GetLayer() );
        break;

    case PCB_SHAPE_T:
        r["kind"] = item->GetLayer() == Edge_Cuts ? "outline" : "shape";
        r["layer"] = ccLayer( item->GetLayer() );
        break;

    default:
        r["layer"] = ccLayer( item->GetLayer() );
        break;
    }

    return r;
}

/** "actual 0.1500 mm" in KiCad's message, in mm; null when the message carries none. */
json ccActual( const wxString& aMessage )
{
    const std::string text = toUtf8( aMessage );
    const std::string key = "actual ";
    size_t            at = text.find( key );

    if( at == std::string::npos )
        return nullptr;

    at += key.size();
    char*        end = nullptr;
    const char*  start = text.c_str() + at;
    const double value = std::strtod( start, &end );

    if( end == start || !std::isfinite( value ) )
        return nullptr;

    while( *end == ' ' )
        ++end;

    if( std::strncmp( end, "mm", 2 ) != 0 )
        return nullptr;

    return ccRound( value, 10000.0 );
}

struct CC_CHECK_COUNTS
{
    int errors = 0;
    int warnings = 0;
    int excluded = 0;
    int listed = 0;
    bool truncated = false;
};

/** The findings of one provider, appended to aOut until the island's cap of 4,096 in all. */
void ccCollectFindings( BOARD* aBoard, DRC_ITEMS_PROVIDER& aProvider,
                        std::map<KIID, EDA_ITEM*>& aItemMap, UNITS_PROVIDER& aUnits, json& aOut,
                        CC_CHECK_COUNTS& aCounts )
{
    BOARD_DESIGN_SETTINGS& bds = aBoard->GetDesignSettings();

    for( int i = 0; i < aProvider.GetCount(); ++i )
    {
        const std::shared_ptr<RC_ITEM> item = aProvider.GetItem( i );
        PCB_MARKER* marker = item ? dynamic_cast<PCB_MARKER*>( item->GetParent() ) : nullptr;

        if( !marker )
            continue;

        const bool     excluded = marker->IsTreatedAsExcluded();
        const SEVERITY severity = bds.GetSeverity( item->GetErrorCode() );
        const char*    word = severity == RPT_SEVERITY_ERROR ? "error" : "warning";

        if( excluded )
            aCounts.excluded++;
        else if( severity == RPT_SEVERITY_ERROR )
            aCounts.errors++;
        else
            aCounts.warnings++;

        if( aCounts.listed >= 4096 )
        {
            aCounts.truncated = true;
            continue;
        }

        json finding = json::object();
        finding["marker"] = toUtf8( marker->m_Uuid.AsString() );
        finding["type"] = ccText( item->GetSettingsKey() );
        finding["severity"] = word;
        finding["excluded"] = excluded;
        finding["comment"] = excluded ? ccText( marker->GetComment() ) : std::string();
        finding["layer"] = ccLayer( marker->GetLayer() );
        finding["x"] = ccMm( marker->GetPosition().x );
        finding["y"] = ccMm( marker->GetPosition().y );

        json items = json::array();

        for( const KIID& id : item->GetIDs() )
        {
            if( items.size() >= 64 )
                break;

            auto it = aItemMap.find( id );

            if( it != aItemMap.end() && it->second )
                items.push_back( ccDrcItem( it->second, aUnits ) );
        }

        finding["items"] = std::move( items );

        const DRC_ITEM* drcItem = dynamic_cast<const DRC_ITEM*>( item.get() );

        if( const DRC_RULE* rule = drcItem ? drcItem->GetViolatingRule() : nullptr )
        {
            auto [source, name] = ccRuleSource( rule );
            json r = json::object();
            r["source"] = source;
            r["name"] = name;
            finding["rule"] = std::move( r );
        }
        else
        {
            finding["rule"] = nullptr;
        }

        finding["actual"] = ccActual( item->GetErrorMessage( false ) );
        finding["message"] = ccText( item->GetErrorMessage( false ) );

        aOut.push_back( std::move( finding ) );
        aCounts.listed++;
    }
}

/**
 * Check the board, on the apply coroutine: the DRC dialog's preparation and the DRC job's
 * body, minus the job's library wait (FootprintLibAdapter AsyncLoad + BlockUntilLoaded would
 * queue every footprint library the island serves): the custom rules compiled first (a parse
 * error ends the run, rules_invalid, as KiCad's dialog stops), the copper areas filled when
 * asked (headless, its own undo step), the schematic read for parity when asked
 * (boardSchematicNetlist), the marks the reader has selected unselected, the exclusions
 * recorded, the old marks deleted, RunTests in mm, the marks pushed with no undo step and the
 * board not marked changed, the exclusions resolved. Writes <dir>/drc.json (the island's
 * BoardCheckResult, every finding, excluded ones flagged) then <dir>/.cc-drc.json LAST:
 * {code, error, message, lines}.
 */
void ccRunBoardCheck( const std::string& aDir, bool aFill, bool aParity, bool aAllTrackErrors )
{
    PLOT_LINES_REPORTER lines;
    int                 code = -1;
    std::string         error;
    std::string         message;

    ccEnsureDir( aDir );

    auto finish = [&]()
                  {
                      json done = json::object();
                      done["code"] = code;
                      done["error"] = error;
                      done["message"] = message;
                      done["lines"] = ccLines( lines );
                      ccWriteJson( aDir, ".cc-drc.json", done );
                  };

    PCB_EDIT_FRAME* fr = pcbFrame();   // re-resolved on the coroutine (S4)

    if( !fr || !fr->GetBoard() )
    {
        lines.Report( wxS( "no board editor to check" ), RPT_SEVERITY_ERROR );
        finish();
        return;
    }

    wxLogNull noLog;

    try
    {
        BOARD*                      board = fr->GetBoard();
        TOOL_MANAGER*               toolMgr = fr->GetToolManager();
        DRC_TOOL*                   drcTool = toolMgr->GetTool<DRC_TOOL>();
        std::shared_ptr<DRC_ENGINE> engine = drcTool ? drcTool->GetDRCEngine()
                                                     : board->GetDesignSettings().m_DRCEngine;

        try
        {
            engine->InitEngine( fr->GetDesignRulesPath() );
        }
        catch( const PARSE_ERROR& e )
        {
            error = "rules_invalid";
            message = toUtf8( e.What() );
            finish();
            return;
        }

        bool filled = false;

        if( aFill )
        {
            ZONE_FILLER_TOOL* filler = toolMgr->GetTool<ZONE_FILLER_TOOL>();
            const int         before = fr->GetUndoCommandCount();

            if( filler && !filler->IsBusy() )
            {
                filler->FillAllZones( nullptr, nullptr, /*aHeadless*/ true );
                fr->m_ZoneFillsDirty = false;
            }

            filled = fr->GetUndoCommandCount() != before;
        }

        std::string parityRun = "off";
        NETLIST     netlist;

        if( aParity )
        {
            wxFileName schematic( board->GetFileName() );
            schematic.SetExt( FILEEXT::KiCadSchematicFileExtension );

            if( !schematic.FileExists() )
            {
                parityRun = "no_schematic";
            }
            else
            {
                CC_SCH_NETLIST sch;

                if( boardSchematicNetlist( fr, netlist, sch ) )
                {
                    parityRun = "yes";
                }
                else if( sch.error == "not_annotated" )
                {
                    parityRun = "not_annotated";
                }
                else
                {
                    parityRun = "unreadable";
                    lines.Report( wxString::FromUTF8( sch.message.c_str() ), RPT_SEVERITY_WARNING );
                }
            }
        }

        // The dialog clears the selection before it deletes the marks; only a selected mark
        // would be left dangling, so the reader's selection is kept otherwise.
        if( PCB_SELECTION_TOOL* selTool = toolMgr->GetTool<PCB_SELECTION_TOOL>() )
        {
            bool markSelected = false;

            for( EDA_ITEM* item : selTool->GetSelection() )
                markSelected = markSelected || item->Type() == PCB_MARKER_T;

            if( markSelected )
                selTool->ClearSelection();
        }

        BOARD_COMMIT commit( fr );

        engine->SetDrawingSheet( fr->GetCanvas()->GetDrawingSheet() );
        engine->SetSchematicNetlist( parityRun == "yes" ? &netlist : nullptr );
        engine->SetProgressReporter( nullptr );
        engine->SetViolationHandler(
                [&]( const std::shared_ptr<DRC_ITEM>& aItem, const VECTOR2I& aPos, int aLayer,
                     const std::function<void( PCB_MARKER* )>& aPathGenerator )
                {
                    PCB_MARKER* marker = new PCB_MARKER( aItem, aPos, aLayer );
                    aPathGenerator( marker );
                    commit.Add( marker );
                } );

        board->RecordDRCExclusions();
        board->DeleteMARKERs( true, true );
        engine->RunTests( EDA_UNITS::MM, aAllTrackErrors, parityRun == "yes", &commit );
        engine->ClearViolationHandler();
        engine->SetSchematicNetlist( nullptr );   // the netlist dies with this run

        commit.Push( _( "DRC" ), SKIP_UNDO | SKIP_SET_DIRTY );
        board->ResolveDRCExclusions( false );

        const int severities = RPT_SEVERITY_ERROR | RPT_SEVERITY_WARNING | RPT_SEVERITY_EXCLUSION;
        DRC_ITEMS_PROVIDER markers( board, MARKER_BASE::MARKER_DRC, MARKER_BASE::MARKER_DRAWING_SHEET );
        DRC_ITEMS_PROVIDER ratsnest( board, MARKER_BASE::MARKER_RATSNEST );
        DRC_ITEMS_PROVIDER parity( board, MARKER_BASE::MARKER_PARITY );

        markers.SetSeverities( severities );
        ratsnest.SetSeverities( severities );
        parity.SetSeverities( severities );

        std::map<KIID, EDA_ITEM*> itemMap;
        board->FillItemMap( itemMap );

        UNITS_PROVIDER  units( pcbIUScale, EDA_UNITS::MM );
        CC_CHECK_COUNTS counts;
        json            violations = json::array();
        json            unconnected = json::array();
        json            parityList = json::array();

        ccCollectFindings( board, markers, itemMap, units, violations, counts );
        ccCollectFindings( board, ratsnest, itemMap, units, unconnected, counts );
        ccCollectFindings( board, parity, itemMap, units, parityList, counts );

        json ignored = json::array();

        for( const RC_ITEM& item : DRC_ITEM::GetItemsWithSeverities() )
        {
            const int errorCode = item.GetErrorCode();

            if( errorCode > 0 && board->GetDesignSettings().Ignore( errorCode )
                    && ignored.size() < 128 )
            {
                ignored.push_back( ccText( item.GetSettingsKey() ) );
            }
        }

        json result = json::object();
        result["violations"] = std::move( violations );
        result["unconnected"] = std::move( unconnected );
        result["parity"] = std::move( parityList );
        result["parityRun"] = parityRun;
        result["filled"] = filled;
        result["skipped"] = json::array();
        result["ignored"] = std::move( ignored );
        result["errors"] = counts.errors;
        result["warnings"] = counts.warnings;
        result["excludedCount"] = counts.excluded;
        result["depth"] = pcbjam_collab::testUndoDepth( fr );

        if( counts.truncated )
            result["truncated"] = true;

        ccWriteJson( aDir, "drc.json", result );

        // The new marks into the view (the dialog's own refresh after a run).
        fr->GetCanvas()->Refresh();
        code = 0;
    }
    catch( const std::exception& e )
    {
        lines.Report( wxString::Format( wxS( "the check failed: %s" ),
                                        wxString::FromUTF8( e.what() ) ),
                      RPT_SEVERITY_ERROR );
    }
    catch( ... )
    {
        lines.Report( wxS( "the check failed" ), RPT_SEVERITY_ERROR );
    }

    finish();
}

// ───────────────────────────── why a rule applies ─────────────────────────────

/** The constraint a DRC error code is explained by: InspectDRCError's own table. */
bool ccExplainedConstraint( int aCode, const char*& aKind, DRC_CONSTRAINT_T& aType )
{
    switch( aCode )
    {
    case DRCE_CLEARANCE:                          aKind = "clearance";            aType = CLEARANCE_CONSTRAINT;            return true;
    case DRCE_HOLE_CLEARANCE:                     aKind = "hole_clearance";       aType = HOLE_CLEARANCE_CONSTRAINT;       return true;
    case DRCE_DRILLED_HOLES_TOO_CLOSE:            aKind = "hole_to_hole";         aType = HOLE_TO_HOLE_CONSTRAINT;         return true;
    case DRCE_EDGE_CLEARANCE:                     aKind = "edge_clearance";       aType = EDGE_CLEARANCE_CONSTRAINT;       return true;
    case DRCE_TRACK_WIDTH:                        aKind = "track_width";          aType = TRACK_WIDTH_CONSTRAINT;          return true;
    case DRCE_TRACK_ANGLE:                        aKind = "track_angle";          aType = TRACK_ANGLE_CONSTRAINT;          return true;
    case DRCE_TRACK_SEGMENT_LENGTH:               aKind = "track_segment_length"; aType = TRACK_SEGMENT_LENGTH_CONSTRAINT; return true;
    case DRCE_CONNECTION_WIDTH:                   aKind = "connection_width";     aType = CONNECTION_WIDTH_CONSTRAINT;     return true;
    case DRCE_VIA_DIAMETER:                       aKind = "via_diameter";         aType = VIA_DIAMETER_CONSTRAINT;         return true;
    case DRCE_ANNULAR_WIDTH:                      aKind = "annular_width";        aType = ANNULAR_WIDTH_CONSTRAINT;        return true;
    case DRCE_DRILL_OUT_OF_RANGE:
    case DRCE_MICROVIA_DRILL_OUT_OF_RANGE:        aKind = "hole_size";            aType = HOLE_SIZE_CONSTRAINT;            return true;
    case DRCE_TEXT_HEIGHT:                        aKind = "text_height";          aType = TEXT_HEIGHT_CONSTRAINT;          return true;
    case DRCE_TEXT_THICKNESS:                     aKind = "text_thickness";       aType = TEXT_THICKNESS_CONSTRAINT;       return true;
    case DRCE_DIFF_PAIR_UNCOUPLED_LENGTH_TOO_LONG: aKind = "max_uncoupled";       aType = MAX_UNCOUPLED_CONSTRAINT;        return true;
    case DRCE_ASSERTION_FAILURE:                  aKind = "assertion";            aType = ASSERTION_CONSTRAINT;            return true;
    default:                                      return false;
    }
}

/** A connected item's effective net class name; "" for an item that has none. */
std::string ccNetclass( const BOARD_ITEM* aItem )
{
    const BOARD_CONNECTED_ITEM* connected = dynamic_cast<const BOARD_CONNECTED_ITEM*>( aItem );

    if( !connected )
        return "";

    if( NETCLASS* nc = connected->GetEffectiveNetClass() )
        return ccText( nc->GetHumanReadableName() );

    return "";
}

/**
 * Why a mark's rule applies: BOARD_INSPECTION_TOOL::InspectDRCError's evaluation, with no
 * dialog and a fresh rules engine as it builds one (makeDRCEngine): the constraint its error
 * code is explained by, evaluated for the mark's two items on the layer InspectDRCError picks
 * (the mark's own layer, else the active layer). For a clearance or a hole clearance KiCad
 * weighs the electrical and the physical constraint; the answer is the one whose minimum is
 * larger, the one that raised the mark. JSON as the island's ExplainResult; {"constraint":
 * "none"} for a code KiCad does not explain, {"constraint":"none","error":"rules_invalid"}
 * when the custom rules do not compile.
 */
std::string ccExplain( PCB_EDIT_FRAME* aFrame, PCB_MARKER* aMarker )
{
    BOARD*                   board = aFrame->GetBoard();
    std::shared_ptr<RC_ITEM> rc = aMarker->GetRCItem();
    json                     none = json::object();

    none["constraint"] = "none";

    const char*      kind = nullptr;
    DRC_CONSTRAINT_T type = NULL_CONSTRAINT;

    if( !rc || !ccExplainedConstraint( rc->GetErrorCode(), kind, type ) )
        return none.dump();

    DRC_ENGINE engine( board, &board->GetDesignSettings() );

    try
    {
        engine.InitEngine( aFrame->GetDesignRulesPath() );
    }
    catch( const PARSE_ERROR& )
    {
        none["error"] = "rules_invalid";
        return none.dump();
    }

    for( ZONE* zone : board->Zones() )
        zone->CacheBoundingBox();

    for( FOOTPRINT* footprint : board->Footprints() )
    {
        for( ZONE* zone : footprint->Zones() )
            zone->CacheBoundingBox();

        footprint->BuildCourtyardCaches();
    }

    BOARD_ITEM* a = board->ResolveItem( rc->GetMainItemID(), true );
    BOARD_ITEM* b = board->ResolveItem( rc->GetAuxItemID(), true );

    if( !a )
        return none.dump();

    const BOARD_CONNECTED_ITEM* ac = dynamic_cast<const BOARD_CONNECTED_ITEM*>( a );
    const BOARD_CONNECTED_ITEM* bc = dynamic_cast<const BOARD_CONNECTED_ITEM*>( b );
    PCB_LAYER_ID                layer = aMarker->GetLayer();

    if( layer < 0 || layer >= PCB_LAYER_ID_COUNT )
        layer = aFrame->GetActiveLayer();

    DRC_CONSTRAINT constraint;

    if( type == MAX_UNCOUPLED_CONSTRAINT )
    {
        for( const KIID& id : rc->GetIDs() )
        {
            b = board->ResolveItem( id, true );
            bc = dynamic_cast<const BOARD_CONNECTED_ITEM*>( b );

            if( ac && bc && ac->GetNetCode() != bc->GetNetCode() )
                break;
        }

        constraint = engine.EvalRules( type, a, b, layer, nullptr );
    }
    else if( type == ASSERTION_CONSTRAINT )
    {
        bool found = false;

        engine.ProcessAssertions( a,
                                  [&]( const DRC_CONSTRAINT* aFailed )
                                  {
                                      if( !found && aFailed )
                                      {
                                          constraint = *aFailed;
                                          found = true;
                                      }
                                  } );

        if( !found )
            return none.dump();
    }
    else if( type == CLEARANCE_CONSTRAINT || type == HOLE_CLEARANCE_CONSTRAINT )
    {
        if( type == CLEARANCE_CONSTRAINT && b )
        {
            // InspectDRCError's layer for a clearance: the track's, else an SMD pad's side.
            if( a->Type() == PCB_TRACE_T || a->Type() == PCB_ARC_T )
                layer = a->GetLayer();
            else if( b->Type() == PCB_TRACE_T || b->Type() == PCB_ARC_T )
                layer = b->GetLayer();
            else if( a->Type() == PCB_PAD_T
                     && static_cast<PAD*>( a )->GetAttribute() == PAD_ATTRIB::SMD )
                layer = static_cast<PAD*>( a )->IsOnLayer( F_Cu ) ? F_Cu : B_Cu;
            else if( b->Type() == PCB_PAD_T
                     && static_cast<PAD*>( b )->GetAttribute() == PAD_ATTRIB::SMD )
                layer = static_cast<PAD*>( b )->IsOnLayer( F_Cu ) ? F_Cu : B_Cu;
        }

        const DRC_CONSTRAINT_T physical = type == CLEARANCE_CONSTRAINT
                                                  ? PHYSICAL_CLEARANCE_CONSTRAINT
                                                  : PHYSICAL_HOLE_CLEARANCE_CONSTRAINT;
        const bool sameNet = ac && bc && ac->GetNetCode() == bc->GetNetCode();
        bool       have = false;

        if( !sameNet )
        {
            constraint = engine.EvalRules( type, a, b, layer, nullptr );
            have = true;
        }

        if( engine.HasRulesForConstraintType( physical ) )
        {
            DRC_CONSTRAINT phys = engine.EvalRules( physical, a, b, layer, nullptr );

            if( !have || ( phys.m_Value.HasMin() && phys.m_Value.Min() > constraint.m_Value.Min() ) )
            {
                constraint = phys;
                have = true;
            }
        }

        if( !have )
            return none.dump();
    }
    else
    {
        if( type == HOLE_TO_HOLE_CONSTRAINT )
            layer = UNDEFINED_LAYER;

        constraint = engine.EvalRules( type, a, b, layer, nullptr );
    }

    const bool degrees = type == TRACK_ANGLE_CONSTRAINT;
    auto       value = [&]( bool aHas, int aIU ) -> json
                       {
                           if( !aHas )
                               return nullptr;

                           return degrees ? json( (double) aIU ) : json( ccMm( aIU ) );
                       };

    json r = json::object();
    r["constraint"] = kind;
    r["unit"] = degrees ? "deg" : "mm";
    r["min"] = value( constraint.m_Value.HasMin(), constraint.m_Value.HasMin() ? constraint.m_Value.Min() : 0 );
    r["opt"] = value( constraint.m_Value.HasOpt(), constraint.m_Value.HasOpt() ? constraint.m_Value.Opt() : 0 );
    r["max"] = value( constraint.m_Value.HasMax(), constraint.m_Value.HasMax() ? constraint.m_Value.Max() : 0 );

    std::string source = "other";
    std::string name;
    std::string condition;

    if( const DRC_RULE* rule = constraint.GetParentRule() )
    {
        std::tie( source, name ) = ccRuleSource( rule );

        if( !rule->IsImplicit() && rule->m_Condition )
            condition = ccText( rule->m_Condition->GetExpression(), 2048 );
    }
    else
    {
        // A constraint resolved without a rule: DRC_ENGINE::EvalRules names it.
        const std::string named = toUtf8( constraint.GetName() );

        if( named == "board setup" || named == "board minimum" || named.empty() )
            source = "board";
        else if( named == "net tie" )
            source = "netTie";
        else
            source = "local";

        name = source == "local" ? ccText( constraint.GetName() ) : std::string();
    }

    r["source"] = source;
    r["name"] = source == "board" ? std::string() : name;
    r["condition"] = condition;
    r["layer"] = ccLayer( layer );

    json items = json::array();

    for( const BOARD_ITEM* item : { (const BOARD_ITEM*) a, (const BOARD_ITEM*) b } )
    {
        if( !item )
            continue;

        json entry = json::object();
        entry["uuid"] = toUtf8( item->m_Uuid.AsString() );
        entry["netclass"] = ccNetclass( item );
        items.push_back( std::move( entry ) );
    }

    r["items"] = std::move( items );
    return r.dump( -1, ' ', false, json::error_handler_t::replace );
}

// ───────────────────────────── the board item's properties ─────────────────────────────

/** Degrees in (-180, 180], 3 decimals. */
double ccDegrees( EDA_ANGLE aAngle )
{
    double deg = aAngle.Normalize180().AsDegrees();

    if( deg <= -180.0 )
        deg += 360.0;

    return ccRound( deg, 1000.0 );
}

const char* ccPadConnection( ZONE_CONNECTION aConnection )
{
    switch( aConnection )
    {
    case ZONE_CONNECTION::FULL:        return "solid";
    case ZONE_CONNECTION::NONE:        return "none";
    case ZONE_CONNECTION::THT_THERMAL: return "thtThermal";
    default:                           return "thermal";
    }
}

const char* ccHAlign( GR_TEXT_H_ALIGN_T aAlign )
{
    switch( aAlign )
    {
    case GR_TEXT_H_ALIGN_LEFT:  return "left";
    case GR_TEXT_H_ALIGN_RIGHT: return "right";
    default:                    return "center";
    }
}

/** The kind a board item answers as; "" for a kind the window does not edit. */
std::string ccEditableKind( const BOARD_ITEM* aItem )
{
    switch( aItem->Type() )
    {
    case PCB_FOOTPRINT_T: return "footprint";
    case PCB_TRACE_T:     return "track";
    case PCB_ARC_T:       return "arc";
    case PCB_VIA_T:       return "via";

    case PCB_ZONE_T:
    {
        const ZONE* zone = static_cast<const ZONE*>( aItem );
        return zone->GetIsRuleArea() || zone->GetParentFootprint() ? "" : "zone";
    }

    case PCB_TEXT_T:
        return aItem->GetParentFootprint() ? "" : "text";

    default:
        return "";
    }
}

/** The OtherSheet type of an item the window does not edit. */
const char* ccOtherType( const BOARD_ITEM* aItem )
{
    switch( aItem->Type() )
    {
    case PCB_PAD_T:              return "pad";
    case PCB_SHAPE_T:            return "shape";
    case PCB_DIM_ALIGNED_T:
    case PCB_DIM_LEADER_T:
    case PCB_DIM_CENTER_T:
    case PCB_DIM_RADIAL_T:
    case PCB_DIM_ORTHOGONAL_T:   return "dimension";
    case PCB_TEXTBOX_T:          return "textbox";
    case PCB_TABLE_T:
    case PCB_TABLECELL_T:        return "table";
    case PCB_GROUP_T:            return "group";
    case PCB_REFERENCE_IMAGE_T:  return "image";
    case PCB_BARCODE_T:          return "barcode";
    case PCB_MARKER_T:           return "marker";
    case PCB_ZONE_T:
        return static_cast<const ZONE*>( aItem )->GetIsRuleArea() ? "rulearea" : "other";
    default:                     return "other";
    }
}

/** One item's sheet in the island's closed shape (BoardItemSheet). */
json ccItemSheet( BOARD* aBoard, BOARD_ITEM* aItem )
{
    json r = json::object();
    r["uuid"] = toUtf8( aItem->m_Uuid.AsString() );

    const std::string kind = ccEditableKind( aItem );

    if( kind.empty() )
    {
        r["kind"] = "other";
        r["type"] = ccOtherType( aItem );
        return r;
    }

    r["kind"] = kind;
    r["locked"] = aItem->IsLocked();

    if( kind == "footprint" )
    {
        FOOTPRINT*     fp = static_cast<FOOTPRINT*>( aItem );
        const wxString variant = aBoard->GetCurrentVariant();

        r["ref"] = ccText( fp->GetReference() );
        r["value"] = ccText( fp->GetValue() );
        r["libId"] = ccText( fp->GetFPIDAsString() );
        r["side"] = fp->IsFlipped() ? "back" : "front";
        r["x"] = ccMm( fp->GetPosition().x );
        r["y"] = ccMm( fp->GetPosition().y );
        r["rotation"] = ccDegrees( fp->GetOrientation() );
        r["inBom"] = variant.IsEmpty() ? !fp->IsExcludedFromBOM()
                                       : !fp->GetExcludedFromBOMForVariant( variant );
        r["inPos"] = variant.IsEmpty() ? !fp->IsExcludedFromPosFiles()
                                       : !fp->GetExcludedFromPosFilesForVariant( variant );
        r["dnp"] = variant.IsEmpty() ? fp->IsDNP() : fp->GetDNPForVariant( variant );
        r["boardOnly"] = fp->IsBoardOnly();
        r["variant"] = ccText( variant );
        r["symbolPath"] = ccText( fp->GetPath().AsString() );
    }
    else if( kind == "track" || kind == "arc" )
    {
        PCB_TRACK* track = static_cast<PCB_TRACK*>( aItem );

        r["net"] = ccNet( track->GetNetname() );
        r["layer"] = ccLayer( track->GetLayer() );
        r["width"] = ccMm( track->GetWidth() );
        r["length"] = ccRound( pcbIUScale.IUTomm( 1 ) * track->GetLength(), 10000.0 );
    }
    else if( kind == "via" )
    {
        PCB_VIA*     via = static_cast<PCB_VIA*>( aItem );
        PCB_LAYER_ID top = F_Cu;
        PCB_LAYER_ID bottom = B_Cu;

        via->LayerPair( &top, &bottom );

        r["net"] = ccNet( via->GetNetname() );
        r["x"] = ccMm( via->GetPosition().x );
        r["y"] = ccMm( via->GetPosition().y );
        r["diameter"] = ccMm( via->GetWidth( top ) );
        r["drill"] = ccMm( via->GetDrillValue() );

        switch( via->GetViaType() )
        {
        case VIATYPE::BLIND:    r["viaType"] = "blind";   break;
        case VIATYPE::BURIED:   r["viaType"] = "buried";  break;
        case VIATYPE::MICROVIA: r["viaType"] = "micro";   break;
        default:                r["viaType"] = "through"; break;
        }

        r["top"] = ccLayer( top );
        r["bottom"] = ccLayer( bottom );
    }
    else if( kind == "zone" )
    {
        ZONE* zone = static_cast<ZONE*>( aItem );
        json  layers = json::array();

        for( PCB_LAYER_ID layer : zone->GetLayerSet().Seq() )
            layers.push_back( ccLayer( layer ) );

        std::vector<std::string> nets;

        for( NETINFO_ITEM* net : aBoard->GetNetInfo() )
        {
            if( net && net->GetNetCode() > 0 )
                nets.push_back( ccNet( net->GetNetname() ) );
        }

        std::sort( nets.begin(), nets.end() );
        nets.erase( std::unique( nets.begin(), nets.end() ), nets.end() );

        if( nets.size() > 4096 )
            nets.resize( 4096 );

        r["name"] = ccText( zone->GetZoneName() );
        r["net"] = ccNet( zone->GetNetname() );
        r["layers"] = std::move( layers );
        r["priority"] = (int) zone->GetAssignedPriority();
        r["clearance"] = ccMm( zone->GetLocalClearance().value_or( 0 ) );
        r["minWidth"] = ccMm( zone->GetMinThickness() );
        r["padConnection"] = ccPadConnection( zone->GetPadConnection() );
        r["thermalGap"] = ccMm( zone->GetThermalReliefGap() );
        r["spokeWidth"] = ccMm( zone->GetThermalReliefSpokeWidth() );
        r["netChoices"] = nets;
    }
    else   // text
    {
        PCB_TEXT* text = static_cast<PCB_TEXT*>( aItem );

        r["text"] = ccText( text->GetText(), 4096 );
        r["layer"] = ccLayer( text->GetLayer() );
        r["x"] = ccMm( text->GetTextPos().x );
        r["y"] = ccMm( text->GetTextPos().y );
        r["rotation"] = ccDegrees( text->GetTextAngle() );
        r["height"] = ccMm( text->GetTextHeight() );
        r["width"] = ccMm( text->GetTextWidth() );
        r["thickness"] = ccMm( text->GetTextThickness() );
        r["bold"] = text->IsBold();
        r["italic"] = text->IsItalic();
        r["knockout"] = text->IsKnockout();
        r["mirrored"] = text->IsMirrored();
        r["hAlign"] = ccHAlign( text->GetHorizJustify() );
    }

    return r;
}

/** A plain one-line string: no control character. */
bool ccPlain( const std::string& aText )
{
    for( unsigned char c : aText )
    {
        if( c < 0x20 || c == 0x7f )
            return false;
    }

    return true;
}

/** Text of a board text: line breaks and tabs allowed, no other control character. */
bool ccTextBody( const std::string& aText )
{
    for( unsigned char c : aText )
    {
        if( ( c < 0x20 && c != '\n' && c != '\t' ) || c == 0x7f )
            return false;
    }

    return true;
}

/** The board's net a sheet's (unescaped) net name names. */
NETINFO_ITEM* ccFindNet( BOARD* aBoard, const std::string& aName )
{
    if( NETINFO_ITEM* net = aBoard->FindNet( wxString::FromUTF8( aName.c_str() ) ) )
        return net;

    for( NETINFO_ITEM* net : aBoard->GetNetInfo() )
    {
        if( net && net->GetNetCode() > 0 && ccNet( net->GetNetname() ) == aName )
            return net;
    }

    return nullptr;
}

/** One set, read whole against the item before anything is queued. */
struct CC_ITEM_SET
{
    std::string  key;
    double       number = 0;
    bool         flag = false;
    std::string  text;
    PCB_LAYER_ID layer = UNDEFINED_LAYER;
};

/**
 * The set's one key is a key of the item's kind and its value in bounds; false otherwise.
 * Positions at most 50,000 mm in magnitude, sizes 0.001 to 1,000 mm (a via's hole under its
 * diameter), rotation -360 to 360 degrees, a layer the board enables (copper for a track),
 * a zone's net one the board has, a designator and value only for a footprint with no symbol.
 */
bool ccReadItemSet( BOARD* aBoard, BOARD_ITEM* aItem, const json& aSet, CC_ITEM_SET& aOut )
{
    if( !aSet.is_object() || aSet.size() != 1 )
        return false;

    const std::string kind = ccEditableKind( aItem );

    if( kind.empty() )
        return false;

    const std::string& key = aSet.begin().key();
    const json&        v = aSet.begin().value();

    static const std::map<std::string, std::set<std::string>> keys = {
        { "footprint", { "x", "y", "rotation", "locked", "side", "inBom", "inPos", "dnp", "ref", "value" } },
        { "track",     { "width", "layer", "locked" } },
        { "arc",       { "width", "layer", "locked" } },
        { "via",       { "x", "y", "locked", "diameter", "drill" } },
        { "zone",      { "name", "net", "priority", "clearance", "minWidth", "padConnection",
                         "thermalGap", "spokeWidth", "locked" } },
        { "text",      { "text", "layer", "x", "y", "rotation", "height", "width", "thickness",
                         "bold", "italic", "knockout", "hAlign", "locked" } },
    };

    if( !keys.at( kind ).count( key ) )
        return false;

    aOut.key = key;

    auto number = [&]( double aMin, double aMax ) -> bool
                  {
                      if( !v.is_number() )
                          return false;

                      const double d = v.get<double>();

                      if( !std::isfinite( d ) || d < aMin || d > aMax )
                          return false;

                      aOut.number = d;
                      return true;
                  };

    if( key == "x" || key == "y" )
        return number( -50000.0, 50000.0 );

    if( key == "rotation" )
        return number( -360.0, 360.0 );

    if( key == "width" || key == "height" || key == "thickness" || key == "minWidth"
            || key == "thermalGap" || key == "spokeWidth" )
        return number( 0.001, 1000.0 );

    if( key == "clearance" )
        return number( 0.0, 1000.0 );

    if( key == "diameter" || key == "drill" )
    {
        if( !number( 0.001, 1000.0 ) )
            return false;

        PCB_VIA* via = static_cast<PCB_VIA*>( aItem );
        const int iu = pcbIUScale.mmToIU( aOut.number );

        return key == "drill" ? iu < via->GetWidth( F_Cu ) : via->GetDrillValue() < iu;
    }

    if( key == "priority" )
    {
        if( !v.is_number_integer() )
            return false;

        const long long p = v.get<long long>();

        if( p < 0 || p > 1000000 )
            return false;

        aOut.number = (double) p;
        return true;
    }

    if( key == "locked" || key == "inBom" || key == "inPos" || key == "dnp" || key == "bold"
            || key == "italic" || key == "knockout" )
    {
        if( !v.is_boolean() )
            return false;

        aOut.flag = v.get<bool>();
        return true;
    }

    if( !v.is_string() )
        return false;

    aOut.text = v.get<std::string>();

    if( key == "side" )
        return aOut.text == "front" || aOut.text == "back";

    if( key == "hAlign" )
        return aOut.text == "left" || aOut.text == "center" || aOut.text == "right";

    if( key == "padConnection" )
        return aOut.text == "thermal" || aOut.text == "solid" || aOut.text == "none"
               || aOut.text == "thtThermal";

    if( key == "layer" )
    {
        aOut.layer = ccLayerByName( aBoard, aOut.text );

        if( aOut.layer == UNDEFINED_LAYER )
            return false;

        return kind == "text" || IsCopperLayer( aOut.layer );
    }

    if( key == "ref" || key == "value" )
    {
        if( !static_cast<FOOTPRINT*>( aItem )->IsBoardOnly() || !ccPlain( aOut.text ) )
            return false;

        return key == "ref" ? !aOut.text.empty() && aOut.text.size() <= 64
                            : aOut.text.size() <= 1024;
    }

    if( key == "name" )
        return aOut.text.size() <= 1024 && ccPlain( aOut.text );

    if( key == "net" )
        return ccFindNet( aBoard, aOut.text ) != nullptr;

    if( key == "text" )
        return aOut.text.size() <= 4096 && ccTextBody( aOut.text );

    return false;
}

/** The set applied to the item, inside the caller's commit (Modify already taken). */
void ccApplyItemSet( PCB_EDIT_FRAME* aFrame, BOARD_ITEM* aItem, const CC_ITEM_SET& aSet )
{
    BOARD*            board = aFrame->GetBoard();
    const std::string kind = ccEditableKind( aItem );
    const std::string& key = aSet.key;
    const int         iu = pcbIUScale.mmToIU( aSet.number );

    if( key == "locked" )
    {
        aItem->SetLocked( aSet.flag );
        return;
    }

    if( kind == "footprint" )
    {
        FOOTPRINT*     fp = static_cast<FOOTPRINT*>( aItem );
        const wxString variantName = board->GetCurrentVariant();

        auto variant = [&]() -> FOOTPRINT_VARIANT*
                       {
                           FOOTPRINT_VARIANT* v = fp->GetVariant( variantName );
                           return v ? v : fp->AddVariant( variantName );
                       };

        if( key == "x" )
            fp->SetPosition( VECTOR2I( iu, fp->GetPosition().y ) );
        else if( key == "y" )
            fp->SetPosition( VECTOR2I( fp->GetPosition().x, iu ) );
        else if( key == "rotation" )
            fp->SetOrientation( EDA_ANGLE( aSet.number, DEGREES_T ).Normalize180() );
        else if( key == "side" && ( aSet.text == "back" ) != fp->IsFlipped() )
            fp->Flip( fp->GetPosition(), aFrame->GetPcbNewSettings()->m_FlipDirection );
        else if( key == "ref" )
            fp->SetReference( wxString::FromUTF8( aSet.text.c_str() ) );
        else if( key == "value" )
            fp->SetValue( wxString::FromUTF8( aSet.text.c_str() ) );
        else if( key == "inBom" || key == "inPos" || key == "dnp" )
        {
            // Under a variant the three flags are the variant's, as KiCad's properties panel
            // writes them (PCB_PROPERTIES_PANEL::valueChanged).
            if( !variantName.IsEmpty() )
            {
                if( FOOTPRINT_VARIANT* v = variant() )
                {
                    if( key == "inBom" )
                        v->SetExcludedFromBOM( !aSet.flag );
                    else if( key == "inPos" )
                        v->SetExcludedFromPosFiles( !aSet.flag );
                    else
                        v->SetDNP( aSet.flag );
                }
            }
            else if( key == "inBom" )
                fp->SetExcludedFromBOM( !aSet.flag );
            else if( key == "inPos" )
                fp->SetExcludedFromPosFiles( !aSet.flag );
            else
                fp->SetDNP( aSet.flag );
        }

        return;
    }

    if( kind == "track" || kind == "arc" )
    {
        PCB_TRACK* track = static_cast<PCB_TRACK*>( aItem );

        if( key == "width" )
            track->SetWidth( iu );
        else if( key == "layer" )
            track->SetLayer( aSet.layer );

        return;
    }

    if( kind == "via" )
    {
        PCB_VIA* via = static_cast<PCB_VIA*>( aItem );

        if( key == "x" )
            via->SetPosition( VECTOR2I( iu, via->GetPosition().y ) );
        else if( key == "y" )
            via->SetPosition( VECTOR2I( via->GetPosition().x, iu ) );
        else if( key == "diameter" )
            via->SetWidth( iu );
        else if( key == "drill" )
            via->SetDrill( iu );

        return;
    }

    if( kind == "zone" )
    {
        ZONE* zone = static_cast<ZONE*>( aItem );

        if( key == "name" )
            zone->SetZoneName( wxString::FromUTF8( aSet.text.c_str() ) );
        else if( key == "net" )
        {
            if( NETINFO_ITEM* net = ccFindNet( board, aSet.text ) )
                zone->SetNet( net );
        }
        else if( key == "priority" )
            zone->SetAssignedPriority( (unsigned) aSet.number );
        else if( key == "clearance" )
            zone->SetLocalClearance( std::optional<int>( iu ) );
        else if( key == "minWidth" )
            zone->SetMinThickness( iu );
        else if( key == "thermalGap" )
            zone->SetThermalReliefGap( iu );
        else if( key == "spokeWidth" )
            zone->SetThermalReliefSpokeWidth( iu );
        else if( key == "padConnection" )
        {
            zone->SetPadConnection( aSet.text == "solid"        ? ZONE_CONNECTION::FULL
                                    : aSet.text == "none"       ? ZONE_CONNECTION::NONE
                                    : aSet.text == "thtThermal" ? ZONE_CONNECTION::THT_THERMAL
                                                                : ZONE_CONNECTION::THERMAL );
        }

        return;
    }

    if( kind == "text" )
    {
        PCB_TEXT* text = static_cast<PCB_TEXT*>( aItem );

        if( key == "text" )
            text->SetText( wxString::FromUTF8( aSet.text.c_str() ) );
        else if( key == "layer" )
        {
            text->SetLayer( aSet.layer );
            text->SetMirrored( IsBackLayer( aSet.layer ) );   // mirrored follows the side
        }
        else if( key == "x" )
            text->SetTextPos( VECTOR2I( iu, text->GetTextPos().y ) );
        else if( key == "y" )
            text->SetTextPos( VECTOR2I( text->GetTextPos().x, iu ) );
        else if( key == "rotation" )
            text->SetTextAngle( EDA_ANGLE( aSet.number, DEGREES_T ).Normalize180() );
        else if( key == "height" )
            text->SetTextHeight( iu );
        else if( key == "width" )
            text->SetTextWidth( iu );
        else if( key == "thickness" )
            text->SetTextThickness( iu );
        else if( key == "bold" )
            text->SetBold( aSet.flag );
        else if( key == "italic" )
            text->SetItalic( aSet.flag );
        else if( key == "knockout" )
            text->SetIsKnockout( aSet.flag );
        else if( key == "hAlign" )
        {
            text->SetHorizJustify( aSet.text == "left"    ? GR_TEXT_H_ALIGN_LEFT
                                   : aSet.text == "right" ? GR_TEXT_H_ALIGN_RIGHT
                                                          : GR_TEXT_H_ALIGN_CENTER );
        }
    }
}
} // namespace

// ───────────────────────────── the exports ─────────────────────────────

// board.preview / board.update (cap boardUpdate). aArgsJson exactly {"run": int >= 0,
// "dir": "/tmp/cc-update/<n>", "apply": bool, "replaceFootprints": bool, "deleteExtra": bool,
// "relinkByReference": bool, "token": string} ("" on a preview, 1 to 128 characters on an
// apply). False (nothing queued) for any other JSON, with no board, or while an open is in
// flight; true once the run is queued on the apply coroutine (the host waits for the apply
// queue to empty and <dir>/.cc-update.json).
bool pcbUpdateBoardFromSchematic( std::string aArgsJson )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 7 )
        return false;

    CC_UPDATE_ARGS a;

    auto flag = [&]( const char* aKey, bool& aOut ) -> bool
                {
                    auto it = args.find( aKey );

                    if( it == args.end() || !it->is_boolean() )
                        return false;

                    aOut = it->get<bool>();
                    return true;
                };

    auto run = args.find( "run" );
    auto dir = args.find( "dir" );
    auto token = args.find( "token" );

    if( run == args.end() || !run->is_number_integer() || run->get<long long>() < 0
            || dir == args.end() || !dir->is_string() || token == args.end()
            || !token->is_string() || !flag( "apply", a.apply )
            || !flag( "replaceFootprints", a.replaceFootprints )
            || !flag( "deleteExtra", a.deleteExtra )
            || !flag( "relinkByReference", a.relinkByReference ) )
    {
        return false;
    }

    a.run = run->get<long long>();
    a.dir = dir->get<std::string>();
    a.token = token->get<std::string>();

    if( !ccIsRunDir( a.dir, "/tmp/cc-update/" ) )
        return false;

    if( a.apply ? ( a.token.empty() || a.token.size() > 128 ) : !a.token.empty() )
        return false;

    pcbjam_collab::runOnCoroutine( fr, [a]() { ccRunBoardUpdate( a ); } );
    return true;
}

// board.check (cap drc). aArgsJson exactly {"run": int >= 0, "dir": "/tmp/cc-drc/<n>",
// "fill": bool, "parity": bool, "allTrackErrors": bool}. False (nothing queued) for any
// other JSON, with no board, or while an open is in flight; true once queued (the host waits
// for the apply queue to empty and <dir>/.cc-drc.json).
bool pcbCheckBoard( std::string aArgsJson )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 5 )
        return false;

    auto run = args.find( "run" );
    auto dir = args.find( "dir" );
    auto fill = args.find( "fill" );
    auto parity = args.find( "parity" );
    auto all = args.find( "allTrackErrors" );

    if( run == args.end() || !run->is_number_integer() || run->get<long long>() < 0
            || dir == args.end() || !dir->is_string() || fill == args.end()
            || !fill->is_boolean() || parity == args.end() || !parity->is_boolean()
            || all == args.end() || !all->is_boolean() )
    {
        return false;
    }

    const std::string d = dir->get<std::string>();

    if( !ccIsRunDir( d, "/tmp/cc-drc/" ) )
        return false;

    const bool withFill = fill->get<bool>();
    const bool withParity = parity->get<bool>();
    const bool withAll = all->get<bool>();

    pcbjam_collab::runOnCoroutine( fr, [d, withFill, withParity, withAll]() {
        ccRunBoardCheck( d, withFill, withParity, withAll );
    } );

    return true;
}

// board.explain (cap explain): why the mark aMarkerUuid's rule applies, as the island's
// ExplainResult JSON; "" when the board has no such mark (or no board, or an open in
// flight). A read: synchronous, no coroutine, nothing changed.
std::string pcbExplainViolation( std::string aMarkerUuid )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() || !ccIsLowerUuid( aMarkerUuid ) )
        return "";

    for( PCB_MARKER* marker : fr->GetBoard()->Markers() )
    {
        if( marker && toUtf8( marker->m_Uuid.AsString() ) == aMarkerUuid )
        {
            try
            {
                return ccExplain( fr, marker );
            }
            catch( ... )
            {
                return "";
            }
        }
    }

    return "";
}

// board.item.get (cap boardItem): aUuidsJson a JSON array of 1 to 512 lowercase uuids;
// answers {"items": [...]}, each uuid the board holds as the island's BoardItemSheet in the
// order asked (a field or a text of a footprint answers its footprint, once); a uuid the
// board does not hold is left out. "" for args of any other shape. A read: synchronous.
std::string pcbBoardItemGet( std::string aUuidsJson )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return "";

    json uuids = json::parse( aUuidsJson, nullptr, /*allow_exceptions*/ false );

    if( uuids.is_discarded() || !uuids.is_array() || uuids.empty() || uuids.size() > 512 )
        return "";

    for( const json& u : uuids )
    {
        if( !u.is_string() || !ccIsLowerUuid( u.get<std::string>() ) )
            return "";
    }

    BOARD*                board = fr->GetBoard();
    json                  items = json::array();
    std::set<std::string> answered;

    for( const json& u : uuids )
    {
        BOARD_ITEM* item = ccResolve( board, u.get<std::string>() );

        if( !item )
            continue;

        if( ( item->Type() == PCB_FIELD_T || item->Type() == PCB_TEXT_T )
                && item->GetParentFootprint() )
        {
            item = item->GetParentFootprint();
        }

        const std::string uuid = toUtf8( item->m_Uuid.AsString() );

        if( !answered.insert( uuid ).second )
            continue;

        items.push_back( ccItemSheet( board, item ) );
    }

    json out = json::object();
    out["items"] = std::move( items );
    return out.dump( -1, ' ', false, json::error_handler_t::replace );
}

// board.item.set (cap boardItem): aArgsJson exactly {"uuid": string, "set": {one key}}, the
// set read whole against the item first (ccReadItemSet). False (nothing queued) for anything
// else; true once ONE commit is queued on the apply coroutine, the item re-resolved there:
// BOARD_COMMIT Modify, the change through the item's own setters (the ones KiCad's properties
// panel and dialogs call; a side change is KiCad's Flip with the editor's flip direction),
// Push "Edit properties": one undo step.
bool pcbBoardItemSet( std::string aArgsJson )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 2 )
        return false;

    auto uuid = args.find( "uuid" );
    auto set = args.find( "set" );

    if( uuid == args.end() || !uuid->is_string() || set == args.end() )
        return false;

    const std::string id = uuid->get<std::string>();

    if( !ccIsLowerUuid( id ) )
        return false;

    BOARD_ITEM* item = ccResolve( fr->GetBoard(), id );
    CC_ITEM_SET parsed;

    if( !item || !ccReadItemSet( fr->GetBoard(), item, *set, parsed ) )
        return false;

    pcbjam_collab::runOnCoroutine( fr, [fr, id, parsed]() {   // re-resolve on the coroutine (S4)
        BOARD_ITEM* live = ccResolve( fr->GetBoard(), id );

        if( !live || ccEditableKind( live ).empty() )
            return;

        BOARD_COMMIT commit( fr );
        commit.Modify( live );
        ccApplyItemSet( fr, live, parsed );
        commit.Push( _( "Edit properties" ) );
    } );

    return true;
}

// ════════════════════════ Circuit Center, engine run 14 (outputs and rules) ════════════════════════
//
// The board half of the island's project.export (cap export) and board.rules (cap rules), from
// the outputs spec (2026-11-16): kicadExportBoardOutput runs ONE board output, a KiCad job by its
// registry name with KiCad's own settings JSON (6.2's merge, cc_output_common.h) or one of our
// runners (cc_*) calling the exporter beneath KiCad's dialog, into /tmp/cc-out/<n>, its record
// <dir>/.cc-export.json {code, lines, dropped} written LAST; kicadResolveRules answers KiCad's
// Constraints Resolution (one item) and Clearance Resolution (two items) as records, a read.
// Nothing in the board changes: no commit, no undo step, the modified flag as it was; no KiCad
// window opens on any of these roads (wxLogNull keeps wx's log window shut too).

// KiCad's free functions beneath the board editor's Export dialogs, declared here: specctra.h
// includes generated lexer headers outside the bindings' include roots, and RecreateCmpFile has
// no header (pcbnew/exporters/export_footprint_associations.cpp).
namespace DSN
{
void ExportBoardToSpecctraFile( BOARD* aBoard, const wxString& aFullFilename );
}

bool RecreateCmpFile( BOARD* aBrd, const wxString& aFullCmpFileName );

namespace
{
/** One board output, read and checked before anything is queued. */
struct CC_BOARD_OUTPUT
{
    std::string              dir;
    std::string              job;
    std::shared_ptr<JOB>     kicadJob;   // null for ours
    cc_output::OURS_SETTINGS ours;
    cc_output::MERGE_RESULT  merge;
};

/** <dir>/<the board's file name>.<ext>, KiCad's own naming for a board output. */
wxString ccOutputFile( const std::string& aDir, BOARD* aBoard, const wxString& aExt )
{
    const wxString stem = wxFileName( aBoard->GetFileName() ).GetName();
    return wxFileName( wxString::FromUTF8( aDir.c_str() ), stem, aExt ).GetFullPath();
}

/**
 * Ours (the outputs spec's 6.3): each the body of KiCad's own menu command after its dialog,
 * with no box. Answers the record's code: 0 made, 1 KiCad's exporter refused (its line said).
 */
int ccRunOurBoardOutput( PCB_EDIT_FRAME* aFrame, BOARD* aBoard, const CC_BOARD_OUTPUT& aArgs,
                         cc_output::LINES_REPORTER& aLines )
{
    const std::string& job = aArgs.job;

    auto cannotCreate =
            [&]( const wxString& aPath ) -> int
            {
                aLines.Report( wxString::Format( _( "Failed to create file '%s'." ), aPath ),
                               RPT_SEVERITY_ERROR );
                return 1;
            };

    if( job == "cc_footprint_report" )
    {
        // BOARD_EDITOR_CONTROL::GenFootprintsReport after its folder dialog.
        const wxString path = ccOutputFile( aArgs.dir, aBoard, wxS( "rpt" ) );
        PLACE_FILE_EXPORTER exporter( aBoard, aArgs.ours.units == "mm",
                                      false,        // aOnlySMD
                                      false,        // aNoTHItems
                                      false,        // aExcludeDNP
                                      false,        // aExcludeBOM
                                      true, true,   // aTopSide, aBottomSide
                                      false,        // aFormatCSV
                                      true,         // aUseAuxOrigin
                                      false );      // aNegateBottomX

        exporter.SetVariant( aBoard->GetCurrentVariant() );

        const std::string data = exporter.GenReportData();
        FILE*             f = std::fopen( toUtf8( path ).c_str(), "wt" );

        if( !f )
            return cannotCreate( path );

        std::fputs( data.c_str(), f );
        std::fclose( f );
        return 0;
    }

    if( job == "cc_idf" )
    {
        // DIALOG_EXPORT_IDF3's call with the origin at 0, 0 (absolute), its boxes reported
        // (kicad cc/main K-O1).
        const wxString path = ccOutputFile( aArgs.dir, aBoard, wxS( "emn" ) );

        return aFrame->Export_IDF3( aBoard, path, aArgs.ours.units == "thou", 0.0, 0.0,
                                    aArgs.ours.includeUnspecified, aArgs.ours.includeDnp, &aLines )
                       ? 0 : 1;
    }

    if( job == "cc_specctra_dsn" )
    {
        // PCB_EDIT_FRAME::ExportSpecctraFile without its error box: the exporter's own line.
        // Its two footprint flips set the modified flag and cancel out, so the flag is put
        // back as KiCad's wrapper puts it back.
        const wxString path = ccOutputFile( aArgs.dir, aBoard, wxS( "dsn" ) );
        BASE_SCREEN*   screen = aFrame->GetScreen();
        const bool     wasModified = screen && screen->IsContentModified();
        int            code = 0;

        try
        {
            DSN::ExportBoardToSpecctraFile( aBoard, path );
        }
        catch( const IO_ERROR& ioe )
        {
            aLines.Report( ioe.What(), RPT_SEVERITY_ERROR );
            code = 1;
        }

        if( screen && !wasModified )
            screen->SetContentModified( false );

        if( code != 0 && wxFileName::FileExists( path ) )
            wxRemoveFile( path );   // a half written file is no output

        return code;
    }

    if( job == "cc_hyperlynx" )
    {
        // BOARD_EDITOR_CONTROL::ExportHyperlynx after its file dialog (kicad cc/main K-O2).
        const wxString path = ccOutputFile( aArgs.dir, aBoard, wxS( "hyp" ) );

        if( !ExportBoardToHyperlynx( aBoard, wxFileName( path ) ) )
        {
            if( wxFileName::FileExists( path ) )
                wxRemoveFile( path );

            return cannotCreate( path );
        }

        return 0;
    }

    if( job == "cc_footprint_associations" )
    {
        // BOARD_EDITOR_CONTROL::ExportCmpFile after its file dialog.
        const wxString path = ccOutputFile( aArgs.dir, aBoard, wxS( "cmp" ) );

        return RecreateCmpFile( aBoard, path ) ? 0 : cannotCreate( path );
    }

    if( job == "cc_footprints" )
    {
        // PCB_EDIT_FRAME::ExportFootprintsToLibrary without its library chooser, its boxes and
        // any library table: each footprint copied and reset as KiCad resets it, written by
        // the KiCad S-expression writer into <board>.pretty/. The folder is made first (a
        // save into a folder that is missing asks in a box). Two footprints of one name: the
        // first is kept and the rest are named in one warning line.
        const wxString   stem = wxFileName( aBoard->GetFileName() ).GetName();
        wxFileName       lib;
        lib.AssignDir( wxString::FromUTF8( aArgs.dir.c_str() ) );
        lib.AppendDir( stem + wxS( ".pretty" ) );
        const wxString   libPath = lib.GetPath();
        PCB_IO_KICAD_SEXPR io;

        io.CreateLibrary( libPath );   // throws IO_ERROR, an error line in the caller

        std::set<wxString> saved;
        wxArrayString      twins;

        for( FOOTPRINT* footprint : aBoard->Footprints() )
        {
            if( footprint->GetFPID().GetLibItemName().empty() )   // as KiCad: old boards
                continue;

            const wxString name = footprint->GetFPID().GetUniStringLibItemName();

            if( !saved.insert( name ).second )
            {
                if( twins.Index( name ) == wxNOT_FOUND )
                    twins.Add( name );

                continue;
            }

            std::unique_ptr<FOOTPRINT> copy(
                    static_cast<FOOTPRINT*>( footprint->Duplicate( IGNORE_PARENT_GROUP ) ) );

            // Reset reference designator, group membership, and zone offset before saving.
            copy->SetReference( wxS( "REF**" ) );
            copy->SetParentGroup( nullptr );

            for( ZONE* zone : copy->Zones() )
                zone->Move( -copy->GetPosition() );

            io.FootprintSave( libPath, copy.get() );
        }

        if( !twins.IsEmpty() )
        {
            wxString list;

            for( size_t i = 0; i < twins.GetCount(); ++i )
                list += ( i ? wxS( ", " ) : wxString() ) + twins[i];

            aLines.Report( wxString::Format( wxS( "More than one footprint is named %s; the "
                                                  "first of each was kept." ),
                                             list ),
                           RPT_SEVERITY_WARNING );
        }

        return 0;
    }

    aLines.Report( wxS( "this engine does not run that output" ), RPT_SEVERITY_ERROR );
    return -1;
}

/** One board output, on the apply coroutine; the record last. */
void ccRunBoardOutput( const CC_BOARD_OUTPUT& aArgs )
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

    PCB_EDIT_FRAME* fr = pcbFrame();   // re-resolved on the coroutine (S4)
    BOARD*          brd = fr ? fr->GetBoard() : nullptr;

    if( !brd || !fr->Kiway().KiFACE( KIWAY::FACE_PCB, false ) )
    {
        lines.Report( wxS( "no board editor to export" ), RPT_SEVERITY_ERROR );
        finish();
        return;
    }

    if( aArgs.kicadJob && ( !Pgm().IsGUI() || !Pgm().GetSettingsManager().IsProjectOpen() ) )
    {
        // getBoard would load the saved file instead of the editor's board (and keep it).
        lines.Report( wxS( "the job handler would not export the editor's board" ),
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

    wxLogNull    noLog;
    BASE_SCREEN* screen = fr->GetScreen();
    const bool   wasModified = screen && screen->IsContentModified();

    try
    {
        if( aArgs.kicadJob )
        {
            // As KiCad's jobset runner (kicad/jobs_runner.cpp): the run folder is the job's
            // temporary output directory and its own output path stays empty, so the job's
            // handler names the files as kicad-cli jobset run names them.
            aArgs.kicadJob->SetTempOutputDirectory( wxString::FromUTF8( aArgs.dir.c_str() ) );
            code = fr->Kiway().ProcessJob( KIWAY::FACE_PCB, aArgs.kicadJob.get(), &lines );
        }
        else
        {
            code = ccRunOurBoardOutput( fr, brd, aArgs, lines );
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

    // An output never marks the board changed.
    if( screen && !wasModified && screen->IsContentModified() )
        screen->SetContentModified( false );

    // The VRML output: on the first run 14 build KiCad's exporter answered success on Glasgow
    // and wrote only the 16 byte "#VRML V2.0 utf8" header (kicad-cli 10.0.4 writes 18 MB from
    // the same board). Until that is found, a header-only file is a failed output with its own
    // line, never a file the page offers.
    if( code == 0 && aArgs.job == "pcb_export_3d" )
    {
        wxFileName wrl( ccOutputFile( aArgs.dir, brd, wxS( "wrl" ) ) );

        if( !wrl.FileExists() || wrl.GetSize() <= 64 )
        {
            if( wrl.FileExists() )
                wxRemoveFile( wrl.GetFullPath() );

            lines.Report( wxS( "The VRML exporter wrote no board (an empty scene)." ),
                          RPT_SEVERITY_ERROR );
            code = -1;
        }
    }

    finish();
}

// ───────────────────────────── the rules for an item and a pair ─────────────────────────────

/** KiCad's rules engine with the assertions ProcessAssertions tests, read one by one. */
class CC_RULES_ENGINE : public DRC_ENGINE
{
public:
    CC_RULES_ENGINE( BOARD* aBoard, BOARD_DESIGN_SETTINGS* aSettings ) :
            DRC_ENGINE( aBoard, aSettings )
    {
    }

    /**
     * Each assertion DRC_ENGINE::ProcessAssertions tests on aItem, in its order, with its
     * verdict: an unconditional rule or one whose condition holds (a rule whose layer does not
     * match is still tested, as KiCad tests it).
     */
    void ForEachAssertion( const BOARD_ITEM* aItem,
                           const std::function<void( const DRC_CONSTRAINT&, bool )>& aVisit )
    {
        auto it = m_constraintMap.find( ASSERTION_CONSTRAINT );

        if( it == m_constraintMap.end() || !it->second )
            return;

        for( const auto* c : *it->second )
        {
            if( !c || !c->constraint.m_Test )
                continue;

            const bool applies = !c->condition || c->condition->GetExpression().IsEmpty()
                                 || c->condition->EvaluateFor( aItem, nullptr, c->constraint.m_Type,
                                                               aItem->GetLayer(), nullptr );

            if( !applies )
                continue;

            const bool pass = c->constraint.m_Test->EvaluateFor( aItem, nullptr, c->constraint.m_Type,
                                                                 aItem->GetLayer(), nullptr );
            aVisit( c->constraint, pass );
        }
    }
};

/** Where a resolved constraint came from, its rule's name and its rule's condition. */
void ccConstraintOrigin( const DRC_CONSTRAINT& aConstraint, std::string& aSource, std::string& aName,
                         std::string& aCondition )
{
    aSource = "other";
    aName.clear();
    aCondition.clear();

    if( const DRC_RULE* rule = aConstraint.GetParentRule() )
    {
        std::tie( aSource, aName ) = ccRuleSource( rule );

        if( !rule->IsImplicit() && rule->m_Condition )
            aCondition = ccText( rule->m_Condition->GetExpression(), 2048 );
    }
    else
    {
        // A constraint resolved without a rule: DRC_ENGINE::EvalRules names it.
        const std::string named = toUtf8( aConstraint.GetName() );

        if( named == "board setup" || named == "board minimum" || named.empty() )
            aSource = "board";
        else if( named == "net tie" )
            aSource = "netTie";
        else
            aSource = "local";

        aName = aSource == "local" ? ccText( aConstraint.GetName() ) : std::string();
    }

    if( aSource == "board" )
        aName.clear();
}

/** The records of one resolution, in KiCad's report order, at most 128. */
struct CC_RULES
{
    json        rules = json::array();
    json        notes = json::array();
    BOARD_ITEM* a = nullptr;
    BOARD_ITEM* b = nullptr;

    json items() const
    {
        json list = json::array();

        for( const BOARD_ITEM* item : { (const BOARD_ITEM*) a, (const BOARD_ITEM*) b } )
        {
            if( !item )
                continue;

            json entry = json::object();
            entry["uuid"] = toUtf8( item->m_Uuid.AsString() );
            entry["netclass"] = ccNetclass( item );
            list.push_back( std::move( entry ) );
        }

        return list;
    }

    /** aUnit "mm", "deg", "percent", "count" or "none" (no values). */
    void add( const char* aConstraint, const char* aUnit, const DRC_CONSTRAINT& aValue,
              const std::string& aVerdict, PCB_LAYER_ID aLayer )
    {
        if( rules.size() >= 128 )
            return;

        const std::string unit = aUnit;

        auto value = [&]( bool aHas, int aIU ) -> json
                     {
                         if( !aHas || unit == "none" )
                             return nullptr;

                         if( unit == "mm" )
                             return ccMm( aIU );

                         if( unit == "percent" )   // KiCad keeps the ratio in thousandths
                             return ccRound( aIU / 10.0, 100.0 );

                         return json( aIU );       // deg, count
                     };

        const MINOPTMAX<int>& v = aValue.m_Value;

        json r = json::object();
        r["constraint"] = aConstraint;
        r["unit"] = unit;
        r["min"] = value( v.HasMin(), v.HasMin() ? v.Min() : 0 );
        r["opt"] = value( v.HasOpt(), v.HasOpt() ? v.Opt() : 0 );
        r["max"] = value( v.HasMax(), v.HasMax() ? v.Max() : 0 );
        r["verdict"] = aVerdict;

        std::string source, name, condition;
        ccConstraintOrigin( aValue, source, name, condition );

        r["source"] = source;
        r["name"] = name;
        r["condition"] = condition;
        r["layer"] = ccLayer( aLayer );
        r["items"] = items();
        rules.push_back( std::move( r ) );
    }

    /** Two items on one net: no clearance applies (KiCad's "Min clearance is 0."). */
    void addSameNet( PCB_LAYER_ID aLayer )
    {
        if( rules.size() >= 128 )
            return;

        json r = json::object();
        r["constraint"] = "clearance";
        r["unit"] = "mm";
        r["min"] = 0.0;
        r["opt"] = nullptr;
        r["max"] = nullptr;
        r["verdict"] = "";
        r["source"] = "sameNet";
        r["name"] = "";
        r["condition"] = "";
        r["layer"] = ccLayer( aLayer );
        r["items"] = items();
        rules.push_back( std::move( r ) );
    }

    void note( const wxString& aText )
    {
        if( notes.size() < 32 )
            notes.push_back( ccText( aText ) );
    }
};

const char* ccZoneConnectionWord( ZONE_CONNECTION aConnection )
{
    switch( aConnection )
    {
    case ZONE_CONNECTION::FULL:        return "solid";
    case ZONE_CONNECTION::THERMAL:     return "thermal";
    case ZONE_CONNECTION::THT_THERMAL: return "thermal_tht";
    case ZONE_CONNECTION::NONE:        return "none";
    default:                           return "";
    }
}

/** BOARD_INSPECTION_TOOL::InspectConstraints, its pages as records. */
void ccInspectConstraints( CC_RULES_ENGINE& aEngine, BOARD_ITEM* aItem, bool aCourtyardError,
                           CC_RULES& aOut )
{
    DRC_CONSTRAINT c;

    if( aItem->Type() == PCB_TRACE_T )
    {
        c = aEngine.EvalRules( TRACK_WIDTH_CONSTRAINT, aItem, nullptr, aItem->GetLayer(), nullptr );
        aOut.add( "track_width", "mm", c, "", aItem->GetLayer() );
    }

    if( aItem->Type() == PCB_VIA_T )
    {
        c = aEngine.EvalRules( VIA_DIAMETER_CONSTRAINT, aItem, nullptr, UNDEFINED_LAYER, nullptr );
        aOut.add( "via_diameter", "mm", c, "", UNDEFINED_LAYER );

        c = aEngine.EvalRules( ANNULAR_WIDTH_CONSTRAINT, aItem, nullptr, UNDEFINED_LAYER, nullptr );
        aOut.add( "annular_width", "mm", c, "", UNDEFINED_LAYER );
    }

    if( ( aItem->Type() == PCB_PAD_T && static_cast<PAD*>( aItem )->GetDrillSize().x > 0 )
            || aItem->Type() == PCB_VIA_T )
    {
        c = aEngine.EvalRules( HOLE_SIZE_CONSTRAINT, aItem, nullptr, UNDEFINED_LAYER, nullptr );
        aOut.add( "hole_size", "mm", c, "", UNDEFINED_LAYER );
    }

    if( aItem->Type() == PCB_PAD_T || aItem->Type() == PCB_SHAPE_T
            || dynamic_cast<PCB_TRACK*>( aItem ) )
    {
        c = aEngine.EvalRules( SOLDER_MASK_EXPANSION_CONSTRAINT, aItem, nullptr, UNDEFINED_LAYER,
                               nullptr );
        aOut.add( "solder_mask_expansion", "mm", c, "", UNDEFINED_LAYER );
    }

    if( aItem->Type() == PCB_PAD_T )
    {
        c = aEngine.EvalRules( SOLDER_PASTE_ABS_MARGIN_CONSTRAINT, aItem, nullptr, UNDEFINED_LAYER,
                               nullptr );
        aOut.add( "solder_paste_abs_margin", "mm", c, "", UNDEFINED_LAYER );

        c = aEngine.EvalRules( SOLDER_PASTE_REL_MARGIN_CONSTRAINT, aItem, nullptr, UNDEFINED_LAYER,
                               nullptr );
        aOut.add( "solder_paste_rel_margin", "percent", c, "", UNDEFINED_LAYER );
    }

    if( aItem->Type() == PCB_FIELD_T || aItem->Type() == PCB_TEXT_T
            || aItem->Type() == PCB_TEXTBOX_T )
    {
        c = aEngine.EvalRules( TEXT_HEIGHT_CONSTRAINT, aItem, nullptr, UNDEFINED_LAYER, nullptr );
        aOut.add( "text_height", "mm", c, "", UNDEFINED_LAYER );

        c = aEngine.EvalRules( TEXT_THICKNESS_CONSTRAINT, aItem, nullptr, UNDEFINED_LAYER, nullptr );
        aOut.add( "text_thickness", "mm", c, "", UNDEFINED_LAYER );
    }

    c = aEngine.EvalRules( DISALLOW_CONSTRAINT, aItem, nullptr, aItem->GetLayer(), nullptr );
    aOut.add( "disallow", "none", c, c.m_DisallowFlags ? "disallowed" : "allowed",
              aItem->GetLayer() );

    if( aCourtyardError )
        aOut.note( _( "Report may be incomplete: some footprint courtyards are malformed." ) );

    aEngine.ForEachAssertion( aItem,
                              [&]( const DRC_CONSTRAINT& aAssertion, bool aPass )
                              {
                                  aOut.add( "assertion", "none", aAssertion, aPass ? "pass" : "fail",
                                            aItem->GetLayer() );
                              } );
}

/** BOARD_INSPECTION_TOOL::reportClearance, its pages as records. */
void ccReportClearance( PCB_EDIT_FRAME* aFrame, CC_RULES_ENGINE& aEngine, BOARD_ITEM* aItemA,
                        BOARD_ITEM* aItemB, CC_RULES& aOut )
{
    BOARD_ITEM* a = aItemA;
    BOARD_ITEM* b = aItemB;

    // A group is its first item, as KiCad reports it.
    auto firstOfGroup = [&]( BOARD_ITEM*& aItem ) -> bool
                        {
                            if( aItem->Type() != PCB_GROUP_T )
                                return true;

                            PCB_GROUP* group = static_cast<PCB_GROUP*>( aItem );

                            if( group->GetItems().empty() )
                                return false;

                            aItem = static_cast<BOARD_ITEM*>( *group->GetItems().begin() );
                            return aItem != nullptr;
                        };

    if( !firstOfGroup( a ) || !firstOfGroup( b ) )
    {
        aOut.note( _( "Cannot generate clearance report on empty group." ) );
        return;
    }

    // A footprint is its pad when all its pads are one logical pad.
    auto checkFootprint = []( FOOTPRINT* footprint ) -> BOARD_ITEM*
                          {
                              PAD* foundPad = nullptr;

                              for( PAD* pad : footprint->Pads() )
                              {
                                  if( !foundPad || pad->SameLogicalPadAs( foundPad ) )
                                      foundPad = pad;
                                  else
                                      return footprint;
                              }

                              if( !foundPad )
                                  return footprint;

                              return foundPad;
                          };

    if( a->Type() == PCB_FOOTPRINT_T )
        a = checkFootprint( static_cast<FOOTPRINT*>( a ) );

    if( b->Type() == PCB_FOOTPRINT_T )
        b = checkFootprint( static_cast<FOOTPRINT*>( b ) );

    if( !a || !b )
        return;

    if( a->Type() != PCB_ZONE_T && b->Type() == PCB_ZONE_T )
        std::swap( a, b );
    else if( !a->IsConnected() && b->IsConnected() )
        std::swap( a, b );

    aOut.a = a;
    aOut.b = b;

    const PCB_LAYER_ID    active = aFrame->GetActiveLayer();
    const LSET            layerIntersection = a->GetLayerSet() & b->GetLayerSet();
    const LSET            copperIntersection = layerIntersection & LSET::AllCuMask();
    BOARD_CONNECTED_ITEM* ac = dynamic_cast<BOARD_CONNECTED_ITEM*>( a );
    BOARD_CONNECTED_ITEM* bc = dynamic_cast<BOARD_CONNECTED_ITEM*>( b );
    ZONE*                 zone = dynamic_cast<ZONE*>( a );
    PAD*                  pad = dynamic_cast<PAD*>( b );
    FOOTPRINT*            aFP = dynamic_cast<FOOTPRINT*>( a );
    FOOTPRINT*            bFP = dynamic_cast<FOOTPRINT*>( b );
    DRC_CONSTRAINT        c;

    if( copperIntersection.any() && zone && pad && zone->GetNetCode() == pad->GetNetCode() )
    {
        PCB_LAYER_ID layer = UNDEFINED_LAYER;

        if( zone->IsOnLayer( active ) )
            layer = active;
        else if( zone->GetLayerSet().count() > 0 )
            layer = zone->GetLayerSet().Seq().front();

        c = aEngine.EvalZoneConnection( pad, zone, layer, nullptr );
        const ZONE_CONNECTION connection = c.m_ZoneConnection;
        aOut.add( "zone_connection", "none", c, ccZoneConnectionWord( connection ), layer );

        if( connection == ZONE_CONNECTION::THERMAL )
        {
            c = aEngine.EvalRules( THERMAL_RELIEF_GAP_CONSTRAINT, pad, zone, layer, nullptr );
            aOut.add( "thermal_relief_gap", "mm", c, "", layer );

            c = aEngine.EvalRules( THERMAL_SPOKE_WIDTH_CONSTRAINT, pad, zone, layer, nullptr );
            aOut.add( "thermal_spoke_width", "mm", c, "", layer );

            c = aEngine.EvalRules( MIN_RESOLVED_SPOKES_CONSTRAINT, pad, zone, layer, nullptr );
            aOut.add( "min_resolved_spokes", "count", c, "", layer );
        }
        else if( connection == ZONE_CONNECTION::NONE )
        {
            // The zone's own clearance, then a larger physical clearance or physical hole
            // clearance in its place, as KiCad resolves it.
            DRC_CONSTRAINT zoneClearance( CLEARANCE_CONSTRAINT, _( "zone" ) );
            zoneClearance.m_Value.SetMin( zone->GetLocalClearance().value_or( 0 ) );
            DRC_CONSTRAINT resolved = zoneClearance;

            c = aEngine.EvalRules( PHYSICAL_CLEARANCE_CONSTRAINT, pad, zone, layer, nullptr );

            if( c.m_Value.HasMin() && c.m_Value.Min() > resolved.m_Value.Min() )
                resolved = c;

            if( !pad->FlashLayer( layer ) )
            {
                c = aEngine.EvalRules( PHYSICAL_HOLE_CLEARANCE_CONSTRAINT, pad, zone, layer, nullptr );

                if( c.m_Value.HasMin() && c.m_Value.Min() > resolved.m_Value.Min() )
                    resolved = c;
            }

            aOut.add( "clearance", "mm", resolved, "", layer );
        }
        else
        {
            aOut.addSameNet( layer );
        }
    }
    else if( copperIntersection.any() && !aFP && !bFP )
    {
        const bool sameNet = ac && bc && ac->GetNetCode() > 0 && ac->GetNetCode() == bc->GetNetCode();
        std::vector<PCB_LAYER_ID> layers;

        if( copperIntersection.test( active ) )
            layers.push_back( active );

        for( PCB_LAYER_ID layer : copperIntersection.Seq() )
        {
            if( layer != active )
                layers.push_back( layer );
        }

        for( PCB_LAYER_ID layer : layers )
        {
            if( sameNet )
            {
                aOut.addSameNet( layer );
                continue;
            }

            c = aEngine.EvalRules( CLEARANCE_CONSTRAINT, a, b, layer, nullptr );

            if( c.IsNull() )   // KiCad: "Min clearance is 0."
            {
                DRC_CONSTRAINT none( CLEARANCE_CONSTRAINT );
                none.m_Value.SetMin( 0 );
                aOut.add( "clearance", "mm", none, "", layer );
            }
            else
            {
                aOut.add( "clearance", "mm", c, "", layer );
            }
        }
    }

    if( ac && bc && ac->GetNet() )
    {
        wxString coupledNet;
        wxString dummy;

        if( DRC_ENGINE::MatchDpSuffix( ac->GetNet()->GetNetname(), coupledNet, dummy )
                && bc->GetNetname() == coupledNet )
        {
            const LSET   dpIntersection = ac->GetLayerSet() & bc->GetLayerSet() & LSET::AllCuMask();
            PCB_LAYER_ID dpLayer = active;

            if( !dpIntersection.test( dpLayer ) && dpIntersection.any() )
                dpLayer = dpIntersection.Seq().front();

            c = aEngine.EvalRules( DIFF_PAIR_GAP_CONSTRAINT, ac, bc, dpLayer, nullptr );
            aOut.add( "diff_pair_gap", "mm", c, "", dpLayer );

            if( aEngine.HasRulesForConstraintType( MAX_UNCOUPLED_CONSTRAINT ) )
            {
                c = aEngine.EvalRules( MAX_UNCOUPLED_CONSTRAINT, ac, bc, dpLayer, nullptr );
                aOut.add( "max_uncoupled", "mm", c, "", dpLayer );
            }
            else
            {
                aOut.note( _( "No 'diff_pair_uncoupled' constraints defined." ) );
            }
        }
    }

    auto isOnCorrespondingLayer =
            [&]( BOARD_ITEM* aItem, PCB_LAYER_ID aLayer, bool* aTented ) -> bool
            {
                if( aItem->IsOnLayer( aLayer ) )
                    return true;

                const PCB_LAYER_ID mask = IsFrontLayer( aLayer ) ? F_Mask : B_Mask;
                const PCB_LAYER_ID copper = IsFrontLayer( aLayer ) ? F_Cu : B_Cu;

                if( aItem->IsOnLayer( mask ) )
                    return true;

                if( aItem->IsTented( mask ) && aItem->IsOnLayer( copper ) )
                {
                    *aTented = true;
                    return true;
                }

                return false;
            };

    for( PCB_LAYER_ID layer : { F_SilkS, B_SilkS } )
    {
        bool tentedA = false;
        bool tentedB = false;

        if( ( a->IsOnLayer( layer ) && isOnCorrespondingLayer( b, layer, &tentedB ) )
                || ( b->IsOnLayer( layer ) && isOnCorrespondingLayer( a, layer, &tentedA ) ) )
        {
            c = aEngine.EvalRules( SILK_CLEARANCE_CONSTRAINT, a, b, layer, nullptr );
            aOut.add( "silk_clearance", "mm", c, "", layer );

            if( tentedA || tentedB )
            {
                aOut.note( wxString::Format( _( "Note: %s is tented; clearance will only be "
                                                "applied to holes." ),
                                             ( tentedB ? b : a )->GetItemDescription( aFrame, true ) ) );
            }
        }
    }

    for( PCB_LAYER_ID layer : { F_CrtYd, B_CrtYd } )
    {
        const bool aCourtyard = aFP && !aFP->GetCourtyard( layer ).IsEmpty();
        const bool bCourtyard = bFP && !bFP->GetCourtyard( layer ).IsEmpty();

        if( aCourtyard && bCourtyard )
        {
            c = aEngine.EvalRules( COURTYARD_CLEARANCE_CONSTRAINT, a, b, layer, nullptr );
            aOut.add( "courtyard_clearance", "mm", c, "", layer );
        }
    }

    if( a->HasHole() || b->HasHole() )
    {
        PCB_LAYER_ID layer = UNDEFINED_LAYER;

        if( a->HasHole() && b->IsOnLayer( active ) && IsCopperLayer( active ) )
            layer = active;
        else if( b->HasHole() && a->IsOnLayer( active ) && IsCopperLayer( active ) )
            layer = active;
        else if( a->HasHole() && b->IsOnCopperLayer() )
            layer = b->GetLayer();
        else if( b->HasHole() && a->IsOnCopperLayer() )
            layer = a->GetLayer();

        if( layer >= 0 )
        {
            c = aEngine.EvalRules( HOLE_CLEARANCE_CONSTRAINT, a, b, layer, nullptr );
            aOut.add( "hole_clearance", "mm", c, "", layer );
        }

        if( a->HasDrilledHole() || b->HasDrilledHole() )
        {
            c = aEngine.EvalRules( HOLE_TO_HOLE_CONSTRAINT, a, b, UNDEFINED_LAYER, nullptr );
            aOut.add( "hole_to_hole", "mm", c, "", UNDEFINED_LAYER );
        }
    }

    for( PCB_LAYER_ID edgeLayer : { Edge_Cuts, Margin } )
    {
        PCB_LAYER_ID layer = UNDEFINED_LAYER;

        if( a->IsOnLayer( edgeLayer ) && b->Type() != PCB_FOOTPRINT_T )
        {
            if( b->IsOnLayer( active ) && IsCopperLayer( active ) )
                layer = active;
            else if( IsCopperLayer( b->GetLayer() ) )
                layer = b->GetLayer();
        }
        else if( b->IsOnLayer( edgeLayer ) && a->Type() != PCB_FOOTPRINT_T )
        {
            if( a->IsOnLayer( active ) && IsCopperLayer( active ) )
                layer = active;
            else if( IsCopperLayer( a->GetLayer() ) )
                layer = a->GetLayer();
        }

        if( layer >= 0 )
        {
            c = aEngine.EvalRules( EDGE_CLEARANCE_CONSTRAINT, a, b, layer, nullptr );
            aOut.add( "edge_clearance", "mm", c, "", layer );
        }
    }

    if( aEngine.HasRulesForConstraintType( PHYSICAL_CLEARANCE_CONSTRAINT ) )
    {
        LSET reportLayers = layerIntersection;

        if( a->IsOnLayer( Edge_Cuts ) )
            reportLayers |= bFP ? LSET( { F_CrtYd, B_CrtYd } )
                                : LSET( b->GetLayerSet() & LSET::PhysicalLayersMask() );

        if( b->IsOnLayer( Edge_Cuts ) )
            reportLayers |= aFP ? LSET( { F_CrtYd, B_CrtYd } )
                                : LSET( a->GetLayerSet() & LSET::PhysicalLayersMask() );

        for( PCB_LAYER_ID layer : reportLayers )
        {
            c = aEngine.EvalRules( PHYSICAL_CLEARANCE_CONSTRAINT, a, b, layer, nullptr );

            if( !c.IsNull() )
                aOut.add( "physical_clearance", "mm", c, "", layer );
        }
    }

    if( ( a->HasHole() || b->HasHole() )
            && aEngine.HasRulesForConstraintType( PHYSICAL_HOLE_CLEARANCE_CONSTRAINT ) )
    {
        PCB_LAYER_ID layer;

        if( a->HasHole() && b->IsOnLayer( active ) )
            layer = active;
        else if( b->HasHole() && a->IsOnLayer( active ) )
            layer = active;
        else if( a->HasHole() )
            layer = b->GetLayer();
        else
            layer = a->GetLayer();

        c = aEngine.EvalRules( PHYSICAL_HOLE_CLEARANCE_CONSTRAINT, a, b, layer, nullptr );
        aOut.add( "physical_hole_clearance", "mm", c, "", layer );
    }
}

/** The resolution, as the island's board.rules reads it; never throws. */
std::string ccResolveRules( PCB_EDIT_FRAME* aFrame, const std::string& aA, const std::string& aB )
{
    BOARD* board = aFrame->GetBoard();
    json   out = json::object();

    out["rules"] = json::array();
    out["notes"] = json::array();
    out["error"] = "";

    BOARD_ITEM* a = ccResolve( board, aA );
    BOARD_ITEM* b = aB.empty() ? nullptr : ccResolve( board, aB );

    if( !a || ( !aB.empty() && !b ) )
    {
        out["error"] = "not_found";
        return out.dump( -1, ' ', false, json::error_handler_t::replace );
    }

    // BOARD_INSPECTION_TOOL::makeDRCEngine, with no dialog: the custom rules compiled first
    // (a parse error ends the answer, as the Why panel's), the zone and courtyard caches built.
    CC_RULES_ENGINE engine( board, &board->GetDesignSettings() );

    try
    {
        engine.InitEngine( aFrame->GetDesignRulesPath() );
    }
    catch( const PARSE_ERROR& )
    {
        out["error"] = "rules_invalid";
        return out.dump( -1, ' ', false, json::error_handler_t::replace );
    }

    bool courtyardError = false;

    for( ZONE* zone : board->Zones() )
        zone->CacheBoundingBox();

    for( FOOTPRINT* footprint : board->Footprints() )
    {
        for( ZONE* zone : footprint->Zones() )
            zone->CacheBoundingBox();

        footprint->BuildCourtyardCaches();

        if( ( footprint->GetFlags() & MALFORMED_COURTYARDS ) != 0 )
            courtyardError = true;
    }

    CC_RULES rules;

    if( !b )
    {
        rules.a = a;
        ccInspectConstraints( engine, a, courtyardError, rules );
    }
    else
    {
        ccReportClearance( aFrame, engine, a, b, rules );
    }

    out["rules"] = std::move( rules.rules );
    out["notes"] = std::move( rules.notes );
    return out.dump( -1, ' ', false, json::error_handler_t::replace );
}
} // namespace

// project.export, a board output (cap export). aArgsJson exactly {"run": int >= 0, "dir":
// "/tmp/cc-out/<n>", "job": one of cc_output::BoardJobs(), "settings": object}. A KiCad job's
// settings are KiCad's own job JSON (a jobset's), merged by the outputs spec's 6.2 (unknown keys
// dropped and named, a known key of another JSON type refused); ours take their closed keys only.
// False (nothing queued) for anything else, with no board, or while an open is in flight; true
// once the run is queued on the apply coroutine (the host waits for the apply queue to empty and
// <dir>/.cc-export.json {"code", "lines": [{severity, text}], "dropped": [key]}).
bool pcbExportBoardOutput( std::string aArgsJson )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 4 )
        return false;

    auto run = args.find( "run" );
    auto dir = args.find( "dir" );
    auto job = args.find( "job" );
    auto settings = args.find( "settings" );

    if( run == args.end() || !run->is_number_integer() || run->get<long long>() < 0
            || dir == args.end() || !dir->is_string() || job == args.end() || !job->is_string()
            || settings == args.end() || !settings->is_object() )
    {
        return false;
    }

    CC_BOARD_OUTPUT a;
    a.dir = dir->get<std::string>();
    a.job = job->get<std::string>();

    if( !cc_output::IsRunDir( a.dir, cc_output::OutputRoot() )
            || !cc_output::Lists( cc_output::BoardJobs(), a.job ) )
    {
        return false;
    }

    if( cc_output::IsOurs( a.job ) )
    {
        if( !cc_output::ReadOurs( a.job, *settings, a.ours ) )
            return false;
    }
    else
    {
        const wxString name = wxString::FromUTF8( a.job.c_str() );

        if( JOB_REGISTRY::GetKifaceType( name ) != KIWAY::FACE_PCB )
            return false;

        a.kicadJob.reset( JOB_REGISTRY::CreateInstance<JOB>( name ) );

        if( !a.kicadJob || !cc_output::MergeSettings( *a.kicadJob, a.job, *settings, a.merge ) )
            return false;
    }

    pcbjam_collab::runOnCoroutine( fr, [a]() { ccRunBoardOutput( a ); } );
    return true;
}

// board.rules (cap rules): KiCad's Constraints Resolution for one board item and Clearance
// Resolution for two. aArgsJson exactly {"a": uuid, "b": uuid or ""}, lowercase 8-4-4-4-12, b
// not a. Answers {"rules": [RuleRecord], "notes": [string], "error": "" | "rules_invalid" |
// "not_found"}; "" for args of another shape, with no board, or while an open is in flight. A
// read: synchronous, no coroutine, nothing changed.
std::string pcbResolveRules( std::string aArgsJson )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return "";

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 2 )
        return "";

    auto a = args.find( "a" );
    auto b = args.find( "b" );

    if( a == args.end() || !a->is_string() || b == args.end() || !b->is_string() )
        return "";

    const std::string first = a->get<std::string>();
    const std::string second = b->get<std::string>();

    if( !ccIsLowerUuid( first ) || ( !second.empty() && ( !ccIsLowerUuid( second ) || second == first ) ) )
        return "";

    try
    {
        return ccResolveRules( fr, first, second );
    }
    catch( ... )
    {
        return "";
    }
}

// Circuit Center (engine run 14): properties, numbering and rules (every footprint's row, the
// eleven global board edits, Change and Update Footprints, the settings sections), in their
// own file.
#include "cc_rules_pcb.inc"

// Circuit Center (engine run 15): each author lane's board face, in its own file, namespace
// (cc_eN) and bindings block (EMSCRIPTEN_BINDINGS( cc_eN_pcb )), outside any
// KICAD_MERGED_EMBIND guard. Placed once by the seams commit; no lane edits these lines.
#include "cc_e1_pcb.inc"
#include "cc_e2_pcb.inc"
#include "cc_e3_pcb.inc"
#include "cc_e5_pcb.inc"
#include "cc_e6_pcb.inc"
#include "cc_e7_pcb.inc"
#include "cc_e8_pcb.inc"
#include "cc_e9_pcb.inc"
#include "cc_e10_pcb.inc"
#include "cc_e11_pcb.inc"
#include "cc_e12_pcb.inc"
#include "cc_e13_pcb.inc"

EMSCRIPTEN_BINDINGS(pcbnew) {
    // Register vector types for iteration
    register_vector<FOOTPRINT*>("FootprintVector");
    register_vector<PAD*>("PadVector");

    // Helper functions that operate on pointers
    // Note: GetBoard() not available - will be added when Pyodide integration is done
    function("Board_GetFootprints", &Board_GetFootprints, allow_raw_pointers());
    function("Board_GetFileName", &Board_GetFileName, allow_raw_pointers());
    function("Footprint_GetPads", &Footprint_GetPads, allow_raw_pointers());
    function("Footprint_GetReference", &Footprint_GetReference, allow_raw_pointers());
    function("Footprint_GetValue", &Footprint_GetValue, allow_raw_pointers());
    function("Pad_GetNumber", &Pad_GetNumber, allow_raw_pointers());
    function("Pad_GetPinFunction", &Pad_GetPinFunction, allow_raw_pointers());

    // Programmatic save of the in-memory board (round-trip tests, README §A).
    function("kicadSaveBoard", &kicadSaveBoard);
#ifndef KICAD_MERGED_EMBIND
    // Placed-footprint usage + update-from-library (libs 0017 §2c/2d).
    function("kicadLibsFootprintUsage", &pcbLibsFootprintUsage);
    function("kicadUpdateFromLibrary", &pcbUpdateFromLibraryShim);
    function("kicadLibsTestPreload", &pcbLibsTestPreload);
    function("kicadLibsTestLoadFootprint", &pcbLibsTestLoadFootprint);
    function("kicadLibsTestEditorFootprint", &pcbLibsTestEditorFootprint);
    function("kicadLibsTestEditorLoad", &pcbLibsTestEditorLoad);
#endif
    // Layer bridge (viewer-panels) — pcbnew-only names, merged-image safe
    // (null-frame no-op when eeschema is the live frame).
    function("kicadLayersGetState", &pcbLayersGetState);
    function("kicadLayersSetVisible", &pcbLayersSetVisible);
    function("kicadLayersSetActive", &pcbLayersSetActive);
    // Session-menu 3D entry (read-only-viewer 0003) — pcbnew-only name,
    // merged-image safe (false when no board frame is up).
    function("kicadShow3DViewer", &pcbShow3DViewer);
    // pcbnew-only test helper (no eeschema counterpart — name is not shared).
    function("kicadCollabTestItemBlob", &kicadCollabTestItemBlob);
    // pcbnew-only ysync-review repro hooks (names not shared with eeschema).
    function("kicadCollabTestSetPadSize", &pcbCollabTestSetPadSize);
    function("kicadCollabTestMoveEndpoint", &pcbCollabTestMoveEndpoint);
    // drift-trio phase B action hooks (tool-unique names, merged-image safe).
    function("kicadCollabTestAddTrack", &pcbCollabTestAddTrack);
    function("kicadCollabTestAddVia", &pcbCollabTestAddVia);
    function("kicadCollabTestAddBoardText", &pcbCollabTestAddBoardText);
    function("kicadCollabTestAddZone", &pcbCollabTestAddZone);
    function("kicadCollabTestFlipBoardItem", &pcbCollabTestFlipBoardItem);
    function("kicadCollabTestSetFootprintField", &pcbCollabTestSetFootprintField);
    function("kicadCollabTestSetBoardItemLocked", &pcbCollabTestSetBoardItemLocked);
    function("kicadCollabTestMoveBoardItem", &pcbCollabTestMoveBoardItem);
    function("kicadCollabTestDuplicateBoardItem", &pcbCollabTestDuplicateBoardItem);
    // Circuit Center (island cc12): a footprint's 3D models as one undo step (footprint.models).
    function("kicadFootprintSetModels", &pcbFootprintSetModels);
    // Circuit Center (island cc13): the board's Gerbers and drill files, no dialog (project.plot).
    function("kicadPlotBoard", &pcbPlotBoard);
    // Circuit Center (island cc18, engine run 12): Update the board (board.preview, board.update),
    // the board check (board.check), why a mark's rule applies (board.explain) and the
    // selected board item's properties (board.item.get, board.item.set); no dialog on any.
    function("kicadUpdateBoardFromSchematic", &pcbUpdateBoardFromSchematic);
    function("kicadCheckBoard", &pcbCheckBoard);
    function("kicadExplainViolation", &pcbExplainViolation);
    function("kicadBoardItemGet", &pcbBoardItemGet);
    function("kicadBoardItemSet", &pcbBoardItemSet);
    // Circuit Center (island cc23, engine run 14): one board output through KiCad's own job or
    // exporter, no dialog (project.export), and the rules for an item or a pair (board.rules).
    function("kicadExportBoardOutput", &pcbExportBoardOutput);
    function("kicadResolveRules", &pcbResolveRules);
    // Circuit Center (island cc23, engine run 14): every footprint's row (board.footprints), the
    // global board edits (board.edit) and Change and Update Footprints (footprints.exchange).
    function("kicadBoardFootprints", &pcbBoardFootprints);
    function("kicadBoardEdit", &pcbBoardEdit);
    function("kicadFootprintsExchange", &pcbFootprintsExchange);

#ifndef KICAD_MERGED_EMBIND
    // JS names ALSO registered by eeschema_embind.cpp — in the merged image these are
    // registered once by kicad_editor_embind.cpp, dispatching on the active frame.
    // Programmatic file open (preferred over UI automation from the web app).
    function("kicadOpenFile", &kicadOpenFile PCBJAM_PARKER_POLICY);
    function("kicadOpenFileBusy", &kicadOpenFileBusy);
    function("kicadTestSetOpenPark", &kicadTestSetOpenPark);
    function("kicadTestArmTimerPark", &kicadTestArmTimerPark);
    function("kicadTestTimerParkState", &kicadTestTimerParkState);
    function("kicadCollabBusy", &kicadCollabBusyProbe);
    // Read-only viewer lock (read-only-viewer).
    function("kicadSetReadOnly", &kicadSetReadOnly);
    // Yjs collaborative bridge entry points (same contract as pl_editor / eeschema).
    function("kicadCollabApply", &pcbCollabApply);
    function("kicadCollabSnapshot", &pcbCollabSnapshot);
    // v2 items bridge: per-item s-expr payloads (ysync 0008).
    function("kicadCollabApplyItems", &pcbCollabApplyItems);
    function("kicadPlaceImportedItem", &pcbPlaceImportedItem);
    function("kicadCollabSnapshotItems", &pcbCollabSnapshotItems);
    function("kicadCollabTestMoveFirst", &pcbCollabTestMoveFirst);
    function("kicadCollabGetPos", &pcbCollabGetPos);
    // ysync-review repro hooks shared with eeschema (dispatched when merged).
    function("kicadCollabTestRemoveItem", &pcbCollabTestRemoveItem);
    function("kicadCollabTestUndo", &pcbCollabTestUndo);
    function("kicadCollabTestUndoDepth", &pcbCollabTestUndoDepth);
    function("kicadCollabTestRotateItem", &pcbCollabTestRotateItem);
    // Presence (collab-presence 0002) — shared names; eeschema's counterparts land
    // with 0003 (the merged image dispatches pcb-only until then).
    function("kicadCollabPresenceStart", &pcbCollabPresenceStart);
    function("kicadCollabSetRemote", &pcbCollabSetRemote);
    function("kicadCollabSetRemoteCursors", &pcbCollabSetRemoteCursors);
    function("kicadCollabSetPins", &pcbCollabSetPins);
    function("kicadCollabSetViewport", &pcbCollabSetViewport);
    // Follow-user (collab-presence 0008).
    function("kicadCollabFitViewport", &pcbCollabFitViewport);
    function("kicadCollabSetStyle", &pcbCollabSetStyle);
    // Live color-theme switch (comments-ux 0002 F4).
    function("kicadSetColorTheme", &pcbSetColorTheme);
    function("kicadSetDarkChrome", &pcbSetDarkChrome);
    function("kicadCollabTestListItems", &pcbCollabTestListItems);
    function("kicadCollabTestDemoSet", &pcbCollabTestDemoSet);
    function("kicadCollabGetViewport", &pcbCollabGetViewport);
    function("kicadCollabGetSelection", &pcbCollabGetSelection);
    // Cross-app selection (0006).
    function("kicadCollabGetSelectionFull", &pcbCollabGetSelectionFull);
    function("kicadCollabTestGetCrossMapped", &pcbCollabTestGetCrossMapped);
    // Selection soft-locks (0007).
    function("kicadCollabReleaseSelection", &pcbCollabReleaseSelection);
    function("kicadCollabTestGetLocked", &pcbCollabTestGetLocked);
    function("kicadCollabTestSelectFirst", &pcbCollabTestSelectFirst);
    function("kicadCollabTestSelectComponent", &pcbCollabTestSelectComponent);
    function("kicadCollabTestSelectByUuid", &pcbCollabTestSelectByUuid);
    function("kicadCollabTestClearSelection", &pcbCollabTestClearSelection);
    // Library reload after a remote (synced) lib edit — r2-idb-sync realtime.
    function("kicadLibsReload", &pcbjam_libs::reloadLibrary PCBJAM_PARKER_POLICY);
    // Runtime lib-table row insert + load (a new team library appeared
    // mid-session; the lib set is otherwise frozen at boot).
    function("kicadLibsAddEntry", &pcbjam_libs::addLibraryEntry PCBJAM_PARKER_POLICY);
    // Settings sections (engine run 14); the merged image dispatches these two names.
    function("kicadSettingsGet", &pcbSettingsGet);
    function("kicadSettingsSet", &pcbSettingsSet);
#endif // !KICAD_MERGED_EMBIND
}
#endif
