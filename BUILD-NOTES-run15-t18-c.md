<!-- Copyright (C) 2026 Circuit Center Inc -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Train 18, group C: item 3, the schematic half (t18-3)

## Cause, as read in the code

`ccRunSchEdit` (`wasm/bindings/cc_rules_sch.inc`) walks `Schematic().Hierarchy()`, one entry per sheet PATH, and visits every item of `path.LastScreen()`. A sheet file used twice (Glasgow's `io_buffer.kicad_sch`) gives two paths over one screen, so every item of it is visited twice. `ccSchProcessText` names the settings that differ from the item: in a preview nothing changes, so the second visit finds the same differences and adds a second row; in an apply the first visit sets the values, the second finds none and adds nothing. E0's cause holds. The same double count hit the sheet border branch of `ccSchVisit` (a sheet placed inside a shared sheet: its border row came twice in a preview, once in an apply).

An item already at the value asked for: no word, so no row and no count in either mode, today and after the fix. They agree.

Other walks over sheet paths in the file, checked: the symbol collection `ccRulesInstances` and the symbols change (rows per instance on purpose, built before the apply branch, the same rows in both modes; the change runs once per SCH_SYMBOL); the page section's read (`pageNumbers`, per path on purpose) and the page numbers' set (per path on purpose). None previews differently from its apply. textGraphics is the only bulk op changed.

## Change

- `CC_EDIT_ROWS` gains `std::set<const SCH_ITEM*> seen` and `First( item )`.
- `ccSchProcessText` returns at once for an item already met (both modes, before any filter on words), so its row is the first path's (the same path in both modes, so the row's designator text agrees too).
- The sheet border branch takes the same once only rule (`&& aRows.First( sheet )`); a sheet is never handed to `ccSchProcessText`, so one set serves both.
- Filters stay per path (net, designator, selection): an item that fails on its first path and passes on a later one is taken on that later path, in both modes.
- Dated "Modified by Circuit Center Inc on 2026-11-23" line in the file's header. No export, marker or host ask added.

## Checks

`/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e0-kicad all`

```
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (13406 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (13585 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (7299 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (9354 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (5055 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (4952 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (12262 ms)
syntax-check: 0 failed
```

`scripts/kicad/check-run15-exports.sh --since run15/main`: `check-run15-exports: 262 names registered in the merged image (255 kicad names), 117 of 118 run 15 names, 0 failures`.

## What build T18 must probe

1. Glasgow, schematic frame, `sch.edit {op: textGraphics, preview: true, filter: {}, set: {size: 1.0}}`: count equals the apply's (2,657 on T17), rows carry each io_buffer item once. Then the same with `preview: false`: the same count, undo depth +1. A second preview: count 0. Undo once: a preview again gives the first count.
2. Glasgow, `filter: {sheetBorders: true}`, `set: {lineWidth: <a width no sheet has>}`: preview count = apply count.
3. A design with no shared sheet (the bundled example, or any flat schematic): the preview and apply counts are what T17 gave (no change where nothing is shared).
4. Failure meaning it is wrong: preview count above the apply count (the set was not reached), or below it (an item skipped on a path where its first visit was filtered out).
