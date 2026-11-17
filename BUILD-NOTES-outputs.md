# Build notes: outputs (engine run 14, island cc23)

The engine side of `circuits-com/docs/superpowers/specs/2026-11-16-outputs-design.md`
(sections 6 and 12). Written without a build; read by the run 14 integrator.

## Branches

- `pcbjam:seat/run14-outputs` (from `cc/main` 5f8bed4): the bindings and this file.
- `kicad-source-mirror:seat/run14-outputs` (from `cc/main` f4b38976): K-O1 to K-O4.

**The pcbjam branch needs the kicad commits.** `cc_idf` calls the 8 argument
`Export_IDF3` (K-O1) and `cc_hyperlynx` calls `ExportBoardToHyperlynx` (K-O2): merge the
kicad branch into kicad `cc/main` and bump pcbjam's `kicad` gitlink before building. This
seat did not bump the gitlink (it would point at an unmerged seat commit).

## The exports

| export | file | registered |
|---|---|---|
| `kicadExportBoardOutput(argsJson) -> bool` | `wasm/bindings/pcbnew_embind.cpp` (`pcbExportBoardOutput`, runners `ccRunBoardOutput`, `ccRunOurBoardOutput`) | `EMSCRIPTEN_BINDINGS(pcbnew)`, outside the `KICAD_MERGED_EMBIND` guard, beside `kicadPlotBoard` |
| `kicadResolveRules(argsJson) -> string` | `wasm/bindings/pcbnew_embind.cpp` (`pcbResolveRules`, `ccResolveRules`, `ccInspectConstraints`, `ccReportClearance`, class `CC_RULES_ENGINE`) | same |
| `kicadExportSchematicOutput(argsJson) -> bool` | `wasm/bindings/eeschema_embind.cpp` (`ccRunSchematicOutput`, `ccRunSymbolsOutput`) | `EMSCRIPTEN_BINDINGS(eeschema)`, outside the guard, beside `kicadCheckSheet` |
| `kicadOutputJobs() -> string` | `wasm/bindings/kicad_editor_embind.cpp` | `EMSCRIPTEN_BINDINGS(kicad_editor)` (merged image only) |
| shared header | `wasm/bindings/cc_output_common.h` (new): `cc_output::LINES_REPORTER`, `SeverityWord`, `LinesJson`, `WriteRecord`, `IsRunDir`, `EnsureDir`, `BoardJobs`, `SchematicJobs`, `ReadOurs`, `MergeSettings` (6.2), `JobsWithDefaults` | included by all three TUs; common headers only (`jobs/job.h`, `jobs/job_registry.h`, `reporter.h`, nlohmann, wx) |

`kicadPlotBoard` now uses the header (`PLOT_LINES_REPORTER` is an alias of
`cc_output::LINES_REPORTER`, `writePlotDone` writes through `cc_output::WriteRecord`, its
folder check is `cc_output::IsRunDir`, the run 12 `ccIsRunDir` delegates to it): the bytes
it writes and the args it accepts are unchanged.

No build flag, export list or JSPI list changes: embind functions need none, and none of the
new exports suspends (the board and schematic outputs queue on the apply coroutine as
`kicadCheckBoard` does; the rules export is a synchronous read as `kicadExplainViolation` is).

### Contracts as built

- `kicadExportBoardOutput`: args exactly `{run, dir, job, settings}`; `dir` under
  `/tmp/cc-out/`; `job` in `cc_output::BoardJobs()`; false with no board frame or board,
  while `pcbjam_open::busy()`, for a registry name whose face is not `FACE_PCB`, for our
  settings outside their closed keys, or when the merge refuses. The record
  `<dir>/.cc-export.json` is `{code, lines: [{severity, text}], dropped: [key]}`, written
  last through a `.part` file.
- `kicadExportSchematicOutput`: the same with `FACE_SCH` and `cc_output::SchematicJobs()`,
  plus an optional fifth key `pages` (array of 0 to 1,024 strings of 1 to 32 characters; a
  non-empty array only for the four `sch_export_plot_*` jobs, which then get `m_plotAll`
  false and `m_plotPages`, set after the merge so `plot_all` cannot undo it).
- `kicadOutputJobs`: `{"board": {job: settings}, "schematic": {job: settings}}`; a KiCad
  job is a fresh instance's `ToJson` (all its keys, `output_filename` / `output_dir` and
  `description` included); ours are their closed keys (`cc_footprint_report`
  `{units: "mm"}`, `cc_idf` `{units: "mm", include_unspecified: false, include_dnp:
  false}`, the rest `{}`). A registry name that does not resolve is left out.
- `kicadResolveRules`: args exactly `{a, b}`, lowercase uuids, `b` `""` or another uuid;
  `""` for anything else, with no board, or while an open is in flight. Answer `{rules,
  notes, error}`, `error` `""`, `"rules_invalid"` (the custom rules throw `PARSE_ERROR`,
  no records) or `"not_found"` (either uuid). At most 128 records, 32 notes.

## The settings merge (6.2), and where I read the spec

`cc_output::MergeSettings`: defaults from `ToJson`; each sent key the job lacks goes to
`dropped`; a known key whose JSON kind (bool, number, string, array, object, null) differs
refuses the call; array elements must be strings or numbers; then the fixes; then
`FromJson` in a `try` (a throw refuses too, so nothing is queued). The fixes are applied to
the JSON before `FromJson`, only where the job has the key:

- every job: `output_filename`, `output_dir`, `description` reset to the defaults;
  `color_theme` `_builtin_default`; `check_zones` false; `drawing_sheet` `""`; `variant`,
  `variant_name` `""`; `variant_names` `[]`; `use_global_settings` false.
- `CC_OUTPUT_FIXES` (one block, easy to trim): section 4's **Fixed** column taken as the
  spec's header says ("what the engine forces whatever is sent"):
  `pcb_export_3d` (format must be `vrml`, else code -1 and the line "Only VRML is made
  here."; `overwrite` true, `vrml_model_dir` `""`, `vrml_relative_paths` false,
  `subst_models` false), `pcb_export_odb` (`compression` `zip`, a warning line "ODB++ is
  made as a zip here." when it was anything else), `pcb_export_drill` (`report_filename`
  default, `format` excellon, `units` mm, `zero_format` decimal, `drill_origin` abs,
  `excellon.combine_pth_npth` false, `generate_map` true), `pcb_export_ipc2581`
  (`precision` 6, `field_bom_map.dist_pn` and `.dist` `""`, `bom_rev` `""`),
  `pcb_export_pdf` (`scale` 1), `pcb_export_svg` (`precision` default), `pcb_export_ps`
  (`track_width_correction` 0, `x_scale_factor` and `y_scale_factor` 1), `sch_export_bom`
  (the four delimiters, `keep_tabs`, `keep_line_breaks` default), `sch_export_netlist` (the
  four `spice.*` default), every `sch_export_plot_*` (`format` the job's own) and
  `sch_export_plot_pdf` (`pdf_hierarchical_links`, `pdf_property_popups`, `pdf_metadata`
  true). **Decision for the controller:** forcing the drill job's format also applies to a
  desktop jobset's drill job (a Gerber format drill becomes Excellon). If that is not
  wanted, drop those lines from `CC_OUTPUT_FIXES`; nothing else depends on them.
- **Output names.** 6.1 says to set the configured path with `SetDefaultOutputPath` for
  pos, ipc2581, ipcd356, odb and stats. I left the configured path empty for every job and
  set only `SetTempOutputDirectory( dir )`, so each handler names the files itself, as
  `kicad-cli jobset run` does for a job whose `output_filename` is empty. Reason, read in
  the tree: `JOB_EXPORT_PCB_ODB::SetDefaultOutputPath` names `<board>.zip`, while the
  handler names `<board>-odb.zip` (section 4 says `-odb.zip`); for pos, ipcd356 and stats the
  two agree, and for ipc2581 the handler picks `.zip` when `compress` is on. **Seat P1's
  reference jobset must write `output_filename` (or `output_dir`) as `""`** for the
  references to carry the same names.

## Our runners (6.3), each the body beneath KiCad's dialog

- `cc_footprint_report`: `PLACE_FILE_EXPORTER( board, mm, false, false, false, false,
  true, true, false, true, false )`, `SetVariant( board->GetCurrentVariant() )`,
  `GenReportData()`, written to `<stem>.rpt`.
- `cc_idf`: `PCB_EDIT_FRAME::Export_IDF3( board, <stem>.emn, units == thou, 0, 0,
  include_unspecified, include_dnp, &reporter )` (K-O1).
- `cc_specctra_dsn`: `DSN::ExportBoardToSpecctraFile` (declared in the TU, since
  `specctra.h` pulls generated lexer headers from outside the bindings' include roots); an
  `IO_ERROR` is reported as its own text (KiCad's desktop box prefixes "Unable to export,
  please fix and try again"); the modified flag put back as KiCad's wrapper does; a half
  written `.dsn` is removed.
- `cc_hyperlynx`: `ExportBoardToHyperlynx( board, <stem>.hyp )` (K-O2); false is KiCad's
  own "Failed to create file '%s'." line (the exporter reports nothing itself).
- `cc_footprint_associations`: `RecreateCmpFile( board, <stem>.cmp )` (declared in the TU;
  the function has no header).
- `cc_footprints`: `PCB_IO_KICAD_SEXPR::CreateLibrary( <dir>/<stem>.pretty )` first (a save
  into a missing library asks in a `wxMessageBox` in a GUI build,
  `pcb_io_kicad_sexpr.cpp:3395`), then each footprint with a library item name duplicated,
  `REF**`, no group, zones moved by minus the position, `FootprintSave`; a repeated name
  keeps the first and one warning line lists the rest. No library table is touched.
- `cc_symbols`: KiCad's `ExportSymbolsToLibrary` at its own defaults (power symbols left out,
  nothing relinked): the placed symbols by library id, flattened, `SCH_IO_KICAD_SEXPR`
  `CreateLibrary` then `SaveSymbol` into `<root stem>.kicad_sym`. Two library ids with one
  symbol name: the later replaces the earlier, as KiCad's own cache does. (The spec says
  "the schematic's own symbol cache"; KiCad's body reads the placed symbols, which is what
  is built.)

## The boxes found on the job paths (the reading 6.3 asked for)

| path | box | handled |
|---|---|---|
| `export_idf.cpp` `Export_IDF3` | three `wxMessageBox` | K-O1 (kicad 42b16c34) |
| `export_hyperlynx.cpp` | exporter class private to the file, its caller opens a save dialog | K-O2 (kicad 03537007) |
| `exporter_vrml.cpp` | **not a box: every WebAssembly build compiled stubs** ("VRML export is not available in this build"), so O17 could never make a file | K-O3 (kicad 9a513448): with `KICAD_BUILD_3D_VIEWER_WASM` (our build passes it ON), `pcbnew_kiface_objects` defines `KICAD_WASM_VRML_EXPORT` and the real exporter compiles |
| `netlist_exporter_allegro.cpp` `toAllegroPackages` | `DisplayError` when a device file cannot be written | K-O4 (kicad ddc90fe3): reported through `WriteNetlist`'s reporter |
| `pcb_io_kicad_sexpr.cpp:3395` `FootprintSave` into a missing library | `wxMessageBox` | avoided in our code (`CreateLibrary` first) |
| `specctra_export.cpp` frame wrapper, `export_footprint_associations.cpp`, `dialog_gen_footprint_position.cpp`, `footprint_libraries_utils.cpp`, `sch_editor_control.cpp` | dialogs and boxes around the exporters | not reached: our runners call the exporters beneath them |
| `dialog_export_odbpp.cpp` `GenerateODBPPFiles` | `KIDIALOG` overwrite prompts | only with a parent frame; the job passes none |
| both jobs handlers | every dialog is in a `Register` configuration lambda | not reached through `ProcessJob` |

Not boxes, noted: `GenerateODBPPFiles` runs its save on KiCad's thread pool and waits with
`wait_for` (the spec said no exporter in scope does); the zone filler and DRC do the same in
this engine, so it is expected to work, but the ODB row is the first output to measure.
`wxLogWarning` and friends (Specctra's malformed outline) are swallowed by the `wxLogNull`
each run holds. A Debug build's `wxASSERT` / `wxCHECK` failures can still raise wx's assert
dialog on any road, as before.

## What I checked, and how

- **Syntax-only compiles** (`em++ -fsyntax-only`, emscripten from the
  `kicad-wasm-cc-engine-kicad-wasm-builder` image, the `kicad-build-cache` volume mounted
  read-only for `config.h`, the generated lexers and wx's setup; my two worktrees mounted
  read-only; nothing written to the volume):
  - the three binding TUs with the build's own `compile_embind_tu` include set, `-DDEBUG`,
    `-DKICAD_MERGED_EMBIND` for pcbnew and eeschema: **0 errors each** (pcbnew_embind.cpp
    took 11.8 s, so it is a real parse; a planted error in a probe TU was caught);
  - `export_idf.cpp`, `export_hyperlynx.cpp`, `exporter_vrml.cpp` with
    `-DKICAD_WASM_VRML_EXPORT`, `dialog_export_idf.cpp` (the old 7 argument caller), with
    `pcbnew_kiface_objects`' own `flags.make` (PCH flags dropped), and
    `netlist_exporter_allegro.cpp` with `eeschema_kiface_objects`' flags: **0 errors each**
    (a planted 9 argument `Export_IDF3` call was caught).
- **Not checked:** linking, and anything at run time. The probe of 6.5 (every row on Glasgow
  and the business card, the references, undo depth, modified flag, heap after ten IPC-2581
  runs) needs run 14 built.

## Unsure, in order of risk

1. **K-O3 links the real VRML exporter for the first time in this image.** It needs
   `kicad_3dsg` (IFSG_*, `S3D::WriteVRML`), the 3D cache (`PROJECT_PCB::Get3DCacheManager`)
   and `vrml_layer` (idf3); all three are built and on the editor's link line (3d-viewer is
   linked and links kicad_3dsg; idf3 is in `PCBNEW_KIFACE_LIBRARIES`), but nothing in this
   image referenced them from pcbnew before. If the link fails, revert kicad 9a513448 alone:
   VRML then answers `export_failed` with "Error exporting VRML" and every other row stands.
   The new target definition recompiles all of `pcbnew_kiface_objects` (its PCH too), and
   K-O1 touches `pcb_edit_frame.h`, which does the same: budget the run for a full pcbnew
   recompile.
2. **Job registry entries.** `REGISTER_JOB` statics live in kicommon's job sources; each
   handler references its job class, which should pull them in. `kicadOutputJobs()` leaves
   out any name that does not resolve, so the island's e2e equality check names it.
3. **Name collisions with the other run 14 seats** in `pcbnew_embind.cpp`: mine are
   `CC_BOARD_OUTPUT`, `ccOutputFile`, `ccRunOurBoardOutput`, `ccRunBoardOutput`,
   `CC_RULES_ENGINE`, `ccConstraintOrigin`, `CC_RULES`, `ccZoneConnectionWord`,
   `ccInspectConstraints`, `ccReportClearance`, `ccResolveRules`, `pcbExportBoardOutput`,
   `pcbResolveRules` (all but the last two in the anonymous namespace); in
   `eeschema_embind.cpp` `CC_SCH_OUTPUT`, `ccRunSymbolsOutput`, `ccRunSchematicOutput`,
   `kicadExportSchematicOutput`.
4. **`CC_RULES_ENGINE` reads `DRC_ENGINE::m_constraintMap`** (protected) to list each
   assertion with its pass or fail; the element type is a private nested struct, reached
   only through `auto` (legal, and it parsed). It mirrors `ProcessAssertions` exactly,
   including that a rule whose layer does not match is still tested.
5. **RuleRecord units.** `solder_paste_rel_margin` is KiCad's ratio in thousandths
   (`m_SolderPasteMarginRatio * 1000`), answered as percent (value / 10). `track_angle`
   never appears in these two reports. `min_resolved_spokes` is a count. A pad and zone of
   one net with connection `none` answers one `clearance` record: the zone's own clearance
   (source `local`, name `zone`) or a larger physical or physical hole clearance in its
   place, as KiCad resolves it; any other connection answers `clearance` min 0 `sameNet`.
   Two items on one net (net code above 0) answer `clearance` min 0 `sameNet` per shared
   copper layer, active layer first. A layer with no clearance rule answers min 0 from
   source `board`. KiCad's report-only lines (no `diff_pair_uncoupled` rules, a tented
   item, an empty group, malformed courtyards) are `notes`.
6. A schematic plot walks every sheet (SCH_PLOTTER sets and restores the current sheet);
   the run asks the canvas to redraw after it, and puts back any sheet's modified flag that
   flipped.
