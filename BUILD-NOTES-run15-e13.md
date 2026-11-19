<!-- Copyright (C) 2026 Circuit Center Inc -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Engine run 15, lane e13: simulator faults, user signals, the ngspice service, multichannel

Branch `seat/run15-e13` in pcbjam (worktree `/home/matthew/cc-engine-wt/e13/pcbjam`) and in
kicad-source-mirror (worktree `/home/matthew/cc-engine-wt/e13/kicad`, no commit: the lane's KiCad
file `eeschema/sim/sim_lib_mgr.cpp` needed no change, see lo-e2-e). Units in order: lo-e2-e,
m11-e, st-mc-e, lo-e2-svc-e (conditional). The unit sections named `BUILD-NOTES-run15-sim-faults.md`
and `BUILD-NOTES-run15-sim-stop.md` by the manifest are written here.

Seat start (2026-11-18): both worktrees clean, no commit past `run15/main`, no notes file and no
`engine/fix/e13-*.md`: nothing was left by an earlier seat.

## lo-e2-e: the simulator's faults (ledger SI1, SI2, SI3, SE38)

Commit pcbjam `6e124f7`. No new export, no host ask, no marker. Changed exports:
`kicadSimModelGet`, `kicadSimModelSet`, `kicadSimFileRead`, `kicadSimNetlist`, `kicadSimRun`,
`kicadSimStop` (all still registered in `EMSCRIPTEN_BINDINGS(eeschema)`, outside the merged
guard, as in run 14). Files: `wasm/bindings/eeschema_embind.cpp` (the simulator section and its
helpers in the anonymous namespace just above it, 4425 to 6072 at the base: see "Lease" below; plus
one dated "Modified by Circuit Center Inc" line in the file header), `wasm/bindings/cc_e13_lease.h`.

What changed, and why:

1. **No modal box for a missing model file (SI2).** The box is not raised in `sim_lib_mgr.cpp`
   (its `ResolveLibraryPath` reports through the caller's REPORTER: already a `file_missing`
   problem per symbol). It is `NETLIST_EXPORTER_SPICE::writeInclude`
   (`eeschema/netlist_exporters/netlist_exporter_spice.cpp` 670, not a file of this lane),
   which `wxLogError`s "Could not find library file" for every library it cannot resolve; the GUI
   log shows that as a modal box and every later sim op waits behind it. Caught on the pcbjam side,
   so NO KiCad commit: `cc_e13::SIM_LOG_CAPTURE` (lease slot) is a scoped `wxLog` target that keeps
   wxLogError and wxLogWarning lines while it lives; `appendLogProblems` turns them into problems
   (`file_missing` with `path` for that line, else `other`, deduplicated by text). Alive in
   `kicadSimNetlist` (whole body), `startSimRun` (the annotation check through the attach),
   `kicadSimModelGet`, `kicadSimFileRead` (lines added to their `problems`, stamped with the
   symbol or path) and `kicadSimModelSet` (lines dropped: a refusal is `false`). A run whose
   netlist hit the line now ends `circuit` with the `file_missing` problem instead of reaching
   ngspice. Note: `wxLog::SetActiveTarget` flushes the previous target, so a message some other
   code logged earlier and the GUI log still holds is shown at that moment instead of at the next
   idle (no new box, an earlier one).
2. **A value KiCad wants as a number is refused (SI2).** `SIM_MODEL::SetParamValue` keeps any
   text. `readSimParams` now asks `cc_e13::simNumberAccepted` for params of
   `SIM_VALUE::TYPE_INT` and `TYPE_FLOAT`: taken are `""` (the default), KiCad's own SI reading
   (`SIM_VALUE::ToDouble`, `ToInt`), SPICE notation with a digit first (`1Meg`, `10kOhm`:
   `SIM_VALUE_GRAMMAR::IsValid` SPICE), a `{...}` expression, text holding `${`. `abc` is refused:
   `kicadSimModelSet` answers false, nothing queued.
3. **A subcircuit's pins (SI2, AMP10).** `simFileJson` listed `GetPinNames()`, which is empty for
   `SIM_MODEL_SUBCKT` (its pins come from the `.subckt` line through `AddPin`); when empty it now
   lists `GetPins()`' model pin names. Other models unchanged (Q2N3904 still `C, B, E, <S>`).
4. **no_model and pin_map (SI3).** `collectModelProblems` (netlist and run) keeps the model it
   makes: type `SIM_MODEL::TYPE::NONE` (KiCad found nothing to build one from: Glasgow's "U1 __U1")
   is a `no_model` ERROR problem "<ref> has no simulation model" (only when that symbol's model
   read said nothing else); a model pin assigned to a pin number the symbol does not have is a
   `pin_map` error; a required model pin (no angle brackets) on no symbol pin a `pin_map` warning.
   References starting `#` are skipped (as `ERC_TESTER::TestSimModelIssues`). KiCad's own pin map
   refusal text ("Unknown simulation model pin") is now coded `pin_map` in `appendProblems`. An
   error problem refuses the run (`circuit`), so Glasgow's Simulate lists every model-less part
   instead of a bare `sim_failed`.
5. **Runs keyed by number (SI3).** `SIM_LIVE_RUN.running`: the reporter counts an idle event only
   after the running event of the same run (the service sends running then idle per `bg_run`, in
   order, so a halted run's late idle arriving after the next `bg_run` comes before that run's
   running event and is ignored). The queued finish captures the run number
   (`finishSimRun( long long aRun )` returns unless the live run has that number); `kicadSimStop`'s
   queued body captures it too and never halts or finishes a later run.

SI1 Stop on a fine step: see lo-e2-svc-e and the lo-e2-e follow-up below (released by
RULINGS 29a on 2026-11-19). SE38 needs no engine change beyond the above.

**Lease.** The guide's lease names the bodies from `kicadSimModelCatalog` to `kicadSimOpOnSheet`;
the unit's own record names "the simulator section ... and their helpers". Items 2 to 5 live in
those helpers (anonymous namespace 4425 to 6072, simulator code only, no other lane writes there).
Edited in place and REPORTED to E0; new helpers went to the lease slot where they could.

**Header change:** none in KiCad. Recompile reach: `eeschema_embind.cpp` only.

**Merged image only:** none of these (eeschema TU, outside any guard, as before).

### What the build train must probe (lo-e2-e)

- `kicadSimNetlist('{"command":".op","options":{"voltages":true,"currents":true,"dissipations":false,"events":false}}')`
  on Glasgow (await: PARKER): `problems` holds a `no_model` error row with `ref` "U1", `uuid`,
  `sheetPath`, and one per other model-less part; no KiCad window. Wrong: no `no_model` row, or a
  box.
- `kicadSimRun` on Glasgow (`.op`): `.cc-sim.json` `error: "circuit"`, `problems` as above, no
  `result.json`. Wrong: `sim_failed`.
- The island's `sim-missing-lib` fixture (lo-e2-i makes it): `kicadSimNetlist` answers a
  `file_missing` row (KiCad's "Simulation model library not found" with the symbol, and
  "Could not find library file '<path>'" with `path`), no modal box; a second `kicadSimNetlist`
  right after answers (not `""`, not hung). `kicadSimRun` records `circuit`. Wrong: a box, or the
  second call waits.
- sim-divider: `kicadSimModelSet` with R1 builtin `R`, params `{"r":"abc"}`: false, undo depth
  unchanged; `{"r":"10k"}`, `{"r":"1Meg"}`, `{"r":"{RVAL}"}`: true, one undo step each. Wrong:
  `abc` taken, or `1Meg` refused.
- sim-models: `kicadSimFileRead('{"path":"models/<the AMP10 file>"}')`: AMP10 `kind: "subckt"`
  with its `.subckt` pin names in order; Q2N3904 still `C, B, E, <S>`; CC_BUF `1, 2, 3`.
- sim-rc: two `kicadSimRun` back to back (the second after the first's record), the first
  stopped with `kicadSimStop` at once: the second run's record `error: ""`, its transient plot
  whole (length the step count), never ended by the first's late idle. Wrong: the second ends
  early or `stopped`.
- A run that completes: record and `result.json` exactly as run 14 (no change to their keys).

## m11-e: kicadSimUserSignals (ledger M11)

Export `kicadSimUserSignals` (`bool (std::string)`), defined `cc_e13::kicadSimUserSignals` in
`wasm/bindings/cc_e13_sch.inc`, registered in `EMSCRIPTEN_BINDINGS( cc_e13_sch )`. Not merged
image only (the eeschema TU, no guard). No host ask, no marker. Island cap: `simUserSignals`
(m11-i lists it when the name is present).

Args, closed: `{ "run": int >= 0, "dir": "/tmp/cc-sim/<n>", "plot": [A-Za-z0-9_]{1,64},
"signals": [{ "id": 0 to 15 (unique), "expr": 1 to 256 printable ASCII, no ; quote backslash
backtick or brace }] (0 to 16) }`. Refused (false, nothing queued): no schematic frame, an open in
flight, a run live, no circuit model kept (no run yet), the plot not among the plots the run's
finish recorded (`simRunPlots()[run]`), any other shape.

Queued body (`cc_e13::sch::applyUserSignals`): re-checks on the coroutine that no run began and
the plot still lives in the service (`all_plots`), else the record says `no_simulation`. Then
`setplot <plot>` and, per signal, `unlet user<id>` (a refused expression never leaves an older
vector standing under its name), `let user<id> = <expr quoted>`, where the quoting is KiCad's own
`quoteNetNames` lambda of `SIMULATOR_FRAME_UI::applyUserDefinedSignals`
(`eeschema/sim/simulator_frame_ui.cpp` 2225 to 2324, copied with the source cited) over the kept
circuit model's net names collected as `rebuildSignalsList` collects them (1031 to 1070: unescaped,
SPICE markup, GND, 0 and unconnected nets left out). A signal is an error when the plot holds no
`user<id>` vector afterwards or a console line with "error" arrived after its command: `line` is
ngspice's first such line, else "ngspice made no vector for this expression" (or "ngspice refused
the expression" when the command itself failed). Record, the only file and so written last:
`<dir>/user-signals.json` `{ run, plot, error: "" | "no_simulation" | "sim_failed", message?,
vectors: [{ id, name: "user<id>", length, complex }], errors: [{ id, line }] }`. `length` is
`GetRealVector( name ).size()`; `complex` is true when ngspice holds an imaginary part for it
(`GetImaginaryVector( name, 1 )` non-empty: KiCad's own reading of the vector info). No undo step,
nothing modified.

Departure from the addendum's text: the record carries `run`, `plot` and `error` beside `vectors`
and `errors` (so a host can tell a vanished plot from a refused expression).

### What the build train must probe (m11-e)

- sim-rc in Edit mode, `kicadSimRun` a transient (`.tran 10u 5m`), its record `error: ""`; then
  `kicadSimUserSignals('{"run":<n>,"dir":"/tmp/cc-sim/<m>","plot":"<the run's tran plot>","signals":[{"id":0,"expr":"V(out)-V(in)"}]}')`
  answers true; `user-signals.json` lists `{id: 0, name: "user0", length: <the run's length>,
  complex: false}`, `errors: []`; `sim.vectors` reads `user0`. Wrong: an error row, or a length
  that differs from `v(out)`'s.
- Same with `{"id":1,"expr":"V(nosuchnet)"}`: `errors: [{id: 1, line: <ngspice's line>}]` and no
  `user1` vector. Wrong: a vector made, or an empty line.
- A call with `"plot":"tran99"` (not the run's), with a brace or `;` in an expression, with 17
  signals, or while a run is live: false, no folder written.
- A second call with id 0 refused after a first accepted one: no `user0` remains.

## st-mc-e: kicadMultichannel, kicadMultichannelApply (ledger PE37)

Commit pcbjam `05f959c` (relay seat). No KiCad change (nobody in run 15 owns
`pcbnew/tools/multichannel_tool.cpp`), no host ask, no marker. File: `wasm/bindings/cc_e13_pcb.inc`
only. Both exports registered in `EMSCRIPTEN_BINDINGS( cc_e13_pcb )`, both MERGED IMAGE ONLY (the
whole body, includes and registrations sit under `KICAD_MERGED_EMBIND`: occ_service links no
multichannel tool). Includes added at the inc's top (merged only): `board_commit.h`, `footprint.h`,
`pcb_group.h`, `zone.h`, `string_utils.h`, `connectivity/topo_match.h`,
`geometry/convex_hull.h`, `geometry/shape_utils.h`, `tools/multichannel_tool.h` (all resolve
with the existing include list: no include path asked of E0).

KiCad's own entry points are not called where they show windows under the host:
`AutogenerateRuleAreas` (its dialog whenever `Pgm().IsGUI()`), `CheckRACompatibility` (a
`WX_PROGRESS_REPORTER` after 1 s), `RepeatLayout( event, ZONE* )` (info bar, `wxSafeYield`, its
own push). Called as they are: `GetTool<MULTICHANNEL_TOOL>()`, `FindExistingRuleAreas()`,
`GeneratePotentialRuleAreas()` (both refill the tool's `GetData()->m_areas`; the inc copies the
vector out each time) and the public `RepeatLayout( event, refArea, targetArea, options,
&externalCommit, &errorOut )` (silent with `errorOut`). Copied from `multichannel_tool.cpp` (private
or static there, behavior unchanged, traces and timers dropped): `FormatComponentList` (66 to
118), `resolveConnectionTopology` (1717 to 1808, public `TMATCH` API), `queryBoardItemsInGroup`
(435), both `buildRAOutline` (455, 475), `pruneExistingGroups` (1811), and the write half of
`AutogenerateRuleAreas` (1867 to 2043). One deliberate difference: KiCad's group loop would add a
null zone for a group source with no board items (it skips making that zone and still groups it);
the copy passes such an area.

**Read `kicadMultichannel( argsJson ) -> string`** (synchronous, nothing changed; "" for another
shape, no board, an open in flight):
- `{"op":"areas"}` answers `{"areas": [{key, name, source, sheetPath, parts, exists, zone}],
  "ruleAreas": [{zone, name, source, sourceName, parts}], "canGenerate": bool}`. `areas` are
  GeneratePotentialRuleAreas' candidates: `key` `sheet:<path>`, `class:<name>` or
  `group:<name>`; `name` the zone name AutogenerateRuleAreas gives (`auto-placement-area-<src>`);
  `source` `sheet` | `componentClass` | `group`; `sheetPath` for a sheet, else ""; `exists` and
  `zone` (uuid) when a placement rule area holds exactly the same parts (AutogenerateRuleAreas'
  rule, the last such zone wins). `ruleAreas` are FindExistingRuleAreas' areas (zone uuid, zone
  name, the zone's own source type and source text, its part count). `canGenerate` is false
  where KiCad says "Cannot auto-generate any placement areas" (one or no candidate). This is
  spec 7.6's `RuleAreaRow` plus `key` and `zone`, which the apply needs.
- `{"op":"check","ref":"<zone uuid>"}` answers `{"checks": [{zone, name, parts, matched, ok,
  message, reasons}], "error": ""}`: per other placement rule area, KiCad's verdict against the
  reference (`message` is KiCad's `m_errorMsg`, "OK" or the first reason; `reasons` KiCad's own
  mismatch lines, with the component lists when the counts differ). An unknown ref answers
  `{"checks": [], "error": "not_found"}`.

**Apply `kicadMultichannelApply( argsJson ) -> bool`** (true = queued on the apply coroutine;
run folder root `/tmp/cc-multichannel/`):
- `{run, dir, op: "generate", areas: [keys], replaceExisting: bool, groupItems?: bool}`: the
  candidates named by key get a placement rule area each (copied write half, a 0.1 mm margin
  hull rectified to 90 degree segments, all copper layers), an existing one removed first only
  with `replaceExisting`; `groupItems` groups each new zone with its parts, pruning their old
  groups (KiCad's dialog checkbox). ONE commit "Auto-generate placement rule areas"; nothing
  pushed when no area was made. Errors: `too_few` (KiCad's info bar sentence as `message`),
  `not_found` (an unknown key, `message` the key; nothing changed).
- `{run, dir, op: "repeat", ref, targets: [zone uuids], options: {copyRouting?,
  connectedRoutingOnly?, copyPlacement?, copyOtherItems?, includeLockedItems?, anchor?},
  preview: bool}` (exactly these seven keys; options' booleans default to KiCad's
  `REPEAT_LAYOUT_OPTIONS`; `anchor` a footprint uuid of the reference area or ""): per target
  the copied verdict; a target that does not match is reported (`ok: false`, KiCad's reasons)
  and skipped, as KiCad's Repeat Layout skips it; a matching one goes through RepeatLayout into
  ONE external `BOARD_COMMIT`. A preview, or a run where nothing matched, is reverted; a failed
  copy reverts the whole commit (`copy_failed`, KiCad's message), as KiCad's own path does;
  otherwise pushed "Repeat layout". Errors: `not_found` (ref, a target or the anchor; nothing
  changed). **`groupItems` is refused on repeat** (args false): the public per target
  RepeatLayout keeps the items KiCad would group inside a local it never returns, and KiCad's
  zone overload that groups shows an info bar and pushes its own commit (no preview). Reported to
  E0 and to st-mc-i, st-mc-s: the Repeat page must not offer "group the items" this run.
- Record: `<dir>/multichannel.json` = `{op, preview, areas, applied, depth, error, message}`
  (generate rows `{key, name, parts, ok, reasons: [], skipped: "" | "exists" | "empty", zone?}`,
  repeat rows `{zone, name, parts, ok, reasons}`; `depth` the board frame's undo count after the
  run), then `<dir>/.cc-multichannel.json` last `{code: 0 | 1, error}`. `no_board` and `failed`
  (an exception) are the other codes.

**Header change:** none. Recompile reach: `pcbnew_embind.cpp` only.

### What the build train must probe (st-mc-e)

Fixture: kicad `qa/data/pcbnew/issue21739/topology_mismatch.kicad_pro` (read by path; KiCad's qa
case `TopoMatchGlobalNetHierarchicalPins`: the sheet `i2c_thingy` placed twice, the board already
holding the two placement rule areas sourced `/i2c_thingy_ch0/` and `/i2c_thingy_ch1/`, which
KiCad's qa expects to MATCH with equal part counts). Open the board in the board frame.
- `kicadMultichannel('{"op":"areas"}')`: `ruleAreas` holds both zones (source `sheet`,
  sourceName the two paths, equal `parts`); `areas` holds the sheet candidates (keys
  `sheet:/i2c_thingy_ch0/`, `sheet:/i2c_thingy_ch1/`, plus the root sheet's), the two channel
  candidates `exists: true` with those zones' uuids; `canGenerate` true. Wrong: "" (the
  MULTICHANNEL_TOOL not registered in the frame) or no `exists: true`.
- `kicadMultichannel('{"op":"check","ref":"<ch0 zone>"}')`: one row, ch1, `ok: true`, `message`
  "OK", `matched` equal to `parts`, `reasons` empty. Wrong: `ok: false` (the issue 21739 bug
  back), a progress window, or a hang (the match runs on KiCad's thread pool and is joined on
  the calling thread, as DRC is).
- `kicadMultichannelApply('{"run":1,"dir":"/tmp/cc-multichannel/1","op":"repeat","ref":"<ch0>","targets":["<ch1>"],"options":{},"preview":true}')`:
  true; `.cc-multichannel.json` `{"code":0,"error":""}`; one row `ok: true`, `applied` 1,
  `depth` unchanged, board unchanged. The same with `"preview":false`: `depth` +1, ch1's parts
  moved to ch0's placement, one Undo restores them. Wrong: depth +2 (a second commit), the info
  bar "Copied to", or `copy_failed`.
- `kicadMultichannelApply` `generate` with `areas` `["sheet:/i2c_thingy_ch0/",
  "sheet:/i2c_thingy_ch1/"]`, `replaceExisting: true`: two rows `ok: true` with new zone uuids,
  the old two zones gone, `depth` +1, no window; with `replaceExisting: false`: two rows
  `skipped: "exists"`, `applied` 0, depth unchanged. An unknown key: `error` `not_found`.
- `repeat` with `options: {"groupItems": true}` answers false (refused by design).
- Bad shapes: `{"op":"areas","x":1}` answers ""; a repeat with `ref` in `targets` answers false.

## Checks (lo-e2-e and m11-e)

Syntax check, from this worktree after `git merge run15/main` (pcbjam 48e664e, kicad caf17fbe),
with lo-e2-e committed and m11-e in the tree:
`/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e13/kicad eeschema`

    PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (12612 ms)
    syntax-check: 0 failed

## Checks at the hand over (all three units)

After `git merge run15/main` in both worktrees (already up to date: pcbjam 48e664e, kicad
caf17fbe), with lo-e2-e, m11-e and st-mc-e committed. While iterating st-mc-e:
`gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e13/kicad pcbnew occ`
gave PASS pcbnew (14003 ms), PASS occ (14052 ms), 0 failed. The hand over:
`/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e13/kicad all`
(no `--kicad-file`: this lane changed no KiCad file)

    PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (13211 ms)
    PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (12220 ms)
    PASS symedit        symedit_embind.cpp subdir=eeschema  (7590 ms)
    PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (7236 ms)
    PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (1137 ms)
    PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (4933 ms)
    PASS occ            pcbnew_embind.cpp subdir=pcbnew (11680 ms)
    syntax-check: 0 failed

(Only the two warnings wx's own `log.h` always gives.)

Export check: `scripts/kicad/check-run15-exports.sh --since run15/main --kicad
/home/matthew/cc-engine-wt/e13/kicad --kicad-since run15/main`:

    check-run15-exports: 157 names registered in the merged image (150 kicad names), 12 of 110 run 15 names, 0 failures

## Reported to E0

- lo-e2-e edits the simulator's helpers in `eeschema_embind.cpp`'s anonymous namespace just
  above the leased section (simulator code only, see lo-e2-e "Lease").
- st-mc-e: both exports merged image only; no include path, CMake line or build script line
  needed. `groupItems` is refused on `repeat` (see st-mc-e): st-mc-i and st-mc-s should not
  offer "group the items" on Repeat this run.

## lo-e2-svc-e: the ngspice service, Stop on a fine step (ledger SI1)

Released by RULINGS 29a on the island's diagnosis (`docs/si1-stop-diagnosis.md` on island
`w2/base`, report `reports/units/lo-e2-i.md`). This section is the manifest's
`BUILD-NOTES-run15-sim-stop.md`. Commit pcbjam `90ebf15`, one file:
`wasm/ngspice-service/ngspice_service_main.cpp` (leased whole to the lane; it gains the dated
"Modified by Circuit Center Inc on 2026-11-19" lines in its header comment). No KiCad file, no new
export, no new service export: `command` and `circ` change behind their names, `getVecInfo`
changes how it reads, `running` is unchanged (it already answers `ngSpice_running()`, false once
ngspice's thread ended; what was wrong was that the worker could not answer at all).

### The cause, read in ngspice 46's own code (the dep tree in the build cache, read only)

- **Why the worker went silent.** `dvec_extend` (`src/frontend/dvec.c` 142) takes ngspice's
  `vecreallocMutex` and calls `TREALLOC` inside it. When the allocation fails, `trealloc` or
  `tmalloc` (`src/misc/alloc.c` 78, 123) prints "Error: malloc: can't allocate N bytes" and
  calls `controlled_exit`, which in the shared build is `shared_exit` (`src/sharedspice.c` 2113):
  on ngspice's thread it sends the finished and exit callbacks and calls `pthread_exit` with the
  mutex STILL LOCKED. `ngSpice_LockRealloc` (`sharedspice.c` 1442) is that same mutex, and the
  service's `getVecInfo` took it on every read: the engine's finish reads the run's vectors after
  the finished event, so the worker's main thread blocked in `ngSpice_LockRealloc` for good. That
  is the diagnosis's first way (worker silent, the engine's coroutine waiting on a request), and
  the second way is the same orphaned lock before any read came.
- **Why the next run could not start either.** The service never initialized ngspice again:
  the worker's `init` is a no-op on a live instance and `svcInit` guards itself. KiCad's own
  recovery (`NGSPICE::cbControlledExit` sets `m_error`, `validate()` calls `init_dll()`, which
  calls `ngSpice_Init` again and so re-initializes ngspice's mutexes) never reached ngspice. The
  next run's first `dvec_extend` would wait on the orphaned lock forever, and `bg_halt`
  (`_thread_stop`, 1 s of waiting) gives up with "Couldn't stop ngspice".
- **Why ngspice took storage the worker cannot hold.** ngspice's own guard,
  `OUTpD_memory` (`src/frontend/outitf.c` 555), compares `n * vlength2delta(0) * 8` (a transient
  preallocates `CKTtimeListSize + 100` rows per vector at the first row, `outitf.c` 1170) with
  `getAvailableMemorySize()`. On emscripten that is the generic sysconf branch
  (`src/frontend/get_avail_mem_size.c`; `ac_cv_header__proc_meminfo=no` in
  `scripts/deps/build-ngspice.sh`), and emscripten's `sysconf(_SC_AVPHYS_PAGES)` answers the heap
  MAXIMUM (`system/lib/libc/musl/src/conf/sysconf.c` 221 to 224 in the builder image's
  emscripten 6.0.6), never what is free; the product is also in 32 bit `size_t`. So the guard
  never fires and the first row asks for a vector the heap cannot give: 1 ns over 2 s is
  2,000,000,100 rows, `2,000,000,100 * 8 mod 2^32 = 3,115,098,912`, exactly the
  "can't allocate 3115098912 bytes" of the island's second probe.

### What changed

1. **The working room** (`kWorkingRoom`, 128 MiB). One block the service holds, never written
   (address space only), from `init` and again before every `circ` and `command` while
   ngspice's thread is not running (`holdRoom`, inside `readyForRequest`). ngspice's storage
   comes from the same heap, so a storage request past the heap maximum minus the room gets NULL
   from the allocator: ngspice refuses it itself, with its own lines ("Error: malloc: can't
   allocate N bytes.", "Not enough memory or heap corruption", "ngspice.dll cannot recover...")
   and its controlled exit, before the heap's last bytes are gone. The room is let go
   (`letRoomGo`) in the finished callback and the exit callback, on ngspice's thread as it ends
   (no allocator lock is held there: the failed calloc or realloc has returned), so the worker
   has room for the requests that follow. If an ended run's storage still fills the heap, the
   next hold fails quietly and is asked again at the next request; the engine destroys the
   kind's old plots before each run, so the hold at that run's `bg_run` gets it.
2. **`getVecInfo` takes ngspice's realloc lock only while ngspice's thread runs**
   (`ngSpice_running()`). With no thread nothing can move a vector (every entry point is on the
   module's main thread, so no thread starts between the test and the read), and a dead thread's
   lock is never waited on. While a thread runs, the vector is copied under the lock as before;
   with no thread, the view is over ngspice's own data (the worker wrapper copies it into a fresh
   array at once), so a read asks no second heap block the size of the vector. The values and
   every key are the same as before (`ngcomplex_t` is `{cx_real, cx_imag}`, already the
   interleaved pairs the old loop built; a static_assert holds it).
3. **ngspice initialized again after a controlled exit** (`s_resetDue`, set in the exit
   callback). Before the next `circ` or `command`, when no thread runs, `ngSpice_Init` is called
   again with the service's callbacks: KiCad's native recovery step, which re-initializes
   ngspice's mutexes (the orphaned realloc lock among them). The order matches KiCad: after the
   exit event KiCad sets `m_error`, and its next `Command` runs `validate()`, `init_dll()`, whose
   own requests (`set noaskquit`, `set nomoremode`, the empty circuit) are the first to reach
   the service, so they apply to the fresh ngspice. ngspice's plots are not destroyed by
   `ngSpice_Init` (natively the same), so the failed run's plot is still there for the engine's
   next `destroy`.

What the service does NOT do: no stand in for an ngspice event or line (every line and every
callback in the console is ngspice's own), no change to ngspice's code, no new export, no change
to the worker wrapper or the build recipe.

### How the service builds, and what was checked

The service is not in `syntax-check-run15.sh`: it builds alone with
`scripts/kicad/build-ngspice_service.sh` (emcmake against the ngspice sysroot,
`wasm/ngspice-service/CMakeLists.txt`: `-pthread`, `NGSPICE_EH_FLAGS` = `DEPS_EH_FLAGS`
(`-fwasm-exceptions -sSUPPORT_LONGJMP=wasm -sWASM_LEGACY_EXCEPTIONS=1`), `-DXSPICE`, the
sysroot's `include`, `-O2`; link `--bind -pthread -sALLOW_MEMORY_GROWTH=1
-sMAXIMUM_MEMORY=4GB ...`). Checked: a syntax only compile of the file with exactly those compile
flags in the builder image (emscripten 6.0.6), the build cache volume mounted read only, no
network, through the gate:

    PASS ngspice_service ngspice_service_main.cpp (1655 ms)

Not checkable without a build: the link (no new symbol is used: `ngSpice_Init`,
`ngSpice_running`, `ngSpice_LockRealloc`, `ngSpice_UnlockRealloc` were all linked already) and
every behavior below.

### What the next build must probe (lo-e2-svc-e)

Stack and fixture of the unit's recipe (`mq.py unit lo-e2-svc-e`): island cc25 on engine run 15,
fixture `island:tests/fixtures/sim-rc/`, Edit mode, Simulate, transient step 1 ns, stop 2 s.

- **The run that wedged run 14:** press Run. Expected: within about 1 to 3 s (one 3.1 GB
  allocation is zeroed before the second fails) the run ends with no Stop pressed: record
  `.cc-sim.json` `error: "sim_failed"`, `message` "The simulation needs more memory than the
  simulator can hold. Use fewer points, for example a larger step or a shorter span.",
  `result.json` `ended: "failed"`, `plots: []`; the console holds ngspice's own lines ("malloc:
  can't allocate 3115098912 bytes.", "ngspice.dll cannot recover and awaits to be reset or
  detached"). Pressing Stop at about 6 percent (if the run is still live) ends it the same way
  within a second or two (timestamp the press and the record). Wrong: no record within 5 s, an
  `ended: "done"` or `"stopped"`, or a worker `running` read slower than 50 ms at any time.
- **The worker keeps answering:** through the run and after it, `globalThis.ngspiceService`
  `running` answers in a few ms (false after the end); the island's wedge watch (`endIfWedged`)
  never fires (log it: it should find ngspice not running and the engine's run already ended).
- **The next run starts at once, on the SAME worker:** right after, 1 us over 5 ms: `done`,
  its transient plot whole, within about the time it took on build T3 (204 ms after its request
  in the island's measure). Read the console: one fresh ngspice banner ("** ngspice-46 shared
  library") after the failed run, from the reset. Wrong: the run hangs (the realloc lock still
  held), "Couldn't stop ngspice", or the renderer's memory stays near 3.9 GiB after this run
  (the failed run's plot should be destroyed by the engine's per kind destroy).
- **1 ns over 200 ms** (the diagnosis's other wedge): ends `failed` with the same message, or
  (if only two vectors are saved and both fit) runs and Stop ends it `stopped` within 1 s with
  its partial plot. Wrong: a silent worker.
- **An ordinary transient answers exactly as build T3's did:** sim-rc 1 us over 5 ms and
  `tests/e2e/sim.spec.ts`: every vector's values byte for byte equal to T3's (`getVecInfo` now
  hands out a view over ngspice's data after the run instead of a copy), the same keys, and Stop
  200 ms into a 20 s transient of 1 us steps ends `stopped` within about 180 ms as before.
- **Memory:** the worker's heap grows by 128 MiB of address space at the first request (the
  room, never written: the renderer's resident memory should not move by it).

## lo-e2-e follow-up: an ngspice error ends the run failed (RULINGS 29a)

Commit pcbjam `f6e13bd`, `wasm/bindings/eeschema_embind.cpp` (the simulator helpers and the
leased section, as lo-e2-e above; a new dated "Modified by Circuit Center Inc on 2026-11-19"
line). No export added, renamed or registered: `kicadSimRun` and `kicadSimStop` change behind
their names.

The cause (the island report's second finding), in the code: `finishSimRun` set `ended` to
`"done"` whenever ngspice had made a plot, and a run that hits its allocation failure at the
first row has made one (`beginPlot` runs before the first row), so the finish read the vectors of
a spent ngspice and recorded `done`.

- `SIM_LIVE_RUN` gains `ngspiceExited`, `outOfMemory`, `ngspiceError`. The reporter
  (`SIM_RUN_REPORTER::noteRunLine`, called for each console line while a run is started; it
  only stores) marks them from ngspice's own lines: "cannot recover" (printed by `shared_exit`
  for every controlled exit that is not a quit, before the thread's finished event, so the run is
  marked before its finish is queued; KiCad's own exit message comes after the finished event and
  is not relied on), "can't allocate", "not enough memory" (`misc/alloc.c`) and "more than memory
  available" (`outitf.c`); the first line holding "error" is kept.
- `finishSimRun`: a run with `ngspiceExited` reads NO vector and sets no operating point; it
  lists only the plots ngspice made (so `simKindPlots` destroys them at the kind's next run and
  their storage comes back) and records `error: "sim_failed"`, `ended: "failed"`, with the
  plain sentence above when ngspice could not get memory, else ngspice's first error line, else
  "ngspice ended the run with an error."; `simRunPlots` of that run is empty (an FFT never reads
  a failed run). An exit wins over a pressed Stop (ngspice's error ended the run).
- `kicadSimStop`: after the halt, a run whose ngspice already exited, or whose thread was seen
  running and has ended (the client's mirror `IsRunning`, never an RPC), is finished at once (its
  idle event came already, none follows the halt); the live run is checked again after the
  halt, which can let events in.

### What the build train must probe (lo-e2-e follow-up)

- The two sim-rc cases above: the records exactly as given (never `done` for a run whose console
  holds "cannot recover").
- A run whose ngspice ends with an error that is not memory (the island's `sim-missing-lib` has
  none on the run path; any netlist that makes ngspice call `controlled_exit` on its thread will
  do, for example an XSPICE source with no callback): `failed`, `message` ngspice's first error
  line.
- Stop pressed after such an exit but before the record (slow the apply queue): `failed`, one
  record. Stop on a normal run: `stopped`, as before.

## For the island

- `endIfWedged` (src/sim-service.ts) should now never fire on SI1: keep it as the guard it is,
  and log when it does (a fire on run 15 is a fault to report: the service or the engine wedged
  again). Its second test, "ngspice runs nothing while the engine is idle on two looks in a row",
  may still see that state for a moment between ngspice's exit and the engine's record; two
  looks 300 ms apart should outlast it, check that the run's record has not been written before
  ending the worker.
- The run 15 case of `tests/e2e/lo/sim-faults.spec.ts` that asks for `stopped` or `failed` on
  1 ns over 2 s can now ask for `failed` and the message, with no Stop needed (the run ends by
  itself within about 3 s), and should assert the next run is served by the SAME worker (no
  `released` or `died` notice).
- Optional and harmless: after an `exit` event whose run has its record, `release()` the worker
  once no run is live, so the next run boots a fresh one and the failed run's storage is given
  back to the browser at once. The service no longer needs it (it initializes ngspice again
  itself), so this is only for memory, not for correctness.
- The failed record's `message` is a whole sentence meant for the reader; show it as is.

## Checks at the hand over (all four units)

After `git merge run15/main` in both worktrees (pcbjam fast forward to 6e5ab71, kicad already
held c01ee866's merge), with lo-e2-svc-e and the lo-e2-e follow-up committed:
`/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e13/kicad all`
(no `--kicad-file`: this lane changed no KiCad file)

    PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (20545 ms)
    PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (21515 ms)
    PASS symedit        symedit_embind.cpp subdir=eeschema  (10186 ms)
    PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (13184 ms)
    PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (7018 ms)
    PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (7157 ms)
    PASS occ            pcbnew_embind.cpp subdir=pcbnew (15994 ms)
    syntax-check: 0 failed

The service's own syntax only compile (above): `PASS ngspice_service ngspice_service_main.cpp`.

Export check, `scripts/kicad/check-run15-exports.sh --since run15/main --kicad
/home/matthew/cc-engine-wt/e13/kicad --kicad-since run15/main`:

    check-run15-exports: 256 names registered in the merged image (249 kicad names), 111 of 111 run 15 names, 0 failures

and with `--join --kicad /home/matthew/cc-engine-wt/e13/kicad`:

    check-run15-exports: 256 names registered in the merged image (249 kicad names), 111 of 111 run 15 names, 0 failures

## Reported to E0 (2026-11-19)

- The service builds only through `scripts/kicad/build-ngspice_service.sh`, which neither check
  script covers; the next build must rebuild `ngspice_service.{js,wasm}` (the island's
  `RECIPE_ARGS` carries it, spec 5.1) or none of lo-e2-svc-e ships.
- An ngspice 46 defect on wasm32 that the service cannot reach (ngspice's own source, the dep
  script is E0's): one vector's preallocation is `(CKTtimeListSize + 100) * sizeof(double)` in a
  32 bit `size_t` (`TREALLOC` in `dvec_extend`), so a transient of more than 536,870,812 rows per
  vector asks for its size modulo 4 GiB. When the wrapped sizes fit (for example 1 ns over 600 ms
  with four vectors: 505 MB each, 2 GB together) the run starts with vectors too small for their
  `v_alloc_length` and writes past them after about 63 million rows (heap corruption, then a
  crash or wrong values). The fix belongs beside `build-ngspice.sh`'s patch 3 (the inpgmod.c
  wasm32 fix): in `outitf.c` compute `OUTpD_memory`'s estimate in 64 bit and refuse (its own
  `controlled_exit(1)` with its own message) when one vector's rows times `sizeof(double)` pass
  `SIZE_MAX`, or when the estimate passes the room; with that, ngspice's own guard covers every
  size. Not done by this lane (not its file).

## Hand over state

lo-e2-e, m11-e, st-mc-e and lo-e2-svc-e committed (lo-e2-svc-e released by RULINGS 29a), checks
above clean, `e13.unit.lo-e2-svc-e` and `e13.done` written.
