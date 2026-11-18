// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e13, the schematic face: textually included by eeschema_embind.cpp after
// cc_rules_sch.inc, outside any KICAD_MERGED_EMBIND guard (include subdir eeschema; the
// merged image compiles that TU with -DKICAD_MERGED_EMBIND). It sees the TU's includes and
// helpers (schFrame(), the cc_output namespace) and calls them without editing them.
//
// Every definition of the lane lives in namespace cc_e13 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

#include <string_utils.h>
#include <netlist_exporters/netlist_exporter_spice.h>

namespace cc_e13
{
namespace sch
{
/**
 * The circuit model's net names as SIMULATOR_FRAME_UI::rebuildSignalsList collects them for
 * quoting (kicad eeschema/sim/simulator_frame_ui.cpp 1031 to 1070): unescaped, in SPICE
 * markup, ground and unconnected nets left out.
 */
std::vector<wxString> userSignalNetNames( const SPICE_CIRCUIT_MODEL& aCircuit )
{
    wxString unconnected = wxString( wxS( "unconnected-(" ) );
    NETLIST_EXPORTER_SPICE::ConvertToSpiceMarkup( &unconnected );
    unconnected.Replace( '(', '_' );    // as KiCad's frame does

    std::vector<wxString> names;

    for( const wxString& net : aCircuit.GetNets() )
    {
        wxString netname = UnescapeString( net );
        NETLIST_EXPORTER_SPICE::ConvertToSpiceMarkup( &netname );

        if( netname == "GND" || netname == "0" || netname.StartsWith( unconnected ) )
            continue;

        names.emplace_back( netname );
    }

    return names;
}

/**
 * KiCad's quoting of net names inside a user defined signal, copied from the quoteNetNames
 * lambda of SIMULATOR_FRAME_UI::applyUserDefinedSignals (kicad eeschema/sim/
 * simulator_frame_ui.cpp 2225 to 2324, GPL-3.0-or-later like this file): every occurrence of a
 * net name, extended over the net name characters that follow it, is put in double quotes.
 */
wxString quoteNetNames( const wxString& aExpression, const std::vector<wxString>& aNetnames )
{
    std::vector<bool> mask( aExpression.length(), false );

    auto isNetnameChar =
            []( wxUniChar aChar ) -> bool
            {
                wxUint32 value = aChar.GetValue();

                if( ( value >= '0' && value <= '9' ) || ( value >= 'A' && value <= 'Z' )
                    || ( value >= 'a' && value <= 'z' ) )
                {
                    return true;
                }

                switch( value )
                {
                case '_':
                case '/':
                case '+':
                case '-':
                case '~':
                case '.':
                    return true;
                default:
                    break;
                }

                return false;
            };

    for( const wxString& netname : aNetnames )
    {
        size_t pos = aExpression.find( netname );

        while( pos != wxString::npos )
        {
            for( size_t i = 0; i < netname.length(); ++i )
                mask[pos + i] = true; // Mark the positions of the netname

            pos = aExpression.find( netname, pos + 1 ); // Find the next occurrence
        }
    }

    for( size_t i = 0; i < aExpression.length(); ++i )
    {
        if( !mask[i] || ( i > 0 && mask[i - 1] ) )
            continue;

        size_t j = i + 1;

        while( j < aExpression.length() )
        {
            if( mask[j] )
            {
                ++j;
                continue;
            }

            if( isNetnameChar( aExpression[j] ) )
            {
                mask[j] = true;
                ++j;
            }
            else
            {
                break;
            }
        }
    }

    wxString quotedNetnames = "";
    bool     startQuote = true;

    // put quotes around all the positions that were found above
    for( size_t i = 0; i < aExpression.length(); i++ )
    {
        if( mask[i] && startQuote )
        {
            quotedNetnames = quotedNetnames + "\"";
            startQuote = false;
        }
        else if( !mask[i] && !startQuote )
        {
            quotedNetnames = quotedNetnames + "\"";
            startQuote = true;
        }

        wxString ch = aExpression[i];
        quotedNetnames = quotedNetnames + ch;
    }

    if( !startQuote )
        quotedNetnames = quotedNetnames + "\"";

    return quotedNetnames;
}

/** One signal of a kicadSimUserSignals call. */
struct USER_SIGNAL
{
    int         id = 0;
    std::string expr;
};

/** The first console line holding "error" among those after aFrom, or "". */
std::string firstErrorLine( size_t aFrom )
{
    const std::deque<std::string>& console = simReporter().m_console;

    for( size_t i = aFrom; i < console.size(); ++i )
    {
        if( wxString::FromUTF8( console[i].c_str() ).Lower().Contains( wxS( "error" ) ) )
            return console[i];
    }

    return "";
}

/** The queued body: setplot, then unlet and let for each signal, then the record last. */
void applyUserSignals( long long aRun, const std::string& aDir, const std::string& aPlot,
                       const std::vector<USER_SIGNAL>& aSignals )
{
    ensureRunDir( aDir );

    json record = json::object();
    record["run"] = aRun;
    record["plot"] = aPlot;
    record["vectors"] = json::array();
    record["errors"] = json::array();

    SCH_EDIT_FRAME*                       fr = schFrame();
    std::shared_ptr<SPICE_CIRCUIT_MODEL>  circuit = simCircuit();
    std::shared_ptr<SPICE_SIMULATOR>      engine = fr ? simEngine( fr ) : nullptr;

    // Re-checked on the coroutine: no run began since, the plot is still the run's and lives.
    auto runPlots = simRunPlots().find( aRun );
    std::vector<std::string> plots;

    if( engine && circuit && !simLive() && runPlots != simRunPlots().end() )
        plots = simAllPlots();

    if( !engine || !circuit || simLive() || runPlots == simRunPlots().end()
            || std::find( runPlots->second.begin(), runPlots->second.end(), aPlot )
                       == runPlots->second.end()
            || std::find( plots.begin(), plots.end(), aPlot ) == plots.end() )
    {
        record["error"] = "no_simulation";
        writeRunDone( aDir, "user-signals.json", record );
        return;
    }

    record["error"] = "";

    try
    {
        // A run's plots are complex for these analyses (finishSimRun's rule); a vector is
        // also read as complex when ngspice holds an imaginary part for it.
        const std::vector<wxString> netnames = userSignalNetNames( *circuit );

        engine->Command( "setplot " + aPlot );

        for( const USER_SIGNAL& signal : aSignals )
        {
            const std::string name = "user" + std::to_string( signal.id );
            const size_t      mark = simReporter().m_console.size();

            // An earlier call's vector of the same id goes first, so a refused expression
            // never leaves the old vector standing under its name.
            engine->Command( "unlet " + name );

            const wxString quoted = quoteNetNames( wxString::FromUTF8( signal.expr.c_str() ),
                                                   netnames );
            const bool     taken = engine->Command( "let " + name + " = " + toUtf8( quoted ) );

            const std::vector<std::string> vecs = simAllVecs( aPlot );
            const bool made = std::find( vecs.begin(), vecs.end(), name ) != vecs.end();
            std::string line = firstErrorLine( mark );

            if( !made || !line.empty() )
            {
                if( line.empty() )
                {
                    line = taken ? "ngspice made no vector for this expression"
                                 : "ngspice refused the expression";
                }

                json error = json::object();
                error["id"] = signal.id;
                error["line"] = line;
                record["errors"].push_back( std::move( error ) );
                continue;
            }

            json vector = json::object();
            vector["id"] = signal.id;
            vector["name"] = name;
            vector["length"] = static_cast<long long>( engine->GetRealVector( name ).size() );
            vector["complex"] = !engine->GetImaginaryVector( name, 1 ).empty();
            record["vectors"].push_back( std::move( vector ) );
        }
    }
    catch( const std::exception& e )
    {
        record["error"] = "sim_failed";
        record["message"] = e.what();
    }

    writeRunDone( aDir, "user-signals.json", record );
}
} // namespace sch

// sim.userSignals: {"run": int, "dir": "/tmp/cc-sim/<n>", "plot": string, "signals": [{"id":
// 0 to 15, "expr": string}] (0 to 16)}, those four keys only. The plot must be one the run
// made (its record written: no run live) and still held by the service; the net names are
// those of the circuit model the last run kept (the island asks for the newest run of a kind).
// Queued: setplot <plot>, then for each signal "let user<id> = <expr>" with the circuit's net
// names quoted by KiCad's own rule (SIMULATOR_FRAME_UI::applyUserDefinedSignals), the record
// <dir>/user-signals.json {run, plot, error: "" | "no_simulation" | "sim_failed", vectors:
// [{id, name: "user<id>", length, complex}], errors: [{id, line}]} (line ngspice's first error
// line for that signal) last. False (nothing queued) for any other args, no schematic frame,
// an open in flight or a run live.
bool kicadSimUserSignals( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() || simLive() || !simCircuit() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 4 )
        return false;

    auto run = args.find( "run" );
    auto dir = args.find( "dir" );
    auto plot = args.find( "plot" );
    auto signals = args.find( "signals" );

    if( run == args.end() || !run->is_number_integer() || run->get<long long>() < 0
            || dir == args.end() || !dir->is_string() || plot == args.end() || !plot->is_string()
            || signals == args.end() || !signals->is_array() || signals->size() > 16 )
    {
        return false;
    }

    const long long   n = run->get<long long>();
    const std::string d = dir->get<std::string>();
    const std::string p = plot->get<std::string>();

    if( !isRunDir( d, "/tmp/cc-sim/" ) || p.empty() || p.size() > 64 )
        return false;

    for( char c : p )
    {
        if( !( ( c >= '0' && c <= '9' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= 'a' && c <= 'z' )
               || c == '_' ) )
        {
            return false;
        }
    }

    // The run's plots as its finish recorded them (no RPC here; the queued body re-checks
    // that the plot still lives in the service).
    auto runPlots = simRunPlots().find( n );

    if( runPlots == simRunPlots().end()
            || std::find( runPlots->second.begin(), runPlots->second.end(), p )
                       == runPlots->second.end() )
    {
        return false;
    }

    std::vector<sch::USER_SIGNAL> read;
    std::set<int>                 ids;

    for( const json& entry : *signals )
    {
        if( !entry.is_object() || entry.size() != 2 || !entry.contains( "id" )
                || !entry["id"].is_number_integer() || !entry.contains( "expr" )
                || !entry["expr"].is_string() )
        {
            return false;
        }

        const long long   id = entry["id"].get<long long>();
        const std::string expr = entry["expr"].get<std::string>();

        // One expression, nothing that could become a second command or a string.
        if( id < 0 || id > 15 || !ids.insert( static_cast<int>( id ) ).second || expr.empty()
                || expr.size() > 256 || !isPlainCommandText( expr )
                || expr.find_first_of( "{}" ) != std::string::npos )
        {
            return false;
        }

        read.push_back( { static_cast<int>( id ), expr } );
    }

    pcbjam_collab::runOnCoroutine( fr, [n, d, p, read]() {
        sch::applyUserSignals( n, d, p, read );
    } );

    return true;
}
} // namespace cc_e13

EMSCRIPTEN_BINDINGS( cc_e13_sch )
{
    function( "kicadSimUserSignals", &cc_e13::kicadSimUserSignals );
}
