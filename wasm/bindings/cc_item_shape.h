// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e5 (fp-e4b, FP spec 7.2): the shape kinds shape, dimension, zone (rule
// areas and keepouts), image (a reference picture) and point. Header only, inline only,
// included by cc_e5_fpedit.inc alone, which registers the five rows once (cc_fp_items.h's
// table); it registers nothing itself.
//
// Each set follows its KiCad dialog's TransferDataFromWindow: DIALOG_SHAPE_PROPERTIES (its
// Validate refusals: a line, an arc or an unfilled closed shape needs a width; a rounded
// corner at most half the short side), DIALOG_DIMENSION_PROPERTIES::updateDimensionFromDialog
// (the same setters in the same order, then Update), DIALOG_RULE_AREA_PROPERTIES (the zone's
// settings read with ZONE_SETTINGS << zone, changed, and written back with ExportSetting as
// FOOTPRINT_EDIT_FRAME::OnEditItemRequest does; no layer refused, the hatch pitch in KiCad's
// bounds), DIALOG_REFERENCE_IMAGE_PROPERTIES (PANEL_IMAGE_EDITOR's scale check and gray
// conversion, the position, the layer, the lock outside the footprint editor) and the point's
// one property (Size; KiCad has no point dialog). Every set is tried on a clone first, so a
// refused patch changes nothing; a taken one calls aCommit.Modify( item ) before the change.
// Lengths in mm, angles in degrees, positions relative to the caller's origin, layers by
// canonical names.
//
// The records (a set carries only keys the item has; each add takes the same keys):
//   shape      layer, shape (segment, rect, arc, circle, polygon, bezier; fixed on a set),
//              points (segment and rect [start, end], arc [start, mid, end], circle [center,
//              a point on it], polygon its outline, bezier [start, c1, c2, end]), width, style
//              (solid, dash, dot, dash_dot, dash_dot_dot), filled, fill (none, solid, hatch,
//              reverse_hatch, cross_hatch), locked, cornerRadius (rect only), hasSolderMask,
//              solderMaskMargin (null for the board's).
//   dimension  dimType (aligned, orthogonal, center, radial, leader; fixed on a set), layer,
//              start, end, height and extensionHeight (aligned, orthogonal), orientation
//              (orthogonal: horizontal, vertical), leaderLength (radial), textBorder (leader:
//              none, rectangle, circle, roundrect), override, value, prefix, suffix,
//              arrowDirection (inward, outward), units (inch, mils, mm, auto), unitsFormat
//              (none, bare, paren), precision (0 to 9, KiCad's list), suppressZeroes,
//              textPosition (outside, inline, manual), textPos, keepAligned, angle, size,
//              thickness, bold, italic, hJustify, mirrored, font, lineThickness, arrowLength,
//              extensionOffset, locked.
//   zone       ruleArea (fixed; an add makes a rule area), name, layers, locked, points (the
//              main outline), borderStyle (none, edge, full), hatchPitch; on a rule area also
//              noTracks, noVias, noPads, noCopperPour, noFootprints, placement (bool),
//              placementSource (sheet, componentClass, group, designBlock), placementName.
//   image      x, y (its center), layer, scale, bytes (the PNG's size), png (base64, absent
//              past 64 kB; an add only, at most 4 MB), grayscale (a set only, true: the
//              editor's Convert to grayscale), locked (the board frame only, as the dialog).
//   point      x, y, layer, size, locked.

#pragma once

#ifdef __EMSCRIPTEN__

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <wx/base64.h>
#include <wx/image.h>
#include <wx/mstream.h>

#include <nlohmann/json.hpp>

#include <bitmap_base.h>
#include <board.h>
#include <board_design_settings.h>
#include <eda_shape.h>
#include <eda_text.h>
#include <font/font.h>
#include <footprint.h>
#include <geometry/shape_line_chain.h>
#include <geometry/shape_poly_set.h>
#include <pcb_dimension.h>
#include <pcb_point.h>
#include <pcb_reference_image.h>
#include <pcb_shape.h>
#include <reference_image.h>
#include <stroke_params.h>
#include <zone.h>
#include <zone_settings.h>
#include <zones.h>

#include "fpedit_common.h"

namespace cc_e5
{
namespace fp
{
namespace shapekind
{

using json = nlohmann::json;
using namespace cc_fpedit;

constexpr size_t IMAGE_INLINE_MAX = 64u * 1024u;        // png in a read up to this size
constexpr size_t IMAGE_ADD_MAX = 4u * 1024u * 1024u;    // a picture's PNG on an add
constexpr double IMAGE_SCALE_MAX = 1000.0;
constexpr int    IMAGE_MIN_PIXELS = 15;                 // PANEL_IMAGE_EDITOR's MIN_SIZE

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

inline const NAMED<SHAPE_T> SHAPES[] = { { "segment", SHAPE_T::SEGMENT },
                                         { "rect", SHAPE_T::RECTANGLE },
                                         { "arc", SHAPE_T::ARC },
                                         { "circle", SHAPE_T::CIRCLE },
                                         { "polygon", SHAPE_T::POLY },
                                         { "bezier", SHAPE_T::BEZIER } };

inline const NAMED<UI_FILL_MODE> FILLS[] = { { "none", UI_FILL_MODE::NONE },
                                             { "solid", UI_FILL_MODE::SOLID },
                                             { "hatch", UI_FILL_MODE::HATCH },
                                             { "reverse_hatch", UI_FILL_MODE::REVERSE_HATCH },
                                             { "cross_hatch", UI_FILL_MODE::CROSS_HATCH } };

inline const NAMED<LINE_STYLE> LINE_STYLES[] = { { "solid", LINE_STYLE::SOLID },
                                                 { "dash", LINE_STYLE::DASH },
                                                 { "dot", LINE_STYLE::DOT },
                                                 { "dash_dot", LINE_STYLE::DASHDOT },
                                                 { "dash_dot_dot", LINE_STYLE::DASHDOTDOT } };

inline const NAMED<KICAD_T> DIM_TYPES[] = { { "aligned", PCB_DIM_ALIGNED_T },
                                            { "orthogonal", PCB_DIM_ORTHOGONAL_T },
                                            { "center", PCB_DIM_CENTER_T },
                                            { "radial", PCB_DIM_RADIAL_T },
                                            { "leader", PCB_DIM_LEADER_T } };

inline const NAMED<DIM_ARROW_DIRECTION> ARROWS[] = { { "inward", DIM_ARROW_DIRECTION::INWARD },
                                                     { "outward", DIM_ARROW_DIRECTION::OUTWARD } };

inline const NAMED<DIM_UNITS_MODE> UNITS[] = { { "inch", DIM_UNITS_MODE::INCH },
                                               { "mils", DIM_UNITS_MODE::MILS },
                                               { "mm", DIM_UNITS_MODE::MM },
                                               { "auto", DIM_UNITS_MODE::AUTOMATIC } };

inline const NAMED<DIM_UNITS_FORMAT> UNITS_FORMATS[] = {
    { "none", DIM_UNITS_FORMAT::NO_SUFFIX },
    { "bare", DIM_UNITS_FORMAT::BARE_SUFFIX },
    { "paren", DIM_UNITS_FORMAT::PAREN_SUFFIX } };

inline const NAMED<DIM_TEXT_POSITION> TEXT_POSITIONS[] = {
    { "outside", DIM_TEXT_POSITION::OUTSIDE },
    { "inline", DIM_TEXT_POSITION::INLINE },
    { "manual", DIM_TEXT_POSITION::MANUAL } };

inline const NAMED<DIM_TEXT_BORDER> TEXT_BORDERS[] = { { "none", DIM_TEXT_BORDER::NONE },
                                                       { "rectangle", DIM_TEXT_BORDER::RECTANGLE },
                                                       { "circle", DIM_TEXT_BORDER::CIRCLE },
                                                       { "roundrect", DIM_TEXT_BORDER::ROUNDRECT } };

inline const NAMED<PCB_DIM_ORTHOGONAL::DIR> ORIENTATIONS[] = {
    { "horizontal", PCB_DIM_ORTHOGONAL::DIR::HORIZONTAL },
    { "vertical", PCB_DIM_ORTHOGONAL::DIR::VERTICAL } };

inline const NAMED<GR_TEXT_H_ALIGN_T> H_ALIGN[] = { { "left", GR_TEXT_H_ALIGN_LEFT },
                                                    { "center", GR_TEXT_H_ALIGN_CENTER },
                                                    { "right", GR_TEXT_H_ALIGN_RIGHT } };

// The rule area dialog's three outline choices (its m_OutlineDisplayCtrl order).
inline const NAMED<ZONE_BORDER_DISPLAY_STYLE> BORDER_STYLES[] = {
    { "none", ZONE_BORDER_DISPLAY_STYLE::NO_HATCH },
    { "edge", ZONE_BORDER_DISPLAY_STYLE::DIAGONAL_EDGE },
    { "full", ZONE_BORDER_DISPLAY_STYLE::DIAGONAL_FULL } };

inline const NAMED<PLACEMENT_SOURCE_T> PLACEMENT_SOURCES[] = {
    { "sheet", PLACEMENT_SOURCE_T::SHEETNAME },
    { "componentClass", PLACEMENT_SOURCE_T::COMPONENT_CLASS },
    { "group", PLACEMENT_SOURCE_T::GROUP_PLACEMENT },
    { "designBlock", PLACEMENT_SOURCE_T::DESIGN_BLOCK } };

inline const char* styleName( LINE_STYLE aStyle )
{
    return nameOf( LINE_STYLES, aStyle == LINE_STYLE::DEFAULT ? LINE_STYLE::SOLID : aStyle );
}

// ── the readers every kind shares ─────────────────────────────────────────────────────

inline std::optional<int> lengthOf( const json& aValue, double aMin, double aMax )
{
    std::optional<double> mm = numberOf( aValue, aMin, aMax );

    if( !mm )
        return std::nullopt;

    return iuOf( *mm );
}

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

inline json pairJson( const VECTOR2I& aValue )
{
    return json::array( { mmOf( aValue.x ), mmOf( aValue.y ) } );
}

/** A list of points relative to aOrigin, aMin to aMax of them; or nothing. */
inline std::optional<std::vector<VECTOR2I>> pointsOf( const json& aValue, size_t aMin,
                                                      size_t aMax, const VECTOR2I& aOrigin )
{
    if( !aValue.is_array() || aValue.size() < aMin || aValue.size() > aMax )
        return std::nullopt;

    std::vector<VECTOR2I> out;
    out.reserve( aValue.size() );

    for( const json& p : aValue )
    {
        std::optional<VECTOR2I> pt = pointOf( p, aOrigin );

        if( !pt )
            return std::nullopt;

        out.push_back( *pt );
    }

    return out;
}

/** A flag key: absent leaves aSlot empty; present must be a bool. */
inline bool flagOf( const json& aSet, const char* aKey, std::optional<bool>& aSlot )
{
    if( !aSet.contains( aKey ) )
        return true;

    aSlot = boolOf( aSet[aKey] );
    return aSlot.has_value();
}

/** A named key: absent leaves aSlot empty; present must be one of aTable's names. */
template <typename E, size_t N>
bool namedOf( const json& aSet, const char* aKey, const NAMED<E> ( &aTable )[N],
              std::optional<E>& aSlot )
{
    if( !aSet.contains( aKey ) )
        return true;

    aSlot = valueOf( aTable, aSet[aKey] );
    return aSlot.has_value();
}

/** A length key: absent leaves aSlot empty; present must be a number within the bounds. */
inline bool lengthKey( const json& aSet, const char* aKey, double aMin, double aMax,
                       std::optional<int>& aSlot )
{
    if( !aSet.contains( aKey ) )
        return true;

    aSlot = lengthOf( aSet[aKey], aMin, aMax );
    return aSlot.has_value();
}

/** Twice the signed area of the triangle a, b, c (zero when they are on one line). */
inline long long cross( const VECTOR2I& a, const VECTOR2I& b, const VECTOR2I& c )
{
    return (long long) ( b.x - a.x ) * ( c.y - a.y ) - (long long) ( b.y - a.y ) * ( c.x - a.x );
}

/** The parent a new item takes: the footprint aParent is or holds, else aParent itself. */
inline BOARD_ITEM* newParent( BOARD_ITEM_CONTAINER* aParent )
{
    if( FOOTPRINT* footprint = dynamic_cast<FOOTPRINT*>( aParent ) )
        return footprint;

    return dynamic_cast<BOARD_ITEM*>( aParent );
}

// ── shape (DIALOG_SHAPE_PROPERTIES) ───────────────────────────────────────────────────

inline json shapeJson( PCB_SHAPE* aShape, const VECTOR2I& aOrigin )
{
    json pts = json::array();

    switch( aShape->GetShape() )
    {
    case SHAPE_T::SEGMENT:
    case SHAPE_T::RECTANGLE:
        pts.push_back( pointJson( aShape->GetStart(), aOrigin ) );
        pts.push_back( pointJson( aShape->GetEnd(), aOrigin ) );
        break;

    case SHAPE_T::ARC:
        pts.push_back( pointJson( aShape->GetStart(), aOrigin ) );
        pts.push_back( pointJson( aShape->GetArcMid(), aOrigin ) );
        pts.push_back( pointJson( aShape->GetEnd(), aOrigin ) );
        break;

    case SHAPE_T::CIRCLE:
        pts.push_back( pointJson( aShape->GetCenter(), aOrigin ) );
        pts.push_back( pointJson( aShape->GetEnd(), aOrigin ) );
        break;

    case SHAPE_T::BEZIER:
        pts.push_back( pointJson( aShape->GetStart(), aOrigin ) );
        pts.push_back( pointJson( aShape->GetBezierC1(), aOrigin ) );
        pts.push_back( pointJson( aShape->GetBezierC2(), aOrigin ) );
        pts.push_back( pointJson( aShape->GetEnd(), aOrigin ) );
        break;

    case SHAPE_T::POLY:
        if( aShape->GetPolyShape().OutlineCount() > 0 )
        {
            const SHAPE_LINE_CHAIN& outline = aShape->GetPolyShape().COutline( 0 );

            for( int i = 0; i < outline.PointCount(); ++i )
                pts.push_back( pointJson( outline.CPoint( i ), aOrigin ) );
        }

        break;

    default:
        return json();
    }

    const std::optional<int> margin = aShape->GetLocalSolderMaskMargin();

    json out = json::object();
    out["layer"] = layerName( aShape->GetLayer() );
    out["shape"] = nameOf( SHAPES, aShape->GetShape() );
    out["points"] = std::move( pts );
    out["width"] = mmOf( aShape->GetWidth() );
    out["style"] = styleName( aShape->GetLineStyle() );
    out["filled"] = aShape->GetFillModeProp() != UI_FILL_MODE::NONE;
    out["fill"] = nameOf( FILLS, aShape->GetFillModeProp() );
    out["locked"] = aShape->IsLocked();

    if( aShape->GetShape() == SHAPE_T::RECTANGLE )
        out["cornerRadius"] = mmOf( aShape->GetCornerRadius() );

    out["hasSolderMask"] = aShape->HasSolderMask();
    out["solderMaskMargin"] = margin ? json( mmOf( *margin ) ) : json();
    return out;
}

inline bool applyShape( PCB_SHAPE* aShape, const json& aSet, const VECTOR2I& aOrigin )
{
    if( !onlyKeys( aSet, { "layer", "shape", "points", "width", "style", "filled", "fill",
                           "locked", "cornerRadius", "hasSolderMask", "solderMaskMargin" } ) )
    {
        return false;
    }

    const SHAPE_T kind = aShape->GetShape();

    std::optional<PCB_LAYER_ID>          layer;
    std::optional<SHAPE_T>               shape;
    std::optional<std::vector<VECTOR2I>> points;
    std::optional<int>                   width, cornerRadius;
    std::optional<LINE_STYLE>            style;
    std::optional<UI_FILL_MODE>          fill;
    std::optional<bool>                  filled, locked, hasMask;
    std::optional<std::optional<int>>    maskMargin;

    if( aSet.contains( "layer" ) && !( layer = layerOf( aSet["layer"] ) ) )
        return false;

    // The dialog edits a shape of one type: the type is fixed.
    if( !namedOf( aSet, "shape", SHAPES, shape ) || ( shape && *shape != kind ) )
        return false;

    if( aSet.contains( "points" ) )
    {
        size_t lo = 2, hi = 2;

        switch( kind )
        {
        case SHAPE_T::ARC:    lo = hi = 3; break;
        case SHAPE_T::BEZIER: lo = hi = 4; break;
        case SHAPE_T::POLY:   lo = 3; hi = POINTS_MAX; break;
        default:              break;
        }

        if( !( points = pointsOf( aSet["points"], lo, hi, aOrigin ) ) )
            return false;

        const std::vector<VECTOR2I>& p = *points;

        switch( kind )
        {
        case SHAPE_T::SEGMENT:
        case SHAPE_T::CIRCLE:
            if( p[0] == p[1] )
                return false;

            break;

        case SHAPE_T::RECTANGLE:
            if( p[0].x == p[1].x || p[0].y == p[1].y )
                return false;

            break;

        case SHAPE_T::ARC:
            if( p[0] == p[2] || cross( p[0], p[1], p[2] ) == 0 )
                return false;

            break;

        case SHAPE_T::POLY:
            // Only a polygon of one outline is replaced whole; one of several would lose some.
            if( aShape->GetPolyShape().OutlineCount() > 1 )
                return false;

            break;

        default:
            break;
        }
    }

    if( !lengthKey( aSet, "width", 0.0, SIZE_MAX_MM, width ) )
        return false;

    if( !namedOf( aSet, "style", LINE_STYLES, style ) || !namedOf( aSet, "fill", FILLS, fill ) )
        return false;

    if( !flagOf( aSet, "filled", filled ) || !flagOf( aSet, "locked", locked )
            || !flagOf( aSet, "hasSolderMask", hasMask ) )
    {
        return false;
    }

    // filled is fill as a yes or no: both given must agree.
    if( filled && fill && *filled != ( *fill != UI_FILL_MODE::NONE ) )
        return false;

    if( filled && !fill )
        fill = *filled ? UI_FILL_MODE::SOLID : UI_FILL_MODE::NONE;

    // A line and an arc are never filled (the dialog offers no fill for them).
    if( fill && *fill != UI_FILL_MODE::NONE
            && ( kind == SHAPE_T::SEGMENT || kind == SHAPE_T::ARC ) )
    {
        return false;
    }

    if( aSet.contains( "cornerRadius" )
            && ( kind != SHAPE_T::RECTANGLE
                 || !lengthKey( aSet, "cornerRadius", 0.0, SIZE_MAX_MM, cornerRadius ) ) )
    {
        return false;
    }

    if( aSet.contains( "solderMaskMargin" ) )
    {
        const json& m = aSet["solderMaskMargin"];

        if( m.is_null() )
        {
            maskMargin.emplace( std::nullopt );
        }
        else
        {
            std::optional<int> v = lengthOf( m, -SIZE_MAX_MM, SIZE_MAX_MM );

            if( !v )
                return false;

            maskMargin.emplace( *v );
        }
    }

    // The geometry first (the dialog's working copy), then its Validate on the final values.
    if( points )
    {
        const std::vector<VECTOR2I>& p = *points;

        switch( kind )
        {
        case SHAPE_T::SEGMENT:
        case SHAPE_T::RECTANGLE:
            aShape->SetStart( p[0] );
            aShape->SetEnd( p[1] );
            break;

        case SHAPE_T::ARC:
            aShape->SetArcGeometry( p[0], p[1], p[2] );
            break;

        case SHAPE_T::CIRCLE:
            aShape->SetCenter( p[0] );
            aShape->SetEnd( p[1] );
            break;

        case SHAPE_T::BEZIER:
            aShape->SetStart( p[0] );
            aShape->SetBezierC1( p[1] );
            aShape->SetBezierC2( p[2] );
            aShape->SetEnd( p[3] );
            break;

        case SHAPE_T::POLY:
            aShape->SetPolyPoints( p );
            break;

        default:
            return false;
        }
    }

    const UI_FILL_MODE finalFill = fill ? *fill : aShape->GetFillModeProp();
    const int          finalWidth = width ? *width : aShape->GetWidth();

    switch( kind )
    {
    case SHAPE_T::ARC:
    case SHAPE_T::SEGMENT:
        if( finalWidth <= 0 )
            return false;

        break;

    default:
        if( finalFill != UI_FILL_MODE::SOLID && finalWidth <= 0 )
            return false;

        break;
    }

    if( kind == SHAPE_T::RECTANGLE )
    {
        const int finalRadius = cornerRadius ? *cornerRadius : aShape->GetCornerRadius();
        const int shortSide = std::min( std::abs( aShape->GetRectangleWidth() ),
                                        std::abs( aShape->GetRectangleHeight() ) );

        if( finalRadius * 2 > shortSide )
            return false;

        if( cornerRadius )
            aShape->SetCornerRadius( *cornerRadius );
    }

    // TransferDataFromWindow's order.
    if( fill )
        aShape->SetFillModeProp( *fill );

    if( locked )
        aShape->SetLocked( *locked );

    if( width )
        aShape->SetWidth( *width );

    if( style )
        aShape->SetLineStyle( *style );

    if( layer )
        aShape->SetLayer( *layer );

    if( hasMask )
        aShape->SetHasSolderMask( *hasMask );

    if( maskMargin )
        aShape->SetLocalSolderMaskMargin( *maskMargin );

    aShape->RebuildBezierToSegmentsPointsList( aShape->GetMaxError() );

    if( !aShape->IsOnCopperLayer() )
        aShape->SetNetCode( -1 );

    return true;
}

/** A new shape: the drawing tool's (its layer, the layer's line width, the default style). */
inline BOARD_ITEM* addShape( BOARD_ITEM_CONTAINER* aParent, const json& aItem,
                             const VECTOR2I& aOrigin, bool )
{
    BOARD_ITEM* parent = newParent( aParent );

    if( !parent || !aItem.is_object() || !hasKeys( aItem, { "shape", "layer", "points" } ) )
        return nullptr;

    std::optional<SHAPE_T>      kind = valueOf( SHAPES, aItem["shape"] );
    std::optional<PCB_LAYER_ID> layer = layerOf( aItem["layer"] );

    if( !kind || !layer )
        return nullptr;

    std::unique_ptr<PCB_SHAPE> shape = std::make_unique<PCB_SHAPE>( parent, *kind );
    int                        width = pcbIUScale.mmToIU( 0.1 );

    if( const BOARD* board = parent->GetBoard() )
        width = board->GetDesignSettings().GetLineThickness( *layer );

    shape->SetStroke( STROKE_PARAMS( width, LINE_STYLE::DEFAULT ) );
    shape->SetFillModeProp( UI_FILL_MODE::NONE );
    shape->SetLayer( *layer );

    // A shape starts as a unit square or line, so a partial record still has a geometry.
    switch( *kind )
    {
    case SHAPE_T::ARC:
        shape->SetArcGeometry( aOrigin, aOrigin + VECTOR2I( width, width ),
                               aOrigin + VECTOR2I( 2 * width, 0 ) );
        break;

    case SHAPE_T::POLY:
        shape->SetPolyPoints( { aOrigin, aOrigin + VECTOR2I( width, 0 ),
                                aOrigin + VECTOR2I( 0, width ) } );
        break;

    default:
        shape->SetStart( aOrigin );
        shape->SetEnd( aOrigin + VECTOR2I( width, width ) );
        break;
    }

    if( !applyShape( shape.get(), aItem, aOrigin ) )
        return nullptr;

    return shape.release();
}

// ── dimension (DIALOG_DIMENSION_PROPERTIES) ───────────────────────────────────────────

inline std::string shownDimText( const BOARD_ITEM* aItem, const wxString& aText )
{
    wxString text = aText;

    if( const BOARD* board = aItem->GetBoard() )
        text = board->ConvertKIIDsToCrossReferences( text );

    return utf8( text );
}

inline wxString storedDimText( const BOARD_ITEM* aItem, const wxString& aText )
{
    if( const BOARD* board = aItem->GetBoard() )
        return board->ConvertCrossReferencesToKIIDs( aText );

    return aText;
}

inline json dimensionJson( PCB_DIMENSION_BASE* aDim, const VECTOR2I& aOrigin )
{
    const KICAD_T type = aDim->Type();

    json out = json::object();
    out["dimType"] = nameOf( DIM_TYPES, type );
    out["layer"] = layerName( aDim->GetLayer() );
    out["start"] = pointJson( aDim->GetStart(), aOrigin );
    out["end"] = pointJson( aDim->GetEnd(), aOrigin );

    if( PCB_DIM_ALIGNED* aligned = dynamic_cast<PCB_DIM_ALIGNED*>( aDim ) )
    {
        out["height"] = mmOf( aligned->GetHeight() );
        out["extensionHeight"] = mmOf( aligned->GetExtensionHeight() );
    }

    if( PCB_DIM_ORTHOGONAL* ortho = dynamic_cast<PCB_DIM_ORTHOGONAL*>( aDim ) )
        out["orientation"] = nameOf( ORIENTATIONS, ortho->GetOrientation() );

    if( PCB_DIM_RADIAL* radial = dynamic_cast<PCB_DIM_RADIAL*>( aDim ) )
        out["leaderLength"] = mmOf( radial->GetLeaderLength() );

    if( PCB_DIM_LEADER* leader = dynamic_cast<PCB_DIM_LEADER*>( aDim ) )
        out["textBorder"] = nameOf( TEXT_BORDERS, leader->GetTextBorder() );

    out["override"] = aDim->GetOverrideTextEnabled();
    out["value"] = shownDimText( aDim, aDim->GetOverrideText() );
    out["prefix"] = shownDimText( aDim, aDim->GetPrefix() );
    out["suffix"] = shownDimText( aDim, aDim->GetSuffix() );
    out["arrowDirection"] = nameOf( ARROWS, aDim->GetArrowDirection() );
    out["units"] = nameOf( UNITS, aDim->GetUnitsMode() );
    out["unitsFormat"] = nameOf( UNITS_FORMATS, aDim->GetUnitsFormat() );
    out["precision"] = static_cast<int>( aDim->GetPrecision() );
    out["suppressZeroes"] = aDim->GetSuppressZeroes();
    out["textPosition"] = nameOf( TEXT_POSITIONS, aDim->GetTextPositionMode() );
    out["textPos"] = pointJson( aDim->GetTextPos(), aOrigin );
    out["keepAligned"] = aDim->GetKeepTextAligned();
    out["angle"] = degreesOf( aDim->GetTextAngle() );
    out["size"] = pairJson( aDim->GetTextSize() );
    out["thickness"] = mmOf( aDim->GetTextThickness() );
    out["bold"] = aDim->IsBold();
    out["italic"] = aDim->IsItalic();
    out["hJustify"] = nameOf( H_ALIGN, aDim->GetHorizJustify() );
    out["mirrored"] = aDim->IsMirrored();
    out["font"] = utf8( aDim->GetFontProp() );
    out["lineThickness"] = mmOf( aDim->GetLineThickness() );
    out["arrowLength"] = mmOf( aDim->GetArrowLength() );
    out["extensionOffset"] = mmOf( aDim->GetExtensionOffset() );
    out["locked"] = aDim->IsLocked();
    return out;
}

inline bool applyDimension( PCB_DIMENSION_BASE* aDim, const json& aSet, const VECTOR2I& aOrigin )
{
    if( !onlyKeys( aSet, { "dimType", "layer", "start", "end", "height", "extensionHeight",
                           "orientation", "leaderLength", "textBorder", "override", "value",
                           "prefix", "suffix", "arrowDirection", "units", "unitsFormat",
                           "precision", "suppressZeroes", "textPosition", "textPos",
                           "keepAligned", "angle", "size", "thickness", "bold", "italic",
                           "hJustify", "mirrored", "font", "lineThickness", "arrowLength",
                           "extensionOffset", "locked" } ) )
    {
        return false;
    }

    PCB_DIM_ALIGNED*    aligned = dynamic_cast<PCB_DIM_ALIGNED*>( aDim );
    PCB_DIM_ORTHOGONAL* ortho = dynamic_cast<PCB_DIM_ORTHOGONAL*>( aDim );
    PCB_DIM_RADIAL*     radial = dynamic_cast<PCB_DIM_RADIAL*>( aDim );
    PCB_DIM_LEADER*     leader = dynamic_cast<PCB_DIM_LEADER*>( aDim );

    std::optional<KICAD_T>                   dimType;
    std::optional<PCB_LAYER_ID>              layer;
    std::optional<VECTOR2I>                  start, end, textPos, size;
    std::optional<int>                       height, extHeight, leaderLength, thickness;
    std::optional<int>                       lineThickness, arrowLength, extOffset, precision;
    std::optional<PCB_DIM_ORTHOGONAL::DIR>   orientation;
    std::optional<DIM_TEXT_BORDER>           textBorder;
    std::optional<DIM_ARROW_DIRECTION>       arrows;
    std::optional<DIM_UNITS_MODE>            units;
    std::optional<DIM_UNITS_FORMAT>          unitsFormat;
    std::optional<DIM_TEXT_POSITION>         textPosition;
    std::optional<GR_TEXT_H_ALIGN_T>         hJustify;
    std::optional<bool>                      overrideOn, suppress, keepAligned, bold, italic;
    std::optional<bool>                      mirrored, locked;
    std::optional<wxString>                  value, prefix, suffix, font;
    std::optional<double>                    angle;

    if( !namedOf( aSet, "dimType", DIM_TYPES, dimType ) || ( dimType && *dimType != aDim->Type() ) )
        return false;

    if( aSet.contains( "layer" ) && !( layer = layerOf( aSet["layer"] ) ) )
        return false;

    if( aSet.contains( "start" ) && !( start = pointOf( aSet["start"], aOrigin ) ) )
        return false;

    if( aSet.contains( "end" ) && !( end = pointOf( aSet["end"], aOrigin ) ) )
        return false;

    // The keys of one subtype are refused on another.
    if( ( aSet.contains( "height" ) || aSet.contains( "extensionHeight" ) ) && !aligned )
        return false;

    if( aSet.contains( "orientation" ) && !ortho )
        return false;

    if( aSet.contains( "leaderLength" ) && !radial )
        return false;

    if( aSet.contains( "textBorder" ) && !leader )
        return false;

    if( !lengthKey( aSet, "height", -POSITION_MAX_MM, POSITION_MAX_MM, height )
            || !lengthKey( aSet, "extensionHeight", 0.0, SIZE_MAX_MM, extHeight )
            || !lengthKey( aSet, "leaderLength", 0.0, SIZE_MAX_MM, leaderLength )
            || !lengthKey( aSet, "thickness", 0.0, TEXT_MAX_SIZE_MM, thickness )
            || !lengthKey( aSet, "lineThickness", 0.0, SIZE_MAX_MM, lineThickness )
            || !lengthKey( aSet, "arrowLength", 0.0, SIZE_MAX_MM, arrowLength )
            || !lengthKey( aSet, "extensionOffset", 0.0, SIZE_MAX_MM, extOffset ) )
    {
        return false;
    }

    if( !namedOf( aSet, "orientation", ORIENTATIONS, orientation )
            || !namedOf( aSet, "textBorder", TEXT_BORDERS, textBorder )
            || !namedOf( aSet, "arrowDirection", ARROWS, arrows )
            || !namedOf( aSet, "units", UNITS, units )
            || !namedOf( aSet, "unitsFormat", UNITS_FORMATS, unitsFormat )
            || !namedOf( aSet, "textPosition", TEXT_POSITIONS, textPosition )
            || !namedOf( aSet, "hJustify", H_ALIGN, hJustify ) )
    {
        return false;
    }

    if( aSet.contains( "precision" )
            && !( precision = intOf( aSet["precision"], 0,
                                     static_cast<int>( DIM_PRECISION::V_VVVVV ) ) ) )
    {
        return false;
    }

    if( !flagOf( aSet, "override", overrideOn ) || !flagOf( aSet, "suppressZeroes", suppress )
            || !flagOf( aSet, "keepAligned", keepAligned ) || !flagOf( aSet, "bold", bold )
            || !flagOf( aSet, "italic", italic ) || !flagOf( aSet, "mirrored", mirrored )
            || !flagOf( aSet, "locked", locked ) )
    {
        return false;
    }

    if( aSet.contains( "value" ) && !( value = textOf( aSet["value"], 0, TEXT_MAX ) ) )
        return false;

    if( aSet.contains( "prefix" ) && !( prefix = textOf( aSet["prefix"], 0, TEXT_MAX ) ) )
        return false;

    if( aSet.contains( "suffix" ) && !( suffix = textOf( aSet["suffix"], 0, TEXT_MAX ) ) )
        return false;

    if( aSet.contains( "font" ) && !( font = textOf( aSet["font"], 1, 256 ) ) )
        return false;

    if( aSet.contains( "angle" ) && !( angle = numberOf( aSet["angle"], -360.0, 360.0 ) ) )
        return false;

    if( aSet.contains( "size" )
            && !( size = pairOf( aSet["size"], TEXT_MIN_SIZE_MM, TEXT_MAX_SIZE_MM ) ) )
    {
        return false;
    }

    if( aSet.contains( "textPos" ) && !( textPos = pointOf( aSet["textPos"], aOrigin ) ) )
        return false;

    // The text is placed by hand only in the manual mode (a leader's text is always free).
    const DIM_TEXT_POSITION finalMode = textPosition ? *textPosition : aDim->GetTextPositionMode();

    if( textPos && !leader && finalMode != DIM_TEXT_POSITION::MANUAL )
        return false;

    // The geometry (the point editor's part), then updateDimensionFromDialog's order.
    if( start )
        aDim->SetStart( *start );

    if( end )
        aDim->SetEnd( *end );

    if( aligned && height )
        aligned->SetHeight( *height );

    if( ortho && orientation )
        ortho->SetOrientation( *orientation );

    if( radial && leaderLength )
        radial->SetLeaderLength( *leaderLength );

    if( overrideOn )
        aDim->SetOverrideTextEnabled( *overrideOn );

    if( value && aDim->GetOverrideTextEnabled() )
        aDim->SetOverrideText( storedDimText( aDim, *value ) );

    if( prefix )
        aDim->SetPrefix( storedDimText( aDim, *prefix ) );

    if( suffix )
        aDim->SetSuffix( storedDimText( aDim, *suffix ) );

    if( layer )
        aDim->SetLayer( *layer );

    if( arrows )
        aDim->SetArrowDirection( *arrows );

    if( units )
        aDim->SetUnitsMode( *units );

    if( unitsFormat )
        aDim->SetUnitsFormat( *unitsFormat );

    if( precision )
        aDim->SetPrecision( static_cast<DIM_PRECISION>( *precision ) );

    if( suppress )
        aDim->SetSuppressZeroes( *suppress );

    if( textPosition )
        aDim->SetTextPositionMode( *textPosition );

    if( textPos )
        aDim->SetTextPos( *textPos );

    if( keepAligned )
        aDim->SetKeepTextAligned( *keepAligned );

    if( angle )
        aDim->SetTextAngle( EDA_ANGLE( *angle, DEGREES_T ).Normalize() );

    if( size )
    {
        aDim->SetTextWidth( size->x );
        aDim->SetTextHeight( size->y );
    }

    if( thickness )
        aDim->SetTextThickness( *thickness );

    if( font )
    {
        aDim->SetFont( KIFONT::FONT::GetFont( *font, bold ? *bold : aDim->IsBold(),
                                              italic ? *italic : aDim->IsItalic() ) );
    }

    // Must come after SetTextWidth/Height (the dialog's own note).
    if( bold )
        aDim->SetBold( *bold );

    if( italic )
        aDim->SetItalic( *italic );

    if( hJustify )
        aDim->SetHorizJustify( *hJustify );

    if( mirrored )
        aDim->SetMirrored( *mirrored );

    if( lineThickness )
        aDim->SetLineThickness( *lineThickness );

    if( arrowLength )
        aDim->SetArrowLength( *arrowLength );

    if( extOffset )
        aDim->SetExtensionOffset( *extOffset );

    if( aligned && extHeight )
        aligned->SetExtensionHeight( *extHeight );

    if( leader && textBorder )
        leader->SetTextBorder( *textBorder );

    if( locked )
        aDim->SetLocked( *locked );

    aDim->Update();
    return true;
}

/** A new dimension: DrawDimension's attributes for its type on its layer, then the record. */
inline BOARD_ITEM* addDimension( BOARD_ITEM_CONTAINER* aParent, const json& aItem,
                                 const VECTOR2I& aOrigin, bool )
{
    BOARD_ITEM* parent = newParent( aParent );

    if( !parent || !aItem.is_object()
            || !hasKeys( aItem, { "dimType", "layer", "start", "end" } ) )
    {
        return nullptr;
    }

    std::optional<KICAD_T>      type = valueOf( DIM_TYPES, aItem["dimType"] );
    std::optional<PCB_LAYER_ID> layer = layerOf( aItem["layer"] );
    std::optional<VECTOR2I>     start = pointOf( aItem["start"], aOrigin );

    if( !type || !layer || !start )
        return nullptr;

    std::unique_ptr<PCB_DIMENSION_BASE> dim;

    switch( *type )
    {
    case PCB_DIM_ALIGNED_T:    dim = std::make_unique<PCB_DIM_ALIGNED>( parent ); break;
    case PCB_DIM_ORTHOGONAL_T: dim = std::make_unique<PCB_DIM_ORTHOGONAL>( parent ); break;
    case PCB_DIM_CENTER_T:     dim = std::make_unique<PCB_DIM_CENTER>( parent ); break;
    case PCB_DIM_RADIAL_T:     dim = std::make_unique<PCB_DIM_RADIAL>( parent ); break;
    case PCB_DIM_LEADER_T:     dim = std::make_unique<PCB_DIM_LEADER>( parent ); break;
    default:                   return nullptr;
    }

    if( const BOARD* board = parent->GetBoard() )
    {
        const BOARD_DESIGN_SETTINGS& bds = board->GetDesignSettings();

        if( *type != PCB_DIM_CENTER_T && *type != PCB_DIM_LEADER_T )
        {
            dim->SetUnitsMode( bds.m_DimensionUnitsMode );
            dim->SetUnitsFormat( bds.m_DimensionUnitsFormat );
            dim->SetPrecision( bds.m_DimensionPrecision );
            dim->SetSuppressZeroes( bds.m_DimensionSuppressZeroes );
            dim->SetTextPositionMode( bds.m_DimensionTextPosition );
            dim->SetKeepTextAligned( bds.m_DimensionKeepTextAligned );
        }

        dim->SetMirrored( board->IsBackLayer( *layer ) );
        dim->SetTextSize( bds.GetTextSize( *layer ) );
        dim->SetTextThickness( bds.GetTextThickness( *layer ) );
        dim->SetItalic( bds.GetTextItalic( *layer ) );
        dim->SetLineThickness( bds.GetLineThickness( *layer ) );
        dim->SetArrowLength( bds.m_DimensionArrowLength );
        dim->SetExtensionOffset( bds.m_DimensionExtensionOffset );
    }

    if( *type == PCB_DIM_LEADER_T )
        dim->SetTextPos( *start );

    dim->SetLayer( *layer );
    dim->SetStart( *start );
    dim->SetEnd( *start );

    if( !applyDimension( dim.get(), aItem, aOrigin ) || dim->GetStart() == dim->GetEnd() )
        return nullptr;

    return dim.release();
}

// ── zone: rule areas and keepouts (DIALOG_RULE_AREA_PROPERTIES) ───────────────────────

inline json zoneJson( ZONE* aZone, const VECTOR2I& aOrigin )
{
    ZONE_SETTINGS zs;
    zs << *aZone;

    json pts = json::array();

    if( aZone->Outline() && aZone->Outline()->OutlineCount() > 0 )
    {
        const SHAPE_LINE_CHAIN& outline = aZone->Outline()->COutline( 0 );

        for( int i = 0; i < outline.PointCount(); ++i )
            pts.push_back( pointJson( outline.CPoint( i ), aOrigin ) );
    }

    json out = json::object();
    out["ruleArea"] = aZone->GetIsRuleArea();
    out["name"] = utf8( aZone->GetZoneName() );
    out["layers"] = layersJson( aZone->GetLayerSet() );
    out["locked"] = aZone->IsLocked();
    out["points"] = std::move( pts );
    out["borderStyle"] = nameOf( BORDER_STYLES, aZone->GetHatchStyle() );
    out["hatchPitch"] = mmOf( aZone->GetBorderHatchPitch() );

    if( aZone->GetIsRuleArea() )
    {
        out["noTracks"] = zs.GetDoNotAllowTracks();
        out["noVias"] = zs.GetDoNotAllowVias();
        out["noPads"] = zs.GetDoNotAllowPads();
        out["noCopperPour"] = zs.GetDoNotAllowZoneFills();
        out["noFootprints"] = zs.GetDoNotAllowFootprints();
        out["placement"] = zs.GetPlacementAreaEnabled();
        out["placementSource"] = nameOf( PLACEMENT_SOURCES, zs.GetPlacementAreaSourceType() );
        out["placementName"] = utf8( zs.GetPlacementAreaSource() );
    }

    return out;
}

/**
 * The patch onto aZone: its settings as the dialog edits them (ZONE_SETTINGS << zone, the
 * keys, ExportSetting as FOOTPRINT_EDIT_FRAME::OnEditItemRequest does), the outline first.
 */
inline bool applyZone( ZONE* aZone, const json& aSet, const VECTOR2I& aOrigin )
{
    if( !onlyKeys( aSet, { "ruleArea", "name", "layers", "locked", "points", "borderStyle",
                           "hatchPitch", "noTracks", "noVias", "noPads", "noCopperPour",
                           "noFootprints", "placement", "placementSource",
                           "placementName" } ) )
    {
        return false;
    }

    const bool rule = aZone->GetIsRuleArea();

    std::optional<bool>                      ruleArea, locked, noTracks, noVias, noPads;
    std::optional<bool>                      noPour, noFootprints, placement;
    std::optional<wxString>                  name, placementName;
    std::optional<LSET>                      layers;
    std::optional<std::vector<VECTOR2I>>     points;
    std::optional<ZONE_BORDER_DISPLAY_STYLE> border;
    std::optional<int>                       pitch;
    std::optional<PLACEMENT_SOURCE_T>        source;

    // A zone stays the kind it is.
    if( !flagOf( aSet, "ruleArea", ruleArea ) || ( ruleArea && *ruleArea != rule ) )
        return false;

    // The rule area's own keys are refused on a filled zone.
    for( const char* k : { "noTracks", "noVias", "noPads", "noCopperPour", "noFootprints",
                           "placement", "placementSource", "placementName" } )
    {
        if( aSet.contains( k ) && !rule )
            return false;
    }

    if( !flagOf( aSet, "locked", locked ) || !flagOf( aSet, "noTracks", noTracks )
            || !flagOf( aSet, "noVias", noVias ) || !flagOf( aSet, "noPads", noPads )
            || !flagOf( aSet, "noCopperPour", noPour )
            || !flagOf( aSet, "noFootprints", noFootprints )
            || !flagOf( aSet, "placement", placement ) )
    {
        return false;
    }

    if( aSet.contains( "name" ) && !( name = textOf( aSet["name"], 0, TEXT_MAX ) ) )
        return false;

    if( aSet.contains( "placementName" )
            && !( placementName = textOf( aSet["placementName"], 0, TEXT_MAX ) ) )
    {
        return false;
    }

    // "No layers selected." is the dialog's refusal.
    if( aSet.contains( "layers" ) && !( layers = layersOf( aSet["layers"], 1 ) ) )
        return false;

    if( !namedOf( aSet, "borderStyle", BORDER_STYLES, border )
            || !namedOf( aSet, "placementSource", PLACEMENT_SOURCES, source ) )
    {
        return false;
    }

    if( !lengthKey( aSet, "hatchPitch", ZONE_BORDER_HATCH_MINDIST_MM,
                    ZONE_BORDER_HATCH_MAXDIST_MM, pitch ) )
    {
        return false;
    }

    if( aSet.contains( "points" ) )
    {
        // Only a zone of one outline and no hole is replaced whole.
        const SHAPE_POLY_SET* poly = aZone->Outline();

        if( poly && ( poly->OutlineCount() > 1
                      || ( poly->OutlineCount() == 1 && poly->HoleCount( 0 ) > 0 ) ) )
        {
            return false;
        }

        if( !( points = pointsOf( aSet["points"], 3, POINTS_MAX, aOrigin ) ) )
            return false;
    }

    ZONE_SETTINGS zs;
    zs << *aZone;

    if( rule )
    {
        if( noTracks )
            zs.SetDoNotAllowTracks( *noTracks );

        if( noVias )
            zs.SetDoNotAllowVias( *noVias );

        if( noPour )
            zs.SetDoNotAllowZoneFills( *noPour );

        if( noPads )
            zs.SetDoNotAllowPads( *noPads );

        if( noFootprints )
            zs.SetDoNotAllowFootprints( *noFootprints );

        if( placement )
            zs.SetPlacementAreaEnabled( *placement );

        if( source )
            zs.SetPlacementAreaSourceType( *source );

        if( placementName )
            zs.SetPlacementAreaSource( *placementName );

        zs.m_ZonePriority = 0;   // for a keepout, this param is not used (the dialog's line)
    }

    if( layers )
        zs.m_Layers = *layers;

    if( border )
        zs.m_ZoneBorderDisplayStyle = *border;

    if( pitch )
        zs.m_BorderHatchPitch = *pitch;

    if( locked )
        zs.m_Locked = *locked;

    if( name )
        zs.m_Name = *name;

    if( points )
    {
        SHAPE_LINE_CHAIN chain;

        for( const VECTOR2I& p : *points )
            chain.Append( p );

        chain.SetClosed( true );
        aZone->Outline()->RemoveAllContours();
        aZone->Outline()->AddOutline( chain );
    }

    zs.ExportSetting( *aZone );
    return true;
}

/** A new rule area: ZONE_CREATE_HELPER::createNewZone's settings, then the record. */
inline BOARD_ITEM* addZone( BOARD_ITEM_CONTAINER* aParent, const json& aItem,
                            const VECTOR2I& aOrigin, bool )
{
    BOARD_ITEM_CONTAINER* parent = aParent;

    if( !parent || !aItem.is_object() || !hasKeys( aItem, { "layers", "points" } ) )
        return nullptr;

    std::optional<LSET> layers = layersOf( aItem["layers"], 1 );

    if( !layers || ( aItem.contains( "ruleArea" ) && aItem["ruleArea"] != json( true ) ) )
        return nullptr;

    BOARD_ITEM* parentItem = dynamic_cast<BOARD_ITEM*>( parent );
    const BOARD* board = parentItem ? parentItem->GetBoard() : nullptr;

    ZONE_SETTINGS zi = board ? board->GetDesignSettings().GetDefaultZoneSettings()
                             : ZONE_SETTINGS::GetDefaultSettings();
    zi.m_Layers = *layers;
    zi.m_LayerProperties.clear();
    zi.m_Netcode = -1;
    zi.SetIsRuleArea( true );

    std::unique_ptr<ZONE> zone = std::make_unique<ZONE>( parent );
    zi.ExportSetting( *zone );

    if( !applyZone( zone.get(), aItem, aOrigin ) )
        return nullptr;

    return zone.release();
}

// ── image (DIALOG_REFERENCE_IMAGE_PROPERTIES) ─────────────────────────────────────────

/** The picture as PNG bytes (its stored data when it is a PNG, else re-encoded). */
inline bool pngOf( const BITMAP_BASE& aBitmap, wxMemoryOutputStream& aOut )
{
    if( aBitmap.GetImageType() == wxBITMAP_TYPE_PNG )
        return aBitmap.SaveImageData( aOut );

    const wxImage* image = aBitmap.GetImageData();
    return image && image->IsOk() && image->SaveFile( aOut, wxBITMAP_TYPE_PNG );
}

inline json imageJson( PCB_REFERENCE_IMAGE* aImage, const VECTOR2I& aOrigin, bool aBoardFrame )
{
    const BITMAP_BASE& bitmap = aImage->GetReferenceImage().GetImage();
    const VECTOR2I     pos = aImage->GetPosition();

    json out = json::object();
    out["x"] = mmOf( (long long) pos.x - aOrigin.x );
    out["y"] = mmOf( (long long) pos.y - aOrigin.y );
    out["layer"] = layerName( aImage->GetLayer() );
    out["scale"] = std::round( bitmap.GetScale() * 1e6 ) / 1e6;

    wxMemoryOutputStream png;
    const bool           ok = pngOf( bitmap, png );
    const size_t         bytes = ok ? (size_t) png.GetLength() : 0;

    out["bytes"] = bytes;

    if( ok && bytes <= IMAGE_INLINE_MAX )
    {
        std::vector<unsigned char> data( bytes );
        png.CopyTo( data.data(), bytes );
        out["png"] = utf8( wxBase64Encode( data.data(), bytes ) );
    }

    if( aBoardFrame )
        out["locked"] = aImage->IsLocked();

    return out;
}

inline bool applyImage( PCB_REFERENCE_IMAGE* aImage, const json& aSet, const VECTOR2I& aOrigin,
                        bool aBoardFrame, bool aIsNew )
{
    // png is an add's key; bytes is read only.
    if( !onlyKeys( aSet, { "x", "y", "layer", "scale", "grayscale", "locked", "png" } )
            || ( aSet.contains( "png" ) && !aIsNew ) )
    {
        return false;
    }

    // The lock is the board frame's (the dialog hides it in the footprint editor).
    if( aSet.contains( "locked" ) && !aBoardFrame )
        return false;

    std::optional<double>       x, y, scale;
    std::optional<PCB_LAYER_ID> layer;
    std::optional<bool>         gray, locked;

    if( aSet.contains( "x" ) && !( x = numberOf( aSet["x"], -POSITION_MAX_MM, POSITION_MAX_MM ) ) )
        return false;

    if( aSet.contains( "y" ) && !( y = numberOf( aSet["y"], -POSITION_MAX_MM, POSITION_MAX_MM ) ) )
        return false;

    if( aSet.contains( "layer" ) && !( layer = layerOf( aSet["layer"] ) ) )
        return false;

    if( aSet.contains( "scale" )
            && ( !( scale = numberOf( aSet["scale"], 0.0, IMAGE_SCALE_MAX ) ) || *scale <= 0.0 ) )
    {
        return false;
    }

    // Convert to grayscale is a press: only true is a request.
    if( !flagOf( aSet, "grayscale", gray ) || ( gray && !*gray ) )
        return false;

    if( !flagOf( aSet, "locked", locked ) )
        return false;

    BITMAP_BASE& bitmap = aImage->GetReferenceImage().MutableImage();

    // PANEL_IMAGE_EDITOR::CheckValues: the scaled picture is at least MIN_SIZE pixels a side.
    // (Its question past MAX_SIZE is the page's to ask; the page's set is the answer.)
    if( scale )
    {
        const VECTOR2I px = bitmap.GetSizePixels();

        if( (int) std::min( px.x * *scale, px.y * *scale ) < IMAGE_MIN_PIXELS )
            return false;
    }

    if( gray )
        bitmap.ConvertToGreyscale();

    if( scale )
        bitmap.SetScale( *scale );

    if( x || y )
    {
        const VECTOR2I pos = aImage->GetPosition();
        aImage->SetPosition( VECTOR2I( x ? iuOf( *x ) + aOrigin.x : pos.x,
                                       y ? iuOf( *y ) + aOrigin.y : pos.y ) );
    }

    if( layer )
        aImage->SetLayer( *layer );

    if( locked )
        aImage->SetLocked( *locked );

    return true;
}

/** A new picture: PlaceReferenceImage's (the PNG read, at the origin, on its layer). */
inline BOARD_ITEM* addImage( BOARD_ITEM_CONTAINER* aParent, const json& aItem,
                             const VECTOR2I& aOrigin, bool aBoardFrame )
{
    BOARD_ITEM* parent = newParent( aParent );

    if( !parent || !aItem.is_object() || !hasKeys( aItem, { "layer", "png" } )
            || !aItem["png"].is_string() )
    {
        return nullptr;
    }

    const std::string& text = aItem["png"].get_ref<const std::string&>();

    if( text.empty() || text.size() > wxBase64EncodedSize( IMAGE_ADD_MAX ) )
        return nullptr;

    size_t         posErr = 0;
    wxMemoryBuffer decoded = wxBase64Decode( text.c_str(), text.size(), wxBase64DecodeMode_Strict,
                                             &posErr );
    static const unsigned char kPngMagic[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };

    if( decoded.GetDataLen() < sizeof( kPngMagic ) || decoded.GetDataLen() > IMAGE_ADD_MAX
            || std::memcmp( decoded.GetData(), kPngMagic, sizeof( kPngMagic ) ) != 0 )
    {
        return nullptr;
    }

    // BITMAP_BASE::ReadImageFile( wxMemoryBuffer& ) reads GetBufSize() bytes: exact size.
    const size_t   len = decoded.GetDataLen();
    wxMemoryBuffer exact( len );
    std::memcpy( exact.GetWriteBuf( len ), decoded.GetData(), len );
    exact.UngetWriteBuf( len );

    std::unique_ptr<PCB_REFERENCE_IMAGE> image =
            std::make_unique<PCB_REFERENCE_IMAGE>( parent, aOrigin );

    if( exact.GetBufSize() != len || !image->GetReferenceImage().ReadImageFile( exact ) )
        return nullptr;

    if( !applyImage( image.get(), aItem, aOrigin, aBoardFrame, /*aIsNew*/ true ) )
        return nullptr;

    return image.release();
}

// ── point (PCB_POINT: its Size property; KiCad has no dialog) ─────────────────────────

inline json pointItemJson( PCB_POINT* aPoint, const VECTOR2I& aOrigin )
{
    const VECTOR2I pos = aPoint->GetPosition();

    json out = json::object();
    out["x"] = mmOf( (long long) pos.x - aOrigin.x );
    out["y"] = mmOf( (long long) pos.y - aOrigin.y );
    out["layer"] = layerName( aPoint->GetLayer() );
    out["size"] = mmOf( aPoint->GetSize() );
    out["locked"] = aPoint->IsLocked();
    return out;
}

inline bool applyPoint( PCB_POINT* aPoint, const json& aSet, const VECTOR2I& aOrigin )
{
    if( !onlyKeys( aSet, { "x", "y", "layer", "size", "locked" } ) )
        return false;

    std::optional<double>       x, y;
    std::optional<int>          size;
    std::optional<PCB_LAYER_ID> layer;
    std::optional<bool>         locked;

    if( aSet.contains( "x" ) && !( x = numberOf( aSet["x"], -POSITION_MAX_MM, POSITION_MAX_MM ) ) )
        return false;

    if( aSet.contains( "y" ) && !( y = numberOf( aSet["y"], -POSITION_MAX_MM, POSITION_MAX_MM ) ) )
        return false;

    if( aSet.contains( "layer" ) && !( layer = layerOf( aSet["layer"] ) ) )
        return false;

    if( !lengthKey( aSet, "size", 0.0, SIZE_MAX_MM, size ) || ( size && *size <= 0 ) )
        return false;

    if( !flagOf( aSet, "locked", locked ) )
        return false;

    if( x || y )
    {
        const VECTOR2I pos = aPoint->GetPosition();
        aPoint->SetPosition( VECTOR2I( x ? iuOf( *x ) + aOrigin.x : pos.x,
                                       y ? iuOf( *y ) + aOrigin.y : pos.y ) );
    }

    if( layer )
        aPoint->SetLayer( *layer );

    if( size )
        aPoint->SetSize( *size );

    if( locked )
        aPoint->SetLocked( *locked );

    return true;
}

/** A new point: POINT_PLACER::CreateItem's (KiCad's default size, its layer). */
inline BOARD_ITEM* addPoint( BOARD_ITEM_CONTAINER* aParent, const json& aItem,
                             const VECTOR2I& aOrigin, bool )
{
    BOARD_ITEM* parent = newParent( aParent );

    if( !parent || !aItem.is_object() || !hasKeys( aItem, { "layer" } ) )
        return nullptr;

    std::optional<PCB_LAYER_ID> layer = layerOf( aItem["layer"] );

    if( !layer )
        return nullptr;

    std::unique_ptr<PCB_POINT> point = std::make_unique<PCB_POINT>( parent );
    point->SetLayer( *layer );
    point->SetPosition( aOrigin );

    if( !applyPoint( point.get(), aItem, aOrigin ) )
        return nullptr;

    return point.release();
}

// ── the five rows' bodies (cc_fp_items.h's contract) ──────────────────────────────────

inline bool isDimension( const BOARD_ITEM* aItem )
{
    switch( aItem->Type() )
    {
    case PCB_DIM_ALIGNED_T:
    case PCB_DIM_ORTHOGONAL_T:
    case PCB_DIM_CENTER_T:
    case PCB_DIM_RADIAL_T:
    case PCB_DIM_LEADER_T:     return true;
    default:                   return false;
    }
}

/** Any of the five: a set (aIsNew false) or the record of an add on its new item. */
inline bool applyAny( BOARD_ITEM* aItem, const json& aSet, const VECTOR2I& aOrigin,
                      bool aBoardFrame, bool aIsNew )
{
    if( isDimension( aItem ) )
        return applyDimension( static_cast<PCB_DIMENSION_BASE*>( aItem ), aSet, aOrigin );

    switch( aItem->Type() )
    {
    case PCB_SHAPE_T: return applyShape( static_cast<PCB_SHAPE*>( aItem ), aSet, aOrigin );
    case PCB_ZONE_T:  return applyZone( static_cast<ZONE*>( aItem ), aSet, aOrigin );
    case PCB_POINT_T: return applyPoint( static_cast<PCB_POINT*>( aItem ), aSet, aOrigin );
    case PCB_REFERENCE_IMAGE_T:
        return applyImage( static_cast<PCB_REFERENCE_IMAGE*>( aItem ), aSet, aOrigin,
                           aBoardFrame, aIsNew );
    default:          return false;
    }
}

inline json kindGet( BOARD_ITEM* aItem, const VECTOR2I& aOrigin, bool aBoardFrame )
{
    if( !aItem )
        return json();

    if( isDimension( aItem ) )
        return dimensionJson( static_cast<PCB_DIMENSION_BASE*>( aItem ), aOrigin );

    switch( aItem->Type() )
    {
    case PCB_SHAPE_T: return shapeJson( static_cast<PCB_SHAPE*>( aItem ), aOrigin );
    case PCB_ZONE_T:  return zoneJson( static_cast<ZONE*>( aItem ), aOrigin );
    case PCB_POINT_T: return pointItemJson( static_cast<PCB_POINT*>( aItem ), aOrigin );
    case PCB_REFERENCE_IMAGE_T:
        return imageJson( static_cast<PCB_REFERENCE_IMAGE*>( aItem ), aOrigin, aBoardFrame );
    default:          return json();
    }
}

/** The patch is tried on a clone first, so a refusal stages and changes nothing. */
inline bool kindSet( BOARD_ITEM* aItem, const json& aSet, BOARD_COMMIT& aCommit,
                     const VECTOR2I& aOrigin, bool aBoardFrame )
{
    if( !aItem || !aSet.is_object() )
        return false;

    {
        std::unique_ptr<BOARD_ITEM> probe( static_cast<BOARD_ITEM*>( aItem->Clone() ) );

        if( !probe || !applyAny( probe.get(), aSet, aOrigin, aBoardFrame, /*aIsNew*/ false ) )
            return false;
    }

    aCommit.Modify( aItem );
    return applyAny( aItem, aSet, aOrigin, aBoardFrame, /*aIsNew*/ false );
}

} // namespace shapekind
} // namespace fp
} // namespace cc_e5

#endif // __EMSCRIPTEN__
