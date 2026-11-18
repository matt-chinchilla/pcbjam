<!-- Copyright (C) 2026 Circuit Center Inc -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Engine run 15, lane e9: legacy imports, rescue, bus migration and routed sessions

Units in order: st-open-kicad5-e (build G gate), st-ses-e, st-rescue-e, m10-e. Worktrees
`/home/matthew/cc-engine-wt/e9/pcbjam` and `/home/matthew/cc-engine-wt/e9/kicad`, branch
`seat/run15-e9` in both. Faces: `wasm/bindings/cc_e9_sch.inc` (block `cc_e9_sch`) and
`wasm/bindings/cc_e9_pcb.inc` (block `cc_e9_pcb`). No lease, no early header.

## st-open-kicad5-e: K-REMAP and kicadImportLegacyProject (build G gate)

Commits: kicad 73e270ac (`eeschema/files-io.cpp`, `eeschema/dialogs/dialog_symbol_remap.cpp`,
`eeschema/project_rescue.cpp`), pcbjam 1f93c1f (`wasm/bindings/cc_e9_sch.inc`).

Exports (both in `cc_e9_sch.inc`, block `cc_e9_sch`, registered in every configuration of the
eeschema TU, not merged image only):

- `bool kicadImportLegacyProject( std::string aArgsJson )` (cap `importLegacy`). Args closed:
  `{"path": "<abs>/<stem>.pro"}` or `{"path", "run": int >= 0, "dir": "/tmp/cc-legacy/<n>"}`.
  Refused (false, nothing touched) with no schematic frame, an open in flight, any other
  shape, a path that is not absolute, holds `..` or NUL, does not end `.pro`, or whose `.pro`
  or `<stem>.sch` is missing. Then, under the open's busy guard and synchronously (as
  `kicadImportSchematic`): `CcLegacyHostBegin( &lines )`, `OpenProjectFiles( { <stem>.sch } )`,
  `SaveProject( false )`, `CcLegacyHostEnd()`. True only when the open and the save both
  succeed. With a run folder, `<dir>/.cc-legacy.json` is written last:
  `{"opened": bool, "saved": bool, "files": [abs path], "lines": [{severity, text}]}`
  (`files`: every screen's file, the `.kicad_pro`, `sym-lib-table`, each project table row's
  file that exists). No commit: the undo list starts empty as after any open.
- `bool kicadHostLegacy()` marker, always true.

Host asks: none (the marker pattern only: the island lists `importLegacy` when the export is
present).

KiCad changes, every one under `#ifdef __EMSCRIPTEN__` (plain builds compile exactly what they
compiled before; where a body had to be re-indented the original stands word for word under
`#else`):

- `files-io.cpp`: the host switch `CcLegacyHostBegin( REPORTER* )`, `CcLegacyHostActive()`,
  `CcLegacyHostEnd()` (file scope, external linkage, declared again in `cc_e9_sch.inc`). While
  active, in `SCH_EDIT_FRAME::OpenProjectFiles`: the legacy libraries are loaded as
  `PROJECT_SCH::LegacySchLibs` loads them, its "Not Found" box a line (a KiCad 5 project names
  libraries the browser does not hold); the "error fixed" info box a line; the remap runs
  `CcRemapLegacySymbols` in place of `DIALOG_SYMBOL_REMAP`; the illegal library list box a
  line; the symbol library table rescue runs headless (every candidate, also when the user
  setting `m_RescueNeverShow` is set); the missing cache box takes "Load Without Cache File"
  and says why; then `CcMigrateProjectLegacyLibs` saves every legacy row in the project
  folder as `.kicad_sym`; the load reporter's lines join the host's; no info bar. In
  `SaveProject`: a null project or Save As refuses with a line (no file chooser), the locked
  files box a line and false, the overwrite box a line and the save goes on.
- `dialog_symbol_remap.cpp`: `CcRemapLegacySymbols( SCH_EDIT_FRAME*, REPORTER& )` is
  `OnRemapSymbols` less `backupProject` (the page keeps the dropped files) with the rescue
  headless; the four private members it calls are lifted as static copies taking the frame's
  project. `CcMigrateProjectLegacyLibs( PROJECT&, REPORTER& )` is the symbol library table
  panel's Migrate Libraries (`SCH_IO_MGR::ConvertLibrary`) for project rows of type Legacy
  whose file sits in the project folder: the row keeps its nickname (every LIB_ID stays
  valid), URI `${KIPRJMOD}/<name>.kicad_sym`, type KiCad; table saved, `ProjectChanged()`.
- `project_rescue.cpp`: `CcSetRescueHeadless( REPORTER* )` / `CcRescueHeadless()`. While set,
  `LEGACY_RESCUER::InvokeDialog` and `SYMBOL_LIB_TABLE_RESCUER::InvokeDialog` take every
  candidate (a line each, the candidate's own action description), `RescueProject`'s two
  message boxes and both `WriteRescueLibrary` error boxes become lines.

No KiCad header changed. No new KiCad file. Nothing reported to E0.

Syntax check (from the pcbjam worktree):

```
/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e9/kicad eeschema --kicad-file eeschema/files-io.cpp --kicad-file eeschema/dialogs/dialog_symbol_remap.cpp --kicad-file eeschema/project_rescue.cpp
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (18465 ms)
PASS kicad eeschema/files-io.cpp (65745 ms)
PASS kicad eeschema/dialogs/dialog_symbol_remap.cpp (9692 ms)
PASS kicad eeschema/project_rescue.cpp (8459 ms)
syntax-check: 0 failed
```

Export check (against the seat base, before merging run15/main):

```
scripts/kicad/check-run15-exports.sh --since 9c8a02f --kicad /home/matthew/cc-engine-wt/e9/kicad --kicad-since 1b214bab
check-run15-exports: 148 names registered in the merged image (141 kicad names), 3 of 110 run 15 names, 0 failures
```

What the build train must probe (gate 0.6, build G):

1. Copy `qa/data/pcbnew/plugins/legacy_demos/ecc83/ecc83-pp.pro`, `ecc83-pp.sch` and
   `ecc83-pp.cache.lib` into a fresh folder `/tmp/p/ecc83/` of the engine's file system, then
   in the schematic frame's console:
   `Module.kicadImportLegacyProject(JSON.stringify({path: '/tmp/p/ecc83/ecc83-pp.pro', run: 1, dir: '/tmp/cc-legacy/1'}))`.
   Expect `true`, NO window of any kind (`modal.ts` `visibleDialogs` empty, no `ev.menu`), and
   in `/tmp/p/ecc83/`: `ecc83-pp.kicad_sch`, `ecc83-pp.kicad_pro`, `sym-lib-table`,
   `ecc83-pp-rescue.kicad_sym` (plus `ecc83-pp-rescue.lib`, the rescue's own legacy file).
   `sym-lib-table` holds a row `ecc83-pp-rescue` of type KiCad at
   `${KIPRJMOD}/ecc83-pp-rescue.kicad_sym`. `/tmp/cc-legacy/1/.cc-legacy.json` has
   `opened: true`, `saved: true`, the files above, and lines including "The following
   libraries were not found:" and one "Rescue ..." action line per cache symbol.
2. Every symbol resolves: in the saved `.kicad_sch` every `(lib_id "...")` names
   `ecc83-pp-rescue:<name>` and the file's `lib_symbols` holds each one; `kicadCheckSheet`
   on the opened design reports no `lib_symbol_issues` "not found" finding.
3. Undo depth 0 after the import (an open clears the list).
4. Failure that means it is wrong: any dialog seen (a box of `project_sch.cpp`, the remap or
   rescue windows, the missing cache box), `.cc-legacy.json` `saved: false`, a missing
   `-rescue.kicad_sym`, or symbols still carrying an empty nickname in the saved file.
5. `electric/` (the second legacy demo) through the same call: `true`, `.kicad_sch` saved.
6. Refusals, each `false` with nothing written: `{}`, `{"path": "relative.pro"}`,
   `{"path": "/tmp/p/ecc83/ecc83-pp.sch"}`, `{"path": "/tmp/p/none.pro"}`, a run folder
   outside `/tmp/cc-legacy/`, an extra key.

Known reach outside the lane: `LEGACY_SYMBOL_LIBS::LoadAllLibraries` builds (never shows) an
`APP_PROGRESS_DIALOG`; the rescue's `WriteRescueLibrary` for the legacy rescuer calls
`LoadAllLibraries` itself and swallows its "not found" exception. If the probe still sees a
window, its title names the file to change; it is reported, not worked around.
