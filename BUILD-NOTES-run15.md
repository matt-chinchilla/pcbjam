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

(none yet)

## Builds

(none yet)
