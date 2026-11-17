# Engine run 14: the integration (BUILD NOTES)

The four seats' branches merged into `cc/main`, built three times, every export probed in the GPU live
window on a scratch copy of the island's cc22 dist with the run 14 engine in place (island code
unchanged; the probe ran on fresh ports so no cached engine was served).

## Merges

- kicad `cc/main` 1b214bab: seat/run14-symbol (K1 to K3), seat/run14-simulator (K-SIM), seat/run14-outputs (K-O1 to K-O4). No conflicts.
- pcbjam `cc/main`: the four seat branches (conflicts were both sides adding at the same places in `eeschema_embind.cpp`, `kicad_editor_embind.cpp` and `pcbnew_embind.cpp`; every side kept), then the kicad gitlink at 1b214bab (e74bb81).
- island `phase4-selection`: seat/run14-simulator fast-forwarded (6339e03, `scripts/engine/build.sh` builds `ngspice_service`).

## Repairs made by the integrator

| commit | what |
|---|---|
| efe58fb | `cc_rules_pcb.inc` caught `PARSE_ERROR` by const reference; `ParseProblem()` is not const. |
| bf3d793 | `compile_embind_tu` lacked `thirdparty/pegtl`; `sim/sim_value.h` includes `<pegtl.hpp>`. The first build stopped there. |
| 980faba | The settings merge refused every `pcb_export_3d` call: a fresh job's `format` is null, so a string failed the kind check and nothing passed the VRML-only fix. A null default now takes a string. |
| 1f335b2 | `job_export_sch_plot.cpp` was never linked (nothing referenced its object), so the registry held no `sch_export_plot_*`: all four schematic plots refused and `kicadOutputJobs` left them out. A used array of their constructors pulls the object in. |
| 39e7c0f | The BOM forced a fresh job's EMPTY delimiters, so every field of a row ran together. Now kicad-cli's: `,` `"` `,` `-`. |
| 44e3e9f | VRML: KiCad's exporter answers success and writes only the 16 byte header (`#VRML V2.0 utf8`), on Glasgow and on first-board, with or without components. Not fixed. A header-only file is now a failed output (code -1, the line "The VRML exporter wrote no board (an empty scene).") and is removed. Its line reads severity `info` in the record, as every `wxS(...)` line of `LINES_REPORTER` does (the code is the truth). |

## What was measured (final build, pcbjam 44e3e9f)

- All 41 run 14 exports present on `Module` (138 `kicad*` functions). Print `head128k-sha256=d5ae5953e1cc89a732f2f65f streamed=16917389B`.
- Symbol editor: gate 0 plan A holds. A `symedit` frame boots with no progress box and no `ev.menu` (heap 256 MiB at ready and at 8 s, against 531 MiB for an empty `sch` frame). P then a canvas click asked the host `{"ask":"pin","isNew":true,...}` (a JSON STRING), the pin was placed (depth +1), Ctrl+Z took it back. Take, open, take is byte for byte equal. Each set is one undo step. STM32F405RGTx: open to state 11 ms, take 1.2 ms, pin set 34 ms (medians of 5). Fifty open and take cycles: heap flat at 369 MiB. A derived symbol (LM358 extends LM2904) lists no pins, as in KiCad. `kicadSymbolLibConvert` read KiCad, Legacy, EAGLE and EasyEDA Std; CADSTAR refused with KiCad's own "Cannot find the .csa file"; Altium `.SchLib` untested (no sample here).
- Simulator: the service boots under the island's headers (no header refused) and matches section 12.3 (RC 3.160602 V at 1 ms, 4.966310 V at 5 ms, 63.2 percent at 0.9999989 ms; divider 3.836735 V and -0.816327 mA). On KiCad's rectifier demo every export answered; netlist element lines equal kicad-cli's; Stop 200 ms into a long transient recorded `stopped` within 256 ms; a model set is one undo step. K-SIM: Inspect > Simulator reached `kicadHostAction` with `eeschema.EditorControl.showSimulator` and no window opened.
- Outputs on Glasgow: every board row writes files but the two above (VRML, Specctra DSN which KiCad itself refuses: duplicate `REF**`); the four schematic plots, netlist, BOM and symbols write. Undo depth and modified flag unchanged. Heap flat over ten IPC-2581 runs.
- Rules: every section read, every board edit op previewed, symbols table 263 rows in 18 ms. Removing copper layers clears the undo list (depth 1 to 0). Deleting a variant a symbol held is one undo step.
