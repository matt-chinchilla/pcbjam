// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e9, the board face: textually included by pcbnew_embind.cpp after cc_rules_pcb.inc,
// outside any KICAD_MERGED_EMBIND guard (include subdir pcbnew; the merged image compiles
// that TU with -DKICAD_MERGED_EMBIND). It sees the TU's includes and helpers (pcbFrame(),
// the cc_output namespace, cc_fp_items.h's declarations in the merged image only) and calls them without editing them.
//
// Every definition of the lane lives in namespace cc_e9 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

#include <connectivity/connectivity_data.h>

// K-SES, defined in kicad pcbnew/specctra_import_export/specctra_import.cpp under
// __EMSCRIPTEN__ (the same image). Declared here: specctra.h includes a generated lexer header
// outside the bindings' include roots (as DSN::ExportBoardToSpecctraFile above).
namespace DSN
{
void CcImportSpecctraSession( BOARD* aBoard, const wxString& fullFileName, BOARD_COMMIT* aCommit );
void CcCountSpecctraSession( BOARD* aBoard, const wxString& fullFileName, int& aTracks, int& aVias,
                             std::vector<std::string>& aNets,
                             std::vector<std::string>& aMissingNets );
} // namespace DSN

namespace cc_e9
{
namespace pcb
{
struct SESSION_ARGS
{
    long long   run = 0;
    std::string dir;
    std::string path;
    bool        preview = true;
};


/** An absolute path with no NUL, no "..", at most 4,096 bytes, ending in aExt. */
static bool isFilePath( const std::string& aPath, const std::string& aExt )
{
    return !aPath.empty() && aPath.size() <= 4096 && aPath[0] == '/'
           && aPath.find( '\0' ) == std::string::npos && aPath.find( ".." ) == std::string::npos
           && aPath.size() > aExt.size()
           && aPath.compare( aPath.size() - aExt.size(), aExt.size(), aExt ) == 0;
}


/** The board's tracks and arcs, and its vias, counted apart; locked ones alone when asked. */
static void countTracks( BOARD* aBoard, bool aLockedOnly, int& aTracks, int& aVias )
{
    aTracks = 0;
    aVias = 0;

    for( PCB_TRACK* track : aBoard->Tracks() )
    {
        if( aLockedOnly && !track->IsLocked() )
            continue;

        if( track->Type() == PCB_VIA_T )
            aVias++;
        else
            aTracks++;
    }
}


/**
 * The session run, on the apply coroutine. The session is loaded and counted against the board
 * (CcCountSpecctraSession: nothing changes); a preview stops there. An apply then reads it
 * through ONE BOARD_COMMIT (CcImportSpecctraSession, K-SES): the unlocked tracks and vias and
 * the markers removed, the placed footprints moved, the routes added, pushed as one undo step;
 * a throw reverts the commit, so the board is as it was. Writes <dir>/session.json (the
 * report, also for a failed apply) then <dir>/.cc-session.json LAST: {error, message, lines}.
 */
void runSession( const SESSION_ARGS& aArgs )
{
    cc_output::LINES_REPORTER lines;
    std::string               error;
    std::string               message;
    json                      report;

    ccEnsureDir( aArgs.dir );

    auto finish = [&]()
                  {
                      if( !report.is_null() )
                      {
                          report["lines"] = cc_output::LinesJson( lines );
                          ccWriteJson( aArgs.dir, "session.json", report );
                      }

                      json done = json::object();
                      done["error"] = error;
                      done["message"] = message;
                      done["lines"] = cc_output::LinesJson( lines );
                      ccWriteJson( aArgs.dir, ".cc-session.json", done );
                  };

    PCB_EDIT_FRAME* fr = pcbFrame();   // re-resolved on the coroutine (S4)

    if( !fr || !fr->GetBoard() )
    {
        error = "island_error";
        message = "no board editor";
        finish();
        return;
    }

    BOARD*         board = fr->GetBoard();
    const wxString file = wxString::FromUTF8( aArgs.path.c_str() );
    wxLogNull      noLog;

    int                      tracks = 0;
    int                      vias = 0;
    std::vector<std::string> nets;
    std::vector<std::string> missing;

    try
    {
        DSN::CcCountSpecctraSession( board, file, tracks, vias, nets, missing );
    }
    catch( const IO_ERROR& ioe )
    {
        error = "session_invalid";
        message = toUtf8( ioe.What() );
        lines.Report( ioe.What(), RPT_SEVERITY_ERROR );
        finish();
        return;
    }
    catch( const std::exception& e )
    {
        error = "session_invalid";
        message = e.what();
        finish();
        return;
    }

    report = json::object();
    report["run"] = aArgs.run;
    report["preview"] = aArgs.preview;
    report["tracks"] = tracks;
    report["vias"] = vias;
    report["nets"] = (int) nets.size();

    json missingNets = json::array();

    for( size_t i = 0; i < missing.size() && i < 256; ++i )
        missingNets.push_back( missing[i] );

    report["missingNets"] = missingNets;
    report["undoCleared"] = false;

    if( aArgs.preview )
    {
        report["depth"] = fr->GetUndoCommandCount();
        finish();
        return;
    }

    int lockedTracks = 0;
    int lockedVias = 0;
    int allTracks = 0;
    int allVias = 0;

    countTracks( board, true, lockedTracks, lockedVias );
    countTracks( board, false, allTracks, allVias );

    BOARD_COMMIT commit( fr );

    try
    {
        DSN::CcImportSpecctraSession( board, file, &commit );
    }
    catch( const IO_ERROR& ioe )
    {
        commit.Revert();
        error = "session_invalid";
        message = toUtf8( ioe.What() );
        lines.Report( ioe.What(), RPT_SEVERITY_ERROR );
        report["depth"] = fr->GetUndoCommandCount();
        finish();
        return;
    }
    catch( const std::exception& e )
    {
        commit.Revert();
        error = "session_invalid";
        message = e.what();
        report["depth"] = fr->GetUndoCommandCount();
        finish();
        return;
    }

    commit.Push( _( "Import Specctra Session" ) );

    // As DSN::ImportSpecctraSession ends: the ratsnest rebuilt from the new copper.
    board->GetConnectivity()->ClearRatsnest();
    board->BuildConnectivity();
    fr->Compile_Ratsnest( true );
    fr->GetCanvas()->Refresh();

    int afterTracks = 0;
    int afterVias = 0;

    countTracks( board, false, afterTracks, afterVias );

    json removed = json::object();
    removed["tracks"] = allTracks - lockedTracks;
    removed["vias"] = allVias - lockedVias;

    json added = json::object();
    added["tracks"] = afterTracks - lockedTracks;
    added["vias"] = afterVias - lockedVias;

    report["removed"] = removed;
    report["added"] = added;
    report["depth"] = fr->GetUndoCommandCount();
    lines.Report( _( "Session file imported and merged OK." ), RPT_SEVERITY_INFO );
    finish();
}
} // namespace pcb


// board.session (cap session). aArgsJson exactly {"run": int >= 0, "dir":
// "/tmp/cc-session/<n>", "path": "<abs>.ses", "preview": bool}. False (nothing queued) for any
// other JSON, a path that is not absolute, holds ".." or NUL, does not end ".ses" or names no
// file, with no board, or while an open is in flight; true once the run is queued on the apply
// coroutine (the host waits for the apply queue to empty and <dir>/.cc-session.json).
bool importSession( std::string aArgsJson )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 4 )
        return false;

    auto run = args.find( "run" );
    auto dir = args.find( "dir" );
    auto path = args.find( "path" );
    auto preview = args.find( "preview" );

    if( run == args.end() || !run->is_number_integer() || run->get<long long>() < 0
            || dir == args.end() || !dir->is_string() || path == args.end()
            || !path->is_string() || preview == args.end() || !preview->is_boolean() )
    {
        return false;
    }

    pcb::SESSION_ARGS a;
    a.run = run->get<long long>();
    a.dir = dir->get<std::string>();
    a.path = path->get<std::string>();
    a.preview = preview->get<bool>();

    if( !ccIsRunDir( a.dir, "/tmp/cc-session/" ) || !pcb::isFilePath( a.path, ".ses" )
            || !wxFileName::FileExists( wxString::FromUTF8( a.path.c_str() ) ) )
    {
        return false;
    }

    pcbjam_collab::runOnCoroutine( fr, [a]() { pcb::runSession( a ); } );
    return true;
}
} // namespace cc_e9

EMSCRIPTEN_BINDINGS( cc_e9_pcb )
{
    function( "kicadImportSession", &cc_e9::importSession );
}
