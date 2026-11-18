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

#include <erc/erc_settings.h>
#include <settings/common_settings.h>
#include <settings/settings_manager.h>
#include <project/component_class_settings.h>
#include <project/project_file.h>
#include <schematic_settings.h>
#include <settings/bom_settings.h>

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

// ── m7-e (M7): Schematic Setup's Annotation and BOM Presets panels, and the board's component
// classes read from the schematic frame (the project file holds them; Schematic Setup has no
// such panel, so the set is the board's). Each section reads a given project (the m8-e reader
// reads another design with the same function) and sets the live one; a set is a closed patch
// read whole before anything changes, Schematic Setup's OK steps after.

SCHEMATIC_SETTINGS* schSettingsOf( PROJECT* aProject )
{
    return aProject ? aProject->GetProjectFile().m_SchematicSettings : nullptr;
}

// annotation: PANEL_EESCHEMA_ANNOTATION_OPTIONS. Wire keys are KiCad's JSON paths with a dot:
// "annotation.sort_order" (0 across then down, by X; 1 down then across, by Y),
// "annotation.method" (0 first free, 1 sheet number times 100, 2 times 1000),
// "annotate_start_num" (also schFormatting's: either set writes the one key),
// "reuse_designators".

const std::vector<std::pair<std::string, std::string>>& annotationKeys()
{
    static const std::vector<std::pair<std::string, std::string>> keys = {
        { "annotation.sort_order", "/annotation/sort_order" },
        { "annotation.method", "/annotation/method" },
        { "annotate_start_num", "/annotate_start_num" },
        { "reuse_designators", "/reuse_designators" }
    };
    return keys;
}

json annotationRead( PROJECT* aProject )
{
    SCHEMATIC_SETTINGS* settings = schSettingsOf( aProject );

    if( !settings )
        return json();

    return ccJsonSectionGet( CC_JSON_SECTION{ settings, annotationKeys() } );
}

json annotationGet( SCH_EDIT_FRAME* aFrame )
{
    return annotationRead( &aFrame->Prj() );
}

bool annotationSet( SCH_EDIT_FRAME* aFrame, const json& aValue, bool aDryRun, std::string& aError,
                    std::string& aMessage, json& aRecord )
{
    if( !aValue.is_object() || aValue.empty() )
        return false;

    for( const auto& [key, v] : aValue.items() )
    {
        if( std::none_of( annotationKeys().begin(), annotationKeys().end(),
                          [&]( const auto& aKey ) { return aKey.first == key; } ) )
        {
            return false;
        }

        if( key == "reuse_designators" ? !v.is_boolean() : !v.is_number_integer() )
            return false;

        if( key == "annotation.sort_order" && v != 0 && v != 1 )
            return false;

        if( key == "annotation.method" && v != 0 && v != 1 && v != 2 )
            return false;

        if( key == "annotate_start_num" && ( v.get<long long>() < 0 || v.get<long long>() > 999999 ) )
            return false;
    }

    if( aDryRun )
        return true;

    CC_JSON_SECTION section{ &aFrame->Schematic().Settings(), annotationKeys() };

    return ccJsonSectionSet( section, aValue );
}

// bomPresets: PANEL_BOM_PRESETS. KiCad's JSON: bom_presets and bom_fmt_presets (the user's
// presets; KiCad's built in ones are not stored), bom_settings and bom_fmt_settings (the
// current choice). Each value is read by KiCad's own from_json first (a value KiCad would not
// load is refused), then replaces its key whole.

const std::vector<std::pair<std::string, std::string>>& bomKeys()
{
    static const std::vector<std::pair<std::string, std::string>> keys = {
        { "bom_presets", "/bom_presets" },
        { "bom_fmt_presets", "/bom_fmt_presets" },
        { "bom_settings", "/bom_settings" },
        { "bom_fmt_settings", "/bom_fmt_settings" }
    };
    return keys;
}

json bomPresetsRead( PROJECT* aProject )
{
    SCHEMATIC_SETTINGS* settings = schSettingsOf( aProject );

    if( !settings )
        return json();

    return ccJsonSectionGet( CC_JSON_SECTION{ settings, bomKeys() } );
}

json bomPresetsGet( SCH_EDIT_FRAME* aFrame )
{
    return bomPresetsRead( &aFrame->Prj() );
}

bool bomPresetsSet( SCH_EDIT_FRAME* aFrame, const json& aValue, bool aDryRun, std::string& aError,
                    std::string& aMessage, json& aRecord )
{
    if( !aValue.is_object() || aValue.empty() )
        return false;

    try
    {
        for( const auto& [key, v] : aValue.items() )
        {
            if( key == "bom_presets" && v.is_array() && v.size() <= 1024 )
                (void) v.get<std::vector<BOM_PRESET>>();
            else if( key == "bom_fmt_presets" && v.is_array() && v.size() <= 1024 )
                (void) v.get<std::vector<BOM_FMT_PRESET>>();
            else if( key == "bom_settings" && v.is_object() )
                (void) v.get<BOM_PRESET>();
            else if( key == "bom_fmt_settings" && v.is_object() )
                (void) v.get<BOM_FMT_PRESET>();
            else
                return false;
        }
    }
    catch( ... )
    {
        return false;
    }

    if( aDryRun )
        return true;

    SCHEMATIC_SETTINGS& settings = aFrame->Schematic().Settings();
    settings.Store();
    nlohmann::json& doc = *settings.Internals();

    for( const auto& [key, v] : aValue.items() )
        doc[json::json_pointer( "/" + key )] = v;   // replaced whole, never merged

    settings.Load();
    return true;
}

// componentClasses, read from the schematic frame: the same project file section the board
// face registers (see cc_e12_pcb.inc); its set is the board's (Board Setup's panel and
// SynchronizeComponentClasses), so here a set is refused.

json componentClassesRead( PROJECT* aProject )
{
    if( !aProject )
        return json();

    std::shared_ptr<COMPONENT_CLASS_SETTINGS>& settings = aProject->GetProjectFile().ComponentClassSettings();

    if( !settings )
        return json();

    return ccJsonSectionGet( CC_JSON_SECTION{ settings.get(),
                                              { { "sheet_component_classes.enabled",
                                                  "/sheet_component_classes/enabled" },
                                                { "assignments", "/assignments" } } } );
}

json componentClassesGet( SCH_EDIT_FRAME* aFrame )
{
    return componentClassesRead( &aFrame->Prj() );
}

bool componentClassesSet( SCH_EDIT_FRAME* aFrame, const json& aValue, bool aDryRun, std::string& aError,
                          std::string& aMessage, json& aRecord )
{
    aError = "bad_args";
    aMessage = "component classes are set in the board editor";
    return false;
}

// ── kicadSettingsRead, the schematic half (m8-e, 6.2): Schematic Setup's Import Settings
// without its dialog. The other project is loaded by the frame's SETTINGS_MANAGER (LoadProject,
// not made active); its ERC and schematic settings are made and read from its project file
// exactly as SCHEMATIC::SetProject makes them (onAuxiliaryAction builds a whole SCHEMATIC for
// this, whose destructor clears SCHEMATIC::m_IsSchematicExists and the SCH_FIELD listeners of
// the open schematic too; the settings alone are what the sections read); each asked section is
// read by ccSchSettingsRead, the same bodies kicadSettingsGet answers with; then the settings
// made here are deleted and the project unloaded without a save. Nothing of the open design
// changes and no undo step is made.

namespace read
{
/** kicadSettingsRead's arguments, read whole: {run, dir: "/tmp/cc-settings-read/<n>",
 *  project: "<name>.kicad_pro", board?: "<name>.kicad_pcb", sections: [1 to 64 names]}, the
 *  files already in dir (plain names, no path). */
struct READ_ARGS
{
    std::string              dir;
    std::string              project;
    std::string              board;
    std::vector<std::string> sections;
};

bool plainFileName( const std::string& aName, const std::string& aExt )
{
    return aName.size() > aExt.size() && aName.size() <= 255
           && aName.compare( aName.size() - aExt.size(), aExt.size(), aExt ) == 0
           && aName.find_first_of( std::string( "/\\:\0", 4 ) ) == std::string::npos && aName[0] != '.';
}

std::optional<READ_ARGS> readArgs( const std::string& aArgsJson )
{
    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() < 4 || args.size() > 5 )
        return std::nullopt;

    for( const auto& [key, v] : args.items() )
    {
        if( key != "run" && key != "dir" && key != "project" && key != "board" && key != "sections" )
            return std::nullopt;
    }

    if( !args.contains( "run" ) || !args["run"].is_number_integer() || args["run"].get<long long>() < 0
            || !args.contains( "dir" ) || !args["dir"].is_string() || !args.contains( "project" )
            || !args["project"].is_string() || !args.contains( "sections" ) || !args["sections"].is_array()
            || args["sections"].empty() || args["sections"].size() > 64
            || ( args.contains( "board" ) && !args["board"].is_string() ) )
    {
        return std::nullopt;
    }

    READ_ARGS a;
    a.dir = args["dir"].get<std::string>();
    a.project = args["project"].get<std::string>();
    a.board = args.contains( "board" ) ? args["board"].get<std::string>() : std::string();

    if( !cc_output::IsRunDir( a.dir, "/tmp/cc-settings-read/" ) || !plainFileName( a.project, ".kicad_pro" )
            || ( args.contains( "board" ) && !plainFileName( a.board, ".kicad_pcb" ) )
            || !wxFileName::FileExists( wxString::FromUTF8( a.dir + "/" + a.project ) )
            || ( !a.board.empty() && !wxFileName::FileExists( wxString::FromUTF8( a.dir + "/" + a.board ) ) ) )
    {
        return std::nullopt;
    }

    std::set<std::string> seen;

    for( const json& name : args["sections"] )
    {
        if( !name.is_string() || name.get<std::string>().empty() || name.get<std::string>().size() > 64
                || !seen.insert( name.get<std::string>() ).second )
        {
            return std::nullopt;
        }

        a.sections.push_back( name.get<std::string>() );
    }

    return a;
}

/** KiCad's message, first line, the run folder taken out of it (the page shows it). */
std::string readProblem( const wxString& aText, const std::string& aDir )
{
    std::string text = std::string( aText.utf8_str() );
    text = text.substr( 0, text.find( '\n' ) );

    for( size_t at = text.find( aDir + "/" ); at != std::string::npos; at = text.find( aDir + "/" ) )
        text.erase( at, aDir.size() + 1 );

    for( size_t at = text.find( aDir ); at != std::string::npos; at = text.find( aDir ) )
        text.erase( at, aDir.size() );

    return text.size() > 512 ? text.substr( 0, 512 ) : text;
}

/** settings-read.json {sections, missing}, then .cc-settings-read.json {error, message} last. */
void writeReadRecord( const std::string& aDir, const json& aSections, const json& aMissing, const std::string& aError,
                      const std::string& aMessage )
{
    json record = json::object();
    record["sections"] = aSections;
    record["missing"] = aMissing;
    cc_output::WriteRecord( aDir, "settings-read.json", record );

    json done = json::object();
    done["error"] = aError;
    done["message"] = aMessage;
    cc_output::WriteRecord( aDir, ".cc-settings-read.json", done );
}

void runSch( const READ_ARGS& aArgs )
{
    json        sections = json::object();
    json        missing = json::array();
    std::string error;
    std::string message;

    SCH_EDIT_FRAME* frame = schFrame();

    if( !frame )
    {
        writeReadRecord( aArgs.dir, sections, missing, "island_error", "no schematic editor" );
        return;
    }

    wxLogNull         noLog;
    SETTINGS_MANAGER* manager = frame->GetSettingsManager();
    const wxString    projectPath = wxString::FromUTF8( aArgs.dir + "/" + aArgs.project );
    const bool        loadedBefore = manager->GetProject( projectPath ) != nullptr;

    if( !loadedBefore && !manager->LoadProject( projectPath, false ) )
    {
        error = "unreadable";
        message = "KiCad could not load the project file.";
    }
    else if( PROJECT* other = manager->GetProject( projectPath ) )
    {
        PROJECT_FILE& file = other->GetProjectFile();
        const bool    madeErc = !file.m_ErcSettings;
        const bool    madeSch = !file.m_SchematicSettings;

        if( madeErc )
        {
            file.m_ErcSettings = new ERC_SETTINGS( &file, "erc" );
            file.m_ErcSettings->LoadFromFile();
        }

        if( madeSch )
        {
            file.m_SchematicSettings = new SCHEMATIC_SETTINGS( &file, "schematic" );
            file.m_SchematicSettings->LoadFromFile();
        }

        const std::set<std::string> known = ccSchSections();

        for( const std::string& name : aArgs.sections )
        {
            json value;

            if( known.count( name ) )
            {
                try
                {
                    value = ccSchSettingsRead( other, name );
                }
                catch( ... )
                {
                    value = json();
                }
            }

            if( value.is_null() )
                missing.push_back( name );
            else
                sections[name] = std::move( value );
        }

        if( madeSch )
        {
            delete file.m_SchematicSettings;
            file.m_SchematicSettings = nullptr;
        }

        if( madeErc )
        {
            delete file.m_ErcSettings;
            file.m_ErcSettings = nullptr;
        }

        if( !loadedBefore && other != &frame->Prj() )
            manager->UnloadProject( other, false );
    }
    else
    {
        error = "unreadable";
        message = "KiCad could not load the project file.";
    }

    writeReadRecord( aArgs.dir, sections, missing, error, message );
}
} // namespace read
} // namespace sch

const bool kSchEditing = ccRegisterSchSection( { "schEditing", "sch", &sch::schEditingGet, &sch::schEditingSet,
                                                 false } );
const bool kSchMouse = ccRegisterSchSection( { "mouse", "sch", &sch::mouseGet, &sch::mouseSet, false } );
const bool kAnnotation = ccRegisterSchSection( { "annotation", "sch", &sch::annotationGet, &sch::annotationSet, true,
                                                false, &sch::annotationRead } );
const bool kBomPresets = ccRegisterSchSection( { "bomPresets", "sch", &sch::bomPresetsGet, &sch::bomPresetsSet, true,
                                                false, &sch::bomPresetsRead } );
const bool kSchComponentClasses = ccRegisterSchSection( { "componentClasses", "sch", &sch::componentClassesGet,
                                                          &sch::componentClassesSet,
                                                          true, false, &sch::componentClassesRead } );

// The schematic half of kicadSettingsRead (registered in cc_e12_dispatch.inc, which picks it on
// schEditorActive()): the arguments read whole (a staged board is not read here), then the
// read queued on the apply coroutine. false for any other shape, no frame or an open in flight.
bool settingsReadSch( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return false;

    try
    {
        std::optional<sch::read::READ_ARGS> args = sch::read::readArgs( aArgsJson );

        if( !args )
            return false;

        pcbjam_collab::runOnCoroutine( fr, [a = *args]() { sch::read::runSch( a ); } );
        return true;
    }
    catch( ... )
    {
        return false;
    }
}
} // namespace cc_e12

EMSCRIPTEN_BINDINGS( cc_e12_sch )
{
}
