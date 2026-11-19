<!-- Copyright (C) 2026 Circuit Center Inc -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Engine run 15, lane e5: footprint shapes, body, check and embedded files; board round trip; place from a file

pcbjam only (no KiCad file in this lane). Worktree `/home/matthew/cc-engine-wt/e5/pcbjam`, branch
`seat/run15-e5`. Every syntax check runs against `/home/matthew/cc-engine-wt/e0-kicad` (kicad
`run15/main`), through `/home/matthew/cc-wave2/bin/gate.sh`. The units' own notes files named by
the manifest (`BUILD-NOTES-run15-fp-e7.md` and so on) are the sections of this file.

Files of the lane: `wasm/bindings/cc_e5_pcb.inc` (block `cc_e5_pcb`), `cc_e5_fpedit.inc`
(block `cc_e5_fpedit`), `cc_e5_sch.inc` (block `cc_e5_sch`), `cc_e5_dispatch.inc` (block
`cc_e5_dispatch`), `cc_item_shape.h`. Helpers of one face sit in `cc_e5::pcb`, `cc_e5::fp`,
`cc_e5::sch`.

## fp-e7: the board round trip and footprint file conversion (BUILD-NOTES-run15-fp-e7.md)

Commits: pcbjam c188111 (board face, the dispatcher's board half), 72d05a0 (the footprint editor half).
No KiCad change, no host ask, no marker.

Exports:

| name | inc, block | frame | merged image only |
|---|---|---|---|
| `kicadFootprintForEditor` | `cc_e5_pcb.inc`, `cc_e5_pcb` | pcb | no (occ_service registers it too; it calls nothing outside pcbnew_embind.cpp) |
| `kicadFootprintPushToBoard` | `cc_e5_pcb.inc`, `cc_e5_pcb` | pcb | no (as above) |
| `kicadFootprintConvert` | `cc_e5_dispatch.inc`, `cc_e5_dispatch` | fpedit, then pcb | yes (the dispatcher TU); halves `cc_e5::footprintConvertFp` (`cc_e5_fpedit.inc`) and `cc_e5::footprintConvertPcb` (`cc_e5_pcb.inc`), one body `cc_e5::footprintConvertBody` in `cc_e5_pcb.inc` called by both |

What each does:

- `kicadFootprintForEditor( '{"uuid"}' )`: `LoadFootprintFromBoard`'s copy (load_select_footprint.cpp
  77 to 170) with the board's uuids kept: `Clone`, flags cleared, unlocked (pads too), nets cleared,
  position 0, flipped to F.Cu with the board editor's `m_FlipDirection`, angle 0, then detached and
  formatted by `PCB_IO_KICAD_SEXPR( CTL_FOR_LIBRARY )` and `KICAD_FORMAT::Prettify` (what a library
  save writes: the footprint's own uuid, `(at)`, path and library nickname left out; EVERY CHILD
  KEEPS ITS UUID; no pad nets; embedded files written). Answers `{ name, libId, ref, text }`.
- `kicadFootprintPushToBoard( '{"uuid","text","libId","token"}' )`: the text parsed BEFORE
  anything is queued (`PCB_IO_KICAD_SEXPR::Parse` with no board: a library read); on the apply
  coroutine `SaveFootprintToBoard( false )`'s update body (footprint_libraries_utils.cpp 853):
  selection cleared, a clone with `SetLink( niluuid )`, a child whose uuid the placed footprint
  holds keeps it, any other a fresh `KIID`, the footprint's own uuid the placed one's, nets re-bound
  by name (KiCad's own loop; the library text carries none, so `ExchangeFootprint` gives the matched
  pads their old nets), FPID = the placed part's when `libId` is "", else `libId`; `ApplyDefaultSettings
  ( board, false, false, false, false, false )`: a fresh footprint editor's `BOARD_DESIGN_SETTINGS`
  carries `m_StyleFPFields`, `m_StyleFPText`, `m_StyleFPShapes`, `m_StyleFPDimensions`,
  `m_StyleFPBarcodes` all false (board_design_settings.cpp 125 to 129 and the `defaults.apply_defaults_to_fp_*`
  params' defaults at 863 to 871), so nothing is restyled; then `ExchangeFootprint( source, fresh,
  commit )` with KiCad's defaults and ONE `commit.Push( "Update footprint" )`. `token`: 1 to 64
  printable ASCII, checked, otherwise unused (the page reads back through the board form).
- `kicadFootprintConvert( '{"path":"/tmp/cc-import/<n>/<file>"}' )`: the path's folder must pass
  `cc_output::IsRunDir( dir, "/tmp/cc-import/" )`. Library road: `PCB_IO_MGR::GuessPluginTypeFromLibPath`
  (NESTED_TABLE and PCBJAM_FP set aside), `FootprintEnumerate( names, path, false )`, each
  `FootprintLoad( path, name, false )` (PCB_IO_MGR::ConvertLibrary's road). One footprint road: the
  first plugin with library extensions whose `CanReadFootprint` takes it, `ImportFootprint`
  (FOOTPRINT_EDIT_FRAME::ImportFootprint's road). Each footprint: FPID `"":name`, reset as
  FP_CACHE resets before a library save (angle 0, front), formatted as above. Answers `{ format:
  PCB_IO_MGR::ShowType(type), footprints: [{ name, text }] }`, `{ error: "too_large" }` past 2,000
  footprints or 8 MB of text, `{ error: "unreadable", message }` for no plugin, a thrown IO_ERROR
  or nothing read. `wxLogNull` keeps wx's log window shut.

Refusals: every export answers `""` or `false` at once for args of any other shape, no frame, an
open in flight (`pcbjam_open::busy()`) or a dialog up (any shown `wxDialog`).

What the build train must probe (island fixtures `tests/fixtures/fpedit-small/`,
`tests/fixtures/footprint-convert/`):

1. Board frame on fpedit-small: `kicadFootprintForEditor('{"uuid":"<U1>"}')` answers JSON whose
   `text` starts `(footprint "SOIC-8_3.9x4.9mm_P1.27mm"` (no library nickname), holds
   `(version 20260206)`, holds every pad uuid U1 has on the board, no `(net`, and `ref` "U1".
   Wrong if a pad uuid differs (the copy lost them) or the text holds `(at` on the footprint line.
2. `kicadFootprintPushToBoard` of that text with one pad's `(at` changed, `libId` "", any token:
   true; after the queue drains the undo depth is +1 (one step "Update footprint"), U1's position,
   angle, side and every pad's net unchanged, only that pad moved; Ctrl+Z restores it. Then with
   `libId` "fpedit-small_parts:SOIC-8_3.9x4.9mm_P1.27mm": U1's FPID reads that. Wrong if depth
   +2, nets lost (all pads on no net), or U1 jumps to the origin.
3. `kicadFootprintPushToBoard` with text "(kicad_pcb)" or "(footprint" (truncated): false and no
   step.
4. `kicadFootprintConvert` in the board frame and in an fpedit frame on `legacy.mod` (staged at
   `/tmp/cc-import/1/legacy.mod`): `format` "Legacy", one footprint; on `package.lbr`: `format`
   "Eagle", one footprint whose text parses back through `kicadFpEditOpen`. A path outside
   `/tmp/cc-import/<n>/` answers "". Wrong if the format reads "KiCad" for either.

Syntax check (`gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e0-kicad all`, on ff543c3 plus the notes):

```
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (32562 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (16190 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (9811 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (87620 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (1931 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (6988 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (16161 ms)
syntax-check: 0 failed
```

Export check (`scripts/kicad/check-run15-exports.sh --since run15/main`): `149 names registered in the merged image (142 kicad names), 4 of 110 run 15 names, 0 failures`.

## fp-e5: the footprint's properties and its embedded files (BUILD-NOTES-run15-fp-e5.md)

Commit: pcbjam c5acacb (`cc_e5_fpedit.inc`). No KiCad change, no host ask, no marker. Every
export below is merged image only (the fpedit TU exists only there).

| name | inc, block | answer |
|---|---|---|
| `kicadFpEditFootprintGet` | `cc_e5_fpedit.inc`, `cc_e5_fpedit` | `FootprintProps` (FP spec 7.3), "" with no ready frame or footprint; a pure read |
| `kicadFpEditFootprintSet` | `cc_e5_fpedit.inc`, `cc_e5_fpedit` | `{ set: FootprintSet }`: true when queued |
| `kicadFpEditEmbed` | `cc_e5_fpedit.inc`, `cc_e5_fpedit` | `{ path, name, type }` or `{ remove }`: true when queued |

What each does:

- Get: as `DIALOG_FOOTPRINT_PROPERTIES_FP_EDITOR::TransferDataToWindow` reads it. `fields` in the
  footprint's order, `name` the canonical name (Reference, Value, Datasheet, Description, then the
  user names), `kind` reference, value, datasheet, description or user. `pasteRatio` is KiCad's own
  ratio (the file's `solder_paste_ratio`, -1 to 1; the dialog shows it as a percentage).
  `zoneConnection` maps the dialog's four choices (any other stored value reads `inherit`).
  `customStackup.copperLayers` is 2 unless the stackup is custom (the dialog's first choice).
  `userLayerNames` = every user layer the frame's board enables, canonical name to shown name (the
  footprint editor's preferences hold these, not the footprint). `embedded[].bytes` = the
  decompressed size; KiCad's WORKSHEET type reads `other`.
- Set: the set read whole and closed (any unknown key, `embedded`, a wrong type or bound refuses),
  then checked against the footprint before anything is queued and again on the coroutine:
  `fields[]` patch by canonical name (value, visible, layer), `addField` (a free name, never a
  mandatory one; text size, thickness and italics from the design settings for its layer, as
  `OnAddField`), `removeField` (never a mandatory field), jumper groups only of pad numbers the
  footprint has, a custom stackup that orphans no layer in use (`LAYER_UTILS::GetOrphanedFootprintLayers`,
  as `Validate`), private layers never copper, technical, Edge.Cuts or Margin (the dialog's grid),
  copper count even 2 to 32, clearance 0 to 100 mm, margins -100 to 100 mm, models at most 32 (path
  up to 1,024 characters, offset within 1,000 mm, rotation within 360 degrees, scale 0.0001 to
  10,000, opacity 0 to 1). `userLayerNames` is READ ONLY: accepted only when equal to the get's.
  On the apply coroutine ONE commit "Footprint properties" in `TransferDataFromWindow`'s order
  (Modify, embedded files no field names any more removed, name, description, keywords, fields,
  private layers, stackup, attributes, courtyard, bridges, clearances, zone connection, net ties,
  jumpers, models), then `editFootprintProperties`' follow up: KiCad's own
  `FOOTPRINT_EDIT_FRAME::updateEnabledLayers` (protected; reached through a pointer to member
  formed in a derived class's using declaration, `cc_e5::fp::FRAME_LAYERS`, never instantiated),
  `UpdateTitle`, `UpdateMsgPanel`, `UpdateUserInterface`, a canvas refresh. A name set renames in
  the editor only (no library check: the page keeps names unique). Fields are patched in place
  (the dialog rebuilds the list from clones; the result is the same footprint, and every field
  keeps its uuid).
- Embed: `path` must be a regular file of at most 25 MB in a folder that passes
  `cc_output::IsEmbedRunDir` (`/tmp/cc-embed/<n>/<file>`), `name` 1 to 255 characters with no
  slash, `type` model, datasheet, font or other. On the coroutine `EMBEDDED_FILES::AddFile` reads,
  compresses and hashes it into a staging collection, the file is taken out under `name` and
  `type` and added to the footprint (a file of that name is replaced). ONE commit "Embed a file".
  `{ remove: name }`: refused when the footprint holds no such file; ONE commit "Remove an
  embedded file".

What the build train must probe (fpedit frame, island fixture `tests/fixtures/fpedit-small/`, the
SOIC opened by `kicadFpEditOpen`):

1. `kicadFpEditFootprintGet()`: `name` "SOIC-8_3.9x4.9mm_P1.27mm", four fields first (Reference,
   Value, Datasheet, Description), `type` "smd", `models` the SOIC's model line, `embedded` [].
   Wrong if `fields` is empty or a mandatory field reads `kind` "user".
2. `kicadFpEditFootprintSet('{"set":{"description":"probe"}}')`: true; after the queue drains the
   get reads it, `kicadFpEditState().depth` +1, `modified` true; Ctrl+Z restores. The same for
   `{"addField":{"name":"MPN","value":"X","visible":false,"layer":"F.Fab"}}`, for
   `{"allowMissingCourtyard":true}` and for one model line with offset [0,0,1] and scale [2,2,2].
   Wrong if depth +2 (two steps) or +0.
3. Refusals answer false at once with depth unchanged: `{"set":{}}`, `{"set":{"embedded":[]}}`,
   `{"set":{"removeField":"Reference"}}`, `{"set":{"jumpers":{"duplicateNumbersAreJumpers":false,"groups":[["99"]]}}}`,
   `{"set":{"userLayerNames":{"User.1":"x"}}}`, `{"set":{"customStackup":{"on":true,"copperLayers":3}}}`.
4. `kicadFpEditEmbed` with `body.step` staged at `/tmp/cc-embed/1/body.step`, name "body.step",
   type "model": true; the get lists `{name:"body.step", type:"model", bytes: <file size>}`;
   `kicadFpEditTake()` holds `(embedded_files` with it; depth +1. `{"remove":"body.step"}`: true,
   gone, depth +1. A path under `/tmp/cc-import/` or a file over 25 MB answers false. Wrong if
   bytes is 0 or the take lacks the file.

## fp-e6: the footprint checker, Tidy and SVG (BUILD-NOTES-run15-fp-e6.md)

Commit: pcbjam c5acacb (one commit with fp-e5: the two units share the inc) (`cc_e5_fpedit.inc`). No KiCad change, no host ask, no marker. Merged
image only.

| name | inc, block | answer |
|---|---|---|
| `kicadFpEditCheck` | `cc_e5_fpedit.inc`, `cc_e5_fpedit` | `{ findings: FpFinding[] }`, a pure read |
| `kicadFpEditCleanup` | `cc_e5_fpedit.inc`, `cc_e5_fpedit` | `{ items: [{code, uuids}], repaired }` |
| `kicadFpEditPlotSvg` | `cc_e5_fpedit.inc`, `cc_e5_fpedit` | true when queued; `<dir>/footprint.svg`, then `<dir>/.cc-svg.json` |

- Check: `DIALOG_FOOTPRINT_CHECKER::runChecks` (dialog_footprint_checker.cpp 88) in its order with
  collecting callbacks in place of markers: `BuildCourtyardCaches` (DRCE_MALFORMED_COURTYARD),
  missing courtyard, `CheckFootprintAttributes` (DRCE_FOOTPRINT_TYPE_MISMATCH), `CheckPads`,
  `CheckShortingPads`, for a net tie footprint `CheckNetTiePadGroups` (DRCE_FOOTPRINT) and
  `CheckNetTies` (DRCE_SHORTING_ITEMS), `CheckClippedSilk` (DRCE_SILK_MASK_CLEARANCE). `code` =
  `DRC_ITEM::GetSettingsKey()`; severity from the board's design settings (an ignored code is left
  out, as the dialog's tree hides it); `x`, `y` null for the three findings on the footprint itself.
  The keys this code can answer (drc_item.cpp, read 2026-11-18): `malformed_courtyard`,
  `missing_courtyard`, `footprint_type_mismatch`, from `PAD::CheckPad` `padstack`,
  `padstack_invalid`, `through_hole_pad_without_hole`, from `CheckShortingPads` `holes_co_located`,
  `hole_to_hole`, `shorting_items`, then `footprint`, `shorting_items` (net ties) and
  `silk_over_copper`. The spec's `pad_th_with_no_hole`, `drilled_holes_colocated` and `drilled_holes_too_close` are
  KiCad's enum names; their settings keys are `through_hole_pad_without_hole`, `holes_co_located` and `hole_to_hole` (fp-s10 and fp-i3 must word these). The train
  records the exact list CHECKME answers for fp-s10.
- Cleanup: the answer is read at once by a dry run (`GRAPHICS_CLEANER::CleanupBoard( true, ...)`
  on the footprint's graphics, tolerance 2 mm as the dialog first shows it, board outlines off)
  plus a count of duplicate uuids (`RepairFootprint`'s body, counting only). Without `dryRun` the
  same runs on the coroutine in ONE commit "Tidy footprint" (cleaner, then the uuid renewal), none
  when nothing changes. Cleanup codes (CLEANUP_ITEM keeps no settings key; ours from the enum):
  `null_graphic`, `duplicate_graphic`, `lines_to_rect`, `merge_pad`. KiCad 10's `RepairFootprint`
  renews duplicate uuids only (the spec's "missing nets dropped" is not in its body).
- SVG: `{dir: "/tmp/cc-svg/<n>"}`; on the coroutine KiCad's `PCB_PLOTTER` plots the editor's own
  board (the footprint alone, at the origin) to `<dir>/footprint.svg` in one file: the layers the
  footprint draws on that the view shows, in `SeqStackupForPlotting` order, plot options from a
  `JOB_EXPORT_PCB_SVG` with the footprint SVG job's defaults through
  `PCB_PLOTTER::PlotJobToPlotOpts`, then `doFpExportSvg`'s fixes (no frame, page fitted, no
  mirror, NPTH plotted). The board SVG job is used, not `JOB_FP_EXPORT_SVG`: `PlotJobToPlotOpts`
  static casts every SVG job to `JOB_EXPORT_PCB_SVG`, which the footprint job is not. Then
  `.cc-svg.json` last: `{"error": ""}` or `{"error": "plot_failed"}`.

What the build train must probe (fpedit frame):

1. On `CHECKME.kicad_mod`: `kicadFpEditCheck()` lists `missing_courtyard`, `through_hole_pad_without_hole`,
   `shorting_items` and the silk key (expected `silk_over_copper`); record the exact list. On the
   SOIC: no `missing_courtyard`. Wrong if `findings` is empty on CHECKME or a code is "".
2. `kicadFpEditCleanup('{"mergeRects":true,"deleteRedundant":true,"mergePads":false,"repair":true,"dryRun":true}')`
   on a footprint with a zero length line: `items` holds `null_graphic` with its uuid, depth
   unchanged. The same with `dryRun` false: the same items, depth +1, the line gone, Ctrl+Z brings
   it back. On the SOIC both answer `{"items":[],"repaired":0}` and depth is unchanged.
3. `kicadFpEditPlotSvg('{"dir":"/tmp/cc-svg/1"}')`: true; `.cc-svg.json` reads `{"error":""}` and
   `footprint.svg` starts `<?xml` or `<svg` with the eight SOIC pads drawn. Wrong if the record is
   written before the svg or reads `plot_failed` (a PCB_PLOTTER link gap would show here). A dir
   outside `/tmp/cc-svg/` answers false.

### fp-e5 and fp-e6: the checks

Syntax check (`gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e0-kicad all`,
on c5acacb merged with run15/main 48e664e):

```
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (12645 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (11808 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (7379 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (7764 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (1075 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (5074 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (11544 ms)
syntax-check: 0 failed
```

The fpedit unit was proved to compile this inc: a planted `fp::noSuchThing` line gave
`FAIL fpedit`, `syntax-check: 1 failed`, and was removed.

Export check (`scripts/kicad/check-run15-exports.sh --since run15/main`): `163 names registered in
the merged image (156 kicad names), 18 of 110 run 15 names, 0 failures`.

Link reach the train must watch (nothing a syntax check sees): the fpedit TU now calls
`LAYER_UTILS::GetOrphanedFootprintLayers` and `GetAllFootprintLayers` (pcbnew/layer_utils.cpp),
`GRAPHICS_CLEANER` (pcbnew/graphics_cleaner.cpp), `PCB_PLOTTER` and `JOB_EXPORT_PCB_SVG`
(pcbnew/pcb_plotter.cpp, common/jobs), `DRC_ITEM::Create`, `NULL_REPORTER::GetInstance`. All are in
the libraries the merged image links for the board editor's own plot and checks; a missing one
shows as an undefined symbol at the merged link.

## fp-e4b: the shape kinds (BUILD-NOTES-run15-fp-e4b.md)

Relay seat. Commit pcbjam `47bb3a4` (`wasm/bindings/cc_item_shape.h` new, `cc_e5_fpedit.inc`).
No new export, no marker, no host ask, no KiCad file. The five rows `shape`, `dimension`,
`zone`, `image`, `point` are registered once from `cc_e5_fpedit.inc`
(`cc_e5::kShapeKindsRegistered`, five `cc_fpitems::ccRegisterItemKind` calls) in the fpedit
TU; their bodies are inline in `cc_item_shape.h`, namespace `cc_e5::fp::shapekind`, included by
`cc_e5_fpedit.inc` alone. They are reached through e4's item exports and e3's board face via
the table only. Merged image only (the fpedit TU is merged only).

Coded to e4's widened contract of `cc_fp_items.h` (seat/run15-e4 `d22c98d`, not yet on
run15/main at this commit): get answers the record WITHOUT `kind` and `uuid`; set reads and
checks every key on a CLONE of the item first (`kindSet`), so a refused patch stages and
changes nothing, then `aCommit.Modify( item )` and the same patch on the item; add answers a
NEW item parented to the footprint (or the board item container) and not added, nullptr on a
refused record. A key of another subtype is a refusal.

The records and the KiCad code each follows (the header's comment lists every key):

- shape: `DIALOG_SHAPE_PROPERTIES::TransferDataFromWindow` (dialog_shape_properties.cpp 1160)
  and its `Validate` (1225): corner radius, fill, locked, width, style, layer, solder mask,
  margin, `RebuildBezierToSegmentsPointsList`, net -1 off copper. Refusals: segment or arc
  width 0; an unfilled circle, rect, polygon or bezier width 0; corner radius over half the
  short side; a filled segment or arc; `shape` changed; `points` of the wrong count, a
  degenerate segment, circle, rect or arc (three points on a line); `points` on a polygon of
  several outlines. Keys beyond FP 7.2's FpShape, from the dialog: `fill` (the five
  UI_FILL_MODE names; `filled` is fill != none, both given must agree), `cornerRadius` (rect),
  `hasSolderMask`, `solderMaskMargin` (null = the board's). Add: `new PCB_SHAPE( parent,
  shape )`, the layer's line thickness, LINE_STYLE::DEFAULT, no fill (the drawing tool's).
- dimension: `DIALOG_DIMENSION_PROPERTIES::updateDimensionFromDialog`
  (dialog_dimension_properties.cpp 376) in its order, then `Update()`; geometry keys `start`,
  `end`, `height`, `orientation`, `leaderLength` set first (the point editor's part).
  `textPos` refused unless the final mode is manual (a leader always takes it). `precision` is
  DIM_PRECISION's index 0 to 9. Add: `DRAWING_TOOL::DrawDimension`'s attributes (1745 to 1797),
  refused when start equals end.
- zone: `DIALOG_RULE_AREA_PROPERTIES::TransferDataFromWindow` (dialog_rule_area_properties.cpp
  419) on `ZONE_SETTINGS zs; zs << *zone;` then `zs.ExportSetting( *zone )` (as
  `FOOTPRINT_EDIT_FRAME::OnEditItemRequest`, footprint_editor_utils.cpp 225 to 243). Refusals:
  no layer, hatch pitch outside 0.1 to 2.0 mm, `ruleArea` changed, a rule area key on a filled
  zone, `points` on a zone with holes or several outlines, under 3 points. Add: a rule area
  with `ZONE_CREATE_HELPER::createNewZone`'s settings (the board's default zone settings, the
  given layers, no layer properties, net -1).
- image: `DIALOG_REFERENCE_IMAGE_PROPERTIES::TransferDataFromWindow` and
  `PANEL_IMAGE_EDITOR::CheckValues` (a scaled side under 15 pixels refused; the editor's "very
  large, are you sure" question past 6000 pixels is the page's to ask: the set is taken),
  `grayscale: true` is the editor's Convert to grayscale button (`BITMAP_BASE::
  ConvertToGreyscale`), `locked` only in the board frame (the dialog hides it in the footprint
  editor; a set of it there is refused). `png` is the PNG's base64 (the stored data when the
  picture is a PNG, else re-encoded as PNG), present only up to 64 kB, `bytes` its size. Add:
  `png` required, base64 strict, decoded at most 4 MB and starting with the PNG signature,
  read by `REFERENCE_IMAGE::ReadImageFile( wxMemoryBuffer& )` (a buffer of the exact size: that
  reader reads `GetBufSize()` bytes); `png` on a set is refused.
- point: KiCad has no point dialog; its one property is Size (pcb_point.cpp 253). Keys x, y,
  layer, size (above 0), locked. Add: `POINT_PLACER::CreateItem`'s (KiCad's 1 mm default).

Syntax check (iterating, the fpedit unit; the lane's full `all` run is in the hand over section):

```
/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e0-kicad fpedit
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (10537 ms)
syntax-check: 0 failed
```

What the build train must probe (fp-i2a's e2e on fpedit-small, through e4's item exports
`kicadFpEditItemsAdd`, `kicadFpEditItemsGet`, `kicadFpEditItemsSet`):

- add one shape of each type on F.SilkS (segment, rect, arc, circle, polygon, bezier), read
  back: `points` as given (an arc's mid may move by rounding), width, style, `filled`; each one
  undo step; Ctrl+Z removes it. A set `{width: 0}` on the segment answers false and the undo
  depth does not change. A set `{fill: "solid"}` on the circle then `{width: 0}` is taken.
- an aligned dimension `{dimType: "aligned", layer: "F.Fab", start: [0,0], end: [5,0]}`: read
  back with `height` 0 and the board's units; `{override: true, value: "X"}` then a read shows
  `value` "X"; `{textPos: [1,1]}` with mode outside answers false.
- a rule area `{kind: "zone", layers: ["F.Cu"], points: [[0,0],[3,0],[3,3]], noTracks: true}`:
  read back `ruleArea` true, `noTracks` true, `hatchPitch` within 0.1 to 2.0; `{layers: []}`
  answers false.
- a picture with mark.png on User.1: read back `bytes` equal to the file's size (KiCad keeps
  the PNG's own bytes) and `png` equal to the given base64; a set `{scale: 0.0001}` answers
  false (under 15 pixels), `{grayscale: true}` is one undo step and changes `png`; a picture
  over 64 kB reads with `bytes` and no `png`. `locked` in the footprint editor answers false.
- a point `{layer: "F.Fab"}`: `size` 1, a set `{size: 0}` answers false.
- the failure that means it is wrong: a refused set that still leaves an undo step or changes
  the item (the clone probe broken), a picture whose read `bytes` is 0 (the PNG encode of a
  JPEG or a missing PNG handler in the engine), any KiCad window.

## m13-e: the document's embedded files (BUILD-NOTES-run15-m13-e.md)

Relay seat. Commit pcbjam `0ae7a6d` (`cc_e5_pcb.inc`, `cc_e5_sch.inc`, `cc_e5_dispatch.inc`),
merged with run15/main at `2bc9ad0`. No KiCad file, no marker, no host ask.

Exports, registered in `EMSCRIPTEN_BINDINGS( cc_e5_dispatch )` (`cc_e5_dispatch.inc`), each
picking its half with `pcbEditorActive()` then `schEditorActive()` (the footprint and symbol
editor frames read false on both and answer "" or false; fp-e5's `kicadFpEditEmbed` serves the
footprint editor):

- `kicadEmbeddedFilesGet()` -> `cc_e5::embeddedFilesGet`, halves `embeddedFilesGetPcb`
  (`cc_e5_pcb.inc`) and `embeddedFilesGetSch` (`cc_e5_sch.inc`). Answers addendum 10.1's
  `{owner, fontsEmbedded, files: [{name, type, bytes, heldBy}], fonts: [{name, embedded}]}`
  (at most 1,024 files; `bytes` the decompressed size, read from a COPY of a file that holds
  only its compressed text; `heldBy` the nested sets, `RunOnNestedEmbeddedFiles`: the
  footprints or the screens' library symbols, that hold a file of that name). A read.
- `kicadEmbeddedFilesSet( json )` -> `cc_e5::embeddedFilesSet`, halves `embeddedFilesSetPcb`,
  `embeddedFilesSetSch`. Closed shapes, read whole before anything is queued: `{run, dir, op:
  "add", path, name, type}` (dir an embed run folder `/tmp/cc-embed/<n>`, `cc_output::
  IsRunDir`; path a regular file of at most 25 MB directly inside dir; name 1 to 255
  characters, no separator or control character, not `.` or `..`; type font, model,
  worksheet, datasheet, other), `{op: "remove", name, alsoHeld}`, `{op: "fonts", embed}`; the
  last two may also carry `run` and `dir` (both or neither) for the record. A removal of a name
  the document does not hold answers false. On the apply coroutine: PANEL_EMBEDDED_FILES' own
  steps (common/dialogs/panel_embedded_files.cpp): an add by `EMBEDDED_FILES::AddFile(
  wxFileName, true )` into a staging set (KiCad's read, ZSTD and hash), moved in under the
  page's name and type (a file of that name replaced: the panel's Overwrite); a removal with
  `alsoHeld` removes the nested copies first (TransferDataFromWindow 222 to 236, its "Delete
  from footprints (symbols) as well?"); fonts drops the FONT files (onFontEmbedClick 278 to
  288), with `embed` calls the document's own `EmbedFonts()` (BOARD::EmbedFonts board.cpp
  3178, SCHEMATIC::EmbedFonts), then `SetAreFontsEmbedded`. NO undo step; the frame's
  `OnModify()`; then `<dir>/embedded.json` `{files, error: "" | "not_applied"}` written last
  when a dir was given.
- `kicadEmbeddedFileExtract( json )` -> `cc_e5::embeddedFileExtract`, halves
  `embeddedFileExtractPcb`, `embeddedFileExtractSch`. Exactly `{dir: "/tmp/cc-embed-out/<n>",
  name}`: the file decompressed from a copy (`EMBEDDED_FILES::DecompressAndDecode`) and written
  to `<dir>/<name>`, then `<dir>/.cc-embed-out.json` `{name, bytes, error: "" |
  "extract_failed"}`. Synchronous (the document is not changed).

The shared rules are one block per face, `cc_e5::pcb::embfiles` and `cc_e5::sch::embfiles`
(one copy each: the two faces are separate translation units). The halves are external
linkage at `cc_e5` scope and are called only from the dispatcher, so the exports are merged
image only; the pcb face's halves compile in occ_service's unit too (the `occ` check passes)
and are simply unused there.

Font names without FreeType: `font/outline_font.h` includes `<freetype/freetype.h>`, which the
embind compile cannot find (the first syntax check of this unit FAILED pcbnew, eeschema and
occ on `'freetype/freetype.h' file not found`). The code does not include it: the font list
walks the document's texts as its own `GetFonts()` walks them (the board's drawings; every
sheet's items), keeps a font only when `GetFonts()` (KiCad's embedding permission filter)
holds it, comparing the two pointers as addresses (OUTLINE_FONT's one public base is FONT),
and reads the file name through a pointer to FONT's protected `m_fontFileName` formed in a
derived class (`FONT_FILE_NAME`, never made). REPORTED to E0 as information: a future unit
that needs `OUTLINE_FONT` in an embind TU needs `${SYSROOT}/include/freetype2` on
`compile_embind_tu`'s include list; this unit does not.

Syntax check, after `git merge run15/main` (88b68c5, e4's fp-e-items and the item kind
table's definitions on it), the lane's whole tree at `2bc9ad0`:

```
/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e0-kicad all
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (14594 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (15238 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (8222 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (10420 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (1501 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (5911 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (13764 ms)
syntax-check: 0 failed
scripts/kicad/check-run15-exports.sh --since run15/main
check-run15-exports: 225 names registered in the merged image (218 kicad names), 80 of 110 run 15 names, 0 failures
```

What the build train must probe (the unit's recipe):

- open kicad-source-mirror's `qa/data/pcbnew/issue22102.kicad_pcb` in /viewer, Edit mode,
  Board tab: `kicadEmbeddedFilesGet()` answers `owner` "board" and lists
  `SchematicTemplate.kicad_wks` with `type` "worksheet" and `bytes` above 0 (0 or -1 means the
  copy's decompress failed).
- `kicadEmbeddedFileExtract('{"dir":"/tmp/cc-embed-out/1","name":"SchematicTemplate.kicad_wks"}')`
  is true; `/tmp/cc-embed-out/1/SchematicTemplate.kicad_wks` starts with `(kicad_wks` and its
  size equals the get's `bytes`; `.cc-embed-out.json` has `error` "".
- `kicadEmbeddedFilesSet('{"run":1,"dir":"/tmp/cc-embed/1","op":"remove","name":
  "SchematicTemplate.kicad_wks","alsoHeld":false}')` is true; after the apply queue empties
  `/tmp/cc-embed/1/embedded.json` lists no file, a get lists none, the undo depth did not move,
  the board is modified. A second identical call answers false (the name is gone).
- add: stage a PDF at `/tmp/cc-embed/2/sheet.pdf`, `{run:2, dir:"/tmp/cc-embed/2", op:"add",
  path:"/tmp/cc-embed/2/sheet.pdf", name:"datasheet.pdf", type:"datasheet"}`: a get lists
  `datasheet.pdf` of type datasheet and the PDF's size. A path outside the dir answers false.
- fonts, on the embedded-fonts fixture: the get answers `fontsEmbedded` true and Noto Sans
  `embedded` true; `{op:"fonts", embed:false}` removes the font file and the switch; `{op:
  "fonts", embed:true}` brings both back. A save after it carries the font.
- the same three calls in the schematic frame answer `owner` "schematic".
- the failure that means it is wrong: an undo step after a set, the board not modified, a
  file in `heldBy` 0 that a footprint visibly carries, any KiCad window.

## st-placefile-e: a staged sheet or board onto the pointer (BUILD-NOTES-run15-st-placefile-e.md)

Relay seat. Commit pcbjam `7399336` (`cc_e5_pcb.inc`, `cc_e5_sch.inc`, `cc_e5_dispatch.inc`);
its syntax check and export check are the hand over's below (the tree at `b1db0fd`). No KiCad
file, no marker, no host ask.

`kicadPlaceFromFile( json )` -> `cc_e5::placeFromFile`, registered in `EMSCRIPTEN_BINDINGS(
cc_e5_dispatch )`; halves `placeFromFilePcb` (`cc_e5_pcb.inc`) and `placeFromFileSch`
(`cc_e5_sch.inc`), picked by `pcbEditorActive()` then `schEditorActive()` (the footprint and
symbol editors answer false). Merged image only (the halves are reached from the dispatcher;
the board half compiles in occ_service's unit unused).

- args exactly `{kind, path}`: the board frame takes only `kind` "board" and a `.kicad_pcb`,
  the schematic frame only "sheet" and a `.kicad_sch`; `path` a regular file of at most 25 MB
  directly inside a stage run folder `/tmp/cc-stage/<n>` (small tools spec 6.1: the island
  stages there, never in the project folder, and removes the folder after the answer). Any
  other shape, kind, extension or folder, no ready frame (an open in flight, a dialog up):
  false, nothing queued.
- board: on the apply coroutine `PCB_CONTROL::AppendBoard( *pi, path )` with the plugin
  `AppendBoardFromFile` itself picks (`PCB_IO_MGR::FindPluginTypeFromBoardPath( path,
  KICTL_KICAD_ONLY )`), in place of its file dialog; KiCad's own body loads the board into the
  live one, keeps the destination stackup, page and title block, places the new items on the
  move tool (`placeBoardItems` then `RunSynchronousAction( PCB_ACTIONS::move )`, the same road
  as `pcbPlaceImportedItem`), groups them when the design block option says so, and pushes ONE
  commit "Append Board" on the press (a cancel reverts).
- sheet: on the apply coroutine `RunAction<wxString*>( SCH_ACTIONS::importSheet, &s )` with
  `s` a function static holding the path (`SCH_DRAWING_TOOLS::ImportSheet` reads the parameter
  at sch_drawing_tools.cpp 874 and, the name given, never opens its file chooser); the sheet,
  or its contents when `m_DesignBlockChooserPanel.place_as_sheet` is off, is on the pointer;
  one commit on the press.

What AppendBoard builds that is not ours to change (pcb_control.cpp 2061 on, no KiCad file in
this lane; REPORTED to E0 by the first seat and again here):
- a `WX_PROGRESS_REPORTER` "Load PCB": its `updateUI` is already a no op in the engine
  (wx_progress_reporters.cpp 62), and the reporter is made and destroyed inside the load's
  `try` block, which runs straight through with no yield, before the move tool starts. Whether
  the wx port paints the wxProgressDialog it constructs is for the train to see (below).
- the plugin's query callback (a `KIDIALOG`) and the IO_ERROR path's `DisplayErrorMessage`:
  both are KiCad message dialogs, which e1's message ask (`CcHostMessage`, confirm.cpp and
  kidialog.cpp under `__EMSCRIPTEN__` on run15/main) now offers to the page first; the page's
  card answers them. A KiCad file that loads cleanly asks neither.
- placing as a sheet (when the user's design block option says so) makes a sheet symbol whose
  file is the staged path; the island removes that folder after the answer. The default
  option places the contents. The train must check which one the island's config gives.

What the build train must probe (the unit's recipe, st-placefile-s):
- board frame, the business card board open: stage another `.kicad_pcb` (complex_hierarchy's
  board) at `/tmp/cc-stage/1/other.kicad_pcb`, `kicadPlaceFromFile('{"kind":"board","path":
  "/tmp/cc-stage/1/other.kicad_pcb"}')` is true; its items follow the pointer; a click places
  them and the undo depth goes up by exactly one; Ctrl+Z removes them all. Esc instead leaves
  the board as it was with no undo step.
- no window seen at any frame between the call and the click: the classifier's zero chrome,
  zero beige frames (a "Load PCB" box seen is the progress reporter's: a defect, reported
  back to E0 with its wx type).
- schematic frame, complex_hierarchy open: stage a `.kicad_sch` at `/tmp/cc-stage/2/`,
  `{"kind":"sheet","path":...}` true, the contents (or the sheet) on the pointer, one undo step
  on the click, no file chooser.
- refusals: `{"kind":"sheet"}` in the board frame, a `.kicad_sch` named as a board, a path in
  `/tmp/cc-stage/` itself or outside it, a missing file: each false, nothing queued.

## Hand over state (relay seat, 2026-11-18): every unit of the lane done

Units: fp-e7, fp-e5, fp-e6 (first seat); fp-e4b `47bb3a4`, m13-e `0ae7a6d`, st-placefile-e
`7399336` (this seat). Nothing uncommitted, nothing stashed; no repair note `fix/e5-*.md` was
ever written. Branch `seat/run15-e5`, run15/main merged at `b1db0fd` (run15/main `ba5656b`).

Exports of the lane (13): `kicadFootprintForEditor`, `kicadFootprintPushToBoard` (block
`cc_e5_pcb`); `kicadFootprintConvert`, `kicadEmbeddedFilesGet`, `kicadEmbeddedFilesSet`,
`kicadEmbeddedFileExtract`, `kicadPlaceFromFile` (block `cc_e5_dispatch`, merged image only);
`kicadFpEditFootprintGet`, `kicadFpEditFootprintSet`, `kicadFpEditEmbed`, `kicadFpEditCheck`,
`kicadFpEditCleanup`, `kicadFpEditPlotSvg` (block `cc_e5_fpedit`, merged image only). The five
shape kinds of the item table register from `cc_e5_fpedit.inc` with no export.

Reported to E0 (information, nothing blocks): `font/outline_font.h` cannot be included by an
embind TU (`<freetype/freetype.h>` not on `compile_embind_tu`'s include list; m13-e reads font
names without it); AppendBoard's "Load PCB" progress reporter is made and destroyed with no
yield in the engine, for the train to watch (st-placefile-e's section). No include path,
CMake line, build script line or new name is needed by this lane.

The hand over checks, on `b1db0fd`:

```
cd /home/matthew/cc-engine-wt/e5/pcbjam
/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e0-kicad all
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (12472 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (12693 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (6825 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (7980 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (1011 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (5186 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (12025 ms)
syntax-check: 0 failed

scripts/kicad/check-run15-exports.sh --since run15/main
check-run15-exports: 227 names registered in the merged image (220 kicad names), 82 of 111 run 15 names, 0 failures
```

## Repairs after build T3 (fix notes e5-1 and e5-2)

Merged `run15/main` (690a16c) first. Checks after both repairs, from this worktree through the gate:

- `/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e0-kicad all`: PASS pcbnew, PASS eeschema, PASS symedit, PASS fpedit, PASS dsedit, PASS kicad_editor, PASS occ, `syntax-check: 0 failed`.
- `scripts/kicad/check-run15-exports.sh --since run15/main`: 256 names registered (249 kicad names), 111 of 111 run 15 names, 0 failures.

### e5-1 (fp-e4b): a picture read back re-encoded, commit 7d4fb78

Cause: `BITMAP_BASE::ReadImageFile( wxMemoryBuffer& )` (and KiCad's file parser, which takes the same road) keeps the original bytes in `m_imageData` but never sets `m_imageType`, which stays `wxBITMAP_TYPE_INVALID`. `pngOf` asked `SaveImageData` only when the type said PNG, so every picture fell to the re-encode (195 bytes for the 12,420 byte fixture). Fix in `cc_item_shape.h` `pngOf`: `SaveImageData` into a buffer (it writes the stored original when it holds one, else KiCad's own PNG encode), keep it when it starts with the PNG signature, else re-encode the wxImage as PNG (a JPEG original). After `{grayscale: true}` KiCad clears the stored data, so `png` still changes there, as before. No change of contract.

Probe: the fp-e4b add of `mark.png` then `kicadFpEditItemsGet`: `bytes` 12420 and `png` equal to the given base64; a footprint read from a file with a PNG picture gives its file's bytes; grayscale still one step and `png` changed.

### e5-2 (st-placefile-e): the Continue Load Schematic box, commit ec260a0

Cause: `SCH_EDIT_FRAME::LoadSheetFromFile` (`eeschema/sheet.cpp`) builds its own `KICAD_MESSAGE_DIALOG` question boxes, all titled "Continue Load Schematic" (another project's file, library names missing from a table, a conflicting library name, an older file without symbol instances), never through a host ask. The stage folder is always outside the project folder, so the contents road meets the "different project" box every time. sheet.cpp is no lane's file, so the fix stays in this lane's own inc: `cc_e5::sch::placefile::CONTINUE_LOAD_HOOK`, a `wxModalDialogHook` (wx's own hook, called at the top of `wxGenericMessageDialog::ShowModal`), registered for the span of the `importSheet` `RunAction` on the coroutine only. Its `Enter` answers a dialog titled `_( "Continue Load Schematic" )` with `wxID_OK` (KiCad's Continue Load, the reader having chosen the file) and returns `wxID_NONE` for every other modal, which goes on as before. Placing the contents, `LoadSheetFromFile` runs inside that call before the tool's first wait; placing a sheet, the drawn sheet loads the file later through `ccChangeSheetFile` or `EditSheetProperties`, which pass `aSkipLibCheck` true, so only the older file box could show there (not met by the fixtures).

Probe: the e5-2 recipe (complex_hierarchy_shared open, first-board staged at `/tmp/cc-stage/2/`, `kicadPlaceFromFile` sheet): true, no window, the contents on the pointer, one undo step on the click, Esc leaves no step; a later unrelated message box (any other title) still shows or reaches the host ask as before.
