// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e12, the schematic face: textually included by eeschema_embind.cpp after
// cc_rules_sch.inc, outside any KICAD_MERGED_EMBIND guard (include subdir eeschema; the
// merged image compiles that TU with -DKICAD_MERGED_EMBIND). It sees the TU's includes and
// helpers (schFrame(), the cc_output namespace) and calls them without editing them.
//
// Every definition of the lane lives in namespace cc_e12 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

#include <settings/common_settings.h>

namespace cc_e12
{
namespace sch
{
// ── schEditing and mouse (st-editprefs-e, SE39): editor preferences, KiCad's Preferences
// panels PANEL_EESCHEMA_EDITING_OPTIONS (EESCHEMA_SETTINGS) and PANEL_MOUSE_SETTINGS
// (COMMON_SETTINGS, the one object every frame of the image reads: the board face registers
// the same "mouse" section over it). App settings: setupOk false, the schematic is never marked
// modified. Wire keys are KiCad's own JSON paths with dots; the two sheet colors are left out
// (colors stay the viewer's). A taken set runs Preferences' OK steps (ResetTools REDRAW, then
// Kiway CommonSettingsChanged) without saving any file.

struct PREF_KEY
{
    const char*      wire;
    char             kind;   // 'b' bool, 'i' one of ints, 'r' an int in [lo, hi]
    std::vector<int> ints;
    double           lo = 0.0;
    double           hi = 0.0;
};

const std::vector<PREF_KEY>& schEditingKeys()
{
    static const std::vector<PREF_KEY> keys = {
        { "drawing.new_power_symbols", 'i', { 0, 1, 2 } },
        { "drawing.default_repeat_offset_x", 'r', {}, -100000, 100000 },   // mils
        { "drawing.default_repeat_offset_y", 'r', {}, -100000, 100000 },
        { "drawing.repeat_label_increment", 'r', {}, -1000000, 1000000 },
        { "drawing.line_mode", 'i', { 0, 1, 2 } },
        { "editing.arc_edit_mode", 'i', { 0, 1, 2 } },
        { "appearance.footprint_preview", 'b', {} },
        { "system.never_show_rescue_dialog", 'b', {} },
        { "autoplace_fields.enable", 'b', {} },
        { "autoplace_fields.allow_rejustify", 'b', {} },
        { "autoplace_fields.align_to_grid", 'b', {} },
        { "input.drag_is_move", 'b', {} },
        { "drawing.auto_start_wires", 'b', {} },
        { "input.esc_clears_net_highlight", 'b', {} },
        { "annotation.automatic", 'b', {} },
        { "input.allow_unconstrained_pin_swaps", 'b', {} }
    };
    return keys;
}

const std::vector<PREF_KEY>& mouseKeys()
{
    // The same keys as the board face's (cc_e12_pcb.inc): MOUSE_DRAG_ACTION as the file stores
    // it (DRAG_ANY -2, DRAG_SELECTED -1, SELECT 0, ZOOM 1, PAN 2, NONE 3), modifiers 0 or a wx
    // key code.
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

CC_JSON_SECTION prefSection( JSON_SETTINGS* aSettings, const std::vector<PREF_KEY>& aKeys )
{
    CC_JSON_SECTION section;
    section.settings = aSettings;

    for( const PREF_KEY& key : aKeys )
    {
        std::string pointer = std::string( "/" ) + key.wire;
        std::replace( pointer.begin(), pointer.end(), '.', '/' );
        section.keys.emplace_back( key.wire, pointer );
    }

    return section;
}

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
            continue;
        }

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

    return true;
}

void prefsChanged( SCH_EDIT_FRAME* aFrame )
{
    if( TOOL_MANAGER* tools = aFrame->GetToolManager() )
        tools->ResetTools( TOOL_BASE::REDRAW );

    aFrame->Kiway().CommonSettingsChanged( 0 );
    aFrame->GetCanvas()->Refresh();
}

bool prefsSet( SCH_EDIT_FRAME* aFrame, JSON_SETTINGS* aSettings, const std::vector<PREF_KEY>& aKeys,
               const json& aValue, bool aDryRun )
{
    json patch = json::object();

    if( !aSettings || !prefPatch( aValue, aKeys, patch ) )
        return false;

    if( aDryRun )
        return true;

    if( !ccJsonSectionSet( prefSection( aSettings, aKeys ), patch ) )
        return false;

    prefsChanged( aFrame );
    return true;
}

json schEditingGet( SCH_EDIT_FRAME* aFrame )
{
    EESCHEMA_SETTINGS* cfg = aFrame->eeconfig();
    return cfg ? ccJsonSectionGet( prefSection( cfg, schEditingKeys() ) ) : json();
}

bool schEditingSet( SCH_EDIT_FRAME* aFrame, const json& aValue, bool aDryRun, std::string& aError,
                    std::string& aMessage, json& aRecord )
{
    return prefsSet( aFrame, aFrame->eeconfig(), schEditingKeys(), aValue, aDryRun );
}

json mouseGet( SCH_EDIT_FRAME* aFrame )
{
    COMMON_SETTINGS* cfg = Pgm().GetCommonSettings();
    return cfg ? ccJsonSectionGet( prefSection( cfg, mouseKeys() ) ) : json();
}

bool mouseSet( SCH_EDIT_FRAME* aFrame, const json& aValue, bool aDryRun, std::string& aError,
               std::string& aMessage, json& aRecord )
{
    return prefsSet( aFrame, Pgm().GetCommonSettings(), mouseKeys(), aValue, aDryRun );
}
} // namespace sch

const bool kSchEditing = ccRegisterSchSection( { "schEditing", "sch", &sch::schEditingGet, &sch::schEditingSet,
                                                 false } );
const bool kSchMouse = ccRegisterSchSection( { "mouse", "sch", &sch::mouseGet, &sch::mouseSet, false } );
} // namespace cc_e12

EMSCRIPTEN_BINDINGS( cc_e12_sch )
{
}
