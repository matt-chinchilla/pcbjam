// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e3, the dispatcher face: textually included by kicad_editor_embind.cpp just
// before EMSCRIPTEN_BINDINGS( kicad_editor ). That TU is header light (include subdir
// common): no pcbnew or eeschema header here. An export of both frames declares its
// halves as externs in namespace cc_eN and picks one on pcbEditorActive(); the halves
// are defined (external linkage, never static) in this lane's own pcb, sch or fpedit inc.
//
// Every definition of the lane lives in namespace cc_e3 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

namespace cc_e3
{
// A marker (engine run 15, fp-e1a): this engine's footprint editor asks the page before it
// opens KiCad's item, footprint, default pad, wizard, new footprint, rename, Cleanup Graphics,
// pad tool and drawing tool windows (kicad pcbnew/tools/footprint_editor_host.h
// CcAskHostFpEdit, through window.kicadHostFpEdit.onRequest), so the page may take that
// request (the island's ev.fpedit, cap fpeditHeld).
bool kicadHostFpEdit()
{
    return true;
}
} // namespace cc_e3

EMSCRIPTEN_BINDINGS( cc_e3_dispatch )
{
    function( "kicadHostFpEdit", &cc_e3::kicadHostFpEdit );
}
