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
// island's form and neither KIPRJMOD nor one of KiCad's predefined variables other than the
// versioned 3RD_PARTY one (pcbjam_libs::pathVarNameOk), each value a string starting with '/'
// and holding no NUL.
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

// Repair e7-2, the marker kicadPathVarsThirdParty: this engine's kicadPathVarsSet admits KiCad's
// versioned 3RD_PARTY variable (KICAD10_3RD_PARTY on KiCad 10), so the island may point the
// add-on parts folder with it. Always true; registered beside kicadPathVarsSet.
bool pathVarsThirdParty()
{
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

// e-block-place (DB1, K-B1). kicadDesignBlockPlace( {"dir", "name", "asSheet",
// "keepAnnotations", "asGroup", optionally "nickname"} ) on whichever editor is live (with a
// nickname the placed group links its block as from the Design Blocks pane): the board half
// (cc_e7_pcb.inc, merged image only) places the block's layout, the schematic half (cc_e7_sch.inc) its
// schematic. Each half reads the arguments whole and the block itself before anything is queued.
bool blockPlacePcb( std::string aArgsJson );
bool blockPlaceSch( std::string aArgsJson );

bool designBlockPlace( std::string aArgsJson )
{
    if( pcbEditorActive() )
        return blockPlacePcb( aArgsJson );

    if( schEditorActive() )
        return blockPlaceSch( aArgsJson );

    return false;
}

// st-paste-e (K-PASTE). kicadPasteSpecial( {"mode": "unique" | "keep" | "remove",
// "clearNets": bool} ), read whole (exactly those two keys) before anything is queued; clearNets
// counts on the board only. The halves (cc_e7_pcb.inc, cc_e7_sch.inc) arm the choices and start
// KiCad's Paste Special on the apply coroutine.
bool pastePcb( int aMode, bool aClearNets );
bool pasteSch( int aMode, bool aClearNets );

bool pasteSpecial( std::string aArgsJson )
{
    nlohmann::json args = nlohmann::json::parse( aArgsJson, nullptr, false );

    if( args.is_discarded() || !args.is_object() || args.size() != 2 )
        return false;

    auto mode = args.find( "mode" );
    auto clearNets = args.find( "clearNets" );

    if( mode == args.end() || !mode->is_string() || clearNets == args.end()
            || !clearNets->is_boolean() )
    {
        return false;
    }

    const std::string m = mode->get<std::string>();
    const int         code = m == "unique" ? 0 : m == "keep" ? 1 : m == "remove" ? 2 : -1;

    if( code < 0 )
        return false;

    if( pcbEditorActive() )
        return pastePcb( code, clearNets->get<bool>() );

    if( schEditorActive() )
        return pasteSch( code, clearNets->get<bool>() );

    return false;
}

// e-sheetreload (DS1, PM10). kicadDrawingSheetReload(): the live board or schematic frame
// re-reads the drawing sheet its document names (KiCad's default when it names none or the file
// does not read) and redraws it, on the apply coroutine after any write queued before it. No
// undo step, nothing marked modified, no info bar. False with no board or schematic frame or an
// open in flight; true once queued (the host waits for the apply queue to empty). The halves
// are in cc_e7_pcb.inc and cc_e7_sch.inc.
bool sheetReloadPcb();
bool sheetReloadSch();

bool drawingSheetReload()
{
    if( pcbEditorActive() )
        return sheetReloadPcb();

    if( schEditorActive() )
        return sheetReloadSch();

    return false;
}

} // namespace cc_e7

EMSCRIPTEN_BINDINGS( cc_e7_dispatch )
{
    emscripten::function( "kicadDrawingSheetReload", &cc_e7::drawingSheetReload );
    emscripten::function( "kicadPasteSpecial", &cc_e7::pasteSpecial );
    emscripten::function( "kicadDesignBlockPlace", &cc_e7::designBlockPlace );
    emscripten::function( "kicadDesignBlockSave", &cc_e7::designBlockSave );
    emscripten::function( "kicadLibsRemoveEntry", &cc_e7::libsRemoveEntry );
    emscripten::function( "kicadPathVarsSet", &cc_e7::pathVarsSet );
    emscripten::function( "kicadPathVarsThirdParty", &cc_e7::pathVarsThirdParty );
}
