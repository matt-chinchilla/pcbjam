// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15: the render job (pcb_render), called by cc_output::MergeSettings and read by
// cc_output::JobsWithDefaults. Included ONLY at the end of cc_output_common.h, which declares
// both functions before their use. The job itself is KiCad's JobExportRender, compiled into the
// WebAssembly build by kicad's pcbnew/pcbnew_jobs_handler.cpp (no dialog, a yield to the browser
// and a "Rendering <n> percent" line per raytracer pass). Here: the settings the engine takes.

#pragma once

#ifdef __EMSCRIPTEN__

namespace cc_output
{

/** The largest side of a rendered picture, in pixels (the smallest is 16). */
inline constexpr long long kRenderMinSide = 16;
inline constexpr long long kRenderMaxSide = 4096;

/**
 * The pcb_render fixes on aMerged (the job's settings after the sent keys): format png or jpeg
 * (KiCad's own names), width and height whole numbers from 16 to 4,096, the appearance preset
 * empty or KiCad's Follow PCB Editor (sent as its key "follow_pcb_editor" or its menu words
 * "Follow PCB Editor"), no variant. Anything else is a refusal: the run makes nothing.
 */
inline void ccFixPcbRender( json& aMerged, const json& aDefaults, MERGE_RESULT& aOut )
{
    auto force = [&]( const char* aKey, const json& aValue )
                 {
                     if( aDefaults.contains( aKey ) )
                         aMerged[aKey] = aValue;
                 };

    auto format = aMerged.find( "format" );

    if( format == aMerged.end() || !format->is_string()
            || ( format->get<std::string>() != "png" && format->get<std::string>() != "jpeg" ) )
    {
        aOut.refusal = "The picture is made as PNG or JPEG.";
        return;
    }

    for( const char* side : { "width", "height" } )
    {
        auto value = aMerged.find( side );

        if( value == aMerged.end() || !value->is_number_integer()
                || value->get<long long>() < kRenderMinSide
                || value->get<long long>() > kRenderMaxSide )
        {
            aOut.refusal = "The picture's width and height are 16 to 4,096 pixels.";
            return;
        }
    }

    auto preset = aMerged.find( "preset" );

    if( preset != aMerged.end() )
    {
        const std::string name = preset->is_string() ? preset->get<std::string>() : "?";

        if( name == "Follow PCB Editor" || name == "follow_pcb_editor" )
        {
            force( "preset", "follow_pcb_editor" );
        }
        else if( !name.empty() )
        {
            aOut.refusal = "The render follows the board editor's look or none.";
            return;
        }
    }

    force( "variant", "" );
}

/** Whether kicadOutputJobs lists pcb_render: the render job is compiled in (engine run 15). */
inline bool ccPcbRenderListed()
{
    return true;
}

} // namespace cc_output

#endif // __EMSCRIPTEN__
