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
