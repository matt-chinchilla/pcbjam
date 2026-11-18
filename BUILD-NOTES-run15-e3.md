<!-- Copyright (C) 2026 Circuit Center Inc -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Engine run 15, lane e3: footprint editor and board item asks

Branches `seat/run15-e3` in pcbjam (`/home/matthew/cc-engine-wt/e3/pcbjam`) and kicad
(`/home/matthew/cc-engine-wt/e3/kicad`). Units in order: fp-e1a, fp-e1b, fp-e1c, m4-e, then the
conditional fp-f1 and flt-ds4-e. The unit notes the manifest names `BUILD-NOTES-run15-<unit>.md`
are the sections of this one file.

## Early commit

kicad `426e5044`: `pcbnew/tools/pad_tool.h` declares, under `__EMSCRIPTEN__`, the public
`int PAD_TOOL::enumerateLoop( const SEQUENTIAL_PAD_ENUMERATION_PARAMS& aParams )` with a forward
declaration of the struct (for e4's `kicadFpEditEnumerate`). The body is fp-e1b's. Recompile reach:
`pad_tool.h` is included by the pcbnew tools that use PAD_TOOL (edit_tool.cpp, pad_tool.cpp,
footprint_edit_frame.cpp, pcb_edit_frame.cpp, tools setup and a few more); the train takes it.

## fp-e1a: the footprint editor asks the page

Host object `window.kicadHostFpEdit.onRequest( JSON {ask, uuid, isNew} )`, helper
`CcAskHostFpEdit( const char* ask, const KIID* uuid, bool isNew )` in the NEW header
`pcbnew/tools/footprint_editor_host.h` (inline, `__EMSCRIPTEN__` only, the run 14 K3 shape: a
missing listener, a falsy answer or a throwing listener answers false and KiCad's path runs).

Asks (all `isNew` false here):

| ask | uuid | KiCad site |
|---|---|---|
| `pad`, `field`, `text`, `textbox`, `shape`, `dimension`, `zone`, `table`, `barcode`, `image`, `point` | the item's (a table cell asks `table` with its table's uuid) | `EDIT_TOOL::Properties`, `pcbnew/tools/edit_tool.cpp`, the `m_isFootprintEditor` block after run A's board ask (helper `fpEditAskSubject`) |
| `footprint` | "" | the same block when the footprint itself is selected; `FOOTPRINT_EDITOR_CONTROL::Properties` (`footprint_editor_control.cpp`, first lines; also reached from E on nothing through `footprintProperties`) |
| `padDefaults` | "" | `FOOTPRINT_EDITOR_CONTROL::DefaultPadProperties` |
| `wizard` | "" | `FOOTPRINT_EDITOR_CONTROL::CreateFootprint` (before the unsaved changes box) |
| `newFootprint` | "" | `FOOTPRINT_EDITOR_CONTROL::NewFootprint` (before `Clear_Pcb( true )`) |
| `rename` | "" | `FOOTPRINT_EDITOR_CONTROL::RenameFootprint` |
| `cleanup` | "" | `FOOTPRINT_EDITOR_CONTROL::CleanupGraphics` |

A held item ask keeps the asked item the one selected, no longer a hover selection (run A's rule);
a held frame ask returns before anything changes (no unsaved changes box, nothing cleared).
A group in the footprint editor is not asked (no FP form for it this wave) and opens KiCad's dialog.

Marker: `kicadHostFpEdit` (`bool ()`, always true) in `cc_e3_dispatch.inc`, block
`EMSCRIPTEN_BINDINGS( cc_e3_dispatch )`. Not merged image only (the dispatcher face is the
kicad_editor TU).

Commits: kicad `70489094` (footprint_editor_host.h new, edit_tool.cpp, footprint_editor_control.cpp);
pcbjam `0a74f1a` (cc_e3_dispatch.inc).

Syntax check (`/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad
/home/matthew/cc-engine-wt/e3/kicad kicad_editor --kicad-file pcbnew/tools/edit_tool.cpp
--kicad-file pcbnew/tools/footprint_editor_control.cpp`):

```
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (6187 ms)
PASS kicad pcbnew/tools/edit_tool.cpp (17743 ms)
PASS kicad pcbnew/tools/footprint_editor_control.cpp (9176 ms)
syntax-check: 0 failed
```

What the build train must probe (fixture `island:tests/fixtures/fpedit-small/`, the SOIC open in
`?frame=fpedit`, a listener `window.kicadHostFpEdit = { onRequest: r => (log.push(JSON.parse(r)), true) }`):
- `Module.kicadHostFpEdit()` is `true`.
- Select a pad, a silk line, a text, the Reference field; press E each time: one request each,
  `{ask: "pad" | "shape" | "text" | "field", uuid: <the item's uuid, lowercase>, isNew: false}`, no
  wx dialog exists in the frame, the item stays selected; undo depth unchanged (0 new steps).
- E on empty canvas (nothing selected): `{ask: "footprint", uuid: "", isNew: false}`.
- `pcbnew.PadTool.defaultPadProperties`, `pcbnew.ModuleEditor.createFootprint`,
  `.newFootprint`, `.renameFootprint`, `pcbnew.GlobalEdit.cleanupGraphics` run through
  `key.press` or `kicadRunAction` with the island's hold lifted: `padDefaults`, `wizard`,
  `newFootprint`, `rename`, `cleanup` in turn, no dialog, the footprint unchanged and unmodified.
- With the listener answering `false` (or removed): E on a pad opens KiCad's pad dialog (proves
  the fallthrough; close it by Esc). A listener that throws logs `[cc fpedit] onRequest listener
  threw` and KiCad's dialog opens.
- Wrong if: any wx dialog with the listener answering true; a request with `uuid` not lowercase
  8-4-4-4-12 for an item ask; two requests for one press; the selection lost after a held ask.
