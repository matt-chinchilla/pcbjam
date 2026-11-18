<!-- Copyright (C) 2026 Circuit Center Inc -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Engine run 15, lane e12: rules and settings sections

Lane e12 owns `cc_rules_pcb.inc` and `cc_rules_sch.inc` after the seams and writes its own faces
`cc_e12_pcb.inc` (block `cc_e12_pcb`), `cc_e12_sch.inc` (block `cc_e12_sch`) and
`cc_e12_dispatch.inc` (block `cc_e12_dispatch`). Every new section registers through E0's R7
registry from those incs (`ccRegisterPcbSection`, `ccRegisterSchSection`); no built-in list or set
chain was edited for a new section. Units in the lane's order: lo-e3s-e, st-routing-e,
st-origins-e, st-editprefs-e, m7-e, m8-e. This file also holds the section lo-e3s-e's manifest
names `BUILD-NOTES-run15-board-faults.md` (written here, as the guide's section 2 says).

Worktrees: pcbjam `/home/matthew/cc-engine-wt/e12/pcbjam`, kicad `/home/matthew/cc-engine-wt/e12/kicad`,
both on `seat/run15-e12`, merged forward to `run15/main` (pcbjam 9f3a5d9, kicad b49e4476) before the
first unit. No uncommitted work and no commits were found from an earlier seat of this lane.

Shared rules of every section below: a get answers a JSON object (the export answers "" with no
frame or an open in flight, as run 14); a set is the run 14 `kicadSettingsSet` envelope
`{run, dir: "/tmp/cc-settings/<n>", section, value, dryRun?}`, its value read whole and refused
(`bad_args`) on any key or kind the section does not name; no undo step (KiCad's rule for
settings); record `settings.json` `{value}` then `.cc-settings.json` last. None of this lane's
code sits under `KICAD_MERGED_EMBIND`: every section exists in the merged image and in
occ_service's compile of `pcbnew_embind.cpp` (the `occ` unit passes).

## lo-e4-e: net names from the board onto the schematic

Commits: kicad KLO (`backannotate.h`, `backannotate.cpp`), pcbjam PLO (`cc_e8_pcb.inc`, `cc_e8_sch.inc`).

**KiCad (under `#ifdef __EMSCRIPTEN__`):** `BACK_ANNOTATE` gains `CC_PAD_NET`, `CC_NET_ROW`, `CcProcessNetNames( SCH_COMMIT*, pads )` and two members (`m_ccLastLabel`, `m_ccLastOutcome`). `CcProcessNetNames` is applyChangelist's net name loop (a pin's `Connection( &sheetPath )->Name( true )` against the pad's net, `processNetNameChange` when they differ) over the host's pads, symbols matched by reference (`GetSymbols( SYMBOL_FILTER_NON_POWER )`, each unit tried until one holds the pin). `processNetNameChange` records its outcome (`rename` with the label's uuid, `add`, `refused` for a power pin) beside its own lines. **Header change:** `eeschema/tools/backannotate.h` (class `BACK_ANNOTATE`) is included only by `backannotate.cpp` and `dialogs/dialog_update_from_pcb.cpp`: two files recompile.

**PCBJam:**
- `kicadBoardPadNets()` in `cc_e8_pcb.inc`, block `cc_e8_pcb` (pcbnew TU; not merged only, it calls only the board): `{"pads": [{ref, pad, net}], "truncated"}`, the board's `MAIL_PCB_GET_NETLIST` reply's facts (board only footprints left out, `GetShortNetname`, pads with no net left out), at most 32,768. A read.
- `kicadSchRenameNets( std::string )` in `cc_e8_sch.inc`: `{run, dir: "/tmp/cc-renameNets/<n>", preview, pads (1 to 32,768)}`. On the coroutine: `RecalculateConnections( NO_CLEANUP )`, a `BACK_ANNOTATE` with net names alone (no relink, no other property, no swaps), `aDryRun` = preview; the rows; ONE commit "Update net names" when not a preview. Record `renameNets.json` `{preview, rows: [{ref, pin, from, to, sheetPath, outcome, label?}], applied, truncated, depth}` last. `outcome` is ADDED beyond the manifest's row (for lo-e4-i).

## st-routing-e: sections router and layerPairs

Changed `kicadSettingsGet`, `kicadSettingsSet` (two registered sections, no new export). File:
pcbjam `wasm/bindings/cc_e12_pcb.inc` (includes `layer_pairs.h`, `pcbnew_settings.h`,
`router/pns_routing_settings.h`, all in the board TU's include path). Both register with
`setupOk` true, so a taken set runs Board Setup's OK steps and lo-e3s-e's tool reset (no second
reset here).

**router** (`kRouter`). The router's own settings are `PCBNEW_SETTINGS::m_PnsSettings`
(`tools.pns`), the object the live router holds by pointer (`PNS_TOOL_BASE::Reset` calls
`LoadSettings` with it; the router menu and DIALOG_PNS_SETTINGS edit it in place). When the router
has not run yet the object is made exactly as the router tool makes it. Get: KiCad's JSON keys
`mode` (0 highlight collisions, 1 shove, 2 walk around), `effort` (0 low, 1 medium, 2 full),
`remove_loops`, `smart_pads`, `shove_vias`, `suggest_finish`, `follow_mouse`, `free_angle_mode`,
`snap_to_tracks`, `snap_to_pads`, `optimize_dragged_track`, `auto_posture`, `fix_all_segments`
(booleans), read and written through the object's JSON (`Store`, the patch, `Load`, run 14's
`ccPcbJsonGet` and `ccPcbJsonSet`); plus ours from BOARD_DESIGN_SETTINGS:
`currentTrack {index, custom, width, customWidth}`, `currentVia {index, custom, diameter, drill,
customDiameter, customDrill}`, `currentDiffPair {index, custom, width, gap, viaGap, customWidth,
customGap, customViaGap}` (mm; `width`, `diameter`, `drill`, `gap`, `viaGap` are the CURRENT
figures and read only; `index` into the board's predefined list, 0 the net class value; `custom`
of track and via is ONE flag in KiCad, `UseCustomTrackViaSize`). Set: any of the keys; mode and
effort 0 to 2; a current object takes `index`, `custom` and its `custom*` numbers (0.001 to 100
mm), applied in KiCad's order (the index setter clears custom, so index, then numbers, then
custom).

**layerPairs** (`kLayerPairs`). `PCB_BASE_EDIT_FRAME::GetLayerPairSettings()`. Get
`{pairs: [{top, bottom, enabled, name}], current: {top, bottom}}` (layers by file name, `name` ""
for none). Set `{pairs?, current?}`: `pairs` replaces the list whole through `SetLayerPairs`
(KiCad drops a repeated pair; at most 256 rows; each row `top`, `bottom`, optional `enabled`
default true and `name` up to 128), `current` through `SetCurrentLayerPair`; layers must be two
different copper layers the board enables. KiCad's own queued events write the project file's
`m_LayerPairInfos` and the screen's route layers.

**What the build train must probe (Glasgow, board frame console):**
1. `kicadSettingsSections()` lists `router` and `layerPairs`.
2. `kicadSettingsGet('router')` answers the 13 keys and the three current objects; mode 2 on a
   fresh engine (KiCad's default walk around). Set `{mode:1}` then get reads 1; press X and route:
   the router shoves. Wrong: the get still reads the old mode, or the route still walks around.
3. Set `{currentTrack:{customWidth:0.4, custom:true}}`: get reads `currentTrack.width` 0.4 and
   `currentVia.custom` true (one flag); a new track is 0.4 mm. Set `{currentTrack:{index:0}}`:
   custom false again. Set `{mode:3}`, `{currentTrack:{index:999}}`, `{shove_vias:1}`: bad_args.
4. `kicadSettingsSet` layerPairs `{pairs:[{top:'F.Cu', bottom:'In1.Cu', name:'Top pair'}],
   current:{top:'F.Cu', bottom:'In1.Cu'}}`: get reads the one pair and the current; the undo depth
   did not move. `{current:{top:'F.Cu', bottom:'F.Cu'}}` and a layer the board does not enable:
   bad_args.

Syntax check at this commit:
`/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e12/kicad pcbnew`:
`PASS pcbnew pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (12064 ms)`, `syntax-check: 0 failed`.

## st-origins-e: section origins

Changed `kicadSettingsGet`, `kicadSettingsSet` (one registered section `origins`, `kOrigins`, in
`cc_e12_pcb.inc`; no new export). Get `{grid_origin: {x, y}, aux_origin: {x, y}}` in mm (the
board's design settings, KiCad's file names; the aux origin is the drill and place origin). Set
`{grid_origin?, aux_origin?}`, each exactly `{x, y}` within plus or minus 1000 mm; `{x: 0, y: 0}`
is KiCad's Reset (`gridResetOrigin`, `drillResetOrigin`). The set does what
`PCB_CONTROL::DoSetGridOrigin` and `BOARD_EDITOR_CONTROL::DoSetDrillOrigin` do (design settings,
the GAL's grid origin, the view marked dirty); the two origin markers follow on the tool reset of
Board Setup's OK steps (`setupOk` true: both tools re-read the design settings on MODEL_RELOAD,
`pcb_control.cpp` 124, `board_editor_control.cpp` 340). KiCad's own reset press makes an undo step
(UNDO_REDO::GRIDORIGIN); a settings set makes none (the section rule).

**What the build train must probe (Glasgow, board frame console):**
1. `kicadSettingsSections()` lists `origins`.
2. Set `{grid_origin:{x:10, y:20}}`: get reads 10 and 20; the grid origin marker and the grid
   itself move to (10, 20) mm on the canvas. Set `{grid_origin:{x:0, y:0}, aux_origin:{x:0,
   y:0}}`: both at 0, both markers at the page origin. Undo depth unchanged. Wrong: the get moves
   but the marker or the grid stays (the tool reset did not reach PCB_CONTROL).
3. `{grid_origin:{x:10}}`, `{origin:{x:1,y:1}}`, `{aux_origin:{x:5000, y:0}}`: bad_args.

Syntax check at this commit:

`/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e12/kicad pcbnew`:
`PASS pcbnew pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (11951 ms)`, `syntax-check: 0 failed`.
