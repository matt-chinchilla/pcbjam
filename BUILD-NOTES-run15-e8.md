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

## m9-e: sheet pins against labels

Commits: kicad 402fb267 (K-SYNCPIN), pcbjam 234c890 (`cc_e8_sch.inc`).

**KiCad (under `#ifdef __EMSCRIPTEN__`):**
- `eeschema/tools/sch_item_host.h`: `CC_SYNC_TEMPLATE` (owned copies of the counterparts, `Current()`, `CanPlaceMore()`, `EndPlaceItem()`, `EndPlacement()`) and `CcSyncTemplate()`, the one armed template.
- `eeschema/tools/sch_drawing_tools.cpp`: `ccSyncPlacementTemplate( dialog )` (the dialog's template first, else the armed one) read at the hierarchical label template site and the sheet pin from label site of `TwoClickPlace`; after a placement with the armed template, `EndPlaceItem()` and the next name (`goto PLACE_NEXT`) or the tool pops (as the dialog's path, without `Show`); at the tool's end `EndPlacement()` (Esc ends, nothing stays armed).

**PCBJam, `cc_e8_sch.inc` (not merged only):**
- `kicadSheetPinsGet( std::string )`: `{}` or `{"sheets": [{uuid, sheetPath}]}` (sheetPath = the path the sheet sits on). Answers `{"sheets": [{sheet, sheetPath, name, file, pairs: [{name, pin, label, pinShape, labelShape}]}], "truncated"}`. Pairing: PANEL_SYNC_SHEET_PINS::UpdateForms' rule first (labels de-duplicated by name, StrNumCmp order; same name AND shape pair), then the same name with differing shapes paired into one row (spec 7.3's "a dot where the shapes differ"), then singles. At most 4,096 pairs a sheet.
- `kicadSheetPinsApply( std::string )`: `{run, dir: "/tmp/cc-sheet-pins/<n>", actions (1 to 4,096)}`, actions as spec 7.3 (closed per op; every pin and label must exist when called). ONE `SCH_COMMIT` "Synchronize sheet pins": a pin staged through its sheet (`Modify( sheet )`), removed as `SCH_EDIT_TOOL::DoDelete` does (`RemovePin` after staging); a label `Modify` or `Remove`. Record `sheet-pins.json` `{applied, depth}` last.
- `kicadSheetPinsPlace( std::string )`: `{sheet, sheetPath, place: "pin"|"label", names (1 to 64)}`; every name needs its counterpart (for a pin: a hierarchical label of that name in the sub sheet; for a label: a pin of that name), else false. On the coroutine, as `doSyncSheetsPins`' placement callback: arms the template with copies of the counterparts; label: `changeSheet` into the sheet, `placeHierLabel`; pin: `changeSheet` to the parent when not shown, `SyncSelection( {}, nullptr, { sheet } )`, `placeSheetPin`. Each press places the next name; KiCad's commit per placement.

**Syntax check** (`all --kicad-file eeschema/sheet.cpp --kicad-file eeschema/tools/sch_edit_tool.cpp --kicad-file eeschema/tools/sch_drawing_tools.cpp --kicad-file eeschema/tools/backannotate.cpp`, `--kicad /home/matthew/cc-engine-wt/e8/kicad`, after merging run15/main in both worktrees): `PASS pcbnew`, `PASS eeschema`, `PASS symedit`, `PASS fpedit`, `PASS dsedit`, `PASS kicad_editor`, `PASS occ`, `PASS kicad eeschema/sheet.cpp`, `PASS kicad eeschema/tools/sch_edit_tool.cpp`, `PASS kicad eeschema/tools/sch_drawing_tools.cpp`, `PASS kicad eeschema/tools/backannotate.cpp`, `syntax-check: 0 failed`. (Two earlier runs were refused by the check while an engine build ran.)

**What the build train must probe** (sheet-pins/ fixture):
- `kicadSheetPinsGet("{}")`: sheet IO with pairs A (pin and label, both uuids), B (label only, `pin` null), C (both, `pinShape` and `labelShape` differ), D (pin only), E and E1 (two singles). Bad JSON: "".
- `kicadSheetPinsApply({run:1, dir:"/tmp/cc-sheet-pins/1", actions:[{op:"removePin", pin:D}, {op:"shapePin", pin:C, to:"output"}]})` true; `sheet-pins.json` applied 2, depth +1; ONE Ctrl+Z brings D back and C's old shape (failure: two undo steps, or D gone after undo).
- `kicadSheetPinsPlace({sheet, sheetPath, place:"pin", names:["B"]})` true: a pin named B hangs on the pointer at the sheet's edge; a press places it (one undo step); the tool ends; no KiCad dialog. With `place:"label"` and a pin-only name: the editor enters the sub sheet and a hierarchical label of that name hangs on the pointer. Esc mid placement: nothing stays armed (a later S or H tool behaves as KiCad's own). A name with no counterpart: false.

## lo-e4-e: net names from the board onto the schematic

Commits: kicad be29acad (`backannotate.h`, `backannotate.cpp`), pcbjam 234c890 (`cc_e8_pcb.inc`, `cc_e8_sch.inc`).

**KiCad (under `#ifdef __EMSCRIPTEN__`):** `BACK_ANNOTATE` gains `CC_PAD_NET`, `CC_NET_ROW`, `CcProcessNetNames( SCH_COMMIT*, pads )` and two members (`m_ccLastLabel`, `m_ccLastOutcome`). `CcProcessNetNames` is applyChangelist's net name loop (a pin's `Connection( &sheetPath )->Name( true )` against the pad's net, `processNetNameChange` when they differ) over the host's pads, symbols matched by reference (`GetSymbols( SYMBOL_FILTER_NON_POWER )`, each unit tried until one holds the pin). `processNetNameChange` records its outcome (`rename` with the label's uuid, `add`, `refused` for a power pin) beside its own lines. **Header change:** `eeschema/tools/backannotate.h` (class `BACK_ANNOTATE`) is included only by `backannotate.cpp` and `dialogs/dialog_update_from_pcb.cpp`: two files recompile.

**PCBJam:**
- `kicadBoardPadNets()` in `cc_e8_pcb.inc`, block `cc_e8_pcb` (pcbnew TU; not merged only, it calls only the board): `{"pads": [{ref, pad, net}], "truncated"}`, the board's `MAIL_PCB_GET_NETLIST` reply's facts (board only footprints left out, `GetShortNetname`, pads with no net left out), at most 32,768. A read.
- `kicadSchRenameNets( std::string )` in `cc_e8_sch.inc`: `{run, dir: "/tmp/cc-renameNets/<n>", preview, pads (1 to 32,768)}`. On the coroutine: `RecalculateConnections( NO_CLEANUP )`, a `BACK_ANNOTATE` with net names alone (no relink, no other property, no swaps), `aDryRun` = preview; the rows; ONE commit "Update net names" when not a preview. Record `renameNets.json` `{preview, rows: [{ref, pin, from, to, sheetPath, outcome, label?}], applied, truncated, depth}` last. `outcome` is ADDED beyond the manifest's row (for lo-e4-i).

Syntax check: as m9-e's (the same run).

**What the build train must probe** (a board and schematic pair where a board net was renamed, e.g. Glasgow with one net renamed on the board):
- `kicadBoardPadNets()` in the pcb frame: rows `{ref, pad, net}` with short names; `truncated` false.
- In the sch frame, `kicadSchRenameNets({run:1, dir:"/tmp/cc-renameNets/1", preview:true, pads})`: `renameNets.json` rows for the renamed net's pins (`outcome` "rename" with the label's uuid), `applied` 0, depth unchanged, the schematic unchanged.
- The same with `preview:false`: the label renamed, depth +1, one Ctrl+Z restores it. A net driven by a power symbol: `outcome` "refused", nothing changed. A net with no label driver: a label added at the pin (`outcome` "add").
- Failure signs: a kiway call or a KiCad dialog; two undo steps; rows in a preview that changed the drawing.

## st-incanno-e: shift designators

Commit: pcbjam 234c890 (`cc_e8_sch.inc`; no KiCad file).

- `kicadSchIncrementAnnotations( std::string )`: `{first, by, allSheets}` exactly (the manifest's args; no run folder, the host reads back as after `symbol.set`). `first` must split into a prefix and a number (`SCH_REFERENCE::IsSplitNeeded`), else false; `by` 1 to 100,000 (KiCad's dialog spins 1 to 64; the spec's test shifts by 100, so the bound is wider than the dialog's). On the coroutine, `SCH_EDITOR_CONTROL::IncrementAnnotations`' body after its dialog, line for line: the references of the shown sheet or every sheet, split; each with first's prefix and a number at or above it moves up by `by`; ONE commit "Increment Annotations", none when nothing matched.

Syntax check: as m9-e's (the same run).

**What the build train must probe** (Glasgow): `kicadSchIncrementAnnotations({first:"R10", by:100, allSheets:true})` true; R10 and above become R110 and above, R1 to R9 unchanged, one Ctrl+Z restores all. `first:"R"` (no number): false. `by:0`: false. A first that matches nothing: true, no undo step.

## e-block-place-sch: K-B1's schematic hunk

Commit: kicad 402fb267 (`eeschema/tools/sch_drawing_tools.cpp`; no pcbjam file, no export: `kicadDesignBlockPlace` is e7's). Needs e7's `include/cc_host_design_block.h`, taken by `git merge run15/main` in the kicad worktree (merge efe741e8, kicad run15/main c57840f9 and after).

Under `#ifdef __EMSCRIPTEN__`: `ImportSheet` for `placeDesignBlock` takes the host block first (`ccHostDesignBlock()`, `ccTakeHostDesignBlock( &options )`): the Design Blocks pane is never read for it; a block with no schematic file ends the tool quietly (no info bar). Its options stand where KiCad reads the chooser's settings: `asSheet` (false: `placeSheetContents` once, no repeat), `keepAnnotations` and `asGroup` (contents path and `DrawSheet`'s sheet path), and a host block is placed once (no repeated placement). The options live in a file static `s_ccBlockOptions`, reset at every pane placement and after the host block's placement, so a pane block never reads them.

Syntax check: as m9-e's (the same run, `--kicad-file eeschema/tools/sch_drawing_tools.cpp`).

**What the build train must probe** (with e7's `kicadDesignBlockPlace` on the joined build): place a block as a sheet (`asSheet` true): the sheet hangs on the pointer, a click places it, one undo step, the pane never opened; with `keepAnnotations` true the designators are kept, false they are renumbered; with `asGroup` true the sheet sits in a group named after the block; `asSheet` false: the contents hang on the pointer once (no second placement after the click). A pane block afterwards (desktop path, not reachable in the browser) reads the chooser settings again. Failure: the pane read, an info bar, or a second placement of a host block.

## Handover

Both worktrees merged `run15/main` (pcbjam merge 44190d2, kicad merge efe741e8) before the final checks.

- Syntax check `all` with the four changed KiCad files: every PASS line in m9-e's section above, `syntax-check: 0 failed`.
- Export check `scripts/kicad/check-run15-exports.sh --since run15/main --kicad /home/matthew/cc-engine-wt/e8/kicad --kicad-since run15/main`: `check-run15-exports: 164 names registered in the merged image (157 kicad names), 19 of 110 run 15 names, 0 failures`.
- The lane's ten names, all registered in `cc_e8_sch` (`kicadSchItemGet`, `kicadSchItemSet`, `kicadSchPlaceImage`, `kicadHostSchItems`, `kicadSheetPinsGet`, `kicadSheetPinsApply`, `kicadSheetPinsPlace`, `kicadSchRenameNets`, `kicadSchIncrementAnnotations`) and `cc_e8_pcb` (`kicadBoardPadNets`). None is merged image only: each calls only its own TU and KiCad.
- Header changes and their reach: `eeschema/tools/sch_item_host.h` (new; included by sheet.cpp, sch_edit_tool.cpp, sch_drawing_tools.cpp and eeschema_embind.cpp through the inc), `eeschema/tools/backannotate.h` (class member additions under `__EMSCRIPTEN__`; two KiCad files include it, plus eeschema_embind.cpp through the inc). No widely included header changed.
- Reported to E0: nothing needed (no include path, CMake line, build script line or new name; `<backannotate.h>` resolves through the TU's `eeschema/tools` include directory).
- For the island seats (keys added beyond the spec's records): `color` on text and text box records (m5); `replaced` in `items.json` (m5); `outcome` in `renameNets.json` rows (lo-e4); table and table cell records are read only (RULINGS 2).

## Repair e8-1: a block placed as a sheet in one press

The fault (fix note e8-1, PJ gate 1 clause 3, build T3): `kicadDesignBlockPlace` with `asSheet: true` went through KiCad's draw sheet road: a first press drew the box, a second opened the Sheet Properties dialog, undo depth 0.

The cause: K-B1's hunk took the host's block and options into `SCH_DRAWING_TOOLS::DrawSheet`, but that tool still made the sheet in two presses and, with no host form holding `sheet`, called `EditSheetProperties`.

The repair, kicad 76051709 (`eeschema/tools/sch_drawing_tools.cpp`, `DrawSheet`, under `__EMSCRIPTEN__`, only while a host block's options are held): before the press KiCad's smallest sheet box with the block's name hangs off the pointer (a preview); the press makes the sheet as KiCad does (name from the block, file the block's schematic name, its fields, its page number) and queues `ACTIONS::finishInteractive`, which the tool manager runs before that press's dispatch returns, so the same press commits; the commit takes the file `ccNextSheetFileName` gives for the block's file name and `ccChangeSheetFile` (RENAME, the block's schematic as the source copy), never the dialog and no message box; then KiCad's own annotation by the block's `keepAnnotations`, the group by `asGroup`, ONE `c.Push` ("Draw Sheet"), and the tool ends. The options are cleared when the tool ends, placed or canceled. The road for a pane block and for a plain sheet is unchanged.

Checks, from this worktree after `git merge run15/main` (pcbjam 690a16c, kicad merged):

```
/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e8/kicad all --kicad-file eeschema/tools/sch_drawing_tools.cpp
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (16231 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (15470 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (8340 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (11751 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (5691 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (6908 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (15940 ms)
PASS kicad eeschema/tools/sch_drawing_tools.cpp (9265 ms)
syntax-check: 0 failed

scripts/kicad/check-run15-exports.sh --since run15/main --kicad /home/matthew/cc-engine-wt/e8/kicad --kicad-since run15/main
check-run15-exports: 256 names registered in the merged image (249 kicad names), 111 of 111 run 15 names, 0 failures
```

What the train must probe (the fix note's call): fixture `blocks-small`, frame `sch`, the library entry added, LDO's three files under `/tmp/cc-block/1/LDO.kicad_block/`, then `kicadDesignBlockPlace({dir:'/tmp/cc-block/1',name:'LDO',asSheet:true,keepAnnotations:false,asGroup:false})`. Expected: true, no pane and no dialog; a pointer move shows the box; ONE press puts down one sheet named LDO with file `LDO.kicad_sch` (or `LDO_2.kicad_sch` when taken), the file copied beside the project, its symbols annotated, undo depth 1, the tool ended (a second press places nothing). With `asGroup: true` and `nickname`: the sheet in one group whose `lib_id` is the block's, still one undo step. Esc before the press: nothing placed, depth 0, and a later pane placement reads the pane's settings. Failure that means it is wrong: depth 0 after one press, a second sheet on a second press, or any `wxDialog` on the canvas.
