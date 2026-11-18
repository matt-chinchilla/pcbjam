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

#ifdef KICAD_MERGED_EMBIND
#include <host_prompt.h>
#include <pcb_base_frame.h>
#include <zone_settings.h>
#endif

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


#ifdef KICAD_MERGED_EMBIND

namespace pcb
{

/**
 * m3-e2: a new zone's settings answered by the host (host_prompt.h, ask zoneSettings, modal),
 * reached from KiCad's three zone editors' Invoke functions through g_ccZoneSettingsHost.
 *
 * The record { kind, sheet } and the answer { set } are the records the page already reads and
 * writes for zones, never a second shape: the settings are exported into a zone that is never
 * added to the board (ZONE_SETTINGS::ExportSetting), its record read, the answer applied to it,
 * and the settings imported back from it (ZONE_SETTINGS::operator<<), all KiCad's code.
 * - copper: wave 1's zone sheet (ccItemSheet, board.item.get's zone keys) with uuid "", the
 *   answer's set keys board.item.set's zone keys (each read by ccReadItemSet as its one key set
 *   and applied by ccApplyItemSet) plus layers (copper layers the board enables, by canonical
 *   name), the layers a new zone needs. The board editor only (ccApplyItemSet is the board
 *   editor's); in another frame KiCad's own dialog shows.
 * - nonCopper, ruleArea: M4's zone record through the item kinds table (cc_fp_items.h,
 *   ccFpItemJson and ccFpItemApply of the kind zone, board frame, origin 0, 0), uuid "". While
 *   no zone kind is registered the ask is not made and KiCad's own dialog shows.
 */
class ZONE_SETTINGS_RECEIVER : public CC_PROMPT_RECEIVER
{
public:
    ZONE_SETTINGS_RECEIVER( const std::string& aKind, PCB_BASE_FRAME* aFrame, ZONE& aZone ) :
            m_kind( aKind ), m_frame( aFrame ), m_zone( aZone )
    {
    }

    bool CcApply( const json& aAnswer, std::string& aField ) override
    {
        if( !aAnswer.is_object() )
            return false;

        for( auto it = aAnswer.begin(); it != aAnswer.end(); ++it )
        {
            if( it.key() != "set" )
            {
                aField = it.key();
                return false;
            }
        }

        aField = "set";

        if( !aAnswer.contains( "set" ) || !aAnswer["set"].is_object() )
            return false;

        const json& set = aAnswer["set"];
        aField.clear();

        if( m_kind == "copper" )
            return applyCopper( set, aField );

        // The kinds table's own apply, inside a commit that is never pushed (the zone is not
        // on the board; a commit that goes out of scope only frees its copies).
        BOARD_COMMIT commit( m_frame );
        return cc_fpitems::ccFpItemApply( &m_zone, set, commit, VECTOR2I( 0, 0 ), true );
    }

private:
    bool applyCopper( const json& aSet, std::string& aField )
    {
        PCB_EDIT_FRAME* frame = static_cast<PCB_EDIT_FRAME*>( m_frame );
        BOARD*          board = frame->GetBoard();

        // Read the whole set first.
        std::vector<CC_ITEM_SET> sets;
        std::optional<LSET>      layers;

        for( auto it = aSet.begin(); it != aSet.end(); ++it )
        {
            if( it.key() == "layers" )
            {
                LSET chosen;

                if( it->is_array() && !it->empty() && it->size() <= PCB_LAYER_ID_COUNT )
                {
                    for( const json& name : *it )
                    {
                        const PCB_LAYER_ID layer = name.is_string()
                                                           ? ccLayerByName( board, name.get<std::string>() )
                                                           : UNDEFINED_LAYER;

                        if( layer == UNDEFINED_LAYER || !IsCopperLayer( layer ) )
                        {
                            chosen.reset();
                            break;
                        }

                        chosen.set( layer );
                    }
                }

                if( chosen.none() )
                {
                    aField = "layers";
                    return false;
                }

                layers = chosen;
                continue;
            }

            CC_ITEM_SET one;

            if( !ccReadItemSet( board, &m_zone, json{ { it.key(), it.value() } }, one ) )
            {
                aField = it.key();
                return false;
            }

            sets.push_back( one );
        }

        for( const CC_ITEM_SET& one : sets )
            ccApplyItemSet( frame, &m_zone, one );

        if( layers )
            m_zone.SetLayerSet( *layers );

        return true;
    }

    std::string     m_kind;
    PCB_BASE_FRAME* m_frame;
    ZONE&           m_zone;
};


int zoneSettingsAsk( const char* aKind, PCB_BASE_FRAME* aFrame, ZONE_SETTINGS* aSettings )
{
    if( !aKind || !aFrame || !aSettings || !aFrame->GetBoard() )
        return -1;

    const std::string kind = aKind;
    const bool        copper = kind == "copper";

    if( copper && !aFrame->IsType( FRAME_PCB_EDITOR ) )
        return -1;

    if( !copper && ( ( kind != "nonCopper" && kind != "ruleArea" )
                     || !cc_fpitems::ccItemKind( "zone" ) ) )
    {
        return -1;
    }

    // The settings in a zone of the board that is never added to it.
    ZONE zone( aFrame->GetBoard() );
    aSettings->ExportSetting( zone );

    json sheet = copper ? ccItemSheet( aFrame->GetBoard(), &zone )
                        : cc_fpitems::ccFpItemJson( &zone, VECTOR2I( 0, 0 ), true );

    if( !sheet.is_object() )
        return -1;

    sheet["uuid"] = "";

    json record;
    record["kind"] = kind;
    record["sheet"] = std::move( sheet );

    ZONE_SETTINGS_RECEIVER receiver( kind, aFrame, zone );
    const int              result = CcHostPromptModal( "zoneSettings", record, receiver );

    if( result == 1 )
        *aSettings << zone;

    return result;
}

} // namespace pcb

#endif // KICAD_MERGED_EMBIND

} // namespace cc_e2

#ifdef KICAD_MERGED_EMBIND
// dialog_copper_zones.cpp's hook (m3-e2), declared here as each zone editor declares it.
extern int ( *g_ccZoneSettingsHost )( const char* aKind, PCB_BASE_FRAME* aFrame,
                                      ZONE_SETTINGS* aSettings );

namespace cc_e2
{
namespace pcb
{
// Set before anything can draw a zone (static initialization of this translation unit).
[[maybe_unused]] static const bool kZoneSettingsHost = ( g_ccZoneSettingsHost = &zoneSettingsAsk, true );
} // namespace pcb
} // namespace cc_e2
#endif

EMSCRIPTEN_BINDINGS( cc_e2_pcb )
{
    emscripten::function( "kicadMicrowaveArm", &cc_e2::microwaveArm );
    emscripten::function( "kicadAutoplaceQuiet", &cc_e2::autoplaceQuiet );
}
