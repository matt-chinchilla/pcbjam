// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e5, the dispatcher face: textually included by kicad_editor_embind.cpp just
// before EMSCRIPTEN_BINDINGS( kicad_editor ). That TU is header light (include subdir
// common): no pcbnew or eeschema header here. An export of both frames declares its
// halves as externs in namespace cc_eN and picks one on pcbEditorActive(); the halves
// are defined (external linkage, never static) in this lane's own pcb, sch or fpedit inc.
//
// Every definition of the lane lives in namespace cc_e5 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

namespace cc_e5
{
// The halves, defined in this lane's board face (cc_e5_pcb.inc) and footprint editor face
// (cc_e5_fpedit.inc): external linkage, merged image only.
std::string footprintConvertPcb( std::string aArgsJson );
std::string footprintConvertFp( std::string aArgsJson );

/** kicadFootprintConvert (fp-e7): the footprint editor's half first, then the board's. */
std::string footprintConvert( std::string aArgsJson )
{
    if( fpEditorActive() )
        return footprintConvertFp( aArgsJson );

    if( pcbEditorActive() )
        return footprintConvertPcb( aArgsJson );

    return "";
}
} // namespace cc_e5

EMSCRIPTEN_BINDINGS( cc_e5_dispatch )
{
    function( "kicadFootprintConvert", &cc_e5::footprintConvert );
}
