// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e2, the board face: textually included by pcbnew_embind.cpp after cc_rules_pcb.inc,
// outside any KICAD_MERGED_EMBIND guard (include subdir pcbnew; the merged image compiles
// that TU with -DKICAD_MERGED_EMBIND). It sees the TU's includes and helpers (pcbFrame(),
// the cc_output namespace, cc_fp_items.h's declarations in the merged image only) and calls them without editing them.
//
// Every definition of the lane lives in namespace cc_e2 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

#include <cmath>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <wx/dialog.h>
#include <wx/toplevel.h>

#include <microwave/cc_microwave_arm.h>

namespace cc_e2
{
namespace pcb
{

/** A shown dialog of any kind (KiCad's or wx's): the frame is not to be driven meanwhile. */
static bool dialogUp()
{
    for( wxWindowList::const_iterator it = wxTopLevelWindows.begin();
         it != wxTopLevelWindows.end(); ++it )
    {
        wxWindow* w = *it;

        if( w && w->IsShown() && wxDynamicCast( w, wxDialog ) )
            return true;
    }

    return false;
}

/** The board editor frame when it may be asked anything: none while an open is in flight or a dialog is up. */
static PCB_EDIT_FRAME* readyFrame()
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() || dialogUp() )
        return nullptr;

    return fr;
}

/** Only these keys, each at most once (a JSON object holds a key once). */
static bool onlyKeys( const json& aObj, const std::set<std::string>& aAllowed )
{
    for( auto it = aObj.begin(); it != aObj.end(); ++it )
    {
        if( !aAllowed.count( it.key() ) )
            return false;
    }

    return true;
}

/** A finite JSON number (an integer or a float, never a boolean). */
static bool finiteNumber( const json& aValue, double& aOut )
{
    if( !aValue.is_number() )
        return false;

    aOut = aValue.get<double>();
    return std::isfinite( aOut );
}

// Bounds of the arm: a length or size above 0 and at most 500 mm (KiCad's own clearance
// ceiling against int overflow, board_design_settings.h MAXIMUM_CLEARANCE); an arc stub's
// angle 0 to 180 degrees (createFootprint clamps to that range); a polygon of 2 to 2,048
// points whose every coordinate times its scale stays within 1,000 mm.
constexpr double kMaxLengthMm = 500.0;
constexpr double kMaxExtentMm = 1000.0;
constexpr size_t kMinPoints = 2;
constexpr size_t kMaxPoints = 2048;

static bool readLength( const json& aObj, const char* aKey, double& aOut )
{
    auto it = aObj.find( aKey );

    if( it == aObj.end() || !finiteNumber( *it, aOut ) )
        return false;

    return aOut > 0.0 && aOut <= kMaxLengthMm;
}

/**
 * Reads the whole arm. Closed: a key the shape does not take, a missing key, a value of the
 * wrong type or out of bounds refuses it all.
 */
static bool readArm( const json& aArgs, CC_MICROWAVE_ARM& aArm )
{
    if( !aArgs.is_object() )
        return false;

    auto shapeIt = aArgs.find( "shape" );

    if( shapeIt == aArgs.end() || !shapeIt->is_string() )
        return false;

    const std::string shape = shapeIt->get<std::string>();

    if( shape == "line" || shape == "gap" || shape == "stub" )
    {
        if( !onlyKeys( aArgs, { "shape", "value" } ) || !readLength( aArgs, "value", aArm.m_valueMm ) )
            return false;

        aArm.m_shape = shape == "line" ? CC_MICROWAVE_SHAPE::LINE
                     : shape == "gap"  ? CC_MICROWAVE_SHAPE::GAP
                                       : CC_MICROWAVE_SHAPE::STUB;
        return true;
    }

    if( shape == "arcStub" )
    {
        if( !onlyKeys( aArgs, { "shape", "value", "angle" } )
                || !readLength( aArgs, "value", aArm.m_valueMm ) )
        {
            return false;
        }

        // The angle is optional: createFootprint's own dialog offers 0.0.
        aArm.m_angleDeg = 0.0;
        auto angleIt = aArgs.find( "angle" );

        if( angleIt != aArgs.end() )
        {
            if( !finiteNumber( *angleIt, aArm.m_angleDeg ) || aArm.m_angleDeg < 0.0
                    || aArm.m_angleDeg > 180.0 )
            {
                return false;
            }
        }

        aArm.m_shape = CC_MICROWAVE_SHAPE::ARC_STUB;
        return true;
    }

    if( shape == "polygon" )
    {
        if( !onlyKeys( aArgs, { "shape", "polygon" } ) )
            return false;

        auto polyIt = aArgs.find( "polygon" );

        if( polyIt == aArgs.end() || !polyIt->is_object() || polyIt->size() != 3
                || !onlyKeys( *polyIt, { "points", "kind", "size" } ) )
        {
            return false;
        }

        const json& poly = *polyIt;
        auto kindIt = poly.find( "kind" );
        auto sizeIt = poly.find( "size" );
        auto pointsIt = poly.find( "points" );

        if( !kindIt->is_string() || !sizeIt->is_array() || sizeIt->size() != 2
                || !pointsIt->is_array() )
        {
            return false;
        }

        // MWAVE_POLYGONAL_SHAPE_DLG's radio box order: Normal, Symmetrical, Mirrored.
        const std::string kind = kindIt->get<std::string>();

        if( kind == "plain" )
            aArm.m_kind = 0;
        else if( kind == "symmetrical" )
            aArm.m_kind = 1;
        else if( kind == "mirrored" )
            aArm.m_kind = 2;
        else
            return false;

        if( !finiteNumber( ( *sizeIt )[0], aArm.m_sizeXMm )
                || !finiteNumber( ( *sizeIt )[1], aArm.m_sizeYMm )
                || !( std::fabs( aArm.m_sizeXMm ) > 0.0 ) || !( std::fabs( aArm.m_sizeYMm ) > 0.0 )
                || std::fabs( aArm.m_sizeXMm ) > kMaxLengthMm
                || std::fabs( aArm.m_sizeYMm ) > kMaxLengthMm )
        {
            return false;
        }

        const json& points = *pointsIt;

        if( points.size() < kMinPoints || points.size() > kMaxPoints )
            return false;

        aArm.m_points.clear();
        aArm.m_points.reserve( points.size() );

        for( const json& pt : points )
        {
            double x = 0.0;
            double y = 0.0;

            if( !pt.is_array() || pt.size() != 2 || !finiteNumber( pt[0], x )
                    || !finiteNumber( pt[1], y )
                    || std::fabs( x * aArm.m_sizeXMm ) > kMaxExtentMm
                    || std::fabs( y * aArm.m_sizeYMm ) > kMaxExtentMm )
            {
                return false;
            }

            aArm.m_points.emplace_back( x, y );
        }

        aArm.m_shape = CC_MICROWAVE_SHAPE::POLYGON;
        return true;
    }

    return false;
}

} // namespace pcb

/**
 * kicadMicrowaveArm( argsJson ) -> bool (cap microwave, small tools spec 5.2, K-MW).
 *
 * { shape: "line" | "gap" | "stub" | "arcStub" | "polygon", value?: mm, angle?: deg,
 *   polygon?: { points: [x, y][] (2 to 2,048), kind: "symmetrical" | "mirrored" | "plain",
 *               size: [x, y] mm } }
 *
 * Arms the next microwave action's values (cc_microwave_arm.h) and answers true; the page
 * then presses the shape's action. Nothing changes on the board here: the placement is
 * KiCad's own, on the press, one commit. A read of the args only, synchronous. false for any
 * other shape of args, no board frame, an open in flight or a dialog up; a refused arm leaves
 * whatever was armed before untouched.
 */
bool microwaveArm( std::string aArgsJson )
{
    if( !pcb::readyFrame() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() )
        return false;

    CC_MICROWAVE_ARM arm;

    if( !pcb::readArm( args, arm ) )
        return false;

    cc_microwave::Arm( arm );
    return true;
}


/**
 * kicadAutoplaceQuiet() -> bool, a marker (st-ap-e, K-AP): this engine's auto-placer
 * (pcbnew.Autoplacer.autoplaceOffboard, autoplaceSelected) runs with no KiCad progress window
 * (kicad pcbnew/autorouter/autoplace_tool.cpp, AUTOPLACE_TOOL::autoplace, under
 * __EMSCRIPTEN__), so the island may list its cap autoplace and the page show its own working
 * state. Always true.
 */
bool autoplaceQuiet()
{
    return true;
}

} // namespace cc_e2

EMSCRIPTEN_BINDINGS( cc_e2_pcb )
{
    emscripten::function( "kicadMicrowaveArm", &cc_e2::microwaveArm );
    emscripten::function( "kicadAutoplaceQuiet", &cc_e2::autoplaceQuiet );
}
