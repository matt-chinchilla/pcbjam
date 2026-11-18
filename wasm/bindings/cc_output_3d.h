// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15: the 3D export's fixes (pcb_export_3d), called by cc_output::MergeSettings.
// Included ONLY at the end of cc_output_common.h, which declares the function before
// MergeSettings uses it. Lane e11 owns the body after the seams commit (every 3D format through
// the OpenCASCADE service); the seam holds run 14's behavior unchanged: VRML only, refused
// otherwise, with the forced keys below.

#pragma once

#ifdef __EMSCRIPTEN__

namespace cc_output
{

/**
 * The pcb_export_3d fixes on aMerged (the job's settings after the sent keys). A key is forced
 * only where the fresh job has it (aDefaults). A refusal goes in aOut.refusal.
 */
inline void ccFixPcbExport3d( json& aMerged, const json& aDefaults, MERGE_RESULT& aOut )
{
    auto force = [&]( const char* aKey, const json& aValue )
                 {
                     if( aDefaults.contains( aKey ) )
                         aMerged[aKey] = aValue;
                 };

    auto format = aMerged.find( "format" );

    if( format == aMerged.end() || !format->is_string() || format->get<std::string>() != "vrml" )
        aOut.refusal = "Only VRML is made here.";

    force( "overwrite", true );
    force( "vrml_model_dir", "" );
    force( "vrml_relative_paths", false );
    force( "subst_models", false );
}

} // namespace cc_output

#endif // __EMSCRIPTEN__
