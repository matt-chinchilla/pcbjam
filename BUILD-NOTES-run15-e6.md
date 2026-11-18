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
