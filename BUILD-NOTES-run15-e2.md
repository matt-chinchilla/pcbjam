<!-- Copyright (C) 2026 Circuit Center Inc -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Engine run 15, lane e2: placement, zone and convert dialogs, the microwave tools, the auto-placer

Branches `seat/run15-e2` in pcbjam (`/home/matthew/cc-engine-wt/e2/pcbjam`) and kicad (`/home/matthew/cc-engine-wt/e2/kicad`), both cut from `run15/main` (pcbjam 9c8a02f, kicad 1b214bab). Board face only: `wasm/bindings/cc_e2_pcb.inc`, namespace `cc_e2`, block `EMSCRIPTEN_BINDINGS( cc_e2_pcb )`.

Unit order: st-mw-e, m6-e2, m6-e1, m3-e2, m3-e1, st-ap-e.

## st-mw-e: K-MW and kicadMicrowaveArm

**Export** `kicadMicrowaveArm( std::string argsJson ) -> bool` in `cc_e2_pcb.inc`, function `cc_e2::microwaveArm`, block `cc_e2_pcb`. Registered in both pcbnew configurations (merged image and occ_service; it calls nothing outside the TU and the header only `cc_microwave_arm.h`). Not merged image only.

Args (closed, read whole before anything is armed): `{ shape: "line" | "gap" | "stub", value }`, `{ shape: "arcStub", value, angle? }`, `{ shape: "polygon", polygon: { points, kind, size } }`. `value` a number, 0 < mm <= 500 (track length for line, gap or stub size, arc stub radius). `angle` 0 to 180 degrees, absent = 0 (the dialog's own default "0.0"). `points` 2 to 2,048 pairs `[x, y]`, `kind` `plain` (KiCad's Normal) | `symmetrical` | `mirrored`, `size` `[x, y]` mm, each nonzero, |size| <= 500, every |coordinate x size| <= 1,000 mm. Any other key, a key of another shape, a wrong type, a bound broken: false and the earlier arm untouched. false also with no board frame, an open in flight (`pcbjam_open::busy()`) or any dialog shown. A read: nothing is queued, no run folder, no record.

**Contract note for the island and site seats (st-mw-i, st-mw-s):** `polygon.points` are the shape description's `$COORD` pairs AS THE FILE WRITES THEM (fractions, what `MWAVE_POLYGONAL_SHAPE_DLG::ReadDataShapeDescr` reads into `g_PolyEdges`), and `polygon.size` is the scale in millimeters (the file's `XScale`, `YScale` converted from its `Unit=`, or the size the reader typed). KiCad multiplies each point by the scale (`createPolygonShape`); its own dialog's size fields never reached the shape (the code overwrites `g_ShapeSize` from the file's scale), so the reader's size here is the scale. The spec's "points (mm)" reads as this.

**KiCad (kicad `seat/run15-e2`, commit 95f6d0a7):**
- new `pcbnew/microwave/cc_microwave_arm.h` (header only, everything under `__EMSCRIPTEN__`): `CC_MICROWAVE_SHAPE`, `CC_MICROWAVE_ARM`, `cc_microwave::Arm`, `Armed`, `Begin( shape ) -> run id`, `Active( shape )`, `End( run )`. One pending arm; the start of ANY microwave action consumes it, and only a matching shape becomes the run's values.
- `pcbnew/microwave/microwave_tool.cpp`: `addMicrowaveFootprint` begins the run (`Begin`), the local placer's `PlaceItem` override posts `ACTIONS::cancelInteractive` after its commit when armed (one placement, then the tool ends as on Esc), `End` after `doInteractiveItemPlacement`; `drawMicrowaveInductor` begins a LINE run, after one inductor pops the tool and breaks (as its own Esc branch does), `End` at exit.
- `pcbnew/microwave/microwave_footprint.cpp`: `createFootprint` takes the armed size or radius (and the arc stub's angle, with KiCad's own abs and 180 clamp) in place of both `WX_TEXT_ENTRY_DIALOG`s.
- `pcbnew/microwave/microwave_inductor.cpp`: `createMicrowaveInductor` takes the armed length in place of "Length of track:" and keeps the value dialog's default `L` in place of "Component value:". (This file is not in the lane's `kicadFiles` list but inside the unit's owned path `kicad:pcbnew/microwave/`; the line shape needs it. Reported to E0.)
- `pcbnew/microwave/microwave_polygon.cpp`: `createPolygonShape` fills `g_PolyEdges`, `g_ShapeScaleX/Y`, `g_ShapeSize`, `g_PolyShapeType` from the arm in place of `MWAVE_POLYGONAL_SHAPE_DLG` and its file chooser; the rest is KiCad's code (mirror, symmetry, null size and no point errors).
- Every change under `#ifdef __EMSCRIPTEN__`; plain builds compile what they did. No KiCad header changed (the class header `microwave_tool.h` is untouched).

Unarmed presses keep KiCad's dialogs (the generic `entry` ask of m1x-e1 then answers the three entries; the polygon dialog has no ask: the page always arms before pressing Shape). Errors KiCad raises after the values (inductor "Requested length < minimum length", "too large", "too small", polygon "Shape has a null size.") stay KiCad's info bar messages; the armed run still ends.

**Probes for the build train (Glasgow, Board, Edit mode, console):**
1. `kicadMicrowaveArm('{"shape":"stub","value":5}')` true; press `pcbnew.MicrowaveTool.createStub`; click once: a stub follows the pointer with NO dialog; click: placed; undo depth +1 ("Place microwave feature"); the tool has ended (a third click places nothing, no dialog). Wrong if: a dialog opens (Begin never matched), or a second click starts another stub.
2. Same for `gap` 0.5, `arcStub` value 3 angle 60 (pad 1 custom polygon, a 60 degree fan of radius 3 mm), `line` 10 (two clicks about 5 mm apart: an `mw_inductor` footprint, value `L`, track length 10 mm; then the tool ends). `line` 2 with clicks 5 mm apart: KiCad's info bar "Requested length < minimum length", nothing added, tool ends.
3. `polygon` with points `[[0,0],[0.25,0.5],[0.5,0.7],[0.75,0.5],[1,0]]`, kind `symmetrical`, size `[4,2]`: a `muwave_polygon` footprint 4 mm long, mirrored about X, no dialog, no file chooser.
4. Refusals (each false, and a following armed press still uses the earlier good arm only if it was not consumed): `{}`, `{"shape":"stub"}`, `{"shape":"stub","value":0}`, `{"shape":"stub","value":501}`, `{"shape":"gap","value":1,"angle":5}`, `{"shape":"arcStub","value":3,"angle":190}`, `{"shape":"stub","value":"5"}`, polygon with 1 point, with 2,049 points, `kind":"normal"`, size `[0,2]`, an extra key in `polygon`.
5. Consumption: arm `stub`, press Gap: Gap's KiCad entry dialog (or the m1x entry ask) opens, then a Stub press also asks (the arm was consumed by the Gap start).
6. occ_service links with `kicadMicrowaveArm` registered (header only, no symbol outside the TU).

**Syntax check** (from the pcbjam worktree, `scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e2/kicad pcbnew occ --kicad-file pcbnew/microwave/microwave_tool.cpp --kicad-file pcbnew/microwave/microwave_footprint.cpp --kicad-file pcbnew/microwave/microwave_inductor.cpp --kicad-file pcbnew/microwave/microwave_polygon.cpp`, through the gate):

```
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (15393 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (15079 ms)
PASS kicad pcbnew/microwave/microwave_tool.cpp (8499 ms)
PASS kicad pcbnew/microwave/microwave_footprint.cpp (7314 ms)
PASS kicad pcbnew/microwave/microwave_inductor.cpp (7044 ms)
PASS kicad pcbnew/microwave/microwave_polygon.cpp (7005 ms)
syntax-check: 0 failed
```

## m6-e2: Create Array and Position Relative ask the host (modeless)

No export (the prompt exports are e1's m1x-e1: `kicadPromptAnswer`, `kicadPromptCancel`, `kicadPromptPick`, marker `kicadHostPrompts`). Nothing in `cc_e2_pcb.inc`.

**Host asks** (through `include/host_prompt.h`, e1's, merged into `run15/main` 79c5c7af):
- `window.kicadHostPrompt.onAsk({token, ask: "array", modal: false, record})` from `DIALOG_CREATE_ARRAY::Show( true )` (`pcbnew/dialogs/dialog_create_array.cpp`, the `__EMSCRIPTEN__` block at the end; called by `ARRAY_TOOL::CreateArray`, `pcbnew/tools/array_tool.cpp` 104, unchanged). Record `{ shape: "grid" | "circular", arrangeSelection, uniqueReferences, padNumbering, grid: { nx, ny, dx, dy, offsetX, offsetY, centered, stagger, staggerRows, horizontalThenVertical, reverseNumberingAlternate, numberingStartSpecified, numbering2d, primary, secondary }, circular: { centerX, centerY, angle, angleOffset, clockwise, count, rotateItems, numberingStartSpecified, axis } }`, an axis `{ type: "numeric" | "hex" | "alphaShort" | "alphaFull", start: string, step }`; lengths mm (board coordinates for the center), angles degrees; `padNumbering` is the dialog's `m_isFootprintEditor` (true in the footprint editor frame).
- `onUpdate({token, record})` after each pick (`UpdatePickedItem`, `UpdatePickedPoint`).
- `window.kicadHostPrompt.onAsk({token, ask: "positionRelative", modal: false, record})` from `DIALOG_POSITION_RELATIVE::Show( true )` (`pcbnew/dialogs/dialog_position_relative.cpp`; called by `POSITION_RELATIVE_TOOL::PositionRelative`, unchanged). Record `{ anchor: { kind: "item" | "point" | "gridOrigin" | "userOrigin", x, y, label }, x, y }` (label KiCad's `m_referenceInfo` text verbatim, x and y of the anchor and of the offset in mm). A later `Show( true )` while the prompt is parked (after a pick, or Shift+P again) sends `onUpdate` with the new record in place of a second ask.
- `onEnd({token, reason: "gone"})` when a dialog with a parked prompt is destroyed (`CcHostPromptForget` in both destructors).

**Answers:**
- array: any subset of the record's keys (closed: an unknown key refuses with `field` = that key; a bad value inside `grid` or `circular` refuses with `field` `grid` or `circular`), read whole, then written into the dialog's own controls (`ChangeValue`, radio buttons, the notebook page by `shape`); a given circular `angle` clears KiCad's Full circle box so the angle governs; then KiCad's own `Validate()` and `TransferDataFromWindow()`. Accepted: the dialog closes itself and `ARRAY_TOOL::onDialogClosed` makes the array in ONE commit; the prompt ends (no `onEnd`, the answer's `{"accepted":true}` closes the card). Refused: the prompt stays parked; KiCad's error text (it called `wxMessageBox`) is captured as the refusal's `message` through `CcPromptCaptureMessage`; the zero spacing warning is `IsOK`, whose capture is m1x-e2's (`common/confirm.cpp`, e1).
- array `prompt.pick`: `point` and `item` run `OnSelectCenterButton` as the two buttons do (KiCad's picker; the dialog disables itself while picking, as KiCad's does); the result arrives as `onUpdate`.
- array cancel (`kicadPromptCancel`): `Close()` with no options, so ARRAY_TOOL destroys the dialog and makes nothing.
- positionRelative: `{ x?, y? (mm), anchor?: "gridOrigin" | "userOrigin" }`, closed. `anchor` runs `OnUseGridOriginClick` or `OnUseUserOriginClick`; with no x and no y the card is sent the new record and the prompt stays parked. With x or y: a polar entry in the dialog is first turned to Cartesian (the box cleared), the given values written, then KiCad's own `OnOkClick` (one commit "Position Relative"), then the dialog hides as KiCad's OK hides it and the prompt ENDS. **Call, reported:** the addendum says the card stays open after Apply "as KiCad's dialog stays"; KiCad 10's OK hides the dialog (`OnOkClick` skips to the standard OK), and a second Apply against the same tool run would move again from the stale selection anchor (`POSITION_RELATIVE_TOOL::m_selectionAnchor` is only set by the tool's start), so the engine follows KiCad: after Apply, a new Shift+P asks anew. Picks keep the card open.
- positionRelative `prompt.pick`: `item` runs `OnSelectItemClick`, `point` `OnSelectPointClick` (the dialog hides; the result returns through `UpdatePickedItem`/`UpdatePickedPoint`, whose `Show( true )` sends `onUpdate`).
- positionRelative cancel: the dialog hidden (kept, as KiCad keeps it).

**Files (kicad `seat/run15-e2`, commit e7872748):** `pcbnew/dialogs/dialog_create_array.h` and `.cpp`, `pcbnew/dialogs/dialog_position_relative.h` and `.cpp`. Everything under `#ifdef __EMSCRIPTEN__`. **Header change reach:** `dialog_create_array.h` is included by `dialog_create_array.cpp` and `pcbnew/tools/array_tool.cpp`; `dialog_position_relative.h` by `dialog_position_relative.cpp` and `pcbnew/tools/position_relative_tool.cpp` (and `qa/qa_utils/mocks.cpp`, not in the wasm build). Four TUs.

**Probes (canvas-kinds fixture, Board, Edit mode, a `window.kicadHostPrompt` listener that records and returns true):**
1. Select R1, Ctrl+T: one `onAsk` ask `array`, modal false, the record above, no KiCad window. `kicadPromptAnswer(token, {"shape":"grid","grid":{"nx":3,"ny":2,"dx":2.54,"dy":2.54},"uniqueReferences":true,"arrangeSelection":false})` answers `{"accepted":true}`; six items (R1 and five copies with new references); undo depth +1; one Undo removes them. Wrong if: the dialog shows, the answer is stale, or two undo steps.
2. Again, `shape` circular: `kicadPromptPick(token, "point")` returns 0, a click on U1 pad 1, `onUpdate` with `circular.centerX/centerY` at that point; answer `{"shape":"circular","circular":{"count":6,"angle":60}}`: a ring of six.
3. Refusal: `{"grid":{"nx":"3"}}` answers `{"accepted":false,"field":"grid","message":""}` and the prompt stays (a later good answer is accepted); `{"foo":1}` gives `field` `foo`; `{"shape":"grid","grid":{"nx":0}}` reaches KiCad (refused or accepted by KiCad's own rules, never a crash).
4. Cancel: `kicadPromptCancel(token)` 0; nothing made; the next Ctrl+T asks again.
5. Select R1, Shift+P: ask `positionRelative`; `kicadPromptPick(token, "item")`, click U1: `onUpdate` with anchor kind `item` at U1's position and KiCad's label; answer `{"x":5,"y":0}`: R1 sits 5 mm right of U1, one undo step "Position Relative"; the prompt has ended (`kicadPromptAnswer` on the same token answers `{"stale":true}`).
6. Footprint editor frame: Ctrl+T on a pad: the record's `padNumbering` true.
7. No listener: KiCad's own dialogs show as before (both).

**Syntax check** (`--kicad /home/matthew/cc-engine-wt/e2/kicad pcbnew --kicad-file pcbnew/dialogs/dialog_create_array.cpp --kicad-file pcbnew/tools/array_tool.cpp --kicad-file pcbnew/dialogs/dialog_position_relative.cpp --kicad-file pcbnew/tools/position_relative_tool.cpp`):

```
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (41103 ms)
PASS kicad pcbnew/dialogs/dialog_create_array.cpp (44573 ms)
PASS kicad pcbnew/tools/array_tool.cpp (9340 ms)
PASS kicad pcbnew/dialogs/dialog_position_relative.cpp (8181 ms)
PASS kicad pcbnew/tools/position_relative_tool.cpp (9619 ms)
syntax-check: 0 failed
```

## m6-e1: Move Exactly, Interactive Offset, Filter Selected Items and Get and Move Footprint ask the host (modal)

No export (the prompt exports are e1's m1x-e1: `kicadPromptAnswer`, `kicadPromptCancel`, marker `kicadHostPrompts`). Nothing in `cc_e2_pcb.inc`.

Each dialog overrides `ShowModal` under `__EMSCRIPTEN__` in its own file (no tool file touched): `LoadControlState()` then its own `TransferDataToWindow()` (the order of its init dialog event, so the record is what the dialog would show, including the values DIALOG_SHIM keeps between uses), the record, then `CcHostPromptModal( ask, record, receiver )`: 1 returns `wxID_OK` (the receiver already ran the dialog's own `TransferDataFromWindow`, which filled the caller's values), 0 `wxID_CANCEL`, -1 KiCad's own `ShowModal`. Every answer is closed and read whole before anything is written: an unknown key or a bad value refuses with `field` = that key and the prompt stays parked. Lengths mm, angles degrees.

**Host asks** (`window.kicadHostPrompt.onAsk({token, ask, modal: true, record})`):
- `moveExact` from `DIALOG_MOVE_EXACT::ShowModal` (`pcbnew/dialogs/dialog_move_exact.cpp`; called by `EDIT_TOOL::MoveExact`, `pcbnew/tools/edit_tool.cpp` 3326, unchanged). Record `{ x, y, rotation, anchor, anchors }`: x, y the move as a Cartesian vector (a polar entry turned Cartesian as `TransferDataFromWindow` delivers it), rotation degrees, `anchor` and each of `anchors` one of `item`, `selectionCenter`, `userOrigin`, `drillOrigin` (`m_menuIDs` in order: the first is the caller's own anchor, `drillOrigin` only in the board editor). Answer `{ x?, y?, rotation?, anchor? }`: a polar entry is first cleared to Cartesian (the Polar box unchecked), the values written into the dialog's own UNIT_BINDERs and anchor choice, KiCad's maximum board area bound (the one its `OnTextChanged` enforces by disabling OK) refused with field `x` (or `y` when only y was given) and KiCad's sentence "Invalid movement values.  Movement would place selection outside of the maximum board area." as the message; then `TransferDataFromWindow`, then `SaveControlState()` (KiCad keeps the values for the next use when the dialog closes). `rotation` |deg| <= 3600; a length must fit the board's int range.
- `offset` from `DIALOG_OFFSET_ITEM::ShowModal` (`dialog_offset_item.cpp`; called by `POSITION_RELATIVE_TOOL`'s Interactive Offset, `pcbnew/tools/position_relative_tool.cpp` 349, after its two clicks). Record `{ x, y }`, the measured vector. Answer `{ x?, y? }` (Cartesian; a polar entry in the dialog is first turned back through its own `OnPolarChanged`), then `TransferDataFromWindow`, `SaveControlState()`.
- `filterSelection` from `DIALOG_FILTER_SELECTION::ShowModal` (`dialog_filter_selection.cpp`; called by `PCB_SELECTION_TOOL::filterSelection`, `pcbnew/tools/pcb_selection_tool.cpp` 3280). Record `{ includeFootprints, includeLockedFootprints, includeTracks, includeVias, includeZones, includeItemsOnTechLayers, includeBoardOutlineLayer, includePcbTexts }` (booleans, KiCad's `OPTIONS` names). Answer any subset, booleans only; the boxes set, then the dialog's own `checkBoxClicked` (the locked footprints box follows the footprints box), then `TransferDataFromWindow`.
- `findFootprint` from `DIALOG_GET_FOOTPRINT_BY_NAME::ShowModal` (`dialog_get_footprint_by_name.cpp`; called by `GetFootprintFromBoardByReference`, `pcbnew/tools/edit_tool.cpp` 771, for Get and Move Footprint, T). Record `{ references }`: the reference of each entry of the dialog's list in its order (KiCad sorts the "REF    ( value )" strings). Answer `{ reference }` (a string): trimmed, and refused (field `reference`, empty message) when the list holds no entry of that reference, case blind as KiCad's match; else written into the dialog's search field and OK; KiCad then finds the footprint and it hangs on the pointer (KiCad's move).

**Files (kicad `seat/run15-e2`, commit 272aac33):** `pcbnew/dialogs/dialog_move_exact.{h,cpp}`, `dialog_offset_item.{h,cpp}`, `dialog_filter_selection.{h,cpp}`, `dialog_get_footprint_by_name.{h,cpp}`. Every change under `#ifdef __EMSCRIPTEN__` (`dialog_get_footprint_by_name.cpp` also gained its missing final newline). **Header change reach:** each header gains `#include <host_prompt.h>` and `int ShowModal() override;` under `__EMSCRIPTEN__`; included by its own `.cpp` and `pcbnew/tools/edit_tool.cpp`, `edit_tool_move_fct.cpp`, `position_relative_tool.cpp`, `pcb_selection_tool.cpp` (and `qa/qa_utils/mocks.cpp`, not in the wasm build): eight TUs.

**Probes (canvas-kinds fixture, Board, Edit mode, a `window.kicadHostPrompt` listener that records and returns true):**
1. Select R1, Shift+M: one `onAsk` ask `moveExact`, modal true, record `{x:0,y:0,rotation:0,anchor:"item",anchors:["item","userOrigin","drillOrigin"]}` (first use), no KiCad window. `kicadPromptAnswer(token, '{"x":2,"rotation":90}')` answers `{"accepted":true}`: R1 moves 2 mm right and turns 90 degrees, one undo step; Undo restores it. A second Shift+M shows x 2, rotation 90 (the kept values). Wrong if: the dialog shows, two undo steps, or the second record is zeros.
2. Refusals: `{"x":"2"}` gives `field` `x`; `{"anchor":"selectionCenter"}` with one item selected gives `field` `anchor` (not offered); `{"x":3000000}` gives `field` `x` and KiCad's maximum board area sentence; `{"foo":1}` `field` `foo`; the prompt stays parked and a later good answer is accepted. `kicadPromptCancel(token)` 0: nothing moves.
3. Interactive Offset (`pcbnew.PositionRelative.interactiveOffsetTool`): two clicks 3 mm apart horizontally: ask `offset`, record x about 3, y 0; answer `{"y":1}`: the selection moves by (3, 1), one undo step.
4. Select everything, Filter Selected Items: ask `filterSelection`, every key true; answer `{"includeTracks":false}`: only the tracks leave the selection. Answer `{"includeTracks":1}` refuses with `field` `includeTracks`.
5. T: ask `findFootprint`, record `references` `["R1","R2","U1"]` (the fixture's order as KiCad sorts); answer `{"reference":"u1"}`: U1 hangs on the pointer, a click places it, one undo step. `{"reference":"Q9"}` refuses with `field` `reference`.
6. No listener: every one of the four KiCad dialogs shows as before.

**Syntax check** (`--kicad /home/matthew/cc-engine-wt/e2/kicad pcbnew occ --kicad-file` each file below, through the gate; the same run also checked m3-e1's and st-ap-e's files):

```
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (13072 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (12279 ms)
PASS kicad pcbnew/dialogs/dialog_move_exact.cpp (5303 ms)
PASS kicad pcbnew/dialogs/dialog_offset_item.cpp (4117 ms)
PASS kicad pcbnew/dialogs/dialog_filter_selection.cpp (4241 ms)
PASS kicad pcbnew/dialogs/dialog_get_footprint_by_name.cpp (4101 ms)
PASS kicad pcbnew/tools/edit_tool.cpp (8903 ms)
PASS kicad pcbnew/tools/edit_tool_move_fct.cpp (7366 ms)
PASS kicad pcbnew/tools/position_relative_tool.cpp (6414 ms)
PASS kicad pcbnew/tools/pcb_selection_tool.cpp (8918 ms)
PASS kicad pcbnew/tools/convert_tool.cpp (6961 ms)
PASS kicad pcbnew/dialogs/dialog_outset_items.cpp (5455 ms)
PASS kicad pcbnew/autorouter/autoplace_tool.cpp (6042 ms)
syntax-check: 0 failed
```

## m3-e1: the conversion settings and Create Outsets ask the host (modal)

No export (e1's prompt exports answer). Nothing in `cc_e2_pcb.inc`.

**Host asks** (`window.kicadHostPrompt.onAsk({token, ask, modal: true, record})`), each from the dialog's own `ShowModal` override under `__EMSCRIPTEN__` (`LoadControlState()`, its own `TransferDataToWindow()`, the record, `CcHostPromptModal`; accepted `wxID_OK`, canceled `wxID_CANCEL`, not taken KiCad's own window):
- `convert` from `CONVERT_SETTINGS_DIALOG::ShowModal` (the class lives in `pcbnew/tools/convert_tool.cpp`, hooked there; shown by `CONVERT_TOOL::CreatePolys`, 632, with copy line width shown only for some sources and centerline and bounding hull always, and by `CONVERT_TOOL::CreateLines`, 1370, with no strategy at all). Record `{ strategies, strategy, gap, lineWidth, deleteOriginals }`: `strategies` the radio buttons the dialog shows, in order, from `copyLineWidth`, `centerline`, `boundingHull` (`CONVERT_STRATEGY`; the constructor's three show flags are kept in members under `__EMSCRIPTEN__`); `strategy` the selected one (KiCad's settings may select a strategy the dialog hides, as KiCad does); `gap`, `lineWidth` mm (the bounding hull's). Answer `{ strategy?, gap?, lineWidth?, deleteOriginals? }`, closed: a strategy not in `strategies` refuses with field `strategy`; lengths finite and >= 0. The radio set, then its own `onRadioButton`, the lengths, the box, then `TransferDataFromWindow` (fills the tool's `CONVERT_SETTINGS`), then `SaveControlState()`.
- `outset` from `DIALOG_OUTSET_ITEMS::ShowModal` (`pcbnew/dialogs/dialog_outset_items.cpp`; shown by `CONVERT_TOOL::OutsetItems`, `convert_tool.cpp` 1679). Record `{ outsetDistance, roundCorners, useSourceLayers, useSourceWidths, layer, layers, lineWidth, gridRounding, deleteSourceItems }` (`OUTSET_ROUTINE::PARAMETERS`): lengths mm, `gridRounding` mm or null (null when Round to grid is off), `layer` the selected layer's canonical name (`LSET::Name`) and `layers` the canonical names the dialog's layer list offers (the board's enabled layers, in KiCad's UI order). Answer the same keys but `layers`, closed: `layer` must be one of `layers`; `gridRounding` null turns rounding off, a number > 0 turns it on with that grid. KiCad's one refusal ("Line width must be a positive value.", a `DisplayErrorMessage` in its `TransferDataFromWindow`) is checked BEFORE anything is written, so no message box is ever raised: field `lineWidth`, KiCad's sentence as the message (through `CcPromptCaptureMessage`). Then the values through the dialog's own controls and handlers (`OnCopyLayersChecked`, `OnRoundToGridChecked`), `TransferDataFromWindow` (which also keeps KiCad's recent values lists), `SaveControlState()`.

**Files (kicad `seat/run15-e2`, commit 26d7b7e9):** `pcbnew/tools/convert_tool.cpp`, `pcbnew/dialogs/dialog_outset_items.{h,cpp}`. Everything under `#ifdef __EMSCRIPTEN__`. **Header change reach:** `dialog_outset_items.h` (host_prompt.h and the ShowModal declaration) is included by `dialog_outset_items.cpp` and `convert_tool.cpp` only.

**Probes (canvas-kinds fixture, Board, Edit mode, a listener that records and returns true):**
1. Select the two lines meeting at a corner, Convert, Create a polygon from the selection: ask `convert`, `strategies` `["copyLineWidth","centerline","boundingHull"]` (or without `copyLineWidth` where KiCad hides it); answer `{"strategy":"centerline","deleteOriginals":false}`: a polygon appears on the lines' layer, the lines stay; one Undo removes it. Wrong if: KiCad's window shows, or the lines are deleted.
2. Convert, Create lines from a polygon: ask `convert` with `strategies` `[]`; answer `{"deleteOriginals":true}`: the lines appear, the polygon goes, one undo step. `{"strategy":"centerline"}` here refuses with field `strategy`.
3. Select the Edge.Cuts rectangle, Create outsets: ask `outset`, `layer` `Edge.Cuts`-or-the-saved-layer, `layers` the board's enabled layers; answer `{"outsetDistance":0.5,"roundCorners":true}`: a rectangle with round corners 0.5 mm outside; Undo removes it.
4. Refusals: `{"lineWidth":0}` gives field `lineWidth` and message "Line width must be a positive value." with no message box; `{"layer":"In30.Cu"}` on a two layer board field `layer`; `{"gridRounding":0}` field `gridRounding`; `{"layers":[]}` field `layers`.
5. No listener: both KiCad dialogs show as before.

**Syntax check:** the run pasted under m6-e1 (`PASS kicad pcbnew/tools/convert_tool.cpp`, `PASS kicad pcbnew/dialogs/dialog_outset_items.cpp`, `syntax-check: 0 failed`).

## st-ap-e: K-AP, the auto-placer with no progress window, and its marker

**Export** `kicadAutoplaceQuiet() -> bool` in `cc_e2_pcb.inc`, function `cc_e2::autoplaceQuiet`, block `cc_e2_pcb`: a marker, always true (the island lists cap `autoplace` when the engine registers it). Registered in both pcbnew configurations (not merged image only).

**KiCad (kicad `seat/run15-e2`, commit e4659a03):** `pcbnew/autorouter/autoplace_tool.cpp`, `AUTOPLACE_TOOL::autoplace`: under `__EMSCRIPTEN__` the autoplacer gets `SetProgressReporter( nullptr )` in place of the `WX_PROGRESS_REPORTER` "Autoplace Footprints" (`AR_AUTOPLACER` checks the reporter before every report, `ar_autoplacer.cpp` 882 to 923; with none it cannot be aborted midway, as KiCad's own null reporter path). The board edges info bar error and the rest are KiCad's. No header changed.

**pcbjam:** commit e1073be (`cc_e2_pcb.inc`).

**Probes (Glasgow, Board, Edit mode):**
1. `kicadAutoplaceQuiet()` true.
2. Move two footprints off the board outline, press `pcbnew.Autoplacer.autoplaceOffboard`: no KiCad window of any kind (`visibleDialogs` empty throughout), the two footprints land inside the outline, one undo step "Autoplace Footprints". Select one footprint, `autoplaceSelected`: the same, for that one.
3. A board with no Edge.Cuts: KiCad's info bar "Board edges must be defined on the Edge.Cuts layer.", nothing placed, no window.
4. occ_service links with `kicadAutoplaceQuiet` registered.

**Syntax check:** the run pasted under m6-e1 (`PASS pcbnew`, `PASS occ`, `PASS kicad pcbnew/autorouter/autoplace_tool.cpp`, `syntax-check: 0 failed`).

## m3-e2: a new zone's settings ask the host (copper, non copper, rule area)

No export. Merged image only (`#ifdef KICAD_MERGED_EMBIND` in `cc_e2_pcb.inc`: the nonCopper and ruleArea records go through `cc_fp_items.h`, which is merged only, and the hook variable lives in a pcbnew dialog file; occ_service registers nothing for this unit and its zone editors show KiCad's dialogs as before).

**The hook (reported to E0, a shape call):** the ask must use records written in the board face (wave 1's `ccItemSheet`, `ccReadItemSet`, `ccApplyItemSet` in `pcbnew_embind.cpp`, and M4's zone kind through the item kinds table), which no KiCad file can reach. With no KiCad header in this lane for a setter (e7's `cc_host_design_block.h` pattern), the hook is a function pointer DEFINED in `pcbnew/dialogs/dialog_copper_zones.cpp` (`int ( *g_ccZoneSettingsHost )( const char* aKind, PCB_BASE_FRAME* aFrame, ZONE_SETTINGS* aSettings ) = nullptr;`, under `__EMSCRIPTEN__`) and declared `extern`, with the same line, in `dialog_non_copper_zones_properties.cpp`, `dialog_rule_area_properties.cpp` and `cc_e2_pcb.inc`; the inc sets it by a static initializer (`cc_e2::pcb::kZoneSettingsHost`). Null (nothing linked that sets it): KiCad's dialogs as before. If E0 prefers a header, the four declarations become one `include` of it; the behavior is unchanged.

**Host ask** `window.kicadHostPrompt.onAsk({token, ask: "zoneSettings", modal: true, record: { kind, sheet }})` from `cc_e2::pcb::zoneSettingsAsk`, called first in:
- `InvokeCopperZonesEditor` (`dialog_copper_zones.cpp`) only when `aZone` is null (a new zone): `kind` `copper`, board editor frame only (`ccApplyItemSet` is the board editor's; in the footprint editor KiCad's dialog shows). `sheet` is wave 1's zone sheet (`ccItemSheet`: `kind` `zone`, `locked`, `name`, `net`, `layers`, `priority`, `clearance`, `minWidth`, `padConnection`, `thermalGap`, `spokeWidth`, `netChoices`) with `uuid` `""`.
- `InvokeNonCopperZonesEditor` (`dialog_non_copper_zones_properties.cpp`): `kind` `nonCopper`; `InvokeRuleAreaEditor` (`dialog_rule_area_properties.cpp`): `kind` `ruleArea`. `sheet` is `cc_fpitems::ccFpItemJson( zone, (0,0), boardFrame true )`, the zone kind's record (FP 7.2 plus M4's non copper keys), `uuid` `""`. While no `zone` kind is registered in the table (e5's fp-e4b and e3's m4-e not merged) the ask is not made and KiCad's dialog shows.
Callers reached: `ZONE_CREATE_HELPER` (a zone drawn, `pcbnew/tools/zone_create_helper.cpp` 125 to 129), `CONVERT_TOOL::CreatePolys` to a zone (`convert_tool.cpp` 726 to 736), Duplicate Zone onto Layer (`board_editor_control.cpp` 1826 to 1830), `edit_zone_helpers.cpp` 51 to 62 and `footprint_editor_utils.cpp` 232 to 236 for the non copper and rule area kinds.

How: the settings are exported into a `ZONE` of the board that is never added to it (`ZONE_SETTINGS::ExportSetting`), its record read; the answer is applied to that zone; on an accepted answer the settings are imported back (`ZONE_SETTINGS::operator<<`), and the Invoke function returns `wxID_OK` (0: `wxID_CANCEL`; -1: KiCad's dialog). The caller then makes the zone from the settings in its own commit, as after KiCad's OK.

**Answer** `{ set }`, closed (any other key refuses with that key as `field`; `set` not an object refuses with `field` `set`):
- copper: each key of `set` is read whole first by `ccReadItemSet` as a one key set (board.item.set's zone keys `name`, `net`, `priority`, `clearance`, `minWidth`, `padConnection`, `thermalGap`, `spokeWidth`, `locked`, wave 1's bounds) plus `layers` (a non empty array of copper layer names the board enables; the layers a new zone needs, for Duplicate onto a layer), a bad one refused with its key as `field`; then each applied by `ccApplyItemSet`.
- nonCopper, ruleArea: `cc_fpitems::ccFpItemApply( zone, set, commit, (0,0), true )` inside a `BOARD_COMMIT` that is never pushed (the zone is not on the board; `COMMIT::~COMMIT` only frees its copies); refused: `field` `""`.

**Call, reported:** when the zone comes from Convert, KiCad's zone dialogs also show the conversion options (`aConvertSettings`); the ask does not carry them, so the conversion runs with the tool's current settings (the last ones the convert ask or KiCad's dialog set).

**Files:** kicad `seat/run15-e2` 552582c0 (`pcbnew/dialogs/dialog_copper_zones.cpp`, `dialog_non_copper_zones_properties.cpp`, `dialog_rule_area_properties.cpp`; everything under `__EMSCRIPTEN__`, no header changed); pcbjam 236665a (`cc_e2_pcb.inc`).

**Probes (canvas-kinds fixture, Board, Edit mode, a listener that records and returns true):**
1. Pencil case, Add a filled zone, draw a square: ask `zoneSettings`, `record.kind` `copper`, `record.sheet.uuid` `""`, `record.sheet.netChoices` the board's nets, no KiCad window. Answer `{"set":{"net":"GND","priority":1}}`: `{"accepted":true}`, the zone appears on the drawn layer with net GND and priority 1, one undo step; Undo removes it.
2. Right-click the copper zone, Duplicate the zone onto a layer: ask `copper` again (the source zone's settings); answer `{"set":{"layers":["B.Cu"]}}`: a copy on B.Cu. `{"set":{"layers":["F.SilkS"]}}` refuses with `field` `layers`; `{"set":{"net":"NOPE"}}` with `field` `net`; `{"sets":{}}` with `field` `sets`.
3. Convert the Edge.Cuts rectangle to a zone: ask `copper`; answer `{"set":{"net":"GND"}}`: a zone fills inside the rectangle; Undo removes it.
4. Once a `zone` kind is registered (e5, e3 merged): Add a rule area: ask `zoneSettings` `kind` `ruleArea`, the sheet M4's zone record; answer with M4's keys (for example its keepout flags): the area appears with them. Add a non copper zone: `kind` `nonCopper` likewise. Before that merge: KiCad's dialogs show for these two (expected, not a fault).
5. `kicadPromptCancel(token)`: no zone is made.
6. No listener: KiCad's three dialogs as before. The footprint editor's new copper zone: KiCad's dialog (expected).

**Syntax check** (`--kicad /home/matthew/cc-engine-wt/e2/kicad pcbnew occ --kicad-file pcbnew/dialogs/dialog_copper_zones.cpp --kicad-file pcbnew/dialogs/dialog_non_copper_zones_properties.cpp --kicad-file pcbnew/dialogs/dialog_rule_area_properties.cpp`):

```
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (12150 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (12035 ms)
PASS kicad pcbnew/dialogs/dialog_copper_zones.cpp (6938 ms)
PASS kicad pcbnew/dialogs/dialog_non_copper_zones_properties.cpp (4811 ms)
PASS kicad pcbnew/dialogs/dialog_rule_area_properties.cpp (5897 ms)
syntax-check: 0 failed
```

## Handover

Both worktrees merged `run15/main` (pcbjam 48e664e, kicad caf17fbe) before the checks. Units done: st-mw-e, m6-e2, m6-e1, m3-e2, m3-e1, st-ap-e. Reported to E0: `microwave_inductor.cpp` (st-mw-e, inside the unit's owned folder, not in the lane list); the `g_ccZoneSettingsHost` hook shape (m3-e2). Merged image only: m3-e2's zone ask. Learn cards owed: none (engine lane).

**Syntax check** (from this worktree, through the gate: `scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e2/kicad all --kicad-file` each of the 21 KiCad sources below):

```
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (13922 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (13753 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (8425 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (7927 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (1372 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (5511 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (13297 ms)
PASS kicad pcbnew/microwave/microwave_tool.cpp (7651 ms)
PASS kicad pcbnew/microwave/microwave_footprint.cpp (6672 ms)
PASS kicad pcbnew/microwave/microwave_inductor.cpp (6602 ms)
PASS kicad pcbnew/microwave/microwave_polygon.cpp (6100 ms)
PASS kicad pcbnew/dialogs/dialog_create_array.cpp (6712 ms)
PASS kicad pcbnew/tools/array_tool.cpp (7072 ms)
PASS kicad pcbnew/dialogs/dialog_position_relative.cpp (6367 ms)
PASS kicad pcbnew/tools/position_relative_tool.cpp (7034 ms)
PASS kicad pcbnew/dialogs/dialog_move_exact.cpp (4726 ms)
PASS kicad pcbnew/dialogs/dialog_offset_item.cpp (4563 ms)
PASS kicad pcbnew/dialogs/dialog_filter_selection.cpp (4915 ms)
PASS kicad pcbnew/dialogs/dialog_get_footprint_by_name.cpp (4406 ms)
PASS kicad pcbnew/tools/edit_tool.cpp (9610 ms)
PASS kicad pcbnew/tools/edit_tool_move_fct.cpp (8002 ms)
PASS kicad pcbnew/tools/pcb_selection_tool.cpp (10008 ms)
PASS kicad pcbnew/tools/convert_tool.cpp (8147 ms)
PASS kicad pcbnew/dialogs/dialog_outset_items.cpp (6074 ms)
PASS kicad pcbnew/dialogs/dialog_copper_zones.cpp (6476 ms)
PASS kicad pcbnew/dialogs/dialog_non_copper_zones_properties.cpp (5317 ms)
PASS kicad pcbnew/dialogs/dialog_rule_area_properties.cpp (6721 ms)
PASS kicad pcbnew/autorouter/autoplace_tool.cpp (6901 ms)
syntax-check: 0 failed
```

**Export check** (`scripts/kicad/check-run15-exports.sh --since run15/main --kicad /home/matthew/cc-engine-wt/e2/kicad --kicad-since run15/main`):

```
check-run15-exports: 156 names registered in the merged image (149 kicad names), 11 of 110 run 15 names, 0 failures
```

## Repair e2-1 (2026-11-19): the array ask's keys take the American spelling

Finding (`/home/matthew/cc-wave2/engine/fix/e2-1.md`, the island's stage close): the Create
Array record of m6-e2 named three keys of ours in the British spelling, and the island's
spelling guard is red on the island files that repeat them.

Verified in the code: the keys were written by the record builder and read by the answer reader
of `DIALOG_CREATE_ARRAY`, `pcbnew/dialogs/dialog_create_array.cpp` (the `__EMSCRIPTEN__` block at
the end of the file): `grid["centred"]`, `circular["centreX"]`, `circular["centreY"]`, the
`ccOnlyKeys` lists of both halves and the `ccReadBool` and `ccReadNumber` calls. No other file of
the lane used them (`cc_e2_pcb.inc` holds none).

Change, kicad commit 9c3f1e53: the three keys are `centerX`, `centerY` and `centered`
everywhere the record is written and the answer read, with no alias (an answer that still sends
an old key is refused by `ccOnlyKeys` as any unknown key is). Our own locals and two of our
comments follow; KiCad's own identifiers (`m_rbCentreOnSource`, `m_hCentre`, `m_vCentre`,
`m_centred`, `m_centre`) stay. Dated "Modified" line added in the file's notice.

Syntax check (`/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad
/home/matthew/cc-engine-wt/e2/kicad all --kicad-file pcbnew/dialogs/dialog_create_array.cpp`):

```
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (19317 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (18721 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (9898 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (13462 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (7298 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (7045 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (18047 ms)
PASS kicad pcbnew/dialogs/dialog_create_array.cpp (8601 ms)
syntax-check: 0 failed
```

Export check (`scripts/kicad/check-run15-exports.sh --join --kicad
/home/matthew/cc-engine-wt/e2/kicad`):

```
check-run15-exports: 256 names registered in the merged image (249 kicad names), 111 of 111 run 15 names, 0 failures
```

The probe for the next build: the array ask on a selected item carries `grid.centered`,
`circular.centerX` and `circular.centerY` (and no British key); an answer using them (for
example `{"shape":"circular","circular":{"centerX":<x>,"centerY":<y>,"count":6,"angle":60}}`
on the build T6 fixture) makes the same array build T6 made; an answer with `centreX` is refused
as an unknown key.

## Repair e2-2 (2026-11-19): an accepted answer to a modeless ask no longer traps

The fault (fix note e2-2, build T9 and T6): `kicadPromptAnswer` on an accepted answer to the
modeless `array` or `positionRelative` ask threw `SuspendError: trying to suspend without
WebAssembly.promising` and made nothing. Cause: the receivers ran the accepting step inside the
export's own plain stack. For the array, `TransferDataFromWindow` called `Close()`, whose
`wxEVT_CLOSE_WINDOW` ran `ARRAY_TOOL::onDialogClosed` and its `BOARD_COMMIT::Push` at once; for
Position Relative, `CcApply` called `OnOkClick`, whose `RelativeItemSelectionMove` pushes the
commit. A commit may suspend, which a plain embind call cannot do. The modal asks were never
affected: their answer only fills the controls and the parked `ShowModal` resumes on its own
promising chain.

The fix (kicad `pcbnew/dialogs/dialog_create_array.cpp`, `pcbnew/dialogs/dialog_position_relative.cpp`,
both under `__EMSCRIPTEN__`, a dated "Modified" line in each notice): validation and refusal stay
synchronous; the accepting step is queued with `CallAfter` on the dialog, which the port's
promising top level tick (`wxWasmTopLevelTick`, `ProcessPendingEvents`) delivers, the same lane
`pcbjam_collab::runOnCoroutine` queues on, where KiCad's commit may wait as after a press of OK.
- Array: `TransferDataFromWindow` closes with `CallAfter( Close )` while `CcPromptCapturing()`
  (only while a host answer is applied), else `Close()` as before. `CcApply` still erases the
  token before returning, so the export answers `{"accepted":true}`, no `onEnd` follows the
  accept (the destructor's `CcHostPromptForget` finds nothing), and a second answer is `stale`.
- Position Relative: `CcApply` writes the controls, runs `getTranslationInIU` (OnOkClick's own
  check; a failure refuses with the field), ends the prompt and hides the dialog, then queues
  `OnOkClick` (one commit "Position Relative") with `CallAfter`.
No export, marker or host ask changed; `PCBJAM_PARKER_POLICY` untouched; host_prompt.h (e1's)
untouched. Kicad commit 2e703f5e.

Syntax check (`/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad
/home/matthew/cc-engine-wt/e2/kicad all --kicad-file pcbnew/dialogs/dialog_create_array.cpp
--kicad-file pcbnew/dialogs/dialog_position_relative.cpp`):

```
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (19867 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (18330 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (9982 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (12904 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (6971 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (7632 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (18494 ms)
PASS kicad pcbnew/dialogs/dialog_create_array.cpp (9281 ms)
PASS kicad pcbnew/dialogs/dialog_position_relative.cpp (8291 ms)
syntax-check: 0 failed
```

Export check (`scripts/kicad/check-run15-exports.sh --since run15/main --kicad
/home/matthew/cc-engine-wt/e2/kicad --kicad-since run15/main`):

```
check-run15-exports: 256 names registered in the merged image (249 kicad names), 111 of 111 run 15 names, 0 failures
```

The probe for the next build (the fix note's): the grid answer
`{"shape":"grid","grid":{"nx":3,"ny":2,"dx":2.54,"dy":2.54}}` and the circular answer
`{"shape":"circular","circular":{"centerX":<record x + 5>,"centerY":<record y>,"count":6,"angle":60}}`
each answer `{"accepted":true}` with no exception; the grid makes six footprints in one undo
step and one Undo returns to three; the circular ring is one step; Position Relative
`{"x":5,"y":0}` moves R1 in one "Position Relative" step; a second answer to the same token
answers `{"stale":true}`; no KiCad window shows. The array is made a tick after the answer
returns, so the probe waits for the footprint count (or the undo depth) to change rather than
reading it in the same task as the answer.
