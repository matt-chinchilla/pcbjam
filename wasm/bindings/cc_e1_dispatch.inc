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
} // namespace cc_e1

EMSCRIPTEN_BINDINGS( cc_e1_dispatch )
{
    emscripten::function( "kicadHostPrompts", &cc_e1::hostPrompts );
    emscripten::function( "kicadPromptAnswer", &cc_e1::promptAnswer );
    emscripten::function( "kicadPromptCancel", &cc_e1::promptCancel );
    emscripten::function( "kicadPromptPick", &cc_e1::promptPick );
}
