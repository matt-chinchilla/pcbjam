// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e6, the dispatcher face: textually included by kicad_editor_embind.cpp just
// before EMSCRIPTEN_BINDINGS( kicad_editor ). That TU is header light (include subdir
// common): no pcbnew or eeschema header here. An export of both frames declares its
// halves as externs in namespace cc_eN and picks one on pcbEditorActive(); the halves
// are defined (external linkage, never static) in this lane's own pcb, sch or fpedit inc.
//
// Every definition of the lane lives in namespace cc_e6 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

namespace cc_e6
{
// fp-e8: kicadImportGraphics' halves. The board half (board editor and footprint editor) is
// defined in cc_e6_fpedit.inc, the schematic half (schematic editor and symbol editor) in
// cc_e6_sch.inc.
std::string importGraphicsBoard( std::string aArgsJson );
std::string importGraphicsSch( std::string aArgsJson );

/**
 * kicadImportGraphics( args ): a DXF or SVG drawing into the top frame through KiCad's own
 * importer (fp-e8). The frame is picked by the top window, the footprint and symbol editors
 * first (pcbEditorActive() and schEditorActive() both read false there). Answers
 * {items, uuids, warnings}, or "" when refused or with no frame that imports.
 */
std::string importGraphics( std::string aArgsJson )
{
    if( fpEditorActive() || pcbEditorActive() )
        return importGraphicsBoard( aArgsJson );

    if( symEditorActive() || schEditorActive() )
        return importGraphicsSch( aArgsJson );

    return "";
}
} // namespace cc_e6

EMSCRIPTEN_BINDINGS( cc_e6_dispatch )
{
    emscripten::function( "kicadImportGraphics", &cc_e6::importGraphics );
}
