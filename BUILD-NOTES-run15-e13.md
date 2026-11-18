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

Not done here (conditional, no diagnosis): SI1 Stop on a fine step (lo-e2-svc-e, held). SE38 needs
no engine change beyond the above.

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

## st-mc-e: kicadMultichannel, kicadMultichannelApply (ledger PE37): NOT STARTED, plan for the relay

Read so far (kicad `pcbnew/tools/multichannel_tool.{h,cpp}` on `run15/main`); nobody in run 15
owns `multichannel_tool.cpp`, so the unit is pcbjam only, in `cc_e13_pcb.inc`:

- KiCad's own entry points show windows under the host: `AutogenerateRuleAreas` opens
  `DIALOG_MULTICHANNEL_GENERATE_RULE_AREAS` whenever `Pgm().IsGUI()` (always true here);
  `CheckRACompatibility` runs on the thread pool and opens a `WX_PROGRESS_REPORTER` after 1 s;
  `RepeatLayout( event, ZONE* )` shows info bars. Never call those three.
- Safe public pieces: `GetTool<MULTICHANNEL_TOOL>()`, `GeneratePotentialRuleAreas()` and
  `FindExistingRuleAreas()` (both fill `GetData()->m_areas`, each clears it first: copy out the
  existing list before generating), and `RepeatLayout( event, refArea, targetArea, options,
  &externalCommit, &errorOut )`: silent with `aErrorOut`, resolves the topology itself, no dialog.
- Plan: read `{op:"areas"}` = candidates (key `sheet:<path>`, `class:<name>`, `group:<name>`;
  name as KiCad would name the zone `auto-placement-area-<source>`; exists when an existing
  placement rule area holds the same component set, AutogenerateRuleAreas' own rule 1867 to 1911)
  plus existing rule areas matching no candidate (with the zone uuid). `{op:"check", ref: zone
  uuid}` = per other existing area the topology verdict through a COPY of
  `resolveConnectionTopology` (1717 to 1808, public `TMATCH::CONNECTION_GRAPH::
  BuildFromFootprintSet` and `FindIsomorphism`, `FormatComponentList` 66 to 118 copied), cited.
- Apply `generate`: a copy of AutogenerateRuleAreas' write half (1915 to 2043, `buildRAOutline`
  455 to 498 with `BuildConvexHull`, `KIGEOM::CollectBoxCorners`, `RectifyPolygon`), ONE
  `BOARD_COMMIT` pushed "Auto-generate placement rule areas". Apply `repeat`: FindExistingRuleAreas,
  ref and targets by zone uuid, one external `BOARD_COMMIT`, `RepeatLayout` per target with
  `aErrorOut`; preview = `Revert()`, else `Push( "Repeat layout" )`; `groupItems` needs a copy of
  `pruneExistingGroups` (1811 to 1842) and the grouping loop (959 to 978).
- Record under `/tmp/cc-multichannel/<n>`: `multichannel.json` (spec 7.6 `MultichannelReport`)
  then `.cc-multichannel.json` last (`ccIsRunDir`, `ccWriteJson`).
- Fixture: kicad `qa/data/pcbnew/issue21739/` (read by path).

## Checks (lo-e2-e and m11-e)

Syntax check, from this worktree after `git merge run15/main` (pcbjam 48e664e, kicad caf17fbe),
with lo-e2-e committed and m11-e in the tree:
`/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e13/kicad eeschema`

    PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (12612 ms)
    syntax-check: 0 failed

(Only the two warnings wx's own `log.h` always gives.) The `all` run of the hand over is owed:
these two units touch only the eeschema TU, which no other unit of `all` compiles.

Export check: `scripts/kicad/check-run15-exports.sh --since run15/main --kicad
/home/matthew/cc-engine-wt/e13/kicad --kicad-since run15/main`:

    check-run15-exports: 155 names registered in the merged image (148 kicad names), 10 of 110 run 15 names, 0 failures

## Hand over state

lo-e2-e and m11-e committed. st-mc-e not started (plan above). lo-e2-svc-e HELD: no diagnosis by
lo-e2-i exists under `/home/matthew/cc-wave2/reports/` or `plan/`. No `e13.done`: the relay seat
builds st-mc-e, then runs `all` and the export check and writes it.
