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
#include <pcb_painter.h>
#include <pcb_plot_params.h>
#include <pcb_view.h>
#include <project/component_class_settings.h>
#include <project/project_file.h>
#include <project/tuning_profiles.h>

#ifdef KICAD_MERGED_EMBIND
// K-TSOLVE (kicad pcbnew/dialogs/panel_setup_tuning_profile_info.cpp, WebAssembly build only):
// the tuning profile panel's solve as a free function. Declared here, defined in KiCad's file.
bool ccTuningSolve( BOARD* aBoard, const EDA_IU_SCALE& aIuScale, bool aDifferential, double aImpedance,
                    PCB_LAYER_ID aSignal, PCB_LAYER_ID aTopRef, PCB_LAYER_ID aBottomRef, int aSolveFor,
                    int& aWidth, int& aGap, int& aDelay, wxString& aProblem );
#endif

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

// ── m7-e (M7, PE31): the Board Setup panels the page did not have yet. Each section reads a
// given board and project (the m8-e reader reads another design with the same function) and
// sets the live one; a set is a closed patch, read whole before anything changes, Board
// Setup's OK steps after (setupOk). Wire keys are KiCad's own JSON names (zoneDefaults,
// tuningPatterns, componentClasses, tuningProfiles) or this lane's (zoneHatchOffsets,
// boardFormatting); componentClasses names KiCad's nested key with a dot.

/** One value of the kind aKind: 'n' a finite number, 'p' a number >= 0, 'i' an integer, 'b' a
 *  bool, 's' a string, 'o' an object, 'a' an array. */
bool kindOk( const json& aValue, char aKind )
{
    switch( aKind )
    {
    case 'n': return aValue.is_number() && std::isfinite( aValue.get<double>() );
    case 'p': return aValue.is_number() && std::isfinite( aValue.get<double>() ) && aValue.get<double>() >= 0.0;
    case 'i': return aValue.is_number_integer();
    case 'b': return aValue.is_boolean();
    case 's': return aValue.is_string();
    case 'o': return aValue.is_object();
    case 'a': return aValue.is_array();
    default:  return false;
    }
}

/** An object whose every key is one of aKinds, of that key's kind; with aAll every key present. */
bool objectOk( const json& aValue, std::initializer_list<std::pair<const char*, char>> aKinds, bool aAll )
{
    if( !aValue.is_object() || ( aAll && aValue.size() != aKinds.size() ) )
        return false;

    for( const auto& [key, v] : aValue.items() )
    {
        auto kind = std::find_if( aKinds.begin(), aKinds.end(),
                                  [&]( const auto& aPair ) { return key == aPair.first; } );

        if( kind == aKinds.end() || !kindOk( v, kind->second ) )
            return false;
    }

    return true;
}

/** ccPcbJsonSet's refusals without its change: wire keys only, no null, each key's kind kept. */
bool jsonPatchOk( const CC_PCB_JSON_SECTION& aSection, const json& aPatch )
{
    if( !aSection.settings || !aPatch.is_object() || aPatch.empty() || ccPcbHasNull( aPatch ) )
        return false;

    aSection.settings->Store();
    const nlohmann::json& doc = *aSection.settings->Internals();

    for( const auto& [key, value] : aPatch.items() )
    {
        auto it = std::find_if( aSection.keys.begin(), aSection.keys.end(),
                                [&]( const auto& aPair ) { return aPair.first == key; } );

        if( it == aSection.keys.end() )
            return false;

        const json::json_pointer ptr( it->second );

        if( doc.contains( ptr ) )
        {
            const json& target = doc.at( ptr );

            if( !( target.is_number() && value.is_number() ) && target.type() != value.type() )
                return false;
        }
    }

    return true;
}

bool jsonSectionSet( const CC_PCB_JSON_SECTION& aSection, const json& aPatch, bool aDryRun )
{
    if( !jsonPatchOk( aSection, aPatch ) )
        return false;

    return aDryRun || ccPcbJsonSet( aSection, aPatch );
}

// zoneDefaults: PANEL_SETUP_ZONES, BOARD_DESIGN_SETTINGS' defaults.zones params (mm, degrees and
// KiCad's enumeration numbers as the project file holds them). Load() sets the default zone
// settings through the params.

const std::vector<std::string>& zoneDefaultKeys()
{
    static const std::vector<std::string> keys = {
        "min_clearance",       "min_thickness",        "fill_mode",
        "hatch_thickness",     "hatch_gap",            "hatch_orientation",
        "hatch_smoothing_level", "hatch_smoothing_value", "border_display_style",
        "border_hatch_pitch",  "thermal_relief_gap",   "thermal_relief_spoke_width",
        "pad_connection",      "corner_smoothing",     "corner_radius",
        "remove_islands",      "min_island_area"
    };
    return keys;
}

CC_PCB_JSON_SECTION zoneDefaultsSection( BOARD* aBoard )
{
    CC_PCB_JSON_SECTION section;
    section.settings = &aBoard->GetDesignSettings();

    for( const std::string& key : zoneDefaultKeys() )
        section.keys.emplace_back( key, "/defaults/zones/" + key );

    return section;
}

json zoneDefaultsRead( BOARD* aBoard, PROJECT* aProject )
{
    return ccPcbJsonGet( zoneDefaultsSection( aBoard ) );
}

json zoneDefaultsGet( PCB_EDIT_FRAME* aFrame )
{
    return zoneDefaultsRead( aFrame->GetBoard(), &aFrame->Prj() );
}

bool zoneDefaultsSet( PCB_EDIT_FRAME* aFrame, const json& aValue, bool aDryRun, std::string& aError,
                      std::string& aMessage, json& aRecord )
{
    if( !aValue.is_object() )
        return false;

    for( const auto& [key, v] : aValue.items() )
    {
        // Every zone default is a number (a length, an angle or an enumeration).
        if( !kindOk( v, 'n' ) || ( v.get<double>() < 0.0 && key != "hatch_orientation" ) )
            return false;
    }

    return jsonSectionSet( zoneDefaultsSection( aFrame->GetBoard() ), aValue, aDryRun );
}

// zoneHatchOffsets: PANEL_SETUP_ZONE_HATCH_OFFSETS, the board file's m_ZoneLayerProperties.
// {layers: [{layer, offset: [x, y] mm | null}]}, one row per copper layer the board enables, in
// KiCad's order; null is "Board default". A set replaces the list: every enabled copper layer
// it does not name goes back to the board default, as the panel writes every row.

json zoneHatchOffsetsRead( BOARD* aBoard, PROJECT* aProject )
{
    BOARD_DESIGN_SETTINGS& bds = aBoard->GetDesignSettings();
    json                   layers = json::array();

    for( PCB_LAYER_ID layer : LSET::AllCuMask().UIOrder() )
    {
        if( !bds.IsLayerEnabled( layer ) )
            continue;

        json entry = json::object();
        entry["layer"] = ccLayer( layer );

        auto it = bds.m_ZoneLayerProperties.find( layer );

        if( it != bds.m_ZoneLayerProperties.end() && it->second.hatching_offset )
            entry["offset"] = json::array( { ccMm( it->second.hatching_offset->x ),
                                             ccMm( it->second.hatching_offset->y ) } );
        else
            entry["offset"] = nullptr;

        layers.push_back( std::move( entry ) );
    }

    json out = json::object();
    out["layers"] = std::move( layers );
    return out;
}

json zoneHatchOffsetsGet( PCB_EDIT_FRAME* aFrame )
{
    return zoneHatchOffsetsRead( aFrame->GetBoard(), &aFrame->Prj() );
}

bool zoneHatchOffsetsSet( PCB_EDIT_FRAME* aFrame, const json& aValue, bool aDryRun, std::string& aError,
                          std::string& aMessage, json& aRecord )
{
    BOARD*                 board = aFrame->GetBoard();
    BOARD_DESIGN_SETTINGS& bds = board->GetDesignSettings();

    if( !aValue.is_object() || aValue.size() != 1 || !aValue.contains( "layers" ) || !aValue["layers"].is_array()
            || aValue["layers"].size() > PCB_LAYER_ID_COUNT )
    {
        return false;
    }

    std::map<PCB_LAYER_ID, std::optional<VECTOR2I>> given;

    for( const json& entry : aValue["layers"] )
    {
        if( !entry.is_object() || entry.size() != 2 || !entry.contains( "layer" ) || !entry.contains( "offset" )
                || !entry["layer"].is_string() )
        {
            return false;
        }

        const PCB_LAYER_ID layer = ccLayerByName( board, entry["layer"].get<std::string>() );

        if( layer == UNDEFINED_LAYER || !IsCopperLayer( layer ) || !bds.IsLayerEnabled( layer )
                || given.count( layer ) )
        {
            return false;
        }

        const json& offset = entry["offset"];

        if( offset.is_null() )
        {
            given[layer] = std::nullopt;
            continue;
        }

        if( !offset.is_array() || offset.size() != 2 || !kindOk( offset[0], 'n' ) || !kindOk( offset[1], 'n' )
                || std::abs( offset[0].get<double>() ) > 1000.0 || std::abs( offset[1].get<double>() ) > 1000.0 )
        {
            return false;
        }

        given[layer] = VECTOR2I( pcbIUScale.mmToIU( offset[0].get<double>() ),
                                 pcbIUScale.mmToIU( offset[1].get<double>() ) );
    }

    if( aDryRun )
        return true;

    for( PCB_LAYER_ID layer : LSET::AllCuMask().UIOrder() )
    {
        if( !bds.IsLayerEnabled( layer ) )
            continue;

        auto it = given.find( layer );
        bds.m_ZoneLayerProperties[layer].hatching_offset = it != given.end() ? it->second : std::nullopt;
    }

    return true;
}

// boardFormatting: PANEL_SETUP_FORMATTING. {applyToFields, applyToText, applyToShapes,
// applyToDimensions, applyToBarcodes} are the design settings' m_StyleFP* (the project file's
// defaults.apply_defaults_to_fp_*), {dashRatio, gapRatio} the board's plot options; a set runs
// the panel's own steps (SetPlotOptions, the painter's dash and gap ratios, a repaint of the
// shapes).

json boardFormattingRead( BOARD* aBoard, PROJECT* aProject )
{
    BOARD_DESIGN_SETTINGS& bds = aBoard->GetDesignSettings();
    const PCB_PLOT_PARAMS& plot = aBoard->GetPlotOptions();
    json                   out = json::object();

    out["applyToFields"] = bds.m_StyleFPFields;
    out["applyToText"] = bds.m_StyleFPText;
    out["applyToShapes"] = bds.m_StyleFPShapes;
    out["applyToDimensions"] = bds.m_StyleFPDimensions;
    out["applyToBarcodes"] = bds.m_StyleFPBarcodes;
    out["dashRatio"] = plot.GetDashedLineDashRatio();
    out["gapRatio"] = plot.GetDashedLineGapRatio();
    return out;
}

json boardFormattingGet( PCB_EDIT_FRAME* aFrame )
{
    return boardFormattingRead( aFrame->GetBoard(), &aFrame->Prj() );
}

bool boardFormattingSet( PCB_EDIT_FRAME* aFrame, const json& aValue, bool aDryRun, std::string& aError,
                         std::string& aMessage, json& aRecord )
{
    if( aValue.empty()
            || !objectOk( aValue, { { "applyToFields", 'b' }, { "applyToText", 'b' }, { "applyToShapes", 'b' },
                                    { "applyToDimensions", 'b' }, { "applyToBarcodes", 'b' },
                                    { "dashRatio", 'p' }, { "gapRatio", 'p' } },
                          false ) )
    {
        return false;
    }

    for( const char* ratio : { "dashRatio", "gapRatio" } )
    {
        if( aValue.contains( ratio ) && ( aValue[ratio].get<double>() <= 0.0 || aValue[ratio].get<double>() > 1000.0 ) )
            return false;
    }

    if( aDryRun )
        return true;

    BOARD*                 board = aFrame->GetBoard();
    BOARD_DESIGN_SETTINGS& bds = board->GetDesignSettings();
    PCB_PLOT_PARAMS        settings = board->GetPlotOptions();

    if( aValue.contains( "dashRatio" ) )
        settings.SetDashedLineDashRatio( aValue["dashRatio"].get<double>() );

    if( aValue.contains( "gapRatio" ) )
        settings.SetDashedLineGapRatio( aValue["gapRatio"].get<double>() );

    board->SetPlotOptions( settings );

    if( aValue.contains( "applyToFields" ) )
        bds.m_StyleFPFields = aValue["applyToFields"].get<bool>();

    if( aValue.contains( "applyToText" ) )
        bds.m_StyleFPText = aValue["applyToText"].get<bool>();

    if( aValue.contains( "applyToShapes" ) )
        bds.m_StyleFPShapes = aValue["applyToShapes"].get<bool>();

    if( aValue.contains( "applyToDimensions" ) )
        bds.m_StyleFPDimensions = aValue["applyToDimensions"].get<bool>();

    if( aValue.contains( "applyToBarcodes" ) )
        bds.m_StyleFPBarcodes = aValue["applyToBarcodes"].get<bool>();

    KIGFX::PCB_VIEW* view = aFrame->GetCanvas()->GetView();

    view->GetPainter()->GetSettings()->SetDashLengthRatio( settings.GetDashedLineDashRatio() );
    view->GetPainter()->GetSettings()->SetGapLengthRatio( settings.GetDashedLineGapRatio() );

    view->UpdateAllItemsConditionally( KIGFX::REPAINT,
            []( KIGFX::VIEW_ITEM* aItem ) -> bool
            {
                const EDA_ITEM* item = dynamic_cast<const EDA_ITEM*>( aItem );
                return item && item->Type() == PCB_SHAPE_T;
            } );

    aFrame->GetCanvas()->Refresh();
    return true;
}

// tuningPatterns: PANEL_SETUP_TUNING_PATTERNS, the design settings' tuning_pattern_settings.
// Three objects (single_track_defaults, diff_pair_defaults, diff_pair_skew_defaults), each
// min_amplitude, max_amplitude, spacing (mm), corner_style (0 chamfer, 1 round),
// corner_radius_percentage, single_sided; a set merges into each named object.

CC_PCB_JSON_SECTION tuningPatternsSection( BOARD* aBoard )
{
    CC_PCB_JSON_SECTION section;
    section.settings = &aBoard->GetDesignSettings();
    section.keys = { { "single_track_defaults", "/tuning_pattern_settings/single_track_defaults" },
                     { "diff_pair_defaults", "/tuning_pattern_settings/diff_pair_defaults" },
                     { "diff_pair_skew_defaults", "/tuning_pattern_settings/diff_pair_skew_defaults" } };
    return section;
}

json tuningPatternsRead( BOARD* aBoard, PROJECT* aProject )
{
    return ccPcbJsonGet( tuningPatternsSection( aBoard ) );
}

json tuningPatternsGet( PCB_EDIT_FRAME* aFrame )
{
    return tuningPatternsRead( aFrame->GetBoard(), &aFrame->Prj() );
}

bool tuningPatternsSet( PCB_EDIT_FRAME* aFrame, const json& aValue, bool aDryRun, std::string& aError,
                        std::string& aMessage, json& aRecord )
{
    if( !aValue.is_object() )
        return false;

    for( const auto& [key, v] : aValue.items() )
    {
        if( v.empty()
                || !objectOk( v, { { "min_amplitude", 'p' }, { "max_amplitude", 'p' }, { "spacing", 'p' },
                                   { "corner_style", 'i' }, { "corner_radius_percentage", 'i' },
                                   { "single_sided", 'b' } },
                              false ) )
        {
            return false;
        }

        for( const char* length : { "min_amplitude", "max_amplitude", "spacing" } )
        {
            if( v.contains( length ) && v[length].get<double>() > 1000.0 )
                return false;
        }

        if( v.contains( "corner_style" ) && v["corner_style"] != 0 && v["corner_style"] != 1 )
            return false;

        if( v.contains( "corner_radius_percentage" )
                && ( v["corner_radius_percentage"].get<long long>() < 0
                     || v["corner_radius_percentage"].get<long long>() > 100 ) )
        {
            return false;
        }
    }

    return jsonSectionSet( tuningPatternsSection( aFrame->GetBoard() ), aValue, aDryRun );
}

// componentClasses: PANEL_ASSIGN_COMPONENT_CLASSES, the project file's
// component_class_settings. {"sheet_component_classes.enabled": bool, "assignments": [{
// component_class, conditions_operator ("ALL" | "ANY"), conditions: {TYPE[-n]: {primary?,
// secondary?}}}]}, TYPE one of KiCad's seven condition names. assignments replaces the list.
// Board Setup's OK steps run SynchronizeComponentClasses.

bool componentClassAssignmentsOk( const json& aValue )
{
    static const std::set<std::string> types = { "REFERENCE", "FOOTPRINT",       "SIDE",      "ROTATION",
                                                  "FOOTPRINT_FIELD", "CUSTOM", "SHEET_NAME" };

    if( !aValue.is_array() || aValue.size() > 4096 )
        return false;

    for( const json& assignment : aValue )
    {
        if( !objectOk( assignment, { { "component_class", 's' }, { "conditions_operator", 's' },
                                     { "conditions", 'o' } },
                       true ) )
        {
            return false;
        }

        const std::string op = assignment["conditions_operator"].get<std::string>();

        if( assignment["component_class"].get<std::string>().empty() || ( op != "ALL" && op != "ANY" ) )
            return false;

        for( const auto& [name, condition] : assignment["conditions"].items() )
        {
            if( !types.count( name.substr( 0, name.find( '-' ) ) )
                    || !objectOk( condition, { { "primary", 's' }, { "secondary", 's' } }, false ) )
            {
                return false;
            }
        }
    }

    return true;
}

std::optional<CC_PCB_JSON_SECTION> componentClassesSection( PROJECT* aProject )
{
    if( !aProject )
        return std::nullopt;

    std::shared_ptr<COMPONENT_CLASS_SETTINGS>& settings = aProject->GetProjectFile().ComponentClassSettings();

    if( !settings )
        return std::nullopt;

    CC_PCB_JSON_SECTION section;
    section.settings = settings.get();
    section.keys = { { "sheet_component_classes.enabled", "/sheet_component_classes/enabled" },
                     { "assignments", "/assignments" } };
    return section;
}

json componentClassesRead( BOARD* aBoard, PROJECT* aProject )
{
    std::optional<CC_PCB_JSON_SECTION> section = componentClassesSection( aProject );
    return section ? ccPcbJsonGet( *section ) : json();
}

json componentClassesGet( PCB_EDIT_FRAME* aFrame )
{
    return componentClassesRead( aFrame->GetBoard(), &aFrame->Prj() );
}

bool componentClassesSet( PCB_EDIT_FRAME* aFrame, const json& aValue, bool aDryRun, std::string& aError,
                          std::string& aMessage, json& aRecord )
{
    std::optional<CC_PCB_JSON_SECTION> section = componentClassesSection( &aFrame->Prj() );

    if( !section || aValue.empty()
            || !objectOk( aValue, { { "sheet_component_classes.enabled", 'b' }, { "assignments", 'a' } }, false )
            || ( aValue.contains( "assignments" ) && !componentClassAssignmentsOk( aValue["assignments"] ) ) )
    {
        return false;
    }

    return jsonSectionSet( *section, aValue, aDryRun );
}

// tuningProfiles: PANEL_SETUP_TUNING_PROFILES, the project file's tuning_profiles.
// {tuning_profiles_impedance_geometric: [{profile_name, type (0 single, 1 differential),
// target_impedance, enable_time_domain_tuning, via_prop_delay, layer_entries: [{signal_layer,
// top_reference_layer, bottom_reference_layer, width, diff_pair_gap, delay}], via_overrides:
// [{signal_layer_from, signal_layer_to, via_layer_from, via_layer_to, delay}]}]}: KiCad's own
// JSON (lengths and delays in KiCad's internal units, as the project file holds them), the
// list replaced whole. A profile's name is unique and not empty, a layer once per profile
// (the panel's ValidateProfile). Board Setup's OK steps run SynchronizeTuningProfileProperties.

bool tuningProfileListOk( const json& aValue )
{
    auto layerOk = []( const json& aName ) -> bool
    {
        if( !aName.is_string() )
            return false;

        wxString name = wxString::FromUTF8( aName.get<std::string>() );
        return LSET::NameToLayer( name ) >= 0;
    };

    if( !aValue.is_array() || aValue.size() > 256 )
        return false;

    std::set<std::string> names;

    for( const json& profile : aValue )
    {
        if( !objectOk( profile, { { "profile_name", 's' }, { "type", 'i' }, { "target_impedance", 'p' },
                                  { "enable_time_domain_tuning", 'b' }, { "via_prop_delay", 'i' },
                                  { "layer_entries", 'a' }, { "via_overrides", 'a' } },
                       true ) )
        {
            return false;
        }

        const std::string name = profile["profile_name"].get<std::string>();

        if( name.empty() || !names.insert( name ).second || ( profile["type"] != 0 && profile["type"] != 1 )
                || profile["layer_entries"].size() > PCB_LAYER_ID_COUNT || profile["via_overrides"].size() > 4096 )
        {
            return false;
        }

        std::set<std::string> signals;

        for( const json& entry : profile["layer_entries"] )
        {
            if( !objectOk( entry, { { "signal_layer", 's' }, { "top_reference_layer", 's' },
                                    { "bottom_reference_layer", 's' }, { "width", 'i' },
                                    { "diff_pair_gap", 'i' }, { "delay", 'i' } },
                           true )
                    || !layerOk( entry["signal_layer"] ) || !layerOk( entry["top_reference_layer"] )
                    || !layerOk( entry["bottom_reference_layer"] )
                    || !signals.insert( entry["signal_layer"].get<std::string>() ).second )
            {
                return false;
            }
        }

        for( const json& entry : profile["via_overrides"] )
        {
            if( !objectOk( entry, { { "signal_layer_from", 's' }, { "signal_layer_to", 's' },
                                    { "via_layer_from", 's' }, { "via_layer_to", 's' }, { "delay", 'i' } },
                           true )
                    || !layerOk( entry["signal_layer_from"] ) || !layerOk( entry["signal_layer_to"] )
                    || !layerOk( entry["via_layer_from"] ) || !layerOk( entry["via_layer_to"] ) )
            {
                return false;
            }
        }
    }

    return true;
}

std::optional<CC_PCB_JSON_SECTION> tuningProfilesSection( PROJECT* aProject )
{
    if( !aProject )
        return std::nullopt;

    std::shared_ptr<TUNING_PROFILES>& settings = aProject->GetProjectFile().TuningProfileParameters();

    if( !settings )
        return std::nullopt;

    CC_PCB_JSON_SECTION section;
    section.settings = settings.get();
    section.keys = { { "tuning_profiles_impedance_geometric", "/tuning_profiles_impedance_geometric" } };
    return section;
}

json tuningProfilesRead( BOARD* aBoard, PROJECT* aProject )
{
    std::optional<CC_PCB_JSON_SECTION> section = tuningProfilesSection( aProject );
    return section ? ccPcbJsonGet( *section ) : json();
}

json tuningProfilesGet( PCB_EDIT_FRAME* aFrame )
{
    return tuningProfilesRead( aFrame->GetBoard(), &aFrame->Prj() );
}

bool tuningProfilesSet( PCB_EDIT_FRAME* aFrame, const json& aValue, bool aDryRun, std::string& aError,
                        std::string& aMessage, json& aRecord )
{
    std::optional<CC_PCB_JSON_SECTION> section = tuningProfilesSection( &aFrame->Prj() );

    if( !section || !aValue.is_object() || aValue.size() != 1
            || !aValue.contains( "tuning_profiles_impedance_geometric" )
            || !tuningProfileListOk( aValue["tuning_profiles_impedance_geometric"] ) )
    {
        return false;
    }

    return jsonSectionSet( *section, aValue, aDryRun );
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
const bool kZoneDefaults = ccRegisterPcbSection( { "zoneDefaults", "pcb", &pcb::zoneDefaultsGet,
                                                   &pcb::zoneDefaultsSet } );
const bool kZoneHatchOffsets = ccRegisterPcbSection( { "zoneHatchOffsets", "pcb", &pcb::zoneHatchOffsetsGet,
                                                       &pcb::zoneHatchOffsetsSet } );
const bool kBoardFormatting = ccRegisterPcbSection( { "boardFormatting", "pcb", &pcb::boardFormattingGet,
                                                      &pcb::boardFormattingSet } );
const bool kTuningPatterns = ccRegisterPcbSection( { "tuningPatterns", "pcb", &pcb::tuningPatternsGet,
                                                     &pcb::tuningPatternsSet } );
const bool kComponentClasses = ccRegisterPcbSection( { "componentClasses", "pcb", &pcb::componentClassesGet,
                                                       &pcb::componentClassesSet } );
const bool kTuningProfiles = ccRegisterPcbSection( { "tuningProfiles", "pcb", &pcb::tuningProfilesGet,
                                                     &pcb::tuningProfilesSet } );

#ifdef KICAD_MERGED_EMBIND
// ── kicadTuningSolve (m7-e, 5.3; merged image only): KiCad's Tuning Profiles panel's "work out"
// for one row, through ccTuningSolve (kicad pcbnew/dialogs/panel_setup_tuning_profile_info.cpp,
// K-TSOLVE, the panel calling the same function). {type: "single" | "differential", impedance
// (ohms, a pair's differential impedance), signalLayer, topRef?, bottomRef? (canonical layer
// names the board enables; a microstrip on F.Cu reads bottomRef, on B.Cu topRef, a stripline
// both), solveFor: "width" | "gap" | "delay", width?: mm, gap?: mm} answers {width: mm | null,
// gap: mm | null, delay: ps/cm | null, problem: "" | KiCad's own message}. A gap is a pair's
// answer only. A read: nothing changes, no record. "" for any other shape, no frame or an open
// in flight.

std::string tuningSolve( std::string aArgsJson )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return "";

    try
    {
        json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

        if( args.is_discarded()
                || !pcb::objectOk( args, { { "type", 's' }, { "impedance", 'p' }, { "signalLayer", 's' },
                                           { "topRef", 's' }, { "bottomRef", 's' }, { "solveFor", 's' },
                                           { "width", 'p' }, { "gap", 'p' } },
                                   false )
                || !args.contains( "type" ) || !args.contains( "impedance" ) || !args.contains( "signalLayer" )
                || !args.contains( "solveFor" ) )
        {
            return "";
        }

        const std::string type = args["type"].get<std::string>();
        const std::string solve = args["solveFor"].get<std::string>();
        const int         solveFor = solve == "width" ? 0 : solve == "gap" ? 1 : solve == "delay" ? 2 : -1;
        const bool        differential = type == "differential";

        if( ( type != "single" && !differential ) || solveFor < 0 || ( solveFor == 1 && !differential )
                || args["impedance"].get<double>() > 1e6 )
        {
            return "";
        }

        BOARD* board = fr->GetBoard();

        auto layerArg = [&]( const char* aKey ) -> std::optional<PCB_LAYER_ID>
        {
            if( !args.contains( aKey ) )
                return UNDEFINED_LAYER;

            const PCB_LAYER_ID layer = ccLayerByName( board, args[aKey].get<std::string>() );

            if( layer == UNDEFINED_LAYER || !IsCopperLayer( layer ) )
                return std::nullopt;

            return layer;
        };

        std::optional<PCB_LAYER_ID> signal = layerArg( "signalLayer" );
        std::optional<PCB_LAYER_ID> topRef = layerArg( "topRef" );
        std::optional<PCB_LAYER_ID> bottomRef = layerArg( "bottomRef" );

        if( !signal || *signal == UNDEFINED_LAYER || !topRef || !bottomRef )
            return "";

        auto lengthArg = [&]( const char* aKey ) -> int
        {
            if( !args.contains( aKey ) )
                return 0;

            return pcbIUScale.mmToIU( std::min( args[aKey].get<double>(), 1000.0 ) );
        };

        int      width = lengthArg( "width" );
        int      gap = differential ? lengthArg( "gap" ) : 0;
        int      delay = 0;
        wxString problem;
        json     out = json::object();

        if( ccTuningSolve( board, pcbIUScale, differential, args["impedance"].get<double>(), *signal, *topRef,
                           *bottomRef, solveFor, width, gap, delay, problem ) )
        {
            out["width"] = width > 0 ? json( ccMm( width ) ) : json();
            out["gap"] = differential && gap > 0 ? json( ccMm( gap ) ) : json();
            out["delay"] = std::round( EDA_UNIT_UTILS::UI::ToUserUnit( pcbIUScale, EDA_UNITS::PS_PER_CM, delay )
                                       * 10000.0 )
                           / 10000.0;
            out["problem"] = "";
        }
        else
        {
            out["width"] = nullptr;
            out["gap"] = nullptr;
            out["delay"] = nullptr;
            out["problem"] = problem.IsEmpty() ? std::string( "No answer for these values." ) : toUtf8( problem );
        }

        return out.dump( -1, ' ', false, json::error_handler_t::replace );
    }
    catch( ... )
    {
        return "";
    }
}
#endif
} // namespace cc_e12

EMSCRIPTEN_BINDINGS( cc_e12_pcb )
{
#ifdef KICAD_MERGED_EMBIND
    function( "kicadTuningSolve", &cc_e12::tuningSolve );
#endif
}
