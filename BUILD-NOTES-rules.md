# Engine run 14: properties, numbering and rules (BUILD NOTES)

Seat: the engine side of `circuits-com/docs/superpowers/specs/2026-11-16-properties-rules-design.md` (section 5 and the table "Engine exports for run 14"). Branch `pcbjam:seat/run14-rules`, two commits on `cc/main` 5f8bed4. NOT COMPILED: every line was written against the KiCad sources in `tree-cc8/kicad` (f4b38976) and read back by hand. The integrator's run 14 build is the first compile.

## Layout

The code lives in two NEW files, textually included by the two binding TUs, so this seat's lines do not collide with the other three run 14 seats in the same files:

| file | included by | where |
|---|---|---|
| `wasm/bindings/cc_rules_sch.inc` | `eeschema_embind.cpp` | just before `#ifdef KICAD_MERGED_EMBIND` (after `kicadCheckSheet`) |
| `wasm/bindings/cc_rules_pcb.inc` | `pcbnew_embind.cpp` | just before `EMSCRIPTEN_BINDINGS(pcbnew)` |

Each `.inc` uses the anonymous-namespace helpers of its TU (named at its head), so it must stay included where it is, after those helpers. The build compiles only `<app>_embind.cpp`; a `.inc` is not a TU and needs no build script change. The quote include resolves next to the including file.

Registration (no build flags or export lists: embind `function(...)` registrations are all it takes):

- `eeschema_embind.cpp` `EMSCRIPTEN_BINDINGS(eeschema)`, outside the merged guard (eeschema-only names): `kicadSymbolsGet`, `kicadSymbolsSet`, `kicadSymbolsExchange`, `kicadSchEdit`. Inside `#ifndef KICAD_MERGED_EMBIND` (standalone image only): `kicadSettingsGet` -> `schSettingsGet`, `kicadSettingsSet` -> `schSettingsSet`.
- `pcbnew_embind.cpp` `EMSCRIPTEN_BINDINGS(pcbnew)`, outside the guard: `kicadBoardFootprints`, `kicadBoardEdit`, `kicadFootprintsExchange`. Inside `#ifndef KICAD_MERGED_EMBIND`: `kicadSettingsGet` -> `pcbSettingsGet`, `kicadSettingsSet` -> `pcbSettingsSet`.
- `kicad_editor_embind.cpp` (merged image, what the island runs): extern declarations of `pcbSettingsGet/Set` and `schSettingsGet/Set` and the two shims `settingsGet` and `settingsSet` dispatching on `pcbEditorActive()` / `schEditorActive()`, registered as `kicadSettingsGet` and `kicadSettingsSet`.

External (non-static) symbols this seat adds: `schSettingsGet`, `schSettingsSet`, `pcbBoardFootprints`, `pcbBoardEdit`, `pcbFootprintsExchange`, `pcbSettingsGet`, `pcbSettingsSet`. The four schematic exports are `static`. Helper names in the `.inc` files are `cc...` / `CC_...` and the generic ones carry `Rules` (`ccRulesString`, `ccRulesBool`, `ccRulesMmToIu`, `ccRulesCut`, ...); none existed in the TUs at 5f8bed4, but another seat may add one of the same name to the same TU: a redefinition error at compile is a rename, nothing more.

No kicad `cc/main` commit. The spec expected the dialogs' cores lifted into free functions in KiCad; every core needed only public KiCad API, so each is carried in the binding instead (the bindings are GPL, KiCad's code is copied freely there, and the fork stays as close to upstream as pcbjam's CLAUDE.md asks). The kicad submodule pin is unchanged.

## Run folders and records (for the island seat)

Every write follows run A: closed JSON args read whole, `false` queues nothing, the work on `pcbjam_collab::runOnCoroutine`, the report then the done file LAST through a `.part` rename.

| export | dir root | report | done file (`{error, message}`) |
|---|---|---|---|
| `kicadSymbolsSet` | `/tmp/cc-symbols/` | `symbols.json` `{applied, depth}` | `.cc-symbols.json` |
| `kicadSymbolsExchange` | `/tmp/cc-exchange/` | `exchange.json` (7.3 ExchangeReport) | `.cc-exchange.json` |
| `kicadSchEdit` | `/tmp/cc-edit/` | `edit.json` (7.5 EditReport) | `.cc-edit.json` |
| `kicadBoardEdit` | `/tmp/cc-edit/` | `edit.json` (7.5, plus `lines` for reannotate) | `.cc-edit.json` |
| `kicadFootprintsExchange` | `/tmp/cc-exchange/` | `exchange.json` | `.cc-exchange.json` |
| `kicadSettingsSet` | `/tmp/cc-settings/` | `settings.json` `{value, errors?, wouldRemove?}` | `.cc-settings.json` |

`error` in a done file is `""` on success; otherwise `island_error`, `bad_args` (a settings value the section refused on the coroutine), `no_variant` (symbols.set when the variant vanished between the call and the apply), `rules_invalid` (customRules apply with parse errors: NOTHING is written, see below), `duplicate_designator` (reannotate would give two parts one designator: nothing applied, the rows still listed). Reads (`kicadSymbolsGet`, `kicadBoardFootprints`, `kicadSettingsGet`) are synchronous and answer JSON or `""`.

## Each export

### `kicadSymbolsGet(argsJson) -> string` (cap `symbols`)
- Args exactly `{variant, scope: "all"}` or `{variant, items: [{uuid, sheetPath}]}` (1 to 4,096). A variant the design lacks answers `""` (the island's `no_variant`). An asked item the design lacks is left out.
- Answer: 7.1 `SymbolTable` `{variant, variants, rows, power, truncated, depth}`. Rows in hierarchy order (sheet by sheet; within a sheet, the screen's item order: the page sorts). Power instances counted in `power` (within the scope), never listed.
- Values: `GetValue(.., variant)`, footprint and fields through `SCH_SYMBOL::GetFieldText(name, path, variant)`; private fields left out; `fields` excludes Reference, Value, Footprint, capped 256.
- Flags: resolved as the fields table does (`Resolve*` with rule areas, OR the sheet path's own flag for the variant). `inPos` has no sheet flag in KiCad (`SCH_SHEET_PATH` has none), so it folds only rule areas.
- `sheetHeld`: a flag the resolved value forces that the symbol's own value does not (sheet or rule area).
- `overrides`: by the file writer's own test (`sch_io_kicad_sexpr.cpp`): every field the instance's variant names (Value -> `value`, Footprint -> `footprint`, else the field name) and each flag differing from the symbol's default.
- `pins`: distinct pin numbers over `GetAllLibPins()` (every unit), the count a footprint's pads match.

### `kicadSymbolsSet(argsJson) -> bool` (cap `symbols`)
- Args exactly `{run, dir, variant, changes}`; each change exactly `{uuid, sheetPath, set}`, the instance must exist, one change per instance.
- `set` keys: `ref value footprint fields addField removeField inBom onBoard inSim inPos dnp clear`, read through the existing `readSymbolSet` (so `symbol.set`'s bounds hold) plus `inPos` and `clear` read here.
- Under a variant: `ref`, `addField`, `removeField` refuse the WHOLE call (spec). ALSO refused: a `fields[]` entry carrying `shown` (KiCad's variants hold values and flags only; visibility is not per variant). `clear` is accepted only under a variant; a key both set and cleared refuses.
- Apply: ONE `SCH_COMMIT` pushed "Edit fields" (reverted, no undo step, when no change resolved). Default variant: the same setters as `kicadSymbolSet`. Variant: `SCH_SYMBOL::SetFieldText(name, text, path, variant)` (which drops an override equal to the default, KiCad's own rule) and the flag setters with the variant name. `clear`: the instance's variant copied, entries erased or flags set back to the symbol's default, `DeleteVariant` then `AddVariant`. `SyncOtherUnits(path, commit, nullptr, variant)` for a multi-unit part.

### `kicadSymbolsExchange(argsJson) -> bool` (cap `symbolExchange`)
- Args exactly `{run, dir, mode, targets, libId, fields, options, preview}`. `libId` "" for update, `lib:name` for change (read with `LIB_ID::Parse`).
- Options: the spec's eight plus TWO of KiCad's dialog checkboxes the spec lacks, accepted as optional bools: `resetPinTextVisibility` ("Reset pin name/number visibility") and `resetCustomPower` ("Reset custom power symbols"). `resetPins` maps to "Reset alternate pin assignments". Every option defaults false; KiCad's dialog defaults some on: the page sends what it wants.
- Core: `DIALOG_CHANGE_SYMBOLS::processSymbols` ported (`ccExchangeOne`), the same order (screen Remove + `commit.Modified` for every symbol before any library symbol changes).
- Status per instance row: `missing` (no library symbol, an invalid id, or too few units), `same` (update mode, the library's flattened symbol equal to the placed one by `Compare(EQUALITY | ERC)`, still processed so the reset options apply), else `changed`. `locked` never occurs on the schematic (KiCad 10 schematic items have no lock). `applied` counts symbols processed (a reused sheet's instances share one symbol; rows are per instance).
- The library symbol comes from `SCH_BASE_FRAME::GetLibSymbol(id)` on the coroutine, as PCBJam's `schUpdateFromLibrary` does (the island's library mirror or the design's own libraries).

### `kicadSchEdit(argsJson) -> bool` (cap `schEdit`)
- Args exactly `{run, dir, op: "textGraphics", preview, filter, set}`.
- 7.5's keys, mapped: `labels` = every label kind (local, global, hierarchical, directive); `wires` = wire lines and the junctions on wires; `buses` likewise; `texts` = text and text boxes; `graphics` = shapes and notes lines; `sheetPath` = one sheet path only; `refGlob` = symbols whose designator matches.
- KiCad's other dialog controls, accepted as optional extras: filter `sheetFields`, `sheetBorders`, `labelFields`, `symbolGlob`, `fieldGlob`, `netGlob`, `power` (true power only, false parts only), `selected`; set `vAlign`, `lineStyle` (`default solid dash dot dashDot dashDotDot`), `junctionSize`, `showFieldNames`. NOT carried: the label orientation choice, the font choice and every color (spec: colors never set).
- A designator, symbol or power filter skips non-symbol items (KiCad's dialog only tests symbols and lets other items through; ours keeps "the parts matching R*" literal).
- A preview reads only: each row names what would change, `from`/`to` for a size or width.

### `kicadBoardFootprints() -> string` (cap `boardFootprints`)
`{footprints: FootprintRow[], variant, truncated}`, at most 8,192, the board's current variant applied (`Get*ForVariant`, `GetFieldValueForVariant`). `side` from `IsFlipped()`. `fields` = non-mandatory fields. `symbolPath` = `GetPath().AsString()`.

### `kicadBoardEdit(argsJson) -> bool` (cap `boardEdit`)
Args exactly `{run, dir, op, preview, filter, set}`; each op's filter and set closed. Extras beyond 7.5 are KiCad's own dialog controls, listed here so the phase 2 page can offer them:

| op | 7.5 keys | KiCad's extras accepted | KiCad's controls NOT carried |
|---|---|---|---|
| `tracksVias` | filter `tracks vias nets netClass layer uuids`; set `width viaDiameter viaDrill` (mm or `"netclass"`), `layer` (copper) | filter `throughVias microVias blindVias buriedVias` (default all on), `trackWidth`, `viaSize`, `selected`; set `annularRings` (`keepAll removeExceptStartAndEnd removeAll startEndOnly`) | via protection presets (IPC-4761: the dialog's private `setViaConfiguration`) |
| `textGraphics` | filter `references values otherFields texts graphics dimensions layer refGlob uuids`; set `layer visible lineWidth height width thickness bold italic keepUpright knockout` | filter `boardItems footprintItems` (both default on), `footprintGlob`, `selected`; set `autoThickness`, `defaults` (KiCad's "Set to layer default values", `StyleFromSettings`) | font choice, "center on footprint" |
| `teardrops` | filter `roundPads rectPads vias trackToTrack uuids`; set `on lengthRatio widthRatio maxLength maxWidth curved` | filter `pthPads smdPads` (default on), `nets netClass layer selected existing`; set `allowTwoTracks preferZoneConnection` | the dialog's "remove all" (on false with every target is the same) |
| `swapLayers` | set `map` | | |
| `deleteGlobal` | filter `zones texts graphics footprints tracks vias outline layer locked` | filter `teardrops markers all unlocked` (`unlocked` default true; `locked` default false = locked items kept) | |
| `cleanupTracks` | set `mergeCollinear removeDangling removeShorts redundantVias tracksInPads` | set `danglingVias`, `refillZones`; filter `nets netClass layer uuids selected` | |
| `unusedPads` | set `vias pads keepOuter` | set `remove` (default true; false = KiCad's "Restore"), filter `uuids` (KiCad's selected only) | |
| `cleanupGraphics` | set `mergeRects fixOutline` | set `deleteRedundant`, `tolerance` (mm, default 0.01) | merge pads (Footprint Editor only) |
| `repair` | none | | |
| `reannotate` | set `order round by frontStart backStart frontPrefix backPrefix removePrefix skipLocked scope uuids` | set `removeFrontPrefix removeBackPrefix` (KiCad keeps two), `exclude` (KiCad's exclusion list, "R*, C") | |
| `zoneOrder` | set `order` | | |

Notes:
- `order` 0 to 7 is KiCad's radio order: down then right, right then down, down then left, left then down, up then right, right then up, up then left, left then up (`FrontDirectionsArray`, the back mirrored by `BackDirectionsArray`).
- `zoneOrder`: the FIRST uuid gets the highest priority (`n - 1`), the last 0. Then `BuildConnectivity` and the ratsnest, as the zone manager.
- `reannotate`: rows carry `symbolPath`, `from` the old designator, `to` the new; parts with empty or unnumbered designators keep theirs and are named in `lines` (KiCad asks "Reannotate anyway?"; we go on). A would-be duplicate stops the run (`duplicate_designator`), nothing applied. No kiway mail, no schematic touched.
- `teardrops`: `on: false` removes, `on: true` with no size key takes the board's defaults for the pad's shape (KiCad's "Add"), any size key is KiCad's "Set to specified values". Ratios are FRACTIONS (0.5 = 50 percent). Pushed with BOARD_COMMIT's default flags (its own teardrop update of the touched items), the dialog's filtered branch; the dialog's no-filter "full update" branch is not used.
- `deleteGlobal` markers: `BOARD::DeleteMARKERs()` after the push, as KiCad (not undoable in KiCad either).
- `repair`: KiCad's RepairBoard makes NO undo step (ids are rewritten in place); ours the same, `depth` unchanged. Rows are listed before an id is replaced, so a row's uuid is the OLD (duplicate) one.
- `cleanupTracks` with `refillZones`: the fill is KiCad's `FillAllZones(nullptr, nullptr, headless)` after the push: its OWN undo step (the dialog's way). Every other op is one undo step.
- Previews: the cleaners run with `aDryRun` true and their CLEANUP_ITEMs become rows (`words` = KiCad's message); every other op runs its core reading only.

### `kicadFootprintsExchange(argsJson) -> bool` (cap `footprintExchange`)
Args exactly `{run, dir, mode, match, libId, options, preview}`; `match` exactly one of `all: true`, `uuids` (1 to 8,192), `ref` glob, `value` glob, `libId`. Core `PCB_EDIT_FRAME::ExchangeFootprint` from the last footprint back (it appends the replacement), `LoadFootprint` per footprint (deleted again in a preview). Status `missing`, `same` (update mode, `FootprintNeedsUpdate` false), `locked` (STILL exchanged: KiCad's dialog exchanges locked footprints; the status only says so), else `changed`. Rows are in board order.

### `kicadSettingsGet(section) -> string` and `kicadSettingsSet(argsJson) -> bool` (cap `settings`)
Set args `{run, dir, section, value, dryRun?}`; `value` at most 1 MB of JSON. A section the frame does not hold: get `""`, set `false`.

Project-file sections are read with the nested settings' `Store()` and the JSON at fixed pointers, and set by merging the patch into that JSON and `Load()` (objects merge as RFC 7386 but a `null` anywhere refuses: a patch never deletes a key; arrays and scalars replace; a named key must keep its JSON kind):

| section | frame | settings object | wire keys -> pointer |
|---|---|---|---|
| `netClasses` | both | `PROJECT_FILE::NetSettings()` | `classes netclass_patterns netclass_assignments net_colors` |
| `ercSeverities` | sch | `SCHEMATIC::ErcSettings()` | `rule_severities` |
| `pinMap` | sch | same | `pin_map` |
| `schFormatting` | sch | `SCHEMATIC::Settings()` | `drawing` (minus `field_names`), `connection_grid_size subpart_id_separator subpart_first_id annotate_start_num` |
| `fieldTemplates` | sch | same | `field_names` -> `/drawing/field_names` |
| `textDefaults` | pcb | `BOARD_DESIGN_SETTINGS` | `defaults` |
| `constraints` | pcb | same | `rules` |
| `predefinedSizes` | pcb | same | `track_widths via_dimensions diff_pair_dimensions` |
| `teardrops` | pcb | same | `teardrop_options teardrop_parameters` |
| `checkSeverities` | pcb | same | `rule_severities` |

The keys `schFormatting` covers (spec 6 asked the seat to list them, from `panel_setup_formatting.cpp`): `drawing.default_text_size`, `default_line_thickness`, `pin_symbol_size`, `junction_size_choice`, `hop_over_size_choice`, `intersheets_ref_show`, `intersheets_ref_short`, `intersheets_ref_prefix`, `intersheets_ref_suffix`, `intersheets_ref_own_page`, `text_offset_ratio`, `overbar_offset_ratio`, `dashed_lines_dash_length_ratio`, `dashed_lines_gap_length_ratio`, `label_size_ratio`, `operating_point_overlay_v_precision`, `operating_point_overlay_v_range`, `operating_point_overlay_i_precision`, `operating_point_overlay_i_range`, and top-level `connection_grid_size`; plus the numbering keys `subpart_id_separator`, `subpart_first_id`, `annotate_start_num`. Values are in KiCad's own JSON units, as the `.kicad_pro` stores them (the `PARAM_SCALED` sizes in their scaled unit; not converted here).

Spec 10 item 6 (key names, measured from the sources): `field_names` is `schematic.drawing.field_names` (objects `{name, visible, url}`). Bus aliases are NOT in `schematic`: KiCad 10 keeps them in the PROJECT file at `schematic.bus_aliases` as an object name -> members array, and the editor holds them in `SCHEMATIC::GetAllBusAliases()`. So `busAliases` is NOT a JSON section here: get answers `{aliases: [{name, members}]}` and a set takes that same shape, WHOLE (it replaces the list, through `SCHEMATIC::SetBusAliases`, as the Buses panel does), then `RecalculateConnections(GLOBAL_CLEANUP)`.

Other sections, by their own setters:
- `textVariables` (both): get `{name: text}`; set a patch where a `null` value REMOVES the variable (the one place a null is taken).
- `variants`: sch get `{names: [{name, description}], current}`; set exactly ONE action `{add: name}`, `{copy: [from, to]}`, `{rename: [from, to]}`, `{delete: name}`, `{describe: [name, text]}`, `{current: name or ""}`. pcb get the board's names and current; pcb set always refused (the board takes variants through Update the board).
- `page`: get `{paper, width, height (mm), portrait, title, date, revision, company, comments[9], drawingSheet}` and on the schematic `pageNumbers [{sheetPath, page}]`. Set: a patch of those; `paper` is KiCad's own name (`A4`, `USLetter`, `User`, ...) checked with `PAGE_INFO::SetType`; `width`/`height` apply only to `User`; `comments` an array of up to nine, `null` keeping one; the schematic's page goes to the SHOWN sheet, every sheet with `allSheets: true`; `drawingSheet` sets `m_SchDrawingSheetFileName` or `PROJECT_FILE::m_BoardDrawingSheetFile` then `LoadDrawingSheet()`.
- `layers` (pcb): get `{copperCount, layers: [{name, userName, type, enabled}]}` over every layer (`type` is KiCad's word, `LAYER::ShowType`). Set `{copperCount?, layers?: [{name, userName?, type?, enabled?}]}`: copper layers follow `copperCount` (even, 2 to 32; a copper entry's `enabled` refuses); a user name follows the Layers panel's rules (no blank, space, quote, file-name forbidden character or "signal"). `dryRun` answers `wouldRemove`.
- `stackup` (pcb): get `{rows: [{index, sub, kind, layer, name, enabled, material, thickness, epsilon, lossTangent, color}], boardThickness}` (one row per dielectric sublayer). Set `{rows: [{index, sub?, material?, thickness?, epsilon?, lossTangent?, color?}]}`, each key refused where KiCad's own `Is*Editable` / `Has*Value` says the item does not take it; then the board thickness rebuilt from the stackup. Adding or removing layers is the `layers` section (the stackup follows with `SynchronizeWithBoard`).
- `finish` (pcb): `finish edgePlating edgeConnector (none inUse bevelled) impedanceControlled maskExpansion maskMinWidth pasteClearance (mm) pasteRatio (fraction)`; `castellated` is READ ONLY (KiCad derives it from the pads: `GetPadWithCastellatedAttrCount() > 0`).
- `customRules` (pcb): get `{text}` (at most 256 KB, the file `GetDesignRulesPath()`), set `{text}`. The text is parsed as Board Setup's "Check rules" does (cross references to KIIDs, text variables expanded, `DRC_RULES_PARSER`); errors as `{line, column, message}` (from KiCad's reporter lines `ERROR: <a href='L:C'>msg</a>` and a thrown `PARSE_ERROR`). `dryRun` writes nothing. An apply with errors writes NOTHING and answers `rules_invalid` (KiCad's panel saves bad rules anyway; the spec says a failed apply is `rules_invalid`); a clean apply writes the file then re-inits the DRC engine. The island sends `ev.saved` for the `.kicad_dru` (spec 6).

After a schematic project-file, text variable or bus alias set: the Schematic Setup OK steps less its `SaveProject()` (nothing is written to disk; the `.kicad_pro` reaches disk on the next save): `OnModify`, `CommonSettingsChanged(TEXTVARS_CHANGED)`, the two tickers, the render settings from the schematic settings, `RecalculateConnections(GLOBAL_CLEANUP)` for net classes and bus aliases, the view repainted. `RefreshOperatingPointDisplay` is NOT called (it shows simulator values).

After a board settings set: Board Setup's OK steps (`SynchronizeTuningProfileProperties`, `SynchronizeNetsAndNetClasses(true)`, `SynchronizeComponentClasses({})`, `OnModify`, `CommonSettingsChanged(TEXTVARS_CHANGED)`, the tickers), then `DRC_TOOL`'s engine `InitEngine(GetDesignRulesPath())` (a `PARSE_ERROR` swallowed: the check reports it), `InitializeClearanceCache`, a repaint and `UpdateUserInterface()`. NOT done: Board Setup's `ResetTools(MODEL_RELOAD)` and its `TA_MODEL_CHANGE` event (the router re-reads its rules on its next start; a reset of every tool mid-coroutine is the riskier call). If routing after a constraints change ignores the new rule, add `GetToolManager()->ProcessEvent(TOOL_EVENT(TC_COMMAND, TA_MODEL_CHANGE, AS_ACTIVE))` there.

## Spec 10 item 6, the two behaviors the seat was to report (read in the sources, not run)
- Layer removal (`PANEL_SETUP_LAYERS::TransferDataFromWindow`): items on a removed layer are DELETED with `BOARD::RemoveAllItemsOnLayer` and the undo list is CLEARED (`ClearUndoRedoList`, the dialog's warning: "cannot be undone"). No undo step. Ours does the same (the list is cleared only when a removed layer held items).
- Variant deletion (`SCH_EDIT_FRAME::RemoveVariant`): `SCHEMATIC::DeleteVariant(name, &commit)` and the commit pushed when not empty, so deleting a variant some symbol or sheet held IS one undo step; a variant nothing held makes none. Rename and copy take the same commit in ours (KiCad's API accepts one; the fields table dialog calls them without). `add`, `describe` and `current` make no undo step.

## Unsure: compile risk, read and list for the integrator
1. Not compiled. The likeliest breaks: a const or overload mismatch on a KiCad accessor (each one used was read in the headers at f4b38976; most are used the same way by the dialog they come from).
2. `nlohmann::json::json_pointer` with `contains(ptr)`, `at(ptr)`, `operator[](ptr)` (3.11.3 has all three). `merge_patch` on a sub-object reference.
3. `JSON_SETTINGS::Internals()` returns `JSON_SETTINGS_INTERNALS*`, used as `nlohmann::json&` (it derives from it), and `Store()` then `Load()` on a NESTED_SETTINGS: assumed to read and write that object's own document (no parent round trip), as `JSON_SETTINGS::Load` iterates its params. If a param's `Load` for a `PARAM_LAMBDA` (net classes, severities) misbehaves on a partial document, that section needs its own setter.
4. `ccParseRules` declares a local class deriving REPORTER with `HasMessage` overridden; REPORTER's `HasMessage` is virtual at `reporter.h:136`.
5. `cc_rules_pcb.inc` includes `<cleanup_item.h>`, `<collectors.h>`, `<tracks_cleaner.h>`, `<graphics_cleaner.h>`, `<teardrop/teardrop.h>`, `<board_stackup_manager/board_stackup.h>`, `<drc/drc_rule_parser.h>` (all under `pcbnew/`, on the bindings' `-I pcbnew`), `<eda_group.h>` (under `common/`, on `-I common`). None includes a header the bindings' include set cannot reach as far as read, but the run 12 lesson (an angle include deep inside a header) applies: if one fails, the error names it.
6. `cc_rules_sch.inc` includes `<bus_alias.h>`, `<sch_pin.h>`, `<sch_textbox.h>`, `<sch_sheet_pin.h>`, `<sch_render_settings.h>`, `<sch_connection.h>` (eeschema), `<refdes_utils.h>`, `<project/net_settings.h>`, `<settings/json_settings_internals.h>`, `<tool/tools_holder.h>`, `<font/font.h>`, `<page_info.h>`, `<title_block.h>`.
7. Behavior not run: the variant setters on a design with variants (Glasgow has none; the spec's `variants/` fixture is the island seat's); `SyncOtherUnits` with a variant name; `GetLibSymbol` for a change to a symbol of a library not yet loaded (it may suspend on the coroutine, as PCBJam's update does); `TEARDROP_MANAGER` and the cleaners inside the apply coroutine (they ran only inside dialogs until now); `LoadFootprint` in a preview loop over many footprints (memory: each is deleted).
8. `Compile_Ratsnest(false)` is called before the two cleaners (the dialogs' order) and after a deleting `deleteGlobal`; it touches the status bar text only with `true`.
9. `kicadSymbolsGet` scope `all` builds every row; on Glasgow 263 rows (estimate under 300 KB, spec 4). `kicadSymbolsSet` resolves through an index built once per call and once on the coroutine.

## Not built
Nothing in the table was dropped. Left for the owner or later runs: net name back-annotation (`kicadSchRenameNets`, named later by the spec); the KiCad controls listed "NOT carried" above (via protection presets, the text font choice, "center on footprint", the label orientation choice); colors by spec.
