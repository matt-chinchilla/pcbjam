// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e12, the board face: textually included by pcbnew_embind.cpp after cc_rules_pcb.inc,
// outside any KICAD_MERGED_EMBIND guard (include subdir pcbnew; the merged image compiles
// that TU with -DKICAD_MERGED_EMBIND). It sees the TU's includes and helpers (pcbFrame(),
// the cc_output namespace, cc_fp_items.h's declarations in the merged image only) and calls them without editing them.
//
// Every definition of the lane lives in namespace cc_e12 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

namespace cc_e12
{
namespace pcb
{
// ── stackupLayers (lo-e3s-e): the dielectric sublayers Board Setup's Add and Remove Dielectric
// Layer work on. Its name in kicadSettingsSections tells the island the stackup set takes
// addDielectric and removeDielectric (ccStackupDielectricEdit in cc_rules_pcb.inc).

json stackupLayersGet( PCB_EDIT_FRAME* aFrame )
{
    BOARD_STACKUP& stackup = aFrame->GetBoard()->GetDesignSettings().GetStackupDescriptor();
    json           dielectrics = json::array();
    int            index = 0;
    int            rows = 0;

    for( BOARD_STACKUP_ITEM* item : stackup.GetList() )
    {
        rows += item->GetSublayersCount();

        if( item->IsEnabled() && item->GetType() == BS_ITEM_TYPE_DIELECTRIC )
        {
            json entry = json::object();
            entry["index"] = index;
            entry["name"] = ccText( item->FormatDielectricLayerName(), 256 );
            entry["sublayers"] = item->GetSublayersCount();
            entry["removable"] = item->GetSublayersCount() > 1;
            dielectrics.push_back( std::move( entry ) );
        }

        index++;
    }

    json out = json::object();
    out["dielectrics"] = std::move( dielectrics );
    out["rows"] = rows;
    out["canAdd"] = rows < 256;
    return out;
}

bool stackupLayersSet( PCB_EDIT_FRAME* aFrame, const json& aValue, bool aDryRun, std::string& aError,
                       std::string& aMessage, json& aRecord )
{
    BOARD_DESIGN_SETTINGS& bds = aFrame->GetBoard()->GetDesignSettings();
    BOARD_STACKUP&         stackup = bds.GetStackupDescriptor();
    std::function<void()>  edit = ccStackupDielectricEdit( stackup, aValue );

    if( !edit )
        return false;

    if( !aDryRun )
    {
        edit();
        bds.SetBoardThickness( stackup.BuildBoardThicknessFromStackup() );
    }

    return true;
}
} // namespace pcb

const bool kStackupLayers = ccRegisterPcbSection( { "stackupLayers", "pcb", &pcb::stackupLayersGet,
                                                    &pcb::stackupLayersSet } );
} // namespace cc_e12

EMSCRIPTEN_BINDINGS( cc_e12_pcb )
{
}
