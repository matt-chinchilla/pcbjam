<!-- Copyright (C) 2026 Circuit Center Inc -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Engine run 15, lane e8: notes

Lane e8: schematic items, sheet pins, net names back onto the schematic, symbol editor faults.
Units in order: m5-e, sy4-e, m9-e, lo-e4-e, st-incanno-e, e-block-place-sch.
Worktrees: pcbjam `/home/matthew/cc-engine-wt/e8/pcbjam`, kicad `/home/matthew/cc-engine-wt/e8/kicad`, branch `seat/run15-e8` in both.

## Early commit (R2)

pcbjam 4c1c94f: `symedit_common.h` gains the symbol editor's JSON readers, record tokens, text, text box, shape, stroke and fill readers and appliers out of `symedit_embind.cpp`'s anonymous namespace (namespace `cc_symedit`), unchanged in behavior, with a Y down option for a schematic face. Merged into pcbjam `run15/main` as 9b8bcd7 (merged.log 05:03:43).

## Relay after the machine fault

The seat before this one was cut off by the out of memory fault after the early commit. Its kicad worktree held uncommitted m5-e work (sch_item_host.h new, sch_edit_tool.cpp, sch_drawing_tools.cpp, sheet.cpp); the pcbjam worktree was clean. Read whole and checked: sound, except a half applied hunk at the end of `DrawSheet` (eleven duplicated lines after the `#endif`), removed, and the edit tool's one ask per request tightened (below). Nothing was stashed.

## m5-e: schematic item records and asks

Commits: kicad c2cd9e93 (K-SITEM, K-SHEETFILE), pcbjam e4c177c (`cc_e8_sch.inc`).

**KiCad (all under `#ifdef __EMSCRIPTEN__`, plain builds unchanged):**
- `eeschema/tools/sch_item_host.h` (new, our notice): `CcSchItemKind( item )` (label, wire, line, busEntry, junction, text, textbox, shape, ruleArea, image, sheet, sheetPin, group; null for a symbol, table, table cell, field, pin, no connect, marker), `CcAskHostSchItem( kind, uuid, sheetPath, isNew )` (`window.kicadHostSchItem.onRequest(json)` with `{kind, uuid, sheetPath, isNew}`, the run 14 pattern: no listener, falsy or throwing listener = KiCad's dialog), `CcProposedLabelText` (the last label of the type incremented by `IncrementLabel`, else "Label"), `CC_SHEET_FILE_MODE`, declarations of `ccChangeSheetFile` and `ccNextSheetFileName`. Included only by the three .cpp below and by `cc_e8_sch.inc`: no recompile reach beyond them.
- `eeschema/tools/sch_edit_tool.cpp`: `Properties` asks after run 11's symbol ask for a selection of one or more items all of one kind (first item's uuid); held = return 0, selection kept. `EditProperties` asks before each kind's dialog (double click, `editFieldText` of a label's field asks for its label) unless `Properties` already asked for this request (`s_ccSchItemAsked`, set only when Properties asked; a lone label field is asked by EditProperties). The relay seat changed the flag from "always set" to "set only when asked".
- `eeschema/tools/sch_drawing_tools.cpp`: `PlaceImage` asks `image` (niluuid, isNew) before the file box; held = the tool pops (the host then calls `kicadSchPlaceImage`). `createNewLabel` asks `label` (isNew) when no net name is attached; held = text `CcProposedLabelText`. `createNewText` asks `text`; held = text "Text". `DrawShape` text box asks `textbox`; held = goes down empty. `DrawSheet` asks `sheet`; held = file `ccNextSheetFileName( frame, the default file name )` made through `ccChangeSheetFile( RENAME )`, name as KiCad made it ("Untitled Sheet" path).
- `eeschema/sheet.cpp`: `ccChangeSheetFile` = `SCH_EDIT_FRAME::ChangeSheetFile`'s steps with each question answered by the mode (new sheet over an existing file: `exists` unless USE_EXISTING; existing sheet: USE_EXISTING links, RENAME saves under the new name; over a file another sheet shows: `in_use`); every error a word (`args`, `normalize`, `case_clash`, `in_use`, `exists`, `copy`, `save`, `load`, `recursion`), no message box. `ccNextSheetFileName`: `name.kicad_sch`, then `name_2.kicad_sch` ... free in the hierarchy and on disk.

**PCBJam, `cc_e8_sch.inc`, block `cc_e8_sch` (not merged only: all four live in eeschema_embind.cpp's TU and call only that TU and KiCad):**
- `kicadSchItemGet( std::string )`: `{"items": [{uuid, sheetPath}] (1 to 512)}` (sheetPath "" = the shown sheet, else the first holding it); answers `{"items": [...]}` in order, unknown left out. Records per spec 4.4, Y down, mm; text, textbox, tableCell, shape and ruleArea through `cc_symedit` (`textItemJson`, `textBoxItemJson`, `shapeItemJson`, Y down) with `unit` 0, `bodyStyle` 0. Keys ADDED beyond 4.4 (for m5-i's PROTOCOL.md section): `color` on text and textbox (DIALOG_TEXT_PROPERTIES sets the text color); `table` and `tableCell` are read (the recipe's table get) but never set (RULINGS 2).
- `kicadSchItemSet( std::string )`: `{run, dir: "/tmp/cc-schitems/<n>", items (1 to 4,096), kind, set, fileMode?}`; set keys per kind: label `labelKind text x y orientation size bold italic color shape pinLength fields`; wire and line `points width style color`; busEntry `width style color`; junction `diameter color`; text, textbox, shape, ruleArea the symbol editor's keys less nothing (unit and bodyStyle admit only 0) plus `color` on text kinds; image `x y scale png` (base64, at most 4 MB); sheet `name file page x y size borderWidth borderColor fillColor inBom onBoard inSim dnp fields`; sheetPin `name shape side size`; group `name`. `fields`: `[{name, value?, visible?}]` (at most 32), a named field updated or a user field added. A label kind change and a sheet file change need the item(s) on the SHOWN sheet (ChangeTextType and ChangeSheetFile work there), else false. A file change takes one sheet; `fileMode` `useExisting` or `rename` (absent = `rename`; the island answers `bad_args` when the file exists and fileMode is missing, as spec 4.3 says). On the coroutine: the file change first (outside the undo list, as KiCad's sheet dialog: OnItemsRemoved/Added, RefreshHierarchy, annotation cleared on a link), then ONE `SCH_COMMIT` "Edit": the label kind through `RunSynchronousAction( toLabel|toGLabel|toHLabel|toDLabel, &commit )` (KiCad gives the converted label a NEW uuid; found again by place and text), then every key. Record `items.json` `{applied, depth, renamed: [{from, to}], replaced: [{from, to}], error?}` (`replaced`: label uuids KiCad changed; ADDED beyond 4.3, for m5-i).
- `kicadSchPlaceImage( std::string )`: `{"path": "/tmp/cc-schimage/<file>"}`; the file read by `REFERENCE_IMAGE::ReadImageFile` (probe first, false when KiCad cannot read it), then on the coroutine `RunAction( SCH_ACTIONS::placeImage, image )` (DdAddImage's call): it hangs on the pointer, a click is KiCad's commit.
- `kicadHostSchItems()`: marker, true.

**Syntax check** (`--kicad /home/matthew/cc-engine-wt/e8/kicad`):
- `eeschema --kicad-file eeschema/sheet.cpp --kicad-file eeschema/tools/sch_edit_tool.cpp --kicad-file eeschema/tools/sch_drawing_tools.cpp`: first run `FAIL kicad eeschema/sheet.cpp` (the error words were wxChar strings), fixed; then `PASS eeschema eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND`, `PASS kicad eeschema/sheet.cpp`, `PASS kicad eeschema/tools/sch_edit_tool.cpp`, `PASS kicad eeschema/tools/sch_drawing_tools.cpp`, `syntax-check: 0 failed`.
- With the inc: `eeschema`: `PASS eeschema eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (12946 ms)`, `syntax-check: 0 failed`.

**What the build train must probe (items-sch/ fixture, Schematic tab, Edit mode):**
- `kicadHostSchItems()` is true.
- With a listener `window.kicadHostSchItem = { onRequest: r => (log.push(JSON.parse(r)), true) }`: E on each of a local, global, hierarchical and directive label, a wire, a bus, a graphic line, a bus entry, a junction, a text, a text box, a rectangle, a rule area, a picture, a sheet, a sheet pin, a group: one request each with the right kind and uuid, NO dialog (a wx dialog seen is the failure). A no connect: no request, nothing opens. Without a listener: KiCad's dialog (proves the fallback).
- Double click on a label's field: one request with the LABEL's uuid (a field kind or a second request is the failure).
- L, click: one request `{kind: "label", isNew: true}`, a label placed named "Label" (or the last label incremented), no dialog. T: text "Text". Text box drawn: empty. S, drag: a sheet "Untitled Sheet" with file `untitled.kicad_sch` (or `_2`) created on disk, no dialog. Place image: one request `image` and the tool ends.
- `kicadSchItemGet({"items":[{uuid, sheetPath}]})` for the global label (its `fields` include Sheet references), the bus entry, the sheet (`pins` count), the table (its `cells` text). An unknown uuid: left out. Bad shape: "".
- `kicadSchItemSet` `{run:1, dir:"/tmp/cc-schitems/1", items:[label], kind:"label", set:{labelKind:"global", shape:"output"}}` true; `items.json` `applied` 1, `replaced` one pair, depth +1; one Ctrl+Z returns the local label with its old uuid. A label's net stays (the connection graph). Each other kind: one key set, one undo step each.
- Sheet file: set `file` to an existing sub sheet's file with `fileMode: "useExisting"`: the sheet links (renamed lists it, depth unchanged as KiCad's own); with `rename` to a new name: the new file written on disk. A file another sheet shows with `rename`: `items.json` error `in_use`, nothing changed.
- `kicadSchPlaceImage({"path":"/tmp/cc-schimage/a.png"})` with a PNG written there: the picture hangs on the pointer, a click places it (one undo step). A non image file: false.
- Failure signs: a KiCad dialog or message box on any road; two undo steps for one set; a set that changes nothing yet answers true with applied 0 for a valid item.

## sy4-e: symbol editor faults (SY4)

Commit: pcbjam f4f875d (`wasm/bindings/symedit_embind.cpp` only; no export, no KiCad file). The lane file stands for `BUILD-NOTES-run15-sy4.md` (the unit's ownedPaths name it; written here instead, one notes file per lane).

- The prefix: a placed part opened in the editor (`IsSymbolFromSchematic()`) carries its designator in the reference field (LoadSymbolFromSchematic's copy, "U1"). `refPrefixOf` answers KiCad's `UTIL::GetRefDesPrefix` of it ("U") in `kicadSymEditSymbolGet`'s `refPrefix`; a `symbol.set` `refPrefix` in placed mode keeps the designator's number (`refSuffixOf`: "R" on "U1" sets "R1"), so the push back keeps the part's designator; `kicadSymEditCheck` runs KiCad's `CheckLibSymbol` with the prefix in the field and puts the designator back at once (a read, no commit). A library symbol is unchanged.
- De Morgan on new: `LIB_SYMBOL_LIBRARY_MANAGER::CreateSymbol` calls `SetBodyStyleCount( 2, false, true )` (pins duplicated) but never `SetHasDeMorganBodyStyles( true )`, and `GetBodyStyleCount` counts names or the flag, so the symbol had one body style. `kicadSymEditNew` now sets the flag after CreateSymbol for `bodyStyles: "demorgan"` with no parent (KiCad's symbol properties dialog pairs the same two calls).

**Syntax check**: `symedit`: `PASS symedit symedit_embind.cpp subdir=eeschema (8342 ms)`, `syntax-check: 0 failed`.

**What the build train must probe** (symedit-small fixture and any schematic with U1):
- `symbol.forEditor` on U1, `symedit.open` mode placed, `kicadSymEditSymbolGet()`: `refPrefix` "U" (failure: "U1"). `kicadSymEditCheck()`: no `reference_prefix_digit` finding (failure: one). The field on the canvas still reads U1.
- `symbol.set` `{refPrefix: "IC"}` in placed mode, then push: the part reads IC1 on the schematic (failure: "IC" with no number, or the part unannotated).
- `kicadSymEditNew` with `bodyStyles: "demorgan"`: the state's body style count is 2 and `bodyStyles.mode` "demorgan"; the body style press steps between 1 and 2 (failure: one body style). With "one": one.
- A library symbol (mode library) reads its field unchanged.
