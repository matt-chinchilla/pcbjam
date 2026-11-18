// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15: lane e7's lease slot in pcbnew_embind.cpp. Included near the top of that file, above
// the body of pcbUpdateBoardFromSchematic, which lane e7 holds by lease and which sit ABOVE its cc_e7_pcb.inc (that face
// is included at the end of the file, so nothing it defines is visible from a leased body).
// Here, and only here, the lane places what a leased body calls: its own #include lines (a new
// KiCad header of the lane included), declarations in namespace cc_e7, and inline or static
// helpers. Non-inline definitions go in cc_e7_pcb.inc, with external linkage in namespace
// cc_e7. This header is compiled in every configuration of its file (the merged image and,
// for pcbnew_embind.cpp, occ_service): anything that only links in the merged image sits under
// KICAD_MERGED_EMBIND here as in the inc. No registration here.

#pragma once

#ifdef __EMSCRIPTEN__

// e-block-layout: the host's reporter for Apply Design Block Layout (K-B2), which
// ccRunBoardUpdate sets while it lays out the groups an update added. Header only (inline), so
// it is compiled in every configuration of pcbnew_embind.cpp, occ_service's included.
#include <cc_host_block_layout.h>

namespace cc_e7
{
} // namespace cc_e7

#endif // __EMSCRIPTEN__
