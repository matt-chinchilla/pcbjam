# Engine run 14: the simulator's share (build notes)

Spec: `circuits-com/docs/superpowers/specs/2026-11-16-simulator-design.md`, sections 6 and 14.
Written 2026-11-16 by the simulator's engine seat. Nothing here was built or run in a browser.

## Branches

| Repo | Branch | Commits |
|---|---|---|
| pcbjam | `seat/run14-simulator` (worktree `cc-engine/wt-run14-simulator-pcbjam`) | the eight exports, these notes |
| kicad-source-mirror | `seat/run14-simulator` (worktree `cc-engine/wt-run14-simulator-kicad`) | `e75ebe77` K-SIM |
| cc-editor-island | `seat/run14-simulator` (worktree `/home/matthew/wt-run14-simulator-island`, from `phase4-selection`) | `6339e03` the service in `scripts/engine/build.sh` |

The pcbjam `kicad` submodule pointer is NOT bumped here: the integrator merges the four seats'
kicad branches onto `cc/main` first, then bumps the gitlink once.

## What was checked, and how

A syntax and type check only (`em++ -fsyntax-only`, the eeschema compile line from the build
cache's `compile_commands.json`, in the builder image `kicad-wasm-cc-engine-kicad-wasm-builder`
with the build cache volume mounted READ ONLY, my two worktrees mounted for sources):

- `wasm/bindings/eeschema_embind.cpp` with and without `-DKICAD_MERGED_EMBIND`: exit 0, no
  warning in the new code under `-Wall -Wextra` (the two warnings left are older:
  `schUpdateFromLibraryShim`, `kicadCollabBusyProbe`).
- kicad `common/tool/action_manager.cpp` and `eeschema/tools/sch_inspection_tool.cpp`: exit 0.
- A deliberate error in a scratch file failed the same check (the check is real).

Not checked: the link (the two `extern "C"` client functions, below), the runtime, any
behavior. No PCH was used in the check (the real build uses eeschema's PCH only for KiCad's own
files; the embind TU is compiled by `compile_embind_tu` without one).

## The exports

All in `wasm/bindings/eeschema_embind.cpp`, one block before `ccSchNetlistForBoard`, registered
in `EMSCRIPTEN_BINDINGS(eeschema)` after `kicadCheckSheet` (outside the merged guard: the names
are eeschema-only, like `kicadSymbolGet`). Each re-resolves the schematic frame and refuses at
once (`""` or `false`) with no frame or `pcbjam_open::busy()`.

| Export | Signature | Notes |
|---|---|---|
| `kicadSimModelCatalog` | `std::string(std::string)` | `{}` lists every device with `showInMenu` and its types; `{type}` one type's pins and params. |
| `kicadSimModelGet` | `std::string(std::string)` | `{uuid, sheetPath}`, `resolveSymbol` non-strict (symbol.get's grammar). |
| `kicadSimModelSet` | `bool(std::string)` | `{uuid, sheetPath, model}`, strict path; one `SCH_COMMIT` "Edit simulation model". |
| `kicadSimFileRead` | `std::string(std::string)` | `{path}`. |
| `kicadSimNetlist` | `std::string(std::string)` | **registered with `PCBJAM_PARKER_POLICY`: it answers a Promise** (see below). |
| `kicadSimRun` | `bool(std::string)` | `{run, dir, command, options, overrides, fftOf}`, dir under `/tmp/cc-sim/`. |
| `kicadSimStop` | `bool()` | |
| `kicadSimOpOnSheet` | `bool(std::string)` | `{voltages, currents}`. |

Files touched: `wasm/bindings/eeschema_embind.cpp` only (includes, the block, eight
registration lines). No build flag or export list changed: embind registrations are the export
list for these, and `sharedspice_client.cpp` is already linked statically into the editor
(`wasm/cmake/Findngspice.cmake`).

### Contract details the island seat needs (beyond spec 8)

- **Type keys.** A model type is named by `simTypeKey`: the `Sim.Device` word, then `.` and the
  `Sim.Type` word when the type has one: `R`, `R.POT`, `R.=` (behavioral), `V.DC`, `V.PULSE`,
  `NPN.GUMMELPOON`, `NMOS.BSIM4`, `SPICE` (raw SPICE element), `XSPICE`, `SUBCKT`, `IBIS.DEVICE`.
  The catalog's `{type}` and `SimModelSet.type` take these keys; `SimModelSheet.type` answers them.
- **Catalog answers** carry a little more than 6.2: each device has `description`; a type answer
  has `name` (the Sim.Type word, may be empty) and `description`. Param `kind` is `instance`
  (`isInstanceParam`), `other` (an output of the simulator, `DIR_OUT`, never settable) or `model`.
  `symbolPinHint` is the 1-based symbol pin the default map puts each model pin on. Model pin
  names are KiCad's raw ones (an optional pin keeps its angle brackets, as `GetPinNames` gives).
- **SimModelSheet.params** leaves out `DIR_OUT` params. `given` is true when the instance
  overrides the param (`GetParamOverride(i).value` non-empty). `source` is `file` when
  `Sim.Library` is set, `builtin` when `Sim.Device` is set, `inferred` when KiCad infers a model
  from the value, else `none`.
- **IBIS.** An IBIS part's sheet adds `ibis: {pin, model, diff, type}`. `SimModelSet` of
  `source: "file"` on a `.ibs` path takes four RESERVED keys in `params`: `ibis.pin`,
  `ibis.model`, `ibis.diff` (`""` or `"1"`) and `ibis.type` (`DEVICE`, `DCDRIVER`, `RECTDRIVER`,
  `PRBSDRIVER`, default `DEVICE`), written as KiCad's `Sim.Ibis.*` fields and Sim.Device/Type
  (DIALOG_SIM_MODEL's IBIS rows). On any other file those keys are refused.
- **Set rules.** `builtin` takes only a device the dialog's Built-in tab lists (`showInMenu`),
  never SUBCKT or IBIS. `pins: []` keeps the model's default map (pin n on symbol pin n); a
  non-empty list is the WHOLE map (every model pin unconnected first, as the dialog does), each
  symbol pin and model pin at most once. A param of value `""` returns it to the default. The
  model is written with `SetIsStoredInValue(false)` (never into the Value field). The owned fields
  are Sim.Device, Sim.Type, Sim.Pins, Sim.Params, Sim.Library, Sim.Name, Sim.Ibis.Pin,
  Sim.Ibis.Model, Sim.Ibis.Diff, Sim.NodesFormat; `model: null` removes them (Sim.Enable is
  never touched). New fields are added hidden.
- **Paths.** `simModelPath`: relative to the project folder (no `..`, `.`, empty segment, leading
  `/` or `~`, backslash, `$` or `:`), or `kicad-embed://<name>` with no `/` in the name. At most
  512 characters.
- **Problems.** `code` is `model_error` (a symbol's model failed), `file_missing` (KiCad's text
  says "not found"), `file_error` (a file read failed, `kicadSimFileRead` only), or `other` (the
  annotation check, the circuit read's own lines not already said per symbol, a tuner override
  on a part with no tuner). `no_model` and `pin_map` are never produced: KiCad's reporter gives
  nothing to tell them apart. Problems from a symbol carry `ref`, `uuid`, `sheetPath`.
- **Netlist.** `command: ""` writes the sheet's own directives (KiCad's own rule). The answer
  adds `directive`: the sheet's own analysis lines (`GetSchTextSimCommand`), `""` when none, so
  the page can offer "The sheet's own". `""` (refused) while a run is live.
- **Netlist is a Promise.** An IBIS part's netlist line is made by KIBIS through the ONE shared
  NGSPICE (`SIMULATOR::CreateInstance` is a process singleton; KIBIS calls `Init`, which
  resets ngspice, and `LoadNetlist`), and every client call waits on the service (JSPI). A plain
  synchronous export would throw SuspendError there, so the export is PARKER: `await` it. For a
  design with an IBIS part the netlist also makes the simulator first (the service boots), so
  its settings are set before KIBIS's `Init` reads them.
- **Run command.** `fftOf: null`: `command` is one analysis line, starting with `.`, one of op,
  dc, ac, tran, noise, pz, sp (CommandToSimType), printable ASCII with no `;`, quote, backslash or
  backtick, at most 4096 bytes; `""` runs the sheet's own directive (its FIRST analysis line,
  KiCad's frame does the same through its first tab). `fftOf: <run>`: `command` is the FFT's
  lines (`linearize ...`, `fft ...`, `set specwindow=...`, `set specwindoworder=...`, at least
  one `fft`, at most 16 lines) and `overrides` must be empty. Override values pass spec 7.2's
  number grammar; refs at most 64 characters.
- **Run files.** `<dir>/accepted.json` `{run, plot, problems}` (plot is `CurrentPlotName()` read
  right after `bg_run` is accepted: it may still name the previous plot; the result's `plots` are
  the truth). `<dir>/result.json` is written for every run that reached the simulator (done,
  stopped or failed), never for a refusal before it. The record `.cc-sim.json` is `{run, error,
  message, lines, problems}`; `error` is one of `""`, `circuit`, `no_directive`,
  `no_simulation` (an FFT whose transient is gone), `sim_failed`, `stopped`, `service_failed`.
  `lines` are the netlist read's own `{severity, text}` (like the other run records); the
  simulator's console is `result.json`'s `console`.
- **Result.** `kind` is the analysis actually run (`op`, `dc`, `ac`, `tran`, `noise`, `pz`,
  `sp`, `fft`), never `sheet`. `plots` are every plot the run made (`const` excluded). A vector's
  `trace` is `voltage`, `current`, `power` from `VectorToSignal`, `noise` for every vector of a
  noise run, `other` for the x vector and anything `VectorToSignal` cannot name; `gain`, `phase`
  and `event` are never produced (gain and phase are the page's reading of a complex vector).
  `complex` is decided by the analysis (ac, sp, pz, fft true), NOT read per vector: the island's
  `get_vec_info` answers `real`/`comp` and is the truth. `length` is one vector's length per plot
  (ngspice keeps an analysis plot's vectors the same length); the op values are read one by one.
- **Failure detection.** A run that made no new plot and was not stopped is `sim_failed`, its
  message the first console line containing "error". This is our rule; KiCad shows the console.
- **Stop.** Queued on the apply coroutine. When `bg_halt` is refused (no run in the service,
  e.g. its worker died and the next request booted a fresh one), the run is finished at once
  with `stopped`. THE ISLAND MUST CALL `kicadSimStop` after a worker death: no idle event will
  come, and the engine refuses new runs while one is live.

## Behavior that departs from KiCad's frame, on purpose

- The annotation check is KiCad's `CheckAnnotate` with a collecting handler (never the Annotate
  dialog); an unnumbered or doubled designator refuses the run (`circuit`), as KiCad's
  ReadyToNetlist refuses it. Power symbols are not numbered by the export (KiCad's ReadyToNetlist
  numbers them; ours does not change the design).
- Before a run, the connections are rebuilt with `GLOBAL_CLEANUP` when incremental
  connectivity is on, exactly as `SIMULATOR_FRAME::LoadSimulator`. That cleanup can merge or
  split wires without an undo step, as on desktop. The netlist export does not rebuild (as
  KiCad's analysis dialog does not).
- KiCad's frame forces ngspice's LTspice and PSpice reading when the project has no workbook.
  The run does the same around the attach and then puts the project's own mode back, so the
  project file is not changed.
- KiCad clears the operating point values at every run's finish; ours clears them only when a
  new operating point finishes, so the labels keep the last operating point (spec 6.3).
- The tuner overrides are sent once per run, before `bg_run`, as KiCad's `applyTuners` sends
  them; a part with no tuner is a warning problem, never a dialog.
- Every symbol's model is made once before the run (one `SIM_LIB_MGR`, files read once) to
  attach the symbol to each failure. On a large design this costs a second model pass.

## Things I am unsure will compile or behave

1. Link: `extern "C" char** pcbjam_ngSpice_AllPlots(void)` and `pcbjam_ngSpice_AllVecs(char*)`
   are declared at global scope in the bindings TU and defined in
   `wasm/stubs/sharedspice_client.cpp` (C linkage, same signatures as its `sharedspice.h`). If
   the client is ever built without `__EMSCRIPTEN__` they are absent.
2. `engine->GetRealVector(<x vector>)` to learn a plot's length copies that whole vector through
   the client's arena into the editor heap once per plot (about 8 MB for a million-point
   transient). Freed by the next read.
3. The finish reads the plot with `setplot <plot>` then the vectors by their bare names. The
   current plot is left at the run's last plot; the island reads vectors directly from the
   provider by name, so if it relies on the current plot it must `setplot` itself.
4. A stray idle event of an earlier, halted run arriving after a new run's `bg_run` would end
   the new run early. The `started` flag covers events before `bg_run`; there is no run
   generation counter (KiCad's frame has one, findings E-7).
5. If `SIMULATOR::CreateInstance` ever throws, KiCad's code shows `DisplayError` (a KiCad
   window). In the wasm build `NGSPICE`'s constructor does not throw (no dlopen).
6. Netlist and runs while an IBIS part is present reset ngspice through KIBIS (desktop does the
   same); plots of earlier runs may then answer `no_simulation` to `sim.vectors`.
7. `wxRegEx` with `wxRE_ADVANCED | wxRE_ICASE` for the number grammar: KiCad uses wxRegEx the
   same way (`VectorToSignal`).
8. A worker death mid FFT is impossible to see (the FFT is synchronous commands); a failed FFT
   (no new plot) records `sim_failed`.

## K-SIM (kicad `e75ebe77`)

`hostHoldsAction` in `common/tool/action_manager.cpp` now takes an action name and is declared
in `include/tool/action_manager.h` under `__EMSCRIPTEN__`; the static `TOOL_ACTION` overload
calls it, so RunHotKey is unchanged. `SCH_INSPECTION_TOOL::RunSimulation` asks it with
`eeschema.EditorControl.showSimulator` before `Kiway().Player( FRAME_SIMULATOR, true )` and
returns 0 when the host takes it: the menu item, the toolbar button and the key are held in one
place. The island must answer truthy for that action (under `heldKeys` and `sim`) and send
`ev.held`. A key press asks once (RunHotKey holds it before the tool runs).

Possible merge conflict: another seat may change `hostHoldsAction` the same way (the symbol
editor spec holds windows too). The two bodies are identical in shape; keep one.

## The service

`scripts/engine/build.sh` in the island (`6339e03`): the recipe runs
`kicad_editor,ngspice_service` (docker/build.sh's comma list; `-j` and `--build-deps` are
ignored by the service's script) and `FILES` collects `ngspice_service.js` and
`ngspice_service.wasm` beside the engine's, so `meta.json` carries their sha256 and bytes.
Gate 0 (the service's build time, raw, gzip and brotli sizes, the browser probe) was NOT run:
it is a build and a browser session, owed to the integrator or the controller.
