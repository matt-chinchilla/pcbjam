<!-- Copyright (C) 2026 Circuit Center Inc -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Engine run 15, train 17, group B (t17-b): notes

Branch `seat/t17-b` in pcbjam and kicad, cut from `run15/main` (pcbjam 772f9c2, kicad 3e13bf12). No new export, no marker, no header change.

## Item 9, part 2: Keep the fonts keeps a font the reader added (fixed)

- pcbjam c84840d, `wasm/bindings/cc_e5_pcb.inc` and `cc_e5_sch.inc`, `applySet`'s fonts branch (each face its own copy). With `embed` true only the FONT files the document's own `EmbedFonts()` adds back are removed first (the full names of `GetFonts()`' files, read through `FONT_FILE_NAME::of` on the font's FONT base, since `font/outline_font.h` is not includable in the embind compile); every other FONT file stays. With `embed` false every FONT file is removed (KiCad's panel, unchanged). The comment above `applySet` says so.
- Part 1 (a text finding its face) not attempted, as planned.
- Probe: fixture `embedded-fonts`, pcb frame: `embedded.set add NotoSans-Regular.ttf` (type font), then `{"op":"fonts","embed":true}`: `files` still holds `NotoSans-Regular.ttf` (font), `fontsEmbedded` true. Then `{"op":"fonts","embed":false}`: no FONT file, `fontsEmbedded` false. Schematic frame the same. Wrong if the file is gone after embed true, or any FONT file is left after embed false.

## Item 3: the pickers start from the frame's event loop (fixed)

- kicad 2038848b, `pcbnew/dialogs/dialog_position_relative.cpp` (`CcPick`: item and point each run `OnSelectItemClick` / `OnSelectPointClick` inside `CallAfter`) and `pcbnew/dialogs/dialog_create_array.cpp` (`CcPick`: `OnSelectCenterButton` inside `CallAfter`, the button captured). Both return true at once; both sit inside the files' existing `__EMSCRIPTEN__` blocks; each file gains its dated line.
- Probe: `canvas-kinds` pcb frame: R2, Make an array, Around a point, `kicadPromptPick( token, "point" )` returns true with no throw; a click sets the center and the prompt's record update reaches the host. R1, Place relative to an item, pick `item`, click U1: "Reference item: U1". Island e2e `tests/e2e/m/m6-placement.spec.ts -g "Position Relative|Create Array"`: 2 passed. Wrong if the call throws SuspendError, or the picker never starts (no prompt update after the click).

## Item 11: a converted label stays selected (fixed)

- pcbjam 960965a, `wasm/bindings/cc_e8_sch.inc` `runSchItemSet`: the uuids selected before the conversion are recorded; the clear after the conversion is now quiet (`ClearSelection( true )`); after `commit.Push` every recorded uuid that resolves on the sheet shown (`GetCurrentSheet().ResolveItem`) is selected again, a converted one through `replaced` (from to to), quietly but the last (one `SelectedEvent`). Nothing left to select sends `EVENTS::ClearedEvent`, so the frame never keeps the conversion's own selection report.
- Note: KiCad's `ChangeTextType` still sends its own unselect and select events during the conversion, and the first clear stays non quiet, so the host may see transient `ev.selected` reports before the last one.
- Probe: `items-sch`, label `cc05a11e-0000-4000-8000-00000000011a` selected, `sch.item.set {"items":[{"uuid":"...11a","sheetPath":"/"}],"kind":"label","set":{"labelKind":"global"}}`: answer `replaced` one pair; the LAST `ev.selected` names the new uuid. Also with nothing selected before: the last `ev.selected` is `[]`. Undo depth +1 ("Edit").

## Checks

```
/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/t17-b/kicad all --kicad-file pcbnew/dialogs/dialog_position_relative.cpp --kicad-file pcbnew/dialogs/dialog_create_array.cpp
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (22535 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (23114 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (8518 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (12214 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (7246 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (7160 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (16180 ms)
PASS kicad pcbnew/dialogs/dialog_position_relative.cpp (7692 ms)
PASS kicad pcbnew/dialogs/dialog_create_array.cpp (6708 ms)
syntax-check: 0 failed

scripts/kicad/check-run15-exports.sh --since run15/main --kicad /home/matthew/cc-engine-wt/t17-b/kicad --kicad-since run15/main
check-run15-exports: 257 names registered in the merged image (250 kicad names), 112 of 117 run 15 names, 0 failures
```

Island and site: no change needed for items 9, 3 and 11 (the same ops and answers; the island e2e named above is rerun on T17).
