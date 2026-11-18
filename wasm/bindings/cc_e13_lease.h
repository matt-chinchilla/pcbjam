// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15: lane e13's lease slot in eeschema_embind.cpp. Included near the top of that file, above
// the simulator section (kicadSimModelCatalog to kicadSimOpOnSheet), which lane e13 holds by lease and which sit ABOVE its cc_e13_sch.inc (that face
// is included at the end of the file, so nothing it defines is visible from a leased body).
// Here, and only here, the lane places what a leased body calls: its own #include lines (a new
// KiCad header of the lane included), declarations in namespace cc_e13, and inline or static
// helpers. Non-inline definitions go in cc_e13_sch.inc, with external linkage in namespace
// cc_e13. This header is compiled in every configuration of its file (the merged image and,
// for pcbnew_embind.cpp, occ_service): anything that only links in the merged image sits under
// KICAD_MERGED_EMBIND here as in the inc. No registration here.

#pragma once

#ifdef __EMSCRIPTEN__

#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include <sim/sim_value.h>
#include <wx/log.h>

namespace cc_e13
{
/**
 * While alive, KiCad's wxLogError and wxLogWarning lines are kept here instead of reaching the
 * GUI log, which would show them in a modal KiCad message box and hold every later simulator
 * export until it is dismissed (NETLIST_EXPORTER_SPICE::writeInclude logs "Could not find
 * library file" that way). The simulator's exports turn the kept lines into problem records.
 * Scoped: the previous target is put back on destruction (nest them, never interleave).
 */
class SIM_LOG_CAPTURE : public wxLog
{
public:
    SIM_LOG_CAPTURE() : m_previous( wxLog::SetActiveTarget( this ) ) {}

    ~SIM_LOG_CAPTURE() override
    {
        if( wxLog::GetActiveTarget() == this )
            wxLog::SetActiveTarget( m_previous );
    }

    SIM_LOG_CAPTURE( const SIM_LOG_CAPTURE& ) = delete;
    SIM_LOG_CAPTURE& operator=( const SIM_LOG_CAPTURE& ) = delete;

    /** Each kept line: true for an error (wxLOG_Error and graver), false for a warning. */
    std::vector<std::pair<bool, wxString>> m_lines;

protected:
    void DoLogRecord( wxLogLevel aLevel, const wxString& aMsg, const wxLogRecordInfo& ) override
    {
        if( aLevel <= wxLOG_Warning )
            m_lines.emplace_back( aLevel <= wxLOG_Error, aMsg );
    }

private:
    wxLog* m_previous;
};

/**
 * Whether a simulation model parameter's value is one KiCad can use where it wants a number
 * (a param of SIM_VALUE::TYPE_INT or TYPE_FLOAT): KiCad's own reading in SI notation
 * (SIM_VALUE::ToDouble, ToInt, the numbers the model dialog writes), or in SPICE notation with
 * a number first ("1Meg", "10kOhm", passed to ngspice as KiCad passes them), or a SPICE
 * expression in braces, or text holding a KiCad text variable. "" (the default) is taken.
 * Values of other types are not judged here.
 */
inline bool simNumberAccepted( const std::string& aValue, SIM_VALUE::TYPE aType )
{
    if( aType != SIM_VALUE::TYPE_INT && aType != SIM_VALUE::TYPE_FLOAT )
        return true;

    if( aValue.empty() )
        return true;

    if( aValue.size() >= 3 && aValue.front() == '{' && aValue.back() == '}' )
        return true;

    if( aValue.find( "${" ) != std::string::npos )
        return true;

    if( aType == SIM_VALUE::TYPE_FLOAT && !std::isnan( SIM_VALUE::ToDouble( aValue ) ) )
        return true;

    if( aType == SIM_VALUE::TYPE_INT && SIM_VALUE::ToInt( aValue, -1 ) != -1 )
        return true;

    if( aType == SIM_VALUE::TYPE_INT && SIM_VALUE::ToInt( aValue, 0 ) != 0 )
        return true;

    // SPICE notation: its grammar takes any trailing letters, so a digit must lead.
    size_t i = 0;

    if( aValue[i] == '+' || aValue[i] == '-' )
        ++i;

    if( i < aValue.size() && aValue[i] == '.' )
        ++i;

    if( i >= aValue.size() || aValue[i] < '0' || aValue[i] > '9' )
        return false;

    return SIM_VALUE_GRAMMAR::IsValid( aValue, aType, SIM_VALUE_GRAMMAR::NOTATION::SPICE );
}
} // namespace cc_e13

#endif // __EMSCRIPTEN__
