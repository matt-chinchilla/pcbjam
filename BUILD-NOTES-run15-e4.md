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

## fp-e-items: the kind table and the four item exports (FP spec 7.2)

Commit `fc24faa` (`cc_e4_fpedit.inc`, `cc_fp_items.h`).

`cc_fp_items.h` (lane e4's after the seams) keeps E0's declarations and gains ONE declaration,
`const char* ccItemKindName( const BOARD_ITEM* )` (the kind name by KiCad type: `PCB_PAD_T` pad,
`PCB_FIELD_T` field, `PCB_TEXT_T` text, `PCB_TEXTBOX_T` textbox, `PCB_SHAPE_T` shape, the five
`PCB_DIM_*_T` dimension, `PCB_ZONE_T` zone, `PCB_TABLE_T` table, `PCB_BARCODE_T` barcode,
`PCB_REFERENCE_IMAGE_T` image, `PCB_POINT_T` point), and the BODIES' CONTRACT every kind and
every caller keeps (written in the header):

- `get` answers the record WITHOUT `kind` and `uuid`; `ccFpItemJson` writes both after it.
- `set` takes the patch alone; it checks EVERY key before changing anything, a refusal stages and
  changes nothing, a taken patch calls `aCommit.Modify( item )` BEFORE the change. A key the
  item lacks is a refusal (FP spec: a set over several uuids carries only keys every one has).
- `add` takes the record less `kind` and `uuid` and answers a NEW item parented to the
  footprint and NOT yet added to it (nullptr when refused); the caller stamps the page's uuid and
  stages `aCommit.Add( item )`, whose Push puts it into the footprint (in the footprint editor
  `BOARD_COMMIT::Push` adds a CHT_ADD item to `GetFirstFootprint()`) or the board.

Lanes e5 (the shape, dimension, zone, image and point rows, `cc_item_shape.h`) and e3 (m4-e,
the board face over the table) write against this; reported to E0.

Definitions (`namespace cc_fpitems` in `cc_e4_fpedit.inc`): the table is a function local static
`std::vector<CcItemKind>` in `kindTable()`; `ccRegisterItemKind` refuses a row with a null name
or body and keeps the first row of a name; `ccFpItemJson` answers a null json for an item with no
row; `ccFpItemApply` refuses a patch that is not a non empty object or holds `kind` or `uuid`.

| export | signature | body |
|---|---|---|
| `kicadFpEditItemsGet` | `std::string (std::string)` | `{uuids?[1..4096], kinds?[1..11], layers?[1..256]}`; the footprint's own items by `RunOnChildren( NO_RECURSE )` (fields, pads, zones, groups skipped, points, drawings: KiCad's child order) that have a row; a listed uuid not an item answers ""; layers keeps an item whose `GetLayerSet()` meets the list; positions relative to `footprint->GetPosition()` |
| `kicadFpEditItemsSet` | `bool (std::string)` | `{uuids[1..4096], set}`; every uuid a top item, all of one kind; a dry run of the patch on a `Clone()` of the first item on a scratch `BOARD_COMMIT` never pushed (in the footprint editor `undoLevelItem` is the item itself, so the commit copies the clone alone and frees the copy) answers false before anything is queued; on the coroutine every item through `ccFpItemApply` in ONE commit `Edit`, reverted whole on any refusal |
| `kicadFpEditItemsAdd` | `bool (std::string)` | `{items[1..512]}`; each `{kind, uuid, ...}`, the uuid lowercase, new to the footprint (recursive, the footprint's own included) and unique; each made once by its row and thrown away before queuing; on the coroutine made again and `commit.Add` in ONE commit `Add` |
| `kicadFpEditItemsRemove` | `bool (std::string)` | `{uuids[1..4096]}`; every uuid a top item with a row, never the Reference or Value field (`PCB_FIELD::IsReference`, `IsValue`); the selection cleared, `commit.Remove` each, ONE commit `Delete` |

Syntax check (`--kicad /home/matthew/cc-engine-wt/e0-kicad fpedit pcbnew occ`, the header is
included by the merged pcbnew TU): `PASS pcbnew pcbnew_embind.cpp subdir=pcbnew
-DKICAD_MERGED_EMBIND`, `PASS fpedit fpedit_embind.cpp subdir=pcbnew`, `PASS occ
pcbnew_embind.cpp subdir=pcbnew`, `syntax-check: 0 failed`. Export check after merging
run15/main 9f3a5d9: `0 failures`.

What the build train must probe (fixture `fpedit-small`, the SOIC open; every kind row merged):

- `kicadFpEditItemsGet("{}")`: every item with `kind` and `uuid`, fields first, then the 8 pads, then the drawings; `{"kinds":["pad"]}` the 8 pads only; `{"layers":["F.SilkS"]}` only silk items; `{"uuids":[<one pad>]}` that pad; `{"uuids":[<unknown>]}` answers "". Wrong: an item twice, a group or table cell listed, a missing kind or uuid.
- `kicadFpEditItemsSet` over two pads `{set:{x: ...}}` is ONE undo step labeled Edit (depth +1, one Ctrl+Z restores both); a patch with a key no pad has answers false and depth stays.
- `kicadFpEditItemsSet` over a pad and a field answers false (two kinds).
- `kicadFpEditItemsAdd` of two shapes with page made uuids: ONE undo step Add, both read back with those uuids; a uuid already in the footprint answers false.
- `kicadFpEditItemsRemove` of the Reference field's uuid answers false; of a silk line, one step Delete.

## fp-e3: the pad kind, the master pad, the numbering loop (FP spec 7.2)

Commit `9780108` (`wasm/bindings/cc_item_pad.h` new, `cc_e4_fpedit.inc`). The section the
manifest names `BUILD-NOTES-run15-fp-e3.md` is this one.

The kind row `pad` is registered in `cc_e4_fpedit.inc` (`cc_e4::kPadKindRegistered`, a namespace
scope registrar in the fpedit TU over `cc_fp_items.h`'s table); its bodies are
`cc_e4::padkind::kindGet`, `kindSet`, `kindAdd` in `cc_item_pad.h` (inline, included by the inc
alone). The four item exports of fp-e-items serve pads through that row; the board face (m4-e)
reaches it through `ccItemKind( "pad" )`.

**The pad record** (for fp-i2a and fp-s6; mm, degrees, positions relative to the caller's origin,
`angle` relative to the footprint as the pad dialog shows it, `GetFPRelativeOrientation`):

| key | values | KiCad |
|---|---|---|
| `number`, `pinName`, `pinType` | text 0..1,024 | `SetNumber`, `SetPinFunction`, `SetPinType` |
| `type` | `smd` `th` `npth` `conn` `aperture` (an SMD pad with no copper, `IsAperturePad`) | `SetAttribute` (the dialog's `code_type`) |
| `x`, `y`, `angle`, `locked` | | `SetPosition`, `SetFPRelativeOrientation`, `SetLocked` |
| `padstack` | `normal` `front_inner_back` `custom` (non normal for th only) | `PADSTACK::SetMode`, new layers copied as the dialog's `newLayerMap` |
| `copper` | PadShape of F.Cu | per layer setters |
| `layerShapes` | `[{layer, copper}]`, every unique layer but F.Cu | the same per layer |
| `drill` | `{shape: circle or oval, size}` or null | `SetDrillShape`, `SetDrillSize` |
| `layers` | names | `SetLayerSet` by the dialog's copper choices |
| `dieLength` (mm), `dieDelay` (ps, 0..2000) | | `SetPadToDieLength`, `SetPadToDieDelay` |
| `clearance`, `maskMargin`, `pasteMargin`, `thermalSpokeWidth`, `thermalGap` | mm or null | the local overrides |
| `pasteRatio` | -1..1 or null | `SetLocalSolderPasteMarginRatio` |
| `zoneConnection` | `inherit` `none` `thermal` `solid` | `SetLocalZoneConnection` |
| `customZoneShape` | `outline` `convexhull` | `SetCustomShapeInZoneOpt` |
| `thermalAngle` | degrees | `SetThermalSpokeAngle` |
| `property` | `none` `bga` `fiducial_global` `fiducial_local` `testpoint` `heatsink` `castellated` `mechanical` `pressfit` | `SetProperty` |
| **added** `unconnectedLayers` | `keep_all` `remove_except_start_and_end` `remove_all` | the dialog's copper radio (th with copper only, else keep_all) |
| **added** `teardrop` | `{enabled, useTwoTracks, preferZoneConnection, maxLength, maxWidth, lengthRatio, widthRatio, curved, sizeFilterRatio}` (partial on set) | `GetTeardropParams()` fields as `transferDataToPad` writes them (`preferZoneConnection` is `!m_TdOnPadsInZones`) |
| **added** `backdrill` | `{front, back}`, each `{size, end}` (end a copper layer name or null) or null | front = `TertiaryDrill` from F.Cu, back = `SecondaryDrill` from B.Cu, circle |
| **added** `postMachining` | `{front, back}`, each `{mode: none, countersink, counterbore, size, depth, angle}` (whole) | `FrontPostMachining`, `BackPostMachining`; depth kept for counterbore, angle (degrees, stored in tenths) for countersink, as the dialog does |

PadShape: `shape` (`circle` `rect` `oval` `trapezoid` `roundrect` `chamfered_rect` `custom`),
`size`, `offset`, `delta`, `roundRatio` and `chamferRatio` (0..0.5), `chamferCorners`
(`tl` `tr` `bl` `br`), `anchor` (`circle` `rect`), `primitives` (custom only, at most 512, pad local
coordinates: `line` and `rect` [start, end], `arc` [start, mid, end], `circle` [center, a point on
it], `bezier` [start, c1, c2, end], `polygon` its outline of 3 or more points; `width`, `filled`).
Not set by the dialog and so not in the record: drill filled and capped, the net (the footprint
editor has none).

**Set** (a patch; `copper` and each `layerShapes` entry partial, `primitives` and `layerShapes`
replacing what they name, `backdrill` and `postMachining` sides whole): read whole first, tried on
a `Clone()` and checked by `CheckPad` (refused on `DRCE_PADSTACK_INVALID` and
`DRCE_PAD_TH_WITH_NO_HOLE`, `padValuesOK`'s errors; `DRCE_PADSTACK` is a warning and passes),
then `aCommit.Modify( pad )` and applied. The dialog's resets: a shape change into circle squares
the size and drops the offset (and moves the spoke angle 90 to 45; out of a circle 45 to 90); into
roundrect with ratio 0 takes `PAD_UTILS::GetDefaultIpcRoundingRatio`; into chamfered_rect takes
ratio 0.2 and the top left corner when none is given; a custom pad on a circular anchor has one
size; a non trapezoid has no delta, a non chamfered shape no corners, a non custom shape the
circle anchor (non empty primitives on a non custom shape refused). A type change takes the type's
default layers (`PTHMask`, `SMDMask`, `ConnSMDMask`, `UnplatedHoleMask`, `ApertureMask`) unless
`layers` is given, gives a hole type with no hole KiCad's default 0.8 mm drill, and drops number
and die length and delay for npth and aperture (a non empty one given is refused). Holes: smd,
conn and aperture have none (a drill given is refused). Layers by the dialog's choices: th any
copper means every copper layer, smd and conn exactly one of F.Cu or B.Cu, npth F.Cu and B.Cu at
most, aperture none (non copper layers kept as given). A trapezoid delta on both axes or past the
size is refused (the dialog's error). An oval hole has no backdrill or post machining (a non
none side given with an oval hole is refused).

**Add**: a `new PAD( footprint )` that first takes `m_Pad_Master` (`ImportSettingsFrom`, as the pad
tool's new pad), at the origin, then the record as a patch; refused when the parent is not a
footprint or the record fails as a set would.

| export | signature | body |
|---|---|---|
| `kicadFpEditPadDefaultsGet` | `std::string ()` | the master pad (`GetDesignSettings().m_Pad_Master`) as a pad record from 0, 0 |
| `kicadFpEditPadDefaultsSet` | `bool (std::string)` | `{set}` exactly; the patch on a copy and checked, then on the master pad, synchronously; net cleared, `PAD_TOOL::SetLastPadNumber` when a number is given (the dialog's two lines); no commit, no undo step, footprint not modified |
| `kicadFpEditEnumerate` | `bool (std::string)` | `{start 0..999, step 0..999, prefix text 0..64}` exactly (`DIALOG_ENUM_PADS`' ranges, prefix always set); `PAD_TOOL::enumerateLoop( params ) == 1` (e3's fp-e1b; links once e3's `pad_tool.cpp` body is merged) |

All in `EMSCRIPTEN_BINDINGS( cc_e4_fpedit )`, merged image only (the fpedit TU), none in the
board TU. No header change, no KiCad file, nothing reported to E0 (the include
`dialogs/dialog_enum_pads.h` resolves through the existing pcbnew include list).

Syntax check (`/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad
/home/matthew/cc-engine-wt/e0-kicad fpedit`): `PASS fpedit fpedit_embind.cpp subdir=pcbnew`,
`syntax-check: 0 failed`. Export check after merging run15/main 48e664e: `165 names registered in
the merged image (158 kicad names), 20 of 110 run 15 names, 0 failures`.

What the build train must probe (fixture `fpedit-small`, the SOIC open, and `CUSTOMPAD.kicad_mod`):

- `kicadFpEditItemsGet('{"kinds":["pad"]}')` on the SOIC: 8 records, `type "smd"`, `copper.shape` `"roundrect"` or `"rect"` as the file says, `layers` `["F.Cu","F.Paste","F.Mask"]`, `drill null`, `padstack "normal"`, `layerShapes []`. On CUSTOMPAD: the custom pad with `copper.shape "custom"` and 3 `primitives`, the thermal pad with `thermalSpokeWidth`, `thermalGap`, `thermalAngle` as the file. Wrong: a missing key, primitives on a non custom pad, positions off by the footprint position.
- `kicadFpEditItemsSet` one key at a time on one pad (`copper: {size: [1, 2]}`, `copper: {shape: "custom"}`, `drill`, `layers`, `zoneConnection`, `padstack`): each one undo step (depth +1, Ctrl+Z restores). `{copper:{shape:"circle"}}` on a 1 by 2 rect reads back size [1, 1], offset [0, 0]; `{copper:{shape:"roundrect"}}` from rect reads a non zero `roundRatio`. `{type:"th"}` on an smd pad reads `drill {circle, [0.8, 0.8]}` and every copper layer. Wrong: two undo steps, a dialog, a key unchanged.
- Refusals with depth unchanged: `{type:"smd", drill:{shape:"circle", size:[1,1]}}`, `{copper:{delta:[0.2,0.2], shape:"trapezoid"}}`, `{type:"th", drill:null}` (pad_th_with_no_hole), `{type:"npth", number:"5"}`, `{padstack:"custom"}` on an smd pad, `{layers:["F.Cu","B.Cu"]}` on an smd pad.
- `kicadFpEditPadDefaultsSet('{"set":{"copper":{"size":[1,2]}}}')`: true, depth and modified unchanged; `kicadFpEditPadDefaultsGet()` reads size [1, 2]; the pad tool's next pad (or `ItemsAdd` of `{kind:"pad", uuid, number:"9"}`) has size [1, 2]. Wrong: an undo step, the footprint modified.
- `kicadFpEditEnumerate('{"start":1,"step":1,"prefix":""}')`: true, no dialog, no `enumerate` ask, the bullseye cursor; a drag over three pads numbers them 1, 2, 3, a double click commits one undo step; Ctrl+Z restores. `{"start":1,"step":2,"prefix":"A"}`: A1, A3, A5. With no footprint pads it answers false and a later call does not reuse its numbers. Wrong: KiCad's numbering dialog, two undo steps.
