<!-- Copyright (C) 2026 Circuit Center Inc -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Engine run 15, train 17, group A (items 1 and 2)

Worktrees `/home/matthew/cc-engine-wt/t17-a/pcbjam` and `/kicad`, branch `seat/t17-a` in both, cut from pcbjam 772f9c2 and kicad 3e13bf12. No export added, no name registered anew, no header changed, nothing reported to E0 beyond what T17.md already holds.

## Item 1. A write traps (SuspendError) while its item is selected, yet commits

The cause in T17.md held when read: both roads push a commit from inside a plain export, and the push dispatches `SelectedItemsModified` when a staged item is selected.

- pcbjam 9d180e3, `wasm/bindings/cc_e10_pcb.inc` (ours, no Modified line needed): `kicadBoardVerticesSet` reads and checks its args, the item and the corner at once (new helper `pcb::vertexAt`, shared by the check and the queued body). A corner already there answers the read back record at once with no commit. Otherwise the move is queued with `pcbjam_collab::runOnCoroutine( fr, ... )` and `{"queued":true}` is answered. The queued body re-checks (S4): no open in flight, `pcb::vertexFrame()` still `fr`, the item re-resolved by uuid, the corner still there and not already at the point; then Modify, SetVertex, the zone steps, Push "Edit Vertex", the view update. Both frames `vertexFrame()` serves take the same road.
- kicad 2c1214b2, the four table dialogs (each gains a dated "Modified by Circuit Center Inc on 2026-11-23" line): `eeschema/dialogs/dialog_table_properties.cpp`, `eeschema/dialogs/dialog_tablecell_properties.cpp`, `pcbnew/dialogs/dialog_table_properties.cpp`, `pcbnew/dialogs/dialog_tablecell_properties.cpp`. Inside the existing `#ifdef __EMSCRIPTEN__` block only. The local `RECEIVER::CcApply` writes the controls as before, then runs only the refusals `TransferDataFromWindow` begins with (tables: `m_grid->CommitPendingChanges()` then `wxDialog::TransferDataFromWindow()`; cells: `wxDialog::TransferDataFromWindow()`), sets `commitPending` (cells also keep `editTable`) and returns true. `ccAskHost` calls `TransferDataFromWindow()` (the commit) when `CcHostPromptModal` returned 1 with `commitPending` set, after the parked wait resumed, on the tool's coroutine; a refusal there (not expected: the same checks just passed) returns 0, the cancel. Cells set `m_returnValue = TABLECELL_PROPS_EDIT_TABLE` after that commit, since the commit sets `TABLECELL_PROPS_OK` at its end.

## Item 2. Compare with its source parks inside a plain export

- pcbjam 7062040, `wasm/bindings/cc_e10_dispatch.inc`: `function( "kicadCompareWithSource", &cc_e10::compareWithSource PCBJAM_PARKER_POLICY );` (the macro from `pcbjam_async_policy.h`, included by `kicad_editor_embind.cpp` before the inc). The export now answers a Promise; E0's seam 772f9c2 has the name in `PARKER_NAMES`.

## Checks

```
cd /home/matthew/cc-engine-wt/t17-a/pcbjam
/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/t17-a/kicad all \
    --kicad-file eeschema/dialogs/dialog_table_properties.cpp --kicad-file eeschema/dialogs/dialog_tablecell_properties.cpp \
    --kicad-file pcbnew/dialogs/dialog_table_properties.cpp --kicad-file pcbnew/dialogs/dialog_tablecell_properties.cpp
```
(the script takes one file per `--kicad-file`)

```
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (20491 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (19544 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (9386 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (13755 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (7983 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (8369 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (20466 ms)
PASS kicad eeschema/dialogs/dialog_table_properties.cpp (12598 ms)
PASS kicad eeschema/dialogs/dialog_tablecell_properties.cpp (8562 ms)
PASS kicad pcbnew/dialogs/dialog_table_properties.cpp (7814 ms)
PASS kicad pcbnew/dialogs/dialog_tablecell_properties.cpp (8248 ms)
syntax-check: 0 failed
```

`scripts/kicad/check-run15-exports.sh --since run15/main --kicad /home/matthew/cc-engine-wt/t17-a/kicad --kicad-since run15/main`: `check-run15-exports: 257 names registered in the merged image (250 kicad names), 112 of 117 run 15 names, 0 failures` (the five missing names are groups C and D's).

## What build T17 must probe

The probes of T17.md section 1, unchanged:
- Item 1, vertices: pcb frame of `canvas-kinds`, silk zone `cc0ca4d5-0000-4000-8000-000000000321` selected, `kicadBoardVerticesSet` with corner 1 at 132, 115 answers `{"queued":true}` with no throw and no SuspendError line; once `kicadCollabBusy` clears, `kicadBoardVerticesGet` reads 132, 115, undo depth 1, `ev.edited {depth:1}`; Ctrl+Z puts 130, 115 back at depth 0. The same unselected; the same corner sent again answers the record at once, depth unchanged. The footprint editor's `fpedit-small` polygon the same. Wrong if: a record comes back for a changed corner (old road), the corner never moves (queued body refused), or two undo steps.
- Item 1, tables: schematic frame, table `...12f` selected, the table ask answered with the 3x3 cells answers `{"accepted":true}` with no throw; then `ev.edited {depth:1}`, the prompt no longer parked, the cell reads SMOKE; one Ctrl+Z restores it. Cell `...134` `{"text":"R1"}` the same; `{"editTable":true}` is followed by the table ask. Board frame the same with the table and with a cell selected. A refused answer (cells of the wrong shape) answers `accepted:false` with `field`, commits nothing, and the prompt stays parked. Wrong if: a SuspendError line, no `ev.edited`, or the cell unchanged after `ev.edited`. A read right after the answer may still see the old cell (the commit runs once the wait resumes), so reads wait for `ev.edited`.
- Item 2: Glasgow sch frame `await Module.kicadCompareWithSource('{"uuid":"00000000-0000-0000-0000-00005f06d941","sheetPath":"/"}')` resolves a record, no throw; board R48 the same; island e2e `st-compare-i -g "run 15"` 3 passed. Wrong if: a SuspendError line, or a call that returns a string rather than a Promise (the policy missed).

## For the island and the site

- Island I1 (`bridge/ext/m3-i.ts` 102 to 126) must read `{"queued":true}`: wait for the apply queue, then read the corner back, `not_applied` when it is not there.
- Island `m12-tables` e2e must read the table after `ev.edited`, never right after the answer (the commit now runs after the parked wait resumes).
- Island compare op must `await` `kicadCompareWithSource` (it is now a Promise).
- No site change for these items.
