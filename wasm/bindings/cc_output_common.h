/*
 * Circuit Center (engine run 14): the parts every output export shares, for both faces.
 *
 * Written by Chirichella Inc. on 2026-11-16 for the island's project.export (cap export):
 *   - the lines reporter (every line a job reports, with its severity, in order) and the
 *     severity words the records carry, moved here from pcbnew_embind.cpp's plot so the
 *     plot (kicadPlotBoard), the board outputs (kicadExportBoardOutput) and the schematic
 *     outputs (kicadExportSchematicOutput) answer one record shape;
 *   - the record writer (a part file renamed into place, so the host never reads half a
 *     record) and the run folder rule (under a root, longer than it, no "..", no trailing
 *     slash, no NUL), the plot's own;
 *   - the jobs this engine runs, by face, KiCad's registry names and ours (cc_*), with our
 *     closed settings;
 *   - the settings merge: KiCad's own job JSON (a jobset's settings), checked key by key
 *     against a fresh instance's ToJson, unknown keys dropped and named, a known key of
 *     another JSON type refusing the call, then the fixes the engine makes whatever was sent.
 *
 * Included by pcbnew_embind.cpp, eeschema_embind.cpp and kicad_editor_embind.cpp, whose
 * include roots differ: this header reaches only common headers (common/jobs, reporter,
 * nlohmann, wx). Everything is inline in namespace cc_output, so the three translation
 * units share one definition.
 */

#pragma once

#ifdef __EMSCRIPTEN__

#include <jobs/job.h>
#include <jobs/job_registry.h>
#include <reporter.h>
#include <nlohmann/json.hpp>
#include <wx/filename.h>
#include <wx/string.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cc_output
{

using json = nlohmann::json;

/** Every line a job reports, with its severity, in order (WX_STRING_REPORTER keeps the text alone). */
class LINES_REPORTER : public REPORTER
{
public:
    using REPORTER::Report;

    REPORTER& Report( const wxString& aText, SEVERITY aSeverity = RPT_SEVERITY_UNDEFINED ) override
    {
        REPORTER::Report( aText, aSeverity );
        wxString text = aText;
        text.Trim( true ).Trim( false );

        if( !text.IsEmpty() )
            m_lines.emplace_back( aSeverity, text );

        return *this;
    }

    std::vector<std::pair<SEVERITY, wxString>> m_lines;
};

inline const char* SeverityWord( SEVERITY aSeverity )
{
    switch( aSeverity )
    {
    case RPT_SEVERITY_ERROR:   return "error";
    case RPT_SEVERITY_WARNING: return "warning";
    case RPT_SEVERITY_ACTION:  return "action";
    default:                   return "info";
    }
}

/** The reporter's lines as the records carry them: [{severity, text}]. */
inline json LinesJson( const LINES_REPORTER& aReporter )
{
    json lines = json::array();

    for( const auto& [severity, text] : aReporter.m_lines )
    {
        json line = json::object();
        line["severity"] = SeverityWord( severity );
        line["text"] = std::string( text.ToUTF8() );
        lines.push_back( std::move( line ) );
    }

    return lines;
}

/** <dir>/<name>, written LAST and whole: a part file renamed into place. */
inline void WriteRecord( const std::string& aDir, const std::string& aName, const json& aBody )
{
    const std::string body = aBody.dump( -1, ' ', false, json::error_handler_t::replace );
    const std::string fin = aDir + "/" + aName;
    const std::string part = fin + ".part";

    if( FILE* f = std::fopen( part.c_str(), "wb" ) )
    {
        std::fwrite( body.data(), 1, body.size(), f );
        std::fclose( f );
        std::rename( part.c_str(), fin.c_str() );
    }
}

/** A run folder: under aRoot, longer than it, no "..", no trailing slash, no NUL (the plot's rule). */
inline bool IsRunDir( const std::string& aDir, const std::string& aRoot )
{
    return aDir.size() > aRoot.size() && aDir.compare( 0, aRoot.size(), aRoot ) == 0
           && aDir.find( ".." ) == std::string::npos && aDir.back() != '/'
           && aDir.find( '\0' ) == std::string::npos;
}

inline void EnsureDir( const std::string& aDir )
{
    const wxString dir = wxString::FromUTF8( aDir.c_str() );

    if( !wxFileName::DirExists( dir ) )
        wxFileName::Mkdir( dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL );
}

/** Where every output run writes: /tmp/cc-out/<n>. */
inline const std::string& OutputRoot()
{
    static const std::string root = "/tmp/cc-out/";
    return root;
}

/** The record name every output run writes last. */
inline const std::string& RecordName()
{
    static const std::string name = ".cc-export.json";
    return name;
}

// ───────────────────────────── the jobs this engine runs ─────────────────────────────

/** Board jobs (the pcb frame): KiCad's registry names, then ours. */
inline const std::vector<std::string>& BoardJobs()
{
    static const std::vector<std::string> jobs = {
        "pcb_export_gerbers", "pcb_export_drill", "pcb_export_pos", "pcb_export_ipc2581",
        "pcb_export_odb", "pcb_export_ipcd356", "pcb_export_pdf", "pcb_export_svg",
        "pcb_export_dxf", "pcb_export_ps", "pcb_export_gencad", "pcb_export_stats",
        "pcb_export_3d",
        "cc_footprint_report", "cc_idf", "cc_specctra_dsn", "cc_hyperlynx",
        "cc_footprint_associations", "cc_footprints"
    };

    return jobs;
}

/** Schematic jobs (the sch frame): KiCad's registry names, then ours. */
inline const std::vector<std::string>& SchematicJobs()
{
    static const std::vector<std::string> jobs = {
        "sch_export_plot_pdf", "sch_export_plot_svg", "sch_export_plot_dxf",
        "sch_export_plot_ps", "sch_export_netlist", "sch_export_bom",
        "cc_symbols"
    };

    return jobs;
}

inline bool Lists( const std::vector<std::string>& aJobs, const std::string& aName )
{
    return std::find( aJobs.begin(), aJobs.end(), aName ) != aJobs.end();
}

/** Ours: no KiCad job, a runner in the export calling the exporter beneath KiCad's dialog. */
inline bool IsOurs( const std::string& aName )
{
    return aName.rfind( "cc_", 0 ) == 0;
}

/** The schematic plot jobs, the only ones that take "pages". */
inline bool IsSchematicPlot( const std::string& aName )
{
    return aName == "sch_export_plot_pdf" || aName == "sch_export_plot_svg"
           || aName == "sch_export_plot_dxf" || aName == "sch_export_plot_ps";
}

/** Our closed settings, read and checked. */
struct OURS_SETTINGS
{
    std::string units;                       // cc_footprint_report: mm | in; cc_idf: mm | thou
    bool        includeUnspecified = false;  // cc_idf
    bool        includeDnp = false;          // cc_idf
};

/** Our jobs' keys with their defaults; {} for a job that takes none. */
inline json OursDefaults( const std::string& aName )
{
    json d = json::object();

    if( aName == "cc_footprint_report" )
    {
        d["units"] = "mm";
    }
    else if( aName == "cc_idf" )
    {
        d["units"] = "mm";
        d["include_unspecified"] = false;
        d["include_dnp"] = false;
    }

    return d;
}

/** Our closed settings: any other key, a value of another type or out of its set refuses. */
inline bool ReadOurs( const std::string& aName, const json& aSent, OURS_SETTINGS& aOut )
{
    if( !aSent.is_object() )
        return false;

    const json defaults = OursDefaults( aName );

    for( auto it = aSent.begin(); it != aSent.end(); ++it )
    {
        if( !defaults.contains( it.key() ) )
            return false;
    }

    if( aName == "cc_footprint_report" || aName == "cc_idf" )
    {
        aOut.units = defaults["units"].get<std::string>();
        auto units = aSent.find( "units" );

        if( units != aSent.end() )
        {
            if( !units->is_string() )
                return false;

            const std::string u = units->get<std::string>();

            if( aName == "cc_footprint_report" ? ( u != "mm" && u != "in" )
                                               : ( u != "mm" && u != "thou" ) )
            {
                return false;
            }

            aOut.units = u;
        }
    }

    auto flag = [&]( const char* aKey, bool& aValue ) -> bool
                {
                    auto it = aSent.find( aKey );

                    if( it == aSent.end() )
                        return true;

                    if( !it->is_boolean() )
                        return false;

                    aValue = it->get<bool>();
                    return true;
                };

    if( aName == "cc_idf" )
    {
        if( !flag( "include_unspecified", aOut.includeUnspecified )
                || !flag( "include_dnp", aOut.includeDnp ) )
        {
            return false;
        }
    }

    return true;
}

// ───────────────────────────── the settings merge ─────────────────────────────

/** A JSON value's kind for the merge's type check: every number is one kind. */
inline int JsonKind( const json& aValue )
{
    if( aValue.is_boolean() )
        return 1;

    if( aValue.is_number() )
        return 2;

    if( aValue.is_string() )
        return 3;

    if( aValue.is_array() )
        return 4;

    if( aValue.is_object() )
        return 5;

    return 0;   // null
}

struct MERGE_RESULT
{
    std::vector<std::string> dropped;    // sent keys the job does not have, in key order
    std::vector<std::string> warnings;   // lines the run reports as warnings
    std::string              refusal;    // not empty: the run makes nothing, code -1, this line
};

/**
 * KiCad's own job settings onto aJob (a fresh instance), as KiCad's jobset runner would read
 * them, then the fixes the engine makes whatever was sent:
 *  1. the job's own keys and defaults from its ToJson;
 *  2. each sent key the job does not have left out and named in aOut.dropped; a known key
 *     whose value is of another JSON type (boolean, number, string, array, object) refuses
 *     the whole call, so KiCad's FromJson never throws on it; an array may hold only strings
 *     and numbers;
 *  3. the defaults updated with what was sent;
 *  4. the fixes: the output path, output_filename, output_dir and description ignored;
 *     color_theme KiCad's default theme (the editor's own is the viewer's night palette);
 *     check_zones off (pours plot as last filled: a refill is an edit); drawing_sheet the
 *     project's own; variants empty; use_global_settings off; per job the fixes of the
 *     outputs spec's section 4 and 6.2 (CC_OUTPUT_FIXES below);
 *  5. KiCad's FromJson inside a try (a throw refuses too).
 * False: the call is refused and nothing may be queued.
 */
inline bool MergeSettings( JOB& aJob, const std::string& aName, const json& aSent,
                           MERGE_RESULT& aOut )
{
    if( !aSent.is_object() )
        return false;

    json defaults = json::object();

    try
    {
        aJob.ToJson( defaults );
    }
    catch( ... )
    {
        return false;
    }

    json merged = defaults;

    for( auto it = aSent.begin(); it != aSent.end(); ++it )
    {
        auto known = defaults.find( it.key() );

        if( known == defaults.end() )
        {
            aOut.dropped.push_back( it.key() );
            continue;
        }

        const json& value = it.value();

        // A KiCad enum serializes its unset value as null (pcb_export_3d's format on a fresh
        // job): such a key takes a string, the enum's name, which FromJson maps.
        if( known->is_null() ? !( value.is_string() || value.is_null() )
                             : JsonKind( value ) != JsonKind( *known ) )
        {
            return false;
        }

        if( value.is_array() )
        {
            for( const json& element : value )
            {
                if( !element.is_string() && !element.is_number() )
                    return false;
            }
        }

        merged[it.key()] = value;
    }

    // ── the fixes (each only where the job has the key) ──
    auto reset = [&]( const char* aKey )
                 {
                     auto it = defaults.find( aKey );

                     if( it != defaults.end() )
                         merged[aKey] = *it;
                 };

    auto force = [&]( const char* aKey, const json& aValue )
                 {
                     if( defaults.contains( aKey ) )
                         merged[aKey] = aValue;
                 };

    reset( "output_filename" );
    reset( "output_dir" );
    reset( "description" );
    force( "color_theme", "_builtin_default" );
    force( "check_zones", false );
    force( "drawing_sheet", "" );
    force( "variant", "" );
    force( "variant_name", "" );
    force( "variant_names", json::array() );
    force( "use_global_settings", false );

    // CC_OUTPUT_FIXES: the per job fixes (the outputs spec, section 4's Fixed column and 6.2).
    if( aName == "pcb_export_3d" )
    {
        auto format = merged.find( "format" );

        if( format == merged.end() || !format->is_string()
                || format->get<std::string>() != "vrml" )
        {
            aOut.refusal = "Only VRML is made here.";
        }

        force( "overwrite", true );
        force( "vrml_model_dir", "" );
        force( "vrml_relative_paths", false );
        force( "subst_models", false );
    }
    else if( aName == "pcb_export_odb" )
    {
        auto compression = merged.find( "compression" );

        if( compression != merged.end()
                && !( compression->is_string() && compression->get<std::string>() == "zip" ) )
        {
            aOut.warnings.push_back( "ODB++ is made as a zip here." );
        }

        force( "compression", "zip" );
    }
    else if( aName == "pcb_export_drill" )
    {
        reset( "report_filename" );
        force( "format", "excellon" );
        force( "units", "mm" );
        force( "zero_format", "decimal" );
        force( "drill_origin", "abs" );
        force( "excellon.combine_pth_npth", false );
        force( "generate_map", true );
    }
    else if( aName == "pcb_export_ipc2581" )
    {
        force( "precision", 6 );
        force( "field_bom_map.dist_pn", "" );
        force( "field_bom_map.dist", "" );
        force( "bom_rev", "" );
    }
    else if( aName == "pcb_export_pdf" )
    {
        force( "scale", 1.0 );
    }
    else if( aName == "pcb_export_svg" )
    {
        reset( "precision" );
    }
    else if( aName == "pcb_export_ps" )
    {
        force( "track_width_correction", 0.0 );
        force( "x_scale_factor", 1.0 );
        force( "y_scale_factor", 1.0 );
    }
    else if( aName == "sch_export_bom" )
    {
        reset( "field_delimiter" );
        reset( "string_delimiter" );
        reset( "ref_delimiter" );
        reset( "ref_range_delimiter" );
        reset( "keep_tabs" );
        reset( "keep_line_breaks" );
    }
    else if( aName == "sch_export_netlist" )
    {
        reset( "spice.save_all_voltages" );
        reset( "spice.save_all_currents" );
        reset( "spice.save_all_events" );
        reset( "spice.save_all_dissipations" );
    }
    else if( IsSchematicPlot( aName ) )
    {
        reset( "format" );   // the job's own format: sch_export_plot_svg plots SVG

        if( aName == "sch_export_plot_pdf" )
        {
            force( "pdf_hierarchical_links", true );
            force( "pdf_property_popups", true );
            force( "pdf_metadata", true );
        }
    }

    try
    {
        aJob.FromJson( merged );
    }
    catch( ... )
    {
        return false;
    }

    return true;
}

/** Every job this engine runs, with its own keys and defaults: {"board": {...}, "schematic": {...}}. */
inline json JobsWithDefaults()
{
    json out = json::object();

    auto face = [&]( const std::vector<std::string>& aJobs ) -> json
                {
                    json jobs = json::object();

                    for( const std::string& name : aJobs )
                    {
                        if( IsOurs( name ) )
                        {
                            jobs[name] = OursDefaults( name );
                            continue;
                        }

                        std::unique_ptr<JOB> job(
                                JOB_REGISTRY::CreateInstance<JOB>( wxString::FromUTF8( name.c_str() ) ) );

                        // A name the registry does not hold is left out, so a check against the
                        // island's own list names it.
                        if( !job )
                            continue;

                        json settings = json::object();

                        try
                        {
                            job->ToJson( settings );
                        }
                        catch( ... )
                        {
                            continue;
                        }

                        jobs[name] = std::move( settings );
                    }

                    return jobs;
                };

    out["board"] = face( BoardJobs() );
    out["schematic"] = face( SchematicJobs() );
    return out;
}

} // namespace cc_output

#endif // __EMSCRIPTEN__
