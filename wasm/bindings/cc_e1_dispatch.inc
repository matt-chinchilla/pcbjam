// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e1, the dispatcher face: textually included by kicad_editor_embind.cpp just
// before EMSCRIPTEN_BINDINGS( kicad_editor ). That TU is header light (include subdir
// common): no pcbnew or eeschema header here. An export of both frames declares its
// halves as externs in namespace cc_eN and picks one on pcbEditorActive(); the halves
// are defined (external linkage, never static) in this lane's own pcb, sch or fpedit inc.
//
// Every definition of the lane lives in namespace cc_e1 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

#include <host_prompt.h>
#include <tool/host_context_menu.h>
#include <hotkeys_basic.h>
#include <tool/action_manager.h>
#include <tool/tool_action.h>
#include <tool/tool_manager.h>
#include <wx/dialog.h>
#include <wx/toplevel.h>
#include "collab_common.h"
#include "open_gate.h"

namespace cc_e1
{
// ---- m1x-e1: host prompts (kicad include/host_prompt.h, canvas addendum 4.1) -------------------

// A marker: this engine's dialogs ask the page first (window.kicadHostPrompt.onAsk) before they
// show: the entry dialogs, the layer chooser, and every ask other lanes hook through the same
// header. The island lists its cap prompts on it.
bool hostPrompts()
{
    return true;
}

static std::string promptAnswerText( const nlohmann::json& aResult )
{
    return aResult.dump( -1, ' ', false, nlohmann::json::error_handler_t::replace );
}

// The page's answer to a parked prompt: {"accepted":true}, {"accepted":false,"field","message"}
// (the prompt stays parked) or {"stale":true}; "" for an answer that is not a JSON object.
std::string promptAnswer( int aToken, std::string aAnswer )
{
    nlohmann::json answer = nlohmann::json::parse( aAnswer, nullptr, false );

    if( answer.is_discarded() || !answer.is_object() )
        return "";

    return promptAnswerText( CcHostPromptAnswer( aToken, answer ) );
}

// The page canceled a parked prompt: 0 canceled, 1 stale.
int promptCancel( int aToken )
{
    return CcHostPromptCancel( aToken );
}

// A modeless prompt's own picker ("item" or "point"): 0 runs, 1 stale, 2 this ask has none.
int promptPick( int aToken, std::string aKind )
{
    return CcHostPromptPick( aToken, aKind );
}

// ---- st-hold-e: K-HOLD (kicad ACTION_MENU::OnMenuEvent, ACTION_TOOLBAR, the language range) ----

// A marker: this engine's menu bar, context menu, toolbar and palette presses ask the page as its
// keys do (run 12's window.kicadHostAction.onRequest), and the Set Language items ask with the
// name cc.Control.setLanguage. The island lists its cap heldMenus on it.
bool heldMenus()
{
    return true;
}

// ---- m1-e: K-MENU (kicad include/tool/host_context_menu.h, canvas addendum 3.1) ----------------

// A marker: this engine reports every canvas context menu and clarify list to the page first
// (window.kicadHostMenu.onMenu) and shows KiCad's popup only when the page does not take it.
bool hostContextMenu()
{
    return true;
}

// The page's choice in the parked menu (-1 cancels): 0 done, 1 stale, 2 no item with that id,
// 3 disabled, 4 a submenu or separator.
int contextMenuChoose( int aToken, int aId )
{
    return CcHostMenuChoose( aToken, aId );
}

// The item under the page's pointer in the parked menu (the clarify list brightens it): the
// codes of contextMenuChoose.
int contextMenuHighlight( int aToken, int aId )
{
    return CcHostMenuHighlight( aToken, aId );
}

// True while a modal KiCad dialog shows on the port.
static bool modalDialogUp()
{
    for( wxWindowList::compatibility_iterator node = wxTopLevelWindows.GetFirst(); node;
         node = node->GetNext() )
    {
        wxDialog* dialog = dynamic_cast<wxDialog*>( node->GetData() );

        if( dialog && dialog->IsModal() )
            return true;
    }

    return false;
}

// Run one tool action of the live frame by name, as a menu press would (on its own coroutine,
// from the frame's event loop, so an action that opens a window may suspend): 0 dispatched,
// 1 not a tool action of this frame (nothing run), 3 busy (a parked menu, a parked prompt, a
// modal dialog, KiCad's own popup or an open in flight), 4 no frame. Never asks
// hostHoldsAction: the island refuses held names first.
int runAction( std::string aName )
{
    KIWAY_PLAYER* frame =
            wxTheApp ? dynamic_cast<KIWAY_PLAYER*>( wxTheApp->GetTopWindow() ) : nullptr;
    TOOL_MANAGER* toolMgr = frame ? frame->GetToolManager() : nullptr;

    if( !toolMgr || !toolMgr->GetActionManager() )
        return 4;

    if( !toolMgr->GetActionManager()->FindAction( aName ) )
        return 1;

    // KiCad's DIALOG_SHIM names itself the Kiway's blocking dialog from its constructor to its
    // destructor, and a modeless dialog KiCad keeps alive hidden between uses (Position
    // Relative) stays named after it closes: only a shown one is busy.
    wxWindow* blocking = frame->Kiway().GetBlockingDialog();

    if( CcHostMenuParked() || CcHostPromptModalParked() || toolMgr->IsContextMenuActive()
            || modalDialogUp() || ( blocking && blocking->IsShown() ) || pcbjam_open::busy() )
    {
        return 3;
    }

    pcbjam_collab::runOnCoroutine( frame,
            [toolMgr, aName]()
            {
                toolMgr->RunAction( aName );
            } );

    return 0;
}
// ---- m12-e: kicadTableCsv, the CSV text of Export Table to CSV (both editors) ---------------

// The halves (cc_e1_pcb.inc, cc_e1_sch.inc): {"uuid"} -> {"name","text"} or "".
std::string tableCsvPcb( std::string aArgs );
std::string tableCsvSch( std::string aArgs );

// {"uuid"} of a table or one of its cells -> {"name","text"}, the text KiCad's own exporter
// builds (the held action exportTableCSV, cap tableCsv: the page saves <name>.csv). "" in the
// footprint, symbol and drawing sheet editors, with a modal dialog or a prompt up, and for
// anything the live editor's half refuses.
std::string tableCsv( std::string aArgs )
{
    if( fpEditorActive() || symEditorActive() || dsEditorActive() )
        return "";

    if( modalDialogUp() || CcHostPromptModalParked() )
        return "";

    if( pcbEditorActive() )
        return tableCsvPcb( aArgs );

    if( schEditorActive() )
        return tableCsvSch( aArgs );

    return "";
}
// ---- st-keys-e: kicadHotkeysGet, kicadHotkeysSet (the editor's keys, both editors) ----------

// The live schematic or board editor's tool manager and its actions' prefix ("eeschema",
// "pcbnew"); null in any other frame or with no frame.
static TOOL_MANAGER* keysToolManager( std::string& aApp )
{
    if( fpEditorActive() || symEditorActive() || dsEditorActive() )
        return nullptr;

    if( pcbEditorActive() )
        aApp = "pcbnew";
    else if( schEditorActive() )
        aApp = "eeschema";
    else
        return nullptr;

    KIWAY_PLAYER* frame =
            wxTheApp ? dynamic_cast<KIWAY_PLAYER*>( wxTheApp->GetTopWindow() ) : nullptr;
    TOOL_MANAGER* toolMgr = frame ? frame->GetToolManager() : nullptr;

    return toolMgr && toolMgr->GetActionManager() ? toolMgr : nullptr;
}

// An action of this editor's key list, as KiCad's hotkey list counts them (HOTKEY_STORE::Init):
// this editor's own and the common ones, never an internal action (no friendly name).
static bool keysListed( const TOOL_ACTION* aAction, const std::string& aApp )
{
    const std::string& name = aAction->GetName();

    if( aAction->GetFriendlyName().IsEmpty() )
        return false;

    return name.rfind( aApp + ".", 0 ) == 0 || name.rfind( "common.", 0 ) == 0;
}

// KiCad's key grammar ("Ctrl+Shift+M"); "" for no key.
static std::string keysName( int aKeycode )
{
    if( aKeycode <= 0 )
        return std::string();

    return KeyNameFromKeyCode( aKeycode ).ToStdString( wxConvUTF8 );
}

// Every action of the live editor's key list with its keys: {"editor":"sch"|"pcb","actions":
// [{"name","label","key","default","alt"}]}, sorted by name; key the primary key now, default
// KiCad's own, alt the secondary (the island's seeded chord). "" in any other frame. A read.
std::string hotkeysGet()
{
    std::string   app;
    TOOL_MANAGER* toolMgr = keysToolManager( app );

    if( !toolMgr )
        return "";

    nlohmann::json actions = nlohmann::json::array();

    for( const auto& [name, action] : toolMgr->GetActionManager()->GetActions() )
    {
        if( !action || !keysListed( action, app ) )
            continue;

        actions.push_back( { { "name", name },
                             { "label", action->GetFriendlyName().ToStdString( wxConvUTF8 ) },
                             { "key", keysName( action->GetHotKey() ) },
                             { "default", keysName( action->GetDefaultHotKey() ) },
                             { "alt", keysName( action->GetHotKeyAlt() ) } } );
    }

    nlohmann::json answer;
    answer["editor"] = app == "pcbnew" ? "pcb" : "sch";
    answer["actions"] = actions;

    return answer.dump( -1, ' ', false, nlohmann::json::error_handler_t::replace );
}

// {"keys":{"<action>":"<key>"|""}} (at most 2,048): each named action's PRIMARY key, in KiCad's
// grammar, "" for none; the secondary stays as it is (the island's seeded chord). Read whole
// first: an action not in this editor's list, a key KiCad cannot read, any other shape, a
// modal dialog, a parked prompt or an open in flight refuses the whole set (false, nothing
// changes). Then the action manager's key map is rebuilt (ACTION_MANAGER::UpdateHotKeys,
// from the actions, no file read). Not an edit: no undo step, no modified flag.
bool hotkeysSet( std::string aArgs )
{
    std::string   app;
    TOOL_MANAGER* toolMgr = keysToolManager( app );

    if( !toolMgr || modalDialogUp() || CcHostPromptModalParked() || pcbjam_open::busy() )
        return false;

    const nlohmann::json args = nlohmann::json::parse( aArgs, nullptr, false );

    if( args.is_discarded() || !args.is_object() || args.size() != 1 || !args.contains( "keys" )
        || !args["keys"].is_object() || args["keys"].size() > 2048 )
    {
        return false;
    }

    ACTION_MANAGER*                          actionMgr = toolMgr->GetActionManager();
    std::vector<std::pair<TOOL_ACTION*, int>> changes;

    for( const auto& [name, key] : args["keys"].items() )
    {
        TOOL_ACTION* action = actionMgr->FindAction( name );

        if( !action || !keysListed( action, app ) || !key.is_string() )
            return false;

        const std::string text = key.get<std::string>();
        int               code = 0;

        if( !text.empty() )
        {
            code = KeyCodeFromKeyName( wxString::FromUTF8( text ) );

            if( code <= 0 )
                return false;
        }

        changes.emplace_back( action, code );
    }

    for( const auto& [action, code] : changes )
        action->SetHotKey( code, action->GetHotKeyAlt() );

    actionMgr->UpdateHotKeys( false );
    return true;
}
// ---- m6-e3: kicadSelectionFilterGet, kicadSelectionFilterSet (both editors) -------------------

// The halves (cc_e1_pcb.inc, cc_e1_sch.inc, merged image only).
std::string selectionFilterGetPcb();
std::string selectionFilterGetSch();
bool        selectionFilterSetPcb( std::string aArgs );
bool        selectionFilterSetSch( std::string aArgs );

// The live editor's selection filter (KiCad's Selection Filter pane): the board's twelve keys
// or the schematic's ten, each a boolean. "" in any other frame.
std::string selectionFilterGet()
{
    if( fpEditorActive() || symEditorActive() || dsEditorActive() )
        return "";

    if( pcbEditorActive() )
        return selectionFilterGetPcb();

    if( schEditorActive() )
        return selectionFilterGetSch();

    return "";
}

// Some of the live editor's keys with booleans: true taken, false refused (nothing changes).
bool selectionFilterSet( std::string aArgs )
{
    if( fpEditorActive() || symEditorActive() || dsEditorActive() )
        return false;

    if( modalDialogUp() || CcHostPromptModalParked() )
        return false;

    if( pcbEditorActive() )
        return selectionFilterSetPcb( aArgs );

    if( schEditorActive() )
        return selectionFilterSetSch( aArgs );

    return false;
}
} // namespace cc_e1

EMSCRIPTEN_BINDINGS( cc_e1_dispatch )
{
    emscripten::function( "kicadHostPrompts", &cc_e1::hostPrompts );
    emscripten::function( "kicadPromptAnswer", &cc_e1::promptAnswer );
    emscripten::function( "kicadPromptCancel", &cc_e1::promptCancel );
    emscripten::function( "kicadPromptPick", &cc_e1::promptPick );
    emscripten::function( "kicadHeldMenus", &cc_e1::heldMenus );
    emscripten::function( "kicadHostContextMenu", &cc_e1::hostContextMenu );
    emscripten::function( "kicadContextMenuChoose", &cc_e1::contextMenuChoose );
    emscripten::function( "kicadContextMenuHighlight", &cc_e1::contextMenuHighlight );
    emscripten::function( "kicadRunAction", &cc_e1::runAction );
    emscripten::function( "kicadTableCsv", &cc_e1::tableCsv );
    emscripten::function( "kicadHotkeysGet", &cc_e1::hotkeysGet );
    emscripten::function( "kicadHotkeysSet", &cc_e1::hotkeysSet );
    emscripten::function( "kicadSelectionFilterGet", &cc_e1::selectionFilterGet );
    emscripten::function( "kicadSelectionFilterSet", &cc_e1::selectionFilterSet );
}
