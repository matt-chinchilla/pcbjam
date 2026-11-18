<!-- Copyright (C) 2026 Circuit Center Inc -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Engine run 15, lane e10: Compare with source, find and replace, net report, board corners, model files for 3D

Branches `seat/run15-e10` in pcbjam and kicad. Units in order: st-compare-e, st-find-e-e, st-len-e-e, m3-e3, 3d-e3 (3d-e3's notes, `BUILD-NOTES-run15-3d-e3.md` in its ownedPaths, are its section here).

## Hand over state after the machine fault

The seat before this one was cut off by the out of memory fault. It left UNCOMMITTED work in the kicad worktree only (K-DIFF's tags in `eeschema/lib_symbol.cpp` and `pcbnew/drc/drc_test_provider_library_parity.cpp`, the new `common/cc_diff_finding.h`), no commit, no notes and no status file. This seat read it whole, checked every REPORT site against both functions (all tagged), added one `using REPORTER::Report;` and committed it as st-compare-e's KiCad half. Nothing was stashed.

## Where every name lives

| export | face file | block | frames | merged image only |
|---|---|---|---|---|
| `kicadCompareWithSource` | `cc_e10_dispatch.inc` (halves `comparePcb` in `cc_e10_pcb.inc`, `compareSch` in `cc_e10_sch.inc`) | `cc_e10_dispatch` | board, schematic; "" elsewhere | yes (the dispatcher TU is the merged image's) |
| `kicadFind` | `cc_e10_dispatch.inc` (halves `findPcb`, `findSch`) | `cc_e10_dispatch` | board, schematic; "" elsewhere | yes (as above) |
| `kicadSchReplace` | `cc_e10_sch.inc` | `cc_e10_sch` | schematic | yes (eeschema TU) |
| `kicadNetReport` | `cc_e10_pcb.inc` | `cc_e10_pcb` | board | no: compiled and registered in occ_service too (no other TU called) |
| `kicadBoardVerticesGet`, `kicadBoardVerticesSet` | `cc_e10_pcb.inc` | `cc_e10_pcb` | board | no: as above |

No host asks, no markers. No `KICAD_MERGED_EMBIND` guard is needed: no board face body calls another TU (the dispatcher halves are defined in the board and schematic TUs and called only from the dispatcher).

## st-compare-e: K-DIFF and kicadCompareWithSource

**KiCad (kicad 70698fee).** The spec's shape was "an optional `std::vector<CC_DIFF_FINDING>*` beside the reporter" in `lib_symbol.h` and `footprint.h`. Built instead as a REPORTER subclass, so NO class header changes and the wide recompile the lane was ordered first for does not happen:

- `common/cc_diff_finding.h` (new, ours, all under `__EMSCRIPTEN__`): `CC_DIFF_FINDING { code, hasUuid, uuid, pin, field }`; `CC_DIFF_REPORTER : REPORTER` keeps one finding per `Report` carrying the tag set last, and forwards to an inner reporter when given; `CcDiffTag( REPORTER*, code, uuid?, pin?, field? )` tags only when the reporter is a `CC_DIFF_REPORTER` (a `dynamic_cast`, so KiCad's own reporters see nothing).
- `eeschema/lib_symbol.cpp` `LIB_SYMBOL::Compare`: a tag before each of its 20 REPORT sites (`#ifdef __EMSCRIPTEN__`), sentences unchanged. Codes: power, unitCount, graphicCount, graphic, pinExtra, pin, pinMissing (pin = the pin number), fieldExtra, field, fieldMissing (field = the field name), fpFilters, keywords, pinNameOffset, showPinNames, showPinNumbers, excludeSim, excludeBom, excludeBoard, excludePos.
- `pcbnew/drc/drc_test_provider_library_parity.cpp` `FOOTPRINT::FootprintNeedsUpdate`: a tag before each check group; every line a group reports (the TEST macros and the helpers `padNeedsUpdate`, `padHasOverrides`, `zoneNeedsUpdate`, `stackupNeedsUpdate`, `footprintVsBoardStackup`) carries it. Codes, the 7.4 list EXTENDED (the site and island seats must add these to `DiffCode`): `boardStackup` (KiCad calls these lines informational), `attributes` (field `type`, `allowSolderMaskBridges`, `boardOnly`), `excludePos`, `excludeBom`, `dnp` (new), `stackup` (new), `override` (new; field `clearance`, `solderMask`, `solderPaste`, `solderPasteRatio`, `zoneConnection`), `netTie` (new), `graphicCount`, `shape` (uuid = the board copy's shape), `barcodeCount` (new), `barcode` (new, uuid), `padCount`, `pad` (uuid = the board pad, pin = its number; also its override lines), `ruleAreaCount` (new), `ruleArea` (new, uuid). `text` and `model` are never sent: KiCad's comparison ignores footprint texts ("Currently we punt") and never compares models.

**pcbjam (5813f8e).** `kicadCompareWithSource( argsJson )` `{uuid, sheetPath?}` (those keys only; board: sheetPath absent or ""). Schematic: `resolveSymbol` (symbol.get's grammar), then exactly `SCH_INSPECTION_TOOL::DiffSymbol`: `PROJECT_SCH::SymbolLibAdapter`, library present AND enabled, `LoadSymbol`, both flattened, the instance's fields carried onto the schematic copy, `Compare( ERC flags )`. Board: `ccResolve` a footprint, exactly `BOARD_INSPECTION_TOOL::DiffFootprint`: `PROJECT_PCB::FootprintLibAdapter`, `LoadFootprint( keep uuids )`, `FootprintNeedsUpdate( lib, 0 )`. Answer `{status, source, findings}`: status from KiCad's own return value (`same` or `differs`), `noSource` when the library is absent or disabled or the item is gone (findings empty); `source` the LIB_ID text (`Device:R`); at most 512 findings `{code, uuid | null, pin, field}`. Findings are kept whatever the status (a `same` footprint can still carry `boardStackup` lines). `""` for any other shape, no frame, an open in flight, no such item, or a placed symbol with no library copy.

**Probe.** Glasgow, schematic frame: `Module.kicadCompareWithSource(JSON.stringify({uuid: <R4's uuid>, sheetPath: ""}))` answers `status` `same` (or `differs` with codes) and `source` `Device:R` (or the design's own nickname); the board frame on the st-compare-drift fixture (one 0603 pad turned 90 degrees): `status` `differs`, a finding `{code: "pad", uuid: <that pad's uuid>, pin: <its number>}`; a footprint whose library is missing from the table: `noSource`. Wrong if: every answer is `same` (the reporter was not passed, KiCad stopped at the first difference), a finding reads `other` (a site lost its tag), or `noSource` for a library that is present (adapter not loaded: the library bundles not yet fetched; probe again after `kicadLibsReload`).

## st-find-e-e: kicadFind and kicadSchReplace

**pcbjam (5813f8e)**, no KiCad edit. `FindArgs` read closed: `text` (1 to 256 code points), `matchCase`, `wholeWord`, `wildcards`, `regex`, and exactly the face object (`sch` 9 booleans, `pcb` 5 booleans); mode order as the find dialogs: regex, whole words, wildcards, plain; a regex that does not compile is refused. Every match is decided by KiCad's own `Matches` (the item's override, or `EDA_ITEM::Matches( text, data )` reached through a derived name for a designator, `CcTextMatcher`). `start` and `length` locate the match in `text` by `EDA_ITEM::Matches`' own rules (`matchSpan`); a wildcard match or a net name match lights the whole text. Strings cut at 1,024.

- Schematic: `SCH_SEARCH_DATA` with `searchAllFields = hidden`, `searchAllPins = pins`, `searchNetNames = nets`, `searchCurrentSheetOnly = thisSheet || selected`, `searchSelectedOnly = selected`, `replaceReferences = refs`. The walk is `SCH_FIND_REPLACE_TOOL`'s: every sheet path, a screen's items with their fields and pins (the symbol itself is not listed, its fields stand for it); a designator on EVERY path holding it, every other item on the first path only (ReplaceAll's rule). Kinds: a designator or value `symbol` (uuid the symbol, field the field name, ref the designator on that path), other fields `field` (uuid the owner: symbol, sheet or label), pins `pin` (uuid the symbol, field the pin number) or `net` when only the net name matched, labels and sheet pins `label` or `net`, text and text boxes `text`. The designator is matched on `GetRef( path )` (then with the unit letter), never through `SCH_FIELD::Matches`, which first asks the whole symbol and would list a designator for any other field's match. Not searched: graphic text inside a library symbol, markers (FindArgs has no flag for them on the schematic).
- Board: `DIALOG_FIND::search`'s walk with separate flags: footprints by designator (`refs`), value (`values`), then texts and other fields (`texts`), one hit per footprint (kind `footprint`, or `text` / `field`), the board's texts (`text`) and zone names (`other`), markers (`marker`), nets (`net`, uuid the net item's).
- At most 4,096 hits, `truncated` past that.

`kicadSchReplace( argsJson )` `{run, dir: "/tmp/cc-replace/<n>", find, with (0 to 256), targets: "all" | Hit[1..4096]}`: queued on the apply coroutine; re-runs the find with `searchAndReplace` (so KiCad's `IsReplaceable` and `replaceReferences` rules apply), a target is matched by `uuid`, `sheetPath`, `kind`, `field`; a target not found again is `skipped`; each hit `commit.Modify( item, path.LastScreen(), NO_RECURSE )` then `item->Replace( data, &path )` and `UpdateItem` (ReplaceAll's own lines); ONE `SCH_COMMIT` pushed as "Find and Replace" when anything was replaced (else reverted); `UpdateAllScreenReferences` after a designator change. Writes `replace.json {replaced, skipped, depth}` then `.cc-replace.json {error, message}` last (`error` `no_match` when nothing was replaced, else "").

**Probe.** Glasgow schematic: `kicadFind` `{text:"R4", wholeWord:true, ..., sch:{refs:true, all others false}}` answers one hit, kind `symbol`, field `Reference`, ref `R4`, start 0, length 2 (with `wholeWord` false it also lists R40 and up if the design has them); a regex `^C[0-9]+$` lists capacitors; `text` of 300 characters answers "". Board: `{text:"GND", pcb:{nets:true, ...false}}` one `net` hit. Replace: a label text replaced with `targets:"all"`, `replace.json` `replaced` = the label count, `depth` up by exactly one; one Undo restores every label. Wrong if: a value search lists the designator too (the designator path above regressed), a replace leaves two undo steps, or `.cc-replace.json` appears before `replace.json`.

## st-len-e-e: kicadNetReport

**pcbjam (5813f8e)**, no KiCad edit. `{nets?: string[1..8192]}` (that key only, or `{}`); a name matches the board's stored net name or its unescaped form; a name the board lacks is left out. `PCB_NET_INSPECTOR_PANEL::calculateNets`' own lines: connectivity items of tracks, arcs, vias and pads per net code, `GetLengthCalculationItem`, `CalculateLengthDetails` with the panel's optimisations (vias, merged tracks, traces in pads, no via in pad inference), layer detail and delay detail, run in sequence (the panel uses the thread pool). Row: `name` (as stored), `netClass` (`GetHumanReadableName`), `length` = `TrackLength` (the routed length, what viewing mode's `netLengths.ts` sums), `byLayer` (non zero layers, `LSET::Name`), `vias`, `viaLength`, `pads`, `dieLength` (mm), `delay` = `TotalDelay / IU_PER_PS` in ps, null when the board gives no delay (0). Without `nets`: every net but code 0, in net code order.

**Probe.** Glasgow board: `Module.kicadNetReport('{"nets":["GND"]}')` answers GND `length` 255.8152 mm (within 0.001 mm of viewing mode) and `vias` 121. Wrong if `length` equals viewing mode's plus the via length (the wrong figure was taken), or every `vias` is 0 (the via optimisation dropped them), or `{}` answers an empty list (connectivity not built).

## m3-e3: board corners

**pcbjam (5813f8e)**, no KiCad edit. The vertex editor pane (`pcbnew/widgets/vertex_editor_pane.cpp`) as two exports, board frame only (the footprint editor frame answers ""):

- `kicadBoardVerticesGet( args )`: `{uuid}` or the bare lowercase uuid (the addendum's recipe calls it that way). A zone (kind `copper`, `nonCopper`, `ruleArea`) or a polygon `PCB_SHAPE` (kind `polygon`). Answer `{uuid, kind, outlines: [{outline, hole, points: [{x, y}]}]}`, the pane's own order (`CIterateWithHoles`), `hole` -1 for an outline itself, board coordinates in mm (no origin transform). "" for anything else.
- `kicadBoardVerticesSet( args )`: `{uuid, outline, hole, index, x, y}` (six keys, mm, finite). Exactly the pane's `OnGridCellChange`: `BOARD_COMMIT` Modify, `SetVertex`, a zone `UnFill`, `SetNeedRefill`, `HatchBorder`, `Push( "Edit Vertex" )`, view update; no commit when the corner is already there. Answers the read back record (the island's m3-i note: "answers the read back record"). It runs AT ONCE, not on the apply coroutine, because it answers synchronously and the pane itself commits from a plain grid event; the integrator should know this differs from run A's write rule.

**Probe.** canvas-kinds fixture, board frame, the non copper zone: Get answers its outline in mm; Set with one corner moved 1 mm answers the moved point, the drawing shows it, undo depth up by one, one Undo restores it. A Set to the same point leaves the depth unchanged. Wrong if: the answer's point is not moved (SetVertex on a copy), two undo steps, or a hole index of 0 for the outline (the -1 convention lost).

## 3d-e3: the design's own model files reach the 3D service (K-3D1)

**KiCad (kicad 4ac7bb90)** `3d-viewer/3d_cache/pcbjam_model_fetch.cpp` (the dated Modified line added): `FindStagedModel` first, under `#ifdef __EMSCRIPTEN__`, `ccProjectModelRel`: a `${KIPRJMOD}/` or `$(KIPRJMOD)/` reference, or a relative path (no variable, not absolute, no URI, no `:NAME:` alias, no drive letter), backslashes read as slashes, `.` segments dropped, `..` taken back; a `..` past the project folder answers nothing. Then `<MODELS_MEMFS_ROOT>/kiprjmod/<path>` if staged, then for a VRML line only `.stp`, `.step` of the same stem (the page's rule: a STEP line never takes a VRML). A `${KIPRJMOD}` line that is not staged answers nothing; a relative line goes on to the library lookup as before. The folder name `kiprjmod` is shared with 3d-i3 (`bridge/occModels.ts`). The file is compiled into kicad_editor and occ_service. No header change.

**Probe.** Island harness, pcb frame on `tests/fixtures/models3d-small`: `project.export` `pcb_export_3d` format step: the report names no missing model for the `${KIPRJMOD}/cube.step` line, and the STEP file holds three `MANIFOLD_SOLID_BREP` bodies; a copy whose line reads `../../outside/cube.step` is reported not found and the file holds two. Wrong if the cube is missing (the stage folder or name differs from occModels.ts) or the outside path is found (the `..` rule).

## Checks

Syntax check, from `/home/matthew/cc-engine-wt/e10/pcbjam` after `git merge run15/main` (fast forward to 48e664e) and before the commits:

```
/home/matthew/cc-wave2/bin/gate.sh scripts/kicad/syntax-check-run15.sh --kicad /home/matthew/cc-engine-wt/e10/kicad all --kicad-file eeschema/lib_symbol.cpp --kicad-file pcbnew/drc/drc_test_provider_library_parity.cpp --kicad-file 3d-viewer/3d_cache/pcbjam_model_fetch.cpp
PASS pcbnew         pcbnew_embind.cpp subdir=pcbnew -DKICAD_MERGED_EMBIND (13052 ms)
PASS eeschema       eeschema_embind.cpp subdir=eeschema -DKICAD_MERGED_EMBIND (12345 ms)
PASS symedit        symedit_embind.cpp subdir=eeschema  (7546 ms)
PASS fpedit         fpedit_embind.cpp subdir=pcbnew  (7149 ms)
PASS dsedit         dsedit_embind.cpp subdir=pagelayout_editor  (1210 ms)
PASS kicad_editor   kicad_editor_embind.cpp subdir=common  (5146 ms)
PASS occ            pcbnew_embind.cpp subdir=pcbnew (12795 ms)
PASS kicad eeschema/lib_symbol.cpp (7678 ms)
PASS kicad pcbnew/drc/drc_test_provider_library_parity.cpp (6080 ms)
PASS kicad 3d-viewer/3d_cache/pcbjam_model_fetch.cpp (1882 ms)
syntax-check: 0 failed
```

Export check, same state:

```
scripts/kicad/check-run15-exports.sh --since run15/main --kicad /home/matthew/cc-engine-wt/e10/kicad --kicad-since run15/main
check-run15-exports: 160 names registered in the merged image (153 kicad names), 15 of 110 run 15 names, 0 failures
```

## Reported to E0

- No class header changed: `eeschema/lib_symbol.h` and `pcbnew/footprint.h` (in the lane's file list for K-DIFF) are untouched, so st-compare-e costs no wide recompile. The only KiCad header added is `common/cc_diff_finding.h`, included by `lib_symbol.cpp`, `drc_test_provider_library_parity.cpp` and the two lane incs (the `common` folder is on both include lists; the syntax check found it).
- `DiffCode` (spec 7.4) needs the footprint codes listed under st-compare-e for the island and site parsers.
- `kicadBoardVerticesSet` commits synchronously (see m3-e3).
- No include path, CMake line, build script line or new name needed.
