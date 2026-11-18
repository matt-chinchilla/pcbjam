<!-- Copyright (C) 2026 Circuit Center Inc -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Engine run 15, lane e7: design blocks, library rows and paste special

Worktrees: pcbjam `/home/matthew/cc-engine-wt/e7/pcbjam`, kicad `/home/matthew/cc-engine-wt/e7/kicad`, both on `seat/run15-e7`.

Relay note (2026-11-18 07:00): the seat before this one was cut off by the machine fault. It left pcbjam 1e3c400 (e-librows) committed with no notes and the kicad K-B3 bodies (e-block-save) uncommitted. Both were read line by line against KiCad's own bodies and kept: the only changes were a dash in a doc comment of `pcbjam_libs_reload.h` (ef1de16) and the schematic folder error message, which formatted a literal `%s` (now KiCad's own "Cannot create output directory '%s'."). Nothing was stashed.

Early commit: kicad a0bd3c27 `include/cc_host_design_block.h`, merged into kicad `run15/main` as c57840f9 (merged.log 04:49).

Build G gate: `pcbjam 7e67dba kicad 10d3945e units e-librows e-block-save`.

## e-librows (PM11, SE40, DB4)

Commits: pcbjam 1e3c400, ef1de16. No kicad file.

Exports (dispatcher face, `cc_e7_dispatch.inc`, block `cc_e7_dispatch`):
- `kicadLibsAddEntry` (run 14's registration in `kicad_editor_embind.cpp`, unchanged): widened in `pcbjam_libs_reload.h` `addLibraryEntry` to the kind `design_block`: a row of type `KiCad` in the design block GLOBAL table (the adapter is made first through `PROJECT::DesignBlockLibs()` when no use made it yet), its uri the real `.kicad_blocks` folder, loaded by KiCad's own `DESIGN_BLOCK_IO` through `LIBRARY_MANAGER::LoadLibraryEntry`. Every row it inserts is remembered in `runTimeRows()`.
- `kicadLibsRemoveEntry( kind, nickname )` -> `cc_e7::libsRemoveEntry`: false for a kind other than symbol, footprint, design_block, for a nickname this session did not add (a boot row), an open in flight or no frame. Else on the apply coroutine: the row erased from the live global table, the table saved, `LIBRARY_MANAGER::ReloadTables( GLOBAL, { type } )` (the road KiCad's table dialogs take, the one that drops every cached row pointer before the deque moves), then `MAIL_RELOAD_LIB` to the symbol or footprint editor. A footprint row also drops pcbnew's preloaded footprints (`pcbLibsInvalidatePreloaded`) at once when pcbnew is live.
- `kicadPathVarsSet( {"vars": {NAME: "/abs"}} )` -> `cc_e7::pathVarsSet`: read whole first: exactly the key `vars`, 1 to 64 entries, names `[A-Z][A-Z0-9_]{0,63}`, never `KIPRJMOD` or a predefined name (`ENV_VAR::IsEnvVarImmutable`), values absolute with no NUL; anything else false and nothing set. Then on the coroutine each value goes into `Pgm().GetLocalEnvVariables()` and `Pgm().SetLocalEnvVariables()` puts the map in the environment (Configure Paths' OK), and every symbol, footprint and design block row whose uri names `${NAME}` or `$(NAME)` is reloaded (`ReloadLibraryEntry` + `LoadLibraryEntry`) and its editor tree re-synced. No undo step, nothing marked modified.

Merged image only: none (the dispatcher face is compiled only in the merged image anyway; nothing in pcbnew_embind.cpp).

What the build train must probe:
1. Frame sch on path-vars: `Module.kicadPathVarsSet(JSON.stringify({vars:{MYLIB:'<project folder>/mylib'}}))` answers true; after the apply queue empties `lib.index` lists MyLib's symbols. With `{vars:{KIPRJMOD:'/x'}}`, `{vars:{KICAD10_SYMBOL_DIR:'/x'}}`, `{vars:{mylib:'/x'}}`, `{vars:{MYLIB:'rel'}}`, `{vars:{}}` and `{vars:{MYLIB:'/x'},extra:1}` it answers false. Wrong if: the row still fails to load (the reload missed `$(NAME)` or `${NAME}`), or the design is marked modified.
2. `Module.kicadLibsAddEntry('design_block','blocks-small_blocks','<blocks-small folder>/blocks-small_blocks.kicad_blocks')`, then `project.libs` answers LDO, LDO_PARTIAL and RC under block. Wrong if: nothing listed (the adapter was not made, or the row type is not `KiCad`).
3. `Module.kicadLibsRemoveEntry('symbol', <a nickname added at run time>)` answers true and `lib.index` drops its symbols; a second call answers false; for a boot row (`Device`) it answers false and `Device` still lists. Wrong if: a crash after removal (a stale row pointer: ReloadTables not reached), or the other rows stop loading.
4. The island e2e `lib-rows.spec.ts`.

## e-block-save (DB2, K-B3)

Commits: kicad 10d3945e; pcbjam 7e67dba.

Kicad (each new code under `#ifdef __EMSCRIPTEN__`, KiCad's own member functions untouched, so plain builds compile exactly what they did):
- `eeschema/cc_sch_design_block_write.h` (new): `CcWriteSheetDesignBlock( frame, sheetPath, folder, name, lines )`, `CcWriteSchSelectionDesignBlock( frame, folder, name, lines )`.
- `eeschema/sch_design_block_utils.cpp`: their bodies at the end of the file: `SaveSheetAsDesignBlock`'s nested sheet refusal and `SaveSelectionAsDesignBlock`'s selection copy (a single selected sheet saves that sheet, a single group saves its children, groups deep cloned), written by KiCad's schematic writer (`SCH_IO_MGR::SCH_KICAD`'s `SaveSchematicFile`) straight to `<folder>/<name>.kicad_sch`. Not through `saveSchematicFile`, which would clear the sheet's modified flag and fire the collab save hook. No properties dialog, no overwrite question, no library write, and the selection is not grouped afterwards.
- `pcbnew/cc_pcb_design_block_write.h` (new): `CcWriteBoardDesignBlock( frame, folder, name, lines )`, `CcWritePcbSelectionDesignBlock( frame, linkedGroupOnly, folder, name, lines )`.
- `pcbnew/pcb_design_block_utils.cpp`: board scope = `SavePcbCopy`'s preparation (`SynchronizeNetsAndNetClasses( false )`) and `saveBoardAsFile`'s writer (`PCB_IO_MGR::KICAD_SEXP` `SaveBoard`); selection and linked group = `saveSelectionToDesignBlock`'s temporary board copy word for word (linked group: exactly one selected group with a design block link, else refused). Lines are KiCad's own messages.
- Header reach: two new headers, included only by their own .cpp and by `cc_e7_pcb.inc` / `cc_e7_sch.inc`; no KiCad header changed.

PCBJam:
- `kicadDesignBlockSave( argsJson )` registered in `cc_e7_dispatch` (`cc_e7::designBlockSave`): `pcbEditorActive()` -> `cc_e7::blockSavePcb` (`cc_e7_pcb.inc`), `schEditorActive()` -> `cc_e7::blockSaveSch` (`cc_e7_sch.inc`), else false.
- Args read whole: exactly `run` (integer >= 0), `dir` (a run folder under `/tmp/cc-blocksave/`), `name` (1 to 120 characters, none of `/ \ :` or NUL, not `.` or `..`), `scope`, and `sheetPath` only with scope `sheet`. Board scopes `board`, `selection`, `linkedGroup`; schematic scopes `sheet`, `selection`. False and nothing queued for anything else, no frame, or an open in flight.
- On the apply coroutine: the file at `<dir>/<name>.kicad_block/<name>.kicad_{sch,pcb}`, then `<dir>/.cc-block.json` `{"code": 0, "lines": []}` on success or `{"code": 1, "lines": [KiCad's message]}` on a refusal or failure (a `.part` renamed into place, last). No commit, no undo step.
- Merged image only: `blockSavePcb` (and its include of `cc_pcb_design_block_write.h`) sits under `KICAD_MERGED_EMBIND`, so occ_service never needs pcbnew's design block code.

What the build train must probe:
1. Frame sch on Glasgow: `Module.kicadDesignBlockSave(JSON.stringify({run:1,dir:'/tmp/cc-blocksave/1',name:'iobuf',scope:'sheet',sheetPath:<io_buffer's PathAsString>}))` answers true; after the queue empties `/tmp/cc-blocksave/1/iobuf.kicad_block/iobuf.kicad_sch` exists, reads clean (kicad-cli or the engine's own reader), and `.cc-block.json` is `{"code":0,"lines":[]}`; no dialog; undo depth and the modified flag unchanged. Wrong if: the design reads modified afterwards, a dialog shows, or the record is missing.
2. Scope `sheet` with the root path `/`: record `{code:1}` with "Design blocks with nested sheets are not supported."; a path that matches nothing: `{code:1}` "No sheet has that path.".
3. Scope `selection` with two parts selected: the file holds exactly those two symbols; the selection is not grouped afterwards (undo depth unchanged). With nothing selected: `{code:1}` "Please select some items to save as a design block.".
4. Frame pcb: scope `board` writes `<name>.kicad_pcb` holding every footprint; scope `selection` the selected footprints with their nets; scope `linkedGroup` on a group placed from a block writes that group's children, and on a plain group answers `{code:1}`. Undo depth and modified flag unchanged in each.
5. Refusals answer false at once: `name` `a/b`, `a:b`, a 121 character name, `dir` `/tmp/cc-other/1` or `/tmp/cc-blocksave/../x`, an unknown scope, a `sheetPath` with scope `selection`, scope `board` in frame sch.
6. The island e2e `blocks-save.spec.ts`.

Checks (2026-11-18 07:04, after `git merge run15/main` in both worktrees):

```
/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e7/kicad all --kicad-file eeschema/sch_design_block_utils.cpp --kicad-file pcbnew/pcb_design_block_utils.cpp
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (15721 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (14138 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (11081 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (8246 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (1339 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (6990 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (14768 ms)
PASS kicad eeschema/sch_design_block_utils.cpp (8799 ms)
PASS kicad pcbnew/pcb_design_block_utils.cpp (6973 ms)
syntax-check: 0 failed

scripts/kicad/check-run15-exports.sh --since run15/main --kicad /home/matthew/cc-engine-wt/e7/kicad --kicad-since run15/main
check-run15-exports: 154 names registered in the merged image (147 kicad names), 9 of 110 run 15 names, 0 failures
```

Reported to E0: nothing (no include path, CMake or build line needed: both new KiCad headers sit in their subdirs, already on the include lists).

## e-block-place (DB1, K-B1)

Commits: kicad a0bd3c27 (the early header, merged as c57840f9), cedda45b; pcbjam ebda9f7.

Kicad:
- `include/cc_host_design_block.h` (the early commit): the slot `ccHostDesignBlockSlot()`, `ccHostDesignBlock()`, `ccHostDesignBlockOptions()`, `ccSetHostDesignBlock( block, options )`, `ccTakeHostDesignBlock( &options )`, options `{ asSheet, keepAnnotations, asGroup }`.
- `pcbnew/tools/pcb_control.cpp` `PCB_CONTROL::AppendDesignBlock` (about 1430): under `__EMSCRIPTEN__` it first takes the host block (`ccTakeHostDesignBlock`); with one it checks its layout file and calls `AppendBoard` once (never repeated), the pane never read and no info bar line shown. A file static `s_ccHostPlaceAsGroup`, set by an RAII guard for that call, stands in for `m_DesignBlockChooserPanel.place_as_group` in `AppendBoard` (about 2225). With no host block KiCad's path runs word for word. Recompile reach: that one .cpp.
- The schematic hunk (`eeschema/tools/sch_drawing_tools.cpp` 790 to 815, `SCH_DRAWING_TOOLS` taking the host block and its asSheet and keepAnnotations) is lane e8's (e-block-place-sch). Until it lands, the schematic half below starts KiCad's tool, which reads the pane as before and leaves the host block unused (the half then clears the slot).

PCBJam:
- `kicadDesignBlockPlace( argsJson )` registered in `cc_e7_dispatch` (`cc_e7::designBlockPlace`): `pcbEditorActive()` -> `cc_e7::blockPlacePcb` (`cc_e7_pcb.inc`, merged image only), `schEditorActive()` -> `cc_e7::blockPlaceSch` (`cc_e7_sch.inc`), else false.
- Args read whole: exactly `dir` (a run folder under `/tmp/cc-block/`), `name` (the save's rule), `asSheet`, `keepAnnotations`, `asGroup` (booleans). The block is read synchronously by `DESIGN_BLOCK_IO::DesignBlockLoad( dir, name )`; false when it cannot be read or holds no file for the frame (`.kicad_pcb` on the board, `.kicad_sch` on the schematic), with no frame, or an open in flight. Then on the apply coroutine: `ccSetHostDesignBlock( block, options )`, `RunAction( PCB_ACTIONS::placeDesignBlock )` or `SCH_ACTIONS::placeDesignBlock`, then the slot cleared (a tool that did not start leaves no stale block for the pane's own placement).
- The block's `LIB_ID` is the reader's: an empty nickname and the block's name. A group placed as a group therefore carries no design block LINK (`EDA_GROUP::HasDesignBlockLink` wants a nickname): Apply its layout and Save to its block need a group linked by Update from the schematic or by KiCad's pane. Open question for the site: the contract has no library nickname key.

What the build train must probe:
1. Frame sch on blocks-small, LDO's files written under `/tmp/cc-block/1/LDO.kicad_block/`: `Module.kicadDesignBlockPlace(JSON.stringify({dir:'/tmp/cc-block/1',name:'LDO',asSheet:true,keepAnnotations:false,asGroup:false}))` answers true, the block hangs off the pointer, a press commits ONE undo step, no dialog and no pane. Needs e8's e-block-place-sch merged; without it the tool reads the pane (wrong if seen on the joined build).
2. keepAnnotations true: the designators are kept.
3. Frame pcb, a block with a layout: asGroup true places ONE group named after the block (two or more items), one undo step; asGroup false places loose items; Esc cancels with no undo step.
4. False at once: a block with no `.kicad_pcb` in frame pcb, a missing block, `dir` `/tmp/cc-place/1`, a sixth key, a non boolean option.
5. After a refusal or a cancel, KiCad's own pane placement (if any door reaches it) places the pane's block, never a stale host one.
6. The island e2e `blocks-place.spec.ts`.

Checks: `syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e7/kicad pcbnew eeschema kicad_editor occ --kicad-file pcbnew/tools/pcb_control.cpp`:

```
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (12123 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (12261 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (5146 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (11872 ms)
PASS kicad pcbnew/tools/pcb_control.cpp (10240 ms)
syntax-check: 0 failed
```
(the handover runs `all`.)

## e-block-layout (DB3, K-B2): PART DONE, the Update widening held

Commits: kicad 8397a505 (with st-paste-e's hunks: one file); pcbjam 9183722.

Kicad:
- `include/cc_host_block_layout.h` (new): `CC_HOST_BLOCK_LAYOUT_REPORT { applied, linked, skippedNoLink, failures[{ uuid, reason }], lines }`, the slot `ccHostBlockLayoutReport()`, the RAII `CC_HOST_BLOCK_LAYOUT_SCOPE`, and `ccHostBlockLayoutLine( line )` (true when a reporter took an info bar line).
- `pcbnew/tools/pcb_control.cpp` `PCB_CONTROL::ApplyDesignBlockLayout` (about 1510 to 1830): under `__EMSCRIPTEN__` with a reporter set, its three info bar lines become reporter lines, no `WX_PROGRESS_REPORTER` is made (so no cancel path), and its summary `HTML_MESSAGE_BOX` is replaced by filling the record (KiCad's own failure reasons, per group uuid). The work itself (RepeatLayout into ONE `sharedCommit`, "Apply design block layout to %d group(s)") is KiCad's, unchanged. With no reporter KiCad's path runs word for word (the progress condition is an `#ifdef`/`#else` pair). Recompile reach: that .cpp.

PCBJam:
- `kicadDesignBlockApplyLayout( argsJson )` registered in `cc_e7_pcb` under `KICAD_MERGED_EMBIND` (merged image only), `cc_e7::blockApplyLayout`: exactly `run` (>= 0), `dir` (run folder under `/tmp/cc-blocklayout/`), `uuids` (1 to 256 distinct lower case uuids, each a group on the board, checked synchronously); false otherwise, with no board or an open in flight. On the apply coroutine: selection cleared, those groups selected (`PCB_SELECTION_TOOL::select`, as KiCad's Update PCB dialog does), `PCB_ACTIONS::applyDesignBlockLayout` under the reporter scope, selection cleared, then `<dir>/.cc-block-layout.json` last: `{"applied", "failures": [{"uuid", "reason"}], "lines"}`.
- The block of each group is found as KiCad finds it: the group's design block link through the Design Blocks pane's libraries (`GetDesignBlock( group->GetDesignBlockLibId() )`), so the group's library row must be loaded (kicadLibsAddEntry design_block). A group placed by kicadDesignBlockPlace carries no link (see e-block-place).

HELD, reported to E0: the widening of `kicadUpdateBoardFromSchematic` (`transferGroups`, `applyDesignBlockLayouts`, the layouts' records) and its marker `kicadBoardUpdateGroups`. The lease is only the body of `pcbUpdateBoardFromSchematic` (the argument reader). The updater runs in `ccRunBoardUpdate` (pcbnew_embind.cpp about 3982, NOT leased), which hard codes `updater.SetTransferGroups( false )` and `SetApplyDesignBlockLayouts( false )` and takes `CC_UPDATE_ARGS` (about 3963, not leased); KiCad's Update dialog also lays out `updater.GetAddedGroups()` after `OnNetlistChanged` (dialog_update_pcb.cpp 122 to 136), which must go in the same runner, and the preview token (`ccUpdateToken`) should fold the two flags in. The fix needs the lease widened to `CC_UPDATE_ARGS` and `ccRunBoardUpdate` (about 15 lines: two fields, the two setters, the layout loop with the reporter scope, a `layouts` key in report.json). Not written here, and the marker not registered (the island lists the cap on the marker, so it must not exist before the widening works).

What the build train must probe:
1. Frame pcb on blocks-small with the block row loaded and a group linked to LDO (from Update with groups once the widening lands, or a fixture board holding one): move a footprint of the group, `Module.kicadDesignBlockApplyLayout(JSON.stringify({run:1,dir:'/tmp/cc-blocklayout/1',uuids:['<group uuid>']}))` answers true; the footprint is back where the block's layout puts it; ONE undo step; record `{applied:1,failures:[],lines:[]}`; no message box.
2. A group linked to LDO_PARTIAL: record `{applied:0, failures:[{uuid, reason}]}` with KiCad's reason; no undo step.
3. An unlinked group's uuid: `{applied:0,failures:[],lines:["No groups with a linked design block are selected."]}`.
4. False at once: a uuid that is not a group, a repeated uuid, upper case, an empty list, `dir` outside `/tmp/cc-blocklayout/`.

Checks: see the handover block at the end.

## st-paste-e (K-PASTE)

Commits: kicad 8397a505; pcbjam 9183722.

Kicad:
- `include/cc_paste_special.h` (new): `CC_PASTE_SPECIAL { mode (0 unique, 1 keep, 2 remove), clearNets }`, the slot `ccPasteSpecialSlot()`, `ccTakePasteSpecial()` (taken once). A slot armed by the export, never a tool event parameter (a `TOOL_EVENT::Parameter<T>` of the wrong type asserts; every other pasteSpecial caller passes none).
- `pcbnew/tools/pcb_control.cpp` `PCB_CONTROL::Paste` (about 1240) and `eeschema/tools/sch_editor_control.cpp` `SCH_EDITOR_CONTROL::Paste` (about 2185): under `__EMSCRIPTEN__` an armed choice replaces `DIALOG_PASTE_SPECIAL` (board Clear Nets only for a board clip, as the dialog hides the box otherwise; the schematic's `forceRemoveAnnotations` is computed by KiCad's own line after it); with nothing armed the dialog as before. Recompile reach: those two .cpp.

PCBJam:
- `kicadPasteSpecial( argsJson )` registered in `cc_e7_dispatch` (`cc_e7::pasteSpecial`): exactly `mode` ("unique", "keep", "remove") and `clearNets` (bool); halves `cc_e7::pastePcb` (`cc_e7_pcb.inc`, not merged only: it calls only common code) and `cc_e7::pasteSch` (`cc_e7_sch.inc`). On the apply coroutine: arm, `RunAction( ACTIONS::pasteSpecial )`, empty the slot. The items hang off the pointer; the press commits KiCad's paste as one undo step. No record (an interactive tool, like kicadPlaceImportedItem).

What the build train must probe:
1. Frame sch, a symbol copied: `Module.kicadPasteSpecial('{"mode":"unique","clearNets":false}')` answers true, the copy hangs off the pointer with a new designator, a press commits one undo step, no dialog. `keep` keeps the designator; `remove` gives `R?`.
2. Frame pcb, tracks with nets copied: `{"mode":"keep","clearNets":true}` pastes them with no net; `clearNets:false` keeps the nets.
3. After it, a plain Ctrl+Shift+V (if a door reaches KiCad's own pasteSpecial) shows the dialog path again, never a stale choice (wrong if it pastes silently).
4. False at once: `mode` `"other"`, a missing or extra key, a non boolean clearNets.

## State at the end of this seat (2026-11-18 08:20)

Done and committed: e-librows, e-block-save (both in build G), e-block-place, st-paste-e. e-block-layout: K-B2 and kicadDesignBlockApplyLayout done; the Update widening and the marker `kicadBoardUpdateGroups` HELD until E0 widens the lease to `CC_UPDATE_ARGS` and `ccRunBoardUpdate` (or rules otherwise), so no `e7.unit.e-block-layout` and no `e7.done` yet. e-sheetreload (in the lane's record, not in this seat's list) is not started. No fix notes for e7 were open.

Checks after `git merge run15/main` in both worktrees (pcbjam and kicad up to date with run15/main):

```
/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e7/kicad all --kicad-file pcbnew/tools/pcb_control.cpp --kicad-file eeschema/tools/sch_editor_control.cpp --kicad-file eeschema/sch_design_block_utils.cpp --kicad-file pcbnew/pcb_design_block_utils.cpp
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (12032 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (11825 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (7030 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (7037 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (1165 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (5042 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (11648 ms)
PASS kicad pcbnew/tools/pcb_control.cpp (9657 ms)
PASS kicad eeschema/tools/sch_editor_control.cpp (8945 ms)
PASS kicad eeschema/sch_design_block_utils.cpp (5865 ms)
PASS kicad pcbnew/pcb_design_block_utils.cpp (6375 ms)
syntax-check: 0 failed

scripts/kicad/check-run15-exports.sh --since run15/main --kicad /home/matthew/cc-engine-wt/e7/kicad --kicad-since run15/main
check-run15-exports: 157 names registered in the merged image (150 kicad names), 12 of 110 run 15 names, 0 failures
```

Merged image only: kicadDesignBlockSave's and kicadDesignBlockPlace's board halves, kicadDesignBlockApplyLayout. New KiCad headers (no existing KiCad header changed): `include/cc_host_design_block.h`, `include/cc_host_block_layout.h`, `include/cc_paste_special.h`, `eeschema/cc_sch_design_block_write.h`, `pcbnew/cc_pcb_design_block_write.h`.
