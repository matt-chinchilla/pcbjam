// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e10, the dispatcher face: textually included by kicad_editor_embind.cpp just
// before EMSCRIPTEN_BINDINGS( kicad_editor ). That TU is header light (include subdir
// common): no pcbnew or eeschema header here. An export of both frames declares its
// halves as externs in namespace cc_eN and picks one on pcbEditorActive(); the halves
// are defined (external linkage, never static) in this lane's own pcb, sch or fpedit inc.
//
// Every definition of the lane lives in namespace cc_e10 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

#include <string>

namespace cc_e10
{
// The halves, defined in cc_e10_pcb.inc and cc_e10_sch.inc (external linkage).
std::string comparePcb( std::string aArgsJson );
std::string compareSch( std::string aArgsJson );
std::string findPcb( std::string aArgsJson );
std::string findSch( std::string aArgsJson );

/**
 * kicadCompareWithSource (st-compare-e, cap compare): a placed symbol or a footprint against
 * its source, 7.4's CompareResult. The board and schematic editors only; "" in any other frame
 * (the footprint, symbol and drawing sheet editors read FALSE on both probes).
 */
std::string compareWithSource( std::string aArgsJson )
{
    if( pcbEditorActive() )
        return comparePcb( aArgsJson );

    if( schEditorActive() )
        return compareSch( aArgsJson );

    return "";
}

/**
 * kicadFind (st-find-e-e, cap find): 7.1's FindArgs over the live document, the board or the
 * schematic editor; "" in any other frame.
 */
std::string findDispatch( std::string aArgsJson )
{
    if( pcbEditorActive() )
        return findPcb( aArgsJson );

    if( schEditorActive() )
        return findSch( aArgsJson );

    return "";
}
} // namespace cc_e10

EMSCRIPTEN_BINDINGS( cc_e10_dispatch )
{
    function( "kicadCompareWithSource", &cc_e10::compareWithSource );
    function( "kicadFind", &cc_e10::findDispatch );
}
