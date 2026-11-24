<!-- Copyright (C) 2026 Circuit Center Inc -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Engine run 15, train 18, group B (item 10, item 3 for the board)

Base: pcbjam ce7e81d, kicad 9cfdc023. Branch `seat/t18-b` in both worktrees.

## Item 10. board.item.get reads a stale pad net after an undo

Fixed. kicad 78e338ac, `pcbnew/footprint.cpp`.

The cause, as read (it differs in one detail from the plan, the fix is the same):
- `ccResolve` is `BOARD::ResolveItem`, which answers from `m_itemByIdCache` first.
- A pad edit stages a Modify of the parent footprint F: the image is a clone (pads A'), the live pads A get the edit. The cache names A (BOARD::Add, at load).
- Undo calls `F.swapData( image )`, a `std::swap`: the move constructor and both move assignments re-add every child through `FOOTPRINT::Add`, whose `CacheItemById` is an `insert`, and no step uncaches (each footprint's lists are emptied by the move before its own uncache loop runs). Every insert is a no-op, so the cache keeps A, now held by the image in the undo list, with the edited net. Redo swaps A back, so it reads right by chance.
- The same swap runs in `BOARD_COMMIT::Revert`'s modify branch.
- Fix, under `__EMSCRIPTEN__` at the end of `FOOTPRINT::swapData`, when the footprint is on a board: drop every cache entry that names a child of the image (so a pad the undo took away does not stay readable from the undo list), then uncache and cache every live child (fields, pads, zones, groups, points, drawings).

The same fault elsewhere, NOT fixed (not a group B file): `PCB_TABLE::swapData` (`pcbnew/pcb_table.cpp` 79 to 106) swaps `m_cells`, and a table's cells are cached by `BOARD::Add`. After an undo of a board table edit, a cell's uuid names the cell in the undo list. The lines, after the two SetParent loops:

```
#ifdef __EMSCRIPTEN__
    if( BOARD* board = GetBoard() )
    {
        for( PCB_TABLECELL* cell : table->m_cells )
        {
            auto it = board->GetItemByIdCache().find( cell->m_Uuid );

            if( it != board->GetItemByIdCache().end() && it->second == cell )
                board->UncacheItemById( cell->m_Uuid );
        }

        for( PCB_TABLECELL* cell : m_cells )
        {
            board->UncacheItemById( cell->m_Uuid );
            board->CacheItemById( cell );
        }
    }
#endif
```

Other swapData overrides (track, via, arc, shape, text, zone, pad, group, dimension, ...) hold no cached children; a group's members are board owned, not swapped.

## Item 3, the board: cleanupTracks and cleanupGraphics previews

Fixed. pcbjam 00d01fd, `wasm/bindings/cc_rules_pcb.inc`.

- Both modes call the cleaner with `aDryRun` false. A preview then calls `ccRevertCleanup`, built from `ccHoldCleanup` (held before the run), and the rows are built after it, so every row resolves. The apply's rows fall back to the tracks held before the run (the commit owns a removed track until its Push), so they name their item too.
- What gives the board back exactly:
  - Every change of the two cleaners is in the op's commit: `m_brd->Remove` + `Removed` (CHT_DONE), merges as `Modify`, new rectangles as `Add`, graphics removals staged `Remove`. `Revert` undoes each and clears the commit, so the op's later `commit.Empty()` test pushes nothing: no undo step, depth unchanged.
  - The view: Revert adds a Removed item to the view though nothing took it out (only Push does), and a merged then removed track is added twice. Each removed track is taken out twice (the second finds the older entry, or nothing) and added once.
  - Groups and tuning patterns: `BOARD::Remove` drops a track from its group and Revert does not restore it; each removed track goes back into the group it held (by membership; the file sorts members).
  - Track order: Revert re-adds at the front. The list is set back to the held order when it holds the same tracks (`const_cast` on `Tracks()`: BOARD offers the mutable accessor only under SWIG; the list itself is not const).
  - Marks `IS_DELETED` and `SKIP_STRUCT` cleared, `BuildConnectivity` and `Compile_Ratsnest` from the board, the modified flag put back if it was clear (Revert never calls OnModify; this is a guard).
  - A throw inside the cleaner reverts the preview before it is rethrown.
- Known and unchanged: KiCad's duplicate via pass sets a via's end to its start outside the commit (it did so in the dry run too); a via's end is not saved.
- Revert also rebuilds the selection and posts SelectedItemsModified, which a preview did not do before; the collab listener's diff sees the same board.

## Checks

- `gate.sh scripts/kicad/syntax-check-run15.sh --kicad .../t18-b/kicad all --kicad-file pcbnew/footprint.cpp`: `syntax-check: 0 failed`.
- `scripts/kicad/check-run15-exports.sh --since run15/main --kicad .../t18-b/kicad --kicad-since run15/main`: `0 failures`.

## Probes for build T18

- Item 10: items-board pad `4d000000-0000-4000-8000-000000000002`, net to /SIG, Ctrl+Z: `board.item.get` reads GND (net report and saved file agree). Ctrl+Y: /SIG. Set and undo again: GND. No console error.
- Item 3: Glasgow, cleanupTracks with every option: preview count = apply count; preview rows carry real uuids and kinds; undo depth unchanged by the preview; `.kicad_pcb` saved before and after the preview byte equal; the drawing shows no doubled track (view) after a preview; apply rows carry uuids. cleanupGraphics (mergeRects, deleteRedundant, fixOutline) the same. A board with grouped tracks: the groups list the same members after a preview.
