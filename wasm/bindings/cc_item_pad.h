// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e4 (fp-e3, FP spec 7.2): the pad kind. Header only, inline only, included
// by cc_e4_fpedit.inc alone (the fpedit translation unit), which registers the kind once
// (cc_fp_items.h's table); it registers nothing itself.
//
// The record (lengths in mm, angles in degrees, positions relative to the caller's origin, the
// angle relative to the footprint, layers by KiCad's canonical names):
//   number, pinName, pinType, type (smd, th, npth, conn, aperture), x, y, angle, locked,
//   padstack (normal, front_inner_back, custom), copper (the front, or only, shape),
//   layerShapes ([{layer, copper}], every unique layer of the padstack but F.Cu; empty for
//   normal), drill ({shape: circle | oval, size} or null), layers, dieLength, dieDelay (ps),
//   clearance, maskMargin, pasteMargin, pasteRatio (each null for none), zoneConnection
//   (inherit, none, thermal, solid), customZoneShape (outline, convexhull), thermalSpokeWidth,
//   thermalGap (each null for none), thermalAngle, property (none, bga, fiducial_global,
//   fiducial_local, testpoint, heatsink, castellated, mechanical, pressfit), and the keys
//   DIALOG_PAD_PROPERTIES::transferDataToPad sets that FP spec 7.2 lacks: unconnectedLayers
//   (keep_all, remove_except_start_and_end, remove_all), teardrop ({enabled, useTwoTracks,
//   preferZoneConnection, maxLength, maxWidth, lengthRatio, widthRatio, curved,
//   sizeFilterRatio}), backdrill ({front, back}, each {size, end} or null), postMachining
//   ({front, back}, each {mode: none | countersink | counterbore, size, depth, angle}).
// A shape (copper): shape (circle, rect, oval, trapezoid, roundrect, chamfered_rect, custom),
// size, offset, delta, roundRatio, chamferRatio, chamferCorners (tl, tr, bl, br), anchor
// (circle, rect), primitives ([{shape: line | arc | rect | circle | polygon | bezier, points,
// width, filled}], at most 512, pad local coordinates: line and rect [start, end], arc [start,
// mid, end], circle [center, a point on it], bezier [start, c1, c2, end], polygon its outline).
//
// A set is a patch of those keys (copper and each layerShapes entry partial, primitives and
// layerShapes replacing what they name, backdrill and postMachining sides whole). It follows the
// pad dialog: a shape change resets what onPadShapeSelection resets (circle squares the size and
// drops the offset, roundrect takes the IPC radius when its ratio is 0, chamfered_rect takes 0.2
// and the top left corner when it has none, the spoke angle 90 to 45 into a circle and back), a
// type change what PadTypeSelected resets (the type's default layers, the hole zeroed or given
// KiCad's default drill, number and die length dropped for npth and aperture), the layers by
// the dialog's copper choices, KEEP_ALL for every type but th with copper, an oval hole drops
// backdrill and post machining; and it is refused where the dialog refuses (CheckPad's
// padstack_invalid and pad_th_with_no_hole, a trapezoid delta past the size).

#pragma once

#ifdef __EMSCRIPTEN__

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <board.h>
#include <board_design_settings.h>
#include <convert_basic_shapes_to_polygon.h>
#include <drc/drc_item.h>
#include <footprint.h>
#include <geometry/shape_line_chain.h>
#include <geometry/shape_poly_set.h>
#include <pad.h>
#include <pad_utils.h>
#include <padstack.h>
#include <pcb_shape.h>
#include <units_provider.h>

#include "fpedit_common.h"

namespace cc_e4
{
namespace padkind
{

using json = nlohmann::json;
using namespace cc_fpedit;

constexpr size_t PRIMITIVES_MAX = 512;
constexpr double RATIO_MAX = 0.5;        // KiCad's own cap on corner and chamfer ratios
constexpr double DIE_DELAY_MAX_PS = 2000.0;

// ── names ─────────────────────────────────────────────────────────────────────────────

template <typename E>
struct NAMED
{
    const char* name;
    E           value;
};

template <typename E, size_t N>
const char* nameOf( const NAMED<E> ( &aTable )[N], E aValue )
{
    for( const NAMED<E>& n : aTable )
    {
        if( n.value == aValue )
            return n.name;
    }

    return aTable[0].name;
}

template <typename E, size_t N>
std::optional<E> valueOf( const NAMED<E> ( &aTable )[N], const json& aValue )
{
    if( !aValue.is_string() )
        return std::nullopt;

    for( const NAMED<E>& n : aTable )
    {
        if( aValue.get_ref<const std::string&>() == n.name )
            return n.value;
    }

    return std::nullopt;
}

inline const NAMED<PAD_SHAPE> SHAPES[] = {
    { "circle", PAD_SHAPE::CIRCLE },       { "rect", PAD_SHAPE::RECTANGLE },
    { "oval", PAD_SHAPE::OVAL },           { "trapezoid", PAD_SHAPE::TRAPEZOID },
    { "roundrect", PAD_SHAPE::ROUNDRECT }, { "chamfered_rect", PAD_SHAPE::CHAMFERED_RECT },
    { "custom", PAD_SHAPE::CUSTOM }
};

inline const NAMED<PAD_SHAPE> ANCHORS[] = { { "circle", PAD_SHAPE::CIRCLE },
                                            { "rect", PAD_SHAPE::RECTANGLE } };

inline const NAMED<PADSTACK::MODE> MODES[] = { { "normal", PADSTACK::MODE::NORMAL },
                                               { "front_inner_back",
                                                 PADSTACK::MODE::FRONT_INNER_BACK },
                                               { "custom", PADSTACK::MODE::CUSTOM } };

inline const NAMED<ZONE_CONNECTION> ZONE_CONNECTIONS[] = {
    { "inherit", ZONE_CONNECTION::INHERITED }, { "none", ZONE_CONNECTION::NONE },
    { "thermal", ZONE_CONNECTION::THERMAL },   { "solid", ZONE_CONNECTION::FULL }
};

inline const NAMED<CUSTOM_SHAPE_ZONE_MODE> ZONE_SHAPES[] = {
    { "outline", CUSTOM_SHAPE_ZONE_MODE::OUTLINE },
    { "convexhull", CUSTOM_SHAPE_ZONE_MODE::CONVEXHULL }
};

inline const NAMED<PAD_PROP> PROPERTIES[] = {
    { "none", PAD_PROP::NONE },
    { "bga", PAD_PROP::BGA },
    { "fiducial_global", PAD_PROP::FIDUCIAL_GLBL },
    { "fiducial_local", PAD_PROP::FIDUCIAL_LOCAL },
    { "testpoint", PAD_PROP::TESTPOINT },
    { "heatsink", PAD_PROP::HEATSINK },
    { "castellated", PAD_PROP::CASTELLATED },
    { "mechanical", PAD_PROP::MECHANICAL },
    { "pressfit", PAD_PROP::PRESSFIT }
};

inline const NAMED<UNCONNECTED_LAYER_MODE> UNCONNECTED[] = {
    { "keep_all", UNCONNECTED_LAYER_MODE::KEEP_ALL },
    { "remove_except_start_and_end", UNCONNECTED_LAYER_MODE::REMOVE_EXCEPT_START_AND_END },
    { "remove_all", UNCONNECTED_LAYER_MODE::REMOVE_ALL }
};

inline const NAMED<PAD_DRILL_POST_MACHINING_MODE> POST_MODES[] = {
    { "none", PAD_DRILL_POST_MACHINING_MODE::NOT_POST_MACHINED },
    { "countersink", PAD_DRILL_POST_MACHINING_MODE::COUNTERSINK },
    { "counterbore", PAD_DRILL_POST_MACHINING_MODE::COUNTERBORE }
};

inline const NAMED<SHAPE_T> PRIM_SHAPES[] = {
    { "line", SHAPE_T::SEGMENT }, { "arc", SHAPE_T::ARC },     { "rect", SHAPE_T::RECTANGLE },
    { "circle", SHAPE_T::CIRCLE }, { "polygon", SHAPE_T::POLY }, { "bezier", SHAPE_T::BEZIER }
};

inline const NAMED<int> CORNERS[] = { { "tl", RECT_CHAMFER_TOP_LEFT },
                                      { "tr", RECT_CHAMFER_TOP_RIGHT },
                                      { "bl", RECT_CHAMFER_BOTTOM_LEFT },
                                      { "br", RECT_CHAMFER_BOTTOM_RIGHT } };

/** The dialog's five types: aperture is an SMD pad with no copper (PAD::IsAperturePad). */
inline std::string typeName( const PAD* aPad )
{
    switch( aPad->GetAttribute() )
    {
    case PAD_ATTRIB::PTH:  return "th";
    case PAD_ATTRIB::CONN: return "conn";
    case PAD_ATTRIB::NPTH: return "npth";
    case PAD_ATTRIB::SMD:
    default:               return aPad->IsAperturePad() ? "aperture" : "smd";
    }
}

inline std::optional<PAD_ATTRIB> attribOf( const std::string& aType )
{
    if( aType == "th" )       return PAD_ATTRIB::PTH;
    if( aType == "smd" )      return PAD_ATTRIB::SMD;
    if( aType == "conn" )     return PAD_ATTRIB::CONN;
    if( aType == "npth" )     return PAD_ATTRIB::NPTH;
    if( aType == "aperture" ) return PAD_ATTRIB::SMD;
    return std::nullopt;
}

inline bool typeHasHole( const std::string& aType )
{
    return aType == "th" || aType == "npth";
}

inline bool typeHasConnection( const std::string& aType )
{
    return aType == "th" || aType == "smd" || aType == "conn";
}

/** PadTypeSelected's defaults for a type (updatePadLayersList with no stored mask). */
inline LSET defaultLayers( const std::string& aType )
{
    if( aType == "th" )   return PAD::PTHMask();
    if( aType == "conn" ) return PAD::ConnSMDMask();
    if( aType == "npth" ) return PAD::UnplatedHoleMask();
    if( aType == "aperture" ) return PAD::ApertureMask();
    return PAD::SMDMask();
}

// ── number formats ────────────────────────────────────────────────────────────────────

inline double ratioJson( double aRatio )
{
    return std::round( aRatio * 1000000.0 ) / 1000000.0;
}

inline json sizeJson( const VECTOR2I& aSize )
{
    return json::array( { mmOf( aSize.x ), mmOf( aSize.y ) } );
}

inline json nullableMm( const std::optional<int>& aValue )
{
    return aValue ? json( mmOf( *aValue ) ) : json();
}

/** A length in mm within [aMin, aMax] as internal units. */
inline std::optional<int> lengthOf( const json& aValue, double aMin, double aMax )
{
    std::optional<double> mm = numberOf( aValue, aMin, aMax );

    if( !mm )
        return std::nullopt;

    return iuOf( *mm );
}

/** [x, y] in mm, each within [aMin, aMax], as internal units. */
inline std::optional<VECTOR2I> pairOf( const json& aValue, double aMin, double aMax )
{
    if( !aValue.is_array() || aValue.size() != 2 )
        return std::nullopt;

    std::optional<int> x = lengthOf( aValue[0], aMin, aMax );
    std::optional<int> y = lengthOf( aValue[1], aMin, aMax );

    if( !x || !y )
        return std::nullopt;

    return VECTOR2I( *x, *y );
}

/** A key given as null (none) or a length: outer empty = refused. */
inline std::optional<std::optional<int>> nullableLengthOf( const json& aValue, double aMin,
                                                          double aMax )
{
    if( aValue.is_null() )
        return std::optional<int>();

    std::optional<int> v = lengthOf( aValue, aMin, aMax );

    if( !v )
        return std::nullopt;

    return std::optional<int>( *v );
}

// ── the record ────────────────────────────────────────────────────────────────────────

inline json primitiveJson( const PCB_SHAPE& aShape )
{
    json pts = json::array();

    switch( aShape.GetShape() )
    {
    case SHAPE_T::SEGMENT:
    case SHAPE_T::RECTANGLE:
    case SHAPE_T::CIRCLE:
        pts.push_back( pointJson( aShape.GetStart() ) );
        pts.push_back( pointJson( aShape.GetEnd() ) );
        break;

    case SHAPE_T::ARC:
        pts.push_back( pointJson( aShape.GetStart() ) );
        pts.push_back( pointJson( aShape.GetArcMid() ) );
        pts.push_back( pointJson( aShape.GetEnd() ) );
        break;

    case SHAPE_T::BEZIER:
        pts.push_back( pointJson( aShape.GetStart() ) );
        pts.push_back( pointJson( aShape.GetBezierC1() ) );
        pts.push_back( pointJson( aShape.GetBezierC2() ) );
        pts.push_back( pointJson( aShape.GetEnd() ) );
        break;

    case SHAPE_T::POLY:
        if( aShape.GetPolyShape().OutlineCount() > 0 )
        {
            const SHAPE_LINE_CHAIN& outline = aShape.GetPolyShape().COutline( 0 );

            for( int i = 0; i < outline.PointCount(); ++i )
                pts.push_back( pointJson( outline.CPoint( i ) ) );
        }

        break;

    default:
        break;
    }

    json out = json::object();
    out["shape"] = nameOf( PRIM_SHAPES, aShape.GetShape() );
    out["points"] = std::move( pts );
    out["width"] = mmOf( aShape.GetWidth() );
    out["filled"] = aShape.IsSolidFill();
    return out;
}

inline json shapeJson( const PAD* aPad, PCB_LAYER_ID aLayer )
{
    const PAD_SHAPE shape = aPad->GetShape( aLayer );
    json            corners = json::array();

    for( const NAMED<int>& c : CORNERS )
    {
        if( aPad->GetChamferPositions( aLayer ) & c.value )
            corners.push_back( c.name );
    }

    json prims = json::array();

    if( shape == PAD_SHAPE::CUSTOM )
    {
        for( const std::shared_ptr<PCB_SHAPE>& p : aPad->GetPrimitives( aLayer ) )
        {
            if( p )
                prims.push_back( primitiveJson( *p ) );
        }
    }

    json out = json::object();
    out["shape"] = nameOf( SHAPES, shape );
    out["size"] = sizeJson( aPad->GetSize( aLayer ) );
    out["offset"] = sizeJson( aPad->GetOffset( aLayer ) );
    out["delta"] = sizeJson( aPad->GetDelta( aLayer ) );
    out["roundRatio"] = ratioJson( aPad->GetRoundRectRadiusRatio( aLayer ) );
    out["chamferRatio"] = ratioJson( aPad->GetChamferRectRatio( aLayer ) );
    out["chamferCorners"] = std::move( corners );
    out["anchor"] = nameOf( ANCHORS, aPad->GetAnchorPadShape( aLayer ) );
    out["primitives"] = std::move( prims );
    return out;
}

inline json backdrillJson( const PADSTACK::DRILL_PROPS& aDrill )
{
    if( aDrill.start == UNDEFINED_LAYER )
        return json();

    json out = json::object();
    out["size"] = mmOf( aDrill.size.x );
    out["end"] = aDrill.end == UNDEFINED_LAYER ? json() : json( layerName( aDrill.end ) );
    return out;
}

inline json postJson( const PADSTACK::POST_MACHINING_PROPS& aPost )
{
    const PAD_DRILL_POST_MACHINING_MODE mode =
            aPost.mode.value_or( PAD_DRILL_POST_MACHINING_MODE::NOT_POST_MACHINED );

    json out = json::object();
    out["mode"] = nameOf( POST_MODES, mode );
    out["size"] = mmOf( aPost.size );
    out["depth"] = mmOf( aPost.depth );
    out["angle"] = aPost.angle / 10.0;   // the dialog keeps tenths of a degree
    return out;
}

/** The pad's record (no kind, no uuid), positions relative to aOrigin. */
inline json padJson( const PAD* aPad, const VECTOR2I& aOrigin )
{
    const PADSTACK& stack = aPad->Padstack();
    json            layerShapes = json::array();

    if( stack.Mode() != PADSTACK::MODE::NORMAL )
    {
        for( PCB_LAYER_ID layer : stack.UniqueLayers() )
        {
            if( layer == F_Cu )
                continue;

            json entry = json::object();
            entry["layer"] = layerName( layer );
            entry["copper"] = shapeJson( aPad, layer );
            layerShapes.push_back( std::move( entry ) );
        }
    }

    json drill;

    if( aPad->GetDrillSizeX() > 0 || aPad->GetDrillSizeY() > 0 )
    {
        drill = json::object();
        const bool circle = aPad->GetDrillShape() != PAD_DRILL_SHAPE::OBLONG;
        drill["shape"] = circle ? "circle" : "oval";
        // A circle hole reads by its diameter, as the dialog shows it (fix e4-2).
        drill["size"] = sizeJson( circle ? VECTOR2I( aPad->GetDrillSizeX(), aPad->GetDrillSizeX() )
                                         : aPad->GetDrillSize() );
    }

    const TEARDROP_PARAMETERS& td = aPad->GetTeardropParams();
    json                       teardrop = json::object();
    teardrop["enabled"] = td.m_Enabled;
    teardrop["useTwoTracks"] = td.m_AllowUseTwoTracks;
    teardrop["preferZoneConnection"] = !td.m_TdOnPadsInZones;
    teardrop["maxLength"] = mmOf( td.m_TdMaxLen );
    teardrop["maxWidth"] = mmOf( td.m_TdMaxWidth );
    teardrop["lengthRatio"] = ratioJson( td.m_BestLengthRatio );
    teardrop["widthRatio"] = ratioJson( td.m_BestWidthRatio );
    teardrop["curved"] = td.m_CurvedEdges;
    teardrop["sizeFilterRatio"] = ratioJson( td.m_WidthtoSizeFilterRatio );

    json backdrill = json::object();
    backdrill["front"] = backdrillJson( stack.TertiaryDrill() );
    backdrill["back"] = backdrillJson( stack.SecondaryDrill() );

    json post = json::object();
    post["front"] = postJson( stack.FrontPostMachining() );
    post["back"] = postJson( stack.BackPostMachining() );

    const std::optional<double> pasteRatio = aPad->GetLocalSolderPasteMarginRatio();

    json out = json::object();
    out["number"] = utf8( aPad->GetNumber() );
    out["pinName"] = utf8( aPad->GetPinFunction() );
    out["pinType"] = utf8( aPad->GetPinType() );
    out["type"] = typeName( aPad );
    out["x"] = mmOf( (long long) aPad->GetPosition().x - aOrigin.x );
    out["y"] = mmOf( (long long) aPad->GetPosition().y - aOrigin.y );
    out["angle"] = degreesOf( aPad->GetFPRelativeOrientation() );
    out["locked"] = aPad->IsLocked();
    out["padstack"] = nameOf( MODES, stack.Mode() );
    out["copper"] = shapeJson( aPad, F_Cu );
    out["layerShapes"] = std::move( layerShapes );
    out["drill"] = std::move( drill );
    out["layers"] = layersJson( aPad->GetLayerSet() );
    out["dieLength"] = mmOf( aPad->GetPadToDieLength() );
    out["dieDelay"] = std::round( aPad->GetPadToDieDelay() / pcbIUScale.IU_PER_PS * 1000.0 )
                      / 1000.0;
    out["clearance"] = nullableMm( aPad->GetLocalClearance() );
    out["maskMargin"] = nullableMm( aPad->GetLocalSolderMaskMargin() );
    out["pasteMargin"] = nullableMm( aPad->GetLocalSolderPasteMargin() );
    out["pasteRatio"] = pasteRatio ? json( ratioJson( *pasteRatio ) ) : json();
    out["zoneConnection"] = nameOf( ZONE_CONNECTIONS, aPad->GetLocalZoneConnection() );
    out["customZoneShape"] = nameOf( ZONE_SHAPES, aPad->GetCustomShapeInZoneOpt() );
    out["thermalSpokeWidth"] = nullableMm( aPad->GetLocalThermalSpokeWidthOverride() );
    out["thermalGap"] = nullableMm( aPad->GetLocalThermalGapOverride() );
    out["thermalAngle"] = degreesOf( aPad->GetThermalSpokeAngle() );
    out["property"] = nameOf( PROPERTIES, aPad->GetProperty() );
    out["unconnectedLayers"] = nameOf( UNCONNECTED, aPad->GetUnconnectedLayerMode() );
    out["teardrop"] = std::move( teardrop );
    out["backdrill"] = std::move( backdrill );
    out["postMachining"] = std::move( post );
    return out;
}

// ── the patch, read whole before anything changes ─────────────────────────────────────

struct SHAPE_PATCH
{
    std::optional<PAD_SHAPE>                               shape;
    std::optional<VECTOR2I>                                size;
    std::optional<VECTOR2I>                                offset;
    std::optional<VECTOR2I>                                delta;
    std::optional<double>                                  roundRatio;
    std::optional<double>                                  chamferRatio;
    std::optional<int>                                     corners;
    std::optional<PAD_SHAPE>                               anchor;
    std::optional<std::vector<std::shared_ptr<PCB_SHAPE>>> primitives;
};

struct BACKDRILL_SIDE
{
    bool         none = true;
    int          size = 0;
    PCB_LAYER_ID end = UNDEFINED_LAYER;
};

struct TEARDROP_PATCH
{
    std::optional<bool>   enabled, useTwoTracks, preferZoneConnection, curved;
    std::optional<int>    maxLength, maxWidth;
    std::optional<double> lengthRatio, widthRatio, sizeFilterRatio;
};

struct PAD_PATCH
{
    std::optional<wxString>                                     number, pinName, pinType;
    std::optional<std::string>                                  type;
    std::optional<int>                                          x, y;
    std::optional<double>                                       angle;
    std::optional<bool>                                         locked;
    std::optional<PADSTACK::MODE>                               padstack;
    std::optional<SHAPE_PATCH>                                  copper;
    std::optional<std::vector<std::pair<PCB_LAYER_ID, SHAPE_PATCH>>> layerShapes;
    bool                                                        drillGiven = false;
    std::optional<std::pair<PAD_DRILL_SHAPE, VECTOR2I>>         drill;   // empty: no hole
    std::optional<LSET>                                         layers;
    std::optional<int>                                          dieLength, dieDelay;
    std::optional<std::optional<int>> clearance, maskMargin, pasteMargin, spokeWidth, thermalGap;
    std::optional<std::optional<double>>                        pasteRatio;
    std::optional<ZONE_CONNECTION>                              zoneConnection;
    std::optional<CUSTOM_SHAPE_ZONE_MODE>                       customZoneShape;
    std::optional<double>                                       thermalAngle;
    std::optional<PAD_PROP>                                     property;
    std::optional<UNCONNECTED_LAYER_MODE>                       unconnected;
    std::optional<TEARDROP_PATCH>                               teardrop;
    std::optional<BACKDRILL_SIDE>                               backFront, backBack;
    std::optional<PADSTACK::POST_MACHINING_PROPS>               postFront, postBack;
};

inline std::shared_ptr<PCB_SHAPE> primitiveOf( const json& aValue )
{
    if( !onlyKeys( aValue, { "shape", "points", "width", "filled" } )
            || !hasKeys( aValue, { "shape", "points", "width", "filled" } ) )
    {
        return nullptr;
    }

    std::optional<SHAPE_T> shape = valueOf( PRIM_SHAPES, aValue["shape"] );
    std::optional<int>     width = lengthOf( aValue["width"], 0.0, SIZE_MAX_MM );
    std::optional<bool>    filled = boolOf( aValue["filled"] );
    const json&            list = aValue["points"];

    if( !shape || !width || !filled || !list.is_array() || list.size() > POINTS_MAX )
        return nullptr;

    std::vector<VECTOR2I> pts;

    for( const json& p : list )
    {
        std::optional<VECTOR2I> pt = pointOf( p );

        if( !pt )
            return nullptr;

        pts.push_back( *pt );
    }

    size_t want = 0;

    switch( *shape )
    {
    case SHAPE_T::SEGMENT:
    case SHAPE_T::RECTANGLE:
    case SHAPE_T::CIRCLE: want = 2; break;
    case SHAPE_T::ARC:    want = 3; break;
    case SHAPE_T::BEZIER: want = 4; break;
    default:              want = 0; break;
    }

    if( ( want && pts.size() != want ) || ( !want && pts.size() < 3 ) )
        return nullptr;

    auto prim = std::make_shared<PCB_SHAPE>( nullptr, *shape );

    switch( *shape )
    {
    case SHAPE_T::SEGMENT:
    case SHAPE_T::RECTANGLE:
    case SHAPE_T::CIRCLE:
        prim->SetStart( pts[0] );
        prim->SetEnd( pts[1] );
        break;

    case SHAPE_T::ARC:
        prim->SetArcGeometry( pts[0], pts[1], pts[2] );
        break;

    case SHAPE_T::BEZIER:
        prim->SetStart( pts[0] );
        prim->SetBezierC1( pts[1] );
        prim->SetBezierC2( pts[2] );
        prim->SetEnd( pts[3] );
        prim->RebuildBezierToSegmentsPointsList( ARC_HIGH_DEF );
        break;

    default:
        prim->SetPolyPoints( pts );
        break;
    }

    prim->SetWidth( *width );
    prim->SetFilled( *filled );
    return prim;
}

inline bool readShape( const json& aValue, SHAPE_PATCH& aOut )
{
    if( !onlyKeys( aValue, { "shape", "size", "offset", "delta", "roundRatio", "chamferRatio",
                             "chamferCorners", "anchor", "primitives" } ) )
    {
        return false;
    }

    if( aValue.contains( "shape" ) && !( aOut.shape = valueOf( SHAPES, aValue["shape"] ) ) )
        return false;

    if( aValue.contains( "size" ) )
    {
        aOut.size = pairOf( aValue["size"], 0.0, SIZE_MAX_MM );

        if( !aOut.size || aOut.size->x <= 0 || aOut.size->y <= 0 )
            return false;
    }

    if( aValue.contains( "offset" )
            && !( aOut.offset = pairOf( aValue["offset"], -SIZE_MAX_MM, SIZE_MAX_MM ) ) )
    {
        return false;
    }

    if( aValue.contains( "delta" )
            && !( aOut.delta = pairOf( aValue["delta"], -SIZE_MAX_MM, SIZE_MAX_MM ) ) )
    {
        return false;
    }

    if( aValue.contains( "roundRatio" )
            && !( aOut.roundRatio = numberOf( aValue["roundRatio"], 0.0, RATIO_MAX ) ) )
    {
        return false;
    }

    if( aValue.contains( "chamferRatio" )
            && !( aOut.chamferRatio = numberOf( aValue["chamferRatio"], 0.0, RATIO_MAX ) ) )
    {
        return false;
    }

    if( aValue.contains( "chamferCorners" ) )
    {
        const json& list = aValue["chamferCorners"];
        int         bits = 0;

        if( !list.is_array() || list.size() > 4 )
            return false;

        for( const json& c : list )
        {
            std::optional<int> bit = valueOf( CORNERS, c );

            if( !bit || ( bits & *bit ) )
                return false;

            bits |= *bit;
        }

        aOut.corners = bits;
    }

    if( aValue.contains( "anchor" ) && !( aOut.anchor = valueOf( ANCHORS, aValue["anchor"] ) ) )
        return false;

    if( aValue.contains( "primitives" ) )
    {
        const json& list = aValue["primitives"];

        if( !list.is_array() || list.size() > PRIMITIVES_MAX )
            return false;

        std::vector<std::shared_ptr<PCB_SHAPE>> prims;

        for( const json& p : list )
        {
            std::shared_ptr<PCB_SHAPE> prim = primitiveOf( p );

            if( !prim )
                return false;

            prims.push_back( prim );
        }

        aOut.primitives = std::move( prims );
    }

    return true;
}

inline bool readBackdrill( const json& aValue, BACKDRILL_SIDE& aOut )
{
    aOut = BACKDRILL_SIDE();

    if( aValue.is_null() )
        return true;

    if( !onlyKeys( aValue, { "size", "end" } ) || !hasKeys( aValue, { "size", "end" } ) )
        return false;

    std::optional<int> size = lengthOf( aValue["size"], 0.0, SIZE_MAX_MM );

    if( !size )
        return false;

    aOut.none = false;
    aOut.size = *size;

    if( !aValue["end"].is_null() )
    {
        std::optional<PCB_LAYER_ID> end = layerOf( aValue["end"] );

        if( !end || !IsCopperLayer( *end ) )
            return false;

        aOut.end = *end;
    }

    return true;
}

inline bool readPost( const json& aValue, PADSTACK::POST_MACHINING_PROPS& aOut )
{
    if( !onlyKeys( aValue, { "mode", "size", "depth", "angle" } ) || !aValue.contains( "mode" ) )
        return false;

    std::optional<PAD_DRILL_POST_MACHINING_MODE> mode = valueOf( POST_MODES, aValue["mode"] );

    if( !mode )
        return false;

    aOut = PADSTACK::POST_MACHINING_PROPS();
    aOut.mode = *mode;

    if( aValue.contains( "size" ) )
    {
        std::optional<int> size = lengthOf( aValue["size"], 0.0, SIZE_MAX_MM );

        if( !size )
            return false;

        aOut.size = *size;
    }

    // The dialog keeps the depth for a counterbore and the angle for a countersink.
    if( aValue.contains( "depth" ) )
    {
        std::optional<int> depth = lengthOf( aValue["depth"], 0.0, SIZE_MAX_MM );

        if( !depth )
            return false;

        if( *mode != PAD_DRILL_POST_MACHINING_MODE::COUNTERSINK )
            aOut.depth = *depth;
    }

    if( aValue.contains( "angle" ) )
    {
        std::optional<double> angle = numberOf( aValue["angle"], 0.0, 180.0 );

        if( !angle )
            return false;

        if( *mode == PAD_DRILL_POST_MACHINING_MODE::COUNTERSINK )
            aOut.angle = KiROUND( *angle * 10.0 );
    }

    return true;
}

inline bool readTeardrop( const json& aValue, TEARDROP_PATCH& aOut )
{
    if( !onlyKeys( aValue, { "enabled", "useTwoTracks", "preferZoneConnection", "maxLength",
                             "maxWidth", "lengthRatio", "widthRatio", "curved",
                             "sizeFilterRatio" } ) )
    {
        return false;
    }

    auto flag = [&]( const char* aKey, std::optional<bool>& aSlot )
    {
        if( !aValue.contains( aKey ) )
            return true;

        aSlot = boolOf( aValue[aKey] );
        return aSlot.has_value();
    };

    auto length = [&]( const char* aKey, std::optional<int>& aSlot )
    {
        if( !aValue.contains( aKey ) )
            return true;

        aSlot = lengthOf( aValue[aKey], 0.0, SIZE_MAX_MM );
        return aSlot.has_value();
    };

    auto ratio = [&]( const char* aKey, std::optional<double>& aSlot )
    {
        if( !aValue.contains( aKey ) )
            return true;

        aSlot = numberOf( aValue[aKey], 0.0, 10.0 );
        return aSlot.has_value();
    };

    return flag( "enabled", aOut.enabled ) && flag( "useTwoTracks", aOut.useTwoTracks )
           && flag( "preferZoneConnection", aOut.preferZoneConnection )
           && flag( "curved", aOut.curved ) && length( "maxLength", aOut.maxLength )
           && length( "maxWidth", aOut.maxWidth ) && ratio( "lengthRatio", aOut.lengthRatio )
           && ratio( "widthRatio", aOut.widthRatio )
           && ratio( "sizeFilterRatio", aOut.sizeFilterRatio );
}

/** A set (or an add's record less kind and uuid) read whole: false for any other shape. */
inline bool readPatch( const json& aSet, PAD_PATCH& aOut )
{
    if( !onlyKeys( aSet, { "number", "pinName", "pinType", "type", "x", "y", "angle", "locked",
                           "padstack", "copper", "layerShapes", "drill", "layers", "dieLength",
                           "dieDelay", "clearance", "maskMargin", "pasteMargin", "pasteRatio",
                           "zoneConnection", "customZoneShape", "thermalSpokeWidth",
                           "thermalGap", "thermalAngle", "property", "unconnectedLayers",
                           "teardrop", "backdrill", "postMachining" } ) )
    {
        return false;
    }

    auto text = [&]( const char* aKey, std::optional<wxString>& aSlot )
    {
        if( !aSet.contains( aKey ) )
            return true;

        aSlot = textOf( aSet[aKey], 0, TEXT_MAX );
        return aSlot.has_value();
    };

    if( !text( "number", aOut.number ) || !text( "pinName", aOut.pinName )
            || !text( "pinType", aOut.pinType ) )
    {
        return false;
    }

    if( aSet.contains( "type" ) )
    {
        if( !aSet["type"].is_string() || !attribOf( aSet["type"].get<std::string>() ) )
            return false;

        aOut.type = aSet["type"].get<std::string>();
    }

    if( aSet.contains( "x" )
            && !( aOut.x = lengthOf( aSet["x"], -POSITION_MAX_MM, POSITION_MAX_MM ) ) )
    {
        return false;
    }

    if( aSet.contains( "y" )
            && !( aOut.y = lengthOf( aSet["y"], -POSITION_MAX_MM, POSITION_MAX_MM ) ) )
    {
        return false;
    }

    if( aSet.contains( "angle" ) && !( aOut.angle = numberOf( aSet["angle"], -360.0, 360.0 ) ) )
        return false;

    if( aSet.contains( "locked" ) && !( aOut.locked = boolOf( aSet["locked"] ) ) )
        return false;

    if( aSet.contains( "padstack" ) && !( aOut.padstack = valueOf( MODES, aSet["padstack"] ) ) )
        return false;

    if( aSet.contains( "copper" ) )
    {
        SHAPE_PATCH s;

        if( !readShape( aSet["copper"], s ) )
            return false;

        aOut.copper = std::move( s );
    }

    if( aSet.contains( "layerShapes" ) )
    {
        const json& list = aSet["layerShapes"];

        if( !list.is_array() || list.size() > 64 )
            return false;

        std::vector<std::pair<PCB_LAYER_ID, SHAPE_PATCH>> shapes;
        LSET                                              seen;

        for( const json& entry : list )
        {
            if( !onlyKeys( entry, { "layer", "copper" } )
                    || !hasKeys( entry, { "layer", "copper" } ) )
            {
                return false;
            }

            std::optional<PCB_LAYER_ID> layer = layerOf( entry["layer"] );
            SHAPE_PATCH                 s;

            if( !layer || !IsCopperLayer( *layer ) || *layer == F_Cu || seen.Contains( *layer )
                    || !readShape( entry["copper"], s ) )
            {
                return false;
            }

            seen.set( *layer );
            shapes.emplace_back( *layer, std::move( s ) );
        }

        aOut.layerShapes = std::move( shapes );
    }

    if( aSet.contains( "drill" ) )
    {
        const json& d = aSet["drill"];
        aOut.drillGiven = true;

        if( !d.is_null() )
        {
            if( !onlyKeys( d, { "shape", "size" } ) || !hasKeys( d, { "shape", "size" } )
                    || !d["shape"].is_string() )
            {
                return false;
            }

            const std::string     shape = d["shape"].get<std::string>();
            std::optional<VECTOR2I> size = pairOf( d["size"], 0.0, SIZE_MAX_MM );

            if( ( shape != "circle" && shape != "oval" ) || !size || size->x <= 0 )
                return false;

            if( shape == "circle" )
                aOut.drill = std::make_pair( PAD_DRILL_SHAPE::CIRCLE, VECTOR2I( size->x, size->x ) );
            else if( size->y > 0 )
                aOut.drill = std::make_pair( PAD_DRILL_SHAPE::OBLONG, *size );
            else
                return false;
        }
    }

    if( aSet.contains( "layers" ) && !( aOut.layers = layersOf( aSet["layers"], 0, 256 ) ) )
        return false;

    if( aSet.contains( "dieLength" )
            && !( aOut.dieLength = lengthOf( aSet["dieLength"], 0.0, SIZE_MAX_MM ) ) )
    {
        return false;
    }

    if( aSet.contains( "dieDelay" ) )
    {
        std::optional<double> ps = numberOf( aSet["dieDelay"], 0.0, DIE_DELAY_MAX_PS );

        if( !ps )
            return false;

        aOut.dieDelay = KiROUND( *ps * pcbIUScale.IU_PER_PS );
    }

    auto nullable = [&]( const char* aKey, double aMin, std::optional<std::optional<int>>& aSlot )
    {
        if( !aSet.contains( aKey ) )
            return true;

        aSlot = nullableLengthOf( aSet[aKey], aMin, SIZE_MAX_MM );
        return aSlot.has_value();
    };

    if( !nullable( "clearance", 0.0, aOut.clearance )
            || !nullable( "maskMargin", -SIZE_MAX_MM, aOut.maskMargin )
            || !nullable( "pasteMargin", -SIZE_MAX_MM, aOut.pasteMargin )
            || !nullable( "thermalSpokeWidth", 0.0, aOut.spokeWidth )
            || !nullable( "thermalGap", 0.0, aOut.thermalGap ) )
    {
        return false;
    }

    if( aSet.contains( "pasteRatio" ) )
    {
        if( aSet["pasteRatio"].is_null() )
        {
            aOut.pasteRatio = std::optional<double>();
        }
        else
        {
            std::optional<double> r = numberOf( aSet["pasteRatio"], -1.0, 1.0 );

            if( !r )
                return false;

            aOut.pasteRatio = std::optional<double>( *r );
        }
    }

    if( aSet.contains( "zoneConnection" )
            && !( aOut.zoneConnection = valueOf( ZONE_CONNECTIONS, aSet["zoneConnection"] ) ) )
    {
        return false;
    }

    if( aSet.contains( "customZoneShape" )
            && !( aOut.customZoneShape = valueOf( ZONE_SHAPES, aSet["customZoneShape"] ) ) )
    {
        return false;
    }

    if( aSet.contains( "thermalAngle" )
            && !( aOut.thermalAngle = numberOf( aSet["thermalAngle"], -360.0, 360.0 ) ) )
    {
        return false;
    }

    if( aSet.contains( "property" ) && !( aOut.property = valueOf( PROPERTIES, aSet["property"] ) ) )
        return false;

    if( aSet.contains( "unconnectedLayers" )
            && !( aOut.unconnected = valueOf( UNCONNECTED, aSet["unconnectedLayers"] ) ) )
    {
        return false;
    }

    if( aSet.contains( "teardrop" ) )
    {
        TEARDROP_PATCH td;

        if( !readTeardrop( aSet["teardrop"], td ) )
            return false;

        aOut.teardrop = td;
    }

    if( aSet.contains( "backdrill" ) )
    {
        const json& b = aSet["backdrill"];

        if( !onlyKeys( b, { "front", "back" } ) )
            return false;

        BACKDRILL_SIDE side;

        if( b.contains( "front" ) )
        {
            if( !readBackdrill( b["front"], side ) )
                return false;

            aOut.backFront = side;
        }

        if( b.contains( "back" ) )
        {
            if( !readBackdrill( b["back"], side ) )
                return false;

            aOut.backBack = side;
        }
    }

    if( aSet.contains( "postMachining" ) )
    {
        const json& m = aSet["postMachining"];

        if( !onlyKeys( m, { "front", "back" } ) )
            return false;

        PADSTACK::POST_MACHINING_PROPS side;

        if( m.contains( "front" ) )
        {
            if( !readPost( m["front"], side ) )
                return false;

            aOut.postFront = side;
        }

        if( m.contains( "back" ) )
        {
            if( !readPost( m["back"], side ) )
                return false;

            aOut.postBack = side;
        }
    }

    return true;
}

// ── applying (the dialog's order and resets) ──────────────────────────────────────────

/** One copper layer's shape; false where the dialog refuses (a trapezoid delta too large). */
inline bool applyShape( PAD* aPad, PCB_LAYER_ID aLayer, const SHAPE_PATCH& aPatch )
{
    const PAD_SHAPE oldShape = aPad->GetShape( aLayer );
    const PAD_SHAPE shape = aPatch.shape.value_or( oldShape );
    const bool      changed = shape != oldShape;
    const PAD_SHAPE anchor = aPatch.anchor.value_or( aPad->GetAnchorPadShape( aLayer ) );
    VECTOR2I        size = aPatch.size.value_or( aPad->GetSize( aLayer ) );

    if( aPatch.primitives && !aPatch.primitives->empty() && shape != PAD_SHAPE::CUSTOM )
        return false;

    aPad->SetShape( aLayer, shape );
    aPad->SetAnchorPadShape( aLayer, shape == PAD_SHAPE::CUSTOM && anchor == PAD_SHAPE::RECTANGLE
                                             ? PAD_SHAPE::RECTANGLE
                                             : PAD_SHAPE::CIRCLE );

    if( shape == PAD_SHAPE::CUSTOM && aPatch.primitives )
        aPad->ReplacePrimitives( aLayer, *aPatch.primitives );

    // A circle, and a custom pad on a circular anchor, have one size (the dialog's size Y off).
    if( shape == PAD_SHAPE::CIRCLE
            || ( shape == PAD_SHAPE::CUSTOM && aPad->GetAnchorPadShape( aLayer ) == PAD_SHAPE::CIRCLE ) )
    {
        size.y = size.x;
    }

    aPad->SetSize( aLayer, size );

    // A trapezoid's delta: one axis only, never past the size (the dialog's error); else none.
    VECTOR2I delta( 0, 0 );

    if( shape == PAD_SHAPE::TRAPEZOID )
    {
        delta = aPatch.delta.value_or( aPad->GetDelta( aLayer ) );

        if( delta.x != 0 && delta.y != 0 )
            return false;

        if( std::abs( delta.x ) >= size.y || std::abs( delta.y ) >= size.x )
            return false;
    }

    aPad->SetDelta( aLayer, delta );

    // A circle has no offset (the dialog disables it and clears its box).
    aPad->SetOffset( aLayer, shape == PAD_SHAPE::CIRCLE
                                     ? VECTOR2I( 0, 0 )
                                     : aPatch.offset.value_or( aPad->GetOffset( aLayer ) ) );

    int corners = 0;

    if( shape == PAD_SHAPE::CHAMFERED_RECT )
    {
        corners = aPatch.corners.value_or( aPad->GetChamferPositions( aLayer ) );

        // A reasonable default is one corner chamfered (onPadShapeSelection).
        if( changed && corners == 0 && !aPatch.corners )
            corners = RECT_CHAMFER_TOP_LEFT;
    }

    aPad->SetChamferPositions( aLayer, corners );

    if( shape == PAD_SHAPE::ROUNDRECT )
    {
        double ratio = aPatch.roundRatio.value_or( aPad->GetRoundRectRadiusRatio( aLayer ) );

        if( changed && ratio == 0.0 && !aPatch.roundRatio )
            ratio = PAD_UTILS::GetDefaultIpcRoundingRatio( *aPad, aLayer );

        aPad->SetRoundRectRadiusRatio( aLayer, ratio );
    }
    else if( shape == PAD_SHAPE::CHAMFERED_RECT )
    {
        double chamfer = aPatch.chamferRatio.value_or( aPad->GetChamferRectRatio( aLayer ) );

        if( changed && chamfer == 0.0 && !aPatch.chamferRatio )
            chamfer = 0.2;

        aPad->SetChamferRectRatio( aLayer, chamfer );

        if( aPatch.roundRatio )
            aPad->SetRoundRectRadiusRatio( aLayer, *aPatch.roundRatio );
    }

    return true;
}

/** The dialog's copper choices for a type: false for a layer set the dialog cannot make. */
inline std::optional<LSET> layersForType( const LSET& aLayers, const std::string& aType )
{
    const LSET copper = aLayers & LSET::AllCuMask();
    LSET       out = aLayers & ~LSET::AllCuMask();

    if( aType == "th" )
    {
        if( copper.any() )
            out |= LSET::AllCuMask();     // all copper layers (KiCad's choice 0 to 2)
    }
    else if( aType == "smd" || aType == "conn" )
    {
        if( copper.count() != 1 || !( copper.test( F_Cu ) || copper.test( B_Cu ) ) )
            return std::nullopt;

        out |= copper;
    }
    else if( aType == "npth" )
    {
        if( ( copper & ~LSET( { F_Cu, B_Cu } ) ).any() )
            return std::nullopt;

        out |= copper;
    }
    else if( copper.any() )   // aperture: no copper
    {
        return std::nullopt;
    }

    return out;
}

/** The patch onto aPad. False where the dialog refuses; then aPad may be half changed, so a
 *  caller runs it on a copy first (applyChecked). */
inline bool applyPatch( PAD* aPad, const PAD_PATCH& aPatch, const VECTOR2I& aOrigin )
{
    const std::string oldType = typeName( aPad );
    const std::string type = aPatch.type.value_or( oldType );
    const bool        typeChanged = type != oldType;

    // The padstack mode (its controls are shown for th pads only).
    if( aPatch.padstack && *aPatch.padstack != PADSTACK::MODE::NORMAL && type != "th" )
        return false;

    std::map<PCB_LAYER_ID, PCB_LAYER_ID> newLayerMap;

    if( aPatch.padstack )
    {
        const PADSTACK oldStack = aPad->Padstack();
        aPad->Padstack().SetMode( *aPatch.padstack );

        aPad->Padstack().ForEachUniqueLayer(
                [&]( PCB_LAYER_ID aLayer )
                {
                    if( !oldStack.HasExplicitDefinitionForLayer( aLayer ) )
                    {
                        if( IsInnerCopperLayer( aLayer ) && aLayer != PADSTACK::INNER_LAYERS )
                            newLayerMap[aLayer] = PADSTACK::INNER_LAYERS;
                        else
                            newLayerMap[aLayer] = F_Cu;
                    }
                } );
    }

    aPad->SetAttribute( *attribOf( type ) );

    // The front shape, and the spoke angle the dialog moves with a circle.
    const PAD_SHAPE oldFront = aPad->GetShape( F_Cu );

    if( !applyShape( aPad, F_Cu, aPatch.copper.value_or( SHAPE_PATCH() ) ) )
        return false;

    const PAD_SHAPE front = aPad->GetShape( F_Cu );

    if( front != oldFront )
    {
        if( front == PAD_SHAPE::CIRCLE && aPad->GetThermalSpokeAngle() == ANGLE_90 )
            aPad->SetThermalSpokeAngle( ANGLE_45 );
        else if( oldFront == PAD_SHAPE::CIRCLE && aPad->GetThermalSpokeAngle() == ANGLE_45 )
            aPad->SetThermalSpokeAngle( ANGLE_90 );
    }

    if( aPatch.teardrop )
    {
        TEARDROP_PARAMETERS&  td = aPad->GetTeardropParams();
        const TEARDROP_PATCH& p = *aPatch.teardrop;

        if( p.enabled )              td.m_Enabled = *p.enabled;
        if( p.useTwoTracks )         td.m_AllowUseTwoTracks = *p.useTwoTracks;
        if( p.preferZoneConnection ) td.m_TdOnPadsInZones = !*p.preferZoneConnection;
        if( p.maxLength )            td.m_TdMaxLen = *p.maxLength;
        if( p.maxWidth )             td.m_TdMaxWidth = *p.maxWidth;
        if( p.lengthRatio )          td.m_BestLengthRatio = *p.lengthRatio;
        if( p.widthRatio )           td.m_BestWidthRatio = *p.widthRatio;
        if( p.curved )               td.m_CurvedEdges = *p.curved;
        if( p.sizeFilterRatio )      td.m_WidthtoSizeFilterRatio = *p.sizeFilterRatio;
    }

    if( aPatch.clearance )   aPad->SetLocalClearance( *aPatch.clearance );
    if( aPatch.maskMargin )  aPad->SetLocalSolderMaskMargin( *aPatch.maskMargin );
    if( aPatch.pasteMargin ) aPad->SetLocalSolderPasteMargin( *aPatch.pasteMargin );
    if( aPatch.pasteRatio )  aPad->SetLocalSolderPasteMarginRatio( *aPatch.pasteRatio );
    if( aPatch.spokeWidth )  aPad->SetLocalThermalSpokeWidthOverride( *aPatch.spokeWidth );
    if( aPatch.thermalGap )  aPad->SetLocalThermalGapOverride( *aPatch.thermalGap );

    if( aPatch.thermalAngle )
        aPad->SetThermalSpokeAngle( EDA_ANGLE( *aPatch.thermalAngle, DEGREES_T ) );

    if( aPatch.angle )
        aPad->SetFPRelativeOrientation( EDA_ANGLE( *aPatch.angle, DEGREES_T ) );

    if( aPatch.zoneConnection )
        aPad->SetLocalZoneConnection( *aPatch.zoneConnection );

    if( aPatch.x || aPatch.y )
    {
        VECTOR2I pos = aPad->GetPosition();

        if( aPatch.x )
            pos.x = aOrigin.x + *aPatch.x;

        if( aPatch.y )
            pos.y = aOrigin.y + *aPatch.y;

        aPad->SetPosition( pos );
    }

    // The hole: what the type allows (PadTypeSelected, then transferDataToPad's switch).
    if( typeHasHole( type ) )
    {
        if( aPatch.drillGiven )
        {
            if( aPatch.drill )
            {
                aPad->SetDrillShape( aPatch.drill->first );
                aPad->SetDrillSize( aPatch.drill->second );
            }
            else
            {
                aPad->SetDrillShape( PAD_DRILL_SHAPE::CIRCLE );
                aPad->SetDrillSize( VECTOR2I( 0, 0 ) );
            }
        }
        else if( typeChanged && aPad->GetDrillSizeX() == 0 )
        {
            const int d = pcbIUScale.mmToIU( DEFAULT_PAD_DRILL_DIAMETER_MM );
            aPad->SetDrillShape( PAD_DRILL_SHAPE::CIRCLE );
            aPad->SetDrillSize( VECTOR2I( d, d ) );
        }

        // A circle hole is its diameter alone: transferDataToPad writes the X box to both sizes
        // (dialog_pad_properties.cpp, CHOICE_SHAPE_CIRCLE). KiCad's own master pad comes from
        // SetDefaultMasterPad with a drill of 0.8 by 0, which CheckPad refuses until the dialog
        // has written it so; without this every set on the master and every new pad (it imports
        // the master) was refused (fix e4-2).
        if( aPad->GetDrillShape() == PAD_DRILL_SHAPE::CIRCLE
                && aPad->GetDrillSizeY() != aPad->GetDrillSizeX() )
        {
            aPad->SetDrillSize( VECTOR2I( aPad->GetDrillSizeX(), aPad->GetDrillSizeX() ) );
        }
    }
    else
    {
        if( aPatch.drillGiven && aPatch.drill )
            return false;

        aPad->SetDrillSize( VECTOR2I( 0, 0 ) );
    }

    // Number and die length: none for a type with no connection.
    if( typeHasConnection( type ) )
    {
        if( aPatch.number )    aPad->SetNumber( *aPatch.number );
        if( aPatch.dieLength ) aPad->SetPadToDieLength( *aPatch.dieLength );
        if( aPatch.dieDelay )  aPad->SetPadToDieDelay( *aPatch.dieDelay );
    }
    else
    {
        if( ( aPatch.number && !aPatch.number->IsEmpty() )
                || ( aPatch.dieLength && *aPatch.dieLength != 0 )
                || ( aPatch.dieDelay && *aPatch.dieDelay != 0 ) )
        {
            return false;
        }

        aPad->SetNumber( wxEmptyString );
        aPad->SetPadToDieLength( 0 );
        aPad->SetPadToDieDelay( 0 );
    }

    if( aPatch.pinName ) aPad->SetPinFunction( *aPatch.pinName );
    if( aPatch.pinType ) aPad->SetPinType( *aPatch.pinType );

    if( aPatch.customZoneShape )
        aPad->SetCustomShapeInZoneOpt( *aPatch.customZoneShape );

    if( aPatch.property )
        aPad->SetProperty( *aPatch.property );

    // The layers: the given set by the dialog's copper choices, the type's defaults when the
    // type changed, else the pad's own (kept within what the type allows).
    std::optional<LSET> layers;

    if( aPatch.layers )
        layers = layersForType( *aPatch.layers, type );
    else if( typeChanged )
        layers = defaultLayers( type );
    else
        layers = aPad->GetLayerSet();

    if( !layers )
        return false;

    aPad->SetLayerSet( *layers );

    // Unconnected copper: KEEP_ALL but for a th pad with copper (the dialog's radio).
    const UNCONNECTED_LAYER_MODE oldMode = aPad->GetUnconnectedLayerMode();

    if( type == "th" && ( *layers & LSET::AllCuMask() ).any() )
        aPad->SetUnconnectedLayerMode( aPatch.unconnected.value_or( oldMode ) );
    else if( aPatch.unconnected && *aPatch.unconnected != UNCONNECTED_LAYER_MODE::KEEP_ALL )
        return false;
    else
        aPad->SetUnconnectedLayerMode( UNCONNECTED_LAYER_MODE::KEEP_ALL );

    // Backdrill (back = secondary from B.Cu, front = tertiary from F.Cu) and post machining;
    // an oval hole has neither (OnDrillShapeSelected).
    PADSTACK&  stack = aPad->Padstack();
    const bool oval = aPad->GetDrillShape() == PAD_DRILL_SHAPE::OBLONG
                      && aPad->GetDrillSizeX() > 0;

    auto backdrill = [&]( const std::optional<BACKDRILL_SIDE>& aSide, PADSTACK::DRILL_PROPS& aDrill,
                          PCB_LAYER_ID aStart )
    {
        if( !aSide )
            return true;

        PADSTACK::DRILL_PROPS d;
        d.shape = PAD_DRILL_SHAPE::CIRCLE;

        if( !aSide->none )
        {
            d.size = VECTOR2I( aSide->size, aSide->size );
            d.start = aStart;
            d.end = aSide->end;
        }

        aDrill = d;
        return true;
    };

    if( oval && ( ( aPatch.backFront && !aPatch.backFront->none )
                  || ( aPatch.backBack && !aPatch.backBack->none )
                  || ( aPatch.postFront && aPatch.postFront->mode
                       != PAD_DRILL_POST_MACHINING_MODE::NOT_POST_MACHINED )
                  || ( aPatch.postBack && aPatch.postBack->mode
                       != PAD_DRILL_POST_MACHINING_MODE::NOT_POST_MACHINED ) ) )
    {
        return false;
    }

    backdrill( aPatch.backFront, stack.TertiaryDrill(), F_Cu );
    backdrill( aPatch.backBack, stack.SecondaryDrill(), B_Cu );

    if( aPatch.postFront )
        stack.FrontPostMachining() = *aPatch.postFront;

    if( aPatch.postBack )
        stack.BackPostMachining() = *aPatch.postBack;

    if( oval )
    {
        stack.SecondaryDrill() = PADSTACK::DRILL_PROPS();
        stack.TertiaryDrill() = PADSTACK::DRILL_PROPS();
        stack.FrontPostMachining() = PADSTACK::POST_MACHINING_PROPS();
        stack.BackPostMachining() = PADSTACK::POST_MACHINING_PROPS();
        stack.FrontPostMachining().mode = PAD_DRILL_POST_MACHINING_MODE::NOT_POST_MACHINED;
        stack.BackPostMachining().mode = PAD_DRILL_POST_MACHINING_MODE::NOT_POST_MACHINED;
    }

    // Layers the new padstack mode made explicit start from the front (or inner) shape.
    for( const auto& [newLayer, initFrom] : newLayerMap )
    {
        if( newLayer != F_Cu && newLayer != initFrom )
            stack.CopperLayer( newLayer ) = stack.CopperLayer( initFrom );
    }

    if( aPatch.layerShapes )
    {
        const std::vector<PCB_LAYER_ID> unique = stack.UniqueLayers();

        for( const auto& [layer, shape] : *aPatch.layerShapes )
        {
            if( std::find( unique.begin(), unique.end(), layer ) == unique.end() )
                return false;

            if( !applyShape( aPad, layer, shape ) )
                return false;
        }
    }

    if( aPatch.locked )
        aPad->SetLocked( *aPatch.locked );

    aPad->SetDirty();
    return true;
}

/** padValuesOK: CheckPad's errors refuse (a warning, DRCE_PADSTACK, does not). */
inline bool padValid( const PAD* aPad )
{
    UNITS_PROVIDER units( pcbIUScale, EDA_UNITS::MM );
    bool           ok = true;

    aPad->CheckPad( &units, true,
            [&]( int aCode, const wxString& )
            {
                if( aCode == DRCE_PADSTACK_INVALID || aCode == DRCE_PAD_TH_WITH_NO_HOLE )
                    ok = false;
            } );

    return ok;
}

/** The patch tried on a copy of aPad: true when the dialog would take it. */
inline bool patchTakes( const PAD* aPad, const PAD_PATCH& aPatch, const VECTOR2I& aOrigin )
{
    std::unique_ptr<PAD> probe( static_cast<PAD*>( aPad->Clone() ) );
    return applyPatch( probe.get(), aPatch, aOrigin ) && padValid( probe.get() );
}

// ── the kind's three bodies (cc_fp_items.h's contract) ────────────────────────────────

inline json kindGet( BOARD_ITEM* aItem, const VECTOR2I& aOrigin, bool /*aBoardFrame*/ )
{
    if( !aItem || aItem->Type() != PCB_PAD_T )
        return json();

    return padJson( static_cast<PAD*>( aItem ), aOrigin );
}

inline bool kindSet( BOARD_ITEM* aItem, const json& aSet, BOARD_COMMIT& aCommit,
                     const VECTOR2I& aOrigin, bool /*aBoardFrame*/ )
{
    if( !aItem || aItem->Type() != PCB_PAD_T )
        return false;

    PAD*      pad = static_cast<PAD*>( aItem );
    PAD_PATCH patch;

    if( !readPatch( aSet, patch ) || !patchTakes( pad, patch, aOrigin ) )
        return false;

    aCommit.Modify( pad );
    return applyPatch( pad, patch, aOrigin );
}

inline BOARD_ITEM* kindAdd( BOARD_ITEM_CONTAINER* aParent, const json& aItem,
                            const VECTOR2I& aOrigin, bool /*aBoardFrame*/ )
{
    FOOTPRINT* footprint = dynamic_cast<FOOTPRINT*>( aParent );
    PAD_PATCH  patch;

    if( !footprint || !readPatch( aItem, patch ) )
        return nullptr;

    // A new pad starts as KiCad's pad tool starts one: the master pad's settings, at the origin.
    std::unique_ptr<PAD> pad = std::make_unique<PAD>( footprint );

    if( BOARD* board = footprint->GetBoard() )
    {
        if( const PAD* master = board->GetDesignSettings().m_Pad_Master.get() )
            pad->ImportSettingsFrom( *master );
    }

    pad->SetPosition( aOrigin );

    if( !applyPatch( pad.get(), patch, aOrigin ) || !padValid( pad.get() ) )
        return nullptr;

    return pad.release();
}

} // namespace padkind
} // namespace cc_e4

#endif // __EMSCRIPTEN__
