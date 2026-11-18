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
