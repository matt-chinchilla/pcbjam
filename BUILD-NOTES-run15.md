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

Seat branches `seat/run15-e1` to `seat/run15-e13` in both repositories and their worktrees under `/home/matthew/cc-engine-wt/` are cut from `run15/main` after the seams are reviewed (not yet).

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

- One build at a time machine wide (`/home/matthew/cc-engine/out/.build.lock`), launched only by E0, detached, from an island worktree on `w2/base` (it carries `occ_service`): `bash scripts/engine/build.sh cc8`, tree `/home/matthew/cc-engine/tree-cc8`, outputs `/home/matthew/cc-engine/out/cc8/`. Before launching: `check-run15-exports.sh` (with `--join` for the last build), the kicad gitlink on `run15/main` equal to kicad `run15/main`, no syntax check running.
- **The build script builds `cc/main`, not `run15/main`:** `cc-editor-island/scripts/engine/build.sh` line 60 on `w2/base` (56 on `phase4-selection`), `cc8)      ROOT_REF=cc/main;     KICAD_REF=cc/main;    WX_REF=cc/main; BUILD_DEPS=$FORCE_DEPS ;;`. That is the one line that would need to change for run 15 (its two refs to `run15/main`, wx staying on `cc/main`). Not changed, and `cc/main` not moved: the controller decides. Recommended instead of editing the `cc8` line: a `run15)` case with the same refs, so the tree is `tree-run15` and the outputs `out/run15/` and `out/cc8/` (run 14's served engine) is not deleted by the new run (`build.sh` drops the old outputs of the name it builds).
- The recipe now runs `kicad_editor,ngspice_service,occ_service`. `occ_service` has never been built in this tree: its first build configures and compiles its own KiCad tree (`build-wasm/kicad-occ_service`, the pcbnew kiface), time not measured. It links `pcbnew_embind.o` compiled WITHOUT `KICAD_MERGED_EMBIND` (the recipe's `EMBIND_APP=pcbnew`): a lane's board face that reaches a definition only the merged image holds must sit under `#ifdef KICAD_MERGED_EMBIND` (the syntax check's `occ` unit compiles that configuration; only the link proves it).

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
