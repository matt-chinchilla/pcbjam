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

#include <layer_pairs.h>
#include <pcbnew_settings.h>
#include <router/pns_routing_settings.h>
#include <settings/common_settings.h>

namespace cc_e12
{
namespace pcb
{
/** An object whose every key is in aKeys (and at least one key): the closed patch rule. */
bool patchKeysIn( const json& aValue, std::initializer_list<const char*> aKeys )
{
    if( !aValue.is_object() || aValue.empty() )
        return false;

    for( const auto& [key, v] : aValue.items() )
    {
        if( std::none_of( aKeys.begin(), aKeys.end(), [&]( const char* k ) { return key == k; } ) )
            return false;
    }

    return true;
}

/** {"x": mm, "y": mm} read into a board point within +-aLimit mm. */
std::optional<VECTOR2I> readPoint( const json& aValue, double aLimit )
{
    if( !aValue.is_object() || aValue.size() != 2 || !aValue.contains( "x" ) || !aValue.contains( "y" ) )
        return std::nullopt;

    std::optional<int> x = ccRulesMmToIu( aValue["x"], -aLimit, aLimit );
    std::optional<int> y = ccRulesMmToIu( aValue["y"], -aLimit, aLimit );

    if( !x || !y )
        return std::nullopt;

    return VECTOR2I( *x, *y );
}

json pointJson( const VECTOR2I& aPoint )
{
    json out = json::object();
    out["x"] = ccMm( aPoint.x );
    out["y"] = ccMm( aPoint.y );
    return out;
}

// ── router (st-routing-e, PE11): the interactive router's own settings, PCBNEW_SETTINGS'
// tools.pns object (DIALOG_PNS_SETTINGS and the router menu edit the same object, which the
// live router holds by pointer), and the board's current track, via and diff pair sizes (the
// aux toolbar's choices, BOARD_DESIGN_SETTINGS). Keys as KiCad's JSON spells them; mode
// 0 highlight collisions, 1 shove, 2 walk around; effort 0 low, 1 medium, 2 full.

const std::vector<std::pair<std::string, std::string>>& routerKeys()
{
    static const std::vector<std::pair<std::string, std::string>> keys = {
        { "mode", "/mode" },
        { "effort", "/effort" },
        { "remove_loops", "/remove_loops" },
        { "smart_pads", "/smart_pads" },
        { "shove_vias", "/shove_vias" },
        { "suggest_finish", "/suggest_finish" },
        { "follow_mouse", "/follow_mouse" },
        { "free_angle_mode", "/free_angle_mode" },
        { "snap_to_tracks", "/snap_to_tracks" },
        { "snap_to_pads", "/snap_to_pads" },
        { "optimize_dragged_track", "/optimize_dragged_track" },
        { "auto_posture", "/auto_posture" },
        { "fix_all_segments", "/fix_all_segments" }
    };
    return keys;
}

/** The router's settings object, made as the router tool makes it when it has not run yet. */
PNS::ROUTING_SETTINGS* pnsSettings( PCB_EDIT_FRAME* aFrame )
{
    PCBNEW_SETTINGS* cfg = aFrame->GetPcbNewSettings();

    if( !cfg )
        return nullptr;

    if( !cfg->m_PnsSettings )
        cfg->m_PnsSettings = std::make_unique<PNS::ROUTING_SETTINGS>( cfg, "tools.pns" );

    return cfg->m_PnsSettings.get();
}

json routerGet( PCB_EDIT_FRAME* aFrame )
{
    PNS::ROUTING_SETTINGS* pns = pnsSettings( aFrame );

    if( !pns )
        return json();

    CC_PCB_JSON_SECTION section;
    section.settings = pns;
    section.keys = routerKeys();

    json                   out = ccPcbJsonGet( section );
    BOARD_DESIGN_SETTINGS& bds = aFrame->GetBoard()->GetDesignSettings();

    json track = json::object();
    track["index"] = bds.GetTrackWidthIndex();
    track["custom"] = bds.UseCustomTrackViaSize();
    track["width"] = ccMm( bds.GetCurrentTrackWidth() );
    track["customWidth"] = ccMm( bds.GetCustomTrackWidth() );
    out["currentTrack"] = std::move( track );

    json via = json::object();
    via["index"] = bds.GetViaSizeIndex();
    via["custom"] = bds.UseCustomTrackViaSize();
    via["diameter"] = ccMm( bds.GetCurrentViaSize() );
    via["drill"] = ccMm( bds.GetCurrentViaDrill() );
    via["customDiameter"] = ccMm( bds.GetCustomViaSize() );
    via["customDrill"] = ccMm( bds.GetCustomViaDrill() );
    out["currentVia"] = std::move( via );

    json pair = json::object();
    pair["index"] = bds.GetDiffPairIndex();
    pair["custom"] = bds.UseCustomDiffPairDimensions();
    pair["width"] = ccMm( bds.GetCurrentDiffPairWidth() );
    pair["gap"] = ccMm( bds.GetCurrentDiffPairGap() );
    pair["viaGap"] = ccMm( bds.GetCurrentDiffPairViaGap() );
    pair["customWidth"] = ccMm( bds.GetCustomDiffPairWidth() );
    pair["customGap"] = ccMm( bds.GetCustomDiffPairGap() );
    pair["customViaGap"] = ccMm( bds.GetCustomDiffPairViaGap() );
    out["currentDiffPair"] = std::move( pair );

    return out;
}

/** One current size object read whole: index (into the board's list, 0 the net class), custom,
 *  and the custom numbers; the edits in KiCad's order (an index clears custom, so it goes
 *  first). */
bool readCurrentSizes( const json& aValue, int aListSize, const char* const* aNumbers, size_t aCount,
                       std::optional<int>& aIndex, std::optional<bool>& aCustom,
                       std::vector<std::optional<int>>& aSizes )
{
    if( !aValue.is_object() || aValue.empty() )
        return false;

    aSizes.assign( aCount, std::nullopt );

    for( const auto& [key, v] : aValue.items() )
    {
        if( key == "index" )
        {
            if( !v.is_number_integer() || v.get<long long>() < 0 || v.get<long long>() >= std::max( aListSize, 1 ) )
                return false;

            aIndex = (int) v.get<long long>();
            continue;
        }

        if( key == "custom" )
        {
            if( !v.is_boolean() )
                return false;

            aCustom = v.get<bool>();
            continue;
        }

        size_t i = 0;

        while( i < aCount && key != aNumbers[i] )
            i++;

        if( i == aCount )
            return false;

        aSizes[i] = ccRulesMmToIu( v, 0.001, 100.0 );

        if( !aSizes[i] )
            return false;
    }

    return true;
}

bool routerSet( PCB_EDIT_FRAME* aFrame, const json& aValue, bool aDryRun, std::string& aError,
                std::string& aMessage, json& aRecord )
{
    PNS::ROUTING_SETTINGS* pns = pnsSettings( aFrame );

    if( !pns || !aValue.is_object() || aValue.empty() )
        return false;

    BOARD_DESIGN_SETTINGS&             bds = aFrame->GetBoard()->GetDesignSettings();
    json                               patch = json::object();
    std::vector<std::function<void()>> edits;

    for( const auto& [key, v] : aValue.items() )
    {
        if( key == "mode" || key == "effort" )
        {
            if( !v.is_number_integer() || v.get<long long>() < 0 || v.get<long long>() > 2 )
                return false;

            patch[key] = v;
        }
        else if( key == "currentTrack" )
        {
            static const char* const numbers[] = { "customWidth" };
            std::optional<int>              index;
            std::optional<bool>             custom;
            std::vector<std::optional<int>> sizes;

            if( !readCurrentSizes( v, (int) bds.m_TrackWidthList.size(), numbers, 1, index, custom, sizes ) )
                return false;

            edits.push_back( [&bds, index, custom, sizes]()
                             {
                                 if( index )
                                     bds.SetTrackWidthIndex( *index );

                                 if( sizes[0] )
                                     bds.SetCustomTrackWidth( *sizes[0] );

                                 if( custom )
                                     bds.UseCustomTrackViaSize( *custom );
                             } );
        }
        else if( key == "currentVia" )
        {
            static const char* const numbers[] = { "customDiameter", "customDrill" };
            std::optional<int>              index;
            std::optional<bool>             custom;
            std::vector<std::optional<int>> sizes;

            if( !readCurrentSizes( v, (int) bds.m_ViasDimensionsList.size(), numbers, 2, index, custom, sizes ) )
                return false;

            edits.push_back( [&bds, index, custom, sizes]()
                             {
                                 if( index )
                                     bds.SetViaSizeIndex( *index );

                                 if( sizes[0] )
                                     bds.SetCustomViaSize( *sizes[0] );

                                 if( sizes[1] )
                                     bds.SetCustomViaDrill( *sizes[1] );

                                 if( custom )
                                     bds.UseCustomTrackViaSize( *custom );
                             } );
        }
        else if( key == "currentDiffPair" )
        {
            static const char* const numbers[] = { "customWidth", "customGap", "customViaGap" };
            std::optional<int>              index;
            std::optional<bool>             custom;
            std::vector<std::optional<int>> sizes;

            if( !readCurrentSizes( v, (int) bds.m_DiffPairDimensionsList.size(), numbers, 3, index, custom,
                                   sizes ) )
            {
                return false;
            }

            edits.push_back( [&bds, index, custom, sizes]()
                             {
                                 if( index )
                                     bds.SetDiffPairIndex( *index );

                                 if( sizes[0] )
                                     bds.SetCustomDiffPairWidth( *sizes[0] );

                                 if( sizes[1] )
                                     bds.SetCustomDiffPairGap( *sizes[1] );

                                 if( sizes[2] )
                                     bds.SetCustomDiffPairViaGap( *sizes[2] );

                                 if( custom )
                                     bds.UseCustomDiffPairDimensions( *custom );
                             } );
        }
        else if( std::any_of( routerKeys().begin(), routerKeys().end(),
                              [&]( const auto& aPair ) { return aPair.first == key; } ) )
        {
            if( !v.is_boolean() )
                return false;

            patch[key] = v;
        }
        else
        {
            return false;
        }
    }

    if( aDryRun )
        return true;

    if( !patch.empty() )
    {
        CC_PCB_JSON_SECTION section;
        section.settings = pns;
        section.keys = routerKeys();

        if( !ccPcbJsonSet( section, patch ) )
            return false;
    }

    for( const std::function<void()>& edit : edits )
        edit();

    return true;
}

// ── layerPairs (st-routing-e): LAYER_PAIR_SETTINGS, the frame's layer pair presets (saved in
// the project file by KiCad's own change event) and the current pair (the routing layers a via
// joins). pairs[] { top, bottom, enabled, name } ("" for a pair with no name), current
// { top, bottom }; layers by their file names, copper layers the board enables, two different
// ones. A set replaces the list whole (SetLayerPairs, which drops a repeated pair as the dialog
// does) and sets the current pair (SetCurrentLayerPair).

json layerPairJson( const LAYER_PAIR& aPair )
{
    json out = json::object();
    out["top"] = ccLayer( aPair.GetLayerA() );
    out["bottom"] = ccLayer( aPair.GetLayerB() );
    return out;
}

json layerPairsGet( PCB_EDIT_FRAME* aFrame )
{
    LAYER_PAIR_SETTINGS* settings = aFrame->GetLayerPairSettings();

    if( !settings )
        return json();

    json pairs = json::array();

    for( const LAYER_PAIR_INFO& info : settings->GetLayerPairs() )
    {
        json entry = layerPairJson( info.GetLayerPair() );
        entry["enabled"] = info.IsEnabled();
        entry["name"] = info.GetName() ? ccText( *info.GetName(), 256 ) : std::string();
        pairs.push_back( std::move( entry ) );
    }

    json out = json::object();
    out["pairs"] = std::move( pairs );
    out["current"] = layerPairJson( settings->GetCurrentLayerPair() );
    return out;
}

std::optional<LAYER_PAIR> readLayerPair( BOARD* aBoard, const json& aValue, bool aExtraKeys )
{
    if( !aValue.is_object() || !aValue.contains( "top" ) || !aValue.contains( "bottom" )
            || !aValue["top"].is_string() || !aValue["bottom"].is_string()
            || ( !aExtraKeys && aValue.size() != 2 ) )
    {
        return std::nullopt;
    }

    const PCB_LAYER_ID top = ccLayerByName( aBoard, aValue["top"].get<std::string>() );
    const PCB_LAYER_ID bottom = ccLayerByName( aBoard, aValue["bottom"].get<std::string>() );

    if( top == UNDEFINED_LAYER || bottom == UNDEFINED_LAYER || top == bottom || !IsCopperLayer( top )
            || !IsCopperLayer( bottom ) )
    {
        return std::nullopt;
    }

    return LAYER_PAIR( top, bottom );
}

bool layerPairsSet( PCB_EDIT_FRAME* aFrame, const json& aValue, bool aDryRun, std::string& aError,
                    std::string& aMessage, json& aRecord )
{
    LAYER_PAIR_SETTINGS* settings = aFrame->GetLayerPairSettings();
    BOARD*               board = aFrame->GetBoard();

    if( !settings || !patchKeysIn( aValue, { "pairs", "current" } ) )
        return false;

    std::optional<std::vector<LAYER_PAIR_INFO>> pairs;
    std::optional<LAYER_PAIR>                   current;

    if( aValue.contains( "pairs" ) )
    {
        const json& list = aValue["pairs"];

        if( !list.is_array() || list.size() > 256 )
            return false;

        pairs.emplace();

        for( const json& row : list )
        {
            std::optional<LAYER_PAIR> pair = readLayerPair( board, row, true );

            if( !pair || !patchKeysIn( row, { "top", "bottom", "enabled", "name" } ) )
                return false;

            bool                    enabled = true;
            std::optional<wxString> name;

            if( row.contains( "enabled" ) )
            {
                if( !row["enabled"].is_boolean() )
                    return false;

                enabled = row["enabled"].get<bool>();
            }

            if( row.contains( "name" ) )
            {
                std::optional<wxString> text = ccRulesString( row["name"], 0, 128 );

                if( !text )
                    return false;

                if( !text->IsEmpty() )
                    name = *text;
            }

            pairs->emplace_back( *pair, enabled, name );
        }
    }

    if( aValue.contains( "current" ) )
    {
        current = readLayerPair( board, aValue["current"], false );

        if( !current )
            return false;
    }

    if( aDryRun )
        return true;

    if( pairs )
        settings->SetLayerPairs( *pairs );

    if( current )
        settings->SetCurrentLayerPair( *current );

    return true;
}

// ── pcbEditing and mouse (st-editprefs-e, SE39 and PE51): editor preferences, KiCad's
// Preferences panels PANEL_EDIT_OPTIONS (the board half, PCBNEW_SETTINGS) and
// PANEL_MOUSE_SETTINGS (COMMON_SETTINGS, shared by every frame of the image). App settings, not
// design settings: registered with setupOk false, so the board is never marked modified; the
// page keeps them per browser and sends them after each boot. Wire keys are KiCad's own JSON
// paths with dots ("editing.magnetic_pads"); a value keeps its JSON kind and an enumeration
// takes only the numbers KiCad's panel offers. A taken set runs what Preferences' OK runs
// (ResetTools REDRAW, then Kiway CommonSettingsChanged) without saving any file.

struct PREF_KEY
{
    const char*      wire;
    char             kind;   // 'b' bool, 'i' one of ints, 'r' an int in [lo, hi], 'd' a number in [lo, hi]
    std::vector<int> ints;
    double           lo = 0.0;
    double           hi = 0.0;
};

const std::vector<PREF_KEY>& pcbEditingKeys()
{
    static const std::vector<PREF_KEY> keys = {
        { "editing.flip_left_right", 'b', {} },
        { "editing.esc_clears_net_highlight", 'b', {} },
        { "editing.show_courtyard_collisions", 'b', {} },
        { "editing.magnetic_graphics", 'b', {} },
        { "editing.magnetic_pads", 'i', { 0, 1, 2 } },
        { "editing.magnetic_tracks", 'i', { 0, 1, 2 } },
        { "editing.track_drag_action", 'i', { 0, 1, 2 } },
        { "editing.arc_edit_mode", 'i', { 0, 1, 2 } },
        { "editing.ctrl_click_highlight", 'b', {} },
        { "editing.pcb_angle_snap_mode", 'i', { 0, 1, 2 } },
        { "editing.auto_fill_zones", 'b', {} },
        { "editing.allow_free_pads", 'b', {} },
        { "editing.rotation_angle", 'r', {}, 1, 3599 },   // tenths of a degree
        { "pcb_display.ratsnest_footprint", 'b', {} },
        { "pcb_display.ratsnest_curved", 'b', {} },
        { "pcb_display.ratsnest_thickness", 'd', {}, 0.5, 10.0 },
        { "pcb_display.show_page_borders", 'b', {} }
    };
    return keys;
}

const std::vector<PREF_KEY>& mouseKeys()
{
    // MOUSE_DRAG_ACTION as the file stores it: DRAG_ANY -2, DRAG_SELECTED -1, SELECT 0, ZOOM 1,
    // PAN 2, NONE 3; modifiers 0 or a wx key code.
    static const std::vector<PREF_KEY> keys = {
        { "input.mouse_left", 'i', { 0, -1, -2 } },
        { "input.mouse_middle", 'i', { 2, 1, 3 } },
        { "input.mouse_right", 'i', { 2, 1, 3 } },
        { "input.motion_pan_modifier", 'i', { 0, WXK_ALT, WXK_CONTROL, WXK_SHIFT } },
        { "input.scroll_modifier_zoom", 'i', { 0, WXK_ALT, WXK_CONTROL, WXK_SHIFT } },
        { "input.scroll_modifier_pan_h", 'i', { 0, WXK_ALT, WXK_CONTROL, WXK_SHIFT } },
        { "input.scroll_modifier_pan_v", 'i', { 0, WXK_ALT, WXK_CONTROL, WXK_SHIFT } },
        { "input.center_on_zoom", 'b', {} },
        { "input.auto_pan", 'b', {} },
        { "input.auto_pan_acceleration", 'r', {}, 1, 10 },
        { "input.zoom_acceleration", 'b', {} },
        { "input.zoom_speed", 'r', {}, 1, 10 },
        { "input.zoom_speed_auto", 'b', {} },
        { "input.horizontal_pan", 'b', {} },
        { "input.reverse_scroll_zoom", 'b', {} },
        { "input.reverse_scroll_pan_h", 'b', {} }
    };
    return keys;
}

CC_PCB_JSON_SECTION prefSection( JSON_SETTINGS* aSettings, const std::vector<PREF_KEY>& aKeys )
{
    CC_PCB_JSON_SECTION section;
    section.settings = aSettings;

    for( const PREF_KEY& key : aKeys )
    {
        std::string pointer = std::string( "/" ) + key.wire;
        std::replace( pointer.begin(), pointer.end(), '.', '/' );
        section.keys.emplace_back( key.wire, pointer );
    }

    return section;
}

/** aValue read whole against aKeys into a patch of KiCad's kinds; false on any other shape. */
bool prefPatch( const json& aValue, const std::vector<PREF_KEY>& aKeys, json& aPatch )
{
    if( !aValue.is_object() || aValue.empty() )
        return false;

    for( const auto& [name, v] : aValue.items() )
    {
        auto key = std::find_if( aKeys.begin(), aKeys.end(),
                                 [&]( const PREF_KEY& aKey ) { return name == aKey.wire; } );

        if( key == aKeys.end() )
            return false;

        if( key->kind == 'b' )
        {
            if( !v.is_boolean() )
                return false;

            aPatch[name] = v.get<bool>();
        }
        else if( key->kind == 'd' )
        {
            if( !v.is_number() || !std::isfinite( v.get<double>() ) || v.get<double>() < key->lo
                    || v.get<double>() > key->hi )
            {
                return false;
            }

            aPatch[name] = v.get<double>();
        }
        else
        {
            if( !v.is_number_integer() )
                return false;

            const long long n = v.get<long long>();

            if( key->kind == 'i' ? std::find( key->ints.begin(), key->ints.end(), n ) == key->ints.end()
                                 : ( n < (long long) key->lo || n > (long long) key->hi ) )
            {
                return false;
            }

            aPatch[name] = (int) n;
        }
    }

    return true;
}

/** Preferences' OK steps, no file written: the tools redraw, every frame re-reads the input. */
void prefsChanged( PCB_EDIT_FRAME* aFrame )
{
    if( TOOL_MANAGER* tools = aFrame->GetToolManager() )
        tools->ResetTools( TOOL_BASE::REDRAW );

    aFrame->Kiway().CommonSettingsChanged( 0 );
    aFrame->GetCanvas()->GetView()->UpdateAllItems( KIGFX::REPAINT );
    aFrame->GetCanvas()->Refresh();
}

json pcbEditingGet( PCB_EDIT_FRAME* aFrame )
{
    PCBNEW_SETTINGS* cfg = aFrame->GetPcbNewSettings();
    return cfg ? ccPcbJsonGet( prefSection( cfg, pcbEditingKeys() ) ) : json();
}

bool pcbEditingSet( PCB_EDIT_FRAME* aFrame, const json& aValue, bool aDryRun, std::string& aError,
                    std::string& aMessage, json& aRecord )
{
    PCBNEW_SETTINGS* cfg = aFrame->GetPcbNewSettings();
    json             patch = json::object();

    if( !cfg || !prefPatch( aValue, pcbEditingKeys(), patch ) )
        return false;

    if( aDryRun )
        return true;

    if( !ccPcbJsonSet( prefSection( cfg, pcbEditingKeys() ), patch ) )
        return false;

    prefsChanged( aFrame );
    return true;
}

json mouseGet( PCB_EDIT_FRAME* aFrame )
{
    COMMON_SETTINGS* cfg = Pgm().GetCommonSettings();
    return cfg ? ccPcbJsonGet( prefSection( cfg, mouseKeys() ) ) : json();
}

bool mouseSet( PCB_EDIT_FRAME* aFrame, const json& aValue, bool aDryRun, std::string& aError,
               std::string& aMessage, json& aRecord )
{
    COMMON_SETTINGS* cfg = Pgm().GetCommonSettings();
    json             patch = json::object();

    if( !cfg || !prefPatch( aValue, mouseKeys(), patch ) )
        return false;

    if( aDryRun )
        return true;

    if( !ccPcbJsonSet( prefSection( cfg, mouseKeys() ), patch ) )
        return false;

    prefsChanged( aFrame );
    return true;
}

// ── origins (st-origins-e, PE8): the board's grid origin and drill (aux) origin, mm, as
// PCB_CONTROL::DoSetGridOrigin and BOARD_EDITOR_CONTROL::DoSetDrillOrigin set them ({x: 0,
// y: 0} is KiCad's Reset). {grid_origin?: {x, y}, aux_origin?: {x, y}}. The two origin markers
// follow on the tool reset of Board Setup's OK steps (both tools re-read the design settings on
// MODEL_RELOAD); KiCad's own grid origin press makes an undo step, a settings set does not.

json originsGet( PCB_EDIT_FRAME* aFrame )
{
    BOARD_DESIGN_SETTINGS& bds = aFrame->GetBoard()->GetDesignSettings();
    json                   out = json::object();

    out["grid_origin"] = pointJson( bds.GetGridOrigin() );
    out["aux_origin"] = pointJson( bds.GetAuxOrigin() );
    return out;
}

bool originsSet( PCB_EDIT_FRAME* aFrame, const json& aValue, bool aDryRun, std::string& aError,
                 std::string& aMessage, json& aRecord )
{
    if( !patchKeysIn( aValue, { "grid_origin", "aux_origin" } ) )
        return false;

    std::optional<VECTOR2I> grid, aux;

    if( aValue.contains( "grid_origin" ) && !( grid = readPoint( aValue["grid_origin"], 1000.0 ) ) )
        return false;

    if( aValue.contains( "aux_origin" ) && !( aux = readPoint( aValue["aux_origin"], 1000.0 ) ) )
        return false;

    if( aDryRun )
        return true;

    BOARD_DESIGN_SETTINGS& bds = aFrame->GetBoard()->GetDesignSettings();

    if( grid )
    {
        bds.SetGridOrigin( *grid );
        aFrame->GetCanvas()->GetGAL()->SetGridOrigin( VECTOR2D( *grid ) );
    }

    if( aux )
        bds.SetAuxOrigin( *aux );

    aFrame->GetCanvas()->GetView()->MarkDirty();
    return true;
}

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

const bool kRouter = ccRegisterPcbSection( { "router", "pcb", &pcb::routerGet, &pcb::routerSet } );
const bool kLayerPairs = ccRegisterPcbSection( { "layerPairs", "pcb", &pcb::layerPairsGet, &pcb::layerPairsSet } );
const bool kOrigins = ccRegisterPcbSection( { "origins", "pcb", &pcb::originsGet, &pcb::originsSet } );
const bool kPcbEditing = ccRegisterPcbSection( { "pcbEditing", "pcb", &pcb::pcbEditingGet, &pcb::pcbEditingSet,
                                                 false } );
const bool kMouse = ccRegisterPcbSection( { "mouse", "pcb", &pcb::mouseGet, &pcb::mouseSet, false } );
const bool kStackupLayers = ccRegisterPcbSection( { "stackupLayers", "pcb", &pcb::stackupLayersGet,
                                                    &pcb::stackupLayersSet } );
} // namespace cc_e12

EMSCRIPTEN_BINDINGS( cc_e12_pcb )
{
}
