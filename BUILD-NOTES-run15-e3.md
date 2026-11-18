<!-- Copyright (C) 2026 Circuit Center Inc -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Engine run 15, lane e3: footprint editor and board item asks

Branches `seat/run15-e3` in pcbjam (`/home/matthew/cc-engine-wt/e3/pcbjam`) and kicad
(`/home/matthew/cc-engine-wt/e3/kicad`). Units in order: fp-e1a, fp-e1b, fp-e1c, m4-e, then the
conditional fp-f1 and flt-ds4-e. The unit notes the manifest names `BUILD-NOTES-run15-<unit>.md`
are the sections of this one file.

## Early commit

kicad `426e5044`: `pcbnew/tools/pad_tool.h` declares, under `__EMSCRIPTEN__`, the public
`int PAD_TOOL::enumerateLoop( const SEQUENTIAL_PAD_ENUMERATION_PARAMS& aParams )` with a forward
declaration of the struct (for e4's `kicadFpEditEnumerate`). The body is fp-e1b's. Recompile reach:
`pad_tool.h` is included by the pcbnew tools that use PAD_TOOL (edit_tool.cpp, pad_tool.cpp,
footprint_edit_frame.cpp, pcb_edit_frame.cpp, tools setup and a few more); the train takes it.

## fp-e1a: the footprint editor asks the page

Host object `window.kicadHostFpEdit.onRequest( JSON {ask, uuid, isNew} )`, helper
`CcAskHostFpEdit( const char* ask, const KIID* uuid, bool isNew )` in the NEW header
`pcbnew/tools/footprint_editor_host.h` (inline, `__EMSCRIPTEN__` only, the run 14 K3 shape: a
missing listener, a falsy answer or a throwing listener answers false and KiCad's path runs).

Asks (all `isNew` false here):

| ask | uuid | KiCad site |
|---|---|---|
| `pad`, `field`, `text`, `textbox`, `shape`, `dimension`, `zone`, `table`, `barcode`, `image`, `point` | the item's (a table cell asks `table` with its table's uuid) | `EDIT_TOOL::Properties`, `pcbnew/tools/edit_tool.cpp`, the `m_isFootprintEditor` block after run A's board ask (helper `fpEditAskSubject`) |
| `footprint` | "" | the same block when the footprint itself is selected; `FOOTPRINT_EDITOR_CONTROL::Properties` (`footprint_editor_control.cpp`, first lines; also reached from E on nothing through `footprintProperties`) |
| `padDefaults` | "" | `FOOTPRINT_EDITOR_CONTROL::DefaultPadProperties` |
| `wizard` | "" | `FOOTPRINT_EDITOR_CONTROL::CreateFootprint` (before the unsaved changes box) |
| `newFootprint` | "" | `FOOTPRINT_EDITOR_CONTROL::NewFootprint` (before `Clear_Pcb( true )`) |
| `rename` | "" | `FOOTPRINT_EDITOR_CONTROL::RenameFootprint` |
| `cleanup` | "" | `FOOTPRINT_EDITOR_CONTROL::CleanupGraphics` |

A held item ask keeps the asked item the one selected, no longer a hover selection (run A's rule);
a held frame ask returns before anything changes (no unsaved changes box, nothing cleared).
A group in the footprint editor is not asked (no FP form for it this wave) and opens KiCad's dialog.

Marker: `kicadHostFpEdit` (`bool ()`, always true) in `cc_e3_dispatch.inc`, block
`EMSCRIPTEN_BINDINGS( cc_e3_dispatch )`. Not merged image only (the dispatcher face is the
kicad_editor TU).

Commits: kicad `70489094` (footprint_editor_host.h new, edit_tool.cpp, footprint_editor_control.cpp);
pcbjam `0a74f1a` (cc_e3_dispatch.inc).

Syntax check (`/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad
/home/matthew/cc-engine-wt/e3/kicad kicad_editor --kicad-file pcbnew/tools/edit_tool.cpp
--kicad-file pcbnew/tools/footprint_editor_control.cpp`):

```
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (6187 ms)
PASS kicad pcbnew/tools/edit_tool.cpp (17743 ms)
PASS kicad pcbnew/tools/footprint_editor_control.cpp (9176 ms)
syntax-check: 0 failed
```

What the build train must probe (fixture `island:tests/fixtures/fpedit-small/`, the SOIC open in
`?frame=fpedit`, a listener `window.kicadHostFpEdit = { onRequest: r => (log.push(JSON.parse(r)), true) }`):
- `Module.kicadHostFpEdit()` is `true`.
- Select a pad, a silk line, a text, the Reference field; press E each time: one request each,
  `{ask: "pad" | "shape" | "text" | "field", uuid: <the item's uuid, lowercase>, isNew: false}`, no
  wx dialog exists in the frame, the item stays selected; undo depth unchanged (0 new steps).
- E on empty canvas (nothing selected): `{ask: "footprint", uuid: "", isNew: false}`.
- `pcbnew.PadTool.defaultPadProperties`, `pcbnew.ModuleEditor.createFootprint`,
  `.newFootprint`, `.renameFootprint`, `pcbnew.GlobalEdit.cleanupGraphics` run through
  `key.press` or `kicadRunAction` with the island's hold lifted: `padDefaults`, `wizard`,
  `newFootprint`, `rename`, `cleanup` in turn, no dialog, the footprint unchanged and unmodified.
- With the listener answering `false` (or removed): E on a pad opens KiCad's pad dialog (proves
  the fallthrough; close it by Esc). A listener that throws logs `[cc fpedit] onRequest listener
  threw` and KiCad's dialog opens.
- Wrong if: any wx dialog with the listener answering true; a request with `uuid` not lowercase
  8-4-4-4-12 for an item ask; two requests for one press; the selection lost after a held ask.

## fp-e1b: the pad tool asks the page; Enumerate Pads split

No export. Asks on `window.kicadHostFpEdit` (fp-e1a's helper), `pcbnew/tools/pad_tool.cpp`:

| ask | uuid | isNew | site |
|---|---|---|---|
| `pushPad` | the source pad's | false | `PAD_TOOL::pushPadSettings`, footprint editor only, before `DIALOG_PUSH_PAD_PROPERTIES` (the board frame keeps KiCad's dialog) |
| `enumerate` | "" | false | `PAD_TOOL::EnumeratePads`, after its own checks, in place of `GetSequentialPadNumberingParams`'s dialog; held: return 0, the loop does NOT start |
| `padTable` | "" | false | `PAD_TOOL::PadTable`, before `DIALOG_FP_EDIT_PAD_TABLE` |

The split: `int PAD_TOOL::enumerateLoop( const SEQUENTIAL_PAD_ENUMERATION_PARAMS& aParams )`
(public, declared in the early commit). The loop waits on tool events, so it must run as the
action's own handler (a tool coroutine): `enumerateLoop` stores the numbers in a file static
(`s_ccHostEnumParams`) and runs `PCB_ACTIONS::enumeratePads` synchronously; `EnumeratePads` takes
the stored numbers (and clears them) in place of the ask and the dialog, then runs KiCad's own
loop unchanged (one commit "Renumber Pads" on double click or tool change, Esc reverts).
`enumerateLoop` answers 1 when the loop started with those numbers, 0 when the action refused
(not the footprint editor, pad edit mode, no footprint or no pads), and then nothing changed and
nothing is left stored. e4's `kicadFpEditEnumerate` calls it (it may call it from the apply
coroutine or directly: `RunAction` from either starts the handler's own coroutine).

Header: `pad_tool.h` (the early commit, `426e5044`) is the only header change of this unit; this
commit touches `pad_tool.cpp` alone.

Commit: kicad `cf1d7365`. Syntax check (`... --kicad /home/matthew/cc-engine-wt/e3/kicad
--kicad-file pcbnew/tools/pad_tool.cpp --kicad-file pcbnew/tools/drawing_tool.cpp`):

```
PASS kicad pcbnew/tools/pad_tool.cpp (76618 ms)
PASS kicad pcbnew/tools/drawing_tool.cpp (13606 ms)
syntax-check: 0 failed
```

What the build train must probe (fpedit-small's SOIC in `?frame=fpedit`, listener answering true):
- Select pad 1, run `pcbnew.PadTool.PushPadSettings`: one request `{ask: "pushPad", uuid: <pad 1>,
  isNew: false}`, no dialog, undo depth unchanged.
- Run `pcbnew.PadTool.enumeratePads`: one request `{ask: "enumerate", uuid: "", isNew: false}`,
  no dialog, the cursor is NOT the bullseye, a click on a pad changes nothing (the loop did not
  start).
- Run `pcbnew.PadTool.padTable` (hold lifted): `{ask: "padTable", uuid: "", isNew: false}`, no dialog.
- Through e4's `kicadFpEditEnumerate( {"start":1,"step":1,"prefix":""} )`: the bullseye cursor,
  NO enumerate request, a drag across three pads numbers them 1, 2, 3, a double click commits:
  undo depth +1, Ctrl+Z restores the old numbers in one step. With `prefix: "A"` and `step: 2`:
  A1, A3, A5.
- Wrong if: the loop starts after a held enumerate; `kicadFpEditEnumerate` opens KiCad's
  numbering dialog; two undo steps for one numbering run; a second `enumerateLoop` call after a
  refused one (no pads) uses the first call's numbers.

## fp-e1c: the drawing tool asks the page

No export. `pcbnew/tools/drawing_tool.cpp`, a file static `ccAskHostNewItem( footprintEditor, ask,
item )` (the footprint editor asks `window.kicadHostFpEdit` with `isNew` true; m4-e adds the board
frame's branch to the same helper):

| ask | uuid | site | held |
|---|---|---|---|
| `image` (isNew) | "" | `PlaceReferenceImage`, the click before the file box | nothing placed, the tool ends (PopTool); the page places the picture with its own export |
| `text` (isNew) | the new text's | `PlaceText`, before `DIALOG_TEXT_PROPERTIES` | the text reads "Text" and is committed at the click point (one commit "Draw Text", selected) |
| `textbox` (isNew) | the new text box's | `DrawRectangle` (text box mode), before `ShowTextBoxPropertiesDialog` | it reads "Text" and is committed as drawn |
| `table` (isNew) | the new table's | `DrawTable`, the second click, before `DIALOG_TABLE_PROPERTIES` | committed as drawn with KiCad's rows and columns ("Draw Table"), point editor on |
| `barcode` (isNew) | the new barcode's | `DrawBarcode`, before `DIALOG_BARCODE_PROPERTIES` | committed with its defaults (QR, 40 mm, no text) at the click ("Draw Barcode") |
| `graphics` | "" | `PlaceImportedGraphics`, footprint editor only, before `DIALOG_IMPORT_GRAPHICS` | return 0, no dialog; the board frame's door is the island's held action `pcbnew.InteractiveDrawing.placeImportedGraphics` |

The text box ask is an addition to the spec's list (the footprint host already reads `textbox`);
it keeps KiCad's text box dialog from ever showing in either frame once m4-e lands.
Every changed site keeps KiCad's own lines word for word in an `#else`.

Commit: kicad `5efb04b7`. Syntax check: the PASS line for drawing_tool.cpp above.

What the build train must probe (`?frame=fpedit`, SOIC, listener answering true):
- Picture tool, click: `{ask: "image", uuid: "", isNew: true}`, no file box, nothing placed, the
  tool is no longer active; undo depth unchanged.
- Text tool, click: `{ask: "text", uuid: <u>, isNew: true}`, no dialog, one new text reading
  `Text` at the click, undo depth +1, `<u>` is that text's uuid.
- Text box tool, drag: `textbox` with the new uuid, it reads `Text`, depth +1.
- Table tool, two clicks: `table` with the new table's uuid, one table placed, depth +1.
- Barcode tool, click: `barcode`, placed, depth +1.
- Import Graphics (`pcbnew.InteractiveDrawing.placeImportedGraphics` with the hold lifted):
  `{ask: "graphics", uuid: "", isNew: false}`, no dialog.
- With the listener answering false each opens KiCad's own dialog (proves the fallthrough).
- Wrong if: any dialog with a listener answering true; a held text with empty text (NoPrintableChars
  would drop it); a held picture leaving the tool waiting for a click.

## m4-e: board item records and asks

Relay note: the seat before this one was cut off by the machine's memory fault after it wrote an
untracked first draft of `pcbnew/tools/board_item_host.h` (nothing else of m4-e was on disk). The
draft was read whole, matched the spec (addendum 3.2) and was kept; its tuning declarations were
reshaped (below). Nothing was stashed.

### KiCad (kicad `seat/run15-e3`)

Host object `window.kicadHostBoardItem.onRequest( JSON {kind, uuid, isNew} )`, helper
`CcAskHostBoardItem( const char* kind, const KIID* uuid, bool isNew )` in the NEW header
`pcbnew/tools/board_item_host.h` (inline, `__EMSCRIPTEN__` only, fp-e1a's shape: a missing
listener, a falsy answer or a throwing listener answers false and KiCad's dialog opens; a throw
logs `[cc boarditem] onRequest listener threw`).

| ask (kind) | uuid | isNew | KiCad site |
|---|---|---|---|
| `pad`, `shape`, `dimension`, `textbox`, `barcode`, `image`, `point`, `target`, `group`, `zone` | the FIRST selected item's | false | `EDIT_TOOL::Properties` (`pcbnew/tools/edit_tool.cpp`), board branch, AFTER run A's ask (`boardSelectionAskKind`: one or more items, all of one kind; `zone` is a rule area or keepout, a zone on no copper layer, or a footprint's zone: a board copper zone stays run A's). Held: no dialog, the selection kept, no longer a hover selection. |
| `tuning` | the placed pattern's | false | `PCB_TUNING_PATTERN::ShowPropertiesDialog` (`pcbnew/generators/pcb_tuning_pattern.cpp`, first lines; E on a placed pattern reaches it through `OnEditItemRequest`). Held: no dialog. |
| `tuning` | "" | false | `DRAWING_TOOL::PlaceTuningPattern`'s loop, the `properties` / `lengthTunerSettings` branch (E and Ctrl+L while tuning), in place of `DIALOG_TUNING_PATTERN_PROPERTIES`. Held: no dialog. |
| `text`, `textbox`, `barcode` | the new item's | true | `DRAWING_TOOL` board frame: fp-e1c's `ccAskHostNewItem` now asks `kicadHostBoardItem` outside the footprint editor. Held: the item goes down as the dialog's OK with defaults (a text reading "Text"; one commit). |
| `image` | "" | true | `DRAWING_TOOL::PlaceReferenceImage`, the click before the file box. Held: nothing placed, the tool ends; the page places with `kicadBoardPlaceImage`. |

Not asked on the board (RULINGS 2 and the unit's merger note): a table and a table cell (KiCad's
table dialogs, whose ShowModal m12-e turns into prompt asks); the new table in `DrawTable`
(`ccAskHostNewItem` answers false for `table` outside the footprint editor). A group reached by
its context menu's "Edit Group Properties" (`ACTIONS::groupProperties` from `pcb_group_tool`, not
a file of this lane) still opens KiCad's group dialog; E on a group asks.

The tuning tool's live settings: `DRAWING_TOOL::PlaceTuningPattern` publishes its loop's
`meanderSettings`, router mode, `m_tuningPattern` and DRC engine in a file static
(`s_ccTuningLive`, cleared by a scope guard on every exit). `CcTuningLiveSet` stores the set and
POSTS `PCB_ACTIONS::lengthTunerSettings`; the loop's own branch takes the stored set in place of
the ask and the dialog, applies it as `TransferDataFromWindow` does (`ccTuningApplySet`), runs
KiCad's `applyCommonSettings` and `updateTuningPattern`, then calls the export's callback with the
settings it now holds. A posted action is no hotkey, so the island's held Ctrl+L does not catch it.

`board_item_host.h` also declares (defined in `pcb_tuning_pattern.cpp`): `CcTuningLiveGet()`,
`CcTuningSetValid( json )`, `CcTuningLiveSet( json, std::function<void( const json& )> )`,
`CcTuningPatternRecord( BOARD_ITEM* )`, `CcTuningPatternApply( PCB_BASE_EDIT_FRAME*, BOARD_ITEM*,
json, BOARD_COMMIT& )` (the placed pattern's dialog OK into the caller's commit: `Modify`,
`m_settings`, `EditStart`, `Update`, `EditFinish`, as `ShowPropertiesDialog` does).

TuningSettings: `{mode ("single" | "diffPair" | "skew"), minAmplitude, maxAmplitude, spacing (mm),
cornerStyle ("chamfer" | "round"), cornerRadiusPercent (0 to 100), singleSided, timeDomain,
overrideCustomRules, target {min, opt, max} (mm, or ps when timeDomain; the skew target in skew
mode; all null when unconstrained), fromRules ({min, opt, max} of the rule KiCad's dialog is given,
or null)}`. A set: any key but `mode` and `fromRules`; `target` replaced whole (`opt` null =
unconstrained; `min`, `max` only beside an `opt`; at most 2,000 mm or ps); the target lands in the
domain in effect and the other domain's target is left unconstrained, as the dialog's two fields.
A placed pattern's record is the same object plus `kind: "tuning"`, `uuid`, `layer` (canonical),
its settings as its dialog shows them (the rule's target when not overridden). The spec's
`end: 'none' | 'start' | 'end'` names no field KiCad's dialog or pattern holds: left out and
reported.

Header reach: `board_item_host.h` is new and included by `edit_tool.cpp`, `drawing_tool.cpp`,
`pcb_tuning_pattern.cpp` and the pcbjam board face only. No existing KiCad header changed.

### pcbjam (`wasm/bindings/cc_e3_pcb.inc`, block `EMSCRIPTEN_BINDINGS( cc_e3_pcb )`)

| export | signature | merged image only | body |
|---|---|---|---|
| `kicadHostBoardItems` | `bool ()` | no | marker, always true |
| `kicadBoardItemRecords` | `std::string (std::string)` | YES (`cc_fpitems`) | `{uuids[1..512]}` lowercase; `{items: BoardRecordSheet[]}` in the order asked, a uuid not held, asked twice or of no listed kind left out; "" for any other shape |
| `kicadBoardItemRecordsSet` | `bool (std::string)` | YES | `{run int>=0, dir "/tmp/cc-boarditems/<n>", uuids[1..4096] unique, kind, set}`; every uuid an item of that kind, checked before queuing; ONE commit "Edit" on the apply coroutine, reverted whole if any item's kind refuses; record `<dir>/items.json {run, applied, depth}` |
| `kicadBoardTuningGet` | `std::string ()` | no | the live settings, "" with no tuning tool |
| `kicadBoardTuningSet` | `bool (std::string)` | no | `{run, dir "/tmp/cc-boarditems/<n>", set}`; handed to the tool's loop; record `<dir>/tuning.json {run, settings}`; no undo step |
| `kicadBoardPlaceImage` | `bool (std::string)` | no | `{path "/tmp/cc-stage/<n>/<file>"}`; false when the file does not read as a picture or a tool is on the stack; KiCad's `placeReferenceImage` run with the picture as its parameter (flags new and moving, the active layer), on the pointer; KiCad's commit "Place Image" on the press |

Kinds (`boardRecordKind`): `pad`, `shape`, `dimension`, `textbox`, `table`, `barcode`, `image`,
`point`, `zone` (not a board copper zone) through the item table (`ccFpItemJson`,
`ccFpItemApply`, origin (0, 0), `aBoardFrame` true); `group` (`{name, locked, members,
designBlock}`; set `name`, `locked`, and `designBlock`, which `DIALOG_GROUP_PROPERTIES` also sets:
ADDED to 3.5's set keys; a link LIB_ID cannot parse is refused as the dialog refuses it),
`target` (`{layer, x, y, shape "plus" | "x", size, width}`; set `shape`, `size` (at least 1 mil,
the dialog's floor), `width` as `DIALOG_TARGET_PROPERTIES`, plus `x`, `y`, `layer`), `tuning`.
Not answered: a table cell (M12, RULINGS 2). A pad's sheet: `net` its net ("" none),
`netChoices` every net sorted; a pad set's `net` is a name the board holds or "" (no net), set
through `SetNet` / `SetNetCode( UNCONNECTED )` in the same commit.

Keys `DIALOG_NON_COPPER_ZONES_PROPERTIES::TransferDataFromWindow` sets that a board non copper
zone's record needs (e5's zone row, FP-I2's section; reported, not written here): layers, corner
smoothing type and radius, minimum width, locked, border display style and hatch pitch, fill mode
(solid or hatched), hatch orientation, hatch thickness, hatch gap, hatch smoothing level and value.

Reported to E0: (1) the `textbox` kind row is not used for table cells (cells left to M12);
(2) e5's zone row should carry the non copper zone keys above for the board; (3) the spec's
tuning `end` key has no KiCad field.

Commits: kicad `2ec15417` (board_item_host.h new, edit_tool.cpp, drawing_tool.cpp,
pcb_tuning_pattern.cpp); pcbjam `9c164d6` (cc_e3_pcb.inc).

Syntax check (`/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad
/home/matthew/cc-engine-wt/e3/kicad pcbnew occ --kicad-file pcbnew/tools/edit_tool.cpp
--kicad-file pcbnew/tools/drawing_tool.cpp --kicad-file pcbnew/generators/pcb_tuning_pattern.cpp`):

```
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (15334 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (13898 ms)
PASS kicad pcbnew/tools/edit_tool.cpp (11578 ms)
PASS kicad pcbnew/tools/drawing_tool.cpp (8875 ms)
PASS kicad pcbnew/generators/pcb_tuning_pattern.cpp (8845 ms)
syntax-check: 0 failed
```

Link note: `kicadBoardItemRecords` and `kicadBoardItemRecordsSet` call `cc_fpitems::ccFpItemJson`
and `ccFpItemApply` (lane e4's definitions, fpedit TU) and link only once e4's item table is
merged; until a kind row is registered its items are left out of the records and a set on them
answers `applied: false`.

What the build train must probe (fixture `island:tests/fixtures/items-board/` in `/viewer`, Edit
mode, Board tab; listener `window.kicadHostBoardItem = { onRequest: r => (log.push(JSON.parse(r)), true) }`):
- `Module.kicadHostBoardItems()` is `true`.
- E on a pad, a silk line, a dimension, a text box, a barcode, a picture, a keepout, a point, a
  target, a group: one request each `{kind, uuid: <the item's>, isNew: false}`, no wx dialog, the
  item still selected, undo depth unchanged. Two pads selected, E: ONE request, the first pad's
  uuid. A pad and a line selected: no request (KiCad's path). E on a footprint, a track, a board
  copper zone, a board text: run A's `kicadProperties` request, NO board item request.
- E on a table or a table cell: no board item request (M12's prompt asks).
- Text tool, click: `{kind: "text", uuid: <new>, isNew: true}`, no dialog, a text reading `Text`,
  depth +1. Text box, barcode the same; picture tool click: `{kind: "image", uuid: "", isNew: true}`,
  no file box, nothing placed, the tool ended.
- `kicadBoardItemRecords('{"uuids":[pad, dimension, group, tuning, target, cell]}')`: the pad's
  sheet with `net` and a sorted `netChoices`, `parent`/`parentRef` its footprint; the group
  `{name, locked, members, designBlock}`; the tuning pattern's settings; the cell LEFT OUT.
  `'{"uuids":[]}'` answers "".
- `kicadBoardItemRecordsSet` `{run:1, dir:"/tmp/cc-boarditems/1", uuids:[pad], kind:"pad",
  set:{net:"GND"}}`: true; `/tmp/cc-boarditems/1/items.json` `{run:1, applied:true, depth:d+1}`,
  the pad on GND; Ctrl+Z restores it in one step. `set:{net:"NO_SUCH"}` answers false. A group
  `{name:"G2", locked:true}` one step; a target `{shape:"x", size:2}` one step; `{size:0}` false.
- Start the tuning tool on a track: `kicadBoardTuningGet()` is the settings JSON (mode
  `single`); with no tuning tool it is "". Ctrl+L with the listener: `{kind:"tuning", uuid:"",
  isNew:false}`, no dialog. `kicadBoardTuningSet({run:2, dir:"/tmp/cc-boarditems/2",
  set:{maxAmplitude:2, cornerStyle:"round"}})`: true, the preview pattern redraws, `tuning.json`
  holds the new settings, undo depth unchanged; `set:{mode:"skew"}` answers false. E on a placed
  pattern: `{kind:"tuning", uuid:<pattern>}`, no dialog; a records set on it with
  `{spacing:1}` rebuilds it in one undo step.
- `kicadBoardPlaceImage({path:"/tmp/cc-stage/3/a.png"})`: true, the picture on the pointer, a
  click places it ("Place Image", depth +1); a text file answers false; a path outside
  `/tmp/cc-stage/` answers false.
- Wrong if: a wx dialog with the listener answering true; two requests for one E; a set that
  changes some items when one refuses; a tuning set adding an undo step; the tool stack left
  busy after a placed picture.

## Handover

`git merge run15/main` in both worktrees (pcbjam `8845342`, kicad `4e6fd5c0`), then:

`/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad
/home/matthew/cc-engine-wt/e3/kicad all --kicad-file pcbnew/tools/edit_tool.cpp --kicad-file
pcbnew/tools/drawing_tool.cpp --kicad-file pcbnew/generators/pcb_tuning_pattern.cpp --kicad-file
pcbnew/tools/pad_tool.cpp --kicad-file pcbnew/tools/footprint_editor_control.cpp`:

```
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (14086 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (13897 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (8484 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (8116 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (1205 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (5735 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (13562 ms)
PASS kicad pcbnew/tools/edit_tool.cpp (10771 ms)
PASS kicad pcbnew/tools/drawing_tool.cpp (9134 ms)
PASS kicad pcbnew/generators/pcb_tuning_pattern.cpp (9089 ms)
PASS kicad pcbnew/tools/pad_tool.cpp (7151 ms)
PASS kicad pcbnew/tools/footprint_editor_control.cpp (7938 ms)
syntax-check: 0 failed
```

`scripts/kicad/check-run15-exports.sh --since run15/main --kicad /home/matthew/cc-engine-wt/e3/kicad
--kicad-since run15/main`: `check-run15-exports: 161 names registered in the merged image (154
kicad names), 16 of 110 run 15 names, 0 failures`.

Held, not built (RULINGS 5): fp-f1 waits for fp-e0's gate item 1 to ask for it; flt-ds4-e waits
for flt-ds4-i's diagnosis to name a pcbnew file (cause (b) or (c) of addendum 13.1). Neither
diagnosis existed under `/home/matthew/cc-wave2/reports/` or `plan/` at this handover.
