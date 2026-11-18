// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e7, the dispatcher face: textually included by kicad_editor_embind.cpp just
// before EMSCRIPTEN_BINDINGS( kicad_editor ). That TU is header light (include subdir
// common): no pcbnew or eeschema header here. An export of both frames declares its
// halves as externs in namespace cc_eN and picks one on pcbEditorActive(); the halves
// are defined (external linkage, never static) in this lane's own pcb, sch or fpedit inc.
//
// Every definition of the lane lives in namespace cc_e7 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

#include <json_common.h>

namespace cc_e7
{

// e-librows (PM11, SE40, DB4). kicadLibsAddEntry (run 14's registration, widened in
// pcbjam_libs_reload.h to the kind design_block) adds a run time row; this removes one again.
// "symbol", "footprint" or "design_block" and a nickname kicadLibsAddEntry added in this
// session; false for any other kind, a boot row, an open in flight or no frame.
bool libsRemoveEntry( std::string aKind, std::string aNickname )
{
    if( pcbjam_open::busy() )
        return false;

    if( !pcbjam_libs::removeLibraryEntry( aKind, aNickname ) )
        return false;

    // pcbnew's second footprint cache (libs 0019 F1) goes with the row.
    if( aKind == "footprint" && pcbEditorActive() )
        pcbLibsInvalidatePreloaded( aNickname );

    return true;
}

// kicadPathVarsSet( {"vars": {NAME: "/absolute/folder"}} ): read whole and checked before
// anything is queued: exactly the one key vars, an object of 1 to 64 entries, each name in the
// island's form and neither KIPRJMOD nor one of KiCad's predefined variables
// (pcbjam_libs::pathVarNameOk), each value a string starting with '/' and holding no NUL.
// Any other shape, an open in flight or no frame answers false and sets nothing. Then the
// variables are set as KiCad's Configure Paths sets them and every row naming one is reloaded
// (pcbjam_libs::setPathVars). Not a design change: no undo step, nothing marked modified.
bool pathVarsSet( std::string aArgsJson )
{
    nlohmann::json args = nlohmann::json::parse( aArgsJson, nullptr, false );

    if( args.is_discarded() || !args.is_object() || args.size() != 1 || !args.contains( "vars" ) )
        return false;

    const nlohmann::json& vars = args["vars"];

    if( !vars.is_object() || vars.empty() || vars.size() > 64 )
        return false;

    std::vector<std::pair<std::string, std::string>> parsed;
    std::vector<std::string>                         names;

    for( auto it = vars.begin(); it != vars.end(); ++it )
    {
        if( !pcbjam_libs::pathVarNameOk( it.key() ) || !it.value().is_string() )
            return false;

        const std::string value = it.value().get<std::string>();

        if( value.empty() || value[0] != '/' || value.find( '\0' ) != std::string::npos )
            return false;

        parsed.emplace_back( it.key(), value );
        names.push_back( it.key() );
    }

    if( pcbjam_open::busy() )
        return false;

    if( !pcbjam_libs::setPathVars( parsed ) )
        return false;

    if( pcbEditorActive() )
    {
        for( const std::string& nick : pcbjam_libs::footprintRowsNaming( names ) )
            pcbLibsInvalidatePreloaded( nick );
    }

    return true;
}

// e-block-save (DB2, K-B3). kicadDesignBlockSave( {"run", "dir", "name", "scope", "sheetPath"?} )
// on whichever editor is live: the board half (cc_e7_pcb.inc, merged image only) takes the scopes
// board, selection and linkedGroup, the schematic half (cc_e7_sch.inc) sheet and selection. Each
// half reads the arguments whole and answers false before anything is queued.
bool blockSavePcb( std::string aArgsJson );
bool blockSaveSch( std::string aArgsJson );

bool designBlockSave( std::string aArgsJson )
{
    if( pcbEditorActive() )
        return blockSavePcb( aArgsJson );

    if( schEditorActive() )
        return blockSaveSch( aArgsJson );

    return false;
}

} // namespace cc_e7

EMSCRIPTEN_BINDINGS( cc_e7_dispatch )
{
    emscripten::function( "kicadDesignBlockSave", &cc_e7::designBlockSave );
    emscripten::function( "kicadLibsRemoveEntry", &cc_e7::libsRemoveEntry );
    emscripten::function( "kicadPathVarsSet", &cc_e7::pathVarsSet );
}
