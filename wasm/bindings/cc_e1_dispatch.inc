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

    if( CcHostMenuParked() || CcHostPromptModalParked() || toolMgr->IsContextMenuActive()
            || modalDialogUp() || frame->Kiway().GetBlockingDialog() || pcbjam_open::busy() )
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
}
