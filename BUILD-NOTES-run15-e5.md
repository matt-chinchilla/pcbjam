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

## Hand over state (this seat, 2026-11-18)

Done: fp-e7 (earlier seat), fp-e5, fp-e6. Not started, for the relay on this same branch, in
this order: fp-e4b, m13-e, st-placefile-e. Nothing uncommitted, nothing stashed.

Notes for the relay:

- fp-e4b: the kind bodies go in `cc_item_shape.h` (inline or static only, namespace `cc_e5::fp` or
  `cc_fpitems`, no registrar) and are registered once from `cc_e5_fpedit.inc` with
  `cc_fpitems::ccRegisterItemKind( { "shape", ... } )` and likewise `dimension`, `zone`, `image`,
  `point`. Lane e4's branch (not yet on run15/main at this hand over) widens `cc_fp_items.h` with
  the bodies' contract (get WITHOUT kind and uuid; set checks every key before it changes
  anything and calls `aCommit.Modify( item )` first; add returns a NEW item not yet added) and
  `ccItemKindName`; code to that contract and `git merge run15/main` once merged.log names it.
  The dimension keys are `DIALOG_DIMENSION_PROPERTIES::updateDimensionFromDialog`
  (dialog_dimension_properties.cpp 376).
- st-placefile-e: `PCB_CONTROL::AppendBoard( PCB_IO&, path )` is public, but its body builds a
  `WX_PROGRESS_REPORTER` ("Load PCB") and a `KIDIALOG` query callback, and on an IO_ERROR calls
  `DisplayErrorMessage` (pcb_control.cpp from 2061). With no KiCad file in this lane those cannot
  be held back here: REPORTED to E0 (whether run 14's progress reporter shows a window in the
  engine, and who owns an `#ifdef __EMSCRIPTEN__` in pcb_control.cpp if it does). The sheet half
  runs `SCH_ACTIONS::importSheet` with a `wxString*` parameter (`SCH_DRAWING_TOOLS::ImportSheet`
  reads it at sch_drawing_tools.cpp 824); keep the string alive past the call (a static). The
  island stages under `/tmp/cc-stage/<n>/` (small tools spec 6.1), so the path rule is
  `cc_output::IsRunDir( dir, "/tmp/cc-stage/" )`; both frames' halves through `cc_e5_dispatch.inc`.
