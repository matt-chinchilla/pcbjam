<!-- Copyright (C) 2026 Circuit Center Inc -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Engine run 15, lane e4: the footprint editor core, the item kinds and the pads

Branch `seat/run15-e4` of pcbjam, worktree `/home/matthew/cc-engine-wt/e4/pcbjam`. No KiCad file
(the syntax check runs against `/home/matthew/cc-engine-wt/e0-kicad`). Files of the lane:
`wasm/bindings/fpedit_common.h`, `wasm/bindings/cc_e4_fpedit.inc`, `wasm/bindings/cc_item_pad.h`,
`wasm/bindings/cc_item_text.h`, this file. The units' own notes files named by the manifest
(`BUILD-NOTES-run15-fp-e2.md`, `-fp-gate0.md`, `-fp-e-items.md`, `-fp-e3.md`, `-fp-e4a.md`) are
written as sections of this one file.

Every export lives in the footprint editor face `cc_e4_fpedit.inc` (TU `fpedit_embind.cpp`,
include subdir pcbnew, merged image only: that TU is linked into the merged image alone and is
compiled without `KICAD_MERGED_EMBIND`), registered in `EMSCRIPTEN_BINDINGS( cc_e4_fpedit )`.
None is in the board TU, so none touches occ_service.

## fp-e2: the core exports (FP spec 5.1, 7.1)

Commits: `6c44e59` (the early header `fpedit_common.h`, merged into `run15/main` as 7181009),
`c46948b` (the gate code: `kicadFpEditOpen`, `kicadFpEditState`, `kicadFpEditTake`), `7978935`
(`kicadFpEditNew`, `kicadFpEditMarkSaved`, `kicadFpEditSelection`, `kicadFpEditSelect`).

`fpedit_common.h` (namespace `cc_fpedit`, `fpEditFrame()` also at global scope): `fpEditFrame()`
(the top window as `FOOTPRINT_EDIT_FRAME`), `dialogUp()`, `readyFrame()` (none while
`pcbjam_open::busy()` or a dialog is up), `currentFootprint( frame )`, the closed JSON readers
(`onlyKeys`, `hasKeys`, `textOf`, `numberOf`, `intOf`, `boolOf`, `tokenOf`, `isLowerUuid`,
`uuidsOf`), units (`mmOf`, `iuOf`, `pointJson`, `pointOf`, `degreesOf`, origin relative), layers
by canonical name (`layerName`, `layerOf`, `layersOf`, `layersJson`, through `LSET::Name` over
`LSET::AllLayersMask()`), `dumpJson`, `utf8`, and `commitOnCoroutine( label, body )` (the body
runs on the apply coroutine with a `BOARD_COMMIT` on the re-resolved frame; true pushes ONE undo
step under the label, false reverts). Bounds: `TEXT_MAX` 1,024, `FOOTPRINT_TEXT_MAX` 8 MB,
`POSITION_MAX_MM` 50,000, `SIZE_MAX_MM` 1,000, `UUIDS_MAX` 4,096, `NEW_ITEMS_MAX` 512,
`POINTS_MAX` 4,096.

| export | signature | body |
|---|---|---|
| `kicadFpEditOpen` | `bool (std::string)` | `{text, name, lib, mode, discard, token}` exactly; the text read synchronously by `PCB_IO_KICAD_SEXPR_PARSER` over a `STRING_LINE_READER` (FP_CACHE::Load's reader), refused unless it yields a `FOOTPRINT`; on the coroutine `Clear_Pcb( false )`, crosshair to 0,0, `AddFootprintToBoard`, `ClearFlags`, `Ref**` and `Val**` refilled when empty (KiCad's own literals in `LoadFootprintFromLibrary`; not for `placed`), `Zoom_Automatique( false )`, `ClearUndoRedoList`, unmodified, `UpdateView`, `Refresh` |
| `kicadFpEditNew` | `bool (std::string)` | `{name, lib, type, discard, token}`; on the coroutine `Clear_Pcb( false )`, `CreateNewFootprint( name, "" )`, `SetAttributes` (smd `FP_SMD`, through_hole `FP_THROUGH_HOLE`, unspecified 0), then the open's tail plus `BuildListOfNets` and position 0 (NewFootprint's lines); mode `new`, unmodified (the spec's "then as open") |
| `kicadFpEditState` | `std::string ()` | `FpEditState`: token, open, name (the FPID's item name), lib (the page's), mode, modified (`GetScreen()->IsContentModified()`), depth (`GetUndoCommandCount()`), padCount (`Pads().size()`, NPTH included), type (from the attributes, through hole first), padEditMode (`PAD_TOOL::InPadEditMode()`); answers while a load is parked |
| `kicadFpEditTake` | `std::string ()` | `{name, text}`: a `Clone()` (uuids kept) reset as `SaveFootprintInLibrary` plus `FootprintSave` reset theirs (FPID without nickname, children's flags cleared, `ANGLE_0`, flipped to the front by `GetPcbNewSettings()->m_FlipDirection` when not on F.Cu, no parent, no group, `ClearAllNets`), `PCB_IO_KICAD_SEXPR( CTL_FOR_LIBRARY )` into a `STRING_FORMATTER`, `KICAD_FORMAT::Prettify` in the mode `PRETTIFIED_FILE_OUTPUTFORMATTER` takes (`NORMAL`, or `COMPACT_TEXT_PROPERTIES` under `m_CompactSave`): byte for byte what FP_CACHE::Save would write |
| `kicadFpEditMarkSaved` | `bool (std::string token)` | the plain token (not JSON); `ClearModify()` and `UpdateTitle()` when it equals the session's; no undo step |
| `kicadFpEditSelection` | `std::string ()` | `{uuids}` of `PCB_SELECTION_TOOL::GetSelection()` |
| `kicadFpEditSelect` | `bool (std::string)` | `{uuids[0..4096], zoom}` exactly, every uuid a child of the footprint (recursive); `ClearSelection`, `AddItemsToSel`, `zoomFitSelection` with zoom |

The read only info bar: `ReloadFootprint` queues a `CallAfter` that raises "Editing footprint
from read-only library" for a nickname the frame's table does not call writable. With no KiCad
change, the footprint is added under an EMPTY nickname (KiCad's own editable case) and a second
`CallAfter`, queued after ReloadFootprint's, sets `lib:name`. So the FPID reads `lib:name` from
the next event loop turn on, and no info bar is raised.

Plan A is coded (open by text, no library row). Plan B would change only the open's read.

Syntax check (from the pcbjam worktree, `--kicad /home/matthew/cc-engine-wt/e0-kicad fpedit`):
`PASS fpedit fpedit_embind.cpp subdir=pcbnew`, `syntax-check: 0 failed` at 6c44e59, c46948b and
7978935. Export check (`--since run15/main`, after merging run15/main 209ff25): `0 failures`.

What the build train must probe (the fixture `fpedit-small`, an `?frame=fpedit` engine):

- `kicadFpEditOpen( JSON.stringify({ text: <SOIC-8_3.9x4.9mm_P1.27mm.kicad_mod>, name: "SOIC-8_3.9x4.9mm_P1.27mm", lib: "fpedit-small_parts", mode: "library", discard: false, token: "t1" }) )` answers true; after the coroutine `kicadFpEditState()` reads `open true, padCount 8, depth 0, modified false, token "t1", type "smd", lib "fpedit-small_parts"`; no info bar, no dialog. Wrong: depth above 0 (the clear ran before the add), or an info bar (the CallAfter order).
- The same with `discard: false` after one edit answers false; with `discard: true` true.
- Text `"(kicad_pcb)"` or `"garbage"` answers false and nothing changes.
- `kicadFpEditTake()` after the open, then open of that text, then take: the two texts byte equal. Wrong: any difference (the reset copy differs from the save's).
- The pad tool places a pad, `kicadFpEditState().depth` reads 1, Ctrl+Z through `key.press` returns it to 0.
- `kicadFpEditNew( JSON.stringify({ name: "NEW1", lib: "", type: "smd", discard: true, token: "t2" }) )` then state: `name "NEW1", type "smd", mode "new", depth 0, padCount 0`.
- `kicadFpEditMarkSaved("t2")` after an edit: modified false, depth unchanged; `"wrong"` answers false.
- `kicadFpEditSelect( JSON.stringify({ uuids: [<a pad uuid>], zoom: true }) )`, then `kicadFpEditSelection()` lists it; an unknown uuid answers false.
- Fifty open and take cycles: `Module.HEAP8.length` flat past noise.
