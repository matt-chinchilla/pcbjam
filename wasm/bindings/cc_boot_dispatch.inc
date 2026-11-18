// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, boot-e1 (RULINGS 15: nothing of KiCad's before the page's engine view), the
// dispatcher face: textually included by kicad_editor_embind.cpp just before
// EMSCRIPTEN_BINDINGS( kicad_editor ), after the chrome toggle (applyChrome, s_chromeSnap),
// which it uses. Header light: no pcbnew or eeschema header.
//
// The start-up input: the page sets window.kicadBoot = { chrome: false } before the engine's
// script runs. KIWAY::Player then hands the frame being booted to onBootFrame (kicad
// include/cc_boot_chrome.h, common/kiway.cpp) while it is still hidden, and KiCad's menu bar,
// toolbars, panes and status bar are held hidden on it before single_top.cpp shows it: the
// frame's first paint is already the canvas only layout, and the hold is the same sticky one
// kicadSetChrome( false ) sets (a later kicadSetChrome( true ) brings KiCad's own chrome back
// from the snapshot taken here). Without the input the boot is as before.
//
// The marker kicadBootCanvasOnly tells the island this engine does three things: it reads the
// start-up input above, kicadSetChrome paints before it returns, and an empty drawing area is
// erased in the drawing's background color, never wx's face color (kicad
// common/draw_panel_gal.cpp).

#include <cc_boot_chrome.h>

namespace cc_boot
{
// The page's start-up input: true when window.kicadBoot.chrome is false.
static bool bootAsksChromeHidden()
{
    return EM_ASM_INT( {
               try
               {
                   return ( window.kicadBoot && window.kicadBoot.chrome === false ) ? 1 : 0;
               }
               catch( e )
               {
                   return 0;
               }
           } )
           != 0;
}

// KIWAY::Player's start-up hook: the frame being booted, created and not yet shown.
static void onBootFrame( wxFrame* aFrame )
{
    if( aFrame && bootAsksChromeHidden() )
        applyChrome( aFrame, false );
}

const bool kBootFrameHook = ( ccBootFrameHook() = &onBootFrame, true );

// The marker (island unit boot-i1 reads it).
bool bootCanvasOnly()
{
    return true;
}
} // namespace cc_boot

EMSCRIPTEN_BINDINGS( cc_boot_dispatch )
{
    function( "kicadBootCanvasOnly", &cc_boot::bootCanvasOnly );
}
