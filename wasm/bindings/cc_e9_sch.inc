// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e9, the schematic face: textually included by eeschema_embind.cpp after
// cc_rules_sch.inc, outside any KICAD_MERGED_EMBIND guard (include subdir eeschema; the
// merged image compiles that TU with -DKICAD_MERGED_EMBIND). It sees the TU's includes and
// helpers (schFrame(), the cc_output namespace) and calls them without editing them.
//
// Every definition of the lane lives in namespace cc_e9 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

#include <reporter.h>
#include <wildcards_and_files_ext.h>
#include <libraries/library_table.h>
#include <libraries/library_manager.h>

// K-REMAP's switch, defined in kicad eeschema/files-io.cpp under __EMSCRIPTEN__ (the same image):
// between Begin and End, SCH_EDIT_FRAME::OpenProjectFiles and SaveProject open and save a
// KiCad 5 project with no window, every line on aReporter.
void CcLegacyHostBegin( REPORTER* aReporter );
void CcLegacyHostEnd();

namespace cc_e9
{
namespace sch
{
/** An absolute path with no NUL, no "..", at most 4,096 bytes, ending in aExt. */
static bool isProjectPath( const std::string& aPath, const std::string& aExt )
{
    return !aPath.empty() && aPath.size() <= 4096 && aPath[0] == '/'
           && aPath.find( '\0' ) == std::string::npos && aPath.find( ".." ) == std::string::npos
           && aPath.size() > aExt.size()
           && aPath.compare( aPath.size() - aExt.size(), aExt.size(), aExt ) == 0;
}


/** The files the save left in the project folder, every schematic, the project and tables. */
static json savedFiles( SCH_EDIT_FRAME* aFrame )
{
    json files = json::array();
    std::set<std::string> seen;

    auto add = [&]( const wxString& aPath )
    {
        std::string path = toUtf8( aPath );

        if( !path.empty() && wxFileName::FileExists( aPath ) && seen.insert( path ).second )
            files.push_back( path );
    };

    SCH_SCREENS screens( aFrame->Schematic().Root() );

    for( SCH_SCREEN* screen = screens.GetFirst(); screen; screen = screens.GetNext() )
        add( screen->GetFileName() );

    add( aFrame->Prj().GetProjectFullName() );
    add( wxFileName( aFrame->Prj().GetProjectPath(),
                     FILEEXT::SymbolLibraryTableFileName ).GetFullPath() );

    std::optional<LIBRARY_TABLE*> table = Pgm().GetLibraryManager().Table(
            LIBRARY_TABLE_TYPE::SYMBOL, LIBRARY_TABLE_SCOPE::PROJECT );

    if( table && *table )
    {
        for( const LIBRARY_TABLE_ROW& row : ( *table )->Rows() )
            add( ExpandEnvVarSubstitutions( row.URI(), &aFrame->Prj() ) );
    }

    return files;
}
} // namespace sch


// project.import format "kicad5" (cap importLegacy): {"path": "<abs>/<stem>.pro"}, or with
// {"run": int, "dir": "/tmp/cc-legacy/<n>"} beside it. The frame opens <stem>.sch (the
// project's root schematic, KiCad's own naming) under K-REMAP: DIALOG_SYMBOL_REMAP's body
// runs with no window and no backup, the rescuer takes every candidate with no
// DIALOG_RESCUE_EACH, the legacy rescue and cache libraries become .kicad_sym with their
// table rows; then KiCad's Save writes every sheet as KiCad 10 (.kicad_sch), the .kicad_pro
// and the tables. Synchronous, as kicadImportSchematic (the open's own busy guard); no
// commit (the undo list starts empty, as after any open). With a run folder, the record
// <dir>/.cc-legacy.json {"opened", "saved", "files": [path], "lines": [{severity, text}]} is
// written last. True only when the project opened and every sheet saved.
bool importLegacyProject( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || ( args.size() != 1 && args.size() != 3 ) )
        return false;

    auto path = args.find( "path" );

    if( path == args.end() || !path->is_string() )
        return false;

    std::string d;

    if( args.size() == 3 )
    {
        auto run = args.find( "run" );
        auto dir = args.find( "dir" );

        if( run == args.end() || !run->is_number_integer() || run->get<long long>() < 0
                || dir == args.end() || !dir->is_string() )
        {
            return false;
        }

        d = dir->get<std::string>();

        if( !isRunDir( d, "/tmp/cc-legacy/" ) )
            return false;
    }

    const std::string pro = path->get<std::string>();

    if( !sch::isProjectPath( pro, ".pro" ) )
        return false;

    wxFileName proFn( wxString::FromUTF8( pro.c_str() ) );
    wxFileName schFn( proFn );
    schFn.SetExt( FILEEXT::LegacySchematicFileExtension );

    if( !proFn.FileExists() || !schFn.FileExists() )
        return false;

    pcbjam_open::BusyGuard busy;

    if( wxWindow* blocking = fr->Kiway().GetBlockingDialog() )
        blocking->Close( true );

    cc_output::LINES_REPORTER lines;
    bool                      opened = false;
    bool                      saved = false;

    CcLegacyHostBegin( &lines );

    try
    {
        opened = fr->OpenProjectFiles( std::vector<wxString>( 1, schFn.GetFullPath() ) );

        if( opened )
            saved = fr->SaveProject( false );
    }
    catch( const std::exception& e )
    {
        lines.Report( wxString::FromUTF8( e.what() ), RPT_SEVERITY_ERROR );
    }
    catch( ... )
    {
        lines.Report( wxS( "The project could not be opened." ), RPT_SEVERITY_ERROR );
    }

    CcLegacyHostEnd();

    if( !d.empty() )
    {
        cc_output::EnsureDir( d );

        json record = json::object();
        record["opened"] = opened;
        record["saved"] = saved;
        record["files"] = opened ? sch::savedFiles( fr ) : json::array();
        record["lines"] = cc_output::LinesJson( lines );
        cc_output::WriteRecord( d, ".cc-legacy.json", record );
    }

    return opened && saved;
}


// Marker (cap importLegacy): this engine opens a KiCad 5 project with no window.
bool hostLegacy()
{
    return true;
}
} // namespace cc_e9

EMSCRIPTEN_BINDINGS( cc_e9_sch )
{
    function( "kicadImportLegacyProject", &cc_e9::importLegacyProject );
    function( "kicadHostLegacy", &cc_e9::hostLegacy );
}
