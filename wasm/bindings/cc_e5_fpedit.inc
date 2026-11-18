// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e5, the footprint editor face: textually included by fpedit_embind.cpp (its own
// TU, merged image only, include subdir pcbnew, no KICAD_MERGED_EMBIND). It sees that TU's
// includes, fpedit_common.h once it lands, cc_fp_items.h and cc_output_common.h.
//
// Every definition of the lane lives in namespace cc_e5 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

#include <core/mirror.h>
#include <pcbnew_settings.h>

namespace cc_e5
{
// ── fp-e7: footprint files to KiCad's text (kicadFootprintConvert, the footprint editor half) ──

// The one body, defined in this lane's board face (cc_e5_pcb.inc): the merged image links both.
std::string footprintConvertBody( const std::string& aArgsJson, FLIP_DIRECTION aFlip );

/** The footprint editor half of kicadFootprintConvert (cc_e5_dispatch.inc picks it first). */
std::string footprintConvertFp( std::string aArgsJson )
{
    FOOTPRINT_EDIT_FRAME* fr = cc_fpedit::readyFrame();

    if( !fr )
        return "";

    PCBNEW_SETTINGS* cfg = fr->GetPcbNewSettings();
    return footprintConvertBody( aArgsJson,
                                 cfg ? cfg->m_FlipDirection : FLIP_DIRECTION::TOP_BOTTOM );
}
} // namespace cc_e5

EMSCRIPTEN_BINDINGS( cc_e5_fpedit )
{
}
