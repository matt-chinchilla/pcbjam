// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Embind bindings for the footprint editor frame (FOOTPRINT_EDIT_FRAME, --frame fpedit) of the
// merged kicad_editor WASM image: engine run 15, the island's fpedit.* ops. Its own TU beside
// pcbnew_embind.cpp, the symedit_embind.cpp precedent: compiled by
// scripts/kicad/build-kicad-target.sh with the pcbnew include set and WITHOUT
// -DKICAD_MERGED_EMBIND (every name here is new and registered once), linked into the merged
// image only.
//
// The seams commit leaves it a frame: the shared headers, then each author lane's footprint
// editor face (cc_eN_fpedit.inc), each with its own namespace and bindings block. Lane e4's
// fpedit_common.h (fpEditFrame(), the closed JSON readers, the commit helper) is taken in the
// slot below as soon as it lands; no lane edits this file.

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <emscripten/bind.h>

#include <nlohmann/json.hpp>

#include <string>

#include "cc_output_common.h"
#include "cc_fp_items.h"

// The slot for lane e4's early header commit: included once the file exists.
#if __has_include( "fpedit_common.h" )
#include "fpedit_common.h"
#endif

using namespace emscripten;
using json = nlohmann::json;

#include "cc_e4_fpedit.inc"
#include "cc_e5_fpedit.inc"
#include "cc_e6_fpedit.inc"

#include <footprint_editor_settings.h>

#include "pcbjam_theme.h"

// kicadSetColorTheme's footprint editor half, called by the dispatcher (kicad_editor_embind.cpp
// setColorTheme), a no-op when this frame is not the top window. The frame's colors come from
// the footprint editor's own settings (FOOTPRINT_EDIT_FRAME::GetColorSettings,
// pcbnew/footprint_edit_frame.cpp 885 to 889), so their color_theme is the one set; the
// frame's CommonSettingsChanged reloads the painter's colors and repaints, the view kept.
// That function also reads the GAL options back from the editor's window settings, whose
// grid.axes_enabled is false until KiCad's own SaveSettings forces it on (it sets
// m_axesEnabled true before saving, footprint_edit_frame.cpp 833): the apply would hide the
// footprint's origin axes. So the same invariant is set first, on the apply queue (FIFO).
void fpSetColorTheme( std::string aTheme )
{
    FOOTPRINT_EDIT_FRAME* frame = fpEditFrame();

    if( !frame )
        return;

    pcbjam_collab::runOnCoroutine( frame, []() {
        if( FOOTPRINT_EDIT_FRAME* fr = fpEditFrame() )
        {
            if( FOOTPRINT_EDITOR_SETTINGS* cfg = fr->GetFootprintEditorSettings() )
                cfg->m_Window.grid.axes_enabled = true;
        }
    } );

    pcbjam_theme::setColorTheme( frame, aTheme, frame->GetFootprintEditorSettings() );
}

#endif // __EMSCRIPTEN__
