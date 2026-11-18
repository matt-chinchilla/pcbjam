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

Commits: pcbjam c188111 (board face, the dispatcher's board half), 3b0e6f1 or later (see `git log
--grep fp-e7`; the footprint editor half). No KiCad change, no host ask, no marker.

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
