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

## Relay note (2026-11-18, after the machine fault)

The seat before this one was cut off after st-open-kicad5-e. Both worktrees were clean (no
uncommitted work, no commit without a status file); nothing was stashed. `run15/main` was
merged into both worktrees before st-ses-e began.

## st-ses-e: K-SES and kicadImportSession

Commits: kicad 78721919 (`pcbnew/specctra_import_export/specctra.h`,
`pcbnew/specctra_import_export/specctra_import.cpp`), pcbjam fcf55bc (`wasm/bindings/cc_e9_pcb.inc`).

Export (in `cc_e9_pcb.inc`, block `cc_e9_pcb`, every configuration of the board TU, not merged
image only: `DSN::ExportBoardToSpecctraFile` already links into occ_service from the same
library):

- `bool kicadImportSession( std::string aArgsJson )` (cap `session`). Args closed, exactly
  `{"run": int >= 0, "dir": "/tmp/cc-session/<n>", "path": "<abs>.ses", "preview": bool}`.
  False (nothing queued) for any other shape, a path not absolute, holding `..` or NUL, not
  ending `.ses` or naming no file, no board frame, an open in flight. Then on the apply
  coroutine: `DSN::CcCountSpecctraSession` (load and count, nothing changed); a preview stops
  there. An apply reads it through ONE `BOARD_COMMIT` (`DSN::CcImportSpecctraSession`),
  pushed as "Import Specctra Session", then as KiCad's own import ends: `ClearRatsnest`,
  `BuildConnectivity`, `Compile_Ratsnest( true )`. A throw reverts the commit (the board as it
  was). Records: `<dir>/session.json` `{run, preview, tracks, vias, nets, missingNets (<= 256),
  undoCleared: false, depth, lines}` plus on an apply `removed: {tracks, vias}` and
  `added: {tracks, vias}` (tracks count tracks and arcs; locked copper is neither removed nor
  added); then `<dir>/.cc-session.json` LAST `{error, message, lines}` (`error` ""
  or `session_invalid` or `island_error`). A refused parse writes no `session.json`.

K-SES (every change under `#ifdef __EMSCRIPTEN__`, `#else` KiCad's text word for word):

- `specctra.h`: a forward `class BOARD_COMMIT;`, the members
  `SPECCTRA_DB::FromSESSION( BOARD*, BOARD_COMMIT* )` and `SPECCTRA_DB::CountSESSION(...)`,
  the free functions `DSN::CcImportSpecctraSession` and `DSN::CcCountSpecctraSession`.
  HEADER CHANGE, recompile reach: the four `specctra_import_export/*.cpp` (and
  `python/scripting/pcbnew_scripting_helpers.cpp` where built); no other includer.
- `specctra_import.cpp`: `FromSESSION( aBoard )` calls the commit form with nullptr (KiCad's
  behavior). With a commit: unlocked tracks and vias and every marker `Remove`d through it
  (locked ones stay, so nothing to re-add), each placed footprint `Modify`d before it moves or
  flips, every new track, arc and via `Add`ed through it. `CountSESSION` throws the same
  IO_ERRORs as FromSESSION for missing sections, a placement reference the board lacks, a
  via padstack the session library lacks. `PCB_EDIT_FRAME::ImportSpecctraSession` (the menu
  path, held by the island) is unchanged: it still clears the undo list.
- `undoCleared` is always false: the commit form held (the removed copper goes to the undo
  list as any delete does, so no dangling pointer is left).
- The bindings declare the two DSN functions (specctra.h includes a generated lexer header
  outside the bindings' include roots, as `ExportBoardToSpecctraFile` before).

What the build train must probe (board frame console, business card board or Glasgow):

1. Export a DSN (`cc_specctra_dsn` output job), take the `.ses` st-ses-i writes (or a hand
   written one: `(session x (base_design x) (routes (resolution um 10) (parser) (library_out
   (padstack "Via[0-1]_600:300_um" (shape (circle F.Cu 6000)) (shape (circle B.Cu 6000))
   (attach off))) (network_out (net GND (wire (path F.Cu 2500 0 0 100000 0)) (via
   "Via[0-1]_600:300_um" 100000 0))))))` with a net name the board holds) into
   `/tmp/p/s.ses`.
2. `Module.kicadImportSession(JSON.stringify({run:1, dir:'/tmp/cc-session/1', path:'/tmp/p/s.ses', preview:true}))`:
   `true`; `session.json` `preview: true`, `tracks`/`vias` the session's segments and via
   instances, `missingNets` [] for names the board holds; `.cc-session.json` `error ""`;
   board unchanged, undo depth unchanged.
3. The same with `preview:false`, run 2: `removed` = the board's unlocked tracks and vias
   before, `added` = the session's counts, `undoCleared: false`, `depth` = depth before + 1.
   One Ctrl+Z (or `kicadUndo`) restores every old track and via and the markers; redo puts
   the session's copper back. No window of any kind; the ratsnest reflects the new copper.
4. Failure that means it is wrong: `depth` not before + 1, undo leaving the session's copper,
   tracks lost after undo, any dialog, a crash on undo (a dangling pointer: the commit path
   is wrong).
5. `corrupt.ses` (truncated): `.cc-session.json` `error: "session_invalid"`, a line with
   KiCad's parse message, no `session.json`, board unchanged. A session naming a reference the
   board lacks in `placement`: `session_invalid` "Reference 'X' not found." on preview and
   apply, board unchanged.
6. Refusals (false, nothing written): `{}`, an extra key, `dir` outside `/tmp/cc-session/`,
   a `.dsn` path, a missing file, a relative path.

## st-rescue-e: kicadSchRescue and kicadSchRescueApply

Commits: kicad 47345af5 (`eeschema/project_rescue.cpp`), pcbjam 4fec2d3
(`wasm/bindings/cc_e9_sch.inc`).

Diagnosis: KiCad's `SYMBOL_LIB_TABLE_RESCUER` finds candidates only from a LEGACY cache
library (`<project>-cache.lib`); a KiCad 6 or later design has none (its cache is the
schematic's own `lib_symbols`), so on the rescue fixture (MyParts:R, its file missing) KiCad's
rescuer as shipped lists nothing. The change stays in this lane's file:
`project_rescue.cpp` gains `CcSetRescueFromDesign( bool )` (under `__EMSCRIPTEN__`); while set,
`RESCUE_SYMBOL_LIB_TABLE_CANDIDATE::FindRescues` takes the symbol's own `LIB_SYMBOL`
(`GetLibSymbolRef()`, the design's copy) as the cache candidate when no legacy cache holds it.
KiCad 5's rule then applies unchanged: library missing and copy present, a candidate "Rescue
symbol X found only in cache library"; library present with pins that conflict with the
design's copy, "Rescue modified symbol"; illegal name characters, always. Set only by these
two exports (K-REMAP's legacy path never sets it).

Exports (both in `cc_e9_sch.inc`, block `cc_e9_sch`, every configuration, not merged only):

- `std::string kicadSchRescue( std::string aArgsJson )` (cap `rescue`), args exactly `{}`.
  Synchronous read: a subclass of `SYMBOL_LIB_TABLE_RESCUER` (`cc_e9::sch::CC_TABLE_RESCUER`,
  only accessors and an `AddSymbol` that records the new LIB_ID then calls KiCad's) runs
  `FindCandidates` and `RemoveDuplicates`. Answers `{"candidates": [{"name", "libId", "uses",
  "rescuedAs"}]}` (<= 4096): `name` the item name unescaped, `libId` the LIB_ID the design
  names (`GetRequestedName`), `uses` placed symbols naming it, `rescuedAs` the LIB_ID KiCad's
  own `PerformAction` gives it, learned by running it with no design symbol in reach (nothing
  relinked, the rescuer discarded). `""` with no frame, an open in flight, other JSON.
- `bool kicadSchRescueApply( std::string aArgsJson )` (cap `rescue`), exactly `{"run": int >=
  0, "dir": "/tmp/cc-rescue/<n>", "names": [1..4096 strings of 1..1024 bytes]}`. A name
  matches a candidate's `libId`, or its `name` when no other candidate shares it. On the apply
  coroutine: candidates found again; an unmatched name ends with `rescue_unknown`, nothing
  changed; every symbol the chosen candidates relink is `Modify`d on ONE `SCH_COMMIT` with its
  screen; then KiCad's `OpenRescueLibrary`, `DoRescues`, `WriteRescueLibrary` (headless:
  `CcSetRescueHeadless`, error boxes are lines); a failure reverts the commit
  (`rescue_failed`); else Push "Rescue Symbols", the symbol viewer's list re-created if open,
  `RecalculateConnections( nullptr, NO_CLEANUP )`, `SyncView`, refresh. The undo list is KEPT
  (KiCad's `rescueProject` clears it). Records `<dir>/rescue.json` `{run, rescued (candidates),
  symbols (relinked), files: [<abs stem>-rescue.kicad_sym, <abs>sym-lib-table] (those that
  exist), depth, lines}` then `<dir>/.cc-rescue.json` LAST `{error, message, lines}`.

What the build train must probe (schematic frame console, the st-rescue-myparts fixture):

1. `Module.kicadSchRescue('{}')`: a candidate `{name:"R", libId:"MyParts:R", uses:12,
   rescuedAs:"<stem>-rescue:R-MyParts"}`; nothing modified, undo depth unchanged.
2. `Module.kicadSchRescueApply(JSON.stringify({run:1, dir:'/tmp/cc-rescue/1', names:['MyParts:R']}))`:
   `true`; `.cc-rescue.json` `error ""`; `rescue.json` `rescued: 1`, `symbols: 12`, both files
   listed, `depth` = before + 1. The project folder holds `<stem>-rescue.kicad_sym` with
   `R-MyParts`, `sym-lib-table` a row `<stem>-rescue` type KiCad at
   `${KIPRJMOD}/<stem>-rescue.kicad_sym`. A second `kicadSchRescue('{}')` lists no MyParts:R.
   One undo restores the 12 symbols to `MyParts:R` (the files stay).
3. Failure that means it is wrong: `uses` 0 or the list empty (the design-as-cache switch did
   not reach FindRescues), any dialog (DIALOG_RESCUE_EACH, a message box), depth unchanged or
   the undo list cleared, symbols drawn as missing after the apply.
4. Refusals: `kicadSchRescue('{"x":1}')` `""`; apply with `names: []`, an extra key, `dir`
   outside `/tmp/cc-rescue/`: false. `names:['Nope:X']`: true then `rescue_unknown`, nothing
   written but the done file.

## m10-e: K-MIGRATE, kicadBusMigrationGet, kicadBusMigrationApply, kicadHostBusMigration

Commits: kicad 5b0e70fe (`eeschema/files-io.cpp`, `eeschema/dialogs/dialog_migrate_buses.cpp`),
pcbjam 4569199 (`wasm/bindings/cc_e9_sch.inc`).

K-MIGRATE (under `#ifdef __EMSCRIPTEN__`): `SCH_EDIT_FRAME::OpenProjectFiles` never builds
`DIALOG_MIGRATE_BUSES` (nor marks the design modified for it); the labels stay as the file
holds them. `dialog_migrate_buses.cpp`: `getProposedLabels`' rule lifted into the free
function `ccProposedBusLabels( CONNECTION_GRAPH*, const std::vector<wxString>& )`, the member
calling it; the `#else` keeps KiCad's member word for word. No header changed.

Exports (in `cc_e9_sch.inc`, block `cc_e9_sch`, every configuration, not merged only):

- `std::string kicadBusMigrationGet()` (cap `busMigration`): synchronous;
  `GetBusesNeedingMigration()` as `{"buses": [{"id", "sheetPath", "labels": [{"uuid",
  "text"}], "proposals": [string]}]}` (<= 1024), `proposals` one per label from
  `ccProposedBusLabels`. `""` with no frame or an open in flight.
- `bool kicadBusMigrationApply( std::string aArgsJson )`: exactly `{"run": int >= 0, "dir":
  "/tmp/cc-buses/<n>", "choices": [1..1024 of {"labels": [1..64 uuid strings], "name": 1..1024
  bytes with no line break}]}`. On the coroutine: every uuid resolved first
  (`Schematic().ResolveItem`, must be a `SCH_LABEL_BASE`), else `bus_label_unknown` and nothing
  changed; each label `SetText( name )` on ONE `SCH_COMMIT` pushed as "Name buses";
  `RecalculateConnections( nullptr, NO_CLEANUP )`, refresh. Records `<dir>/buses.json` `{run,
  applied (choices), labels (labels set), depth, lines}` then `<dir>/.cc-buses.json` LAST
  `{error, message, lines}`.
- `bool kicadHostBusMigration()`: marker, always true.

What the build train must probe (bus-conflict fixture, Schematic tab, Edit mode):

1. The engine opens the design with no Migrate Buses window (modal.ts `visibleDialogs` empty).
2. `Module.kicadBusMigrationGet()`: two buses; one with labels `DATA[0..7]` and `D[0..7]`,
   proposals `["DATA[7..0]", "D[7..0]"]` (KiCad's rule writes highest end first); the other
   `ADR[0..3]`, `A[0..3]` with `["ADR[3..0]", "A[3..0]"]`.
3. Apply naming each bus's labels `DATA[0..7]` and `ADR[0..3]` in `/tmp/cc-buses/1`: true;
   `buses.json` `applied: 2`, `labels: 4`, `depth` = before + 1; a second get answers
   `{"buses": []}`; one undo restores both names and the get lists both buses again.
4. Failure that means it is wrong: a window at open, an empty list on the fixture (the graph
   was not built: then the get ran before the open's RecalculateConnections), depth unchanged,
   undo restoring one label of four.
5. Refusals: an unknown uuid ends `bus_label_unknown`, nothing changed; `choices: []`, a name
   with a newline, an extra key, `dir` outside `/tmp/cc-buses/`: false.

## Checks of this seat (st-ses-e, st-rescue-e, m10-e) and the handover

Each unit was syntax checked before its commit on the union of the three (pcbjam 4fec2d3 is
4569199's file less the m10-e blocks, a strict subset, and was not checked alone). The run on
the unmerged tips (pcbjam 4569199, kicad 5b0e70fe), with also specctra.cpp and
specctra_export.cpp (the other includers of specctra.h): every unit PASS, every KiCad file
PASS, `syntax-check: 0 failed`.

Handover, after `git merge run15/main` in both worktrees (kicad tip 4f0a3a5d):

```
/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e9/kicad all --kicad-file pcbnew/specctra_import_export/specctra_import.cpp --kicad-file eeschema/project_rescue.cpp --kicad-file eeschema/files-io.cpp --kicad-file eeschema/dialogs/dialog_migrate_buses.cpp
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (13810 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (16346 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (10548 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (8640 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (1158 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (6568 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (14874 ms)
PASS kicad pcbnew/specctra_import_export/specctra_import.cpp (9116 ms)
PASS kicad eeschema/project_rescue.cpp (8991 ms)
PASS kicad eeschema/files-io.cpp (11074 ms)
PASS kicad eeschema/dialogs/dialog_migrate_buses.cpp (7824 ms)
syntax-check: 0 failed
```

```
scripts/kicad/check-run15-exports.sh --since run15/main --kicad /home/matthew/cc-engine-wt/e9/kicad --kicad-since run15/main
check-run15-exports: 160 names registered in the merged image (153 kicad names), 15 of 110 run 15 names, 0 failures
```

All eight lane names are registered: kicadImportLegacyProject, kicadHostLegacy (cc_e9_sch),
kicadImportSession (cc_e9_pcb), kicadSchRescue, kicadSchRescueApply, kicadBusMigrationGet,
kicadBusMigrationApply, kicadHostBusMigration (cc_e9_sch). None is merged image only.
Nothing reported to E0 (no include path, CMake line, build script line or new name needed).
The one KiCad header changed is specctra.h (reach: the specctra_import_export sources).

## Repair e9-1 (2026-11-18): the PARKER policy on the two exports that park

Fault (build G probe, `/home/matthew/cc-wave2/engine/fix/e9-1.md`): on `legacy_demos/ecc83` and
`electric`, `kicadImportLegacyProject` threw `SuspendError: trying to suspend without
WebAssembly.promising` after about 1.5 s; only `~ecc83-pp.sch.lck` was written.

Cause, verified in the code: `cc_e9_sch` registered the export with no
`PCBJAM_PARKER_POLICY` (`pcbjam_async_policy.h`: `, emscripten::async()`). Its body calls
`SCH_EDIT_FRAME::OpenProjectFiles`, the same call `kicadOpenFile` makes (registered PARKER,
`eeschema_embind.cpp` 7216, as `kicadImportSchematic` 7179). The park inside the open: a symbol
read on the main thread through `SCH_IO_PCBJAM_LIB::loadOne` goes to
`pcbjam_libs_request_dispatch` (`eeschema/sch_io/pcbjam_lib/sch_io_pcbjam_lib.cpp` 215), which
on the main runtime thread waits with `wxWasmBeginWait` and `wxWasmYieldUntil` (a JSPI
suspension). Without the policy the invoker is not wrapped in `WebAssembly.promising`, so
that first wait throws.

The same miss in a second export: `kicadSchRescue` runs `RESCUE_SYMBOL_LIB_TABLE_CANDIDATE::
FindRescues` in the call, which reads each library symbol through `SchGetLibSymbol` (
`project_rescue.cpp` 431), that is `SCH_IO_PCBJAM_LIB::LoadSymbol`, the same wait for a symbol
not yet in the provider's cache. Registered PARKER too. The other lane exports do not park in
the call: `kicadSchRescueApply`, `kicadBusMigrationApply` and `kicadImportSession` only queue
their run on the apply coroutine (`pcbjam_collab::runOnCoroutine`, run from the event loop);
`kicadBusMigrationGet` reads the connection graph only; `kicadHostLegacy` and
`kicadHostBusMigration` return true. They stay as they were.

Change (pcbjam 5bac987, `wasm/bindings/cc_e9_sch.inc`): both registrations carry
`PCBJAM_PARKER_POLICY`; the two bodies are unchanged (KiCad's open and save, KiCad's rescuer).
No KiCad file changed.

What this changes for the island: `kicadImportLegacyProject` now answers `Promise<boolean>` and
`kicadSchRescue` `Promise<string>`. The island's import and rescue paths must `await` both (as
they await `kicadImportSchematic` and `kicadSimNetlist`); a caller that reads the return
synchronously sees a Promise object, never `true` or the candidates JSON.

Checks (from the pcbjam worktree, after `git merge run15/main` in both worktrees):

```
/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e9/kicad all
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (21913 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (16500 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (8800 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (8736 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (1546 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (6461 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (15416 ms)
syntax-check: 0 failed
```

```
scripts/kicad/check-run15-exports.sh --since run15/main --kicad /home/matthew/cc-engine-wt/e9/kicad --kicad-since run15/main
check-run15-exports: 205 names registered in the merged image (198 kicad names), 60 of 110 run 15 names, 0 failures
```

What the next build must probe:

1. Gate 0.6 as the fix note ran it, now awaited: `await Module.kicadImportLegacyProject(
   JSON.stringify({path: '/tmp/p/ecc83/ecc83-pp.pro', run: 1, dir: '/tmp/cc-legacy/1'}))`. A
   pass: no `SuspendError`; the call resolves `true`; no window of any kind (registry empty,
   no `ev.menu`); `/tmp/p/ecc83/` holds `ecc83-pp.kicad_sch`, `ecc83-pp.kicad_pro`,
   `sym-lib-table` (a row `ecc83-pp-rescue`, type KiCad, `${KIPRJMOD}/ecc83-pp-rescue.kicad_sym`)
   and `ecc83-pp-rescue.kicad_sym`; `/tmp/cc-legacy/1/.cc-legacy.json` `opened: true`,
   `saved: true`, those files and the rescue lines; every `lib_id` in the saved sheet names
   `ecc83-pp-rescue:<name>`; undo depth 0. Then the rest of this lane's st-open-kicad5-e list
   (items 2 to 6 above), `electric/` resolving `true` with its `.kicad_sch` saved.
2. A failure that means the repair is wrong or incomplete: any `SuspendError` (then another
   call in the open path parks outside the policy's wrap, a fresh JS entry such as a timer:
   its stack names it), the call returning a value that is not a Promise, or `saved: false`.
   A window or a missing `-rescue.kicad_sym` is the K-REMAP body, read for the first time in
   this build (the fix note: gate 0.6 was unread).
3. `kicadSchRescue`: `await Module.kicadSchRescue('{}')` on the st-rescue-myparts fixture
   with the engine's library set present (a symbol of `Device` not yet loaded makes the read
   park): resolves to the candidates JSON of the st-rescue-e list above, no `SuspendError`.
   `kicadSchRescue('{"x":1}')` resolves `""`.
