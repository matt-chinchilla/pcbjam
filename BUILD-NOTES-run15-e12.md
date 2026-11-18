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

## lo-e3s-e: the tool reset, numbering on Glasgow, stackup dielectric add and remove

Changed `kicadSettingsGet`, `kicadSettingsSet`, `kicadBoardEdit` (no new export, no marker, no
KiCad file). Files: pcbjam `wasm/bindings/cc_rules_pcb.inc` (the board settings set path, the
stackup section, the reannotate op) and `wasm/bindings/cc_e12_pcb.inc` (section `stackupLayers`).

**PE31, the tool reset.** `ccPcbSettingsChanged` (Board Setup's OK steps, run after every taken
board section set that is not a preference) now ends as `PCB_EDIT_FRAME::ShowBoardSetupDialog`
does: `GetToolManager()->ResetTools( TOOL_BASE::MODEL_RELOAD )`, then a `TOOL_EVENT( TC_COMMAND,
TA_MODEL_CHANGE, AS_ACTIVE )` without position through `ProcessEvent` (the event that makes the
router reload its design rules). The dialog's `ReCreateAuxiliaryToolbar()` is left out: it is a
protected member of `PCB_EDIT_FRAME` (the syntax check refused it) and the aux toolbar is chrome
the engine never shows. The stackup and finish sets, which ran only `OnModify` and a repaint in
run 14, now run `ccPcbSettingsChanged` too, so every board section set resets the tools (router
and layerPairs of st-routing-e ride this reset and add none). ResetTools deactivates the active
tool first (KiCad's own behavior: a route in progress is cancelled by a settings set).

**PE36, diagnosis then fix.** Cause: Glasgow revC3 carries seven footprints whose designator is
`REF**` (logos, no digit: the dialog's INVALID_REFDES). Run 14's duplicate check counted every
footprint's final designator, so the seven `REF**` were "two parts sharing a designator" and every
run answered `duplicate_designator`. KiCad's dialog (`dialog_board_reannotate.cpp` 583 to 605)
scans its change array and never lets an EMPTY or INVALID entry start a duplicate. Fix: the count
keeps, per designator, whether any entry of it is numbered or kept (not empty, not invalid); a
designator only empty or invalid entries share is no duplicate. With it, the scope choice now
overrides the designator's own state as `BuildFootprintList` does (selected, front or back: in
scope is numbered, out of scope excluded, an invalid designator in scope numbered as KiCad does);
"all" keeps empty and invalid designators unnumbered with their warning line, as before.

**Stackup add and remove.** `ccStackupDielectricEdit` (cc_rules_pcb.inc) is the one home:
`{addDielectric: {after: index}}` takes an ENABLED dielectric item (`index` is the item's index as
the stackup section's rows carry it in `index`) and adds a sublayer after its last one with
`BOARD_STACKUP_ITEM::AddDielectricPrms`, material set to KiCad's `NotSpecifiedPrm()` (what the
panel's transfer writes for an empty field); refused when the stackup already reads 256 rows.
`{removeDielectric: {index, sub}}` takes a sublayer of an enabled dielectric with two or more
(`RemoveDielectricPrms`), as the dialog's list offers. Exactly one action per set, never beside
`rows`. After the edit the board thickness is rebuilt from the stackup and Board Setup's OK steps
run. No undo step. Section `stackupLayers` (cc_e12_pcb.inc, registered `kStackupLayers`): get
`{dielectrics: [{index, name (FormatDielectricLayerName), sublayers, removable}], rows, canAdd}`
over enabled dielectrics; set takes the same two actions (same function), so its name in
`kicadSettingsSections` is the island's sign that the stackup set takes them.

**What the build train must probe (Glasgow, board frame console, Edit mode):**
1. `kicadSettingsSections()` lists `stackupLayers`. Wrong: absent (the registrar did not run).
2. `kicadSettingsGet('stackup')`, note the rows count N and the index of the first dielectric
   row D; `kicadSettingsSet({run:1, dir:'/tmp/cc-settings/1', section:'stackup', value:
   {addDielectric:{after:D}}})` then the record's value has N+1 rows, a new row with `index` D,
   `sub` 1, material "Not specified" (or its untranslated keyword), thickness 0, epsilon 1; undo
   depth unchanged. Then `{removeDielectric:{index:D, sub:1}}`: N rows again. Wrong: bad_args on
   a dielectric index, or a row added at sub 0, or the undo depth moved.
3. `{addDielectric:{after:0}}` (index 0 is the top silkscreen or paste, not a dielectric):
   `bad_args`. `{removeDielectric:{index:D, sub:0}}` on a one sublayer dielectric: `bad_args`.
   `{addDielectric:{after:D}, rows:[]}`: the export answers false or bad_args.
4. Tool reset: Setup, Net classes, Default clearance 0.6 mm; press X and route from a pad toward
   another net's track: the router keeps 0.6 mm. Wrong: it keeps the old clearance (the reset or
   the model change event did not reach the router).
5. Numbering: `kicadBoardEdit({run:1, dir:'/tmp/cc-edit/1', op:'reannotate', preview:true,
   filter:{}, set:{}})`: the record has rows and no `duplicate_designator`; its lines carry seven
   warnings "REF** is not numbered and keeps its designator". Apply (preview false): one undo step
   (depth +1), both engines' Undo restore. Wrong: `duplicate_designator` (the check still counts
   invalid designators), or a REF** renumbered in scope "all".

Syntax check (from this worktree, kicad worktree `/home/matthew/cc-engine-wt/e12/kicad`):
`/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e12/kicad pcbnew`
at this commit: `PASS pcbnew pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (14076 ms)`,
`syntax-check: 0 failed`. The first try refused `ReCreateAuxiliaryToolbar` (protected), removed.
No header change, no include path, no CMake or build script line, no new export.

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
