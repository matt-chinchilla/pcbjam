// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15: the 3D export's fixes (pcb_export_3d), called by cc_output::MergeSettings.
// Included ONLY at the end of cc_output_common.h, which declares the function before
// MergeSettings uses it. Every format KiCad's 3D export job names is made: VRML by KiCad's own
// exporter in the engine, the OpenCASCADE formats by the 3D service (exporter_step_stub.cpp).
// The island sends "cc_occ": false when it has no 3D service; then only VRML is made.

#pragma once

#ifdef __EMSCRIPTEN__

namespace cc_output
{

/** KiCad's 3D export formats (the job's FORMAT names) and the extension of the file each makes. */
inline const char* ccExport3dExtension( const std::string& aFormat )
{
    static const std::pair<const char*, const char*> kFormats[] = {
        { "step", "step" }, { "stpz", "stpz" }, { "brep", "brep" }, { "xao", "xao" },
        { "glb", "glb" },   { "ply", "ply" },   { "stl", "stl" },   { "u3d", "u3d" },
        { "pdf", "pdf" },   { "vrml", "wrl" },
    };

    for( const auto& [name, ext] : kFormats )
    {
        if( aFormat == name )
            return ext;
    }

    return nullptr;
}

/**
 * The pcb_export_3d fixes on aMerged (the job's settings after the sent keys). A key is forced
 * only where the fresh job has it (aDefaults). aSent is what the host sent: its "cc_occ" (not a
 * job key, so MergeSettings named it dropped) is read here and taken off the dropped list. A
 * refusal goes in aOut.refusal; aOut.expectFile is the extension of the file a run must leave.
 * False refuses the whole call ("cc_occ" not a boolean).
 */
inline bool ccFixPcbExport3d( json& aMerged, const json& aDefaults, const json& aSent,
                              MERGE_RESULT& aOut )
{
    auto force = [&]( const char* aKey, const json& aValue )
                 {
                     if( aDefaults.contains( aKey ) )
                         aMerged[aKey] = aValue;
                 };

    bool occ = true;
    auto sentOcc = aSent.find( "cc_occ" );

    if( sentOcc != aSent.end() )
    {
        if( !sentOcc->is_boolean() )
            return false;

        occ = sentOcc->get<bool>();
        aOut.dropped.erase( std::remove( aOut.dropped.begin(), aOut.dropped.end(),
                                         std::string( "cc_occ" ) ),
                            aOut.dropped.end() );
    }

    auto              format = aMerged.find( "format" );
    const std::string name = ( format != aMerged.end() && format->is_string() )
                                     ? format->get<std::string>()
                                     : std::string();
    const char*       ext = ccExport3dExtension( name );

    if( !ext )
        aOut.refusal = "The 3D file is made as STEP, STPZ, BREP, XAO, GLB, PLY, STL, U3D, PDF or "
                       "VRML.";
    else if( !occ && name != "vrml" )
        aOut.refusal = "Only VRML is made here.";
    else
        aOut.expectFile = ext;

    force( "overwrite", true );
    force( "vrml_model_dir", "" );
    force( "vrml_relative_paths", false );
    force( "variant", "" );
    return true;
}

} // namespace cc_output

#endif // __EMSCRIPTEN__
