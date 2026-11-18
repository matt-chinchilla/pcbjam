<!-- Copyright (C) 2026 Circuit Center Inc -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Engine run 15, boot-e1: the start-up (BUILD NOTES)

RULINGS 15 (owner 2026-11-17: nothing of KiCad's before his engine). The owner's report: every time he goes to the Board view or the Schematic view he sees KiCad's own interface before our engine view. The island (cc24, `src/reveal.ts`) already holds its loading screen until the first drawn frame; build G's in-frame recorder still saw the chrome laid out and painted for one animation frame under that screen in every start. Three engine changes remove what the island had to wait out, plus a marker for the island (unit boot-i1).

Branches `seat/run15-boot` in both repositories, cut from `run15/main` (pcbjam d11a986, kicad caf17fbe), worktrees `/home/matthew/cc-engine-wt/boot/pcbjam` and `/home/matthew/cc-engine-wt/boot/kicad`. Tips at hand over (each after `git merge run15/main`): see the last commit of this file's branch and `/home/matthew/cc-wave2/engine/status/boot.done`.

## The three changes and where each lands

| change | file | owner | commit |
|---|---|---|---|
| 1. boot with the chrome already hidden | kicad `common/kiway.cpp` (KIWAY::Player calls the hook), NEW kicad `include/cc_boot_chrome.h` (the hook), pcbjam `wasm/bindings/cc_boot_dispatch.inc` (the hook's body and the input), pcbjam `wasm/bindings/kicad_editor_embind.cpp` (the toggle split into `applyChrome( frame, show )`) | no lane (kiway.cpp, the dispatcher TU is E0's) | kicad 20a2d66d, pcbjam f749021 |
| 2. kicadSetChrome repaints before it returns | pcbjam `wasm/bindings/kicad_editor_embind.cpp` (`repaintChromeNow`) | E0's | pcbjam f749021 |
| 3. the empty drawing area in the palette's background, never wx's beige | kicad `common/draw_panel_gal.cpp` | no lane | kicad 20a2d66d |

No file of any lane is touched, so nothing waited on a lane's merge (single_top.cpp, e6's, is not changed: the hook sits in KIWAY::Player, which single_top.cpp calls before it shows the frame).

### 1. The start-up hold

- **The input:** the page sets `window.kicadBoot = { chrome: false }` before the engine's script runs (the island: before `bootKicadTool`). Without it the boot is exactly as before (PCBJam's own pages and an older island are unchanged).
- **The KiCad call site:** `common/kiway.cpp` `KIWAY::Player`, right after `CreateKiWindow` returns (line 499 on 430df587): under `__EMSCRIPTEN__`, `if( frame && !m_top )` the hook `ccBootFrameHook()` (`include/cc_boot_chrome.h`, an inline function's local static, null in any image that sets none) gets the new frame. `m_top` is null until single_top.cpp's `Kiway.SetTop( frame )`, which comes after the Player call and before `frame->Show()`, so the hook sees the frame being booted (sch, pcb, symedit, fpedit, ds: every `--frame` token goes through this one Player call), constructed and not yet shown or painted. A frame a constructor might create on the way would be handed first; the outer frame, handed last, is the one the hold keeps (the snapshot is keyed to the frame).
- **The body:** `cc_boot::onBootFrame` (`cc_boot_dispatch.inc`), set as the hook at static initialization (`kBootFrameHook`), reads the input (`EM_ASM_INT`, a throwing read counts as no) and calls `applyChrome( frame, false )`: the same sticky hold `kicadSetChrome( false )` sets (the snapshot of what KiCad showed, `s_chromeSnap.hidden`, the render and size handlers on the frame's AUI manager, the menu bar and status bar hidden, every pane but the canvas hidden, `mgr->Update()`, the size event, `ev.chrome` reported false). The frame's first paint, at `frame->Show()` in single_top.cpp, is already the canvas only layout. A later `kicadSetChrome( true )` brings KiCad's own chrome back from the snapshot taken at boot, as before.
- **The toggle split:** `kicadSetChrome( aShow )` is now `applyChrome( topFrame, aShow )` then `repaintChromeNow( topFrame )`; `applyChrome` is the old body word for word with the frame given (it no longer reads `GetTopWindow()`, which is not set yet at boot). The theme hook (`setColorTheme`'s `g_afterThemeApplied`) still calls `kicadSetChrome( false )`.

### 2. kicadSetChrome repaints before it returns

`repaintChromeNow( frame )` (`kicad_editor_embind.cpp` line 593): `frame->Refresh()` then `wxTheApp->Paint( /*deferGLCanvasWindows=*/true )`, the synchronous repaint a mouse button runs in the wx wasm port (`wxApp::HandleMouseEvent`). Skipped when the frame is not shown, while a dispatch chain is parked (`wxWasmDispatchParked()`, the half changed widget state the interlock protects) or while an open is in flight (`pcbjam_open::busy()`); the pump paints when it resumes, as before. So the stage never keeps the old picture (toolbars, panes, status bar) after the call for as long as the engine's loop is held back (the host's open, a second engine's boot on the thread).

### 3. The empty drawing area

`common/draw_panel_gal.cpp`, under `__EMSCRIPTEN__`: `ccEraseToDrawingBackground` (line 88) fills an erase with the painter's background color (`GetView()->GetPainter()->GetSettings()->GetBackgroundColor()`, opaque), the color DoRePaint gives the GAL to clear to: the color theme the frame loaded at boot (the island's seeds write `circuitcenter` before `main()`). Bound on the drawing panel (constructor, line 167) and on the GAL's own window (`SwitchBackend`, line 743: WEBGL_GAL is a wxGLCanvas, its 2D erase is what the browser shows through the WebGL canvas until its first draw). With no painter yet the erase is skipped to wx's own, as before. wx's face color was (212, 208, 200) by day, the beige the island measured.

## Exports and markers

| name | kind | file | block | merged image only |
|---|---|---|---|---|
| `kicadBootCanvasOnly` | marker, answers true: this engine reads `window.kicadBoot.chrome`, its `kicadSetChrome` paints before it returns, and its empty drawing area is the drawing's background | `cc_boot_dispatch.inc` | `EMSCRIPTEN_BINDINGS( cc_boot_dispatch )` | yes (the dispatcher TU exists only in the merged image) |

No host ask, no new window object besides the input `window.kicadBoot` (read once per frame created before the top is set).

## Reported to E0

`/home/matthew/cc-wave2/engine/fix/e0-boot.md`: the include line `#include "cc_boot_dispatch.inc"` after `cc_e12_dispatch.inc` in `kicad_editor_embind.cpp`; the list line `new kicadBootCanvasOnly boot boot-e1`; and the check script's `owner_of` taking `boot` as a lane (`cc_(e\d+|boot)_\w+\.inc`), without which the list line fails. Header reach: one NEW KiCad header included by `kiway.cpp` and the inc only; two KiCad `.cpp` files change; no widely included header.

## The checks

Both run from `/home/matthew/cc-engine-wt/boot/pcbjam` after `git merge run15/main` in both worktrees (pcbjam b7915f2, kicad 430df587).

Clean, as committed (the inc not yet included):

```
scripts/kicad/check-run15-exports.sh --since run15/main --kicad /home/matthew/cc-engine-wt/boot/kicad --kicad-since run15/main
check-run15-exports: 213 names registered in the merged image (206 kicad names), 68 of 110 run 15 names, 0 failures

/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/boot/kicad all --kicad-file common/kiway.cpp --kicad-file common/draw_panel_gal.cpp
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (15063 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (15493 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (8289 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (8876 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (1276 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (6750 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (17630 ms)
PASS kicad common/kiway.cpp (5258 ms)
PASS kicad common/draw_panel_gal.cpp (7155 ms)
syntax-check: 0 failed
```

With the three integrator lines of e0-boot.md applied (uncommitted, then reverted):

```
check-run15-exports: 214 names registered in the merged image (207 kicad names), 69 of 111 run 15 names, 0 failures
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (17685 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (18083 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (9668 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (8934 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (1308 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (6314 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (14740 ms)
PASS kicad common/kiway.cpp (4422 ms)
PASS kicad common/draw_panel_gal.cpp (6072 ms)
syntax-check: 0 failed
```

Without the `owner_of` line the list line alone fails: `FAIL kicadBootCanvasOnly: registered at wasm/bindings/cc_boot_dispatch.inc:60, a file of no lane; the list gives it to boot`. A planted `static_assert( false, "planted" )` at the inc's end gave `FAIL kicad_editor` (the unit compiles the inc once it is included); reverted.

## What the build train must probe

From a scratch island worktree of `w2/base` on its own port pair, in the GPU window, one tab at a time closed in a `finally`, no timing read while a build runs. Until boot-i1 lands, set the input by hand in the probe (`window.kicadBoot = { chrome: false }` in the island frame before `bootKicadTool`, or a probe descriptor as build G did).

1. **Marker:** `Module.kicadBootCanvasOnly()` is `true` in every frame (sch, pcb, symedit, fpedit, ds). Wrong if missing: the include line or the list line was not added.
2. **The hold at boot**, each frame `--frame=sch`, `pcb`, `symedit`, `fpedit`, `ds`, with the input set: `Module.kicadChromeShown()` is `false` as soon as `Module` answers (at `ev.ready` and before any `kicadSetChrome` call); the in-frame recorder (a 2D context watch on `#canvas` from the first paint) sees NO frame with the menu bar, a toolbar, a pane caption or the status bar: the first painted frame is already canvas only. Wrong if the first painted frame has chrome: the hook was not reached (KIWAY::Player not the first creation path, or `m_top` already set), or the input was read false.
3. **Without the input** (an older island, PCBJam's pages): the boot shows KiCad's chrome as before and `kicadSetChrome( false )` hides it; `kicadSetChrome( true )` after a boot hold brings back KiCad's own menu bar, toolbars and status bar and only the panes KiCad itself showed (the snapshot), never the default hidden ones (Search, Net Inspector).
4. **kicadSetChrome paints before it returns:** on a shown frame, `kicadSetChrome( true )`, let it paint, then `kicadSetChrome( false )` and, in the same task (before the engine's loop can turn), read `#canvas` with `getImageData` at the old toolbar and status bar rows: they must already be the canvas (no toolbar pixels). Wrong if they still hold the old picture: the paint was skipped or deferred. Also: while an open is in flight (`kicadOpenFileBusy()` true), `kicadSetChrome` still answers true and nothing crashes (the paint is skipped then).
5. **The empty drawing area:** in each frame, before the GAL's first WebGL frame and in a frame with no document (symedit with no symbol, ds, a pcb or sch boot with no open), the 2D pixels of `#canvas` under the drawing area equal the color theme's background (`circuitcenter.json` background of that face: schematic background for sch and symedit, board background for pcb and fpedit, the drawing sheet background for ds), never (212, 208, 200) by day or (45, 45, 62) by night. Wrong if beige: the painter was not there at the erase, or another window paints over the area.
6. **Each frame's start with the screencast classifier** (`rec.mjs`, `summ.py` in the probe folder): schematic, board, symedit, fpedit, ds, each at 1x and at 4x slowed CPU (at least 3 starts at 1x and 2 at 4x each, as build G read ds and fpedit): ZERO chrome frames and ZERO beige frames, with the island's own waits in place (cc24's reveal) and, once boot-i1 lands, with its marker path. Also the held second engine (the board engine booted while the schematic is shown, at opacity 0) and a frame switch (Schematic to Board and back on the site), the owner's report.
7. **No regression:** `kicadChromeShown()` and `ev.chrome` report the true state after a pane toggle (Ctrl+H) while held (sticky handlers rehide, `ev.chrome` stays false); a theme switch (`setColorTheme`) leaves the chrome hidden; the right-drag pan and the first click (the island's synthetic focus click) still work; heap at ready unchanged within noise against build G's 386,662,400 bytes (ds) and the sch frame's 556,793,856.
