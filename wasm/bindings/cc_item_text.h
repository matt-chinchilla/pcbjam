// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e4 (fp-e4a, FP spec 7.2): the text kinds field, text, textbox, table and
// barcode. Header only, inline only, included by cc_e4_fpedit.inc alone, which registers the
// five rows once (cc_fp_items.h's table); it registers nothing itself.
//
// Each set follows its KiCad dialog's TransferDataFromWindow (DIALOG_TEXT_PROPERTIES for field
// and text, DIALOG_TEXTBOX_PROPERTIES, DIALOG_TABLE_PROPERTIES, DIALOG_BARCODE_PROPERTIES):
// the same setters in the same order, the same bounds (text size TEXT_MIN_SIZE_MM to
// TEXT_MAX_SIZE_MM, the pen clamped by ClampTextPenSize), the same text conversions (cross
// references to and from KIIDs, a multi line text escaped as the dialog escapes it), the same
// refusals (an empty text keeps nothing: refused; a barcode KiCad cannot encode). Lengths in mm,
// angles in degrees, positions relative to the caller's origin, layers by canonical names.
//
// The records (the keys each item has; a set carries only keys the item has):
//   field, text  name (read only here: a field's name, "" for a text), text, layer, x, y, angle,
//                size, thickness, autoThickness, bold, italic, mirrored, hJustify (left,
//                center, right), vJustify (top, center, bottom), knockout, font; visible (a
//                field), keepUpright (inside a footprint), locked (outside one: the dialog
//                shows it only there).
//   textbox      text, layer, points (a cardinal box [start, end], else its four corners),
//                angle, size, thickness, autoThickness, bold, italic, mirrored, hJustify,
//                vJustify, font, locked, border, borderWidth, borderStyle.
//   table        x, y, layer, locked, rows and cols and colWidths and rowHeights (geometry,
//                read only on a set), cells (rows of texts as the dialog's grid shows them, a
//                back layer table mirrored, a covered cell null), borderExternal, borderHeader,
//                borderWidth, borderStyle, rowSeparators, colSeparators, separatorsWidth,
//                separatorsStyle (styles solid, dash, dot, dash_dot, dash_dot_dot; a width null
//                when its strokes are off).
//   barcode      text, layer, locked, x, y, width, height, textSize, angle, knockout, margin,
//                showText, kind (code39, code128, datamatrix, qr, microqr), errorCorrection
//                (L, M, Q, H).

#pragma once

#ifdef __EMSCRIPTEN__

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <board.h>
#include <board_design_settings.h>
#include <eda_text.h>
#include <footprint.h>
#include <gr_text.h>
#include <pcb_barcode.h>
#include <pcb_field.h>
#include <pcb_table.h>
#include <pcb_tablecell.h>
#include <pcb_text.h>
#include <pcb_textbox.h>
#include <stroke_params.h>
#include <string_utils.h>

#include "fpedit_common.h"

namespace cc_e4
{
namespace textkind
{

using json = nlohmann::json;
using namespace cc_fpedit;

constexpr size_t LONG_TEXT_MAX = 65536;   // a multi line text, a text box, a table cell
constexpr int    TABLE_SPAN_MAX = 64;     // rows or columns of a new table

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

inline const NAMED<GR_TEXT_H_ALIGN_T> H_ALIGN[] = { { "left", GR_TEXT_H_ALIGN_LEFT },
                                                    { "center", GR_TEXT_H_ALIGN_CENTER },
                                                    { "right", GR_TEXT_H_ALIGN_RIGHT } };

inline const NAMED<GR_TEXT_V_ALIGN_T> V_ALIGN[] = { { "top", GR_TEXT_V_ALIGN_TOP },
                                                    { "center", GR_TEXT_V_ALIGN_CENTER },
                                                    { "bottom", GR_TEXT_V_ALIGN_BOTTOM } };

inline const NAMED<LINE_STYLE> LINE_STYLES[] = { { "solid", LINE_STYLE::SOLID },
                                                 { "dash", LINE_STYLE::DASH },
                                                 { "dot", LINE_STYLE::DOT },
                                                 { "dash_dot", LINE_STYLE::DASHDOT },
                                                 { "dash_dot_dot", LINE_STYLE::DASHDOTDOT } };

inline const NAMED<BARCODE_T> BARCODE_KINDS[] = { { "code39", BARCODE_T::CODE_39 },
                                                  { "code128", BARCODE_T::CODE_128 },
                                                  { "datamatrix", BARCODE_T::DATA_MATRIX },
                                                  { "qr", BARCODE_T::QR_CODE },
                                                  { "microqr", BARCODE_T::MICRO_QR_CODE } };

inline const NAMED<BARCODE_ECC_T> ECC_LEVELS[] = { { "L", BARCODE_ECC_T::L },
                                                   { "M", BARCODE_ECC_T::M },
                                                   { "Q", BARCODE_ECC_T::Q },
                                                   { "H", BARCODE_ECC_T::H } };

inline const char* styleName( LINE_STYLE aStyle )
{
    return nameOf( LINE_STYLES, aStyle == LINE_STYLE::DEFAULT ? LINE_STYLE::SOLID : aStyle );
}

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

/** A length that equals aIu as the record shows it (a read only key given back unchanged). */
inline bool sameLength( const json& aValue, int aIu )
{
    return aValue.is_number() && std::fabs( aValue.get<double>() - mmOf( aIu ) ) < 1e-6;
}

// ── the text as the dialogs show it and take it ───────────────────────────────────────

/** The dialog's text box content: KIIDs as cross references, escapes undone. */
inline std::string shownText( const BOARD_ITEM* aItem, const wxString& aText )
{
    wxString text = UnescapeString( aText );

    if( const BOARD* board = aItem->GetBoard() )
        text = board->ConvertKIIDsToCrossReferences( text );

    return utf8( text );
}

/** The dialog's way back: cross references as KIIDs, a multi line box escaped as it is. */
inline wxString storedText( const BOARD_ITEM* aItem, const wxString& aText, bool aMultiLine )
{
    wxString text = aText;

    if( const BOARD* board = aItem->GetBoard() )
        text = board->ConvertCrossReferencesToKIIDs( text );

    return aMultiLine ? EscapeString( text, CTX_QUOTED_STR ) : text;
}

// ── the EDA_TEXT keys every text kind shares ──────────────────────────────────────────

struct TEXT_PATCH
{
    std::optional<wxString>           text;
    std::optional<PCB_LAYER_ID>       layer;
    std::optional<double>             angle;
    std::optional<VECTOR2I>           size;
    std::optional<int>                thickness;
    std::optional<bool>               autoThickness, bold, italic, mirrored, knockout;
    std::optional<GR_TEXT_H_ALIGN_T>  hJustify;
    std::optional<GR_TEXT_V_ALIGN_T>  vJustify;
    std::optional<wxString>           font;
};

inline json textAttrsJson( const EDA_TEXT& aText )
{
    json out = json::object();
    out["angle"] = degreesOf( aText.GetTextAngle() );
    out["size"] = pairJson( aText.GetTextSize() );
    out["thickness"] = mmOf( aText.GetAutoThickness() ? aText.GetEffectiveTextPenWidth()
                                                      : aText.GetTextThickness() );
    out["autoThickness"] = aText.GetAutoThickness();
    out["bold"] = aText.IsBold();
    out["italic"] = aText.IsItalic();
    out["mirrored"] = aText.IsMirrored();
    out["hJustify"] = nameOf( H_ALIGN, aText.GetHorizJustify() );
    out["vJustify"] = nameOf( V_ALIGN, aText.GetVertJustify() );
    out["font"] = utf8( aText.GetFontProp() );
    return out;
}

/** The shared keys of aSet; aTextMax 0 = no text key. False for any malformed value. */
inline bool readText( const json& aSet, TEXT_PATCH& aOut, size_t aTextMax, bool aLines )
{
    if( aTextMax && aSet.contains( "text" )
            && !( aOut.text = textOf( aSet["text"], 0, aTextMax, aLines ) ) )
    {
        return false;
    }

    if( aSet.contains( "layer" ) && !( aOut.layer = layerOf( aSet["layer"] ) ) )
        return false;

    if( aSet.contains( "angle" ) && !( aOut.angle = numberOf( aSet["angle"], -360.0, 360.0 ) ) )
        return false;

    if( aSet.contains( "size" )
            && !( aOut.size = pairOf( aSet["size"], TEXT_MIN_SIZE_MM, TEXT_MAX_SIZE_MM ) ) )
    {
        return false;
    }

    if( aSet.contains( "thickness" )
            && !( aOut.thickness = lengthOf( aSet["thickness"], 0.0, TEXT_MAX_SIZE_MM ) ) )
    {
        return false;
    }

    auto flag = [&]( const char* aKey, std::optional<bool>& aSlot )
    {
        if( !aSet.contains( aKey ) )
            return true;

        aSlot = boolOf( aSet[aKey] );
        return aSlot.has_value();
    };

    if( !flag( "autoThickness", aOut.autoThickness ) || !flag( "bold", aOut.bold )
            || !flag( "italic", aOut.italic ) || !flag( "mirrored", aOut.mirrored )
            || !flag( "knockout", aOut.knockout ) )
    {
        return false;
    }

    if( aSet.contains( "hJustify" ) && !( aOut.hJustify = valueOf( H_ALIGN, aSet["hJustify"] ) ) )
        return false;

    if( aSet.contains( "vJustify" ) && !( aOut.vJustify = valueOf( V_ALIGN, aSet["vJustify"] ) ) )
        return false;

    if( aSet.contains( "font" ) && !( aOut.font = textOf( aSet["font"], 1, 256 ) ) )
        return false;

    return true;
}

/**
 * The shared keys onto a text item, in the dialog's order: text (an empty text refused, the
 * dialog keeps nothing for it), layer, knockout, font, size, thickness (clamped as the dialog
 * clamps), angle, bold, italic, justification, mirror.
 */
inline bool applyText( BOARD_ITEM* aItem, EDA_TEXT* aText, const TEXT_PATCH& aPatch,
                       bool aMultiLine, bool aEmptyTextOk )
{
    if( aPatch.text )
    {
        if( aPatch.text->IsEmpty() && !aEmptyTextOk )
            return false;

        aText->SetText( storedText( aItem, *aPatch.text, aMultiLine ) );
    }

    if( aPatch.layer )
        aItem->SetLayer( *aPatch.layer );

    if( aPatch.knockout )
        aItem->SetIsKnockout( *aPatch.knockout );

    if( aPatch.bold )
        aText->SetBoldFlag( *aPatch.bold );

    if( aPatch.italic )
        aText->SetItalicFlag( *aPatch.italic );

    // The font after the style flags: KiCad picks the outline font's bold or italic face.
    if( aPatch.font )
        aText->SetFontProp( *aPatch.font );

    if( aPatch.size )
        aText->SetTextSize( *aPatch.size );

    if( aPatch.autoThickness && *aPatch.autoThickness )
    {
        aText->SetAutoThickness( true );
    }
    else if( aPatch.thickness || ( aPatch.autoThickness && aText->GetAutoThickness() ) )
    {
        // A pen given, or auto turned off: the dialog's box then holds the effective pen.
        const int pen = aPatch.thickness ? *aPatch.thickness : aText->GetEffectiveTextPenWidth();
        aText->SetTextThickness( pen );

        const int maxPen = ClampTextPenSize( aText->GetTextThickness(), aText->GetTextSize() );

        if( aText->GetTextThickness() > maxPen )
            aText->SetTextThickness( maxPen );
    }

    if( aPatch.angle )
        aText->SetTextAngle( EDA_ANGLE( *aPatch.angle, DEGREES_T ).Normalize() );

    if( aPatch.hJustify )
        aText->SetHorizJustify( *aPatch.hJustify );

    if( aPatch.vJustify )
        aText->SetVertJustify( *aPatch.vJustify );

    if( aPatch.mirrored )
        aText->SetMirrored( *aPatch.mirrored );

    return true;
}

/** PlaceText's attributes for a new text on aLayer (the board's text defaults). */
inline void styleNewText( BOARD_ITEM* aItem, EDA_TEXT* aText, PCB_LAYER_ID aLayer )
{
    const BOARD* board = aItem->GetBoard();

    if( !board )
        return;

    const BOARD_DESIGN_SETTINGS& bds = board->GetDesignSettings();
    TEXT_ATTRIBUTES              attrs;

    attrs.m_Size = bds.GetTextSize( aLayer );
    attrs.m_StrokeWidth = bds.GetTextThickness( aLayer );
    InferBold( &attrs );
    attrs.m_Italic = bds.GetTextItalic( aLayer );
    attrs.m_KeepUpright = bds.GetTextUpright( aLayer );
    attrs.m_Mirrored = board->IsBackLayer( aLayer );
    attrs.m_Halign = GR_TEXT_H_ALIGN_LEFT;
    attrs.m_Valign = GR_TEXT_V_ALIGN_BOTTOM;
    aText->SetAttributes( attrs );
    aItem->SetLayer( aLayer );
}

// ── field and text (DIALOG_TEXT_PROPERTIES) ───────────────────────────────────────────

inline bool inFootprint( const BOARD_ITEM* aItem )
{
    return aItem->GetParentFootprint() != nullptr;
}

inline json fpTextJson( PCB_TEXT* aText, const VECTOR2I& aOrigin )
{
    json out = textAttrsJson( *aText );
    const bool isField = aText->Type() == PCB_FIELD_T;

    out["name"] = isField ? utf8( static_cast<PCB_FIELD*>( aText )->GetName() ) : std::string();
    out["text"] = shownText( aText, aText->GetText() );
    out["layer"] = layerName( aText->GetLayer() );
    out["x"] = mmOf( (long long) aText->GetPosition().x - aOrigin.x );
    out["y"] = mmOf( (long long) aText->GetPosition().y - aOrigin.y );
    out["knockout"] = aText->IsKnockout();

    if( isField )
        out["visible"] = aText->IsVisible();

    if( inFootprint( aText ) )
        out["keepUpright"] = aText->IsKeepUpright();
    else
        out["locked"] = aText->IsLocked();

    return out;
}

/** A field or text patch onto aText; false where the dialog refuses or the item lacks a key. */
inline bool applyFpText( PCB_TEXT* aText, const json& aSet, const VECTOR2I& aOrigin, bool aIsNew )
{
    const bool isField = aText->Type() == PCB_FIELD_T;
    const bool inFp = inFootprint( aText );

    if( !onlyKeys( aSet, { "name", "text", "layer", "x", "y", "angle", "size", "thickness",
                           "autoThickness", "bold", "italic", "mirrored", "hJustify", "vJustify",
                           "knockout", "font", "visible", "keepUpright", "locked" } ) )
    {
        return false;
    }

    // Keys the item does not have (the dialog hides their boxes).
    if( ( aSet.contains( "visible" ) && !isField ) || ( aSet.contains( "keepUpright" ) && !inFp )
            || ( aSet.contains( "locked" ) && inFp ) )
    {
        return false;
    }

    // The name: a text has none; a field's is read only here (renamed through the footprint's
    // own properties) but for a new field, which takes it.
    if( aSet.contains( "name" ) )
    {
        std::optional<wxString> name = textOf( aSet["name"], 0, TEXT_MAX );

        if( !name )
            return false;

        if( !isField && !name->IsEmpty() )
            return false;

        if( isField && !aIsNew && *name != static_cast<PCB_FIELD*>( aText )->GetName() )
            return false;
    }

    TEXT_PATCH patch;

    if( !readText( aSet, patch, inFp ? TEXT_MAX : LONG_TEXT_MAX, /*aLines*/ !inFp ) )
        return false;

    std::optional<int>  x, y;
    std::optional<bool> visible, keepUpright, locked;

    if( aSet.contains( "x" ) && !( x = lengthOf( aSet["x"], -POSITION_MAX_MM, POSITION_MAX_MM ) ) )
        return false;

    if( aSet.contains( "y" ) && !( y = lengthOf( aSet["y"], -POSITION_MAX_MM, POSITION_MAX_MM ) ) )
        return false;

    if( aSet.contains( "visible" ) && !( visible = boolOf( aSet["visible"] ) ) )
        return false;

    if( aSet.contains( "keepUpright" ) && !( keepUpright = boolOf( aSet["keepUpright"] ) ) )
        return false;

    if( aSet.contains( "locked" ) && !( locked = boolOf( aSet["locked"] ) ) )
        return false;

    // A new user field may start empty (the footprint's own properties allow it).
    if( !applyText( aText, aText, patch, /*aMultiLine*/ !inFp, aIsNew && isField ) )
        return false;

    if( locked )
        aText->SetLocked( *locked );

    if( x || y )
    {
        VECTOR2I pos = aText->GetPosition();

        if( x )
            pos.x = aOrigin.x + *x;

        if( y )
            pos.y = aOrigin.y + *y;

        aText->SetPosition( pos );
    }

    if( visible )
        aText->SetVisible( *visible );

    if( keepUpright )
        aText->SetKeepUpright( *keepUpright );

    return true;
}

// ── textbox (DIALOG_TEXTBOX_PROPERTIES) ───────────────────────────────────────────────

inline json textboxJson( PCB_TEXTBOX* aBox, const VECTOR2I& aOrigin )
{
    json out = textAttrsJson( *aBox );
    json pts = json::array();

    if( aBox->GetShape() == SHAPE_T::RECTANGLE )
    {
        pts.push_back( pointJson( aBox->GetStart(), aOrigin ) );
        pts.push_back( pointJson( aBox->GetEnd(), aOrigin ) );
    }
    else
    {
        for( const VECTOR2I& c : aBox->GetCorners() )
            pts.push_back( pointJson( c, aOrigin ) );
    }

    out["text"] = shownText( aBox, aBox->GetText() );
    out["layer"] = layerName( aBox->GetLayer() );
    out["points"] = std::move( pts );
    out["knockout"] = aBox->IsKnockout();
    out["locked"] = aBox->IsLocked();
    out["border"] = aBox->IsBorderEnabled();
    out["borderWidth"] = mmOf( aBox->GetStroke().GetWidth() );
    out["borderStyle"] = styleName( aBox->GetStroke().GetLineStyle() );
    return out;
}

inline bool applyTextbox( PCB_TEXTBOX* aBox, const json& aSet, const VECTOR2I& aOrigin )
{
    if( !onlyKeys( aSet, { "text", "layer", "points", "angle", "size", "thickness",
                           "autoThickness", "bold", "italic", "mirrored", "hJustify", "vJustify",
                           "knockout", "font", "locked", "border", "borderWidth",
                           "borderStyle" } ) )
    {
        return false;
    }

    TEXT_PATCH patch;

    if( !readText( aSet, patch, LONG_TEXT_MAX, /*aLines*/ true ) )
        return false;

    std::optional<bool>       locked, border;
    std::optional<int>        borderWidth;
    std::optional<LINE_STYLE> borderStyle;
    std::optional<VECTOR2I>   start, end;

    if( aSet.contains( "locked" ) && !( locked = boolOf( aSet["locked"] ) ) )
        return false;

    if( aSet.contains( "border" ) && !( border = boolOf( aSet["border"] ) ) )
        return false;

    if( aSet.contains( "borderWidth" )
            && !( borderWidth = lengthOf( aSet["borderWidth"], 0.0, SIZE_MAX_MM ) ) )
    {
        return false;
    }

    if( aSet.contains( "borderStyle" )
            && !( borderStyle = valueOf( LINE_STYLES, aSet["borderStyle"] ) ) )
    {
        return false;
    }

    // The box's corners: a cardinal box only, by its two corners.
    if( aSet.contains( "points" ) )
    {
        const json& pts = aSet["points"];

        if( aBox->GetShape() != SHAPE_T::RECTANGLE || !pts.is_array() || pts.size() != 2
                || !( start = pointOf( pts[0], aOrigin ) ) || !( end = pointOf( pts[1], aOrigin ) )
                || start->x == end->x || start->y == end->y )
        {
            return false;
        }
    }

    if( start )
    {
        aBox->SetStart( *start );
        aBox->SetEnd( *end );
    }

    if( !applyText( aBox, aBox, patch, /*aMultiLine*/ true, /*aEmptyTextOk*/ false ) )
        return false;

    if( locked )
        aBox->SetLocked( *locked );

    if( border )
        aBox->SetBorderEnabled( *border );

    STROKE_PARAMS stroke = aBox->GetStroke();

    if( borderWidth )
        stroke.SetWidth( *borderWidth );

    if( borderStyle )
        stroke.SetLineStyle( *borderStyle );

    aBox->SetStroke( stroke );
    aBox->ClearBoundingBoxCache();
    aBox->ClearRenderCache();

    // The dialog grows a box too small for its text to the text's minimum size.
    const VECTOR2I minBoxSize = aBox->GetMinSize();
    VECTOR2I       s = aBox->GetStart();
    VECTOR2I       e = aBox->GetEnd();
    bool           expanded = false;

    if( minBoxSize.x > 0 && std::abs( e.x - s.x ) < minBoxSize.x )
    {
        e.x = ( e.x >= s.x ) ? s.x + minBoxSize.x : s.x - minBoxSize.x;
        expanded = true;
    }

    if( minBoxSize.y > 0 && std::abs( e.y - s.y ) < minBoxSize.y )
    {
        e.y = ( e.y >= s.y ) ? s.y + minBoxSize.y : s.y - minBoxSize.y;
        expanded = true;
    }

    if( expanded )
        aBox->SetEnd( e );

    return true;
}

// ── table (DIALOG_TABLE_PROPERTIES) ───────────────────────────────────────────────────

/** The cell the dialog's grid shows at row, col (a back layer table mirrored). */
inline PCB_TABLECELL* gridCell( const PCB_TABLE* aTable, int aRow, int aCol )
{
    if( IsBackLayer( aTable->GetLayer() ) )
        return aTable->GetCell( aRow, aTable->GetColCount() - 1 - aCol );

    return aTable->GetCell( aRow, aCol );
}

inline json tableJson( PCB_TABLE* aTable, const VECTOR2I& aOrigin )
{
    json widths = json::array();
    json heights = json::array();
    json rows = json::array();

    for( int col = 0; col < aTable->GetColCount(); ++col )
        widths.push_back( mmOf( aTable->GetColWidth( col ) ) );

    for( int row = 0; row < aTable->GetRowCount(); ++row )
    {
        heights.push_back( mmOf( aTable->GetRowHeight( row ) ) );

        json cells = json::array();

        for( int col = 0; col < aTable->GetColCount(); ++col )
        {
            PCB_TABLECELL* cell = gridCell( aTable, row, col );

            if( !cell || cell->GetColSpan() == 0 || cell->GetRowSpan() == 0 )
                cells.push_back( json() );
            else
                cells.push_back( shownText( aTable, cell->GetText() ) );
        }

        rows.push_back( std::move( cells ) );
    }

    const STROKE_PARAMS& border = aTable->GetBorderStroke();
    const STROKE_PARAMS& seps = aTable->GetSeparatorsStroke();

    json out = json::object();
    out["x"] = mmOf( (long long) aTable->GetPosition().x - aOrigin.x );
    out["y"] = mmOf( (long long) aTable->GetPosition().y - aOrigin.y );
    out["layer"] = layerName( aTable->GetLayer() );
    out["locked"] = aTable->IsLocked();
    out["rows"] = aTable->GetRowCount();
    out["cols"] = aTable->GetColCount();
    out["colWidths"] = std::move( widths );
    out["rowHeights"] = std::move( heights );
    out["cells"] = std::move( rows );
    out["borderExternal"] = aTable->StrokeExternal();
    out["borderHeader"] = aTable->StrokeHeaderSeparator();
    out["borderWidth"] = border.GetWidth() < 0 ? json() : json( mmOf( border.GetWidth() ) );
    out["borderStyle"] = styleName( border.GetLineStyle() );
    out["rowSeparators"] = aTable->StrokeRows();
    out["colSeparators"] = aTable->StrokeColumns();
    out["separatorsWidth"] = seps.GetWidth() < 0 ? json() : json( mmOf( seps.GetWidth() ) );
    out["separatorsStyle"] = styleName( seps.GetLineStyle() );
    return out;
}

inline bool applyTable( PCB_TABLE* aTable, const json& aSet, const VECTOR2I& aOrigin,
                        bool aIsNew )
{
    if( !onlyKeys( aSet, { "x", "y", "layer", "locked", "rows", "cols", "colWidths",
                           "rowHeights", "cells", "borderExternal", "borderHeader",
                           "borderWidth", "borderStyle", "rowSeparators", "colSeparators",
                           "separatorsWidth", "separatorsStyle" } ) )
    {
        return false;
    }

    // The geometry is read only on a set (the dialog edits none of it): given back unchanged.
    if( !aIsNew )
    {
        if( aSet.contains( "rows" ) && aSet["rows"] != aTable->GetRowCount() )
            return false;

        if( aSet.contains( "cols" ) && aSet["cols"] != aTable->GetColCount() )
            return false;

        if( aSet.contains( "colWidths" ) )
        {
            const json& w = aSet["colWidths"];

            if( !w.is_array() || (int) w.size() != aTable->GetColCount() )
                return false;

            for( int col = 0; col < aTable->GetColCount(); ++col )
            {
                if( !sameLength( w[col], aTable->GetColWidth( col ) ) )
                    return false;
            }
        }

        if( aSet.contains( "rowHeights" ) )
        {
            const json& h = aSet["rowHeights"];

            if( !h.is_array() || (int) h.size() != aTable->GetRowCount() )
                return false;

            for( int row = 0; row < aTable->GetRowCount(); ++row )
            {
                if( !sameLength( h[row], aTable->GetRowHeight( row ) ) )
                    return false;
            }
        }
    }

    std::optional<int>          x, y, borderWidth, sepsWidth;
    std::optional<PCB_LAYER_ID> layer;
    std::optional<bool>         locked, external, header, rowSeps, colSeps;
    std::optional<LINE_STYLE>   borderStyle, sepsStyle;

    if( aSet.contains( "x" ) && !( x = lengthOf( aSet["x"], -POSITION_MAX_MM, POSITION_MAX_MM ) ) )
        return false;

    if( aSet.contains( "y" ) && !( y = lengthOf( aSet["y"], -POSITION_MAX_MM, POSITION_MAX_MM ) ) )
        return false;

    if( aSet.contains( "layer" ) && !( layer = layerOf( aSet["layer"] ) ) )
        return false;

    auto flag = [&]( const char* aKey, std::optional<bool>& aSlot )
    {
        if( !aSet.contains( aKey ) )
            return true;

        aSlot = boolOf( aSet[aKey] );
        return aSlot.has_value();
    };

    auto width = [&]( const char* aKey, std::optional<int>& aSlot )
    {
        if( !aSet.contains( aKey ) || aSet[aKey].is_null() )
            return true;

        aSlot = lengthOf( aSet[aKey], 0.0, SIZE_MAX_MM );
        return aSlot.has_value();
    };

    auto style = [&]( const char* aKey, std::optional<LINE_STYLE>& aSlot )
    {
        if( !aSet.contains( aKey ) )
            return true;

        aSlot = valueOf( LINE_STYLES, aSet[aKey] );
        return aSlot.has_value();
    };

    if( !flag( "locked", locked ) || !flag( "borderExternal", external )
            || !flag( "borderHeader", header ) || !flag( "rowSeparators", rowSeps )
            || !flag( "colSeparators", colSeps ) || !width( "borderWidth", borderWidth )
            || !width( "separatorsWidth", sepsWidth ) || !style( "borderStyle", borderStyle )
            || !style( "separatorsStyle", sepsStyle ) )
    {
        return false;
    }

    // The cells: the grid as the dialog shows it, a covered cell null (or empty).
    std::vector<std::vector<std::optional<wxString>>> grid;

    if( aSet.contains( "cells" ) )
    {
        const json& rows = aSet["cells"];

        if( !rows.is_array() || (int) rows.size() != aTable->GetRowCount() )
            return false;

        for( int row = 0; row < aTable->GetRowCount(); ++row )
        {
            const json& cells = rows[row];

            if( !cells.is_array() || (int) cells.size() != aTable->GetColCount() )
                return false;

            std::vector<std::optional<wxString>> line;

            for( int col = 0; col < aTable->GetColCount(); ++col )
            {
                if( cells[col].is_null() )
                {
                    line.emplace_back( wxString() );
                    continue;
                }

                std::optional<wxString> t = textOf( cells[col], 0, LONG_TEXT_MAX, true );

                if( !t )
                    return false;

                line.push_back( t );
            }

            grid.push_back( std::move( line ) );
        }
    }

    if( x || y )
    {
        VECTOR2I pos = aTable->GetPosition();

        if( x )
            pos.x = aOrigin.x + *x;

        if( y )
            pos.y = aOrigin.y + *y;

        aTable->SetPosition( pos );
    }

    // The dialog's order: every cell's text and layer, then the table's layer, lock and strokes.
    const PCB_LAYER_ID newLayer = layer.value_or( aTable->GetLayer() );

    for( int row = 0; row < aTable->GetRowCount(); ++row )
    {
        for( int col = 0; col < aTable->GetColCount(); ++col )
        {
            PCB_TABLECELL* cell = gridCell( aTable, row, col );

            if( !cell )
                return false;

            if( !grid.empty() )
            {
                wxString txt = *grid[row][col];

                if( cell->GetColSpan() == 0 || cell->GetRowSpan() == 0 )
                    txt = wxEmptyString;

                cell->SetText( storedText( aTable, txt, /*aMultiLine*/ false ) );
            }

            cell->SetLayer( newLayer );
        }
    }

    aTable->SetLayer( newLayer );

    if( locked )
        aTable->SetLocked( *locked );

    if( external )
        aTable->SetStrokeExternal( *external );

    if( header )
        aTable->SetStrokeHeaderSeparator( *header );

    {
        STROKE_PARAMS stroke = aTable->GetBorderStroke();

        if( aTable->StrokeExternal() || aTable->StrokeHeaderSeparator() )
            stroke.SetWidth( std::max( 0, borderWidth.value_or( stroke.GetWidth() ) ) );
        else
            stroke.SetWidth( -1 );

        if( borderStyle )
            stroke.SetLineStyle( *borderStyle );

        aTable->SetBorderStroke( stroke );
    }

    if( rowSeps )
        aTable->SetStrokeRows( *rowSeps );

    if( colSeps )
        aTable->SetStrokeColumns( *colSeps );

    {
        STROKE_PARAMS stroke = aTable->GetSeparatorsStroke();

        if( aTable->StrokeRows() || aTable->StrokeColumns() )
            stroke.SetWidth( std::max( 0, sepsWidth.value_or( stroke.GetWidth() ) ) );
        else
            stroke.SetWidth( -1 );

        if( sepsStyle )
            stroke.SetLineStyle( *sepsStyle );

        aTable->SetSeparatorsStroke( stroke );
    }

    return true;
}

/** DrawTable's table: rows and columns of the given sizes from aPos, then normalized. */
inline PCB_TABLE* makeTable( FOOTPRINT* aParent, const json& aItem, const VECTOR2I& aOrigin )
{
    if( !hasKeys( aItem, { "layer", "colWidths", "rowHeights" } ) )
        return nullptr;

    std::optional<PCB_LAYER_ID> layer = layerOf( aItem["layer"] );
    const json&                 w = aItem["colWidths"];
    const json&                 h = aItem["rowHeights"];

    if( !layer || !w.is_array() || !h.is_array() || w.empty() || h.empty()
            || (int) w.size() > TABLE_SPAN_MAX || (int) h.size() > TABLE_SPAN_MAX )
    {
        return nullptr;
    }

    if( aItem.contains( "rows" ) && aItem["rows"] != h.size() )
        return nullptr;

    if( aItem.contains( "cols" ) && aItem["cols"] != w.size() )
        return nullptr;

    std::vector<int> widths, heights;

    for( const json& v : w )
    {
        std::optional<int> iu = lengthOf( v, 0.0, SIZE_MAX_MM );

        if( !iu || *iu <= 0 )
            return nullptr;

        widths.push_back( *iu );
    }

    for( const json& v : h )
    {
        std::optional<int> iu = lengthOf( v, 0.0, SIZE_MAX_MM );

        if( !iu || *iu <= 0 )
            return nullptr;

        heights.push_back( *iu );
    }

    VECTOR2I origin = aOrigin;

    if( aItem.contains( "x" ) || aItem.contains( "y" ) )
    {
        std::optional<int> x = aItem.contains( "x" )
                                       ? lengthOf( aItem["x"], -POSITION_MAX_MM, POSITION_MAX_MM )
                                       : std::optional<int>( 0 );
        std::optional<int> y = aItem.contains( "y" )
                                       ? lengthOf( aItem["y"], -POSITION_MAX_MM, POSITION_MAX_MM )
                                       : std::optional<int>( 0 );

        if( !x || !y )
            return nullptr;

        origin += VECTOR2I( *x, *y );
    }

    int lineWidth = pcbIUScale.mmToIU( 0.1 );

    if( const BOARD* board = aParent->GetBoard() )
        lineWidth = board->GetDesignSettings().GetLineThickness( *layer );

    std::unique_ptr<PCB_TABLE> table = std::make_unique<PCB_TABLE>( aParent, lineWidth );
    table->SetLayer( *layer );
    table->SetColCount( (int) widths.size() );

    for( int col = 0; col < (int) widths.size(); ++col )
        table->SetColWidth( col, widths[col] );

    int yAt = 0;

    for( int row = 0; row < (int) heights.size(); ++row )
    {
        table->SetRowHeight( row, heights[row] );

        int xAt = 0;

        for( int col = 0; col < (int) widths.size(); ++col )
        {
            PCB_TABLECELL* cell = new PCB_TABLECELL( table.get() );
            cell->SetPosition( origin + VECTOR2I( xAt, yAt ) );
            cell->SetEnd( cell->GetPosition() + VECTOR2I( widths[col], heights[row] ) );
            table->AddCell( cell );
            xAt += widths[col];
        }

        yAt += heights[row];
    }

    table->SetLayer( *layer );
    table->Normalize();

    json rest = aItem;
    rest.erase( "x" );
    rest.erase( "y" );
    rest.erase( "colWidths" );
    rest.erase( "rowHeights" );
    rest.erase( "rows" );
    rest.erase( "cols" );

    if( !applyTable( table.get(), rest, aOrigin, /*aIsNew*/ true ) )
        return nullptr;

    return table.release();
}

// ── barcode (DIALOG_BARCODE_PROPERTIES) ───────────────────────────────────────────────

inline json barcodeJson( PCB_BARCODE* aCode, const VECTOR2I& aOrigin )
{
    json out = json::object();
    out["text"] = utf8( aCode->GetText() );
    out["layer"] = layerName( aCode->GetLayer() );
    out["locked"] = aCode->IsLocked();
    out["x"] = mmOf( (long long) aCode->GetPosition().x - aOrigin.x );
    out["y"] = mmOf( (long long) aCode->GetPosition().y - aOrigin.y );
    out["width"] = mmOf( aCode->GetWidth() );
    out["height"] = mmOf( aCode->GetHeight() );
    out["textSize"] = mmOf( aCode->GetTextSize() );
    out["angle"] = degreesOf( aCode->GetAngle() );
    out["knockout"] = aCode->IsKnockout();
    out["margin"] = pairJson( aCode->GetMargin() );
    out["showText"] = aCode->Text().IsVisible();
    out["kind"] = nameOf( BARCODE_KINDS, aCode->GetKind() );
    out["errorCorrection"] = nameOf( ECC_LEVELS, aCode->GetErrorCorrection() );
    return out;
}

inline bool applyBarcode( PCB_BARCODE* aCode, const json& aSet, const VECTOR2I& aOrigin )
{
    if( !onlyKeys( aSet, { "text", "layer", "locked", "x", "y", "width", "height", "textSize",
                           "angle", "knockout", "margin", "showText", "kind",
                           "errorCorrection" } ) )
    {
        return false;
    }

    std::optional<wxString>      text;
    std::optional<PCB_LAYER_ID>  layer;
    std::optional<bool>          locked, knockout, showText;
    std::optional<int>           x, y, width, height, textSize;
    std::optional<double>        angle;
    std::optional<VECTOR2I>      margin;
    std::optional<BARCODE_T>     kind;
    std::optional<BARCODE_ECC_T> ecc;

    if( aSet.contains( "text" ) && !( text = textOf( aSet["text"], 0, 4096 ) ) )
        return false;

    if( aSet.contains( "layer" ) && !( layer = layerOf( aSet["layer"] ) ) )
        return false;

    if( aSet.contains( "locked" ) && !( locked = boolOf( aSet["locked"] ) ) )
        return false;

    if( aSet.contains( "knockout" ) && !( knockout = boolOf( aSet["knockout"] ) ) )
        return false;

    if( aSet.contains( "showText" ) && !( showText = boolOf( aSet["showText"] ) ) )
        return false;

    if( aSet.contains( "x" ) && !( x = lengthOf( aSet["x"], -POSITION_MAX_MM, POSITION_MAX_MM ) ) )
        return false;

    if( aSet.contains( "y" ) && !( y = lengthOf( aSet["y"], -POSITION_MAX_MM, POSITION_MAX_MM ) ) )
        return false;

    if( aSet.contains( "width" )
            && ( !( width = lengthOf( aSet["width"], 0.0, SIZE_MAX_MM ) ) || *width <= 0 ) )
    {
        return false;
    }

    if( aSet.contains( "height" )
            && ( !( height = lengthOf( aSet["height"], 0.0, SIZE_MAX_MM ) ) || *height <= 0 ) )
    {
        return false;
    }

    if( aSet.contains( "textSize" )
            && !( textSize = lengthOf( aSet["textSize"], TEXT_MIN_SIZE_MM, TEXT_MAX_SIZE_MM ) ) )
    {
        return false;
    }

    if( aSet.contains( "angle" ) && !( angle = numberOf( aSet["angle"], -360.0, 360.0 ) ) )
        return false;

    if( aSet.contains( "margin" ) && !( margin = pairOf( aSet["margin"], 0.0, SIZE_MAX_MM ) ) )
        return false;

    if( aSet.contains( "kind" ) && !( kind = valueOf( BARCODE_KINDS, aSet["kind"] ) ) )
        return false;

    if( aSet.contains( "errorCorrection" )
            && !( ecc = valueOf( ECC_LEVELS, aSet["errorCorrection"] ) ) )
    {
        return false;
    }

    // transferDataToBarcode's order.
    if( text )
        aCode->SetText( *text );

    if( locked )
        aCode->SetLocked( *locked );

    if( layer )
        aCode->SetLayer( *layer );

    if( x || y )
    {
        VECTOR2I pos = aCode->GetPosition();

        if( x )
            pos.x = aOrigin.x + *x;

        if( y )
            pos.y = aOrigin.y + *y;

        aCode->SetPosition( pos );
    }

    if( width )
        aCode->SetWidth( *width );

    if( height )
        aCode->SetHeight( *height );

    if( textSize )
        aCode->SetTextSize( *textSize );

    if( angle )
    {
        const EDA_ANGLE oldAngle = aCode->GetAngle();
        const EDA_ANGLE newAngle( *angle, DEGREES_T );

        if( newAngle != oldAngle )
            aCode->Rotate( aCode->GetPosition(), newAngle - oldAngle );
    }

    if( knockout )
        aCode->SetIsKnockout( *knockout );

    if( margin )
        aCode->SetMargin( *margin );

    if( showText )
        aCode->Text().SetVisible( *showText );

    if( kind )
        aCode->SetKind( *kind );

    if( ecc )
        aCode->SetErrorCorrection( *ecc );

    aCode->AssembleBarcode();

    // The dialog's error: a text KiCad cannot encode as this kind.
    if( !aCode->GetText().empty() && aCode->GetSymbolPoly().OutlineCount() == 0 )
        return false;

    return true;
}

// ── the five rows' bodies (cc_fp_items.h's contract) ──────────────────────────────────

/** Any of the five on a copy: the patch is tried there first, so a refusal changes nothing. */
inline bool applyAny( BOARD_ITEM* aItem, const json& aSet, const VECTOR2I& aOrigin, bool aIsNew )
{
    switch( aItem->Type() )
    {
    case PCB_FIELD_T:
    case PCB_TEXT_T:    return applyFpText( static_cast<PCB_TEXT*>( aItem ), aSet, aOrigin, aIsNew );
    case PCB_TEXTBOX_T: return applyTextbox( static_cast<PCB_TEXTBOX*>( aItem ), aSet, aOrigin );
    case PCB_TABLE_T:   return applyTable( static_cast<PCB_TABLE*>( aItem ), aSet, aOrigin, aIsNew );
    case PCB_BARCODE_T: return applyBarcode( static_cast<PCB_BARCODE*>( aItem ), aSet, aOrigin );
    default:            return false;
    }
}

inline json kindGet( BOARD_ITEM* aItem, const VECTOR2I& aOrigin, bool /*aBoardFrame*/ )
{
    if( !aItem )
        return json();

    switch( aItem->Type() )
    {
    case PCB_FIELD_T:
    case PCB_TEXT_T:    return fpTextJson( static_cast<PCB_TEXT*>( aItem ), aOrigin );
    case PCB_TEXTBOX_T: return textboxJson( static_cast<PCB_TEXTBOX*>( aItem ), aOrigin );
    case PCB_TABLE_T:   return tableJson( static_cast<PCB_TABLE*>( aItem ), aOrigin );
    case PCB_BARCODE_T: return barcodeJson( static_cast<PCB_BARCODE*>( aItem ), aOrigin );
    default:            return json();
    }
}

inline bool kindSet( BOARD_ITEM* aItem, const json& aSet, BOARD_COMMIT& aCommit,
                     const VECTOR2I& aOrigin, bool /*aBoardFrame*/ )
{
    if( !aItem )
        return false;

    {
        std::unique_ptr<BOARD_ITEM> probe( static_cast<BOARD_ITEM*>( aItem->Clone() ) );

        if( !applyAny( probe.get(), aSet, aOrigin, /*aIsNew*/ false ) )
            return false;
    }

    aCommit.Modify( aItem );
    return applyAny( aItem, aSet, aOrigin, /*aIsNew*/ false );
}

/** A new field: a user field of a name the footprint does not hold. */
inline BOARD_ITEM* addField( BOARD_ITEM_CONTAINER* aParent, const json& aItem,
                             const VECTOR2I& aOrigin, bool )
{
    FOOTPRINT* footprint = dynamic_cast<FOOTPRINT*>( aParent );

    if( !footprint || !aItem.is_object() || !hasKeys( aItem, { "name", "layer" } ) )
        return nullptr;

    std::optional<wxString>     name = textOf( aItem["name"], 1, TEXT_MAX );
    std::optional<PCB_LAYER_ID> layer = layerOf( aItem["layer"] );

    if( !name || !layer || footprint->HasField( *name ) )
        return nullptr;

    std::unique_ptr<PCB_FIELD> field =
            std::make_unique<PCB_FIELD>( footprint, FIELD_T::USER, *name );
    styleNewText( field.get(), field.get(), *layer );
    field->SetPosition( aOrigin );

    if( !applyFpText( field.get(), aItem, aOrigin, /*aIsNew*/ true ) )
        return nullptr;

    return field.release();
}

/** A new text: PlaceText's attributes on its layer, then the record. */
inline BOARD_ITEM* addText( BOARD_ITEM_CONTAINER* aParent, const json& aItem,
                            const VECTOR2I& aOrigin, bool )
{
    BOARD_ITEM* parent = dynamic_cast<BOARD_ITEM*>( aParent );

    if( !parent || !aItem.is_object() || !hasKeys( aItem, { "text", "layer" } ) )
        return nullptr;

    std::optional<PCB_LAYER_ID> layer = layerOf( aItem["layer"] );

    if( !layer )
        return nullptr;

    std::unique_ptr<PCB_TEXT> text;

    if( FOOTPRINT* footprint = dynamic_cast<FOOTPRINT*>( aParent ) )
        text = std::make_unique<PCB_TEXT>( footprint );
    else
        text = std::make_unique<PCB_TEXT>( parent );

    styleNewText( text.get(), text.get(), *layer );
    text->SetPosition( aOrigin );

    if( !applyFpText( text.get(), aItem, aOrigin, /*aIsNew*/ true ) )
        return nullptr;

    return text.release();
}

/** A new text box: DrawRectangle's box on its layer between its two points, then the record. */
inline BOARD_ITEM* addTextbox( BOARD_ITEM_CONTAINER* aParent, const json& aItem,
                               const VECTOR2I& aOrigin, bool )
{
    BOARD_ITEM* parent = dynamic_cast<BOARD_ITEM*>( aParent );

    if( !parent || !aItem.is_object() || !hasKeys( aItem, { "text", "layer", "points" } ) )
        return nullptr;

    std::optional<PCB_LAYER_ID> layer = layerOf( aItem["layer"] );

    if( !layer )
        return nullptr;

    std::unique_ptr<PCB_TEXTBOX> box = std::make_unique<PCB_TEXTBOX>( parent );
    box->SetShape( SHAPE_T::RECTANGLE );
    styleNewText( box.get(), box.get(), *layer );
    box->SetStart( aOrigin );
    box->SetEnd( aOrigin + VECTOR2I( 1, 1 ) );

    if( !applyTextbox( box.get(), aItem, aOrigin ) )
        return nullptr;

    return box.release();
}

inline BOARD_ITEM* addTable( BOARD_ITEM_CONTAINER* aParent, const json& aItem,
                             const VECTOR2I& aOrigin, bool )
{
    FOOTPRINT* footprint = dynamic_cast<FOOTPRINT*>( aParent );

    if( !footprint || !aItem.is_object() )
        return nullptr;

    return makeTable( footprint, aItem, aOrigin );
}

/** A new barcode: DrawBarcode's (its layer, the layer's text height), then the record. */
inline BOARD_ITEM* addBarcode( BOARD_ITEM_CONTAINER* aParent, const json& aItem,
                               const VECTOR2I& aOrigin, bool )
{
    BOARD_ITEM* parent = dynamic_cast<BOARD_ITEM*>( aParent );

    if( !parent || !aItem.is_object() || !aItem.contains( "layer" ) )
        return nullptr;

    std::optional<PCB_LAYER_ID> layer = layerOf( aItem["layer"] );

    if( !layer )
        return nullptr;

    std::unique_ptr<PCB_BARCODE> code = std::make_unique<PCB_BARCODE>( parent );
    code->SetLayer( *layer );
    code->SetPosition( aOrigin );

    if( const BOARD* board = parent->GetBoard() )
        code->SetTextSize( board->GetDesignSettings().GetTextSize( *layer ).y );

    if( !applyBarcode( code.get(), aItem, aOrigin ) )
        return nullptr;

    return code.release();
}

} // namespace textkind
} // namespace cc_e4

#endif // __EMSCRIPTEN__
