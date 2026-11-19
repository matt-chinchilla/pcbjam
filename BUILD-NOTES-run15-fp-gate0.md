<!-- Copyright (C) 2026 Circuit Center Inc -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Engine run 15, lane e4, unit fp-e0: the footprint editor's gate 0 report

A measurement report (FP spec 2026-11-16 section 4), no code. Measured 2026-11-19 on build T14,
the last build of run 15 (`out/run15-T14-keep`: pcbjam 643b408, kicad 79481591, wx d235efd,
`kicad_editor.wasm` 65,865,976 bytes, sha256 6fffbeb9...). The served wasm's sha256 was checked
equal to the build's.

## Verdicts

- **Item 2: plan A.** A footprint opens from its text with no library row, KiCad's pad tool
  places a pad, Ctrl+Z through `key.press` takes it back, `kicadSetChrome(false)` leaves two wx
  elements. Build G found it first; T14 confirms it. Plan B is not needed.
- **fp-f1: needed.** With the island's library mirror served, as production serves it, the
  fpedit frame's engine heap climbs from 256 to 307 MiB at `ev.ready` to 2,640 MiB
  (2,767,912,960 bytes) within 3 to 5 s after ready, and stays there; an empty pcb frame on
  the same mirror stays at 369 MiB (386,662,400 bytes). That is 2,271 MiB over the pcb row,
  far past the 50 MB of item 1's rule. Read at the instant of `ev.ready` (as the record's
  tester recipe and every train build so far read it) the heap is under the pcb row, and
  without the mirror (the island harness's default, the example library) the growth never
  happens, which is why trains 4 to 14 held fp-f1. No progress box and no `ev.menu` show
  either way. The cause is not pinned (see item 1): fp-f1's first step is to confirm it
  is `initLibraryTree` or `SyncLibraryTree` over the mirror's rows; if it lies in another
  file, the record moves to that file's author (RULINGS 5). Its done line should read the
  heap 30 s after ready with the mirror served, not at ready.

## Item 1: the fpedit frame's boot against an empty pcb frame

The island's `w2/base` (52c88a1, fp-i1 landed, so `?frame=fpedit` is the product frame, no
probe descriptor) with T14 fetched in, built local, served by `tests/serve.mjs` on 4470 and
4471, the GPU window's HTTP cache and both origins' storage cleared first. One tab at a time,
closed in a finally. `ev.ready` is the harness page's clock from navigation start. The first boot
of each kind (the cold fetch and compile) is left out; then fpedit and pcb alternate, six each.

Without the mirror (the example library):

| | fpedit | empty pcb |
|---|---|---|
| `ev.ready`, median of 6 | 1,796 ms (1,762 to 1,847) | 1,823 ms (1,742 to 1,907) |
| a second pass of 6 | 1,774 ms (1,534 to 1,886) | 1,781 ms (1,618 to 1,822) |
| `ev.menu` before ready, boxes at ready | 0, none (6 of 6) | 0, none |
| wx elements shown at ready | 2 (wxFrame, wxGLCanvas) | 2 |
| `Module.HEAP8.length` at ready and 5 s later | 268,435,456 | 386,662,400 |
| island renderer at ready, working set / private, median | 761 MB / 1,022 MB | 864 MB / 1,115 MB |

With the mirror (`LIBS_DIR` = the 10.0.4 build, the tag `PIN.json` pins):

| | fpedit | empty pcb |
|---|---|---|
| `ev.ready`, median of 6 | 1,632 ms (1,551 to 1,690) | 1,771 ms (1,503 to 1,843) |
| `ev.menu` before ready, boxes | 0, none | 0, none |
| heap at ready | 322,174,976 (256 to 307 MiB in the timeline runs) | 386,662,400 |
| heap 5 s after ready, every run (6 + 4) | **2,767,912,960** | 386,662,400 |
| island renderer 30 s after ready, working set / private | 2,832 to 2,960 MB / 3,348 to 3,482 MB | 866 MB / 1,073 MB |

The timeline (three fpedit runs and one pcb run, heap read at +0, 1, 2, 3, 5, 8, 12, 20, 30 s):
fpedit 307, 1,231, 1,871, 2,640, then 2,640 MiB to +30 s (the other two runs the same within a
second); pcb 369 MiB throughout. A 50 ms timer in the island fired up to 201 to 218 ms late in
the +2 to +3 s window (one long task), at most 68 ms late elsewhere. A cold boot (cache and
island storage cleared) fetched exactly one file under `/libs/`, `manifest.json` (10,055 bytes),
and no footprint bundle, yet reached the same 2,767,912,960 bytes: the growth is not footprint
bodies. `fpedit.open` of the SOIC still answers after it (164 to 186 ms, 8 pads).

The renderer figures are Chrome's process for the island's site, read by its Windows pid at
ready; Chrome kept that process across the closed tabs, but its figures follow the frame kind
booted (they alternate fpedit, pcb, fpedit), so they read the frame in it.

## Item 2: open by text, the pad tool, undo, chrome

Build G (pcbjam 48e664e, fp-e2's gate commit c46948b; integrator's report): `kicadFpEditOpen` of
the SOIC answers true; open, padCount 8, depth 0, unmodified, no dialog or info bar; take, open,
take byte equal (6,453 chars); `(kicad_pcb)` and garbage refused with nothing changed; 50 open
and take cycles leave `HEAP8` flat at 386,662,400.

T14, re-read in the GPU window through the island's ops (fp-i1's `fpedit.*`, `key.press`):

- `fpedit.open` of `fpedit-small`'s SOIC (`(version 20240108)`), lib `fpedit-small_parts`,
  no library row: open, padCount 8, type smd, depth 0, unmodified, no box. Round trip median
  134 ms of 5 (122 to 228); `fpedit.take` median 3 ms (3 to 7).
- KiCad's pad tool through `key.press` of the pencil table's seeded Ctrl+Alt+Shift+I, then a
  click on the canvas: pads 8 to 9, depth 0 to 1, modified, one `ev.edited`; Escape; Ctrl+Z
  through `key.press`: pads 8, depth 0. Five of five. Modified stays true after the undo
  (KiCad's own flag: an undo does not clear it), which the site's save dot must not read as
  unsaved work alone.
- Chrome: at ready `kicadChromeShown()` false and 2 wx elements shown (wxFrame, wxGLCanvas);
  `kicadSetChrome(false)` 2; `(true)` 136; `(false)` 2 again; `ev.chrome` false, true, false;
  no `ev.menu`.
- 20 open and take cycles: `HEAP8` flat at 386,662,400. The heap was 268,435,456 at ready and
  stood at that figure (the pcb row's) by the cycles, after the opens and the pad edits, as in
  build G's 50 cycles.

## Item 3: formats, through `footprint.convert` in an fpedit frame

`kicadFootprintConvert` (fp-e7) runs KiCad's own two roads: a file some plugin's
`CanReadLibrary` takes is read as a library (`FootprintEnumerate`, each `FootprintLoad`),
else the first plugin whose `CanReadFootprint` takes it reads one footprint
(`ImportFootprint`). No export calls the two checks one by one; the road column is KiCad's
source for each plugin (`GetLibraryDesc` a file or a folder) and agrees with the counts read.
Each first footprint was then opened with `fpedit.open` and taken.

| sample (bytes) | road | KiCad's format name | footprints | convert | opened |
|---|---|---|---|---|---|
| `.kicad_mod`, the mirror's SOIC-8 (4,582) | one (a KiCad library is a folder) | KiCad | 1 | 34 ms | 8 pads |
| legacy `.mod`, island fixture (729) | library | Legacy | 1 | 8 ms | 2 pads |
| gEDA `.fp`, written for this report (245) | one (a gEDA library is a folder) | gEDA / Lepton EDA | 1 | 6 ms | 2 pads |
| Eagle `.lbr`, island fixture (2,031) | library | Eagle | 1 | 24 ms | 2 pads |
| Eagle `.lbr`, KiCad qa SparkFun-GPS (190,229) | library | Eagle | 20 | 873 ms | 2 pads (the first) |
| Altium `.PcbLib`, KiCad qa Tracks v6 / v5 (838,656 / 835,584) | library | Altium Designer | 13 / 13 | 3,792 / 3,631 ms | 0 pads (a drawing test) |
| Altium `.PcbLib`, KiCad qa ESP32-WROOM-32 (236,544) | library | Altium Designer | 1 | 1,026 ms | 61 pads |
| EasyEDA Pro `.efoo`, KiCad qa PDFN-8 (13,337) | one (`.efoo` is a footprint file, not a library) | EasyEDA / JLCEDA Pro | 1 | 71 ms | 10 pads |
| EasyEDA Std `.json`, KiCad qa USB meter board (752,205) | library (the reader takes a board's JSON as the library of its footprints) | EasyEDA / JLCEDA Std | 47 | 5,779 ms | 5 pads (the first) |

No box or info bar after any of them. The convert is one synchronous call on the frame's main
thread: the Altium and EasyEDA Std samples hold the island for 3.6 to 5.8 s, so the import page
needs its own working state while it runs. No EasyEDA Std single footprint sample exists in
KiCad's qa (boards only); not read.

`.kicad_mod` text opened straight with `fpedit.open`, one per format version:

| file version (sample) | opened | pads |
|---|---|---|
| KiCad 5 `(module ... (tedit ...))`, no version (KiCad demo Crystal_SMD_HC49-US) | yes, 154 to 204 ms | 2 |
| 20211014, KiCad 6 (RPi_Pico_SMD) | yes, 140 ms | 43 |
| 20221018, KiCad 7 (219-9GULLWING) | yes, 138 ms | 18 |
| 20230517, KiCad 7 (qa CP_32_7) | yes, 139 ms | 42 |
| 20231007, KiCad 8 (qa SAM-M8Q) | yes, 147 ms | 20 |
| 20240108, KiCad 8 (fixture SOIC) | yes, 146 ms | 8 |
| 20241229, KiCad 9 (qa QFN-24) | yes, 149 ms | 25 |
| 20260206, KiCad 10 (mirror Xilinx_FFG1926..., the largest BGA) | yes, median 198 ms of 5 (188 to 221) | 1,924 |

What a take writes back, every case: `(footprint "<name>" (version 20260206) (generator
"pcbnew") (generator_version "10.0")`, KiCad 10's format, a KiCad 5 `(module` included; for
every converted format the take after open is byte equal to the convert's text. The format list
is owed to PROTOCOL.md and the import copy by their owners (fp-i4 and the copy pass).

## Item 4: the site's 3D scene over one footprint

`buildScene( { text: footprintBoard( mod ), stackup: null, quality } )` from the site's
`w2/base` (4232964dd), run in Node by the site's test runner from a scratch file never
committed; one warm up, then 9 runs; host load average about 4 to 5 from other seats.

| footprint | quality | median | min to max | of which the reader | pads | triangles |
|---|---|---|---|---|---|---|
| SOIC-8 (fixture) | full | 2.9 ms | 2.0 to 5.5 | 0.1 ms | 8 | 708 |
| SOIC-8 | reduced | 0.7 ms | 0.6 to 1.3 | 0.1 ms | 8 | 480 |
| Xilinx_FFG1926_FFG1927_FFG1928_FFG1930 (the mirror's largest BGA by pads, 243,993 bytes) | full | 445 ms | 420 to 521 | 6.2 ms | 1,924 | 81,425 |
| the same | reduced | 427 ms | 416 to 713 | 7.7 ms | 1,924 | 81,377 |
| BGA-1295_37.5x37.5mm_Layout36x36_P1.0mm (the largest square BGA) | full | 327 ms | 302 to 392 | 4.8 ms | 1,295 | 62,257 |

Every build draws (pads, silk, the fallback slab); warnings `outline-open` and `no-stackup` as
expected for a footprint alone, and `holes-marked` 424 on the Xilinx. The time is the scene
build, not the reader, and the reduced tier saves nothing on a pad dominated footprint.

Verdict for the Body window: a small footprint can be redrawn on every edit (under 6 ms). A large
BGA cannot redraw in step with edits (420 to 520 ms a build): build in the existing worker,
latest edit wins, with the previous scene kept on screen until the new one lands.

## Not measured, and why

- Run 14 at t0, as the spec first planned: run 14 has no fpedit exports (the island's fpedit
  e2e skips there, its fpedit frame never reaching `ev.ready`); every item was read on T14,
  the build that ships.
- The cause of the 2.3 GB growth: only the manifest is fetched, so it is not footprint bodies;
  which code allocates it is fp-f1's first step.
- `buildScene` in the browser's worker (read in Node on the same machine), and Firefox.
- A fresh renderer process per boot (Chrome reuses one; see item 1).
- `CanReadFootprint` and `CanReadLibrary` as separate calls (no export; the roads are read
  from KiCad's source and agree with the results).
