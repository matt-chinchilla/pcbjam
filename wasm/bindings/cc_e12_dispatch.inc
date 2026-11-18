// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e12, the dispatcher face: textually included by kicad_editor_embind.cpp just
// before EMSCRIPTEN_BINDINGS( kicad_editor ). That TU is header light (include subdir
// common): no pcbnew or eeschema header here. An export of both frames declares its
// halves as externs in namespace cc_eN and picks one on pcbEditorActive(); the halves
// are defined (external linkage, never static) in this lane's own pcb, sch or fpedit inc.
//
// Every definition of the lane lives in namespace cc_e12 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

namespace cc_e12
{
// kicadSettingsRead (m8-e): the halves are defined in cc_e12_pcb.inc and cc_e12_sch.inc.
bool settingsReadPcb( std::string aArgsJson );
bool settingsReadSch( std::string aArgsJson );

// {run, dir: "/tmp/cc-settings-read/<n>", project, board?, sections} from whichever editor is
// live, as kicadSettingsGet is dispatched; false in any other frame (the footprint, symbol and
// drawing sheet editors read false on both probes).
bool settingsRead( std::string aArgsJson )
{
    if( pcbEditorActive() )
        return settingsReadPcb( aArgsJson );

    if( schEditorActive() )
        return settingsReadSch( aArgsJson );

    return false;
}
} // namespace cc_e12

EMSCRIPTEN_BINDINGS( cc_e12_dispatch )
{
    function( "kicadSettingsRead", &cc_e12::settingsRead );
}
