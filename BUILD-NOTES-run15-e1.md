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

## m1-e: K-MENU, our context menu (canvas addendum 3.1)

Exports (pcbjam `cc_e1_dispatch.inc`, block `cc_e1_dispatch`, commit b52f7f7; not merged only, no pcbnew or eeschema header):

- `kicadHostContextMenu() -> bool` marker, true.
- `kicadContextMenuChoose( int token, int id ) -> int`: 0 done (`-1` cancels), 1 stale, 2 no item with that id, 3 disabled, 4 a submenu or separator.
- `kicadContextMenuHighlight( int token, int id ) -> int`: the same codes; 0 posts a `wxEVT_MENU_HIGHLIGHT` for the item to its containing `ACTION_MENU` through the frame's `CallAfter` (delivered by the tick while the menu is parked, as a wx event is; dropped if the menu closed first).
- `kicadRunAction( string name ) -> int`: 0 dispatched (on its own coroutine through `pcbjam_collab::runOnCoroutine`, so an action that opens a window may suspend), 1 `ACTION_MANAGER::FindAction` null (nothing run), 3 busy (a parked host menu, a parked modal prompt, KiCad's own popup `IsContextMenuActive`, a modal `wxDialog`, the Kiway blocking dialog, an open in flight), 4 no frame or tool manager. Never asks `hostHoldsAction`.

Host ask (kicad commit 87580fb2): `window.kicadHostMenu.onMenu( text )`, text the JSON `{ token, clarify, tool, title, at: { x, y }, kinds, items, cut? }`; each item `{ id, kind (normal, check, radio, submenu, separator), label, enabled, checked, action, key, items? }`. Bounds 400 items, depth 3, label 128 characters, `cut: true` past them.

- `include/tool/host_context_menu.h` (new, header only): `CcHostTakesMenu( TOOL_MANAGER&, ACTION_MENU&, wxWindow* frame, const TOOL_BASE* tool, bool clarify, const VECTOR2D& cursor )` (two parameters more than the spec's line: the frame and the owning tool, which `DispatchContextMenu` holds), the parked menu `CcParkedMenu()`, `CcHostMenuParked`, `CcHostMenuCancelParked`, `CcHostMenuChoose`, `CcHostMenuHighlight`, the kinds table `CcMenuKindOf` (a board rule area told from a zone by `GetFriendlyName() == _( "Rule Area" )`; a schematic graphic line reads `shape`; drawing sheet `line`, `rect`, `text`, `image`). With no `window.kicadHostMenu.onMenu` the hook returns before touching anything (plain popup path unchanged). Taken: `SetInvokingWindow`, `UpdateUI()`, `wxEVT_MENU_OPEN` to the menu and every `ACTION_MENU` submenu through `OnMenuEvent`, the model, `wxWasmBeginWait( "menu" )` before the ask, `CcHostWait` (host_prompt.h), then the replay of `DoPopupMenu` (FindItem, Toggle a checkable, `containing->SendEvent`).
- `common/tool/tool_manager.cpp` `TOOL_MANAGER::DispatchContextMenu`: at the top, a right click while a host menu is parked cancels it and opens nothing (the tick keeps dispatching during the wait; this keeps the tool state of the parked menu whole); `clarify` read before `CMENU_NOW` is cleared; `frame->PopupMenu` only when `CcHostTakesMenu` returns false.
- `include/tool/action_menu.h`: `const TOOL_ACTION* ActionFor( int aId ) const` inline under `__EMSCRIPTEN__` (this menu's own map; the serializer asks each submenu for its own items). Header change: recompile reach is every file including `action_menu.h` (most tools: about the same reach as a tool header; the train takes it with the others). `action_menu.cpp` (st-hold-e's) is not touched by this unit.

Syntax check: `/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e1/kicad kicad_editor --kicad-file common/tool/tool_manager.cpp --kicad-file common/kidialog.cpp --kicad-file common/confirm.cpp --kicad-file common/dialogs/dialog_assign_netclass.cpp`

```
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (5923 ms)
PASS kicad common/tool/tool_manager.cpp (5654 ms)
PASS kicad common/kidialog.cpp (3243 ms)
PASS kicad common/confirm.cpp (2059 ms)
PASS kicad common/dialogs/dialog_assign_netclass.cpp (4054 ms)
syntax-check: 0 failed
```

What the build train must probe (canvas-kinds fixture, island frame console):

- `Module.kicadHostContextMenu()` true.
- Schematic, `window.kicadHostMenu = { onMenu: t => (window.__m = JSON.parse(t), true) }`, right click R4: `__m.clarify === false`, `__m.tool` `eeschema.InteractiveSelection`, `__m.kinds` `["symbol"]`, an item with `action` `eeschema.InteractiveEdit.rotateCCW`, `enabled` true, `key` `R`; NO KiCad popup (`.wx-menu-popup` absent). `kicadContextMenuChoose(__m.token, <that id>)` 0: R4 turns, undo depth +1; Undo turns it back. Again: 1 (stale). Wrong: the DOM popup shows anyway, a hang after the choice (the wait never resolved), the choice running nothing (the replay missed `SendEvent`).
- Right click, `kicadContextMenuChoose(token, -1)`: 0, nothing changes, the next right click opens a new model. A right click while a model is parked: no new model, the first is canceled (a choose on it: 1).
- A separator id: 4; a disabled item: 3; `id` 999999: 2.
- `Module.kicadRunAction('eeschema.InteractiveEdit.mirrorV')` with R4 selected: 0, R4 mirrors; `kicadRunAction('no.Such.action')`: 1, nothing changes; while a model is parked: 3.
- Board, two stacked tracks at a crossing: a model with `clarify` true, `title` KiCad's ("Clarify Selection"), one `normal` item per candidate with `action` ""; `kicadContextMenuHighlight(token, id2)` 0 and the second track brightens on the next frame; choosing it selects it (one selected item).
- No listener (`delete window.kicadHostMenu`): KiCad's DOM popup shows exactly as on run 14.

## m1x-e2: messages, confirmations and Assign Netclass ask the host (canvas addendum 4.2)

No export (rides `kicadHostPrompts`, `kicadPromptAnswer`, `kicadPromptCancel` of m1x-e1). kicad commit 9140d111:

| ask | where | record | answer |
|---|---|---|---|
| `message` | `KIDIALOG::ShowModal` (`common/kidialog.cpp`, after KiCad's own do-not-show-again check), `DisplayError`, `DisplayErrorMessage`, `DisplayInfoMessage`, `IsOK`, `OKOrCancelDialog` (`common/confirm.cpp`) | `{ title, text, extra, icon (info, warning, error, question from GetEffectiveIcon), buttons: [{ id, label }], dontAsk }`; buttons yes, no or ok, then cancel, labels KiCad's with `&` stripped | `{ button, dontAsk }`; a button not offered refuses with field `button`, a non boolean `dontAsk` with field `dontAsk` |
| `assignNetclass` | `DIALOG_ASSIGN_NETCLASS::ShowModal` (`common/dialogs/dialog_assign_netclass.cpp`, declared in `include/dialogs/dialog_assign_netclass.h`) | `{ pattern, netclass, netclasses }` from its own `TransferDataToWindow` | `{ pattern, netclass }` (either may be left out: the dialog's value stays); a netclass not in the list refuses with field `netclass`; accepted runs its own `TransferDataFromWindow` (the pattern assignment) |

- The one body is `int CcHostMessage( const wxMessageDialogBase&, bool offerDontAsk, bool* dontAsk )` defined in `kidialog.cpp` (kicommon) and declared locally in `confirm.cpp` (no header change). It returns the pressed button's wx id, the dismissal id (Cancel, else No, else OK) on the host's cancel, or -1 (KiCad's dialog).
- While a prompt answer is applied (`CcPromptCapturing`), a message box is never asked: its text (plus the extended text) becomes that answer's refusal `message` and it returns the dismissal id.
- `dontAsk` is offered only by a KIDIALOG with KiCad's "Do not show again" box (`DoNotShowCheckbox`); a true answer checks the box so KiCad stores the choice as before. `OKOrCancelDialog`'s "Apply to all" keeps KiCad's own default (checked).
- Not hooked (file flows, as the spec says): `UnsavedChangesDialog`, `AskOverrideLock`, `ConfirmRevertDialog`, `SelectSingleOption`, `HandleUnsavedChanges`.
- Header change: `dialog_assign_netclass.h` gains `ShowModal() override` (reach: its own .cpp and the two tools that build it, `sch_editor_control.cpp`, `board_editor_control.cpp`). `kidialog.h` is NOT changed.

Syntax check: the same run as m1-e above (kidialog.cpp, confirm.cpp, dialog_assign_netclass.cpp PASS).

What the build train must probe:

- Board, a locked track selected, D (drag), with `window.kicadHostPrompt = { onAsk: t => (window.__a = JSON.parse(t), true) }`: `__a.ask === "message"`, `__a.record.icon` `warning` or `question`, buttons in KiCad's order with its labels (for example `Drag Anyway`); `kicadPromptAnswer(__a.token, '{"button":"ok"}')` `{"accepted":true}` and the drag starts; `kicadPromptCancel` leaves the track. `{"button":"maybe"}`: `{"accepted":false,"field":"button","message":""}`, still parked.
- A message raised while an entry answer is applied (an entry the dialog refuses with a message box): the entry's refusal carries that sentence and no `message` ask arrives.
- Schematic, right click a wire, Assign a net class: `ask` `assignNetclass`, `record.pattern` the wire's net, `record.netclasses[0]` `Default`; `{"pattern":"<net>","netclass":"Default"}` accepted, Schematic Setup's net class assignments list the pattern; `{"netclass":"Nope"}` refuses with field `netclass`. Wrong: the netclass list doubled after a not taken ask (no listener: KiCad's dialog must list each class once).
- No listener: every message box and the netclass dialog show as before.

## Relay point (after m1x-e2)

Both worktrees merged `run15/main` (pcbjam c7e3948, kicad 67c9069b) and clean. From `/home/matthew/cc-engine-wt/e1/pcbjam`:

`/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e1/kicad all --kicad-file common/tool/tool_manager.cpp --kicad-file common/kidialog.cpp --kicad-file common/confirm.cpp --kicad-file common/dialogs/dialog_assign_netclass.cpp`

```
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (15500 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (15135 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (8679 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (8253 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (1289 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (5967 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (13647 ms)
PASS kicad common/tool/tool_manager.cpp (6352 ms)
PASS kicad common/kidialog.cpp (3455 ms)
PASS kicad common/confirm.cpp (2408 ms)
PASS kicad common/dialogs/dialog_assign_netclass.cpp (4645 ms)
syntax-check: 0 failed
```

`scripts/kicad/check-run15-exports.sh --since run15/main --kicad /home/matthew/cc-engine-wt/e1/kicad --kicad-since run15/main`: `163 names registered in the merged image (156 kicad names), 18 of 110 run 15 names, 0 failures`.

Units left for the relay seat, in order: m12-e, st-keys-e, m6-e3. Read for m12-e so far:

- The CSV builder is NOT in `include/tool/edit_table_tool_base.h`: it is written twice, inside `SCH_EDIT_TABLE_TOOL::ExportTableToCSV` (`eeschema/tools/sch_edit_table_tool.cpp`, from the `wxFileDialog` at about 119; cell text `GetShownText( nullptr, &currentSheet, false, 0 )`) and `PCB_EDIT_TABLE_TOOL::ExportTableToCSV` (`pcbnew/tools/pcb_edit_table_tool.cpp` about 162; `GetShownText( false, 0 )`), each with the same `escapeCSV` lambda (quote a field holding a comma, quote or newline, quotes doubled), fields joined by `,`, rows ended by `\n`. Neither tool .cpp is in this lane's files (no author holds them, `mq.py grep` finds none). The plan: lift the builder into `edit_table_tool_base.h` as a free template under `__EMSCRIPTEN__` (the one builder `kicadTableCsv` calls, given the cell text reader), and REPORT to E0 that making KiCad's two actions call it needs those two tool files (or accept that the action's own copy stays, the action being held by the island under cap `tableCsv`).
- `kicadTableCsv` is an export of both frames: declare `tableCsvPcb` and `tableCsvSch` in `cc_e1_dispatch.inc`, define them in `cc_e1_pcb.inc` and `cc_e1_sch.inc` (record `{ name, text }` for `{ uuid }`).
- The schematic table dialog (`eeschema/dialogs/dialog_table_properties.cpp`) fills the grid in `TransferDataToWindow` (covered cells get a color string), reads it back in `TransferDataFromWindow` with `m_grid->CommitPendingChanges()` first and `ConvertRefsToKIIDs`; strokes through `m_borderCheckbox`, `m_headerBorder`, `m_borderWidth`, `m_borderColorSwatch`, `m_borderStyleCombo` (index into `lineTypeNames`), the same for separators. Per the manifest note, each of the four dialogs overrides both `ShowModal` and `ShowQuasiModal` under `__EMSCRIPTEN__` (the board shows them quasi modal); copy the m1x-e1 receiver pattern (`dialog_text_entry.cpp`).

## m12-e: the table and table cell dialogs ask the host; kicadTableCsv

Export (pcbjam, commit a974bb3): `kicadTableCsv( string json ) -> string`, registered in `cc_e1_dispatch.inc` block `cc_e1_dispatch` (dispatcher TU, NOT merged image only); the halves `cc_e1::tableCsvPcb` (`cc_e1_pcb.inc`, compiled in occ too, calls nothing merged only) and `cc_e1::tableCsvSch` (`cc_e1_sch.inc`). Args `{ uuid }` exactly (a table or any of its cells; the schematic on any sheet through `SCHEMATIC::ResolveItem` with the sheet path, the board through `BOARD::ResolveItem`). Answer `{ name, text }`: `text` built by `CcTableCsvText` (kicad `include/tool/edit_table_tool_base.h`, the action's own escaping and joins, cell text `GetShownText( false, 0 )` on the board, `GetShownText( nullptr, &path, false, 0 )` on the schematic), `name` the project name plus `-table` (`table` when the project has none); the page saves `<name>.csv`. `""` for any other args shape, a missing frame, an open in flight, a modal dialog or a parked prompt, the footprint, symbol or drawing sheet editor, a uuid that is no table. A read: no commit, no record file.

Host asks (kicad), each dialog overriding `ShowModal() override` and hiding `ShowQuasiModal()` (not virtual in DIALOG_SHIM; every call site names the concrete class, so the hidden one is the one called), both through `ccAskHost()` (1 answered `wxID_OK`, 0 canceled `wxID_CANCEL`, -1 KiCad's own dialog):

| ask | class | file (commit) | record |
|---|---|---|---|
| `table` | `DIALOG_TABLE_PROPERTIES` (schematic) | `eeschema/dialogs/dialog_table_properties.cpp` end of file (5856b7e4) | `{ uuid, rows, cols, cells, border: { external, header, width, color, style }, separators: { rows, cols, width, color, style }, layer: "", locked: false }` |
| `table` | `DIALOG_TABLE_PROPERTIES` (board) | `pcbnew/dialogs/dialog_table_properties.cpp` end of file (5856b7e4) | the same, `color` always `""`, `layer` canonical, `locked` the dialog's switch |
| `tableCell` | `DIALOG_TABLECELL_PROPERTIES` (schematic) | `eeschema/dialogs/dialog_tablecell_properties.cpp` end of file (0224c549) | `{ uuids, text, font, bold, italic, hAlign, vAlign, margins: { left, right, top, bottom }, size, width: null, height: null, thickness: null, autoThickness: null, textColor, fill: { mode, color } }` |
| `tableCell` | `DIALOG_TABLECELL_PROPERTIES` (board) | `pcbnew/dialogs/dialog_tablecell_properties.cpp` end of file (0224c549) | the same keys, `size`, `textColor`, `fill` null, `width`, `height`, `thickness`, `autoThickness` set |

Records are read from the dialog's own controls after its `TransferDataToWindow`: `cells` the grid row by row as it shows them (a board table on a back layer mirrored, as KiCad's grid is; text variable references in their readable form), a covered cell null; widths in mm, null when the stroke has none (width < 0); colors `#rrggbbaa` lowercase or `""`; style `solid`, `dash`, `dot`, `dashdot`, `dashdotdot` (`lineTypeNames` order). Cell: a value null where the selected cells differ (KiCad's mixed state); `text` null for several schematic cells (always), for several board cells only when they differ; `font` the font list's text (`KiCad Font`, a system font name), null when mixed; `hAlign` `left`, `center`, `right`; `vAlign` `top`, `center`, `bottom`; `fill.mode` `none` or `color`; `bold` and `italic` booleans on the board (its buttons have no mixed state).

Answers (`kicadPromptAnswer`): table `{ cells, border, separators, layer, locked }`, cell `{ text, font, bold, italic, hAlign, vAlign, margins, size, width, height, thickness, autoThickness, textColor, fill, editTable }`. A missing key or null keeps the value; the whole answer is read before anything is written; refusals name the key (`cells`, `border.width`, `separators.style`, `layer`, `locked`, `margins.left`, `fill.color`, ...): wrong type, a cells array not rows by cols, a negative width, a color not `""`, `#rrggbb` or `#rrggbbaa`, a style not in the list, a board color not `""`, a schematic `layer` not `""` or `locked` true, a board layer the dialog's selector does not offer, a lock change in the footprint editor (the switch is hidden there), a font the font list lacks, a schematic `width`/`height`/`thickness`/`autoThickness` or a board `size`/`textColor`/`fill` that is not null, a zero text size. Then the values go into the dialog's own controls (the grid's open cell editor is committed quietly FIRST, so the editor KiCad's `CallAfter` opens on the selected cell cannot write its text back over the answer; the switches run the dialog's own `onBorderChecked`, so a stroke switched on with no width takes KiCad's default; board cells run `onBoldToggle` when bold changes and `onTextSize` under automatic thickness) and its own `TransferDataFromWindow` commits (one undo step: "Edit Table", "Edit Table Cell Properties"). `editTable: true` sets `TABLECELL_PROPS_EDIT_TABLE` after a successful apply, so the caller (`sch_edit_tool.cpp` 2732, `pcbnew/tools/edit_tool.cpp` 2442) then builds the table dialog, which asks `table`. Schematic, several cells, no `text`: a new member `m_ccKeepText` keeps each cell's own text (KiCad's dialog would write the shown text into every cell); the only change inside a KiCad function body is that guard around `cell->SetText( text )` in `TransferDataFromWindow`.

CSV builder: `CcTableCsvText( T_TABLE*, cellText )`, a free template in `include/tool/edit_table_tool_base.h` (6d627108) under `__EMSCRIPTEN__`. REPORTED to E0: KiCad's two actions (`SCH_EDIT_TABLE_TOOL::ExportTableToCSV`, `PCB_EDIT_TABLE_TOOL::ExportTableToCSV`) keep their own copy of the builder, because `eeschema/tools/sch_edit_table_tool.cpp` and `pcbnew/tools/pcb_edit_table_tool.cpp` belong to no lane; making them call `CcTableCsvText` needs those two files given to a lane. The island holds the action (cap `tableCsv`), so the browser path never runs the copy.

Header changes and reach: the four dialog headers (each included by its own .cpp and the few tools that build the dialog: `sch_edit_tool.cpp`, `sch_drawing_tools.cpp`, `sch_edit_table_tool.cpp`, `pcb_edit_frame.cpp`, `edit.cpp`, `footprint_editor_utils.cpp`, `pcb_edit_table_tool.cpp`, `edit_tool.cpp`); `edit_table_tool_base.h` (included by the two table tool headers and their includers, plus `cc_e1_pcb.inc`, `cc_e1_sch.inc`).

Syntax check (from this worktree): `/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e1/kicad dsedit --kicad-file eeschema/dialogs/dialog_tablecell_properties.cpp --kicad-file pcbnew/dialogs/dialog_tablecell_properties.cpp --kicad-file eeschema/tools/sch_edit_table_tool.cpp --kicad-file pcbnew/tools/pcb_edit_table_tool.cpp`:

```
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (1382 ms)
PASS kicad eeschema/dialogs/dialog_tablecell_properties.cpp (9724 ms)
PASS kicad pcbnew/dialogs/dialog_tablecell_properties.cpp (7125 ms)
PASS kicad eeschema/tools/sch_edit_table_tool.cpp (8193 ms)
PASS kicad pcbnew/tools/pcb_edit_table_tool.cpp (7650 ms)
syntax-check: 0 failed
```

and before it the two table dialogs (`PASS kicad eeschema/dialogs/dialog_table_properties.cpp`, `PASS kicad pcbnew/dialogs/dialog_table_properties.cpp`, 0 failed); `all` after the pcbjam commit: 0 failed (the full lines are in the handover section). Export check: `164 names registered in the merged image (157 kicad names), 19 of 110 run 15 names, 0 failures`.

What the build train must probe (fixture `island:tests/fixtures/canvas-kinds/`, with `window.kicadHostPrompt = { onAsk: t => (window.__a = JSON.parse(t), true) }`):

- Schematic, double click a cell of the 3 by 3 table: `__a.ask === "tableCell"`, `record.uuids.length === 1`, `record.text` the cell's text, `record.width === null`, `record.size` a number (mm). `kicadPromptAnswer(__a.token, '{"text":"R1","bold":true}')` gives `{"accepted":true}`; the cell reads R1 in bold; ONE undo step restores it. Wrong: two undo steps, or the text unchanged (the cell editor's write back).
- The same with `{"editTable":true}`: accepted, then a second ask `table` arrives (`record.rows === 3`, `record.cells[0].length === 3`, `record.layer === ""`). `{"border":{"header":false}}` accepted: the line under the first row goes. `{"border":{"style":"wavy"}}`: `{"accepted":false,"field":"border.style","message":""}`, still parked; `kicadPromptCancel` ends it, nothing changes.
- Schematic, select two cells with different texts, Properties: `record.text === null`; `{"bold":true}` accepted: both keep their own texts (wrong: both read the same text).
- Board, the 2 by 2 table: `ask` `table`, `record.layer` canonical (`F.SilkS` or as drawn), `record.border.color === ""`; `{"border":{"color":"#ff0000ff"}}` refused with field `border.color`; `{"layer":"F.Fab"}` accepted and the table moves to F.Fab; `{"cells":[["a","b"]]}` refused with field `cells`.
- Board cell: `ask` `tableCell`, `record.width` and `record.height` numbers, `record.size === null`; `{"width":2,"height":2}` accepted, the text grows.
- `kicadTableCsv('{"uuid":"<the schematic table uuid>"}')`: `{"name":"canvas-kinds-table","text":"..."}` with three lines of three comma separated fields ending in `\n`; a cell's uuid gives the same; `kicadTableCsv('{}')` and `kicadTableCsv('{"uuid":"<a wire>"}')` give `""`. Board: two lines of two fields. Wrong: fields of a cell holding a comma not quoted.
- No listener: each dialog shows as before (both `ShowModal` and `ShowQuasiModal` paths: the drawing tool's new table uses `ShowQuasiModal() == wxID_OK`).

## st-keys-e: kicadHotkeysGet, kicadHotkeysSet

Pcbjam only (no KiCad file), commit 3620c5e. Both registered in `cc_e1_dispatch.inc` block `cc_e1_dispatch` (dispatcher TU, merged image), no halves: the live schematic or board frame's own `TOOL_MANAGER` (top window `KIWAY_PLAYER`) and its `ACTION_MANAGER`. Footprint, symbol and drawing sheet editors answer `""` / `false`.

- `kicadHotkeysGet() -> string`: `{ "editor": "sch" | "pcb", "actions": [{ name, label, key, default, alt }] }`, sorted by name: every action of the frame's action manager whose name starts with `eeschema.` (schematic) or `pcbnew.` (board) or `common.`, and which has a friendly name (KiCad's own hotkey list rule, `HOTKEY_STORE::Init`). `key` the primary key now, `default` KiCad's default, `alt` the secondary (the island's seeded chord), each in KiCad's key grammar (`KeyNameFromKeyCode`, `Ctrl+Shift+M`), `""` for none; `label` `GetFriendlyName()`. A read.
- `kicadHotkeysSet( string json ) -> bool`: `{ "keys": { "<action>": "<key>" | "" } }` exactly, at most 2,048 entries. Read whole first: an action not in this editor's list, a value not a string, a key `KeyCodeFromKeyName` cannot read (result 0 or less), any other shape, a modal dialog, a parked prompt or an open in flight: `false`, nothing changes. Then `TOOL_ACTION::SetHotKey( key, GetHotKeyAlt() )` per action (PRIMARY only, the secondary kept) and `ACTION_MANAGER::UpdateHotKeys( false )` (rebuilds the key map from the actions, no file read, no reset to defaults). Not an edit: no undo step, no modified flag. Keys live for this engine's lifetime: the page sends `cc.viewer.editorKeys` again at `ev.ready`.

What the build train must probe (Glasgow example, Edit mode, each frame):

- `JSON.parse(Module.kicadHotkeysGet())`: schematic `editor === "sch"`, an action `eeschema.InteractiveEdit.rotateCCW` with `default === "R"`; no `pcbnew.` name; board `editor === "pcb"`, `pcbnew.InteractiveRouter.SingleTrack` with `default === "X"`; no `eeschema.` name. Wrong: both editors' actions in one list.
- `Module.kicadHotkeysSet('{"keys":{"pcbnew.InteractiveRouter.SingleTrack":"Shift+Q"}}')` true; get shows `key: "Shift+Q"`, `alt` unchanged; pressing Shift+Q on the canvas starts routing; X no longer does. `{"keys":{"pcbnew.InteractiveRouter.SingleTrack":""}}` true and the action has no key. `{"keys":{"pcbnew.Nope":"Q"}}`, `{"keys":{"pcbnew.InteractiveRouter.SingleTrack":"NotAKey"}}` and `{}` give false and nothing changes. Undo history and the save state unchanged after a set.

## m6-e3: kicadSelectionFilterGet, kicadSelectionFilterSet

Pcbjam only, commit 3620c5e (the same commit as st-keys-e: both units sit in `cc_e1_dispatch.inc`). Registered in `cc_e1_dispatch.inc` block `cc_e1_dispatch`; halves `selectionFilterGetPcb`, `selectionFilterSetPcb` (`cc_e1_pcb.inc`) and `selectionFilterGetSch`, `selectionFilterSetSch` (`cc_e1_sch.inc`), all under `#ifdef KICAD_MERGED_EMBIND` (MERGED IMAGE ONLY: the dispatcher calling them exists only there, and the pane is found by `dynamic_cast`, which occ_service should not have to link).

- `kicadSelectionFilterGet() -> string`: board `{ lockedItems, footprints, text, tracks, vias, pads, graphics, zones, keepouts, dimensions, points, otherItems }` (`PCB_SELECTION_FILTER_OPTIONS`), schematic `{ lockedItems, symbols, text, wires, labels, pins, graphics, images, ruleAreas, otherItems }` (`SCH_SELECTION_FILTER_OPTIONS`), each a boolean, from `PCB_SELECTION_TOOL::GetFilter()` / `SCH_SELECTION_TOOL::GetFilter()`. `""` in any other frame. A read.
- `kicadSelectionFilterSet( string json ) -> bool`: some of the live editor's keys with booleans, the others kept; read whole first: an unknown key, a non boolean, an empty object, another shape, a modal dialog, a parked prompt or an open in flight: false, nothing changes. Writes the tool's filter and then sets KiCad's hidden Selection Filter pane from it (`PANEL_SELECTION_FILTER::SetCheckboxesFromFilter`, `PANEL_SCH_SELECTION_FILTER::SetCheckboxesFromFilter`; the pane found among the frame's child windows), so the pane and the tool never disagree. No undo step, no modified flag.

What the build train must probe (Glasgow example, Edit mode):

- Board: `JSON.parse(Module.kicadSelectionFilterGet())` has the twelve keys, `tracks === true`. `Module.kicadSelectionFilterSet('{"tracks":false}')` true: a drag box over tracks and pads selects only the pads; get shows `tracks: false`, the rest unchanged. `{"tracks":"no"}`, `{"wires":true}` (a schematic key) and `{}` give false. Wrong: the box still takes tracks (the tool's filter not written), or KiCad's pane switches disagreeing with get (the pane not refreshed).
- Schematic: ten keys; `{"wires":false}` true, a drag box over wires and symbols takes the symbols only; `{"tracks":false}` false.
- Footprint editor frame: get `""`, set false.

Syntax check for both units (and the m12-e dialogs again), from this worktree: `/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e1/kicad all --kicad-file eeschema/dialogs/dialog_table_properties.cpp --kicad-file pcbnew/dialogs/dialog_table_properties.cpp --kicad-file eeschema/dialogs/dialog_tablecell_properties.cpp --kicad-file pcbnew/dialogs/dialog_tablecell_properties.cpp`:

```
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (12363 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (12207 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (7546 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (6893 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (999 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (5424 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (11910 ms)
PASS kicad eeschema/dialogs/dialog_table_properties.cpp (7281 ms)
PASS kicad pcbnew/dialogs/dialog_table_properties.cpp (6068 ms)
PASS kicad eeschema/dialogs/dialog_tablecell_properties.cpp (6222 ms)
PASS kicad pcbnew/dialogs/dialog_tablecell_properties.cpp (5869 ms)
syntax-check: 0 failed
```

Export check: `scripts/kicad/check-run15-exports.sh --since run15/main --kicad /home/matthew/cc-engine-wt/e1/kicad --kicad-since run15/main`: `168 names registered in the merged image (161 kicad names), 23 of 110 run 15 names, 0 failures`.
