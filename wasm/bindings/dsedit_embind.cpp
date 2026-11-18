// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Embind bindings for the drawing sheet editor frame (PL_EDITOR_FRAME, --frame ds) of the merged
// kicad_editor WASM image: engine run 15, the island's dsedit.* ops. Its own TU, compiled by
// scripts/kicad/build-kicad-target.sh with the pagelayout_editor include set and WITHOUT
// -DKICAD_MERGED_EMBIND, linked into the merged image only. The seams commit leaves it empty;
// its content is lane e6's (namespace cc_e6, its own bindings block cc_e6_dsedit).

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <emscripten/bind.h>

using namespace emscripten;

namespace cc_e6
{
// Bumped by the frame's change hook (PL_EDITOR_FRAME::OnModify, below): every change of the
// sheet, by a tool, an undo or redo, or an export, counts once. kicadDsEditState reads it.
static unsigned long long g_dsChangeCount = 0;

unsigned long long dsChangeCount()
{
    return g_dsChangeCount;
}
} // namespace cc_e6


// The drawing sheet frame's change hook. The fork's pagelayout_editor/pl_editor_frame.cpp
// calls it from PL_EDITOR_FRAME::OnModify() under __EMSCRIPTEN__; the standalone pl_editor
// image defined it in pl_editor_embind.cpp (the collaboration bridge), which the merged image
// does not link. Here it only counts: no JS listener, no collaboration bridge.
extern "C" void kicadCollabOnModify()
{
    ++cc_e6::g_dsChangeCount;
}

EMSCRIPTEN_BINDINGS( cc_e6_dsedit )
{
}

#endif // __EMSCRIPTEN__
