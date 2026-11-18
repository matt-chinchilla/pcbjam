<!-- Copyright (C) 2026 Circuit Center Inc -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Engine run 15, lane e1: menus, holds, prompts and tables

Branches: pcbjam `seat/run15-e1` (`/home/matthew/cc-engine-wt/e1/pcbjam`), kicad `seat/run15-e1` (`/home/matthew/cc-engine-wt/e1/kicad`). Every KiCad change sits under `#ifdef __EMSCRIPTEN__`; a plain build compiles what it compiled before.

## Early header (kicad d0055781)

`include/host_prompt.h` alone, header only, included by no TU at that commit. It holds:

- `CC_PROMPT_RECEIVER` (virtual `CcApply( answer, field& ) -> bool`, `CcPick( kind ) -> bool` default false, `CcCancel()` default nothing): what a hooked dialog gives the parked state so an answer reaches its own controls and `TransferDataFromWindow` while it is parked.
- `CcHostPromptModal( ask, record, CC_PROMPT_RECEIVER& ) -> int` (1 answered: return wxID_OK, 0 canceled, -1 not taken: show KiCad's dialog) and the spec's `CcHostPromptModal( ask, record, json& aAnswer )` (the whole answer handed back, accepted as is: for a dialog with nothing to refuse).
- `CcHostPromptModeless( ask, record, CC_PROMPT_RECEIVER* ) -> token` (0 not taken), `CcHostPromptUpdate( token, record )` (onUpdate), `CcHostPromptEnd( token, reason )` (onEnd, `canceled` or `gone`; a parked modal prompt resumes canceled), `CcHostPromptForget( receiver )` (a receiver's destructor ends its prompts `gone`).
- The parked state `CcParkedPrompts()` (by token), `CcHostPromptModalParked()` (busy probe), the message capture `CcPromptCapturing()`, `CcPromptCaptureMessage( text )` (first sentence wins), and the export bodies `CcHostPromptAnswer`, `CcHostPromptCancel`, `CcHostPromptPick`.
- The wait: `wxWasmBeginWait( "prompt" )` begun BEFORE the ask (an answer racing ahead pre-resolves it), `CcHostWait` zeroes `wxWasmDispatchDepth` for the span and restores it through `wxWasmDispatchRestore`, exactly as `wxDialog::ShowModal` does on the port (`src/wasm/dialog.cpp`). The C declarations of `yieldwait.h` and the two of `dispatch.h` are repeated in the header (no wx private header included from KiCad).
- Listener calls pass JSON TEXT, as every host ask since run 12 (`window.kicadHostAction.onRequest( text )`): `window.kicadHostPrompt.onAsk( text )` with `{ token, ask, modal, record }`, `onUpdate( text )` with `{ token, record }`, `onEnd( text )` with `{ token, reason }`. The island parses it. A throwing listener is caught (never unwinds the wasm frame).
- Answers are applied inside `kicadPromptAnswer` itself (the dialog is parked in its ShowModal, its controls alive), so the export answers accepted or the refusal synchronously, the refusal carrying the first message KiCad raised while applying (m1x-e2's hooks capture it).

## m1x-e1: host prompts, the entry dialogs, the one layer chooser

Exports (pcbjam `wasm/bindings/cc_e1_dispatch.inc`, block `cc_e1_dispatch`, both images' shared dispatcher TU, NOT merged only):

- `kicadHostPrompts() -> bool` marker, true.
- `kicadPromptAnswer( int token, string json ) -> string`: `{"accepted":true}`, `{"accepted":false,"field":"<key or empty>","message":"<KiCad's sentence or empty>"}` (stays parked), `{"stale":true}`; `""` when the text is not a JSON object.
- `kicadPromptCancel( int token ) -> int`: 0 canceled (a modal ShowModal returns wxID_CANCEL; a modeless receiver's CcCancel runs), 1 stale.
- `kicadPromptPick( int token, string kind ) -> int`: 0 the picker runs, 1 stale, 2 this ask has no picker of that kind (or the kind is not `item` or `point`). Only e2's modeless receivers (m6-e2) answer it.

Host asks (kicad, commit 202d23b0):

| ask | class | file | record |
|---|---|---|---|
| `entry` | `WX_UNIT_ENTRY_DIALOG::ShowModal` | `common/dialogs/dialog_unit_entry.cpp` (end of file) | `{ title, fields: [{ key: "value", label, kind: "length", value: mm, tip }] }` |
| `entry` | `WX_PT_ENTRY_DIALOG::ShowModal` | same file | `{ title, fields: [{ key: "point", label: "<X label>, <Y label>", kind: "point", value: { x, y } mm, tip }] }`, the frame's own coordinates (`GetValue`, the user origin the fields show undone) |
| `entry` | `WX_TEXT_ENTRY_DIALOG::ShowModal` | `common/dialogs/dialog_text_entry.cpp` | `{ title, fields: [{ key: "value", label (empty when KiCad hides it), kind: "text", value, tip }] }` |
| `entry` | `WX_MULTI_ENTRY_DIALOG::ShowModal` | `common/dialogs/dialog_multi_unit_entry.cpp` | `{ title, fields: [{ key: "f0".., label, kind: "length" or "check", value, tip }] }` in the ENTRY order |
| `layer` | `PCB_BASE_FRAME::SelectOneLayer` (before PCB_ONE_LAYER_SELECTOR is built) | `pcbnew/sel_layer.cpp` | `{ current: canonical or "", layers: [{ canonical, name, color: "#rrggbbaa" }] }`, the board's enabled layers in UI order minus the not allowed mask |

Answers: entry `{ values: { <key>: number mm | string | boolean | { x, y } } }` (a missing key keeps the dialog's value; a wrong type refuses with that key); layer `{ layer: canonical }` (a layer not in the list refuses, field `layer`). Accepted: the values go into the dialog's own controls (UNIT_BINDER::SetValue in internal units, so KiCad's own rounding and GetValue apply), then `Validate() && TransferDataFromWindow()`; the text dialog asks the caller's `wxTextValidator::IsValid` for its sentence (never its message box). The dialog's ShowModal then returns wxID_OK (or wxID_CANCEL on cancel); not taken: KiCad's own ShowModal as before. A dialog whose parent is not a UNITS_PROVIDER (no unit scale) shows KiCad's dialog.

Header changes: `include/dialogs/dialog_unit_entry.h`, `dialog_text_entry.h`, `dialog_multi_unit_entry.h` gain a `ShowModal() override` under `__EMSCRIPTEN__` (recompile reach: the few dialogs and tools that include these three headers; no widely included header).

Syntax check (from `/home/matthew/cc-engine-wt/e1/pcbjam`): `/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e1/kicad kicad_editor --kicad-file common/dialogs/dialog_unit_entry.cpp --kicad-file common/dialogs/dialog_text_entry.cpp --kicad-file common/dialogs/dialog_multi_unit_entry.cpp --kicad-file pcbnew/sel_layer.cpp`

```
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (8576 ms)
PASS kicad common/dialogs/dialog_unit_entry.cpp (6155 ms)
PASS kicad common/dialogs/dialog_text_entry.cpp (3854 ms)
PASS kicad common/dialogs/dialog_multi_unit_entry.cpp (4620 ms)
PASS kicad pcbnew/sel_layer.cpp (8300 ms)
syntax-check: 0 failed
```

What the build train must probe:

- `Module.kicadHostPrompts()` true.
- Board frame, canvas-kinds fixture, two tracks at a 90 degree corner selected, `pcbnew.InteractiveEdit.filletTracks` (run through `kicadRunAction` or the menu): with a listener `window.kicadHostPrompt = { onAsk: t => (window.__ask = JSON.parse(t), true) }`, `__ask.ask === "entry"`, `__ask.record.title === "Fillet Tracks"`, `fields[0]` `{ key: "value", kind: "length" }`; `kicadPromptAnswer(__ask.token, '{"values":{"value":1}}')` returns `{"accepted":true}`, the corner rounds, undo depth +1, one Undo restores it. A second answer to the same token returns `{"stale":true}`. Wrong: the dialog shows (onAsk never called), a hang (the wait never resolves: the export ran but ShowModal never resumed), or two undo steps.
- The same with `kicadPromptCancel(token)`: returns 0, nothing changes, undo depth unchanged; again: 1.
- `{"values":{"value":"x"}}`: `{"accepted":false,"field":"value","message":""}` and the prompt stays parked (a following good answer is accepted).
- Routing a track, `pcbnew.InteractiveRouter.SelLayerAndPlaceVia` (`<`): ask `layer`, `record.layers` copper only (the not allowed mask applied), every `canonical` like `F.Cu`; answer `{"layer":"B.Cu"}` places the via on B.Cu; `{"layer":"F.SilkS"}` refuses with field `layer`.
- No listener (`delete window.kicadHostPrompt`): KiCad's dialog shows as before (plain behavior unchanged).

## st-hold-e: K-HOLD, menus, toolbars and the language list ask as keys do

Export (pcbjam `cc_e1_dispatch.inc`, block `cc_e1_dispatch`, not merged only): `kicadHeldMenus() -> bool` marker, true (the island's cap `heldMenus`).

Host asks, all through run 12's `hostHoldsAction( std::string )` (`window.kicadHostAction.onRequest`, unchanged), kicad commit 290cfa8c:

- `common/tool/action_menu.cpp` `ACTION_MENU::OnMenuEvent`: just before `toolMgr->ProcessEvent( *evt )`, for a `wxEVT_COMMAND_MENU_SELECTED` whose id is an action UI id, the pressed `TOOL_ACTION` is found in this menu and its submenus (the same walk as `findToolAction`) and asked by name; held: the press is dropped (return). Menu bar, submenus and KiCad's own context menus alike (a context menu still opens: no change to `DispatchContextMenu` in this unit).
- `common/tool/action_toolbar.cpp` `ACTION_TOOLBAR::onToolEvent`: the cycling group press (asks for the NEXT action; held: handled, the group keeps its action) and the plain action press (held: handled, nothing dispatched); `onPaletteEvent`: held hides the palette, nothing dispatched. The cancel press (`CancelTool`) is not an action and is not asked.
- `common/eda_base_frame.cpp` `EDA_BASE_FRAME::OnLanguageSelectionEvent`: asks `cc.Control.setLanguage` (a name of ours); held: nothing changes.

Syntax check: `/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e1/kicad kicad_editor --kicad-file common/tool/action_menu.cpp --kicad-file common/tool/action_toolbar.cpp --kicad-file common/eda_base_frame.cpp`

```
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (8648 ms)
PASS kicad common/tool/action_menu.cpp (6547 ms)
PASS kicad common/tool/action_toolbar.cpp (5616 ms)
PASS kicad common/eda_base_frame.cpp (7930 ms)
syntax-check: 0 failed
```

What the build train must probe:

- `Module.kicadHeldMenus()` true.
- With `window.kicadHostAction = { onRequest: t => { const a = JSON.parse(t).action; console.log('held?', a); return a === 'pcbnew.DRCTool.runDRC'; } }` in the Board frame with KiCad's menus shown: Inspect > Design Rules Checker from the menu bar logs `held? pcbnew.DRCTool.runDRC` and NO DRC dialog opens; the DRC toolbar button the same. Wrong: the dialog opens (the hook missed the path), or an unheld item (View > Zoom to Fit) stops working (the hold dropped every press).
- A listener returning true for `cc.Control.setLanguage`: Preferences > Set Language > Deutsch leaves the menus English; no listener: the language changes as before.
- A right click still shows KiCad's context menu; choosing an item the listener holds (`pcbnew.InteractiveEdit.properties`) logs it and runs nothing; another item runs.
- A cycling toolbar group (the units toggle): held, the units stay; not held, they cycle as before.
