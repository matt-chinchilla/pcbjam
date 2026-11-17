# Engine run 14: the Symbol Editor (build notes for the integrator)

Spec: `/home/matthew/circuits-com/docs/superpowers/specs/2026-11-16-symbol-editor-design.md`, section 5 and the records of section 7.

Branches: `pcbjam:seat/run14-symbol` (this worktree, `../wt-run14-symbol-pcbjam`) and `kicad-source-mirror:seat/run14-symbol` (`../wt-run14-symbol-kicad`), both from `cc/main`. The pcbjam branch's last commit points the kicad gitlink at the kicad seat branch head; re-point it at the merged kicad `cc/main` when the four seats are merged.

## What was checked, and what was not

- Not built, not run: no engine build, no GPU window probe, no gate 0 (spec section 4). Gate 0's two questions (is the frame's constructor the only `SyncLibraries` caller at boot; does a symbol set current with no library row have its tools enabled, place a pin, take Ctrl+Z) are answered by reading only; plan A (no library row) is what was written.
- Syntax checked: every changed file compiled with `em++ -fsyntax-only` in the builder image (`kicad-wasm-cc-engine-kicad-wasm-builder`) against the run 13 build cache (mounted read-only): `symedit_embind.cpp` and `kicad_editor_embind.cpp` with the bindings' include set, `eeschema_embind.cpp` with `-DKICAD_MERGED_EMBIND`, and the seven KiCad sources with the eeschema kiface's own CMake flags. No errors. Linking was not checked.

## KiCad (`kicad-source-mirror`), three commits, every line under `__EMSCRIPTEN__`

| commit | files | what |
|---|---|---|
| K1 `8cc0bdd9` | `eeschema/symbol_editor/symbol_edit_frame.{h,cpp}`, `symbol_editor.cpp`, `eeschema/symbol_tree_synchronizing_adapter.cpp` | `SyncLibraries` never shows its progress box; the tree enumerates only rows whose uri starts `/mnt/pcbjam/prj.` (the filter sits in the adapter's Sync, where the walk is; the spec names the frame file only). The host mode: `CcHostOpen( symbol, ancestors, lib, fromSchematic, unit, bodyStyle )`, `CcHostMode()`, `CcHostLib()`, private `ccLeaveHost`, `ccFreeAncestors`, `m_ccHost`, `m_ccLib`, `m_ccAncestors`. In the host mode `storeCurrentSymbol` is a no-op, `IsSymbolFromLegacyLibrary` is false, `SetCurSymbol` dismisses the info bar and returns before every library call; `LoadOneLibrarySymbolAux`, `LoadSymbolFromSchematic` and `emptyScreen` leave the host mode first; the destructor frees the host's symbol, screen and ancestors. |
| K2 `4cca1e74` | `eeschema/symbol_checker.cpp`, new `eeschema/symbol_check_finding.h` | `SYMBOL_CHECK_FINDING` and two overloads, `CheckLibSymbol( ..., std::vector<SYMBOL_CHECK_FINDING>* )` and `CheckDuplicatePins( ..., std::vector<SYMBOL_CHECK_FINDING>* )` (overloads, not a defaulted parameter: `erc.cpp` and `sch_inspection_tool.cpp` declare the old signatures by `extern`, so a changed signature would not link). Codes: `empty_reference`, `reference_prefix_digit` (KiCad's prefix ending in a digit or `?`), `duplicate_pin`, `power_units`, `power_pins`, `power_pin_type`, `power_pin_hidden`, `hidden_power_pin` (info), `off_grid_pin`, `circle_zero_radius`, `rectangle_zero_size`. A power symbol with no pin skips the lines that read `pinList[0]`. |
| K3 `be322767` | new `eeschema/tools/symbol_editor_host.h`, `tools/symbol_editor_edit_tool.cpp`, `symbol_editor_pin_tool.cpp`, `symbol_editor_drawing_tools.cpp` | `CcAskHostSymEdit( ask, uuid, isNew )` to `window.kicadHostSymEdit.onRequest`. Asked in `SYMBOL_EDITOR_EDIT_TOOL::Properties` (pin, shape, text, textbox, field; and symbol before `editSymbolProperties`), BEFORE its `saveCopyInUndoList`, so a taken request leaves no empty undo step: this is one site for the five `edit*Properties` functions the spec lists, which are only ever reached from `Properties`. `EditPinProperties` asks for a NEW pin only (an existing pin was asked by `Properties`; asking twice would send two requests). New text (placed reading `Text`) and new text box in the drawing tools. |

## PCBJam (`pcbjam`)

| commit | files | what |
|---|---|---|
| `1e9ab40` | new `wasm/bindings/symedit_embind.cpp`, new `wasm/bindings/symedit_common.h`, `scripts/kicad/build-kicad-target.sh` | The symbol editor frame's exports, its own TU registered as `EMSCRIPTEN_BINDINGS( symedit )`, outside any `KICAD_MERGED_EMBIND` guard. The build script compiles it with the eeschema include set and adds `symedit_embind.o` to `EMBIND_OBJ` for `kicad_editor` and for a standalone `eeschema`. |
| `671c64c` | `wasm/bindings/eeschema_embind.cpp`, `wasm/bindings/kicad_editor_embind.cpp` | `kicadSymbolForEditor`, `kicadSymbolPushToSchematic` (sch frame, registered beside `kicadSymbolGet`), the marker `kicadHostSymEdit` beside the other host markers. |
| `eab79b3` | `kicad` gitlink | at the kicad seat branch head. |

### Each export

| export | file | answer, and choices the spec left open |
|---|---|---|
| `kicadSymEditOpen` | symedit_embind.cpp | bool. The document is read by KiCad's own library reader (`SCH_IO_KICAD_SEXPR::LoadSymbol`) from a scratch file `/tmp/cc-symedit/doc-<n>.kicad_sym` (removed after): the parser header pulls the generated `schematic_lexer.h`, which the bindings' include set cannot reach. The symbol and its extends chain are copied and relinked among the copies; the frame owns them. `unit` and `bodyStyle` accept 0 to 100000 and are clamped. |
| `kicadSymEditNew` | symedit_embind.cpp | bool. `LIB_SYMBOL_LIBRARY_MANAGER::CreateSymbol`; `keepParentFields` drives all four of the dialog's keep flags (footprint, datasheet, user fields, their content); `power: "local"` is `SetLocalPower` after (CreateSymbol knows global only); `inSim` set after; mode "new", unit 1, body 1. |
| `kicadSymEditState` | symedit_embind.cpp | JSON (7.1); `""` with no symbol editor frame. `lib` is the host's nickname in the host mode. `syncedPins` is `SynchronizePins()` (the mode and a symbol it applies to). |
| `kicadSymEditTake` | symedit_common.h `formatSymbol` | `{name, text, parent}`: KiCad's library header words, `SCH_IO_KICAD_SEXPR_LIB_CACHE::SaveSymbol` for each ancestor then the symbol (copies, so a pure read), `)`, then `KICAD_FORMAT::Prettify` (NORMAL). Byte stability of take, open, take is expected (one formatter both times) but not measured. |
| `kicadSymEditMarkSaved` | symedit_embind.cpp | bool; synchronous (clears the screen's flag and retitles). |
| `kicadSymEditItemsGet` | symedit_embind.cpp | `{items}` in `GetDrawItems()` order (KiCad's type buckets: fields are items there too). |
| `kicadSymEditItemsSet` | symedit_embind.cpp | bool. Allowed keys per kind are in `editableKeys`. A field's name: user fields only, one field per set, a name no other field holds. Synced pins: the pin dialog's loop ported; KiCad's loop reads a twin after removing it, this port stops at the removal. |
| `kicadSymEditItemsAdd` | symedit_embind.cpp | bool. A new `field` must carry unit 0, bodyStyle 0 and a free name; a new `text` carries `name: ""`, `showName: false`; a new `shape` carries `shape` plus unit, bodyStyle, points, stroke, fill. Layer `LAYER_DEVICE` as KiCad's drawing tools use. |
| `kicadSymEditItemsRemove` | symedit_embind.cpp | bool; the selection is cleared first. |
| `kicadSymEditSymbolGet` / `Set` | symedit_embind.cpp | Set keys: every key of 7.3's SymbolSet. `fromParent` takes the text of each named field (Update Symbol Fields' "reset text" box is always on; an empty parent text only with `resetEmpty`). `flatten`: `*symbol = *symbol->Flatten()` inside the commit. A rename never checks other symbols' names (no library row: the page owns the name space). |
| `kicadSymEditShow` | symedit_embind.cpp | bool; `SetUnit` and `SetBodyStyle` on the coroutine (they read the toolbar's unit and body combo boxes, which exist hidden). |
| `kicadSymEditSelection` | symedit_embind.cpp | `{"uuids": [...]}` (the spec says "the selection's uuids"; an object, for room to grow). `""` with no frame. |
| `kicadSymEditSelect` | symedit_embind.cpp | bool; `{uuids: string[0..4096], zoom}`; replaces the selection, `ACTIONS::zoomFitSelection` when `zoom`. |
| `kicadSymEditCheck` | symedit_embind.cpp | `{grid, findings}`; grid = the view's grid as KiCad's Symbol Checker reads it, at least 25 mil, in mm; each finding `{code, severity, pins, unit, bodyStyle, x, y, uuids}`, x and y null for a whole-symbol finding. |
| `kicadSymEditPlotSvg` | symedit_embind.cpp | bool; `<dir>/symbol.svg` then `<dir>/.cc-svg.json` last (part file renamed). `plot_failed` when the file is missing or empty (SVGPlotSymbol reports nothing). |
| `kicadSymbolLibConvert` | symedit_embind.cpp | `{format, symbols}`; `format` is `SCH_IO_MGR::ShowType` (KiCad's own reader names: "KiCad", "Legacy", "EAGLE", ...). Library tables, the pcbjam bridge, Database and HTTP are refused as unreadable. Each symbol named as KiCad's import names it (`EscapeString( name, CTX_LIBID )`). `""` for a bad shape. |
| `kicadSymbolForEditor` | eeschema_embind.cpp | `{name, libId, text}`; `name` is the lib id's item name (the name the formatter writes). |
| `kicadSymbolPushToSchematic` | eeschema_embind.cpp | bool. `libId` checked by the same rule as a footprint id (`isFootprintId`: "" or `lib:name`). The units sharing the principal's designator are found only when it is annotated (KiCad compares against an empty designator otherwise). With a relink, `SetSchSymbolLibraryName("")` too. `token` is checked (1 to 64 printable characters) and not used further. |
| `kicadHostSymEdit` | kicad_editor_embind.cpp | marker, true. |

## Things I am unsure of (watch them in the first build and its probes)

1. Record mismatches for seat I: Fill types also include `hatch`, `reverse_hatch`, `cross_hatch` (KiCad 10 stores them; 7.2 lists four). A shape of `SHAPE_T::SEGMENT` reads as a two-point `polyline`. `kicadSymEditSelection` is an object `{uuids}`.
2. `SetCurSymbol` in the host mode still calls `GetLibTree()->SelectLibId` / `Unselect` on the (empty) tree: harmless by reading, not run.
3. `SetUnit` dereferences `m_unitSelectBox` unguarded (KiCad's own code); the toolbars exist hidden in a canvas-only frame, so it should be set.
4. K1's tree filter: if the mirror's libraries were ever LOADED by another road (the schematic engine's chooser in the same engine is a different engine; the symbol editor engine has its own), nothing enters the tree now; KiCad's chooser (`Add Symbol to Schematic`, held) does not read this tree.
5. The scratch-file read (`/tmp/cc-symedit/`) relies on the sexpr plugin's cache being per plugin instance (it is: `~SCH_IO_KICAD_SEXPR` deletes `m_cache`), and on MEMFS `/tmp`.
6. `kicadSymbolPushToSchematic` re-implements `SCH_EDIT_FRAME::SaveSymbolToSchematic` (public, but it pushes "Save Symbol to Schematic" and cannot relink); the screen's library-symbol cache is refreshed by KiCad's own Remove then Append, as there.
7. The run 13 cache flags carry `-DDEBUG` for the kiface objects; the bindings get `EMBIND_CONFIG_DEFINES` from the build type as before (task #54): the new TU goes through `compile_embind_tu`, so it gets the same.
8. Memory: K1's claim (the heap at ready within 50 MB of an empty `sch` frame) is unmeasured.
