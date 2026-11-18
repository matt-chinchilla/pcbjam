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
#include <project_rescue.h>
#include <symbol_viewer_frame.h>
#include <connection_graph.h>
#include <sch_connection.h>

// K-REMAP's switch, defined in kicad eeschema/files-io.cpp under __EMSCRIPTEN__ (the same image):
// between Begin and End, SCH_EDIT_FRAME::OpenProjectFiles and SaveProject open and save a
// KiCad 5 project with no window, every line on aReporter.
void CcLegacyHostBegin( REPORTER* aReporter );
void CcLegacyHostEnd();

// Defined in kicad eeschema/project_rescue.cpp under __EMSCRIPTEN__: the rescue's message boxes
// as lines on a reporter, and the design's own copy of a symbol as the rescuer's cache.
void CcSetRescueHeadless( REPORTER* aReporter );
void CcSetRescueFromDesign( bool aFromDesign );

// K-MIGRATE, defined in kicad eeschema/dialogs/dialog_migrate_buses.cpp under __EMSCRIPTEN__:
// DIALOG_MIGRATE_BUSES' proposal rule as a free function.
std::vector<wxString> ccProposedBusLabels( CONNECTION_GRAPH* aGraph,
                                           const std::vector<wxString>& aLabelList );

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


/**
 * KiCad's symbol library table rescuer (Tools > Rescue Symbols on a KiCad 6 or later design),
 * its candidates and choice reachable from the bindings. AddSymbol keeps the rescued symbol's
 * LIB_ID as KiCad's own PerformAction names it; newIdOf runs PerformAction with no design
 * symbol in reach, so the read learns a candidate's rescued name with nothing relinked.
 */
class CC_TABLE_RESCUER : public SYMBOL_LIB_TABLE_RESCUER
{
public:
    using SYMBOL_LIB_TABLE_RESCUER::SYMBOL_LIB_TABLE_RESCUER;

    boost::ptr_vector<RESCUE_CANDIDATE>& All() { return m_all_candidates; }
    std::vector<RESCUE_CANDIDATE*>&      Chosen() { return m_chosen_candidates; }
    const std::vector<SCH_SYMBOL*>&      Symbols() const { return m_symbols; }

    void AddSymbol( LIB_SYMBOL* aNewSymbol ) override
    {
        if( aNewSymbol )
            m_lastId = aNewSymbol->GetLibId().Format();

        SYMBOL_LIB_TABLE_RESCUER::AddSymbol( aNewSymbol );
    }

    wxString NewIdOf( RESCUE_CANDIDATE& aCandidate )
    {
        std::vector<SCH_SYMBOL*> held;
        held.swap( m_symbols );
        m_lastId.clear();
        aCandidate.PerformAction( this );
        m_symbols.swap( held );
        return m_lastId;
    }

    int UsesOf( const RESCUE_CANDIDATE& aCandidate ) const
    {
        int uses = 0;

        for( SCH_SYMBOL* symbol : m_symbols )
        {
            if( symbol->GetLibId().Format().wx_str() == aCandidate.GetRequestedName() )
                uses++;
        }

        return uses;
    }

    wxString NameOf( const RESCUE_CANDIDATE& aCandidate ) const
    {
        for( SCH_SYMBOL* symbol : m_symbols )
        {
            if( symbol->GetLibId().Format().wx_str() == aCandidate.GetRequestedName() )
                return UnescapeString( symbol->GetLibId().GetLibItemName().wx_str() );
        }

        return aCandidate.GetRequestedName();
    }

private:
    wxString m_lastId;
};


/** The rescuer of the open design with its candidates found (the design's copies as cache). */
static std::unique_ptr<CC_TABLE_RESCUER> findRescues( SCH_EDIT_FRAME* aFrame,
                                                      REPORTER* aReporter )
{
    auto rescuer = std::make_unique<CC_TABLE_RESCUER>( aFrame->Prj(), &aFrame->Schematic(),
                                                       &aFrame->GetCurrentSheet(),
                                                       aFrame->GetCanvas()->GetBackend() );

    CcSetRescueHeadless( aReporter );
    CcSetRescueFromDesign( true );

    try
    {
        rescuer->FindCandidates();
        rescuer->RemoveDuplicates();
    }
    catch( ... )
    {
        CcSetRescueFromDesign( false );
        CcSetRescueHeadless( nullptr );
        throw;
    }

    CcSetRescueFromDesign( false );
    CcSetRescueHeadless( nullptr );
    return rescuer;
}


struct RESCUE_ARGS
{
    long long                run = 0;
    std::string              dir;
    std::vector<std::string> names;
};


/**
 * The rescue apply, on the apply coroutine: KiCad's rescuer finds its candidates again, takes
 * the named ones (a name is a candidate's libId, or its name when no other candidate shares
 * it), stages every symbol they relink on ONE SCH_COMMIT, runs KiCad's own rescue (the
 * symbols flattened into <stem>-rescue.kicad_sym, its project table row written, the links
 * updated) and pushes the commit: one undo step, the undo list kept (KiCad clears it). A name
 * matching no candidate, or a failed write, reverts and changes nothing. Writes
 * <dir>/rescue.json {rescued, symbols, files, depth, lines} then <dir>/.cc-rescue.json LAST
 * {error, message, lines}.
 */
void runRescue( const RESCUE_ARGS& aArgs )
{
    cc_output::LINES_REPORTER lines;
    std::string               error;
    std::string               message;
    json                      report;

    cc_output::EnsureDir( aArgs.dir );

    auto finish = [&]()
                  {
                      if( !report.is_null() )
                      {
                          report["lines"] = cc_output::LinesJson( lines );
                          cc_output::WriteRecord( aArgs.dir, "rescue.json", report );
                      }

                      json done = json::object();
                      done["error"] = error;
                      done["message"] = message;
                      done["lines"] = cc_output::LinesJson( lines );
                      cc_output::WriteRecord( aArgs.dir, ".cc-rescue.json", done );
                  };

    SCH_EDIT_FRAME* fr = schFrame();   // re-resolved on the coroutine (S4)

    if( !fr )
    {
        error = "island_error";
        message = "no schematic editor";
        finish();
        return;
    }

    wxLogNull                         noLog;
    std::unique_ptr<CC_TABLE_RESCUER> rescuer;

    try
    {
        rescuer = findRescues( fr, &lines );
    }
    catch( const std::exception& e )
    {
        error = "rescue_failed";
        message = e.what();
        finish();
        return;
    }

    std::map<wxString, int> nameCount;

    for( RESCUE_CANDIDATE& candidate : rescuer->All() )
        nameCount[rescuer->NameOf( candidate )]++;

    std::set<RESCUE_CANDIDATE*> chosen;

    for( const std::string& want : aArgs.names )
    {
        const wxString name = wxString::FromUTF8( want.c_str() );
        RESCUE_CANDIDATE* hit = nullptr;

        for( RESCUE_CANDIDATE& candidate : rescuer->All() )
        {
            const wxString shortName = rescuer->NameOf( candidate );

            if( candidate.GetRequestedName() == name
                    || ( shortName == name && nameCount[shortName] == 1 ) )
            {
                hit = &candidate;
                break;
            }
        }

        if( !hit )
        {
            error = "rescue_unknown";
            message = "no rescue candidate named " + want;
            finish();
            return;
        }

        chosen.insert( hit );
    }

    // Every symbol the chosen candidates relink, staged with its screen before KiCad's rescue
    // changes it, so the push holds the old LIB_ID and copy for the undo step.
    std::map<SCH_SYMBOL*, SCH_SCREEN*> screenOf;
    SCH_SCREENS                         screens( fr->Schematic().Root() );

    for( SCH_SCREEN* screen = screens.GetFirst(); screen; screen = screens.GetNext() )
    {
        for( EDA_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
            screenOf[static_cast<SCH_SYMBOL*>( item )] = screen;
    }

    SCH_COMMIT commit( fr );
    int        symbols = 0;

    rescuer->Chosen().clear();

    for( RESCUE_CANDIDATE& candidate : rescuer->All() )
    {
        if( !chosen.count( &candidate ) )
            continue;

        rescuer->Chosen().push_back( &candidate );
        lines.Report( candidate.GetActionDescription(), RPT_SEVERITY_ACTION );

        for( SCH_SYMBOL* symbol : rescuer->Symbols() )
        {
            if( symbol->GetLibId().Format().wx_str() != candidate.GetRequestedName() )
                continue;

            auto it = screenOf.find( symbol );

            if( it != screenOf.end() )
            {
                commit.Modify( symbol, it->second );
                symbols++;
            }
        }
    }

    bool written = false;

    CcSetRescueHeadless( &lines );

    try
    {
        rescuer->OpenRescueLibrary();
        written = rescuer->DoRescues() && rescuer->WriteRescueLibrary( fr );
    }
    catch( const IO_ERROR& ioe )
    {
        lines.Report( ioe.What(), RPT_SEVERITY_ERROR );
    }
    catch( const std::exception& e )
    {
        lines.Report( wxString::FromUTF8( e.what() ), RPT_SEVERITY_ERROR );
    }

    CcSetRescueHeadless( nullptr );

    if( !written )
    {
        commit.Revert();
        error = "rescue_failed";
        message = "the rescue library could not be written";
        finish();
        return;
    }

    commit.Push( _( "Rescue Symbols" ) );

    // As SCH_EDITOR_CONTROL::rescueProject ends, less ClearUndoRedoList: the push is the step.
    if( KIWAY_PLAYER* viewer = fr->Kiway().Player( FRAME_SCH_VIEWER, false ) )
        static_cast<SYMBOL_VIEWER_FRAME*>( viewer )->ReCreateLibList();

    fr->RecalculateConnections( nullptr, NO_CLEANUP );
    fr->SyncView();
    fr->GetCanvas()->Refresh();

    json files = json::array();
    wxFileName lib( fr->Schematic().GetFileName() );
    lib.SetName( lib.GetName() + wxT( "-rescue" ) );
    lib.SetExt( FILEEXT::KiCadSymbolLibFileExtension );

    for( const wxFileName& fn : { lib, wxFileName( fr->Prj().GetProjectPath(),
                                                  FILEEXT::SymbolLibraryTableFileName ) } )
    {
        if( fn.FileExists() )
            files.push_back( toUtf8( fn.GetFullPath() ) );
    }

    report = json::object();
    report["run"] = aArgs.run;
    report["rescued"] = (int) rescuer->Chosen().size();
    report["symbols"] = symbols;
    report["files"] = files;
    report["depth"] = fr->GetUndoCommandCount();
    finish();
}


struct BUS_CHOICE
{
    std::vector<std::string> labels;
    std::string              name;
};


struct BUS_ARGS
{
    long long               run = 0;
    std::string             dir;
    std::vector<BUS_CHOICE> choices;
};


/**
 * The bus naming apply, on the apply coroutine: every label of every choice resolved first
 * (a uuid that names no label refuses the whole apply, nothing changed), then each label set
 * to its choice's name in ONE SCH_COMMIT "Name buses" (KiCad's dialog sets them with no undo
 * step; this is one), and the connections recalculated. Writes <dir>/buses.json {applied,
 * labels, depth, lines} then <dir>/.cc-buses.json LAST {error, message, lines}.
 */
void runBusMigration( const BUS_ARGS& aArgs )
{
    cc_output::LINES_REPORTER lines;
    std::string               error;
    std::string               message;
    json                      report;

    cc_output::EnsureDir( aArgs.dir );

    auto finish = [&]()
                  {
                      if( !report.is_null() )
                      {
                          report["lines"] = cc_output::LinesJson( lines );
                          cc_output::WriteRecord( aArgs.dir, "buses.json", report );
                      }

                      json done = json::object();
                      done["error"] = error;
                      done["message"] = message;
                      done["lines"] = cc_output::LinesJson( lines );
                      cc_output::WriteRecord( aArgs.dir, ".cc-buses.json", done );
                  };

    SCH_EDIT_FRAME* fr = schFrame();   // re-resolved on the coroutine (S4)

    if( !fr )
    {
        error = "island_error";
        message = "no schematic editor";
        finish();
        return;
    }

    struct TARGET
    {
        SCH_LABEL_BASE* label;
        SCH_SCREEN*     screen;
        wxString        name;
    };

    std::vector<TARGET>       targets;
    std::set<SCH_LABEL_BASE*> seen;

    for( const BUS_CHOICE& choice : aArgs.choices )
    {
        for( const std::string& id : choice.labels )
        {
            SCH_SHEET_PATH path;
            SCH_ITEM*      item = fr->Schematic().ResolveItem(
                    KIID( wxString::FromUTF8( id.c_str() ) ), &path, true );
            SCH_LABEL_BASE* label = dynamic_cast<SCH_LABEL_BASE*>( item );

            if( !label || !path.LastScreen() )
            {
                error = "bus_label_unknown";
                message = "no label " + id;
                finish();
                return;
            }

            if( seen.insert( label ).second )
                targets.push_back( { label, path.LastScreen(), wxString::FromUTF8( choice.name.c_str() ) } );
        }
    }

    SCH_COMMIT commit( fr );

    for( const TARGET& target : targets )
    {
        commit.Modify( target.label, target.screen );
        target.label->SetText( target.name );
    }

    commit.Push( _( "Name buses" ) );

    fr->RecalculateConnections( nullptr, NO_CLEANUP );
    fr->GetCanvas()->Refresh();

    report = json::object();
    report["run"] = aArgs.run;
    report["applied"] = (int) aArgs.choices.size();
    report["labels"] = (int) targets.size();
    report["depth"] = fr->GetUndoCommandCount();
    finish();
}
} // namespace sch


// project.import format "kicad5" (cap importLegacy): {"path": "<abs>/<stem>.pro"}, or with
// {"run": int, "dir": "/tmp/cc-legacy/<n>"} beside it. The frame opens <stem>.sch (the
// project's root schematic, KiCad's own naming) under K-REMAP: DIALOG_SYMBOL_REMAP's body
// runs with no window and no backup, the rescuer takes every candidate with no
// DIALOG_RESCUE_EACH, the legacy rescue and cache libraries become .kicad_sym with their
// table rows; then KiCad's Save writes every sheet as KiCad 10 (.kicad_sch), the .kicad_pro
// and the tables. Run in the call, as kicadImportSchematic (the open's own busy guard), and
// registered as it is with the PARKER policy: the open parks (a library read from the
// engine's library provider waits on the page), so the call answers a Promise. No
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


// schematic.rescue (cap rescue), the read. aArgsJson exactly {}. Run in the call and
// registered with the PARKER policy (it answers a Promise): each library symbol the rescuer
// compares is read through the library provider, which parks for one not yet in memory.
// KiCad's symbol library table rescuer finds its candidates on the open design, a symbol's
// copy held in the design standing as the cache a KiCad 6 or later design no longer carries
// in a cache library (a symbol whose library is missing, or whose library copy's pins now
// differ). Answers
// {"candidates": [{"name", "libId", "uses", "rescuedAs"}]} (at most 4,096; name the symbol's
// own name, libId the LIB_ID the design names, uses how many placed symbols name it,
// rescuedAs the LIB_ID KiCad's rescue gives it in <stem>-rescue). Nothing changes. "" with no
// schematic frame, an open in flight or any other JSON.
std::string schRescue( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return "";

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || !args.empty() )
        return "";

    wxLogNull                               noLog;
    cc_output::LINES_REPORTER               lines;
    std::unique_ptr<sch::CC_TABLE_RESCUER>  rescuer;

    try
    {
        rescuer = sch::findRescues( fr, &lines );
    }
    catch( ... )
    {
        return "";
    }

    json candidates = json::array();

    for( RESCUE_CANDIDATE& candidate : rescuer->All() )
    {
        if( candidates.size() >= 4096 )
            break;

        json row = json::object();
        row["name"] = toUtf8( rescuer->NameOf( candidate ) );
        row["libId"] = toUtf8( candidate.GetRequestedName() );
        row["uses"] = rescuer->UsesOf( candidate );
        row["rescuedAs"] = toUtf8( rescuer->NewIdOf( candidate ) );
        candidates.push_back( std::move( row ) );
    }

    json out = json::object();
    out["candidates"] = candidates;
    return out.dump( -1, ' ', false, json::error_handler_t::replace );
}


// schematic.rescueApply (cap rescue). aArgsJson exactly {"run": int >= 0, "dir":
// "/tmp/cc-rescue/<n>", "names": [1 to 4,096 strings of 1 to 1,024 bytes]}. False (nothing
// queued) for any other JSON, with no schematic frame or while an open is in flight; true once
// the run is queued on the apply coroutine (the host waits for <dir>/.cc-rescue.json).
bool schRescueApply( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 3 )
        return false;

    auto run = args.find( "run" );
    auto dir = args.find( "dir" );
    auto names = args.find( "names" );

    if( run == args.end() || !run->is_number_integer() || run->get<long long>() < 0
            || dir == args.end() || !dir->is_string() || names == args.end()
            || !names->is_array() || names->empty() || names->size() > 4096 )
    {
        return false;
    }

    sch::RESCUE_ARGS a;
    a.run = run->get<long long>();
    a.dir = dir->get<std::string>();

    if( !isRunDir( a.dir, "/tmp/cc-rescue/" ) )
        return false;

    for( const json& name : *names )
    {
        if( !name.is_string() || name.get<std::string>().empty()
                || name.get<std::string>().size() > 1024 )
        {
            return false;
        }

        a.names.push_back( name.get<std::string>() );
    }

    pcbjam_collab::runOnCoroutine( fr, [a]() { sch::runRescue( a ); } );
    return true;
}


// buses.migration.get (cap busMigration), a read. Synchronous: the connection graph's buses
// that carry more than one vector label of different names (GetBusesNeedingMigration, the
// list DIALOG_MIGRATE_BUSES shows), each with KiCad's proposals (ccProposedBusLabels):
// {"buses": [{"id", "sheetPath", "labels": [{"uuid", "text"}], "proposals": [string]}]}, at
// most 1,024. Nothing changes. "" with no schematic frame or while an open is in flight.
std::string busMigrationGet()
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return "";

    CONNECTION_GRAPH* graph = fr->Schematic().ConnectionGraph();

    if( !graph )
        return "";

    json buses = json::array();
    int  id = 0;

    for( const CONNECTION_SUBGRAPH* subgraph : graph->GetBusesNeedingMigration() )
    {
        if( buses.size() >= 1024 )
            break;

        json                  labels = json::array();
        std::vector<wxString> texts;

        for( SCH_ITEM* item : subgraph->GetVectorBusLabels() )
        {
            SCH_LABEL_BASE* label = static_cast<SCH_LABEL_BASE*>( item );
            json            row = json::object();
            row["uuid"] = toUtf8( label->m_Uuid.AsString() );
            row["text"] = toUtf8( label->GetText() );
            labels.push_back( std::move( row ) );
            texts.push_back( label->GetText() );
        }

        json proposals = json::array();

        for( const wxString& proposal : ccProposedBusLabels( graph, texts ) )
            proposals.push_back( toUtf8( proposal ) );

        json bus = json::object();
        bus["id"] = id++;
        bus["sheetPath"] = toUtf8( subgraph->GetSheet().PathHumanReadable() );
        bus["labels"] = labels;
        bus["proposals"] = proposals;
        buses.push_back( std::move( bus ) );
    }

    json out = json::object();
    out["buses"] = buses;
    return out.dump( -1, ' ', false, json::error_handler_t::replace );
}


// buses.migration.apply (cap busMigration). aArgsJson exactly {"run": int >= 0, "dir":
// "/tmp/cc-buses/<n>", "choices": [1 to 1,024 of {"labels": [1 to 64 uuid strings], "name":
// 1 to 1,024 bytes, no line break}]}. False (nothing queued) for any other JSON, with no
// schematic frame or while an open is in flight; true once the run is queued on the apply
// coroutine (the host waits for <dir>/.cc-buses.json).
bool busMigrationApply( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 3 )
        return false;

    auto run = args.find( "run" );
    auto dir = args.find( "dir" );
    auto choices = args.find( "choices" );

    if( run == args.end() || !run->is_number_integer() || run->get<long long>() < 0
            || dir == args.end() || !dir->is_string() || choices == args.end()
            || !choices->is_array() || choices->empty() || choices->size() > 1024 )
    {
        return false;
    }

    sch::BUS_ARGS a;
    a.run = run->get<long long>();
    a.dir = dir->get<std::string>();

    if( !isRunDir( a.dir, "/tmp/cc-buses/" ) )
        return false;

    for( const json& choice : *choices )
    {
        if( !choice.is_object() || choice.size() != 2 )
            return false;

        auto labels = choice.find( "labels" );
        auto name = choice.find( "name" );

        if( labels == choice.end() || !labels->is_array() || labels->empty()
                || labels->size() > 64 || name == choice.end() || !name->is_string() )
        {
            return false;
        }

        sch::BUS_CHOICE c;
        c.name = name->get<std::string>();

        if( c.name.empty() || c.name.size() > 1024
                || c.name.find_first_of( "\r\n" ) != std::string::npos )
        {
            return false;
        }

        for( const json& id : *labels )
        {
            if( !id.is_string() || id.get<std::string>().empty()
                    || id.get<std::string>().size() > 64 )
            {
                return false;
            }

            c.labels.push_back( id.get<std::string>() );
        }

        a.choices.push_back( std::move( c ) );
    }

    pcbjam_collab::runOnCoroutine( fr, [a]() { sch::runBusMigration( a ); } );
    return true;
}


// Marker (cap busMigration): this engine never shows KiCad's Migrate Buses window (K-MIGRATE)
// and names buses through kicadBusMigrationApply.
bool hostBusMigration()
{
    return true;
}


// Marker (cap importLegacy): this engine opens a KiCad 5 project with no window.
bool hostLegacy()
{
    return true;
}
} // namespace cc_e9

EMSCRIPTEN_BINDINGS( cc_e9_sch )
{
    // The open and the rescuer's library reads park (as kicadOpenFile), hence the PARKER
    // policy: each of these two answers a Promise.
    function( "kicadImportLegacyProject", &cc_e9::importLegacyProject PCBJAM_PARKER_POLICY );
    function( "kicadHostLegacy", &cc_e9::hostLegacy );
    function( "kicadSchRescue", &cc_e9::schRescue PCBJAM_PARKER_POLICY );
    function( "kicadSchRescueApply", &cc_e9::schRescueApply );
    function( "kicadBusMigrationGet", &cc_e9::busMigrationGet );
    function( "kicadBusMigrationApply", &cc_e9::busMigrationApply );
    function( "kicadHostBusMigration", &cc_e9::hostBusMigration );
}
