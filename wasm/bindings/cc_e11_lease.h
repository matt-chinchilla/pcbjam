// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15: lane e11's lease slot in pcbnew_embind.cpp. Included near the top of that file, above
// the cc_footprints job body, pcbExportBoardOutput and pcbResolveRules, which lane e11 holds by lease and which sit ABOVE its cc_e11_pcb.inc (that face
// is included at the end of the file, so nothing it defines is visible from a leased body).
// Here, and only here, the lane places what a leased body calls: its own #include lines (a new
// KiCad header of the lane included), declarations in namespace cc_e11, and inline or static
// helpers. Non-inline definitions go in cc_e11_pcb.inc, with external linkage in namespace
// cc_e11. This header is compiled in every configuration of its file (the merged image and,
// for pcbnew_embind.cpp, occ_service): anything that only links in the merged image sits under
// KICAD_MERGED_EMBIND here as in the inc. No registration here.

#pragma once

#ifdef __EMSCRIPTEN__

namespace cc_e11
{
} // namespace cc_e11

#endif // __EMSCRIPTEN__
