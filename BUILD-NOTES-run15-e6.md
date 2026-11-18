<!-- Copyright (C) 2026 Circuit Center Inc -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Engine run 15, lane e6: drawing sheet editor and Import a drawing

Branches `seat/run15-e6` in pcbjam (`/home/matthew/cc-engine-wt/e6/pcbjam`) and kicad (`/home/matthew/cc-engine-wt/e6/kicad`). Units in order: ds-e1 (gate code), fp-e8, ds-e2, sy2-e (conditional). The per unit notes the manifest names (`BUILD-NOTES-run15-ds.md`, `BUILD-NOTES-run15-fp-e8.md`, `BUILD-NOTES-run15-sy2.md`) are sections of this file.

## ds-e1: the drawing sheet editor's kiface in the merged image (build G gate code)

Commits: kicad a3d9fc1d, pcbjam b6b7c17 (gate commits; the merges of `run15/main` after them are not part of the gate).

Files:
- kicad `pagelayout_editor/CMakeLists.txt`: an `if( EMSCRIPTEN AND KICAD_WASM_MERGED_EDITOR )` block after `PL_EDITOR_KIFACE_LIBRARIES` (cvpcb's merged block is the precedent): `pl_editor_kiface_objects` takes `KICAD_WASM_PL_SIDE_RENAMES` (`Kiface=PlKiface`), `pl_editor.cpp` compiles with `COMPILING_DLL;KIFACE_GETTER=pl_editor_kiface_getter`, `PL_EDITOR_KIFACE_LIBRARIES` exported `CACHE INTERNAL`. The option OFF tree is unchanged. `pagelayout_editor/` is already added before `${KICAD_WASM_LAYER}/editor` at the top level (1364 and 1374): no top level edit.
- kicad `common/single_top.cpp`: under `KICAD_MERGED_KIFACES` and `__EMSCRIPTEN__`, the extern `pl_editor_kiface_getter` and `Kiway.set_kiface( KIWAY::FACE_PL_EDITOR, ... )` after cvpcb's; `FRAME_PL_EDITOR` maps a bare file argument to `kicad_wks`. `OnKifaceStart` stays lazy (`KIWAY::KiFACE`, `common/kiway.cpp` 223 to 253), so a sch or pcb frame never starts the pl_editor kiface.
- pcbjam `wasm/editor/CMakeLists.txt`: `KICAD_EDITOR_LIBS` adds `${PL_EDITOR_KIFACE_LIBRARIES}`.
- pcbjam `wasm/bindings/dsedit_embind.cpp`: `extern "C" void kicadCollabOnModify()` (the hook `pl_editor_frame.cpp` 388 and 406 calls from `OnModify` under `__EMSCRIPTEN__`) bumps `cc_e6::g_dsChangeCount`, read by `cc_e6::dsChangeCount()`. No registration.
- pcbjam `scripts/kicad/audit-merged-symbols.sh`: a pl_editor section read from the merged tree (`build-wasm/kicad-kicad_editor/pagelayout_editor/CMakeFiles/pl_editor_kiface_objects.dir` and `navlib/libpl_editor_navlib.a`), strong intersections against pcbnew, eeschema and cvpcb of the same tree and a weak review list; expected output: the three strong lists empty.
- `scripts/kicad/build-kicad-target.sh`: no edit by e6 (E0's seams already compile `dsedit_embind.cpp` into the merged `EMBIND_OBJ` with the `pagelayout_editor` subdir and the dialogs and tools folders).

Collision reading (by source, before any build): pl_editor's 27 class names (`PL_*`, `PROPERTIES_FRAME`, `DIALOG_INSPECTOR`, `EDIT_POINTS_FACTORY`, `BitmapGridCellRenderer`, `NL_PL_EDITOR_PLUGIN*`, ...) are defined in no file of eeschema, pcbnew or cvpcb; its external free functions (`fromMM`, `toMM`, `validateMM` in `dialogs/properties_frame.cpp`, `InvokeDialogPrint`, `InvokeDialogPrintPreview`, `pinEditedCorner`) have no namesake outside it (eeschema's `pinEditedCorner` and pcbnew's `toMM` are members); `PL_EDITOR` (directory wide define) changes no shared header (only `single_top.cpp`, which the merged image compiles in `wasm/editor`). The kiface object lives in `namespace PGE`. So the rename list starts and, by reading, ends with `Kiface=PlKiface`; the audit on build G is the proof.

Merged image only: everything here (the kiface block is `KICAD_WASM_MERGED_EDITOR`; `dsedit_embind.cpp` is linked into kicad_editor only).

Reported to E0: nothing needed (the build script line and include folders were already in the seams).

Syntax check (from `/home/matthew/cc-engine-wt/e6/pcbjam`, `gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e6/kicad dsedit kicad_editor --kicad-file common/single_top.cpp`):

```
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (1630 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (5739 ms)
PASS kicad common/single_top.cpp (6427 ms)
syntax-check: 0 failed
```

The CMake files are not compiled by the syntax check; build G's configure step is their check.

Export check (`scripts/kicad/check-run15-exports.sh --since run15/main --kicad /home/matthew/cc-engine-wt/e6/kicad --kicad-since run15/main`, after merging `run15/main`): `146 names registered in the merged image (139 kicad names), 1 of 110 run 15 names, 0 failures`.

What build G must probe (gate 0 of the addendum, section 4):
1. The configure step sees `PL_EDITOR_KIFACE_LIBRARIES` in `wasm/editor` (a missing cache entry links kicad_editor without the kiface: the link then fails on `pl_editor_kiface_getter`, undefined).
2. `?frame=ds` in the island harness answers `ev.ready`; `kicadChromeShown()` false at ready; no wx menu bar, toolbar, message panel or properties pane in any frame sampled from navigation to ready; KiCad's default sheet drawn at A4. Wrong if the boot returns false (the face not registered: `Kiway.Player( FRAME_PL_EDITOR )` finds no kiface).
3. `kicadSetChrome(true)` shows File, Edit, View, Place, Inspect, Preferences, Help; `false` hides them.
4. Boot to ready (median of 5) and `Module.HEAP8.length` at ready beside an empty `?frame=sch` boot; the wasm raw size against run 14's.
5. `scripts/kicad/audit-merged-symbols.sh` in the builder container: the three pl_editor STRONG lists print no line. A line there joins `KICAD_WASM_PL_SIDE_RENAMES`.
6. The sch, pcb and symedit e2e of the island pass unchanged (a sch or pcb frame must not start the pl_editor kiface: no `pl_editor.json` written by a sch boot).

## fp-e8: kicadImportGraphics in four frames (the `BUILD-NOTES-run15-fp-e8.md` section)

Status: code committed on `seat/run15-e6` (pcbjam, see the commit `cc_e6: fp-e8 ...`), NOT green: the syntax check of the `fpedit` and `eeschema` units stops at one missing include directory, reported to E0 (below). No `e6.unit.fp-e8` file until that path is in `compile_embind_tu` and `all` passes. No KiCad file changed (kicad: none).

Export and placement:
- `kicadImportGraphics` (std::string args, answers std::string), registered in `EMSCRIPTEN_BINDINGS( cc_e6_dispatch )` (`cc_e6_dispatch.inc`): `cc_e6::importGraphics` picks the half by the top window, footprint and symbol editors first (`fpEditorActive() || pcbEditorActive()` to the board half, `symEditorActive() || schEditorActive()` to the schematic half), `""` in any other frame.
- Board half `cc_e6::importGraphicsBoard` in `cc_e6_fpedit.inc` (the fpedit TU: merged image only, pcbnew include set) serves PCB_EDIT_FRAME and FOOTPRINT_EDIT_FRAME through `PCB_BASE_EDIT_FRAME` and `GRAPHICS_IMPORTER_PCBNEW( frame->GetModel() )`. It sits there, not in `cc_e6_pcb.inc`, so occ_service never sees it (no `KICAD_MERGED_EMBIND` guard needed) and the footprint editor's frame is reachable. `cc_e6_pcb.inc` stays empty.
- Schematic half `cc_e6::importGraphicsSch` in `cc_e6_sch.inc` serves SCH_EDIT_FRAME (`GRAPHICS_IMPORTER_SCH`) and SYMBOL_EDIT_FRAME (`GRAPHICS_IMPORTER_LIB_SYMBOL( GetCurSymbol(), GetUnit() )`).
- Merged image only: all of it (the dispatcher is in kicad_editor_embind.cpp, the board half in the fpedit TU).
- Args, closed: `path` (required: absolute, no `..`, an existing file of 1 byte to 25 MB; the plugin is KiCad's own choice by the file's extension, `GRAPHICS_IMPORT_MGR::GetPluginByExt`, so the island writes the file as `.dxf` or `.svg`), `place` (required, `at` or `pointer`), `layer` (canonical board layer name, an enabled layer; absent: the frame's active layer, as the dialog with "set layer" off), `scale` (default 1), `origin` ([x, y] mm, default [0, 0]), `lineWidth` (mm or null; DXF only, null keeps the importer's default; an SVG gets 0, its own widths, as the dialog does), `dxfUnits` (`mm`, `cm`, `inch`, `mil`, `feet`; absent: the file's own `$INSUNITS`), `group`, `fixDiscontinuities`, `tolerance` (mm, default 1). The schematic half accepts the board keys with their shapes and ignores them (KiCad's schematic dialog has none of them).
- The body: `DIALOG_IMPORT_GRAPHICS::TransferDataFromWindow` (pcbnew/import_gfx/dialog_import_graphics.cpp 174: the invert axis settings applied to the origin, `SetPlugin`, `SetLayer`, `SetImportOffsetMM( origin / scale )`, `SetLineWidthMM`, `LOCALE_IO`, `Load`, `Import( scale, scale )`) and `DIALOG_IMPORT_GFX_SCH::TransferDataFromWindow` (eeschema/import_gfx/dialog_import_gfx_sch.cpp 175) run SYNCHRONOUSLY (a read: the file is parsed into new items that belong to nothing yet), so the answer carries the uuids. The placement is queued on the apply coroutine: the frame (and the footprint, sheet or symbol it edits) re-resolved and compared; then `DRAWING_TOOL::PlaceImportedGraphics`' placement (group of 2 or more, `ConnectBoardShapes` with the tolerance) as ONE commit "Import Graphics" (board, footprint) or `SCH_DRAWING_TOOLS::ImportGraphics` and `SYMBOL_EDITOR_DRAWING_TOOLS::ImportGraphics`' as ONE commit "Import Graphic" (schematic, symbol).
- `place: "pointer"`: board and footprint through `PCB_CONTROL::placeBoardItems`' hand over (items staged with `commit.Add`, selected, reference point the top left item, `RunSynchronousAction( PCB_ACTIONS::move, &commit )`: a click pushes, Esc reverts and deletes); schematic through `SCH_EDITOR_CONTROL::Paste`'s staging (IS_NEW, IS_PASTED, IS_MOVING, both ends of lines, `AddToScreen`, `commit.Added`, hidden until the move tool shows them) and `SCH_ACTIONS::move`; symbol through `SYMBOL_EDITOR_EDIT_TOOL::Paste`'s (`commit.Modify( symbol )`, IS_NEW, IS_PASTED, SELECTED, `AddDrawItem`, `RebuildSelection`) and `SCH_ACTIONS::move`.
- Answer: `{"items": n, "uuids": [...], "warnings": [...]}`. `items` counts the drawing items (a group made by `group` is not counted and its uuid is not listed), `uuids` lists them (at most 4,096), `warnings` is KiCad's importer messages, one per line (at most 64). A file that loads nothing answers `items` 0 with its warnings and queues nothing. `""` for any other shape of arguments, no frame that imports, an open in flight, a dialog up, a layer not enabled, or no plugin for the extension. In `pointer` mode the answer comes before the placement: the uuids exist only once the click lands.
- No host ask, no marker, no KiCad dialog on any road.

Reported to E0: `dxf_import_plugin.h` (common/import_gfx) includes `<dl_creationadapter.h>`, which lives in `kicad/thirdparty/dxflib_qcad`; `compile_embind_tu` has `-I${KICAD_DIR}/thirdparty` but not that folder. Needed: `_includes+=" -I${KICAD_DIR}/thirdparty/dxflib_qcad"` (at the END of the list, so every earlier include resolves as before) for the fpedit and eeschema TUs (the whole function is fine). Without it both units fail with `fatal error: 'dl_creationadapter.h' file not found`.

Syntax check (from `/home/matthew/cc-engine-wt/e6/pcbjam`, `gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e6/kicad fpedit eeschema kicad_editor`), as committed:

```
FAIL fpedit         fpedit_embind.cpp subdir=pcbnew   (dl_creationadapter.h not found)
FAIL eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND   (the same)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (5391 ms)
```

The rest of the code was checked once with the DXF header replaced in the working tree by a one line declaration of `DXF_IMPORT_UNITS` and `DXF_IMPORT_PLUGIN::SetUnit` (same signature as KiCad's), then restored (nothing of it committed): `PASS eeschema (12950 ms)`, `PASS fpedit (7579 ms)`. So the include directory is the only fault left.

Export check (`scripts/kicad/check-run15-exports.sh --since run15/main --kicad /home/matthew/cc-engine-wt/e6/kicad --kicad-since run15/main`, after merging `run15/main`): `152 names registered in the merged image (145 kicad names), 7 of 110 run 15 names, 0 failures`.

What the build train must probe (fixtures: island `tests/fixtures/drawings/outline.dxf`, `logo.svg`, `fpedit-small/`, `symedit-small/`; write the drawing into MEMFS first, for example `/tmp/cc-gfx/outline.dxf`):
1. Board frame (Glasgow or fpedit-small's board): `Module.kicadImportGraphics('{"path":"/tmp/cc-gfx/outline.dxf","place":"at","layer":"Dwgs.User","dxfUnits":"mm"}')` answers items > 0 with as many uuids; the shapes show on Dwgs.User; the undo depth rises by exactly 1; one Ctrl+Z (`key.press`) removes all of them. Wrong: `""` (the dispatcher picked no half, or the plugin by extension failed), items 0 with a warning (the DXF reader), depth 2 or more (more than one commit).
2. The same with `logo.svg` and `"group":true`: one group holding every item (select one item, the whole group selects); with `"fixDiscontinuities":true,"tolerance":0.5` the outline's segments meet.
3. Footprint editor frame (`?frame=fpedit`, fpedit-small's SOIC open): `logo.svg` with `"layer":"F.SilkS","place":"at"` lands inside the footprint (it saves with the footprint through `fpedit.take`), one undo step. Wrong: the items on the editor's board but not in the footprint.
4. Schematic frame: `outline.dxf` `"place":"at","origin":[50,50]` lands near (50 mm, 50 mm); one Ctrl+Z removes it. Symbol editor frame (symedit-small): the same lands in the open symbol on the current unit, one undo step, `symedit.take` holds it.
5. `"place":"pointer"` in the board frame: the drawing follows the pointer, R rotates it, a click places it (one undo step "Import Graphics"); a second run then Esc leaves nothing and no undo step. The same in the schematic frame. Wrong: items left on the board after Esc, or a step on the undo stack after Esc.
6. Refusals answer `""`: an unknown key, a relative path, a path with `..`, a missing file, a `.txt` file, `"layer":"Nope"`, a layer the board does not enable, the drawing sheet editor frame. No KiCad window or message box on any road (the dialog is never constructed).

## Lane state at the relay (seat 2, 2026-11-18)

- The seat before was cut off by the machine fault; both worktrees were clean, nothing to stash. ds-e1 (gate and unit files) stands as written.
- fp-e8: committed above, waiting only on E0's `dxflib_qcad` include line; once `run15/main` carries it: `git merge run15/main`, the syntax check `all`, paste the lines here, then `e6.unit.fp-e8`.
- ds-e2: not started; it is cut after build G answers gate 0 items 1 to 4 (build G's gitlink bump `48e664e` is on `run15/main`, its result not yet in E0's notes).
- sy2-e: held (RULINGS 5): no diagnosis names an engine file (nothing under `cc-wave2/reports/` or `plan/` but the manifest's own lines).
