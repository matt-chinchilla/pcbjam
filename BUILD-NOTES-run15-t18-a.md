<!-- Copyright (C) 2026 Circuit Center Inc -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Engine run 15, train 18, group A (items 14 and 4)

## Item 14 (t18-14): kicadActionStates, and kicadActionStateWhy

Cause: not found for certain by reading. Read again: the frames register in their constructors (`setupTools` then `setupUIConditions`; the board's toggleGrid at `pcbnew/pcb_edit_frame.cpp` 1084, the schematic's at `eeschema/sch_edit_frame.cpp` 803), `SetConditions` and `GetCondition` key the same `std::map<int, ACTION_CONDITIONS>` on `TOOL_ACTION::GetId()`, ids are given once by the first `ACTION_MANAGER` (`m_id == -1`), and every tool manager is made once per frame (`setupTools`); no road clears the map. The record layouts were already ruled out by E0. One cause reading cannot rule out is a second `TOOL_ACTION` object carrying the same name (the name index keeps the last registered, `SetConditions` keys the one the frame's code names): the new record counts it (`twins`, `twinCondition`), so T18's probe decides.

- kicad: new `include/tool/cc_action_state.h` (`CC_ACTION_CHECK`, `CcActionCheck`), definition in `common/tool/action_manager.cpp` under `__EMSCRIPTEN__`. Road (a): `FindAction`, `GetCondition`, `checkSet`, the `ShowNever` test, `checkCondition( holder->GetCurrentSelection() )` in try. Road (b): `wxUpdateUIEvent( GetUIId() )` processed by the frame (`EDA_BASE_FRAME::onUpdateUI`), `GetSetChecked() ? GetChecked() : -1`. Diagnostics: `conditions` (actions of this manager with a condition), `twins` (actions of KiCad's list with this name), `twinCondition` (a twin other than the found one has one).
- pcbjam `cc_e1_dispatch.inc`: `kicadActionStates` answers `value` when 0 or 1, else `uiValue` when 0 or 1, else null; not found or `showNever` null. Shape unchanged: `{"states":{"<name>":true|false|null}}`.
- New export `kicadActionStateWhy( name )`: `{"found","id","uiId","condition","checkSet","showNever","value","threw","uiValue","conditions","twins","twinCondition"}`; "" with no frame or an open in flight.
- Note: road (b) is only used when road (a) gives no value. When the map misses an action that is no toggle but has an enable condition, KiCad's handler checks it false, so a plain command could read false there. On a working map (a) answers first and `showNever` keeps it null.

Probe on T18: Glasgow board `kicadActionStateWhy("common.Control.toggleGrid")` then the plan's probe. Reading the record: `conditions` 0 means the map read is empty (wrong manager); `conditions` > 0 with `condition` false and `twins` > 1, `twinCondition` true means a twin action; `condition` true with `value` -1 and `threw` false means the check condition read as empty or ShowNever.

## Item 4 (t18-4): the focus after a held prompt

- pcbjam `cc_e1_dispatch.inc` only: `kicadPromptAnswer` (accepted) and `kicadPromptCancel` (0) post one `CallAfter` on the top `KIWAY_PLAYER`. Its try waits while a modal prompt is parked, a modal dialog is up or a SHOWN blocking dialog exists (gives up at the last try), and while the ended prompt's own dialog is still alive (the Kiway's blocking dialog, constructor to destructor; at the last try it goes on). Then, when `FindFocus()` is null or outside the frame, `GetToolCanvas()->SetFocus()`.
- Later tries run on a one-shot `wxTimer`, 20 ms apart, at most 50 (1 s), not on a CallAfter posted again: wx drains pending events until the list is empty (`wxAppConsoleBase::ProcessPendingEvents`), so a re-posted CallAfter runs 50 times in one drain, before the scheduler wakes the modal wait (`wxWasmResolveWait` resolves on a later tick).
- No change to `include/host_prompt.h`.

## Checks

- `gate.sh scripts/kicad/syntax-check-run15.sh --kicad .../t18-a/kicad all --kicad-file common/tool/action_manager.cpp`: every unit PASS, `syntax-check: 0 failed`.
- `scripts/kicad/check-run15-exports.sh --since run15/main --kicad .../t18-a/kicad --kicad-since run15/main`: `118 of 118 run 15 names, 0 failures`.
