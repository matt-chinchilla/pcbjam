<!-- Copyright (C) 2026 Circuit Center Inc -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Engine run 15, train 17, group D (items 8, 14, 15 engine half, 12)

Worktrees `/home/matthew/cc-engine-wt/t17-d/pcbjam` and `/home/matthew/cc-engine-wt/t17-d/kicad`, branch `seat/t17-d` in both, cut from pcbjam `run15/main` 772f9c2 and kicad `run15/main` 3e13bf12. Every KiCad change is under `#ifdef __EMSCRIPTEN__`; plain builds compile what they did. Every export is in `cc_e1_dispatch.inc` (block `cc_e1_dispatch`), none is merged image only (the dispatcher TU is header light: the three new headers are header only and need no pcbnew or eeschema header). No widely included header changed: the three new headers are included by `library_manager.cpp`, `tools_holder.cpp`, `tool_manager.cpp` and `selection_tool.cpp` (all `COMMON_SRCS`, one image) and the dispatch inc. Nothing reported to E0 (no include path, CMake or script line needed).

## Item 8 (t17-8): no warning window for an unreadable library table; kicadLibraryTableProblems

- kicad 1499b78d: new `include/libraries/cc_library_table_problems.h` (`CC_LIBRARY_TABLE_PROBLEM {path, kind, message}`, `CcLibraryTableProblems()`, `CcLibraryTableForget(path)`, `CcLibraryTableProblem(path, kind, message)`); `common/libraries/library_manager.cpp` `LIBRARY_MANAGER::loadTables`: each pass over a table path first forgets that path; when the table's type is not the expected one (a parse failure leaves UNINITIALIZED) the browser build records `{path, kind, message}` in place of `wxLogWarning`, message = `table->ErrorDescription()` (the parser's own sentence, for example "Syntax error at line 5, column 131"), or KiCad's warning sentence when that is empty. kind is "symbol", "footprint" or "designBlock". A table that then loads, or no longer exists, leaves the list. Global and project scopes both.
- pcbjam 243a143: export `kicadLibraryTableProblems()` -> `{"tables":[{"path":"...","kind":"symbol","message":"..."}]}` (always that shape, `[]` when none). A read, any frame, synchronous.
- Probe: `sources-small`, Edit mode, both frames: no visible wx window at engine start; `project.libs {"libs":["sources-small_parts"]}` answers ok in the pcb frame (not busy); `kicadLibraryTableProblems()` lists the project `sym-lib-table` path, kind "symbol", with the syntax error sentence; no fp-lib-table row. Wrong if: the window still shows (another log site on that road), or the list is empty (the pass ran before the export was read and a later pass found the file readable: then the forget-on-pass rule is wrong).

## Item 14 (t17-14): kicadActionStates

- pcbjam b9bdf31: export `kicadActionStates( args )`. args exactly `{"actions":[...]}`, 1 to 256 strings; "" for any other shape, no top frame (`KIWAY_PLAYER` as `kicadRunAction` finds it), or an open in flight. Answer `{"states":{"<name>":true|false|null}}`, one key per distinct name. Per name: `FindAction`, `ACTION_MANAGER::GetCondition`, `checkCondition( frame->GetCurrentSelection() )` inside `try` (as `EDA_BASE_FRAME::HandleUpdateUIEvent` does). null for: no action of this frame, no registered conditions, a check condition that is still KiCad's default `SELECTION_CONDITIONS::ShowNever` (the action is no toggle: reading false there would be a mark that lies), or a throw. A read: synchronous, nothing queued.
- Probe: Glasgow pcb frame: `kicadActionStates('{"actions":["common.Control.toggleGrid","pcbnew.Control.showRatsnest","common.Control.togglePolarCoords","no.such.action"]}')` reads the three as the frame shows them and null for the fourth; `kicadRunAction("common.Control.toggleGrid")`, apply queue empty, read again: the grid flipped, the others not. Also a plain command (for example `common.Control.zoomFitScreen`): null. Schematic and footprint editor frames: the grid the same. Wrong if: a toggle reads null (its frame registers no check condition: name it) or the read does not follow a press.

## Item 15 (t17-15), engine half: the host is told the held tool

- kicad b24857c8: new `include/tool/cc_host_tool.h`: `CcTellHostTool( name )` calls `window.kicadHostTool.onChange( '{"tool":"<name>"}' )` when that is a function, inside `try`, only when the name differs from the last one sent (the last name is kept even with no listener). `common/tool/tools_holder.cpp`: at the end of `PushTool`, and at `PopTool`'s exit after it removed the tool, `ccTellHostToolOf( this )` sends `CurrentToolName()`, ONLY when this holder is the app's top window (a frame behind, the simulator for example, never speaks). Where PopTool re-activates the tool under it (it pops that one and posts its event, which pushes it again) nothing is sent there: the re-push sends it, so the host never sees a passing "selection tool" between two tools.
- pcbjam 8e0177c: marker `kicadHostToolChanges()` (true); `kicadActiveTool()` -> the top frame's `CurrentToolName()` (KiCad gives `common.InteractiveSelection.selectionTool` for an empty stack), "" with no frame. Both reads.
- Probe: Glasgow pcb frame, a recorder as `window.kicadHostTool.onChange`: `kicadRunAction("pcbnew.InteractiveRouter.SingleTrack")` gives `{"tool":"pcbnew.InteractiveRouter.SingleTrack"}`; Esc (twice if a route is in progress, the first cancels the route) gives `{"tool":"common.InteractiveSelection.selectionTool"}`; `kicadActiveTool()` equals the last; the same in the schematic frame with the wire tool (`eeschema.InteractiveDrawingLineWireBus.drawWires`). Also: a tool that runs another and returns to it gives no selection notice in between. Wrong if: no notice (the island's listener installed after the first push: it reads `kicadActiveTool()` after `ev.ready`), or one notice per mouse move (the dedupe broken).

## Item 12 (t17-12): only the clarify list is marked clarify

- kicad 347b0a3a: new `include/tool/cc_clarify_menu.h` (`CcClarifyMenus()`, `CcIsClarifyMenu( menu )`, RAII `CC_CLARIFY_MENU_MARK`); `common/tool/selection_tool.cpp` `SELECTION_TOOL::doSelectionMenu` marks `&menu` just before `SetContextMenu( &menu, CMENU_NOW )`, the mark living to the end of that pass of the loop (the menu's own scope); `common/tool/tool_manager.cpp` `DispatchContextMenu` reads `clarify = trigger == CMENU_NOW && CcIsClarifyMenu( m )`. KiCad's other immediate menus (TOOL_MENU, the bus unfold menu, the drawing tool's) read false. Both KiCad files are `COMMON_SRCS`: every frame (pcbnew, eeschema, the drawing sheet editor, gerbview) uses this one `doSelectionMenu`.
- Probe: `canvas-kinds` board: right click R1, `ev.contextMenu` `clarify:false`; right click where two tracks cross (150, 90): `clarify:true`. Island e2e `tests/e2e/m/m1-menu.spec.ts` (line 114 expects false on R1) passes. Wrong if: the crossing reads false (the dispatched `m` is not the marked menu).

## For the island and the site (section 2 of T17.md, unchanged by what was built)

- I8 `libs.problems` reads `kicadLibraryTableProblems()` (JSON text, parse it). I14 `actions.state` calls `kicadActionStates` with the JSON text, "" means refused; states hold `true`, `false` or `null`; note null also for actions that are no toggle. I15 install `window.kicadHostTool = { onChange( text ) }` before boot (text is JSON `{"tool": string}`), read `kicadActiveTool()` after `ev.ready`; cap on `kicadHostToolChanges`. S12 may take out `isChoiceList` once the island built on T17 is served.

## Checks

The syntax check (from this worktree, through the gate):

```
/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/t17-d/kicad all \
    --kicad-file common/libraries/library_manager.cpp --kicad-file common/tool/tools_holder.cpp \
    --kicad-file common/tool/tool_manager.cpp --kicad-file common/tool/selection_tool.cpp
```

```
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (15952 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (14844 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (8318 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (10411 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (6386 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (5952 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (14144 ms)
PASS kicad common/libraries/library_manager.cpp (6541 ms)
PASS kicad common/tool/tools_holder.cpp (3529 ms)
PASS kicad common/tool/tool_manager.cpp (4793 ms)
PASS kicad common/tool/selection_tool.cpp (2922 ms)
syntax-check: 0 failed
```

