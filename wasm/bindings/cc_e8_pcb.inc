// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e8, the board face: textually included by pcbnew_embind.cpp after cc_rules_pcb.inc,
// outside any KICAD_MERGED_EMBIND guard (include subdir pcbnew; the merged image compiles
// that TU with -DKICAD_MERGED_EMBIND). It sees the TU's includes and helpers (pcbFrame(),
// the cc_output namespace, cc_fp_items.h's declarations in the merged image only) and calls them without editing them.
//
// Every definition of the lane lives in namespace cc_e8 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

namespace cc_e8
{
namespace pcb
{
constexpr size_t PAD_NETS_MAX = 32768;
}

// lo-e4-e: every footprint pad's net as the board's MAIL_PCB_GET_NETLIST reply carries it to
// the schematic's back annotation (board only footprints left out, the pad number, the
// SHORT net name, pads with no net left out): {"pads": [{ref, pad, net}], "truncated"} (at
// most 32,768). "" with no board frame or an open in flight. A read, synchronous.
std::string kicadBoardPadNets()
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return "";

    json pads = json::array();
    bool truncated = false;

    for( FOOTPRINT* footprint : fr->GetBoard()->Footprints() )
    {
        if( footprint->GetAttributes() & FP_BOARD_ONLY )
            continue;

        for( PAD* pad : footprint->Pads() )
        {
            const wxString& netname = pad->GetShortNetname();

            if( netname.IsEmpty() )
                continue;

            if( pads.size() >= pcb::PAD_NETS_MAX )
            {
                truncated = true;
                break;
            }

            json row;
            row["ref"] = toUtf8( footprint->GetReference() );
            row["pad"] = toUtf8( pad->GetNumber() );
            row["net"] = toUtf8( netname );
            pads.push_back( row );
        }
    }

    json out;
    out["pads"] = pads;
    out["truncated"] = truncated;
    return out.dump( -1, ' ', false, json::error_handler_t::replace );
}
} // namespace cc_e8

EMSCRIPTEN_BINDINGS( cc_e8_pcb )
{
    function( "kicadBoardPadNets", &cc_e8::kicadBoardPadNets );
}
