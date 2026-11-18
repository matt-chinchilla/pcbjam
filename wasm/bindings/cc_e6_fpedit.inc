// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e6, the footprint editor face: textually included by fpedit_embind.cpp (its own
// TU, merged image only, include subdir pcbnew, no KICAD_MERGED_EMBIND). It sees that TU's
// includes, fpedit_common.h once it lands, cc_fp_items.h and cc_output_common.h.
//
// Every definition of the lane lives in namespace cc_e6 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

namespace cc_e6
{
} // namespace cc_e6

EMSCRIPTEN_BINDINGS( cc_e6_fpedit )
{
}
