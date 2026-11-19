<!-- Copyright (C) 2026 Circuit Center Inc -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Engine run 15, lane e11: 3D and outputs

Worktrees: pcbjam `/home/matthew/cc-engine-wt/e11/pcbjam`, kicad `/home/matthew/cc-engine-wt/e11/kicad`, both on `seat/run15-e11`. Units in order: 3d-e1, 3d-e4 (build G gate code), 3d-e2, 3d-e5, lo-e3o-e, lo-e3r-e. The per unit note files the manifest names (`BUILD-NOTES-run15-3d-e1.md`, `-3d-e4.md`, `-3d-e2.md`, `-3d-e5.md`, `-outputs-faults.md`, `-pe23.md`) are the sections of this one file (the guide, section 2).

## 3d-e1: the OpenCASCADE service built in our tree (gate code, build G)

- Exports: none new. `occExport( board, paramsJson, models )` and `occLoadModel( bytes, ext )` are PCBJam's own, in `occ_service.wasm` (a separate module, never in kicad_editor), unchanged.
- Files: none edited. PCBJam's recipe stands as it is in our tree, read end to end: `scripts/kicad/build-occ_service.sh` calls `build-kicad-target.sh occ_service`, which configures `-DKICAD_OCC_SERVICE_WASM=ON` (kicad `CMakeLists.txt` 80 and 1346 to 1351 add `wasm/occ-service/` after `pcbnew/`), builds its deps with `--with-occ` (every app does), links pcbnew's bindings object as compiled without `KICAD_MERGED_EMBIND` (`EMBIND_APP=pcbnew`, the syntax check's `occ` unit), and leaves the shadows `exporter_step_stub.cpp` and `oce_plugin_stub.cpp` out of that tree (kicad `pcbnew/CMakeLists.txt` 932 to 939) so it compiles the real exporters (`PCBNEW_OCC_EXPORTER_SRCS`, exported at 297 to 302). `-DKICAD_BUILD_3D_VIEWER_WASM` is passed to every app, so `3d-viewer` is in `PCBNEW_KIFACE_LIBRARIES` (1057 to 1059) and the render job of 3d-e4 links in the service too. The PCBJam files are edited only where the first build refuses them; none is edited before that.
- The island's `scripts/engine/build.sh` edit (`occ_service` in `RECIPE_ARGS`, `occ_service.js` and `occ_service.wasm` in `FILES`) is E0-OCC's, not this unit's.
- Gate 0 step 1 (owed on build G, this unit's): raw, `gzip -9` and brotli bytes of `occ_service.wasm` and `occ_service.js`, build time and peak memory. Stop: over 20 MB gzipped the owner hears the number before shipping. Filled in below once build G's files exist.
- What the build train must probe: (1) build G's log shows the `occ_service` target linked (no undefined symbol; a missing `JOB_PCB_RENDER` or `RENDER_3D_RAYTRACE_RAM` symbol there means 3d-e4's render reached the service without its library); (2) `ls dist/<cc25 id>/` holds `occ_service.js` and `occ_service.wasm`, PIN.json's sha256 and bytes match `sha256sum` and `stat -c %s`; (3) `gzip -9 -c occ_service.wasm | wc -c` is within 1 percent of the number recorded here; (4) the 3d-i1 recipe boots the service in the island frame. Wrong if: the service does not link (then 3d-e1 and 3d-i1 leave run 15 and O25, O26 show the held line), or it is over 20 MB gzipped (the owner hears it).

### Gate 0 step 1 measurements

Not yet: build G has not run.

## 3d-e4: KiCad's raytraced render compiled in (gate code, build G)

- kicad `pcbnew/pcbnew_jobs_handler.cpp` (commit 2ea86e5d), every change under `__EMSCRIPTEN__`, KiCad's path word for word in the `#ifndef` halves (the diff only adds lines):
  - the raytracer and track ball includes in an `#else` (plus `emscripten.h`, `emscripten/threading.h`, `<algorithm>`, `<functional>`);
  - `Register( "render", ... )` in an `#else`: the handler calls `ccJobExportRender` with the handler's own `m_reporter` and two lambdas over its private `getBoard` and `resolveJobOutputPath` (a lambda in the constructor has the class's access), and the dialog answer is `false` (DIALOG_RENDER_JOB never opens);
  - `ccJobExportRender`, a file static copy of `JobExportRender` (the header `pcbnew_jobs_handler.h` declares the member only outside `__EMSCRIPTEN__` and is no lane's file, so it is not edited: no wide recompile, nothing reported), the same body with `m_reporter` as `aReporter`, and in the pass loop: the raytracer's status reporter is `CC_RENDER_PASSES`, which keeps the percent of KiCad's own "Rendering: %.0f %%" line and reports nothing itself (KiCad's "Loading..." and "Rendering time" lines no longer reach the job's lines); after each pass `passes.ReportPercent( *aReporter )` writes one info line `Rendering <n> percent` when the percent moved, then `ccRenderYield()` turns the browser's event loop once: `wxWasmBeginWait( "render" )`, an `EM_JS` `cc_render_yield_start` that resolves the wait from a `setTimeout( ..., 0 )` through `globalThis.__wxScheduler.resolveWait`, `wxWasmYieldUntil` (the same scheduler wait the 3D service and model fetch bridges use; main thread only; a refused token goes on unyielded). The warnings reporter stays the job's reporter (a model it could not load is a warning line, as on desktop).
  - The job's registry name is `pcb_render` (`REGISTER_JOB` in `common/jobs/job_pcb_render.cpp` 144); the handler's own name is `render`. `JOB_PCB_RENDER` now links because the handler names it (the registrar rides the same object).
- pcbjam `wasm/bindings/cc_output_render.h` (commit 5e2e209): `ccFixPcbRender` takes `format` `png` or `jpeg` (KiCad's own enum names; `jpg` refused), `width` and `height` whole numbers 16 to 4,096, `preset` (KiCad's key; the value of Follow PCB Editor is `follow_pcb_editor`, `FOLLOW_PCB` in `eda_3d_viewer_settings.h` 32) empty, `follow_pcb_editor`, or the words `Follow PCB Editor` (normalized to `follow_pcb_editor`), `variant` empty. Refusal lines: "The picture is made as PNG or JPEG.", "The picture's width and height are 16 to 4,096 pixels.", "The render follows the board editor's look or none." A refusal makes nothing: the record's code -1 with that line (`ccRunBoardOutput`). `ccPcbRenderListed()` is now true, so `kicadOutputJobs()` lists `pcb_render` with its keys.
- Exports: none new (`kicadExportBoardOutput`, `kicadOutputJobs` widened). Merged image only: nothing (the job handler is in every pcbnew tree, the occ service included).
- Risk for gate 0 step 4 (200 ms): KiCad's own tracing pass runs blocks for up to 400 ms (750 ms over 40,000 blocks), `RENDER_3D_RAYTRACE_BASE::renderTracing`, `timeLimit`, and the main thread waits on the pool's futures for that pass. The yield sits between passes as the spec says; it does not shorten a pass. If build G measures a block over 200 ms, the lever is that time limit in `3d-viewer/3d_rendering/raytracing/render_3d_raytrace_base.cpp`, which is no file of this lane: the controller decides.
- What the build train must probe (frame pcb, island harness, `tests/fixtures/glasgow`): (1) `JSON.parse(Module.kicadOutputJobs()).board.pcb_render` exists with the keys `side`, `zoom`, `perspective`, `quality`, `bg_style`, `width`, `height`, `pan_x`, `pivot_x`, `rotation_x`, `preset`, `format`; wrong if absent (the job did not link or `ccPcbRenderListed` is false). (2) `project.export` `pcb_render` `{format: 'png', width: 1600, height: 1200, quality: 'basic', side: 'top'}`: record code 0, lines hold `Rendering <n> percent` climbing to 100 and "Successfully created 3D render image"; the file `<board>-<side number>.png` in the run folder, IHDR (bytes 16 to 23) 1568 by 1176 (KiCad's own size for 1600 by 1200, corrected by repair e11-2 below); no KiCad window; undo depth unchanged (a render is no edit; the board stays unmodified). Wrong if: no percent lines (the status reporter or the format string differs), the run never ends (the yield's wait never resolved: `__wxScheduler` missing), a dialog appears (the lambda), or code 2 "Failed to create output directory". (3) A longtask observer in the parent page during (2): no task over 200 ms (the risk above). (4) `width: 5000` answers code -1 with "The picture's width and height are 16 to 4,096 pixels." and no file; `format: 'jpeg'` answers a file starting FF D8 (wx's JPEG handler, `wxUSE_LIBJPEG` 1 in the wasm setup). (5) `quality: 'high'` on the same board ends with a file (shadows, reflections, procedural textures on).

- Repair e11-1 (kicad a2fabc61): in the WebAssembly copy the size is bounded again where KiCad's code checks it (each side 1 to 4,096, `CC_RENDER_MAX_SIDE`; the host's settings already refuse outside 16 to 4,096 in `ccFixPcbRender`), the picture buffers are sized as `size_t`, a failed allocation frees what was taken and fails the job with KiCad's "Error creating 3D render image" (never a write through a null pointer), and a failed `SaveFile` fails the job too. Probe: `project.export` `pcb_render` at width 100000, height 100000 answers the refusal record (code -1, "The picture's width and height are 16 to 4,096 pixels.", no file) and the engine answers the next call (`Module.kicadOutputJobs()` still parses).

### Checks (3d-e1, 3d-e4)

Command, from `/home/matthew/cc-engine-wt/e11/pcbjam`: `/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e11/kicad pcbnew occ kicad_editor --kicad-file pcbnew/pcbnew_jobs_handler.cpp`

```
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (6688 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (17486 ms)
PASS kicad pcbnew/pcbnew_jobs_handler.cpp (13540 ms)
syntax-check: 0 failed
```

(the pcbnew unit's PASS line scrolled off this run; the handover runs `all` and pastes every line.)

Export check, after `git merge run15/main` in both worktrees: `scripts/kicad/check-run15-exports.sh --since run15/main --kicad /home/matthew/cc-engine-wt/e11/kicad --kicad-since run15/main` printed `146 names registered in the merged image (139 kicad names), 1 of 110 run 15 names, 0 failures`.

## 3d-e2: KiCad's 3D export in every format (P3D-1, P3D-2, K-3D3; PE47, PE46's VRML half)

The relay seat after the machine fault found both worktrees clean with nothing ahead of `run15/main`: no cut off work to stash.

- Exports: none new. `kicadExportBoardOutput` and `kicadOutputJobs` widened. Merged image only: nothing (the settings merge and the run are in every pcbnew tree; the stub is compiled only where the 3D service is not, kicad `pcbnew/CMakeLists.txt` 932 to 939).
- kicad `common/jobs/job_export_pcb_3d.cpp` (commit e37af5bf), K-3D3: under `__EMSCRIPTEN__` the constructor binds the job keys `net_filter` and `component_filter` (strings, default empty) to `m_3dparams.m_NetFilter` and `m_3dparams.m_ComponentFilter` (declared in `job_export_pcb_3d.h` 77 and 78, so no header edit and no wide recompile). The stub rebuilds the job JSON from `m_3dparams`, so the filters now reach the service, whose `EXPORTER_STEP` reads them (`exporter_step.cpp` 211, 552, 1192).
- pcbjam (commit 271a792):
  - `wasm/stubs/exporter_step_stub.cpp`, P3D-1: the request to `globalThis.occService.request` is `{ kind: 'export', board, jobJson, fileName, outPath }`, `outPath` the absolute `m_outputFile`; a file already at `outPath` is removed before the request; after an `ok` answer an absent or empty file is the error line "The 3D service made no file." and the export fails. The job's warnings reach the reporter as before.
  - `wasm/bindings/cc_output_3d.h`, P3D-2: `ccFixPcbExport3d( merged, defaults, sent, out )` (now `bool`; the declaration in `cc_output_common.h` and its one call follow). `format` one of `step stpz brep xao glb ply stl u3d pdf vrml` (KiCad's own names), else the refusal "The 3D file is made as STEP, STPZ, BREP, XAO, GLB, PLY, STL, U3D, PDF or VRML."; the island's `cc_occ` (not a job key: read from what was sent, taken off the dropped list) must be a boolean or the whole call is refused (false, nothing queued); `cc_occ: false` keeps "Only VRML is made here." for every format but vrml. Forced: `overwrite` true, `vrml_model_dir` "", `vrml_relative_paths` false, `variant` "". `subst_models` is now as sent (KiCad's default true; run 14 forced false). `MERGE_RESULT::expectFile` names the extension the run must leave (`wrl` for vrml, else the format's name).
  - `wasm/bindings/pcbnew_embind.cpp`: the check after a 3D run in `ccRunBoardOutput` (the function `pcbExportBoardOutput` queues: the manifest's "the header-only check becomes the per format output check" is that hunk, the only place it lives; reported to E0 as the lease's edge) now reads `expectFile`: the format's file must exist and hold more than 0 bytes (VRML more than 64), else it is removed and the run fails with "The 3D service made no file." (VRML: "The VRML exporter wrote no board (an empty scene)."). Before, every format but VRML failed there.
- PE46, the VRML header only file: DONE in repair e11-4 (kicad f24fb9ed, the shim leased to this lane by RULINGS 21; see that section). The diagnosis here held: `S3D::WriteVRML` wrote the 16 byte header and nothing else because every `VRML_LAYER::Tesselate` (`utils/idftools/vrml_layer.cpp`) collects only between its `GLU_TESS_BEGIN_DATA` and `GLU_TESS_END_DATA` callbacks, and PCBJam's earcut shim `libs/kimath/glu_tess/glu_tess_impl.cpp` never called them and ignored `GLU_TESS_BOUNDARY_ONLY`. `exporter_vrml.cpp` is unchanged; the VRML guard in `ccRunBoardOutput` stays (it now guards a real fault only).
- Checks (before the merge of `run15/main`), from `/home/matthew/cc-engine-wt/e11/pcbjam`: `/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e11/kicad all --kicad-file common/jobs/job_export_pcb_3d.cpp`

```
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (12481 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (12061 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (7511 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (6618 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (1113 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (4967 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (11983 ms)
PASS kicad common/jobs/job_export_pcb_3d.cpp (4675 ms)
syntax-check: 0 failed
```

  The stub is a pcbjam file the script's `--kicad-file` cannot name (it prefixes `/workspace/kicad/`); it was checked with its own line from the cache volume's `kicad-kicad_editor/compile_commands.json` (`/workspace/wasm/stubs/exporter_step_stub.cpp`), the same extraction and mounts as the script's KiCad file mode, `-fsyntax-only`, read only: `PASS stub wasm/stubs/exporter_step_stub.cpp`. Reported to E0: the script could take pcbjam stubs (`--pcbjam-file`).
- What the build train must probe (frame pcb, island harness, `tests/fixtures/glasgow`):
  1. `JSON.parse(Module.kicadOutputJobs()).board.pcb_export_3d` holds `net_filter` and `component_filter` (strings, ""). Wrong if absent: K-3D3 not in the image.
  2. `project.export` `pcb_export_3d` `{format: 'step'}` with the island's 3D service writing at `outPath`: code 0, a `.step` in the run folder whose first bytes read `ISO-10303-21`. Wrong if code -1 "The 3D service made no file." (the island ignored `outPath`, or wrote elsewhere: compare the path in the request) or "The 3D file is made as ..." (the format list).
  3. Each of `stpz brep xao glb ply stl u3d pdf`: a non empty file with that extension. 
  4. `{format: 'step', component_filter: 'U1'}`: fewer bodies than without (occt-import-js count).
  5. `{format: 'step', cc_occ: false}`: code -1, "Only VRML is made here.", `dropped` does NOT list `cc_occ`; `{format: 'vrml', cc_occ: false}` runs. `{cc_occ: 'no'}`: the call answers false (nothing queued).
  6. `{format: 'vrml'}`: code 0 and a `.wrl` over 1 MB (kicad-cli 10.0.4 writes about 18 MB on Glasgow), its text holding `IndexedFaceSet` nodes with `coordIndex` lists (the board body, copper, mask, silk). Wrong if code -1 "The VRML exporter wrote no board (an empty scene)." (the image lacks kicad f24fb9ed) or a file far under 1 MB (only some layers tessellated: name the layers present). Repair e11-4 lists the checks of the shim's other user.
  7. Undo depth unchanged and the board unmodified after each run.

## 3d-e5: board model exports (PE33)

- Exports, all in `wasm/bindings/cc_e11_pcb.inc` (commit bc3aa29), block `EMSCRIPTEN_BINDINGS( cc_e11_pcb )`, MERGED IMAGE ONLY (code and registration under `KICAD_MERGED_EMBIND`; occ_service has no editor and the staged model probe is the 3D viewer library's): `kicadBoardModels` -> `cc_e11::boardModels`, `kicadBoardSetModels` -> `cc_e11::boardSetModels`, `kicadBoardEmbedModels` -> `cc_e11::boardEmbedModels`. No host ask, no marker, no KiCad file.
- `kicadBoardModels()`: `{ footprints: [{ uuid, ref, models: [{ path, offset, rotate, scale, opacity, show, found }], kind: { smd, tht, unspecified, notInPos, dnp } }], embedded: [{ name, type, bytes }] }`; `""` with no board or `pcbjam_open::busy()`. `found`: a `kicad-embed://` name the footprint or the board holds is `embedded` (else `missing`); otherwise KiCad's own resolver (`PROJECT_PCB::Get3DFilenameResolver`, the 3D cache's) resolving to an existing file is `project`, or `staged` when that file is under `/pcbjam/3dmodels/`; otherwise `PCBJAM_3D::FindStagedModel` (declared in the inc: its header `3d-viewer/3d_cache/pcbjam_model_fetch.h` is not on the bindings' include path) answering is `staged`; else `missing`. `kind.notInPos` and `kind.dnp` are for the board's current variant (`GetExcludedFromPosFilesForVariant`, `GetDNPForVariant`). `embedded` lists the board's files with KiCad's type word (`model`, `font`, `worksheet`, `datasheet`, `other`) and `bytes` the file's decompressed size.
- `kicadBoardSetModels(argsJson)`: `{ label: 'Update model names' | 'Change models', items: [{ uuid, models: [{ path, offset, rotate, scale, opacity, show }] }] }`, closed shapes (a line with any other key, `found` included, is refused); 1 to 4,096 items, uuids lowercase 8-4-4-4-12, each a footprint of the board and none twice; 0 to 64 lines each; paths 1 to 1,024 characters with no control character; vectors three finite numbers; opacity 0 to 1. False with nothing queued otherwise. On the apply coroutine every footprint is re-resolved (one gone: nothing pushed), then ONE `BOARD_COMMIT`, `Modify` per footprint, the lines replaced, `Push( label )`: one undo step. `kicadFootprintSetModels` (cc12) unchanged.
- `kicadBoardEmbedModels(argsJson)`: `{ items: [{ uuid, index, file, alt? }] }`, 1 to 4,096; every `file` and `alt` in ONE embed run folder `/tmp/cc-embed/<n>/<name>` (`cc_output::IsEmbedRunDir`, no deeper folder), existing, at most 25 MiB each and 512 MiB together (each file counted once); `index` a model line of that footprint, no line twice. Answers `{ embedded: lines relinked, names: [the distinct file names, sorted], bytes: their total }` once queued; `""` on refusal (nothing queued, the folder left to the host). On the coroutine: the footprints re-resolved; each file added with `EMBEDDED_FILES::AddFile( fname, false )` as `PCB_CONTROL::CollectAndEmbed3DModels` adds it (`pcbnew/tools/pcb_control.cpp` 2956, its type from the extension: model), `alt` beside it; if KiCad cannot add one, the names this call added are removed and nothing is pushed; else ONE commit, `Modify` per footprint (before the change, unlike KiCad's own function, which calls it after), each line's path set to `file->GetLink()` (`kicad-embed://<name>`), `Push( "Keep models inside the board" )`; the folder removed in every case. The embedded files themselves are board state outside the undo step, as in KiCad.
- Checks: the same syntax check run as 3d-e2 above (pcbnew and occ PASS: the guarded code compiles in the merged unit and is absent from the occ unit).
- What the build train must probe (frame pcb, island harness):
  1. Glasgow: `JSON.parse(Module.kicadBoardModels())` lists 272 footprints and 224 model lines; the 8 lines that leave the project read `found: 'missing'`. Wrong if every line reads `missing` (the resolver's project path) or `staged` lines read `missing` before the island staged them (expected: staged only after the 3D tab fetched them).
  2. `Module.kicadBoardSetModels` with 40 Glasgow footprints' lines rewritten from `${KICAD6_3DMODEL_DIR}` to `${KICAD10_3DMODEL_DIR}`: true; after `kicadCollabBusy` clears, the undo depth is one more; one Ctrl+Z (`key.press`) restores all 40 (read `kicadBoardModels` again). A uuid that is no footprint's: false, nothing changed; a line holding `found`: false.
  3. `tests/fixtures/models3d-small`: stage `cube.step` in `/tmp/cc-embed/1/`, `kicadBoardEmbedModels({items:[{uuid, index: 0, file: '/tmp/cc-embed/1/cube.step'}]})` answers `{embedded: 1, names: ['cube.step'], bytes: <size>}`; after the queue empties the board text holds `(embedded_files` and the line reads `kicad-embed://cube.step` with `found: 'embedded'`, `/tmp/cc-embed/1` is gone, one undo step restores the old path. A file in `/tmp/elsewhere/` answers `""`. A 26 MiB file answers `""`.

## lo-e3o-e: output job faults (PE48 the drill report, PE46's O23 half the footprint list)

(The manifest's `BUILD-NOTES-run15-outputs-faults.md` is this section.)

- Exports: none new; `kicadExportBoardOutput` changed in behavior. Merged image only: nothing.
- PE48, diagnosed in KiCad's `PCBNEW_JOBS_HANDLER::JobExportDrill`: with a temporary output folder (our run folder, as KiCad's jobset runner sets it) `outPath` is `/tmp/cc-out/<n>/`; the default report path is built inside it, absolute (`<board>-drill.rpt`), then passed again to `JOB::ResolveOutputPath( path, true, ... )`, which turns ANY absolute path into the temporary folder itself (`common/jobs/job.cpp` 127, `fn.AssignDir( m_tempOutputDirectory )`); `GenDrillReportFile` then writes over a folder and the job ends with exit code 4 (`ERR_INVALID_OUTPUT_CONFLICT`). The drill files themselves were written first. Fixed at the cause in kicad `pcbnew/pcbnew_jobs_handler.cpp` (commit ecdf1454, this lane's file; the manifest named the settings merge, but nothing the merge can send resolves once: `report_filename` stays reset and empty), under `__EMSCRIPTEN__`: a report path the handler built itself is used as built, a configured one is resolved as KiCad does; both writers (Excellon and Gerber). Plain builds unchanged. `cc_output_common.h`'s drill merge is unchanged. The plot op (`kicadPlotBoard`) never asks for a report (`pcbnew_embind.cpp` sets `m_generateReport = false`).
- O23, in the `cc_footprints` job body (pcbjam commit 86a6274): each library footprint is written once, keyed by name with the library footprint (`lib:name`, `GetUniStringLibId`) kept; a footprint placed twice from the same library footprint is skipped with NO line; only footprints of one name from different libraries are named in the existing warning ("More than one footprint is named ...; the first of each was kept.").
- Header recompile reach: none (two .cpp files).
- What the build train must probe (stack B, Glasgow, Edit mode, Board tab, Outputs):
  1. Drill files with the report on (`pcb_export_drill` `{generate_report: true}`): code 0, no failure line, the run folder holds the drill files AND `<board>-drill.rpt` (non empty, KiCad's "Drill report for" first line). Wrong if code 4 (the handler change is not in the image) or the report lands outside the run folder.
  2. Footprint list (`cc_footprints`): no warning line on Glasgow (it places several library footprints more than once); the `.pretty` holds one `.kicad_mod` per distinct library footprint (compare with the count of distinct `lib:name` among the board's footprints).

## lo-e3r-e: the rules answer (PE23)

(The manifest's `BUILD-NOTES-run15-pe23.md` is this section.)

- Exports: none new; `kicadResolveRules` changed in behavior. Merged image only: nothing.
- The answer is built by `ccInspectConstraints` and `ccReportClearance`, the helpers `pcbResolveRules` calls through `ccResolveRules` just above it; the lease names the body, the fault is in those helpers (no other lane touches them), so they were edited and this is reported to E0 as the lease's edge (pcbjam commit f4ace52).
- Solder mask expansion: for a track or a shape with no rule and no local value KiCad's `DRC_ENGINE::EvalRules` names no figure (its board setup default, `drc_engine.cpp` 1876, is reached for a pad only), so the record carried min, opt and max null. The item itself uses the board setup's `m_SolderMaskExpansion` in that case (`PCB_TRACK::GetSolderMaskExpansion`, `pcb_track.cpp` 1432; `PCB_SHAPE`'s the same); the answer now gives that figure as the engine gives a pad's: no parent rule, named "board setup" (so the record's source reads `board`), `opt` set.
- A pair's layers: a through hole pad's layer set is every copper layer KiCad knows, so the pair's copper intersection listed 32 layers; every layer set in `ccReportClearance` is now intersected with the board's enabled layers (`BOARD::GetEnabledLayers`), the diff pair layer and the edge cut report layers included.
- What the build train must probe (stack B, Glasgow, Edit mode, Board tab):
  1. `board.rules { a: <a track's uuid> }`: a `solder_mask_expansion` record with a number in mm in `opt` and source `board` (Glasgow's board setup value), never null.
  2. `board.rules { a: <a track>, b: <a pad of another net> }`: records on F.Cu or B.Cu only where both are.
  3. `board.rules { a, b }` for two pads of one through hole part: the records' layers name only Glasgow's 4 enabled copper layers, never `In5.Cu` and beyond. Wrong if 32 layers (the image lacks f4ace52).

## Handover (every unit of the lane done: 3d-e1, 3d-e4, 3d-e2, 3d-e5, lo-e3o-e, lo-e3r-e)

`git merge run15/main` in both worktrees (pcbjam `run15/main` 48e664e, build G's gitlink), then, after build G's container exited, from `/home/matthew/cc-engine-wt/e11/pcbjam`:

`/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e11/kicad all --kicad-file pcbnew/pcbnew_jobs_handler.cpp --kicad-file common/jobs/job_export_pcb_3d.cpp`

```
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (12081 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (11708 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (6942 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (6591 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (994 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (4685 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (10864 ms)
PASS kicad pcbnew/pcbnew_jobs_handler.cpp (9032 ms)
PASS kicad common/jobs/job_export_pcb_3d.cpp (3124 ms)
syntax-check: 0 failed
```

The stub's own compile line (the 3d-e2 section's method): `PASS stub wasm/stubs/exporter_step_stub.cpp`.

`scripts/kicad/check-run15-exports.sh --since run15/main --kicad /home/matthew/cc-engine-wt/e11/kicad --kicad-since run15/main`: `check-run15-exports: 157 names registered in the merged image (150 kicad names), 12 of 110 run 15 names, 0 failures`.

Reported to E0:
1. PE46's VRML cause is PCBJam's earcut GLU shim `libs/kimath/glu_tess/glu_tess_impl.cpp` (no lane's file): it never calls the BEGIN and END callbacks and ignores `GLU_TESS_BOUNDARY_ONLY`, so every `VRML_LAYER` is empty and O17 writes only its header. Held; needs an owner (the 3d-e2 section). Fixed in repair e11-4 (kicad f24fb9ed) once the controller leased the shim to this lane (RULINGS 21).
2. The lease's edge: 3d-e2 edited the 3D file check in `ccRunBoardOutput` (queued by `pcbExportBoardOutput`), lo-e3r-e edited `ccInspectConstraints` and `ccReportClearance` (called by `pcbResolveRules`); the leased bodies themselves held nothing to change. lo-e3o-e's drill fix is in kicad `pcbnew_jobs_handler.cpp` (this lane's file), not in the settings merge.
3. The syntax check script cannot name a pcbjam stub (`--kicad-file` prefixes the KiCad tree); a `--pcbjam-file` mode would cover `wasm/stubs/exporter_step_stub.cpp`.
4. `pcbnew_embind.cpp` carries this lane's dated line at the END of its header comment (not beside the seams' line) to keep merges with other lanes' lines apart.

## Repair e11-2: the render's picture size and its percent lines (build G)

- The size is KiCad's own. `RENDER_3D_RAYTRACE_BASE::initializeBlockPositions` (`3d-viewer/3d_rendering/raytracing/render_3d_raytrace_base.cpp` 1668 onward) lays the picture out in whole blocks of rays: block corners step 28 pixels (`4 * RAYPACKET_DIM - 4`, `RAYPACKET_DIM` 8), a corner counts only below `side - 36`, and the buffer is the last corner plus 32 rounded down to a multiple of 8. For 1600 by 1200 that is 1540 + 32 = 1572, so 1568, and 1148 + 32 = 1180, so 1176; desktop's `JobExportRender` writes `wxImage( raytrace.GetRealBufferSize() )` as our copy does. Measured with `kicad-cli pcb render` in `kicad/kicad:10.0` (image `sha256:3f4a0178...`, run with `--network none` on a copy of `tests/fixtures/glasgow`): `--width 1600 --height 1200 --quality basic` wrote a PNG whose IHDR reads 1568 by 1176 (248,380 bytes; ours was 249,794), `--quality high` 1568 by 1176, and `--width 800 --height 600` JPEG a SOF of 784 by 592. Nothing to fix in the picture.
- The percent lines were ours. kicad-cli's basic render printed `Rendering: 25 %`, `36 %`, `55 %`, `91 %`, `100 %` (8 CPUs; 33, 78, 100 in a second run), high 52 lines up to `100 %`: KiCad's own reporter prints every pass's percent, the last 100. Ours reported a pass's percent in the loop body, and the pass that finishes the tracing runs in the `Redraw` that answers false, so its line was never written: a basic render (no post processing, `raytrace_post_processing` follows the floor, off) ended on "Rendering 40 percent"; a high render's 100 came from the loop only because its post processing passes followed. Fixed (kicad 3ea52ae7, `pcbnew/pcbnew_jobs_handler.cpp`, the WebAssembly copy only): `passes.ReportPercent( *aReporter )` once more after the loop (no line repeats: `ReportPercent` writes only a moved percent).
- The record names the picture's own size: on success the job reports the info line `The picture is <w> by <h> pixels.` (from `GetRealBufferSize`) just before "Successfully created 3D render image". It is a line, not a new record key, because the island's reader of `.cc-export.json` is closed at exactly `{code, lines, dropped}` (`bridge/export.ts` 139 on the island's `w2/base`): a fourth key would make every export unreadable. Reported to E0 for the 3D island and site units: the spec's island e2e line "`pcb_render` answers a PNG of the asked size" should read KiCad's size (1568 by 1176 for 1600 by 1200, 784 by 592 for 800 by 600), and the Save view card's render section shows the size this line (or the picture's own header) names, never the size asked.
- What the next build must probe (frame pcb, island harness, Glasgow): (1) PNG 1600 by 1200 basic: lines `Rendering <n> percent` climbing, the last `Rendering 100 percent`, then `The picture is 1568 by 1176 pixels.`, then "Successfully created 3D render image"; IHDR 1568 by 1176, equal to the line. (2) High: the same, one `Rendering 100 percent` (never two). (3) JPEG 800 by 600: SOF 784 by 592, the line says 784 by 592. Wrong if: no 100 line on a basic render (the image lacks 3ea52ae7), or the line's size differs from the file's header.

## Repair e11-3: the render under the 200 ms line (build G's stop condition, the controller's ruling)

`3d-viewer/3d_rendering/raytracing/render_3d_raytrace_base.cpp` is no lane's file; it was leased to this lane for this repair only. Kicad e651379d, every change under `__EMSCRIPTEN__` (plain builds compile what they compiled):

1. The budget. `renderTracing`'s `timeLimit` is 100 ms in the WebAssembly build (KiCad's 400, or 750 over 40,000 blocks, in the `#else`). KiCad's own loop and its own break are unchanged: each worker stops after the block that crosses the limit, the main thread waits for all of them, then the job yields as before. Why 100: build G's high passes ran 392 to 414 ms on the 400 budget, so a pass overshoots its budget by at most about 15 ms; 100 leaves the 200 ms line about 85 ms clear for a slow block, a slow machine or the percent line, and it is what the ruling asked. Cost: about four times the passes (high: about 160 in place of 40), each paying one thread pool round and one turn of the browser (a `setTimeout( ..., 0 )`, 0 to 4 ms): reasoned under 1 s on high's 16.1 s and a few tens of ms on basic. The 3D viewer frame's interactive raytracing, if ever shown, gets the same shorter passes.
2. The other long steps, found by reading what one task held in build G (the probe listed the basic tasks by size, 568, 486, 404 ms, not in time order): 404 was the first tracing pass at the 400 budget; the other two were (a) the start: the job's setup, the scene build (`Reload`, called by the first `Redraw` while a reload is requested), the block layout and the preview pass, all in the first `Redraw`; and (b) the last tracing pass together with the picture: the RGBA split, `Mirror`, the PNG encode and the file write, which ran with no yield after the loop. Split where KiCad has a seam (`pcbnew/pcbnew_jobs_handler.cpp`, the WebAssembly copy):
   - the scene is built by KiCad's public `RENDER_3D_RAYTRACE_BASE::Reload( reporter, reporter, false )` before the loop, the same call `Redraw` makes (`Reload` clears `m_reloadRequested`, so `Redraw` does not build it again; `Reload` reads no window or block state, so building it before the block layout changes nothing), then a yield: the scene, and the layout with the preview, are two tasks;
   - a yield after the loop, so the last pass and the picture are two tasks;
   - a yield between the picture's buffers (split and mirror) and `SaveFile`.
3. Steps that cannot be split without rewriting KiCad's method (not rewritten):
   - **The scene build, `RENDER_3D_RAYTRACE_BASE::Reload`** (`create_scene.cpp` 405 to 1075): `BOARD_ADAPTER::InitSettings` (the board's layers as polygons), the raytracing objects, the models (on a project's first render, loaded into the project's 3D cache), the BVH accelerator, in one function with no step a caller can reach (`InitSettings` is called inside it; its only other mode, `aOnlyLoadCopperAndShapes`, leaves out the board body, the models and the camera's look at). Reasoned size: kicad-cli 10.0 natively prints "Reload time 0.498 s" to "0.645 s" on Glasgow (with and without the models, 1 or 8 CPUs); in the engine build G's JPEG 800 by 600 start task was most likely its 392 ms (its tracing, about 7,350 blocks at the about 29 blocks a ms of the PNG's first pass, fits the 260 ms task with the encode). So about 300 to 450 ms: OVER the line. Splitting it would take yields inside `Reload` between `InitSettings` and the scene build, inside `BOARD_ADAPTER`'s layer build (`create_layer_items.cpp`, already threaded per layer) and around the BVH build, each needing the job's yield callback passed into KiCad's 3D viewer code: a change to KiCad's method in files of no lane, for the controller to rule.
   - **The encode, wx's `SaveFile`** (libpng or libjpeg through wx's handler, one call): native zlib at libpng's default level takes 43 ms over Glasgow's 7,377,048 raw RGBA bytes (plus libpng's per row filter choice); reasoned 100 to 200 ms in the engine at 1568 by 1176. Probably under the line; it cannot be split (the handler writes the whole picture in one call); a smaller compression level would change KiCad's bytes and was not considered.
   - **High's post processing**, `postProcessShading` and `postProcessBlurFinish`: each one `Redraw` over every row on threads, the main thread sleeping in 10 ms steps until they end, with no budget. Build G's high showed no task over 414 ms, so each is under that; if the probe finds one over 200 ms, splitting it would take resumable row ranges across `Redraw` calls (a new state in KiCad's render state machine).
   - **The preview pass** (`renderPreview`, the first `Redraw`): one threaded call over the coarse blocks; reasoned small (a sixteenth of the rays), not measured.

### Checks (repairs e11-2, e11-3)

After `git merge run15/main` in both worktrees (pcbjam 4f0a02f, kicad f671d9b4), from `/home/matthew/cc-engine-wt/e11/pcbjam`:

`/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e11/kicad all --kicad-file pcbnew/pcbnew_jobs_handler.cpp --kicad-file 3d-viewer/3d_rendering/raytracing/render_3d_raytrace_base.cpp`

```
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (15056 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (14258 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (7516 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (7082 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (1102 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (5552 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (13257 ms)
PASS kicad pcbnew/pcbnew_jobs_handler.cpp (10141 ms)
PASS kicad 3d-viewer/3d_rendering/raytracing/render_3d_raytrace_base.cpp (6081 ms)
syntax-check: 0 failed
```

`scripts/kicad/check-run15-exports.sh --since run15/main --kicad /home/matthew/cc-engine-wt/e11/kicad --kicad-since run15/main`: `check-run15-exports: 205 names registered in the merged image (198 kicad names), 60 of 110 run 15 names, 0 failures`.

Header recompile reach: none (two .cpp files). `render_3d_raytrace_base.cpp` is compiled into the 3D viewer library, so every image that links it (kicad_editor and occ_service) relinks.

### What the next build must probe (e11-3, item 3 of the ruling)

Frame pcb, island harness, `tests/fixtures/glasgow`, the same three renders as build G (PNG 1600 by 1200 basic, PNG 1600 by 1200 high, JPEG 800 by 600 basic), each with a `PerformanceObserver` of type `longtask` (buffered) in the island frame AND one in the page that frames it (the page's own main thread is what a reader feels):

1. Every task over 50 ms in either, IN TIME ORDER with its start time, beside the record's lines. Expected: no task over 200 ms in either, except possibly the scene build (the first long task of each render, reasoned 300 to 450 ms, the step named above that cannot be split): its measured size goes to the controller. Wrong if a tracing pass task is over about 130 ms (the image lacks e651379d's budget), or the last pass and the encode are still one task (the yields are missing).
2. Each render's total time (the call to the record) beside build G's: basic about 1.5 s, high 16.1 s; the difference is the cost of the smaller budget (expected under 1 s on high).
3. The e11-2 lines and sizes above, on the same runs.

## Repair e11-4: the GLU shim (PE46's VRML half, done)

`libs/kimath/glu_tess/glu_tess_impl.cpp` is no lane's file; RULINGS 21 leased it to this lane for this repair. Kicad f24fb9ed (that file only; `earcut.hpp` unchanged).

- The cause, verified in the code. `VRML_LAYER` registers `GLU_TESS_BEGIN_DATA`, `VERTEX_DATA`, `END_DATA`, `ERROR_DATA` and `COMBINE_DATA` (`vrml_layer.cpp` 193 to 201), sets the winding rule (POSITIVE, or NEGATIVE for a holes only layer) and the normal (0, 0, 1), and `Tesselate` runs three passes: a `GLU_TESS_BOUNDARY_ONLY` pass that unites the solid contours into outlines, a second boundary pass over those outlines with the holes (its own and the imported ones) that leaves the outline minus the holes, then a triangle pass. `glPushVertex` stores into `vlist` and only `glEnd` turns it into an outline (`GL_LINE_LOOP`, exterior counterclockwise read as solid) or triangles (`GL_TRIANGLES`, fans, strips). The shim never called begin or end, ignored the boundary property, and treated contour 0 as the only outline and every other contour as its hole, so the first pass left `outline` empty, `Tesselate` failed with "no points in result" and every layer was empty.
- The repair, everything a GLU tessellator does for these calls, done in the shim:
  - Begin and end around every primitive, the DATA form when registered (else the plain one), called with the polygon's data pointer; the same for vertex, edge flag, combine and error.
  - `GLU_TESS_BOUNDARY_ONLY` and `GLU_TESS_TOLERANCE` are stored and read back; `gluTessNormal` is kept. The frame is the xy plane, y negated for a normal pointing down; with no normal, the sign of the polygon's area (GLU computes the normal and turns it to a positive area).
  - A polygon of several contours, or any boundary pass: the contours are resolved under the winding rule (ODD, NONZERO, POSITIVE, NEGATIVE as Clipper2's EvenOdd, NonZero, Positive, Negative; Clipper2 is KiCad's own polygon library, already linked into `gal` through `kimath` and compiled with `USINGZ`) in integer frame coordinates (centered, a power of two scale putting the farthest point between 2^30 and 2^31). Each output point carries in z the number of its input vertex; a crossing reaches the combine callback with the four edge ends and their weights along each edge, as GLU's crossings do, and is reused at the same point; with no combine callback, `GLU_TESS_NEED_COMBINE_CALLBACK` reaches the error callback and nothing is output (GLU's behavior). Boundary mode: one `GL_LINE_LOOP` per exterior (counterclockwise about the normal) and per hole (clockwise). Triangle mode: each resolved region (outline and its holes) triangulated by earcut, all triangles in one `GL_TRIANGLES` primitive, each turned counterclockwise about the normal, the edge flag (only when a callback takes it) before each vertex whenever it changes, true on ring edges.
  - A polygon of one contour in triangle mode keeps the earcut triangulation the shim always gave, kept or dropped by the winding rule as GLU would (one contour's winding is plus or minus one in the frame), each triangle turned counterclockwise.
  - `GLU_TESS_WINDING_ABS_GEQ_TWO` has no Clipper2 rule and no KiCad caller: a polygon of several contours under it gives no output.
- The other users (`grep -rn gluTess` over the kicad tree and the pcbjam tree): in the WebAssembly build only `common/gal/webgl/webgl_gal.cpp` (`WEBGL_GAL::drawPolygon`, 2472 to 2484) and `utils/idftools/vrml_layer.cpp` (the VRML export through `pcbnew/exporters/exporter_vrml.cpp`, and the IDF model reader `plugins/3d/idf/s3d_plugin_idf.cpp`). `opengl_gal.cpp` also calls it but is removed from the WebAssembly build (`common/gal/CMakeLists.txt` 55 onward) and links the system GLU in plain builds. Zone fills, the 3D viewer's layers and the board outline never pass through GLU (they triangulate through `SHAPE_POLY_SET`'s own triangulation); pcbjam's `wasm/gl1` covers GLU quadrics only. `WEBGL_GAL` always passes one contour with POSITIVE winding, no normal, and registers vertex, combine, edge flag and error only: it takes the one contour path, the rule keeps every contour (with no normal the area is turned positive), earcut gets the same input, so the same triangles. What changes for it: a clockwise triangle has two vertices swapped (the GAL draws with no face culling: the same pixels) and its edge flag callback, a no-op, is called on each change in place of once. Begin and end it does not register.
- Plain builds: unchanged. The shim is compiled only inside `if( EMSCRIPTEN )` (`common/gal/CMakeLists.txt` 96, the list that also swaps in the WebGL GAL); plain builds link the system GLU.
- Host check of the shim's logic (scratch, not committed): the shim compiled with `g++ -std=c++20 -Wall -Wextra -DUSINGZ` beside Clipper2's `clipper.engine.cpp`, driven with VRML_LAYER's own callback set: a 10 by 10 square with a 6 by 6 clockwise hole gives one `GL_TRIANGLES` of 8 triangles, area 64, none clockwise; in BOUNDARY_ONLY two `GL_LINE_LOOP`s, area +100 and -36; two overlapping squares (POSITIVE, boundary) one loop of 8 vertices, area 175, with 2 combine calls at (5, 10) and (10, 5), weights 0.25 each; two overlapping clockwise holes (NEGATIVE, boundary) one loop, area 43, 2 combines; two islands, one with a hole, 10 triangles, area 164; one clockwise contour with normal +z under POSITIVE nothing (as GLU); normal -z boundary gives the exterior clockwise in xy; a crossing with no combine callback error 100156 and no output. WEBGL_GAL's call pattern over 400 single contours (clockwise, counterclockwise, star shaped, self crossing, 3 to 42 vertices) gave the same triangle sets as the shim before this change (sorted triples compared, byte equal). A board body of 1,600 round holes of 24 vertices: boundary 36 ms, triangles 426 ms on the host at -O1, 41,602 triangles (n + 2h - 2).

### Checks (repair e11-4)

After `git merge run15/main` in both worktrees (pcbjam 2af2b67, kicad f03bd488), from `/home/matthew/cc-engine-wt/e11/pcbjam`:

`/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e11/kicad all --kicad-file libs/kimath/glu_tess/glu_tess_impl.cpp` (the shim has its own compile_commands line in `gal`, with Clipper2's include and `-DUSINGZ`)

```
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (11913 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (12467 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (7384 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (6830 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (990 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (4932 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (11505 ms)
PASS kicad libs/kimath/glu_tess/glu_tess_impl.cpp (2533 ms)
syntax-check: 0 failed
```

`scripts/kicad/check-run15-exports.sh --since run15/main --kicad /home/matthew/cc-engine-wt/e11/kicad --kicad-since run15/main`: `check-run15-exports: 214 names registered in the merged image (207 kicad names), 69 of 111 run 15 names, 0 failures`.

Recompile reach: one .cpp in `gal`, linked by kicad_editor and occ_service; no header. The syntax check drops `gal`'s precompiled header; the shim's own names sit in an unnamed namespace and its GL macros are guarded, so the header's GL and wx names do not collide.

### What the next build must probe (e11-4)

Frame pcb, island harness, `tests/fixtures/glasgow`:

1. `project.export` `pcb_export_3d` `{format: 'vrml'}`: code 0, a `.wrl` over 1 MB (kicad-cli 10.0.4 writes about 18 MB), its text holding `IndexedFaceSet` and `coordIndex` for the board body and the copper, mask and silk layers. Wrong if code -1 "The VRML exporter wrote no board (an empty scene)." (the image lacks f24fb9ed), or a much smaller file (name which layers are missing: a boundary pass failed on them; the record's lines carry `VRML_LAYER`'s error).
2. Beside kicad-cli 10.0.4's own `.wrl` of Glasgow: the count of `IndexedFaceSet` nodes per layer the same, and the vertex counts within a few percent (GLU and the shim may split crossings differently, never drop a region).
3. The shim's other user unchanged: the board and the schematic of Glasgow draw as before in Edit mode (graphic polygons, filled shapes and pads drawn through `WEBGL_GAL::drawPolygon`), and a zone refill on the fixture gives the same fill (zones never pass through the shim; this confirms nothing else moved). The 3D render of build G (PNG 1600 by 1200 basic) is byte for byte the same picture as build G's (the raytracer never calls GLU).

## Repair e11-5: STPZ answered Unknown export format (done)

The fault (3d-e2's e2e, `pcb_export_3d` `{format: 'stpz'}` on Glasgow): `export_failed: Unknown export format`. Cause, read in the code: `PCBNEW_JOBS_HANDLER::JobExportStep` names the default output file, when the job's configured output path is empty (always so in our engine, the run folder is the working path), by a switch on `m_format` that has no `FORMAT::STEPZ` case and falls to its default, `Unknown export format`, `ERR_UNKNOWN`. Its second switch (the format handed to `EXPORTER_STEP`) has STEPZ, and `EXPORTER_STEP::Export` writes it through `STEP_PCB_MODEL::WriteSTEP( ..., true )` (wxZlib gzip). Every other format of the list (step brep xao glb ply stl u3d pdf, and vrml) has its case in both switches.

The repair: kicad 7dae6c8b, `pcbnew/pcbnew_jobs_handler.cpp`, the case `JOB_EXPORT_PCB_3D::FORMAT::STEPZ` with `FILEEXT::StepZFileAbrvExtension` ("stpz", KiCad's own constant) in the default name switch, under `#ifdef __EMSCRIPTEN__` (a plain build compiles what it did; kicad-cli always passes an output path). Dated "Modified by" line added. No pcbjam change, no export added or renamed.

### Checks (repair e11-5)

After `git merge run15/main` in both worktrees, from `/home/matthew/cc-engine-wt/e11/pcbjam`:

`/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e11/kicad all --kicad-file pcbnew/pcbnew_jobs_handler.cpp`

```
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (14405 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (13501 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (6920 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (9039 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (5126 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (4999 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (12293 ms)
PASS kicad pcbnew/pcbnew_jobs_handler.cpp (10083 ms)
syntax-check: 0 failed
```

`scripts/kicad/check-run15-exports.sh --since run15/main --kicad /home/matthew/cc-engine-wt/e11/kicad --kicad-since run15/main`: `check-run15-exports: 256 names registered in the merged image (249 kicad names), 111 of 111 run 15 names, 0 failures`.

Recompile reach: one .cpp of pcbnew, no header.

### What the next build must probe (e11-5)

Frame pcb, island harness, `tests/fixtures/glasgow`, `project.export` `pcb_export_3d` with each format of 3d-e2's probe 3 in turn, `stpz brep xao glb ply stl u3d pdf`: each code 0 and a non empty file with that extension. For stpz: the file starts with the gzip magic `1f 8b` and unpacks to a STEP text that starts `ISO-10303-21;`, about the size of the `.step` (274,987 bytes in the integrator's probe). Wrong if stpz still answers `Unknown export format` (the image lacks kicad 7dae6c8b). The e2e stopped at stpz, so brep, ply, u3d and pdf run there for the first time: a failure of one of those is a new fault, named with its line.

## Repair e11-6: the render's progress lines while it runs (RULINGS 29 b)

The fault (build T3, ENGINE-FACTS lane e11): the render job's `Rendering <n> percent` lines reached the island only in the final record, because nothing wrote the island's live lines file (`bridge/export.ts` `EXPORT_LINES`, `<run folder>/.cc-export.json.lines`) while the job ran, so the 3D panel could show no running phase.

The repair: kicad 11f3edda, `pcbnew/pcbnew_jobs_handler.cpp`, under `__EMSCRIPTEN__` (the file static `CC_RENDER_PASSES` of the WebAssembly copy of the render job). `SetRunFolder( dir )`, called once the job's output path is resolved (the folder of the picture, which is the run folder: the host ignores every output path key), names `<dir>/.cc-export.json.lines` and removes a file of that name left in the folder. `ReportPercent` reports the line to the job's reporter exactly as before, then appends the same text and `\n` to that file and closes it. Every `ReportPercent` call is followed by `ccRenderYield()`, so each line is whole on disk before the browser gets its turn. The record `.cc-export.json` is written by `ccRunBoardOutput` from the same reporter as before: unchanged. No pcbjam change, no export added.

For the island (3d-i3): read `<run folder>/.cc-export.json.lines`, UTF-8 text, one line per progress step, each `Rendering <n> percent\n` (n 0 to 100, only when it moved, climbing, the last `Rendering 100 percent`); appended between yields, so any read between yields sees whole lines only; complete once `.cc-export.json` exists. Its name starts with the record's, so the folder read never answers it.

### Checks (repair e11-6)

After `git merge run15/main` in both worktrees, from `/home/matthew/cc-engine-wt/e11/pcbjam`:

`/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e11/kicad all --kicad-file pcbnew/pcbnew_jobs_handler.cpp`

```
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (16812 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (15949 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (7937 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (11050 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (5989 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (5779 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (16155 ms)
PASS kicad pcbnew/pcbnew_jobs_handler.cpp (12834 ms)
syntax-check: 0 failed
```

`scripts/kicad/check-run15-exports.sh --join --kicad /home/matthew/cc-engine-wt/e11/kicad` and the `--since run15/main --kicad-since run15/main` form both printed `check-run15-exports: 256 names registered in the merged image (249 kicad names), 111 of 111 run 15 names, 0 failures`.

Recompile reach: one .cpp of pcbnew, no header.

### What the next build must probe (e11-6)

Frame pcb, island harness, `tests/fixtures/glasgow`: `project.export` `pcb_render` `{format: 'png', width: 1600, height: 1200, quality: 'high', side: 'top'}` (about 15 s), and read `<dir>/.cc-export.json.lines` from the page between the engine's yields while it runs: the `Rendering <n> percent` lines appear one after another, climbing, before the answer arrives; the last is `Rendering 100 percent`; and the final `.cc-export.json` is byte for byte what build T3 gave (the same lines, "The picture is 1568 by 1176 pixels.", "Successfully created 3D render image"). Wrong if the file never appears before the record (the image lacks kicad 11f3edda), or the record gained or lost a line.

## Repair e11-7: a 3D model the export could not add is a warning (found by the 3D recheck after T3 and T4)

The fault: when the 3D service could not add a model ("Could not add 3D model for R1. File not found: ...", KiCad's WARNING in `exporter_step.cpp`; the read and add failures of `step_pcb_model.cpp` are ERRORs), the job's lines carried it only as an INFO line, which the island leaves out of `notes`. Cause, read in the code: `occExport` (`wasm/occ-service/occ_service_main.cpp`) gathers every message in one `WX_STRING_REPORTER` text, which keeps no severity, and the editor's EXPORTER_STEP shadow (`wasm/stubs/exporter_step_stub.cpp`) reported that whole text as ONE line: INFO when the export succeeded. The island's provider passes only `{ok, report}` back to the engine, so the severity has to travel inside the report text.

The repair: pcbjam c3c30d2, both files leased to this lane whole.

- `occ_service_main.cpp`: `CC_OCC_REPORTER` (over `WX_STRING_REPORTER`, the report text unchanged) also keeps every WARNING and ERROR message. `ccModelProblemLines` then goes through the board's footprints' 3D models by file name (each name once, empty names skipped). For a model whose file name a kept message holds, it appends one line to the report: `The 3D model <file> was not added: <that message on one line>`. With no warning or error naming a model, nothing is appended.
- `exporter_step_stub.cpp`: the report is split at newlines. Each line that starts `The 3D model ` and holds ` was not added: ` is reported as a WARNING of the job, after the rest, which is rejoined and reported exactly as before (INFO when ok, ERROR when not). A report with no such line is passed through byte for byte.

Kept as it was: every other line, the codes, the file check after the run. No export added; no kicad change.

### Checks (repair e11-7)

After `git merge run15/main` in both worktrees, from `/home/matthew/cc-engine-wt/e11/pcbjam`:

`/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e11/kicad all --kicad-file pcbnew/pcbnew_jobs_handler.cpp`

```
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (20809 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (20153 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (9495 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (12478 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (7198 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (6823 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (18059 ms)
PASS kicad pcbnew/pcbnew_jobs_handler.cpp (13013 ms)
syntax-check: 0 failed
```

The two changed files are pcbjam sources that the script's `--kicad-file` cannot name. Each was compiled `-fsyntax-only` with its own `compile_commands.json` line from the cache volume, using the script's KiCad file mode extraction and the same read only mounts: the stub's line from `kicad-kicad_editor`, the service's from `kicad-occ_service`.

```
PASS pcbjam wasm/stubs/exporter_step_stub.cpp (8554 ms)
PASS pcbjam wasm/occ-service/occ_service_main.cpp (8299 ms)
```

To prove the check bites, an undeclared name was planted in the service and the check printed `FAIL pcbjam wasm/occ-service/occ_service_main.cpp`; then the file was restored. Reported to E0 again: the script could take pcbjam sources (`--pcbjam-file`).

`scripts/kicad/check-run15-exports.sh --join --kicad /home/matthew/cc-engine-wt/e11/kicad` and the `--since run15/main --kicad-since run15/main` form both printed `check-run15-exports: 256 names registered in the merged image (249 kicad names), 111 of 111 run 15 names, 0 failures`.

Rebuild reach: occ_service (one source) and the editor's stub (one source).

### What the next build must probe (e11-7)

Frame pcb, island harness, the `models3d-small` fixture with one body's file left out of the staging. `project.export` `pcb_export_3d` `{format: 'step'}`: the STEP is made, code 0, and the answer's `notes` hold ONE warning, `The 3D model <that file> was not added: Could not add 3D model for <ref>. File not found: <path>`. With every file present, there is no warning, and the record's lines are byte for byte those before the repair. A model used by two footprints and missing gives one warning, not two. Wrong if the warning is absent (the image lacks pcbjam c3c30d2, in the service or the editor), or if a clean board gains or loses a line.

## Repair e11-8: a render of the last saved board (diagnosed, no engine change: the cause is in the island)

What the board output jobs read, from the code:

- **KiCad's jobs** (`pcb_render`, the drill files, the footprint position and report files, and the rest run through `ccRunBoardOutput`): these go to `fr->Kiway().ProcessJob( KIWAY::FACE_PCB, ... )`. Before that, `ccRunBoardOutput` refuses any run unless `Pgm().IsGUI()` is true and a project is open, so in this engine `PCBNEW_JOBS_HANDLER::getBoard` (kicad `pcbnew/pcbnew_jobs_handler.cpp` 481 to 518) always takes its GUI road: `editFrame->GetBoard()`, the frame's live board. The render's copy `ccJobExportRender` takes its board through the same `getBoard` (the lambda in the constructor), and hands that `BOARD*` straight to `BOARD_ADAPTER::SetBoard` and the raytracer. No file is read and nothing is saved.
- **The 3D exports**: the editor's EXPORTER_STEP shadow (`wasm/stubs/exporter_step_stub.cpp`) writes its `m_board`, the same live board, as text, and sends that text to the 3D service.
- **Our own outputs** (`ccRunOurBoardOutput`) are given `fr->GetBoard()`.

So the render does not differ from the other jobs: its picture is drawn from the board on screen, a moved part included. What reads the saved file is the island's staging of the model bodies before a render: `bridge/ext/3d-i3.ts` `before()` reads `ctx.readBytes( ctx.opened() )`, the `.kicad_pcb` as last saved, and stages only the model lines that file names (island-close-1 row 3d-i3, "Staging reads the saved board", DESIGN). A footprint added since the save, or a model line changed since the save, has no body in the engine's model folder. The raytracer then draws that footprint with no body, while every moved part is drawn at its new place. The 3D exports are not affected: their staging goes through the 3D service's stage hook, which is given the live board text.

The repair is therefore the island's (unit 3d-i3, `bridge/ext/3d-i3.ts`), not this lane's. Options:
- (a) the site saves before a render, as the close report recommends;
- (b) the island stages from the live board's model lines. That needs a read of the live board, which no engine export answers today for this purpose; it would be a new export, which E0 would list.

No engine file was changed. No `e11-8.fixed` is written: nothing is fixed in the engine.

### What the next build must probe (e11-8, the diagnosis)

On the Glasgow fixture (pcb frame), move one footprint without saving and run `project.export` `pcb_render`. The picture shows the part at its new place, the board is still marked modified and the undo depth is unchanged (a render is no edit). If the moved part is drawn at its saved place, this diagnosis is wrong and the job reads a file. Then add a footprint with a model not on the saved board and render: its body is missing until the board is saved. That gap is the island staging fault above.

## Repair e11-9: three outputs against kicad-cli 10.0.4 (site close rows E1, E2, E4; RULINGS 36)

Found by the site's Outputs parity test (unit lo-l8-s, captures on engine run 14). Two of the three causes are in KiCad files outside this lane's list, leased to this lane for this repair by the note: `pcbnew/dialogs/dialog_export_odbpp.cpp` and `pcbnew/exporters/export_d356.cpp`. No other lane changes either file.

1. **E1, the editor held after ODB++** (kicad 749b7b85). `DIALOG_EXPORT_ODBPP::GenerateODBPPFiles` runs the save on KiCad's thread pool and waits for it. `ccRunBoardOutput` holds a `wxLogNull`, but wx's logging switch is per thread: a pool thread's messages are buffered (`wxLog::OnLog`, `gs_bufferedLogRecords`) and shown from the main thread at its next idle, after the run has ended, as a message box. That box is a visible wx dialog, so the island's `keysBlocked` answers every later op busy until a reload. On the business card (no Edge.Cuts outline), `BuildBoardPolygonOutlines` returns false, so `ODB_STEP_ENTITY::GenerateProfileFile` logs "Failed to get board outline" every time. kicad-cli prints nothing and exits 0 on the same board (measured in kicad/kicad:10.0.4). The fix, under `__EMSCRIPTEN__`: the caller's `wxLog::IsEnabled()` is read on the calling thread, and the save lambda sets the pool thread's own logging to it for the save and restores it after (RAII, every road: success, a thrown IO_ERROR, an exception caught in ExportODB). KiCad's own menu road logs as before. The zip, the file and the job's lines are unchanged.
2. **E2, the IPC-D-356 via field** (kicad 9e0befee). `D356_RECORD` has no initializers, and `build_via_testpoints` only ORs the tented sides into `rk.soldermask`, so the field kept the stack's value: `S-2147483613` (0x80000023, garbage with both sides ORed in). Pads set the field to 3 first and were right (S0 to S3 equal on Glasgow). The fix, under `__EMSCRIPTEN__`: `rk.soldermask = 0` before the ORs. kicad-cli 10.0.4 writes `S3` on all 416 Glasgow vias (measured); the engine's file with `S-2147483613` replaced by `S3` equals the reference byte for byte, Glasgow and the business card.
3. **E4, BOM ranges** (pcbjam 67b4351). KiCad's `SCH_REFERENCE_LIST::Shorthand` already writes every reference when the range delimiter is empty. The cause was ours: `MergeSettings` forced the four delimiters for `sch_export_bom` to kicad-cli's CLI defaults (`-` for ranges) over whatever was sent, and the page sends `ref_range_delimiter: ""`. Now a delimiter that was sent is the job's, as a jobset gives it to kicad-cli; one not sent still takes the CLI default. This also applies to the other three: a sent empty `string_delimiter` now means no quoting (the TSV preset). kicad-cli 10.0.4 with an empty range delimiter writes `C1,C14,C19,C26,C27,C28,...` (measured), the reference capture's lines.

### Checks (repair e11-9)

After `git merge run15/main` in both worktrees, from `/home/matthew/cc-engine-wt/e11/pcbjam`:

`/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e11/kicad all --kicad-file pcbnew/dialogs/dialog_export_odbpp.cpp --kicad-file pcbnew/exporters/export_d356.cpp`

```
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (16901 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (15483 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (7851 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (10222 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (5542 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (5324 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (13945 ms)
PASS kicad pcbnew/dialogs/dialog_export_odbpp.cpp (7587 ms)
PASS kicad pcbnew/exporters/export_d356.cpp (6230 ms)
syntax-check: 0 failed
```

After the header's dated line was added to `cc_output_common.h`, `pcbnew eeschema occ` again: all PASS, `syntax-check: 0 failed`.

`scripts/kicad/check-run15-exports.sh --join --kicad /home/matthew/cc-engine-wt/e11/kicad` and the `--since run15/main --kicad-since run15/main` form both printed `check-run15-exports: 257 names registered in the merged image (250 kicad names), 112 of 112 run 15 names, 0 failures`.

Recompile reach: two pcbnew .cpp files, no header; `cc_output_common.h` is included by the binding TUs only.

### What the next build must probe (e11-9)

On the business card, board frame (no reload between rows): `project.export` `pcb_export_odb` (defaults, compression zip) answers its zip, then `project.export` `cc_gencad` (or any board row) answers files, not `busy`; no visible wx dialog in the registry after the next idle. Wrong if busy: the image lacks kicad 749b7b85, or another pool thread logs. On Glasgow, `project.export` `pcb_export_ipcd356`: every `VIA` line ends `S3` and the file equals the lo-l8-s reference but for its date line. On Glasgow, sch frame, `project.export` `sch_export_bom` with the catalog's settings (`ref_range_delimiter: ""`): rows read `C1,C14,C19,C26,C27,C28,...` and `D11,D12,D13`, the file equal to the reference; with `ref_range_delimiter: "-"` sent, `C26-C28` again. Wrong if ranges show with the empty delimiter (the image lacks pcbjam 67b4351).
