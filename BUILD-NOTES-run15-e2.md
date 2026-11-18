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
- `window.kicadHostPrompt.onAsk({token, ask: "array", modal: false, record})` from `DIALOG_CREATE_ARRAY::Show( true )` (`pcbnew/dialogs/dialog_create_array.cpp`, the `__EMSCRIPTEN__` block at the end; called by `ARRAY_TOOL::CreateArray`, `pcbnew/tools/array_tool.cpp` 104, unchanged). Record `{ shape: "grid" | "circular", arrangeSelection, uniqueReferences, padNumbering, grid: { nx, ny, dx, dy, offsetX, offsetY, centred, stagger, staggerRows, horizontalThenVertical, reverseNumberingAlternate, numberingStartSpecified, numbering2d, primary, secondary }, circular: { centreX, centreY, angle, angleOffset, clockwise, count, rotateItems, numberingStartSpecified, axis } }`, an axis `{ type: "numeric" | "hex" | "alphaShort" | "alphaFull", start: string, step }`; lengths mm (board coordinates for the centre), angles degrees; `padNumbering` is the dialog's `m_isFootprintEditor` (true in the footprint editor frame).
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
2. Again, `shape` circular: `kicadPromptPick(token, "point")` returns 0, a click on U1 pad 1, `onUpdate` with `circular.centreX/centreY` at that point; answer `{"shape":"circular","circular":{"count":6,"angle":60}}`: a ring of six.
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
