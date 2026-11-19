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
`router/pns_routing_settings.h`, all in the board TU's include path). layerPairs registers
with `setupOk` true, so a taken set runs Board Setup's OK steps and lo-e3s-e's tool reset (no
second reset here). router registers with `setupOk` false since repair e12-1 (below).

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

## st-editprefs-e: sections schEditing, pcbEditing, mouse

Changed `kicadSettingsGet`, `kicadSettingsSet` (registered sections, no new export). Files: pcbjam
`wasm/bindings/cc_e12_pcb.inc` (`pcbEditing` as `kPcbEditing`, `mouse` as `kMouse`, both
`setupOk` false; include `settings/common_settings.h`) and `wasm/bindings/cc_e12_sch.inc`
(`schEditing` as `kSchEditing`, `mouse` as `kSchMouse`, both `setupOk` false). `mouse` is
registered in BOTH frames over the one `COMMON_SETTINGS` of the image (`Pgm().GetCommonSettings()`),
so `kicadSettingsSections()` lists it under board and schematic. App settings, never design
state: no modified flag, no Board or Schematic Setup steps, no file saved (KiCad's Preferences OK
also saves the settings files; the page keeps them per browser and sends them after each boot).
A taken set runs the rest of Preferences' OK (`eda_base_frame.cpp` 1754): `ResetTools( REDRAW )`,
then `Kiway().CommonSettingsChanged( 0 )` (0, not HOTKEYS_CHANGED, so a key map the page set is
never reloaded from a file), then a repaint.

Wire keys are KiCad's own JSON paths with dots; values keep KiCad's kinds, read whole, each
enumeration only the numbers KiCad's panel offers:
- `pcbEditing` (PCBNEW_SETTINGS, PANEL_EDIT_OPTIONS' board half): `editing.flip_left_right`,
  `editing.esc_clears_net_highlight`, `editing.show_courtyard_collisions`,
  `editing.magnetic_graphics`, `editing.ctrl_click_highlight`, `editing.auto_fill_zones`,
  `editing.allow_free_pads`, `pcb_display.ratsnest_footprint`, `pcb_display.ratsnest_curved`,
  `pcb_display.show_page_borders` (bool); `editing.magnetic_pads`, `editing.magnetic_tracks`
  (0 none, 1 in the track tool, 2 always), `editing.track_drag_action` (0 move, 1 drag, 2 drag
  free angle), `editing.arc_edit_mode` (0 to 2), `editing.pcb_angle_snap_mode` (0 direct, 1 45,
  2 90); `editing.rotation_angle` (tenths of a degree, 1 to 3599);
  `pcb_display.ratsnest_thickness` (0.5 to 10).
- `schEditing` (EESCHEMA_SETTINGS, PANEL_EESCHEMA_EDITING_OPTIONS): `drawing.new_power_symbols`
  (0 default, 1 global, 2 local), `drawing.line_mode` (0 free, 1 90, 2 45), `editing.arc_edit_mode`
  (0 to 2), `drawing.default_repeat_offset_x`, `_y` (mils, plus or minus 100000),
  `drawing.repeat_label_increment` (plus or minus 1000000), and the bools
  `appearance.footprint_preview`, `system.never_show_rescue_dialog`, `autoplace_fields.enable`,
  `autoplace_fields.allow_rejustify`, `autoplace_fields.align_to_grid`, `input.drag_is_move`,
  `drawing.auto_start_wires`, `input.esc_clears_net_highlight`, `annotation.automatic`,
  `input.allow_unconstrained_pin_swaps`. The two sheet colors are left out (colors stay the
  viewer's).
- `mouse` (COMMON_SETTINGS, PANEL_MOUSE_SETTINGS): `input.mouse_left` (0 select, -1 drag
  selected, -2 drag any), `input.mouse_middle`, `input.mouse_right` (2 pan, 1 zoom, 3 none),
  `input.motion_pan_modifier`, `input.scroll_modifier_zoom`, `input.scroll_modifier_pan_h`,
  `input.scroll_modifier_pan_v` (0 or the wx key code of Alt, Control or Shift),
  `input.auto_pan_acceleration`, `input.zoom_speed` (1 to 10), and the bools
  `input.center_on_zoom`, `input.auto_pan`, `input.zoom_acceleration`, `input.zoom_speed_auto`,
  `input.horizontal_pan`, `input.reverse_scroll_zoom`, `input.reverse_scroll_pan_h`.

**What the build train must probe:**
1. Board frame: `kicadSettingsSections()` board list has `pcbEditing` and `mouse`; schematic frame
   list has `schEditing` and `mouse`.
2. Board: set `pcbEditing {"editing.magnetic_pads": 0}`: get reads 0; a pointer near a pad no
   longer snaps. Undo depth and the modified flag (title asterisk, `editsHeld`) unchanged. Wrong:
   the board reads modified (setupOk not honored).
3. Schematic: set `schEditing {"drawing.line_mode": 0}`: a new wire takes free angles.
4. Either frame: set `mouse {"input.mouse_middle": 1}`: a middle drag zooms in both frames (one
   object). Set `{"input.mouse_middle": 0}`, `{"input.zoom_speed": 11}`, `{"input.zoomspeed": 5}`:
   bad_args.

Syntax check at this commit (the lane's full faces):
`/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e12/kicad all`:
`PASS pcbnew (11213 ms)`, `PASS eeschema (11440 ms)`, `PASS symedit (7048 ms)`, `PASS fpedit (6779 ms)`,
`PASS dsedit (1000 ms)`, `PASS kicad_editor (4615 ms)`, `PASS occ (11128 ms)`, `syntax-check: 0 failed`.

## m7-e: eight Setup sections and kicadTuningSolve

Commits: kicad 97e33790 (`pcbnew/dialogs/panel_setup_tuning_profile_info.cpp`), pcbjam 7b0f3c1
(`cc_e12_pcb.inc`, `cc_e12_sch.inc`); then `git merge run15/main` (kicad cab6dde8, pcbjam 0edfe52).

**KiCad, K-TSOLVE (under `#ifdef __EMSCRIPTEN__`, `#else` keeps KiCad's text):** a block after the
includes defines `bool ccTuningSolve( BOARD*, const EDA_IU_SCALE&, bool aDifferential, double
aImpedance, PCB_LAYER_ID aSignal, PCB_LAYER_ID aTopRef, PCB_LAYER_ID aBottomRef, int aSolveFor (0
width, 1 gap, 2 delay), int& aWidth, int& aGap, int& aDelay, wxString& aProblem )` at global scope:
`calculateTrackParametersForCell`'s checks and the four `calculate*` members with
`getMicrostripBoardParameters` / `getStriplineBoardParameters`, taking the row's facts as
arguments (the private static helpers are copied into an anonymous namespace of that file as
`ccTsSkinDepth`, `ccTsStackupLayerId`, `ccTsDielectricLayers`, `ccTsAverageDielectric`; the
calculators are locals in place of the panel's members). `calculateTrackParametersForCell` reads
the grid and calls `ccTuningSolve`, showing KiCad's same messages. No header change (only this
`.cpp` recompiles); the declaration the pcbjam face uses is written in `cc_e12_pcb.inc`.

**Sections (registered through R7 in this lane's incs; setupOk true, so Board Setup's or
Schematic Setup's OK steps follow every taken set; no undo step):**

| section | frame | wire value | the set |
|---|---|---|---|
| `zoneDefaults` | pcb | the 17 keys of `defaults.zones.*` by their KiCad names (mm, degrees, enumeration numbers as the project file holds them) | patch, numbers only (only `hatch_orientation` may be negative), kind kept, `Load()` |
| `zoneHatchOffsets` | pcb | `{layers: [{layer, offset: [x, y] mm or null}]}`, every enabled copper layer in KiCad's UI order | replaces the list: an enabled copper layer not named goes back to null (Board default); layers enabled copper, once each, offsets within 1,000 mm |
| `boardFormatting` | pcb | `{applyToFields, applyToText, applyToShapes, applyToDimensions, applyToBarcodes, dashRatio, gapRatio}` | patch; ratios over 0 and at most 1,000; the panel's own steps (SetPlotOptions, the painter's ratios, a shape repaint) |
| `tuningPatterns` | pcb | `{single_track_defaults, diff_pair_defaults, diff_pair_skew_defaults}`, each `min_amplitude`, `max_amplitude`, `spacing` (mm), `corner_style` (0, 1), `corner_radius_percentage` (0 to 100), `single_sided` | each object merged |
| `componentClasses` | pcb (sch reads) | `{"sheet_component_classes.enabled": bool, "assignments": [...]}` (KiCad's JSON, the nested key with a dot) | patch; assignments replaced, each closed `{component_class (not empty), conditions_operator ALL or ANY, conditions {TYPE[-n]: {primary?, secondary?}}}`, TYPE one of the seven. The schematic face registers the same name: its get reads the project file, its set answers `bad_args` "component classes are set in the board editor" |
| `tuningProfiles` | pcb | `{tuning_profiles_impedance_geometric: [...]}` KiCad's JSON (width, gap, delay, via delays in KiCad's internal units) | the list replaced whole, every profile closed, names unique and not empty, type 0 or 1, layer names KiCad's, a signal layer once per profile |
| `annotation` | sch | `{"annotation.sort_order", "annotation.method", annotate_start_num, reuse_designators}` | patch; sort 0 or 1, method 0 to 2, start 0 to 999,999 |
| `bomPresets` | sch | `{bom_presets, bom_fmt_presets, bom_settings, bom_fmt_settings}` | each value first read by KiCad's own `from_json` (a value KiCad would not load is refused), then replaces its key whole, `Load()` |

**Export** `kicadTuningSolve( std::string ) -> std::string` in `cc_e12_pcb.inc`, block
`cc_e12_pcb`, **merged image only** (`KICAD_MERGED_EMBIND`, the call reaches a pcbnew dialog
object). Args closed: `{type: "single" | "differential", impedance, signalLayer, topRef?,
bottomRef?, solveFor: "width" | "gap" | "delay", width?, gap?}` (mm; canonical names of enabled
copper layers; gap only for a pair). Answers `{width: mm | null, gap: mm | null, delay: ps/cm |
null, problem: "" | KiCad's message}`; "" for any other shape, no frame or an open in flight. A
read: no record, no change. **The delay's unit is ps/cm** (KiCad's `EDA_UNITS::PS_PER_CM`, the
unit the panel's Delay column shows), rounded to 4 places.

**What the build train must probe** (fixture `settings-from/` once m8-i lands it; until then any
4 layer board):
1. `kicadSettingsSections()`: the board list holds the six board names, the schematic list
   `annotation`, `bomPresets`, `componentClasses`. Failure: a name missing (a registrar that did
   not run).
2. Board: `kicadSettingsGet("zoneHatchOffsets")` lists one row per enabled copper layer, F.Cu
   first. Set `{layers: [{layer: "In1.Cu", offset: [0.5, 0.5]}]}` then get: In1.Cu has
   `[0.5, 0.5]`, every other row null; the board reads modified; undo depth unchanged.
3. Board: set `tuningPatterns {single_track_defaults: {spacing: 1.2}}`, get reads 1.2 and the
   other keys of that object unchanged; undo depth unchanged. `{corner_style: 2}`: bad_args.
4. Board: `componentClasses` get answers the fixture's POWER assignment; a set of an assignment
   with conditions `{"BOGUS": {}}`: bad_args, nothing changed.
5. Board: `zoneDefaults {min_clearance: 0.6}` then draw a new zone (the zone tool's defaults
   read 0.6). `boardFormatting {dashRatio: 6}`: a dashed shape repaints with longer dashes.
6. Board: `kicadTuningSolve('{"type":"single","impedance":50,"signalLayer":"F.Cu","bottomRef":"In1.Cu","solveFor":"width"}')`
   answers a width over 0 and a delay over 0 and problem "". The same with `"solveFor":"gap"`:
   "". Differential with no gap and solveFor width: problem "Diff pair gap must be greater than
   0 to calculate width", all values null. Failure: "" for the good call (the export did not
   register: check the merged guard) or width null (the stackup read missed the reference).
7. Schematic: `annotation` answers `annotation.sort_order` and `annotate_start_num` (100 on
   the fixture); set `{"annotation.method": 1}` then get reads 1; `{"annotation.method": 3}`:
   bad_args. `bomPresets` set of `bom_presets: [{"name": "x"}]`: bad_args (KiCad's own
   from_json throws on the missing keys).

Syntax check (after the merge, the lane's full faces):
`/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e12/kicad all --kicad-file pcbnew/dialogs/panel_setup_tuning_profile_info.cpp`:
`PASS pcbnew (25682 ms)`, `PASS eeschema (21719 ms)`, `PASS symedit (9177 ms)`, `PASS fpedit
(8919 ms)`, `PASS dsedit (1276 ms)`, `PASS kicad_editor (6429 ms)`, `PASS occ (16452 ms)`, `PASS
kicad pcbnew/dialogs/panel_setup_tuning_profile_info.cpp (7766 ms)`, `syntax-check: 0 failed`.
Export check: `scripts/kicad/check-run15-exports.sh --since run15/main --kicad
/home/matthew/cc-engine-wt/e12/kicad --kicad-since run15/main`: `214 names registered in the
merged image (207 kicad names), 69 of 110 run 15 names, 0 failures`.

## m8-e: kicadSettingsRead

Commits: pcbjam 72e6293 (`cc_rules_pcb.inc`, `cc_rules_sch.inc`, `cc_e12_pcb.inc`,
`cc_e12_sch.inc`, `cc_e12_dispatch.inc`) and 5138ecf (the two rules incs' dated lines); no kicad
file. Then `git merge run15/main` (pcbjam b6d7eb7; kicad already at run15/main).

**Registry widened (guide section 2):** `CC_PCB_SECTION` gains, at its END, `std::function<json(
BOARD* aBoard, PROJECT* aProject )> read = nullptr`; `CC_SCH_SECTION` gains, at its END,
`std::function<json( PROJECT* aProject )> read = nullptr`. Every registration written before keeps
its meaning; a section with no reader never travels (it is listed in `missing`). This lane's m7
sections pass their readers (`zoneDefaults`, `zoneHatchOffsets`, `boardFormatting`,
`tuningPatterns`, `componentClasses`, `tuningProfiles`; `annotation`, `bomPresets`,
`componentClasses` in the schematic). ST's preference and origin sections have none (they never
travel, 6.1).

**Readers refactor, behavior of kicadSettingsGet unchanged:** `ccPcbJsonSection( BOARD*, PROJECT*,
name )` and `ccPcbSettingsRead( BOARD*, PROJECT*, rulesPath, name )` hold the built-in bodies
(project sections, textVariables, layers, stackup, finish, customRules), the frame's
`ccPcbSettingsGet` keeps variants and page and calls the reader for every other built-in, a
registered section's `get` as before. With no board only the project's sections answer
(netClasses, textVariables, and a registered reader given a null board: componentClasses,
tuningProfiles). The schematic twin: `ccSchJsonSection( PROJECT*, name )`, `ccSchSettingsRead(
PROJECT*, name )` (project sections, textVariables, busAliases from the project file's aliases as
KiCad's Import Settings reads them); the frame's get keeps variants, busAliases (the live
schematic's) and page.

**Export** `kicadSettingsRead( std::string ) -> bool`, registered in `cc_e12_dispatch.inc`, block
`cc_e12_dispatch` (merged image; the dispatcher picks `cc_e12::settingsReadPcb` in
`cc_e12_pcb.inc` on `pcbEditorActive()`, `cc_e12::settingsReadSch` in `cc_e12_sch.inc` on
`schEditorActive()`, false in any other frame). Args closed: `{run, dir: "/tmp/cc-settings-read/<n>",
project: "<name>.kicad_pro", board?: "<name>.kicad_pcb", sections: [1 to 64 unique names]}`, plain
file names that exist in `dir`; false for any other shape, no frame or an open in flight. Queued
on the apply coroutine:
- board frame: `SETTINGS_MANAGER::LoadProject( path, false )` (KiCad's own), the board by
  `PCB_IO_MGR::FindPlugin( KICAD_SEXP )->LoadBoard`, `SetProject` as `onAuxiliaryAction` does;
  each name read by `ccPcbSettingsRead` (customRules from `<board>.kicad_dru` beside the board,
  `missing` when that file is not there); `ClearProject`, the board deleted, the project
  unloaded without a save.
- schematic frame: the project loaded the same way; its `ERC_SETTINGS` and `SCHEMATIC_SETTINGS`
  made and `LoadFromFile()`d exactly as `SCHEMATIC::SetProject` makes them, read by
  `ccSchSettingsRead`, then deleted (only what this read made), the project unloaded. KiCad's own
  `onAuxiliaryAction` builds a whole `SCHEMATIC` here; its destructor clears the static
  `SCHEMATIC::m_IsSchematicExists` and unregisters every `SCH_FIELD` listener, the open
  schematic's too, so the engine does not build one.
- A name the frame does not list, a section with no reader, or a board section with no board is
  `missing`. Record `settings-read.json` `{sections: {name: value}, missing: [names]}`, then
  `.cc-settings-read.json` `{error, message}` last: error "" on a read, `unreadable` when KiCad
  could not load the project or the board (message: KiCad's first line with the run folder taken
  out, at most 512 characters; sections empty), `island_error` with no frame. Nothing of the open
  design changes; no undo step.

**What the build train must probe** (fixtures `settings-to/` open, `settings-from/` staged; m8-i):
1. Board frame: stage settings-from's `.kicad_pro`, `.kicad_pcb`, `.kicad_dru` under
   `/tmp/cc-settings-read/1/`; `kicadSettingsRead` with every board section of 6.1 (`layers`,
   `stackup`, `finish`, `zoneHatchOffsets`, `textDefaults`, `zoneDefaults`, `boardFormatting`,
   `constraints`, `predefinedSizes`, `teardrops`, `tuningPatterns`, `netClasses`,
   `componentClasses`, `tuningProfiles`, `customRules`, `checkSeverities`) answers true; the
   record lists netClasses with HV, layers with copperCount 6, customRules with the fixture's
   rule text; `missing` empty; `.cc-settings-read.json` error "". Then `kicadSettingsGet(
   "netClasses")` of the open design still lacks HV, its undo depth unchanged, the board not
   modified. Failure: the open design changed (the reader read the frame's board), or the
   record never lands (a throw on the coroutine).
2. The same with no `.kicad_dru` staged: `customRules` is in `missing`.
3. No board in the args: `netClasses`, `componentClasses`, `tuningProfiles`, `textVariables`
   answer; `layers` and the other board sections are in `missing`.
4. A `.kicad_pcb` that is not a board (any text): error `unreadable`, message without
   `/tmp/cc-settings-read/`; then a second good read still works (the other project was
   unloaded: `SETTINGS_MANAGER` holds one project again).
5. Schematic frame: the same staging with `["schFormatting", "annotation", "fieldTemplates",
   "bomPresets", "ercSeverities", "pinMap", "netClasses", "busAliases", "textVariables",
   "layers"]`: every name answers but `layers` (missing); `annotation` carries the fixture's
   values. After it, deleting a wire and a field edit through the properties panel still work
   (the open schematic's flags and listeners untouched).
6. A name with a path (`"project": "../x.kicad_pro"`) or a dir outside `/tmp/cc-settings-read/`:
   false. In the footprint editor frame: false.

Syntax check after the merge (b6d7eb7):
`/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e12/kicad all --kicad-file pcbnew/dialogs/panel_setup_tuning_profile_info.cpp`:
`PASS pcbnew (16727 ms)`, `PASS eeschema (17184 ms)`, `PASS symedit (8042 ms)`, `PASS fpedit
(8256 ms)`, `PASS dsedit (1193 ms)`, `PASS kicad_editor (6510 ms)`, `PASS occ (14913 ms)`, `PASS
kicad pcbnew/dialogs/panel_setup_tuning_profile_info.cpp (8322 ms)`, `syntax-check: 0 failed`.
Export check (after 5138ecf, which changed comments only):
`scripts/kicad/check-run15-exports.sh --since run15/main --kicad /home/matthew/cc-engine-wt/e12/kicad --kicad-since run15/main`:
`215 names registered in the merged image (208 kicad names), 70 of 110 run 15 names, 0 failures`.

## Repair e12-1: the router's custom sizes (fix note e12-1)

The probe (build T3, Glasgow): `kicadSettingsSet` router `{currentTrack:{customWidth:0.4,
custom:true}}` answered taken, and the get still read `custom` false and `customWidth` 0.15
(the via the same); `{currentTrack:{index:1}}` and `{mode:1}` did take.

Cause: the edits ran, then the registry ran Board Setup's OK steps (`setupOk` true), whose
`board->SynchronizeNetsAndNetClasses( true )` (kicad `pcbnew/board.cpp` 2737 to 2747) sets
`UseCustomTrackViaSize( false )` and every custom track, via and pair size back to the default
net class's. The index is not touched there, which is why `index` survived.

Fix (pcbjam e781ae3, `wasm/bindings/cc_e12_pcb.inc` only): the router section registers with
`setupOk` false: its settings object (`PCBNEW_SETTINGS::m_PnsSettings`) and the current sizes
are editor state, never saved in the board, so the board is no longer marked modified by a
router set either. The live router reads the settings object in place (it holds it by pointer);
after any current size edit the set runs `PCB_ACTIONS::trackViaSizeChanged`, the action KiCad's
own size choices run (`board_editor_control.cpp` 1111, 1135, 1193), so a route in progress takes
the sizes. A `custom` true also sets `m_TempOverrideTrackWidth`, as
`ROUTER_TOOL::CustomTrackWidthDialog` does (`router_tool.cpp` 2873), so the custom width wins over
a connected track's width. Ruling 3 stands: no second tool reset is added; the router section
simply no longer asks for Board Setup's.

Checks after `git merge run15/main` (pcbjam and kicad):
`/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e12/kicad all`:
PASS pcbnew (15400 ms), PASS eeschema (14303 ms), PASS symedit (7538 ms), PASS fpedit
(10071 ms), PASS dsedit (5642 ms), PASS kicad_editor (5533 ms), PASS occ (13215 ms),
`syntax-check: 0 failed`.
`scripts/kicad/check-run15-exports.sh --since run15/main --kicad /home/matthew/cc-engine-wt/e12/kicad --kicad-since run15/main`:
`256 names registered in the merged image (249 kicad names), 111 of 111 run 15 names, 0 failures`.

What the train must probe again (Glasgow, board frame): probe 3 of st-routing-e as written:
set `{currentTrack:{customWidth:0.4, custom:true}}`, the get reads `currentTrack.custom` true,
`width` 0.4, `currentVia.custom` true; set `{currentVia:{custom:true, customDiameter:0.8,
customDrill:0.4}}`, the get reads `currentVia.diameter` 0.8 and `drill` 0.4; a new track routed
after the set is 0.4 mm; `{currentTrack:{index:0}}` reads custom false again; the board's
modified flag did not change on any router set. Wrong: any custom figure reads its old value.

## Handover

Every unit of the lane is done: lo-e3s-e, st-routing-e, st-origins-e, st-editprefs-e, m7-e,
m8-e (each has its status file). Merge-only and merged image facts: `kicadTuningSolve` is merged
image only (`KICAD_MERGED_EMBIND`); `kicadSettingsRead` is registered in the dispatcher face
(merged image), its halves compile in every configuration and call nothing of another TU. The one
KiCad file of the lane, `pcbnew/dialogs/panel_setup_tuning_profile_info.cpp`, has no header
change (one object recompiles). Nothing to report to E0: no include path, CMake or build script
line, no new export name.

## State for the relay (after st-editprefs-e; kept for its history, the handover above is current)

Done and committed: lo-e3s-e (3558c9e), st-routing-e (fe4e114), st-origins-e (134c8f1),
st-editprefs-e (1833c6d); each has its status file. Then `git merge run15/main` (pcbjam fc22233;
kicad fast forward to caf17fbe, no kicad commit of this lane yet). After that merge:
`scripts/kicad/check-run15-exports.sh --since run15/main --kicad /home/matthew/cc-engine-wt/e12/kicad --kicad-since run15/main`:
`154 names registered in the merged image (147 kicad names), 9 of 110 run 15 names, 0 failures`;
syntax check `all`: PASS pcbnew (21951 ms), eeschema (12995 ms), symedit (7926 ms), fpedit
(7148 ms), dsedit (1031 ms), kicad_editor (5119 ms), occ (13272 ms), `syntax-check: 0 failed`.

Not done: m7-e (the eight sections of addendum-properties-setup.md 5.2 and `kicadTuningSolve`
5.3, with the lane's one KiCad file `pcbnew/dialogs/panel_setup_tuning_profile_info.cpp`:
lift `calculateTrackParametersForCell`, line 702, into a free function under `#ifdef
__EMSCRIPTEN__`, the panel calling it), then m8-e (`kicadSettingsRead`, 6.2; the guide's
section 2 allows widening `CC_PCB_SECTION` and `CC_SCH_SECTION` with a reader of a given BOARD
and PROJECT at the END of the struct, defaulted). `kicadTuningSolve` is a board export (register
in `cc_e12_pcb`); `kicadSettingsRead` is both frames (halves in the pcb and sch incs, the
dispatcher picks one in `cc_e12_dispatch`). Helpers the relay can reuse: `cc_e12::pcb::
patchKeysIn`, `readPoint`, `pointJson`, `prefSection` style JSON sections over run 14's
`ccPcbJsonGet` / `ccPcbJsonSet` and the sch twins `ccJsonSectionGet` / `ccJsonSectionSet`.
Watch: the scratchpad folder of the seat sessions is shared; keep per unit file copies elsewhere.

## Repair e12-2: tuning profiles take UNDEFINED reference layers (site close row E3; RULINGS 36)

The fault (site unit m7-s2): `kicadSettingsSet` for `tuningProfiles` refused any layer row whose `top_reference_layer` or `bottom_reference_layer` is `UNDEFINED`. Cause, read in the code: `tuningProfileListOk`'s `layerOk` takes a name only when `LSET::NameToLayer` answers 0 or more. KiCad itself writes a row with no reference layer (an outer layer microstrip, a track on F.Cu has nothing above it) as `LSET::Name( UNDEFINED_LAYER )`, the string `UNDEFINED` (kicad `common/lset.cpp`, the `aLayerId < 0` branch), and `TUNING_PROFILES` reads it back through `NameToLayer`, which answers -1, `UNDEFINED_LAYER` (`common/project/tuning_profiles.cpp` 123 to 131). So a saved KiCad profile with such a row could not be set back at all.

The repair (pcbjam b0424c8, `wasm/bindings/cc_e12_pcb.inc`): `referenceOk` takes the exact string `UNDEFINED` or what `layerOk` takes, for the two reference keys only; `signal_layer` and the via override layers keep `layerOk`. The value is stored as sent through `jsonSectionSet`, so KiCad's own load makes it `UNDEFINED_LAYER` and the get writes `UNDEFINED` again. No KiCad file, no export, no header.

### Checks (repair e12-2)

After `git merge run15/main` in both worktrees, from `/home/matthew/cc-engine-wt/e12/pcbjam`: `/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e12/kicad all`

```
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (16126 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (15251 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (7793 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (10517 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (5706 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (5426 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (13677 ms)
syntax-check: 0 failed
```

`scripts/kicad/check-run15-exports.sh --join --kicad /home/matthew/cc-engine-wt/e12/kicad` and the `--since run15/main --kicad-since run15/main` form both printed `check-run15-exports: 257 names registered in the merged image (250 kicad names), 112 of 112 run 15 names, 0 failures`.

### What the next build must probe (e12-2)

Glasgow, board frame: `settings.set` `tuningProfiles` with one profile whose `layer_entries` hold `{signal_layer: "F.Cu", top_reference_layer: "UNDEFINED", bottom_reference_layer: "In1.Cu", ...}`: accepted; `settings.get` `tuningProfiles` reads the row back with `top_reference_layer` `UNDEFINED`, equal to what was set. The same row with `top_reference_layer: "Nowhere.Cu"` is refused (`bad_args`), and `signal_layer: "UNDEFINED"` is still refused. Wrong if the UNDEFINED row answers `bad_args` (the image lacks pcbjam b0424c8).
