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
} // namespace cc_e6

EMSCRIPTEN_BINDINGS( cc_e6_dsedit )
{
}

#endif // __EMSCRIPTEN__
