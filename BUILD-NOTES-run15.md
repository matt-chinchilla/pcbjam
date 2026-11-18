<!-- Copyright (C) 2026 Circuit Center Inc -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Engine run 15: the integration (BUILD NOTES)

Kept by the integrator (E0) on pcbjam `run15/main`. Thirteen author lanes (e1 to e13) write run 15's C++ in parallel, each in its own files; this file is the record of the seams, the merges, the builds and what each build was probed for. The guide every author follows is `/home/matthew/cc-wave2/plan/ENGINE-AUTHORS.md`; the map of the run is `/home/matthew/cc-wave2/plan/engine-run15.md`; run 14's record is `BUILD-NOTES-run14.md` beside this file.

## Integration branches

| repo | branch | from | holds |
|---|---|---|---|
| pcbjam | `run15/main` | `cc/main` 9706f071 (run 14's notes commit, one past the built 44e3e9f) | the seams, the checks, every lane's merge, the gitlink bump |
| kicad-source-mirror | `run15/main` | `cc/main` 1b214bab (the run 14 build) | every lane's KiCad commits and the early headers |
| island (cc-editor-island) | `w2/base` | `phase4-selection` f42d98e | `scripts/engine/build.sh` builds `occ_service` (01ea889) |

`cc/main` of both engine repositories stays where it is until run 15 is accepted, so the run 14 island (cc23, cc24) still ships from its own commits. The pcbjam gitlink of `kicad` on `run15/main` is 1b214bab (= kicad `run15/main`) until the merge bumps it once.

Seat branches (cut 2026-11-18 from `run15/main`, pcbjam 2c5a086, kicad 1b214bab): `seat/run15-e1` to `seat/run15-e13` in pcbjam, worktrees `/home/matthew/cc-engine-wt/eN/pcbjam`; `seat/run15-eN` in kicad-source-mirror for the eleven lanes with KiCad files (e1, e2, e3, e6 to e13), worktrees `/home/matthew/cc-engine-wt/eN/kicad`. e4 and e5 have none: they read `/home/matthew/cc-engine/kicad-source-mirror` and syntax check against E0's `/home/matthew/cc-engine-wt/e0-kicad` (kicad `run15/main`). Disk: 51 GB free before, 39 GB after (the eleven kicad worktrees about 1.1 GB each, the pcbjam ones 36 MB).

## The seam repairs after review (commits b8eeea4, 2c5a086)

- `cc_fp_items.h` is included by `pcbnew_embind.cpp` under `KICAD_MERGED_EMBIND` only: a board face call to `cc_fpitems` left outside a merged guard now fails the syntax check's `occ` unit (planted `cc_fpitems::ccItemKind( "pad" )` in `cc_e3_pcb.inc`: `PASS pcbnew`, `FAIL occ` with `use of undeclared identifier 'cc_fpitems'`). Every kind body (`get`, `set`, `add`) takes `aOrigin` and `aBoardFrame` as the two wrappers do; the kind headers hold inline or static functions only, register nothing, and are included by their owner's fpedit inc alone (the board face reaches the kinds through the table).
- Lease slots: `cc_e7_lease.h` and `cc_e11_lease.h` near the top of `pcbnew_embind.cpp`, `cc_e13_lease.h` near the top of `eeschema_embind.cpp`, above the leased bodies (which sit above the lane incs): a leased body's includes, declarations and inline helpers go there.
- The dispatcher's frame probes in `kicad_editor_embind.cpp`: `ccActiveFrameType()` (the top `KIWAY_PLAYER`'s `GetFrameType()`, `FRAME_T_COUNT` when none), `fpEditorActive()`, `symEditorActive()`, `dsEditorActive()` (`pcbEditorActive()` and `schEditorActive()` read false in those three frames).
- `check-run15-exports.sh`: a changed file whose first ten lines carry our notice needs no Modified line; a non-kicad name in a lane file, a registration whose name is not a string literal, an em or en dash added, and a dispatcher face's declared half no file of the lane defines now fail; `#if 0` and `#if 1` are decided. Mutation run (planted, then reverted): a correct `kicadMicrowaveArm` in `cc_e2_pcb.inc` with a duplicate under `#if 0` passes with no Modified line; `ccSneaky`, `function( std::string( "kicadBoardTuningSet" ).c_str(), ...)`, an em dash in a comment and an undefined `comparePcb` declared in `cc_e10_dispatch.inc` each give their FAIL line; defining `comparePcb` in `cc_e10_pcb.inc` clears it.
- After: `syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e0-kicad all` PASS on all seven units, `syntax-check: 0 failed`; `check-run15-exports.sh` `146 names registered in the merged image (139 kicad names), 1 of 110 run 15 names, 0 failures`.

## The seams (commit 4ed5328)

- Every lane's faces, created empty but for the notice, `namespace cc_eN { }` and `EMSCRIPTEN_BINDINGS( cc_eN_<face> ) { }`, and included ONCE, outside any `KICAD_MERGED_EMBIND` guard:
  - board `cc_eN_pcb.inc` (e1, e2, e3, e5, e6, e7, e8, e9, e10, e11, e12, e13) after `cc_rules_pcb.inc` in `pcbnew_embind.cpp`;
  - schematic `cc_eN_sch.inc` (e1, e5, e6, e7, e8, e9, e10, e12, e13) after `cc_rules_sch.inc` in `eeschema_embind.cpp`;
  - dispatcher `cc_eN_dispatch.inc` (e1, e3, e5, e6, e7, e10, e12) before `EMSCRIPTEN_BINDINGS(kicad_editor)` in `kicad_editor_embind.cpp`;
  - footprint editor `cc_eN_fpedit.inc` (e4, e5, e6) in the new TU `fpedit_embind.cpp`.
- Two new TUs, merged image only, the `symedit_embind.cpp` precedent: `fpedit_embind.cpp` (include subdir `pcbnew`, no `-DKICAD_MERGED_EMBIND`; `cc_output_common.h`, `cc_fp_items.h`, a `__has_include` slot for e4's `fpedit_common.h`, then the three fpedit incs) and `dsedit_embind.cpp` (include subdir `pagelayout_editor`, an empty frame for e6: `namespace cc_e6`, `EMSCRIPTEN_BINDINGS( cc_e6_dsedit )`). `build-kicad-target.sh` adds both objects to the merged image's `EMBIND_OBJ` and compiles them after `symedit_embind.cpp`.
- `compile_embind_tu` adds `${KICAD_DIR}/${subdir}/dialogs` and `${KICAD_DIR}/${subdir}/tools` at the END of its include list (an include that resolved before resolves the same way). The step 7.1 comment no longer names Pyodide.
- `cc_fp_items.h` (R1): declarations of `cc_fpitems::CcItemKind`, `ccRegisterItemKind`, `ccItemKind`, `ccFpItemJson`, `ccFpItemApply`; included by `pcbnew_embind.cpp` and `fpedit_embind.cpp`; the single definitions are e4's (`cc_e4_fpedit.inc`).
- The settings section registry (R7) in `cc_rules_pcb.inc` (`CC_PCB_SECTION`, `ccRegisterPcbSection`, `ccPcbRegisteredSection`, `ccPcbBuiltinSections`) and its twin in `cc_rules_sch.inc` (`CC_SCH_SECTION`, `ccRegisterSchSection`, with `reconnect`): consulted after the built-in sections in the get (`ccPcbSettingsGet` / `ccSchSettingsGet`), the set (a branch after every built-in one in `ccRunPcbSettingsSet` / `ccRunSchSettingsSet`, then Board Setup's or Schematic Setup's OK steps when `setupOk`) and the lists (`ccPcbSections()` / `ccSchSections()` now return built-ins plus registered names). The schematic set's refusal now keeps a registered section's own error (`if( !ok && error.empty() )`, as the board set already did; no built-in branch sets an error there, so run 14's behavior is unchanged). A registered section's record keys join the record beside `value`.
- The export `kicadSettingsSections` (`kicad_editor_embind.cpp`): `{"board": [names], "schematic": [names]}`, each sorted, from the halves `pcbSettingsSections()` and `schSettingsSections()`.
- `cc_output_common.h`: `EmbedRoot()` (`/tmp/cc-embed/`) and `IsEmbedRunDir()`; `pcb_render` in `BoardJobs()`; `MergeSettings` calls `ccFixPcbExport3d` (`cc_output_3d.h`: run 14's VRML-only refusal and forced keys, moved verbatim) and `ccFixPcbRender` (`cc_output_render.h`: refuses with "The render is not made here."); `JobsWithDefaults()` leaves `pcb_render` out while `ccPcbRenderListed()` is false. The bodies are e11's after the seams.

## The checks (commits b4c97d3, 741cf62)

- `scripts/kicad/check-run15-exports.sh [--join] [--since <rev>] [--kicad <tree>] [--kicad-since <rev>]` with the list `wasm/bindings/run15-exports.txt` (110 `new` lines from the manifest: the 109 names of the lanes' `newExports` plus E0's `kicadSettingsSections`; 138 `run14` lines, the names the merged image registered at 9706f071, the same 138 run 14's probe counted on `Module`). It models the merged image's preprocessing (KICAD_MERGED_EMBIND per TU, every local include followed, comments dropped, undecidable `#if` arms counted both ways) and fails on a name registered twice, a registered name in neither list, a run 15 name registered outside its lane's files, (with `--join`) a listed run 15 name not registered, a new file without the notice, a changed file adding no dated "Modified by Circuit Center Inc" line, and any agent, tool, model or session name in what changed. Run before build G, before every train build and every repair build, with `--join` at the last merge.
- `scripts/kicad/syntax-check-run15.sh --kicad <tree> [all | pcbnew eeschema symedit fpedit dsedit kicad_editor occ] [--kicad-file <path>]`: `em++ -fsyntax-only` in the builder image against the build cache mounted read only, with `build-kicad-target.sh`'s own `compile_embind_tu` and merged call list read at run time (Debug flags, `-DDEBUG`). `occ` is `pcbnew_embind.cpp` without `KICAD_MERGED_EMBIND`, as `occ_service` compiles and links it. It refuses while a build runs; it takes no lock.

Measured on `run15/main` (741cf62): `syntax-check-run15.sh --kicad <run15 kicad> all` passes every unit (pcbnew 15 s, eeschema 16 s, symedit 11 s, fpedit 4 s, dsedit 1 s, kicad_editor 6 s, occ 15 s); `check-run15-exports.sh` passes (139 names registered, 0 failures); `--join` fails with the 109 lane names not yet registered, as it must before the merges.

## How a build is launched, and from where

- **The tree.** Island `w2/base` 98bdfda adds the name `run15` to `scripts/engine/build.sh`: pcbjam `run15/main`, kicad `run15/main`, wx `cc/main`, built in `/home/matthew/cc-engine/tree-cc8` (its clean worktrees moved to those commits, pcbjam on branch `build/run15`; the same container paths, so ccache serves it, and no third source tree on a full disk), outputs in `/home/matthew/cc-engine/out/run15/` (`kicad_editor.js`, `.wasm`, `wx.js`, `wx-dom.js`, `images.tar.gz`, `ngspice_service.{js,wasm}`, `occ_service.{js,wasm}`, `meta.json`, `build.log`, `memory.log`). `out/cc8/` (run 14's served engine) is never touched by it; `cc/main` never moves. A later `build.sh cc8` moves tree-cc8 back to `cc/main`. The script is run from E0's island worktree `/home/matthew/cc-w2/island/e0-occ` (on `w2/base`), never from the main island checkout (`phase4-selection` has no `run15` case and no occ_service).
- **The launch** (E0 only, detached, holding TWO of the machine's three gate slots so at most one other heavy command runs beside a build). Measured: a plain `gate.sh gate.sh ...` holds NO slot by the time the build runs, because each stage reopens fd 9 (the inner gate.sh's `exec 9>>`, then `build.sh`'s own `exec 9>` for the build lock) and closing the descriptor drops the flock; moving the slot to another descriptor before each stage holds two (a probe of the three slot files from inside: `slot 1 held`, `slot 2 held`, `slot 3 free`). So:

  ```bash
  cd /home/matthew/cc-w2/island/e0-occ
  setsid nohup /home/matthew/cc-wave2/bin/gate.sh bash -c \
    'exec 8>&9 9>&-; exec /home/matthew/cc-wave2/bin/gate.sh bash -c "exec 7>&9 9>&-; exec bash scripts/engine/build.sh run15"' \
    > /home/matthew/cc-engine/out/run15-launcher.log 2>&1 < /dev/null &
  ```

  `build.sh` then takes `/home/matthew/cc-engine/out/.build.lock` itself (one engine build machine wide). Watch `out/run15/build.log`; the end line is `engine build run15 done in <s>s (peak used <n> GB)` or the recipe's failure tail.
- **Before every launch** (in `/home/matthew/cc-engine-wt/e0-pcbjam` on `run15/main`): the kicad gitlink equals kicad `run15/main`; `scripts/kicad/check-run15-exports.sh --kicad /home/matthew/cc-engine-wt/e0-kicad` (with `--join` for the last train build) prints `0 failures`; `gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e0-kicad all --kicad-file <every KiCad .cpp the merges changed>` prints `syntax-check: 0 failed`; no syntax check running (an author's check refuses while the build runs; the build rewrites the cache volume).
- **How long** (run 14's logs, `engine-run15.md` section 4): a build whose change is only in the pcbjam bindings 445 to 692 s; a KiCad `.cpp` change 1,448 to 1,600 s; a widely included KiCad header 1,653 s or more. Run 15 adds `occ_service`, never built here: its first build configures and compiles its own KiCad tree (`build-wasm/kicad-occ_service`), not measured (est. 20 to 40 min more on build G only; ccache then). Build G est. 45 to 70 min; a train build with the header lanes est. 30 min; a bindings only repair est. 10 to 12 min. Peak memory 19 to 23 GB.
- `occ_service` links `pcbnew_embind.o` compiled WITHOUT `KICAD_MERGED_EMBIND` (the recipe's `EMBIND_APP=pcbnew`): a board face that reaches a merged-only definition must sit under `#ifdef KICAD_MERGED_EMBIND` (the `occ` unit catches `cc_fpitems`; an extern half of another TU is proved only by this link).

## How a seat branch is merged

In E0's worktrees only (`/home/matthew/cc-engine-wt/e0-pcbjam` and `/home/matthew/cc-engine-wt/e0-kicad`, both on `run15/main`):

1. The lane's handover: its `BUILD-NOTES-run15-eN.md`, its syntax check `all` and export check pasted, both run after `git merge run15/main`.
2. kicad: `git -C /home/matthew/cc-engine-wt/e0-kicad merge --no-ff seat/run15-eN` (lanes with KiCad files). pcbjam: `git -C /home/matthew/cc-engine-wt/e0-pcbjam merge --no-ff seat/run15-eN`. The lanes' files are disjoint (one author per KiCad file, per-lane incs, leases on separate bodies), so a conflict is a rule broken: it goes back to the lane, never resolved by E0 inside a lane's code.
3. Once per build, after the last merge of that build: the gitlink, `git -C /home/matthew/cc-engine-wt/e0-pcbjam update-index --cacheinfo 160000,$(git -C /home/matthew/cc-engine-wt/e0-kicad rev-parse HEAD),kicad` and one commit `kicad: run15/main at <sha> (<lanes>)`.
4. The two checks above on the merged tip, then the launch.
5. An early header commit (ENGINE-AUTHORS.md section 7) is merged the same way, as that one commit (`git merge --no-ff <sha>`), as soon as its lane says it landed; no gitlink bump until the next build.

## The order

1. **Build G** = the seams + E0-OCC + the gate units at their GATE commits: fp-e2 (e4: `fpedit_common.h`, then `kicadFpEditOpen`, `kicadFpEditTake`, `kicadFpEditState`), 3d-e1 and 3d-e4 (e11: occ_service as built, `pcbnew_jobs_handler.cpp`'s render), ds-e1 (e6: `single_top.cpp`, `pagelayout_editor/CMakeLists.txt`), st-open-kicad5-e (e9: K-REMAP, `kicadImportLegacyProject`, `kicadHostLegacy`), e-librows and e-block-save (e7). Each of these lanes names its gate commit in its running notes; E0 merges exactly that commit of the seat branch (`git merge --no-ff <gate sha>`, both repositories), never the branch tip. A gate unit not landed at the cut rides the first train build (RULINGS 4, `buildGNotes`). The stop conditions are read before anything else merges.
2. **The train** (I1 and the repairs): lanes merge as they hand over, at each free lock, the rest of each build G lane merging on top of its gate commit. The four header lanes (e1 `action_menu.h`, `kidialog.h`, the entry dialog headers; e3 `pad_tool.h`; e9 `specctra.h`; e10 `lib_symbol.h`, `footprint.h`) are held until all four have handed over and ride ONE build, so the about 27 min header rebuild is paid once. Lanes leasing existing bodies (e7, e11, e13) merge after the lanes whose incs they call, if any.
3. Probe and repair: every fix found in one probe pass rides ONE repair build. `--join` on the last build.
4. Before a build replaces `out/run15/`, a probed build that must stay readable is copied to `out/run15-<label>-keep` (the script deletes the old outputs of the name it builds).

## How each author's exports are probed

From a scratch island worktree of `w2/base` (never the main island checkout): `npm run fetch-engine -- --local /home/matthew/cc-engine/out/run15`, served on that worktree's own port pair, in the GPU window (CDP 9222), one tab at a time closed in a `finally`, no timing read while a build runs. First the seams list below, then, for each merged lane, the probes its `BUILD-NOTES-run15-eN.md` lists (the call, the fixture, the expected record and undo depth, the failure that would mean it is wrong), each result written under "Builds" here. Island e2e seats run their own specs on their own ports after.

## What every build must probe

Before anything else, on every build (seams regressions):

1. `Module` holds 138 run 14 `kicad*` functions plus every run 15 name merged so far, and `check-run15-exports.sh --join`'s count of registered names equals the count on `Module`.
2. `kicadSettingsSections()` answers both faces with run 14's 13 board and 9 schematic sections plus every registered one; `kicadSettingsGet` and `kicadSettingsSet` of a run 14 section (netClasses on Glasgow) answer as on run 14; a registered section's set writes its record with `value`.
3. `kicadOutputJobs()` lists no `pcb_render` (until e11 flips `ccPcbRenderListed`); `kicadExportBoardOutput` with `pcb_export_3d` and `format: "vrml"` behaves as run 14 (44e3e9f's header-only refusal line), any other format refuses with "Only VRML is made here." (until e11 lands the 3D exports).
4. The fpedit and dsedit objects are linked (their bindings blocks register: any lane name in them is on `Module`); the footprint editor frame (`--frame fpedit`) and the drawing sheet frame (`--frame ds`) boot with no KiCad chrome and no beige frame (RULINGS 15).
5. `occ_service.js` and `occ_service.wasm` are in `out/<name>/` with `meta.json`; their gzipped size (stop over 20 MB, 3D gate 0) and that the service boots under the island's headers.
6. Heap and timing are read only with no build running (the GPU window shares the CPU).

Build G (the seams, E0-OCC and the gate units at the cut: fp-e2, 3d-e1, 3d-e4, ds-e1, st-open-kicad5-e, e-librows, e-block-save) also reads the gates and stop conditions `mq.py author e0` lists under `buildGNotes`: occ over 20 MB gzipped, a render block over 200 ms, FP gate 0 item 2, the ds frame's boot and chrome, K-REMAP on ecc83, PJ gate 1's first two clauses. Each train build adds, per merged lane, the probes that lane's notes list.

## Merges

The early header commits (ENGINE-AUTHORS.md section 7), each merged alone with `git merge --no-ff <sha>`, then `syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e0-kicad all` on the merged tip (every unit PASS, `syntax-check: 0 failed`); the pcbjam ones also `check-run15-exports.sh` (0 failures). No gitlink bump (the next build takes it). The log line of each is in `/home/matthew/cc-wave2/engine/status/merged.log`.

| lane | repo | commit | merge on `run15/main` | holds |
|---|---|---|---|---|
| e3 | kicad | 426e5044 | 54afb1c8 | `pcbnew/tools/pad_tool.h`: `PAD_TOOL::enumerateLoop` declared under `__EMSCRIPTEN__` with the struct's forward declaration (also checked: `--kicad-file pcbnew/tools/pad_tool.cpp` PASS) |
| e7 | kicad | a0bd3c27 | c57840f9 | `include/cc_host_design_block.h` (`ccHostDesignBlock()`, `ccSetHostDesignBlock`, the options and the take), included by no file yet |
| e1 | kicad | d0055781 | 79c5c7af | `include/host_prompt.h` (`CcHostPromptModal`, `CcHostPromptModeless`, `CC_PROMPT_RECEIVER`, the parked prompts, the message capture), included by no file yet |
| e8 | pcbjam | 4c1c94f | 9b8bcd7 | R2: the symbol editor's readers and appliers moved into `symedit_common.h`'s `cc_symedit`; read line by line against the removed code: the same bodies made `inline`, plus a `aYUp = true` default on the point, text, text box and shape helpers and `readItemSetBounded( aSet, aUnits, aBodies, ... )` under the symbol wrapper that passes the symbol's counts as before |
| e4 | pcbjam | 6c44e59 | 7181009 | `wasm/bindings/fpedit_common.h`, taken now by `fpedit_embind.cpp`'s `__has_include` slot (fpedit unit PASS with it) |

Build G's gate commits, each merged alone with `git merge --no-ff <sha>` (never a branch tip), both repositories; the log lines are in `merged.log`:

| lane | units | pcbjam | kicad |
|---|---|---|---|
| e6 | ds-e1 | b6b7c17 (merge a23525a) | a3d9fc1d (merge 50a84e26) |
| e11 | 3d-e1, 3d-e4 | 0aa05db (merge 68459e7), with the e11-1 repair notes 8f6bfce (merge 7ec9d6f) | 2ea86e5d (merge e4e0860a), with the e11-1 repair a2fabc61 (merge be164203) |
| e4 | fp-e2 gate | c46948b (merge af3d036) | none |
| e9 | st-open-kicad5-e | 1f93c1f (merge 9f3a5d9) | 73e270ac (merge b49e4476) |
| e7 | e-librows, e-block-save | 7e67dba (merge ed56f98) | 10d3945e (merge caf17fbe) |

The gitlink, once: 48e664e `kicad: run15/main at caf17fbe`. The two checks on that tip: `check-run15-exports.sh` `154 names registered in the merged image (147 kicad names), 9 of 110 run 15 names, 0 failures`; `syntax-check-run15.sh all` with every KiCad source the merges changed (`single_top.cpp`, `dialog_symbol_remap.cpp`, `files-io.cpp`, `project_rescue.cpp`, `pcbnew_jobs_handler.cpp`, `pad_tool.cpp` for the header, `sch_design_block_utils.cpp`, `pcb_design_block_utils.cpp`): every unit and file PASS, `syntax-check: 0 failed`.

A repair of the checks before build G (2389ab4): the syntax check's build guard read any running container with the compose label of `kicad-wasm-cc-engine`, and a syntax check's own `docker run` of the builder image carries that label from the image under a random name, so one author's check refused every other's as "an engine build is running". The guard now reads the recipe's containers by name (`^kicad-wasm-cc-engine-`); measured: with one author's check up, the old filter counts 1 container and the new one 0, and the new one still matches the recipe's own `kicad-wasm-cc-engine-kicad-wasm-builder-1`.

## Builds

### Build G (2026-11-18)

- Launched 07:14 EST from `/home/matthew/cc-w2/island/e0-occ` as above; it waited 24 min for its two gate slots (about ten seats' heavy commands queued on each slot), ran 07:38:12 to 08:15:45. End line: `engine build run15 done in 2252s (peak used 13 GB): /home/matthew/cc-engine/out/run15`. Sources pcbjam 48e664e, kicad caf17fbe, wx d235efd, shared 7791275. wx rebuilt clean (the backend stamp dropped). The engine's own compile took about 10 min; `occ_service` configured and compiled its own tree (`build-wasm/kicad-occ_service`) in about 18 min and linked (`[100%] Built target occ_service`, no undefined symbol in the log). Kept as `/home/matthew/cc-engine/out/run15-G-keep`.
- Sizes: `kicad_editor.wasm` 64,597,729 bytes (run 14 64,202,030: +395,699), gzip -9 17,009,200, brotli 11,400,138. `occ_service.wasm` 60,622,934 raw, 18,721,167 gzip -9, 13,080,739 brotli; `occ_service.js` 265,497 raw, 64,634 gzip -9, 56,133 brotli; together 18,785,801 bytes gzipped, under the 20 MB stop.
- How it was probed: a scratch island worktree of `w2/base` (`/home/matthew/cc-w2/island/e0-probe`, nothing committed), its own port pair 4320 and 4321, the GPU window, one tab at a time closed in a finally, with no engine build running. `fetch-engine.mjs` of `w2/base` refuses a `meta.json` that lists `occ_service.*`, so the engine was fetched from a staging folder of the same files with those two rows left out, and the icon archive re-recorded (`rasterize.mjs`, `repack.mjs`) as the fetch asks. The fpedit and ds frames were booted through two probe-only frame descriptors in that worktree (`bridge/ext/fp-i1.ts`, `bridge/ext/ds-i1.ts`, the minimal `FrameKind` the specs give; their units have not landed). The occ service was booted with PCBJam's own worker wrapper (`web/standalone/src/wasm/occ-worker.js`) from a blob in the island frame, its glue and wasm served from the island origin. Scripts and logs: the session scratch folder `e0g/`.

Every build's seams:
1. `Module` holds 147 `kicad*` functions: the 138 of run 14, none missing, and the 9 run 15 names merged (`kicadDesignBlockSave`, `kicadFpEditOpen`, `kicadFpEditState`, `kicadFpEditTake`, `kicadHostLegacy`, `kicadImportLegacyProject`, `kicadLibsRemoveEntry`, `kicadPathVarsSet`, `kicadSettingsSections`); none in neither list. Equal to the check's 147 kicad names.
2. `kicadSettingsSections()`: board 13, schematic 9, run 14's (no lane registers a section yet). `settings.get netClasses` on Glasgow answers the section; a patch of the Default class clearance 0.1499 to 0.2 answers and reads back 0.2, and back to 0.1499. (A set of the whole section answers `bad_args`, as on run 14: the island refuses `null` in a project file patch and the section carries `net_colors: null`.) A registered section's set: not read (none registered).
3. `kicadOutputJobs()` lists `pcb_render` (e11 turned `ccPcbRenderListed` on in 3d-e4, as planned) with KiCad's own keys; `pcb_export_3d` VRML answers run 14's "The VRML exporter wrote no board (an empty scene).", STEP "Only VRML is made here.".
4. The fpedit and dsedit objects are linked (the fpedit names are on `Module`). Both frames boot to `ev.ready` with `kicadChromeShown()` false; the screencast classifier saw 0 chrome and 0 beige frames in 10 starts (ds and fpedit, 3 at 1x and 2 at 4x each). The in-frame recorder saw the chrome laid out for one animation frame under the loading screen in each start, as the shipping frames do (boot-e1's to remove).
5. `occ_service.js`, `.wasm` and `meta.json` in `out/run15/`; sizes above; the service boots under the island's headers in 753 ms (`crossOriginIsolated` true) and makes Glasgow's STEP (274,987 bytes, 1.8 s; library bodies not found, as expected with no model store).

Build G's gates:
- 3D gate 0 step 4 (3d-e4): every render blocked the island frame's main thread over 200 ms (PNG 1600 by 1200 basic: 568, 486, 404 ms; high: 34 tasks of 392 to 414 ms over 16.1 s; JPEG 800 by 600: 392, 260 ms). STOP CONDITION FIRED. The files: code 0, PNG IHDR 1568 by 1176 for 1600 by 1200 (fix note e11-2), JPEG starts FF D8, undo depth 0 before and after, no window; width 5000, `jpg` and 100000 by 100000 refused with code -1 and the right line, the engine answered after.
- FP gate 0 item 2 (fp-e2): passed, plan A. The SOIC opens by text with no library row: state `open true, padCount 8, depth 0, modified false, token t1, type smd, lib fpedit-small_parts`, no dialog or info bar; take, open, take byte equal (6,453 characters); `(kicad_pcb)`, `garbage` and an extra key refused with nothing changed; fifty open and take cycles left `HEAP8` at 386,662,400 bytes.
- Drawing sheet editor gate 0 (ds-e1): item 1 passed (PL_EDITOR_FRAME with KiCad's default sheet, drawn at 16,535 by 11,693 mils; boot to ready median 979 ms of 5; chrome hidden at ready; `kicadSetChrome(true)` shows File, Edit, View, Place, Inspect, Preferences, Help and the toolbars, `false` hides them); item 2 passed (heap at ready 386,662,400 bytes against an empty sch frame's 556,793,856; wasm +395,699 bytes); item 3 passed (`audit-merged-symbols.sh` in the builder image against the cache volume: the three pl_editor strong lists and the weak list empty); item 4 not read (needs ds-e2). A sch or pcb boot writes no `pl_editor.json`.
- ST gate 0.6 (st-open-kicad5-e): not read. `kicadImportLegacyProject` throws `SuspendError: trying to suspend without WebAssembly.promising` on ecc83 and on electric (registered without `PCBJAM_PARKER_POLICY`): fix note e9-1. The six refusals answer false.
- PJ gate 1, first clause (e-librows): the `design_block` row is taken (`kicadLibsRemoveEntry('design_block', ...)` then true, a second call false, `Device` false); the blocks KiCad's reader lists were not read (no listing reaches the page before i-librows). Second clause (e-block-save): passed. Glasgow's `io_buffer` sheet written to `/tmp/cc-blocksave/1/iobuf.kicad_block/iobuf.kicad_sch` (419,277 bytes) with `{"code":0,"lines":[]}`, no dialog, undo depth 0 and no `ev.edited`; the root answers "Design blocks with nested sheets are not supported.", an unknown path "No sheet has that path.", four malformed calls false. The third clause rides the first train build (RULINGS 4).
- Not read on build G: the island e2e of the sch, pcb and symedit frames (the island testers' run).
