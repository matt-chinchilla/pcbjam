<!-- Copyright (C) 2026 Circuit Center Inc -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Build notes, run 15, train 17, group C (items 6, 7, 5)

Worktrees `/home/matthew/cc-engine-wt/t17-c/kicad` and `/home/matthew/cc-engine-wt/t17-c/pcbjam`, branch `seat/t17-c`, cut from `run15/main` (kicad 3e13bf12, pcbjam 772f9c2). Nothing pushed. No header changed; every helper is `static` in its `.cpp`.

## Item 6 (t17-6): Text, Table, Barcode not primed (kicad 8afcab1a)

- `pcbnew/tools/drawing_tool.cpp`: new `static bool ccHostTakesNewItems( bool aFootprintEditor )` beside `ccAskHostNewItem` (reads `window.kicadHostFpEdit.onRequest` in the footprint editor, `window.kicadHostBoardItem.onRequest` in the board editor, by `EM_ASM_INT`, never calls it). `PlaceText` skips both prime branches (the event position and the `immediate_actions` {0,0}) when it answers true; `DrawTable` and `DrawBarcode` skip their prime. `PlaceReferenceImage` unchanged (R3, RULINGS 29 (d)). `#else` keeps KiCad's lines.
- Probe: `fpedit-small`, pointer on the drawing, `key.press {key:"T",code:"KeyT",ctrl:true,shift:true}`: no `ev.fpedit`, `kicadFpEditItemsGet {"kinds":["text"]}` count and undo depth unchanged; one click makes one text, one undo step. Barcode `{key:"z",code:"KeyZ",ctrl:true,alt:true}` the same. Board `items-board`, the Text tile chord: the same through `kicadHostBoardItem`. Table: nothing until two clicks. Wrong if an item or an `ev.fpedit`/`ev.boardItem` appears before the first click.

## Item 7 (t17-7): drawing sheet tools (kicad 9dbc0ce6)

- `pagelayout_editor/tools/pl_drawing_tools.cpp`: new `static bool ccHostTakesDsItems()` (reads `window.kicadHostDsEdit.onRequest`). `DrawShape` (Line, Rectangle) skips its prime when true; `PlaceItem` skips it for Text only (`isText`), pictures keep it. In `PlaceItem` `SaveCopyInUndoList()` now runs BEFORE `AddDrawingSheetItem( type )`; a null add (canceled picture) pops that copy with `RollbackFromUndo()`. The cleanup lambda's rollback and one Undo after the placement then remove the item.
- Probe: `ds-basic`, ds frame: Text chord (Ctrl+Alt+4), nothing before the click; one click, Esc: 31 to 32 items; Undo once: 31. Text started then Esc before any click: 31, undo depth unchanged. Line: the reader's two clicks are its ends. Picture: still asks at start (`ds-i2` count unchanged); once placed, one Undo removes it; a canceled picture leaves undo depth unchanged.

## Item 5 (t17-5): status popup and the enumerate end notice (kicad 3057cb8c, pcbjam cf624d3)

- `common/status_popup.cpp`: `STATUS_POPUP::Popup` shows nothing under `__EMSCRIPTEN__` (all ten users; `PopupFor` only arms the hide timer).
- `pcbnew/tools/pad_tool.cpp`: `static void ccNoticeEnumerateEnd( bool )`; `EnumeratePads` keeps `bool ccKept`, set at the two `commit.Push` sites (another tool, double click); after the loop (every exit) one `window.kicadHostFpEdit.onNotice('{"notice":"enumerateEnd","kept":true|false}')` when that is a function, inside `try`, answer ignored. A dialog cancel before the loop sends nothing.
- Marker `kicadHostFpEditEnumerateEnd()` (returns true) in `wasm/bindings/cc_e4_fpedit.inc`, namespace `cc_e4`, block `cc_e4_fpedit` (fpedit TU, merged image only).
- Probe: `fpedit-small`, a recorder as `onNotice`: `fpedit.enumerate {start:10, step:2}`, three clicks: no visible `div.window.popup`; Esc: one notice `kept:false`, old numbers back; again ending with a double click: `kept:true`, one undo step "Renumber Pads". A move (M) in the board frame shows no popup.

## Checks

`/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/t17-c/kicad all --kicad-file pcbnew/tools/drawing_tool.cpp --kicad-file pagelayout_editor/tools/pl_drawing_tools.cpp --kicad-file common/status_popup.cpp --kicad-file pcbnew/tools/pad_tool.cpp` (T17.md's form with the four files after one flag is refused, "unknown argument"; one `--kicad-file` per file):

```
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (18826 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (21518 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (10995 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (16111 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (8765 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (6953 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (18333 ms)
PASS kicad pcbnew/tools/drawing_tool.cpp (15389 ms)
PASS kicad pagelayout_editor/tools/pl_drawing_tools.cpp (7837 ms)
PASS kicad common/status_popup.cpp (3566 ms)
PASS kicad pcbnew/tools/pad_tool.cpp (7271 ms)
syntax-check: 0 failed
```

`scripts/kicad/check-run15-exports.sh --since run15/main --kicad /home/matthew/cc-engine-wt/t17-c/kicad --kicad-since run15/main`:

```
check-run15-exports: 258 names registered in the merged image (251 kicad names), 113 of 117 run 15 names, 0 failures
```

## For the island and the site

- Island I5: `onNotice(text)` on the `installFpEditAsk` slot, closed `{notice:"enumerateEnd", kept:boolean}`, sent as `ev.fpeditNotice`; pseudo-cap `fpeditNotice` when `kicadHostFpEditEnumerateEnd` exists. Items 6 and 7 need no island change (they key off the existing hosts' `onRequest`).
- Site S5: the Pads sheet's Number pads closes its line on `enumerateEnd`; `kept:false` says the old numbers are back.
