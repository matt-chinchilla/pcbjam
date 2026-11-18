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
- PE46, the VRML header only file, DIAGNOSED, not fixed in this lane (RULINGS 5: the cause is in no file of this lane, nor of any lane): `S3D::WriteVRML` writes the 16 byte "#VRML V2.0 utf8" header, then `SCENEGRAPH::WriteVRML` writes nothing for a transform with no shapes (`3d-viewer/3d_cache/sg/scenegraph.cpp` 233). The scene is empty because every `VRML_LAYER::Tesselate` (`utils/idftools/vrml_layer.cpp`) collects its triangles and outlines only between its `GLU_TESS_BEGIN_DATA` and `GLU_TESS_END_DATA` callbacks (`glStart`, `glEnd`, 1424 to 1530), and the WebAssembly GLU tessellator, PCBJam's earcut shim `libs/kimath/glu_tess/glu_tess_impl.cpp` (compiled by `common/gal/CMakeLists.txt` 96), never calls the begin or end callbacks and ignores `GLU_TESS_BOUNDARY_ONLY` (the first pass `Tesselate` runs for the outlines, 754 onward). `exporter_vrml.cpp` is unchanged (the manifest allows it only if the cause is there). The fix belongs in that shim (call begin with `GL_TRIANGLES` and end around the triangles; a boundary only pass needs a real winding rule tessellator, the SGI GLU reference one KiCad's desktop links): reported to E0, held. Until then the VRML guard keeps its line.
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
  6. `{format: 'vrml'}`: today code -1 with "The VRML exporter wrote no board (an empty scene)." (PE46 held, see the diagnosis); once the GLU shim calls begin and end, a `.wrl` over 1 MB (kicad-cli 10.0.4: about 18 MB).
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
1. PE46's VRML cause is PCBJam's earcut GLU shim `libs/kimath/glu_tess/glu_tess_impl.cpp` (no lane's file): it never calls the BEGIN and END callbacks and ignores `GLU_TESS_BOUNDARY_ONLY`, so every `VRML_LAYER` is empty and O17 writes only its header. Held; needs an owner (the 3d-e2 section).
2. The lease's edge: 3d-e2 edited the 3D file check in `ccRunBoardOutput` (queued by `pcbExportBoardOutput`), lo-e3r-e edited `ccInspectConstraints` and `ccReportClearance` (called by `pcbResolveRules`); the leased bodies themselves held nothing to change. lo-e3o-e's drill fix is in kicad `pcbnew_jobs_handler.cpp` (this lane's file), not in the settings merge.
3. The syntax check script cannot name a pcbjam stub (`--kicad-file` prefixes the KiCad tree); a `--pcbjam-file` mode would cover `wasm/stubs/exporter_step_stub.cpp`.
4. `pcbnew_embind.cpp` carries this lane's dated line at the END of its header comment (not beside the seams' line) to keep merges with other lanes' lines apart.

## Repair e11-2: the render's picture size and its percent lines (build G)

- The size is KiCad's own. `RENDER_3D_RAYTRACE_BASE::initializeBlockPositions` (`3d-viewer/3d_rendering/raytracing/render_3d_raytrace_base.cpp` 1668 onward) lays the picture out in whole blocks of rays: block corners step 28 pixels (`4 * RAYPACKET_DIM - 4`, `RAYPACKET_DIM` 8), a corner counts only below `side - 36`, and the buffer is the last corner plus 32 rounded down to a multiple of 8. For 1600 by 1200 that is 1540 + 32 = 1572, so 1568, and 1148 + 32 = 1180, so 1176; desktop's `JobExportRender` writes `wxImage( raytrace.GetRealBufferSize() )` as our copy does. Measured with `kicad-cli pcb render` in `kicad/kicad:10.0` (image `sha256:3f4a0178...`, run with `--network none` on a copy of `tests/fixtures/glasgow`): `--width 1600 --height 1200 --quality basic` wrote a PNG whose IHDR reads 1568 by 1176 (248,380 bytes; ours was 249,794), `--quality high` 1568 by 1176, and `--width 800 --height 600` JPEG a SOF of 784 by 592. Nothing to fix in the picture.
- The percent lines were ours. kicad-cli's basic render printed `Rendering: 25 %`, `36 %`, `55 %`, `91 %`, `100 %` (8 CPUs; 33, 78, 100 in a second run), high 50 lines up to `100 %`: KiCad's own reporter prints every pass's percent, the last 100. Ours reported a pass's percent in the loop body, and the pass that finishes the tracing runs in the `Redraw` that answers false, so its line was never written: a basic render (no post processing, `raytrace_post_processing` follows the floor, off) ended on "Rendering 40 percent"; a high render's 100 came from the loop only because its post processing passes followed. Fixed (kicad 3ea52ae7, `pcbnew/pcbnew_jobs_handler.cpp`, the WebAssembly copy only): `passes.ReportPercent( *aReporter )` once more after the loop (no line repeats: `ReportPercent` writes only a moved percent).
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
