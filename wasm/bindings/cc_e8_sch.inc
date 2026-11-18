// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e8, the schematic face: textually included by eeschema_embind.cpp after
// cc_rules_sch.inc, outside any KICAD_MERGED_EMBIND guard (include subdir eeschema; the
// merged image compiles that TU with -DKICAD_MERGED_EMBIND). It sees the TU's includes and
// helpers (schFrame(), the cc_output namespace) and calls them without editing them.
//
// Every definition of the lane lives in namespace cc_e8 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

#include <sch_bitmap.h>
#include <sch_bus_entry.h>
#include <sch_group.h>
#include <sch_rule_area.h>
#include <sch_sheet_pin.h>
#include <sch_table.h>
#include <sch_tablecell.h>
#include <reference_image.h>
#include <bitmap_base.h>
#include <tools/sch_item_host.h>
#include <backannotate.h>
#include <reporter.h>
#include <wx/base64.h>
#include <wx/mstream.h>

namespace cc_e8
{
namespace sch
{
namespace cs = cc_symedit;

// ── m5-e: schematic item records (kicadSchItemGet, kicadSchItemSet, kicadSchPlaceImage) ──

constexpr size_t GET_MAX = 512;
constexpr size_t SET_MAX = 4096;
constexpr size_t FIELDS_MAX = 32;
constexpr size_t PNG_INLINE_MAX = 64u * 1024u;
constexpr size_t PNG_SET_MAX = 4u * 1024u * 1024u;
constexpr const char* ITEMS_ROOT = "/tmp/cc-schitems/";
constexpr const char* IMAGE_ROOT = "/tmp/cc-schimage/";

/** An item found by uuid on a sheet path, with the screen that holds it. */
struct ITEM_HIT
{
    SCH_ITEM*      item = nullptr;
    SCH_SHEET_PATH path;
};

/** The item whose uuid is aUuid, on aSheetPath when given, else on the shown sheet, else on
 *  the first path holding it. Sheet pins and table cells are found inside their parent. */
ITEM_HIT findItem( SCH_EDIT_FRAME* aFrame, const std::string& aUuid, const std::string& aSheetPath )
{
    ITEM_HIT hit;

    if( !aFrame || !isLowerUuid( aUuid ) )
        return hit;

    const KIID     id( aUuid );
    const wxString wanted = wxString::FromUTF8( aSheetPath.c_str() );
    std::vector<ITEM_HIT> found;

    for( const SCH_SHEET_PATH& path : aFrame->Schematic().Hierarchy() )
    {
        if( !aSheetPath.empty() && path.PathAsString().CmpNoCase( wanted ) != 0 )
            continue;

        SCH_SCREEN* screen = path.LastScreen();

        if( !screen )
            continue;

        SCH_ITEM* match = nullptr;

        for( SCH_ITEM* item : screen->Items() )
        {
            if( item->m_Uuid == id )
                match = item;
            else if( item->Type() == SCH_SHEET_T )
            {
                for( SCH_SHEET_PIN* pin : static_cast<SCH_SHEET*>( item )->GetPins() )
                {
                    if( pin->m_Uuid == id )
                        match = pin;
                }
            }
            else if( item->Type() == SCH_TABLE_T )
            {
                for( SCH_TABLECELL* cell : static_cast<SCH_TABLE*>( item )->GetCells() )
                {
                    if( cell->m_Uuid == id )
                        match = cell;
                }
            }

            if( match )
                break;
        }

        if( match )
            found.push_back( { match, path } );
    }

    if( found.empty() )
        return hit;

    for( const ITEM_HIT& h : found )
    {
        if( h.path == aFrame->GetCurrentSheet() )
            return h;
    }

    return found.front();
}

/** The record kind of an item: CcSchItemKind's names plus table and tableCell. */
std::string recordKind( const EDA_ITEM* aItem )
{
    if( !aItem )
        return std::string();

    if( aItem->Type() == SCH_TABLE_T )
        return "table";

    if( aItem->Type() == SCH_TABLECELL_T )
        return "tableCell";

    const char* kind = CcSchItemKind( aItem );
    return kind ? std::string( kind ) : std::string();
}

const char* labelKindOf( KICAD_T aType )
{
    switch( aType )
    {
    case SCH_GLOBAL_LABEL_T:    return "global";
    case SCH_HIER_LABEL_T:      return "hierarchical";
    case SCH_DIRECTIVE_LABEL_T: return "directive";
    default:                    return "local";
    }
}

std::optional<KICAD_T> labelTypeOf( const std::string& aKind )
{
    if( aKind == "local" )        return SCH_LABEL_T;
    if( aKind == "global" )       return SCH_GLOBAL_LABEL_T;
    if( aKind == "hierarchical" ) return SCH_HIER_LABEL_T;
    if( aKind == "directive" )    return SCH_DIRECTIVE_LABEL_T;
    return std::nullopt;
}

const TOOL_ACTION* labelActionOf( KICAD_T aType )
{
    switch( aType )
    {
    case SCH_LABEL_T:           return &SCH_ACTIONS::toLabel;
    case SCH_GLOBAL_LABEL_T:    return &SCH_ACTIONS::toGLabel;
    case SCH_HIER_LABEL_T:      return &SCH_ACTIONS::toHLabel;
    case SCH_DIRECTIVE_LABEL_T: return &SCH_ACTIONS::toDLabel;
    default:                    return nullptr;
    }
}

const cs::TOKEN<LABEL_FLAG_SHAPE> PORT_SHAPES[] = {
    { LABEL_FLAG_SHAPE::L_INPUT, "input" },
    { LABEL_FLAG_SHAPE::L_OUTPUT, "output" },
    { LABEL_FLAG_SHAPE::L_BIDI, "bidirectional" },
    { LABEL_FLAG_SHAPE::L_TRISTATE, "tri_state" },
    { LABEL_FLAG_SHAPE::L_UNSPECIFIED, "passive" },
};

const cs::TOKEN<LABEL_FLAG_SHAPE> FLAG_SHAPES[] = {
    { LABEL_FLAG_SHAPE::F_DOT, "dot" },
    { LABEL_FLAG_SHAPE::F_ROUND, "round" },
    { LABEL_FLAG_SHAPE::F_DIAMOND, "diamond" },
    { LABEL_FLAG_SHAPE::F_RECTANGLE, "rectangle" },
};

const cs::TOKEN<SPIN_STYLE::SPIN> ORIENTATIONS[] = {
    { SPIN_STYLE::RIGHT, "right" },
    { SPIN_STYLE::UP, "up" },
    { SPIN_STYLE::LEFT, "left" },
    { SPIN_STYLE::BOTTOM, "down" },
};

const cs::TOKEN<SHEET_SIDE> SIDES[] = {
    { SHEET_SIDE::LEFT, "left" },
    { SHEET_SIDE::RIGHT, "right" },
    { SHEET_SIDE::TOP, "top" },
    { SHEET_SIDE::BOTTOM, "bottom" },
};

json strokeKeys( json& aOut, const STROKE_PARAMS& aStroke )
{
    json s = cs::strokeJson( aStroke );
    aOut["width"] = s["width"];
    aOut["style"] = s["style"];
    aOut["color"] = s["color"];
    return aOut;
}

std::string pngOf( const SCH_BITMAP& aImage, size_t& aBytes )
{
    wxMemoryOutputStream out;

    aImage.GetReferenceImage().GetImage().SaveImageData( out );
    aBytes = out.GetLength();

    if( aBytes == 0 || aBytes > PNG_INLINE_MAX )
        return std::string();

    std::vector<char> buf( aBytes );
    out.CopyTo( buf.data(), aBytes );
    return cs::utf8( wxBase64Encode( buf.data(), aBytes ) );
}

void fieldsJson( json& aOut, const std::vector<SCH_FIELD>& aFields, bool aUserOnly, bool aPositions )
{
    json list = json::array();

    for( const SCH_FIELD& f : aFields )
    {
        if( aUserOnly && f.IsMandatory() )
            continue;

        if( list.size() >= FIELDS_MAX )
            break;

        json r;
        r["name"] = cs::utf8( f.GetName() );
        r["value"] = cs::utf8( f.GetText() );
        r["visible"] = f.IsVisible();

        if( aPositions )
        {
            r["x"] = cs::mmOf( f.GetPosition().x );
            r["y"] = cs::mmOf( f.GetPosition().y );
        }

        list.push_back( r );
    }

    aOut["fields"] = list;
}

/** One item's record (spec 4.4), Y down as the schematic stores it. */
json itemRecord( SCH_EDIT_FRAME* aFrame, SCH_ITEM* aItem, const SCH_SHEET_PATH& aPath )
{
    const std::string kind = recordKind( aItem );
    json              r;

    r["kind"] = kind;
    r["uuid"] = cs::utf8( aItem->m_Uuid.AsString() );
    r["sheetPath"] = cs::utf8( aPath.PathAsString() );

    if( kind == "label" )
    {
        SCH_LABEL_BASE* label = static_cast<SCH_LABEL_BASE*>( aItem );
        const KICAD_T   type = label->Type();

        r["labelKind"] = labelKindOf( type );
        r["text"] = cs::utf8( label->GetText() );
        r["x"] = cs::mmOf( label->GetPosition().x );
        r["y"] = cs::mmOf( label->GetPosition().y );
        r["orientation"] = cs::nameOf( ORIENTATIONS, static_cast<SPIN_STYLE::SPIN>( static_cast<int>( label->GetSpinStyle() ) ), "right" );
        r["size"] = cs::mmOf( label->GetTextWidth() );
        r["bold"] = label->IsBold();
        r["italic"] = label->IsItalic();
        r["color"] = cs::colorText( label->GetTextColor() );

        if( type == SCH_GLOBAL_LABEL_T || type == SCH_HIER_LABEL_T )
            r["shape"] = cs::nameOf( PORT_SHAPES, label->GetShape(), "passive" );
        else if( type == SCH_DIRECTIVE_LABEL_T )
            r["shape"] = cs::nameOf( FLAG_SHAPES, label->GetShape(), "round" );
        else
            r["shape"] = nullptr;

        if( type == SCH_DIRECTIVE_LABEL_T )
            r["pinLength"] = cs::mmOf( static_cast<SCH_DIRECTIVE_LABEL*>( label )->GetPinLength() );
        else
            r["pinLength"] = nullptr;

        fieldsJson( r, label->GetFields(), false, true );
    }
    else if( kind == "wire" || kind == "line" )
    {
        SCH_LINE* line = static_cast<SCH_LINE*>( aItem );

        if( kind == "wire" )
            r["bus"] = line->IsBus();

        r["points"] = json::array( { cs::pointJson( line->GetStartPoint(), false ),
                                     cs::pointJson( line->GetEndPoint(), false ) } );
        strokeKeys( r, line->GetStroke() );
    }
    else if( kind == "busEntry" )
    {
        SCH_BUS_ENTRY_BASE* entry = static_cast<SCH_BUS_ENTRY_BASE*>( aItem );

        r["x"] = cs::mmOf( entry->GetPosition().x );
        r["y"] = cs::mmOf( entry->GetPosition().y );
        r["size"] = json::array( { cs::mmOf( entry->GetSize().x ), cs::mmOf( entry->GetSize().y ) } );
        strokeKeys( r, entry->GetStroke() );
    }
    else if( kind == "junction" )
    {
        SCH_JUNCTION* junction = static_cast<SCH_JUNCTION*>( aItem );

        r["x"] = cs::mmOf( junction->GetPosition().x );
        r["y"] = cs::mmOf( junction->GetPosition().y );
        r["diameter"] = cs::mmOf( junction->GetDiameter() );
        r["color"] = cs::colorText( junction->GetColor() );
    }
    else if( kind == "text" )
    {
        SCH_TEXT* text = static_cast<SCH_TEXT*>( aItem );

        r["unit"] = 0;
        r["bodyStyle"] = 0;
        cs::textItemJson( r, *text, false );
        r["color"] = cs::colorText( text->GetTextColor() );
    }
    else if( kind == "textbox" || kind == "tableCell" )
    {
        SCH_TEXTBOX* box = static_cast<SCH_TEXTBOX*>( aItem );

        r["unit"] = 0;
        r["bodyStyle"] = 0;
        cs::textBoxItemJson( r, *box, false );
        r["color"] = cs::colorText( box->GetTextColor() );

        if( kind == "tableCell" )
        {
            SCH_TABLECELL* cell = static_cast<SCH_TABLECELL*>( aItem );

            r["table"] = cell->GetParent() ? cs::utf8( cell->GetParent()->m_Uuid.AsString() )
                                           : std::string();
            r["row"] = cell->GetRow();
            r["col"] = cell->GetColumn();
            r["rowSpan"] = cell->GetRowSpan();
            r["colSpan"] = cell->GetColSpan();
        }
    }
    else if( kind == "shape" || kind == "ruleArea" )
    {
        r["unit"] = 0;
        r["bodyStyle"] = 0;
        cs::shapeItemJson( r, *static_cast<SCH_SHAPE*>( aItem ), false );
    }
    else if( kind == "image" )
    {
        SCH_BITMAP* image = static_cast<SCH_BITMAP*>( aItem );
        size_t      bytes = 0;
        std::string png = pngOf( *image, bytes );

        r["x"] = cs::mmOf( image->GetPosition().x );
        r["y"] = cs::mmOf( image->GetPosition().y );
        r["scale"] = image->GetReferenceImage().GetImageScale();
        r["bytes"] = bytes;
        r["png"] = png.empty() ? json( nullptr ) : json( png );
    }
    else if( kind == "sheet" )
    {
        SCH_SHEET*     sheet = static_cast<SCH_SHEET*>( aItem );
        SCH_SHEET_PATH instance = aPath;

        instance.push_back( sheet );

        r["name"] = cs::utf8( sheet->GetName() );
        r["file"] = cs::utf8( sheet->GetFileName() );
        r["page"] = cs::utf8( instance.GetPageNumber() );
        r["x"] = cs::mmOf( sheet->GetPosition().x );
        r["y"] = cs::mmOf( sheet->GetPosition().y );
        r["size"] = json::array( { cs::mmOf( sheet->GetSize().x ), cs::mmOf( sheet->GetSize().y ) } );
        r["borderWidth"] = cs::mmOf( sheet->GetBorderWidth() );
        r["borderColor"] = cs::colorText( sheet->GetBorderColor() );
        r["fillColor"] = cs::colorText( sheet->GetBackgroundColor() );
        r["inBom"] = !sheet->GetExcludedFromBOM( &aPath );
        r["onBoard"] = !sheet->GetExcludedFromBoard( &aPath );
        r["inSim"] = !sheet->GetExcludedFromSim( &aPath );
        r["dnp"] = sheet->GetDNP( &aPath );
        fieldsJson( r, sheet->GetFields(), true, false );
        r["pins"] = sheet->GetPins().size();
    }
    else if( kind == "sheetPin" )
    {
        SCH_SHEET_PIN* pin = static_cast<SCH_SHEET_PIN*>( aItem );

        r["sheet"] = pin->GetParent() ? cs::utf8( pin->GetParent()->m_Uuid.AsString() )
                                      : std::string();
        r["name"] = cs::utf8( pin->GetText() );
        r["shape"] = cs::nameOf( PORT_SHAPES, pin->GetShape(), "passive" );
        r["side"] = cs::nameOf( SIDES, pin->GetSide(), "left" );
        r["size"] = cs::mmOf( pin->GetTextWidth() );
        r["x"] = cs::mmOf( pin->GetPosition().x );
        r["y"] = cs::mmOf( pin->GetPosition().y );
    }
    else if( kind == "table" )
    {
        SCH_TABLE* table = static_cast<SCH_TABLE*>( aItem );
        json       cells = json::array();
        json       widths = json::array();
        json       heights = json::array();

        for( int row = 0; row < table->GetRowCount(); ++row )
        {
            json line = json::array();

            for( int col = 0; col < table->GetColCount(); ++col )
            {
                SCH_TABLECELL* cell = table->GetCell( row, col );
                line.push_back( cell ? cs::utf8( cell->GetText() ) : std::string() );
            }

            cells.push_back( line );
            heights.push_back( cs::mmOf( table->GetRowHeight( row ) ) );
        }

        for( int col = 0; col < table->GetColCount(); ++col )
            widths.push_back( cs::mmOf( table->GetColWidth( col ) ) );

        r["rows"] = table->GetRowCount();
        r["cols"] = table->GetColCount();
        r["cells"] = cells;
        r["borderExternal"] = table->StrokeExternal();
        r["borderHeader"] = table->StrokeHeaderSeparator();
        r["border"] = cs::strokeJson( table->GetBorderStroke() );
        r["separatorsRows"] = table->StrokeRows();
        r["separatorsCols"] = table->StrokeColumns();
        r["separators"] = cs::strokeJson( table->GetSeparatorsStroke() );
        r["colWidths"] = widths;
        r["rowHeights"] = heights;
    }
    else if( kind == "group" )
    {
        SCH_GROUP* group = static_cast<SCH_GROUP*>( aItem );

        r["name"] = cs::utf8( group->GetName() );
        r["members"] = group->GetItems().size();
        r["designBlock"] = cs::utf8( group->GetDesignBlockLibId().Format().wx_str() );
    }

    return r;
}

// ── the set: read whole and checked before anything is queued ──────────────────────────

struct FIELD_SET
{
    wxString                name;
    std::optional<wxString> value;
    std::optional<bool>     visible;
};

/** One kicadSchItemSet's set, every key checked (spec 4.4, a partial of one kind's keys). */
struct SCH_SET
{
    std::set<std::string> keys;

    // the symbol editor's text, text box and shape keys (text, textbox, shape, ruleArea)
    cs::ITEM_SET symKeys;

    std::optional<KICAD_T>          labelType;
    std::optional<wxString>         text, name, file, page;
    std::optional<double>           x, y, size, width, diameter, scale, pinLength, borderWidth;
    std::optional<SPIN_STYLE::SPIN> orientation;
    std::optional<bool>             bold, italic, inBom, onBoard, inSim, dnp;
    std::optional<COLOR4D>          color, borderColor, fillColor;
    std::optional<LINE_STYLE>       style;
    std::optional<std::string>      shape;
    std::optional<SHEET_SIDE>       side;
    std::optional<VECTOR2I>         sheetSize;
    std::optional<std::array<VECTOR2I, 2>> points;
    std::optional<std::vector<FIELD_SET>>  fields;
    std::optional<wxMemoryBuffer>   png;
};

/** The keys a set may carry for each kind (kind, uuid, sheetPath and the read only keys never). */
const std::set<std::string>& schEditableKeys( const std::string& aKind )
{
    static const std::set<std::string> label = { "labelKind", "text", "x", "y", "orientation",
                                                 "size", "bold", "italic", "color", "shape",
                                                 "pinLength", "fields" };
    static const std::set<std::string> line = { "points", "width", "style", "color" };
    static const std::set<std::string> busEntry = { "width", "style", "color" };
    static const std::set<std::string> junction = { "diameter", "color" };
    static const std::set<std::string> image = { "x", "y", "scale", "png" };
    static const std::set<std::string> sheet = { "name", "file", "page", "x", "y", "size",
                                                 "borderWidth", "borderColor", "fillColor",
                                                 "inBom", "onBoard", "inSim", "dnp", "fields" };
    static const std::set<std::string> sheetPin = { "name", "shape", "side", "size" };
    static const std::set<std::string> group = { "name" };
    static const std::set<std::string> none;

    if( aKind == "label" )                      return label;
    if( aKind == "wire" || aKind == "line" )    return line;
    if( aKind == "busEntry" )                   return busEntry;
    if( aKind == "junction" )                   return junction;
    if( aKind == "image" )                      return image;
    if( aKind == "sheet" )                      return sheet;
    if( aKind == "sheetPin" )                   return sheetPin;
    if( aKind == "group" )                      return group;
    return none;
}

bool isSymbolEditorKind( const std::string& aKind )
{
    return aKind == "text" || aKind == "textbox" || aKind == "shape" || aKind == "ruleArea";
}

std::optional<std::vector<FIELD_SET>> fieldsOf( const json& aValue )
{
    if( !aValue.is_array() || aValue.size() > FIELDS_MAX )
        return std::nullopt;

    std::vector<FIELD_SET> out;
    std::set<wxString>     names;

    for( const json& f : aValue )
    {
        if( !cs::onlyKeys( f, { "name", "value", "visible" } ) || !cs::hasKeys( f, { "name" } ) )
            return std::nullopt;

        FIELD_SET fs;
        std::optional<wxString> name = cs::textOf( f["name"], 1, cs::TEXT_MAX );

        if( !name || !names.insert( *name ).second )
            return std::nullopt;

        fs.name = *name;

        if( f.contains( "value" ) && !( fs.value = cs::textOf( f["value"], 0, cs::TEXT_MAX ) ) )
            return std::nullopt;

        if( f.contains( "visible" ) && !( fs.visible = cs::boolOf( f["visible"] ) ) )
            return std::nullopt;

        out.push_back( fs );
    }

    return out;
}

bool readSchSet( const std::string& aKind, const json& aSet, SCH_SET& aOut )
{
    if( !aSet.is_object() || aSet.empty() )
        return false;

    if( aKind == "table" || aKind == "tableCell" )
        return false;   // RULINGS 2: the table and table cell forms are M12's

    if( isSymbolEditorKind( aKind ) )
    {
        json rest = aSet;

        if( aKind == "text" || aKind == "textbox" )
        {
            if( rest.contains( "color" ) )
            {
                if( !( aOut.color = cs::colorOf( rest["color"] ) ) )
                    return false;

                aOut.keys.insert( "color" );
                rest.erase( "color" );
            }
        }

        if( rest.empty() )
            return !aOut.keys.empty();

        const std::set<std::string>& allowed =
                cs::editableKeys( aKind == "ruleArea" ? std::string( "shape" ) : aKind );

        for( auto it = rest.begin(); it != rest.end(); ++it )
        {
            if( !allowed.count( it.key() ) )
                return false;
        }

        if( !cs::readItemSetBounded( rest, 0, 0, aOut.symKeys, /*aYUp*/ false ) )
            return false;

        aOut.keys.insert( aOut.symKeys.keys.begin(), aOut.symKeys.keys.end() );
        return true;
    }

    const std::set<std::string>& allowed = schEditableKeys( aKind );

    if( allowed.empty() )
        return false;

    for( auto it = aSet.begin(); it != aSet.end(); ++it )
    {
        const std::string& key = it.key();
        const json&        v = it.value();

        if( !allowed.count( key ) )
            return false;

        aOut.keys.insert( key );

        if( key == "labelKind" )
        {
            if( !v.is_string() || !( aOut.labelType = labelTypeOf( v.get<std::string>() ) ) )
                return false;
        }
        else if( key == "text" )
        {
            if( !( aOut.text = cs::textOf( v, 0, cs::TEXT_MAX ) ) )
                return false;
        }
        else if( key == "name" )
        {
            if( !( aOut.name = cs::textOf( v, aKind == "group" ? 0 : 1, cs::TEXT_MAX ) ) )
                return false;
        }
        else if( key == "page" )
        {
            if( !( aOut.page = cs::textOf( v, 1, 64 ) ) )
                return false;
        }
        else if( key == "file" )
        {
            std::optional<wxString> f = cs::textOf( v, 1, cs::TEXT_MAX );

            if( !f || !f->Lower().EndsWith( wxS( ".kicad_sch" ) ) || f->Contains( wxS( ".." ) )
                    || f->Contains( wxS( "\\" ) ) || wxFileName( *f ).IsAbsolute() )
            {
                return false;
            }

            aOut.file = f;
        }
        else if( key == "x" || key == "y" )
        {
            std::optional<double> n = cs::numberOf( v, -cs::POSITION_MAX_MM, cs::POSITION_MAX_MM );

            if( !n )
                return false;

            ( key == "x" ? aOut.x : aOut.y ) = n;
        }
        else if( key == "size" && aKind == "sheet" )
        {
            if( !v.is_array() || v.size() != 2 )
                return false;

            std::optional<double> w = cs::numberOf( v[0], 0.0, cs::POSITION_MAX_MM );
            std::optional<double> h = cs::numberOf( v[1], 0.0, cs::POSITION_MAX_MM );

            if( !w || !h || *w <= 0.0 || *h <= 0.0 )
                return false;

            aOut.sheetSize = VECTOR2I( cs::iuOf( *w ), cs::iuOf( *h ) );
        }
        else if( key == "size" || key == "width" || key == "diameter" || key == "pinLength"
                 || key == "borderWidth" )
        {
            std::optional<double> n = cs::numberOf( v, 0.0, cs::SIZE_MAX_MM );

            if( !n || ( key == "size" && *n <= 0.0 ) )
                return false;

            ( key == "size"        ? aOut.size
              : key == "width"     ? aOut.width
              : key == "diameter"  ? aOut.diameter
              : key == "pinLength" ? aOut.pinLength
                                   : aOut.borderWidth ) = n;
        }
        else if( key == "scale" )
        {
            std::optional<double> n = cs::numberOf( v, 0.001, 1000.0 );

            if( !n || *n <= 0.0 )
                return false;

            aOut.scale = n;
        }
        else if( key == "orientation" )
        {
            if( !( aOut.orientation = cs::valueOf( ORIENTATIONS, v ) ) )
                return false;
        }
        else if( key == "bold" || key == "italic" || key == "inBom" || key == "onBoard"
                 || key == "inSim" || key == "dnp" )
        {
            std::optional<bool> b = cs::boolOf( v );

            if( !b )
                return false;

            ( key == "bold"      ? aOut.bold
              : key == "italic"  ? aOut.italic
              : key == "inBom"   ? aOut.inBom
              : key == "onBoard" ? aOut.onBoard
              : key == "inSim"   ? aOut.inSim
                                 : aOut.dnp ) = b;
        }
        else if( key == "color" || key == "borderColor" || key == "fillColor" )
        {
            std::optional<COLOR4D> c = cs::colorOf( v );

            if( !c )
                return false;

            ( key == "color" ? aOut.color : key == "borderColor" ? aOut.borderColor : aOut.fillColor ) = c;
        }
        else if( key == "style" )
        {
            if( !( aOut.style = cs::valueOf( cs::LINE_STYLES, v ) ) )
                return false;
        }
        else if( key == "shape" )
        {
            if( !v.is_string() )
                return false;

            const std::string s = v.get<std::string>();

            if( !cs::valueOf( PORT_SHAPES, v ) && !cs::valueOf( FLAG_SHAPES, v ) )
                return false;

            if( aKind == "sheetPin" && !cs::valueOf( PORT_SHAPES, v ) )
                return false;

            aOut.shape = s;
        }
        else if( key == "side" )
        {
            if( !( aOut.side = cs::valueOf( SIDES, v ) ) )
                return false;
        }
        else if( key == "points" )
        {
            if( !v.is_array() || v.size() != 2 )
                return false;

            std::array<VECTOR2I, 2> pts;

            for( size_t i = 0; i < 2; ++i )
            {
                if( !v[i].is_array() || v[i].size() != 2 )
                    return false;

                std::optional<double> px = cs::numberOf( v[i][0], -cs::POSITION_MAX_MM, cs::POSITION_MAX_MM );
                std::optional<double> py = cs::numberOf( v[i][1], -cs::POSITION_MAX_MM, cs::POSITION_MAX_MM );

                if( !px || !py )
                    return false;

                pts[i] = cs::storedPoint( *px, *py, /*aYUp*/ false );
            }

            aOut.points = pts;
        }
        else if( key == "fields" )
        {
            if( !( aOut.fields = fieldsOf( v ) ) )
                return false;
        }
        else if( key == "png" )
        {
            if( !v.is_string() || v.get_ref<const std::string&>().size() > PNG_SET_MAX * 4 / 3 + 4 )
                return false;

            const std::string& text = v.get_ref<const std::string&>();
            wxMemoryBuffer     buf = wxBase64Decode( text.c_str(), text.size() );

            if( buf.GetDataLen() == 0 || buf.GetDataLen() > PNG_SET_MAX )
                return false;

            aOut.png = buf;
        }
        else
        {
            return false;
        }
    }

    // a label's shape must suit the kind it ends as: checked per item when applied
    return true;
}

/** True when aShape is a shape the label type admits (none for a local label). */
std::optional<LABEL_FLAG_SHAPE> labelShapeFor( KICAD_T aType, const std::string& aShape )
{
    json v = aShape;

    if( aType == SCH_GLOBAL_LABEL_T || aType == SCH_HIER_LABEL_T )
        return cs::valueOf( PORT_SHAPES, v );

    if( aType == SCH_DIRECTIVE_LABEL_T )
        return cs::valueOf( FLAG_SHAPES, v );

    return std::nullopt;
}

void applyStroke( SCH_ITEM* aItem, const SCH_SET& aSet )
{
    if( !aSet.width && !aSet.style && !aSet.color )
        return;

    STROKE_PARAMS stroke = aItem->GetStroke();

    if( aSet.width )
        stroke.SetWidth( cs::iuOf( *aSet.width ) );

    if( aSet.style )
        stroke.SetLineStyle( *aSet.style );

    if( aSet.color )
        stroke.SetColor( *aSet.color );

    aItem->SetStroke( stroke );
}

void applyFields( SCH_ITEM* aParent, std::vector<SCH_FIELD>& aFields, const std::vector<FIELD_SET>& aSet )
{
    for( const FIELD_SET& fs : aSet )
    {
        SCH_FIELD* field = nullptr;

        for( SCH_FIELD& f : aFields )
        {
            if( f.GetName() == fs.name )
                field = &f;
        }

        if( !field )
        {
            aFields.emplace_back( aParent, FIELD_T::USER, fs.name );
            field = &aFields.back();
            field->SetTextPos( aParent->GetPosition() );
            field->SetVisible( false );
        }

        if( fs.value )
            field->SetText( *fs.value );

        if( fs.visible )
            field->SetVisible( *fs.visible );
    }
}

/** Applies a checked set to one item already staged in the commit. False: the item refused
 *  the set (a label shape its kind has no word for, points that do not fit a shape). */
bool applyToItem( SCH_EDIT_FRAME* aFrame, SCH_ITEM* aItem, const SCH_SHEET_PATH& aPath,
                  const std::string& aKind, const SCH_SET& aSet )
{
    if( aKind == "label" )
    {
        SCH_LABEL_BASE* label = static_cast<SCH_LABEL_BASE*>( aItem );

        if( aSet.text )
            label->SetText( *aSet.text );

        if( aSet.x || aSet.y )
        {
            VECTOR2I pos = label->GetPosition();

            if( aSet.x )
                pos.x = cs::iuOf( *aSet.x );

            if( aSet.y )
                pos.y = cs::iuOf( *aSet.y );

            label->SetPosition( pos );
        }

        if( aSet.orientation )
            label->SetSpinStyle( SPIN_STYLE( *aSet.orientation ) );

        if( aSet.size )
            label->SetTextSize( VECTOR2I( cs::iuOf( *aSet.size ), cs::iuOf( *aSet.size ) ) );

        if( aSet.bold )
            label->SetBold( *aSet.bold );

        if( aSet.italic )
            label->SetItalic( *aSet.italic );

        if( aSet.color )
            label->SetTextColor( *aSet.color );

        if( aSet.shape )
        {
            std::optional<LABEL_FLAG_SHAPE> s = labelShapeFor( label->Type(), *aSet.shape );

            if( !s )
                return false;

            label->SetShape( *s );
        }

        if( aSet.pinLength )
        {
            if( label->Type() != SCH_DIRECTIVE_LABEL_T )
                return false;

            static_cast<SCH_DIRECTIVE_LABEL*>( label )->SetPinLength( cs::iuOf( *aSet.pinLength ) );
        }

        if( aSet.fields )
            applyFields( label, label->GetFields(), *aSet.fields );

        return true;
    }

    if( aKind == "wire" || aKind == "line" )
    {
        SCH_LINE* line = static_cast<SCH_LINE*>( aItem );

        if( aSet.points )
        {
            line->SetStartPoint( ( *aSet.points )[0] );
            line->SetEndPoint( ( *aSet.points )[1] );
        }

        applyStroke( line, aSet );
        return true;
    }

    if( aKind == "busEntry" )
    {
        applyStroke( aItem, aSet );
        return true;
    }

    if( aKind == "junction" )
    {
        SCH_JUNCTION* junction = static_cast<SCH_JUNCTION*>( aItem );

        if( aSet.diameter )
            junction->SetDiameter( cs::iuOf( *aSet.diameter ) );

        if( aSet.color )
            junction->SetColor( *aSet.color );

        return true;
    }

    if( aKind == "text" )
    {
        SCH_TEXT* text = static_cast<SCH_TEXT*>( aItem );

        cs::applyTextItem( *text, aSet.symKeys, false );

        if( aSet.color )
            text->SetTextColor( *aSet.color );

        return true;
    }

    if( aKind == "textbox" )
    {
        SCH_TEXTBOX* box = static_cast<SCH_TEXTBOX*>( aItem );

        cs::applyTextBoxItem( *box, aSet.symKeys, false );

        if( aSet.color )
            box->SetTextColor( *aSet.color );

        return true;
    }

    if( aKind == "shape" || aKind == "ruleArea" )
    {
        SCH_SHAPE* shape = static_cast<SCH_SHAPE*>( aItem );

        if( aSet.symKeys.points
                && !cs::pointsFit( cs::shapeNameOf( *shape ), aSet.symKeys.points->size() ) )
        {
            return false;
        }

        cs::applyShapeItem( *shape, aSet.symKeys );
        return true;
    }

    if( aKind == "image" )
    {
        SCH_BITMAP* image = static_cast<SCH_BITMAP*>( aItem );

        if( aSet.png )
        {
            wxMemoryBuffer buf = *aSet.png;

            if( !image->GetReferenceImage().ReadImageFile( buf ) )
                return false;
        }

        if( aSet.scale )
            image->GetReferenceImage().SetImageScale( *aSet.scale );

        if( aSet.x || aSet.y )
        {
            VECTOR2I pos = image->GetPosition();

            if( aSet.x )
                pos.x = cs::iuOf( *aSet.x );

            if( aSet.y )
                pos.y = cs::iuOf( *aSet.y );

            image->SetPosition( pos );
        }

        return true;
    }

    if( aKind == "sheet" )
    {
        SCH_SHEET* sheet = static_cast<SCH_SHEET*>( aItem );

        if( aSet.name )
            sheet->SetName( *aSet.name );

        if( aSet.page )
        {
            SCH_SHEET_PATH instance = aPath;
            instance.push_back( sheet );
            instance.SetPageNumber( *aSet.page );
        }

        if( aSet.x || aSet.y )
        {
            VECTOR2I pos = sheet->GetPosition();

            if( aSet.x )
                pos.x = cs::iuOf( *aSet.x );

            if( aSet.y )
                pos.y = cs::iuOf( *aSet.y );

            sheet->SetPosition( pos );
        }

        if( aSet.sheetSize )
            sheet->Resize( *aSet.sheetSize );

        if( aSet.borderWidth )
            sheet->SetBorderWidth( cs::iuOf( *aSet.borderWidth ) );

        if( aSet.borderColor )
            sheet->SetBorderColor( *aSet.borderColor );

        if( aSet.fillColor )
            sheet->SetBackgroundColor( *aSet.fillColor );

        if( aSet.inBom )
            sheet->SetExcludedFromBOM( !*aSet.inBom, &aPath );

        if( aSet.onBoard )
            sheet->SetExcludedFromBoard( !*aSet.onBoard, &aPath );

        if( aSet.inSim )
            sheet->SetExcludedFromSim( !*aSet.inSim, &aPath );

        if( aSet.dnp )
            sheet->SetDNP( *aSet.dnp, &aPath );

        if( aSet.fields )
            applyFields( sheet, sheet->GetFields(), *aSet.fields );

        return true;
    }

    if( aKind == "sheetPin" )
    {
        SCH_SHEET_PIN* pin = static_cast<SCH_SHEET_PIN*>( aItem );

        if( aSet.name )
            pin->SetText( *aSet.name );

        if( aSet.shape )
        {
            std::optional<LABEL_FLAG_SHAPE> s = cs::valueOf( PORT_SHAPES, json( *aSet.shape ) );

            if( !s )
                return false;

            pin->SetShape( *s );
        }

        if( aSet.side )
            pin->SetSide( *aSet.side );

        if( aSet.size )
            pin->SetTextSize( VECTOR2I( cs::iuOf( *aSet.size ), cs::iuOf( *aSet.size ) ) );

        return true;
    }

    if( aKind == "group" )
    {
        if( aSet.name )
            static_cast<SCH_GROUP*>( aItem )->SetName( *aSet.name );

        return true;
    }

    return false;
}

struct ASKED_ITEM
{
    std::string uuid;
    std::string sheetPath;
};

/** Runs one checked kicadSchItemSet on the apply coroutine (ONE commit "Edit"), then writes
 *  <dir>/items.json {applied, depth, renamed, replaced} last. */
void runSchItemSet( const std::string& aDir, const std::vector<ASKED_ITEM>& aItems,
                    const std::string& aKind, const SCH_SET& aSet,
                    std::optional<CC_SHEET_FILE_MODE> aFileMode )
{
    SCH_EDIT_FRAME* fr = schFrame();
    json            renamed = json::array();
    json            replaced = json::array();
    int             applied = 0;
    std::string     error;

    ensureRunDir( aDir );

    auto finish =
            [&]()
            {
                json body;
                body["applied"] = applied;
                body["depth"] = fr ? (int) fr->GetUndoCommandCount() : 0;
                body["renamed"] = renamed;
                body["replaced"] = replaced;

                if( !error.empty() )
                    body["error"] = error;

                writeRunDone( aDir, "items.json", body );
            };

    if( !fr )
    {
        error = "no_frame";
        finish();
        return;
    }

    std::vector<ITEM_HIT> hits;

    for( const ASKED_ITEM& a : aItems )
    {
        ITEM_HIT h = findItem( fr, a.uuid, a.sheetPath );

        if( h.item && recordKind( h.item ) == aKind )
            hits.push_back( h );
    }

    if( hits.empty() )
    {
        error = "not_found";
        finish();
        return;
    }

    // The sheet file: ChangeSheetFile's steps (K-SHEETFILE) on the one sheet asked, outside
    // the undo list as KiCad's own sheet dialog leaves a file change.
    if( aKind == "sheet" && aSet.file )
    {
        SCH_SHEET* sheet = static_cast<SCH_SHEET*>( hits.front().item );
        wxString   oldFile = sheet->GetFileName();
        wxString   newFile = *aSet.file;

        if( newFile != oldFile )
        {
            SCH_SHEET_LIST original;
            original.BuildSheetList( &fr->Schematic().Root(), true );

            bool     clearAnnotation = false;
            bool     undoable = true;
            wxString why;

            if( !ccChangeSheetFile( fr, sheet, hits.front().path, newFile,
                                    aFileMode.value_or( CC_SHEET_FILE_MODE::RENAME ),
                                    &clearAnnotation, &undoable, nullptr, &why ) )
            {
                error = cs::utf8( why );
                finish();
                return;
            }

            sheet->SetFileName( newFile );
            renamed.push_back( { { "from", cs::utf8( oldFile ) }, { "to", cs::utf8( newFile ) } } );

            std::vector<SCH_ITEM*> changed{ sheet };
            fr->Schematic().OnItemsRemoved( changed );
            fr->Schematic().OnItemsAdded( changed );
            fr->Schematic().RefreshHierarchy();
            fr->UpdateHierarchyNavigator();

            if( clearAnnotation )
            {
                SCH_SCREENS screens( &fr->Schematic().Root() );
                screens.ClearAnnotationOfNewSheetPaths( original );
                sheet->GetScreen()->ClearAnnotation( &hits.front().path, false );
            }

            fr->OnModify();
        }
    }

    SCH_COMMIT commit( fr );

    // A label kind change runs KiCad's own change label kind (SCH_EDIT_TOOL::ChangeTextType)
    // into this commit: the labels are selected, converted, and the new ones found again by
    // their place and text (KiCad gives a converted label a new uuid).
    if( aKind == "label" && aSet.labelType )
    {
        struct WAS
        {
            std::string uuid;
            VECTOR2I    pos;
            wxString    text;
        };

        std::vector<WAS>     was;
        SCH_SELECTION_TOOL*  selTool = fr->GetToolManager()->GetTool<SCH_SELECTION_TOOL>();
        const TOOL_ACTION*   action = labelActionOf( *aSet.labelType );

        fr->GetToolManager()->RunAction( ACTIONS::selectionClear );

        for( const ITEM_HIT& h : hits )
        {
            if( h.item->Type() == *aSet.labelType )
                continue;

            SCH_LABEL_BASE* label = static_cast<SCH_LABEL_BASE*>( h.item );
            was.push_back( { cs::utf8( label->m_Uuid.AsString() ), label->GetPosition(),
                             label->GetText() } );
            selTool->AddItemToSel( label, true );
        }

        if( !was.empty() && action )
            fr->GetToolManager()->RunSynchronousAction( *action, &commit );

        for( const WAS& w : was )
        {
            for( SCH_ITEM* item : fr->GetScreen()->Items().OfType( *aSet.labelType ) )
            {
                SCH_LABEL_BASE* label = static_cast<SCH_LABEL_BASE*>( item );

                if( label->GetPosition() == w.pos
                        && ( *aSet.labelType == SCH_DIRECTIVE_LABEL_T || label->GetText() == w.text ) )
                {
                    for( ITEM_HIT& h : hits )
                    {
                        if( cs::utf8( h.item->m_Uuid.AsString() ) == w.uuid )
                            h.item = label;
                    }

                    replaced.push_back( { { "from", w.uuid },
                                          { "to", cs::utf8( label->m_Uuid.AsString() ) } } );
                    break;
                }
            }
        }

        fr->GetToolManager()->RunAction( ACTIONS::selectionClear );
    }

    bool anyKey = false;

    for( const std::string& k : aSet.keys )
    {
        if( k != "labelKind" && k != "file" )
            anyKey = true;
    }

    for( const ITEM_HIT& h : hits )
    {
        if( aKind == "label" && aSet.labelType && h.item->Type() != *aSet.labelType )
            continue;   // the conversion refused this one

        if( anyKey )
        {
            SCH_ITEM* staged = h.item;

            if( aKind == "sheetPin" || aKind == "tableCell" )
                staged = static_cast<SCH_ITEM*>( h.item->GetParent() );

            commit.Modify( staged, h.path.LastScreen() );

            if( !applyToItem( fr, h.item, h.path, aKind, aSet ) )
                continue;
        }

        ++applied;
    }

    if( !commit.Empty() )
        commit.Push( _( "Edit" ) );

    fr->GetCanvas()->Refresh();
    finish();
}

} // namespace sch

// schItem: {"items": [{"uuid", "sheetPath"}] (1 to 512)}. Answers {"items": SchItemRecord[]}
// in the order asked, an unknown uuid left out; "" for any other JSON, no frame or an open in
// flight. A read: synchronous, unsaved edits included.
std::string kicadSchItemGet( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return "";

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 1 || !args.contains( "items" ) )
        return "";

    const json& items = args["items"];

    if( !items.is_array() || items.empty() || items.size() > sch::GET_MAX )
        return "";

    json out = json::array();

    try
    {
        for( const json& a : items )
        {
            if( !cc_symedit::onlyKeys( a, { "uuid", "sheetPath" } )
                    || !cc_symedit::hasKeys( a, { "uuid", "sheetPath" } ) || !a["uuid"].is_string()
                    || !a["sheetPath"].is_string() )
            {
                return "";
            }

            sch::ITEM_HIT hit = sch::findItem( fr, a["uuid"].get<std::string>(),
                                               a["sheetPath"].get<std::string>() );

            if( hit.item && !sch::recordKind( hit.item ).empty() )
                out.push_back( sch::itemRecord( fr, hit.item, hit.path ) );
        }
    }
    catch( ... )
    {
        return "";
    }

    json answer;
    answer["items"] = out;
    return answer.dump( -1, ' ', false, json::error_handler_t::replace );
}

// schItem: {"run", "dir": "/tmp/cc-schitems/<n>", "items": [{"uuid", "sheetPath"}] (1 to
// 4,096), "kind", "set", "fileMode"?}. Every item must be of kind; set is a partial of that
// kind's editable keys (spec 4.4). A label kind change needs every label on the shown sheet
// (KiCad's change label kind works there); a sheet file change takes one sheet on the shown
// sheet and fileMode "useExisting" or "rename". False (nothing queued) for any other JSON;
// true once ONE commit "Edit" is queued on the apply coroutine, which writes items.json last.
bool kicadSchItemSet( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() )
        return false;

    try
    {
        if( !cc_symedit::onlyKeys( args, { "run", "dir", "items", "kind", "set", "fileMode" } )
                || !cc_symedit::hasKeys( args, { "run", "dir", "items", "kind", "set" } ) )
        {
            return false;
        }

        if( !args["run"].is_number_integer() || args["run"].get<long long>() < 0
                || !args["dir"].is_string() || !args["kind"].is_string()
                || !args["items"].is_array() || args["items"].empty()
                || args["items"].size() > sch::SET_MAX )
        {
            return false;
        }

        const std::string dir = args["dir"].get<std::string>();
        const std::string kind = args["kind"].get<std::string>();

        if( !isRunDir( dir, sch::ITEMS_ROOT ) )
            return false;

        sch::SCH_SET set;

        if( !sch::readSchSet( kind, args["set"], set ) )
            return false;

        std::optional<CC_SHEET_FILE_MODE> mode;

        if( args.contains( "fileMode" ) )
        {
            if( kind != "sheet" || !set.file || !args["fileMode"].is_string() )
                return false;

            const std::string m = args["fileMode"].get<std::string>();

            if( m == "useExisting" )
                mode = CC_SHEET_FILE_MODE::USE_EXISTING;
            else if( m == "rename" )
                mode = CC_SHEET_FILE_MODE::RENAME;
            else
                return false;
        }

        std::vector<sch::ASKED_ITEM> asked;
        std::set<std::string>        seen;

        for( const json& a : args["items"] )
        {
            if( !cc_symedit::onlyKeys( a, { "uuid", "sheetPath" } )
                    || !cc_symedit::hasKeys( a, { "uuid", "sheetPath" } ) || !a["uuid"].is_string()
                    || !a["sheetPath"].is_string() )
            {
                return false;
            }

            sch::ASKED_ITEM item{ a["uuid"].get<std::string>(), a["sheetPath"].get<std::string>() };
            sch::ITEM_HIT   hit = sch::findItem( fr, item.uuid, item.sheetPath );

            if( !hit.item || sch::recordKind( hit.item ) != kind || !seen.insert( item.uuid ).second )
                return false;

            const bool shown = hit.path == fr->GetCurrentSheet();

            if( ( set.labelType || set.file ) && !shown )
                return false;

            item.sheetPath = cc_symedit::utf8( hit.path.PathAsString() );
            asked.push_back( item );
        }

        if( set.file && asked.size() != 1 )
            return false;

        pcbjam_collab::runOnCoroutine( fr, [dir, asked, kind, set, mode]() {
            sch::runSchItemSet( dir, asked, kind, set, mode );
        } );
    }
    catch( ... )
    {
        return false;
    }

    return true;
}

// schImagePlace: {"path": "/tmp/cc-schimage/<name>"}. Loads the picture through KiCad's
// REFERENCE_IMAGE and hands it to KiCad's own image tool (placeImage with the image as its
// parameter, as DdAddImage does), so it hangs on the pointer until a click places it (one
// commit then, KiCad's). False for any other JSON, no frame, an open in flight or a file
// KiCad cannot read.
bool kicadSchPlaceImage( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 1 || !args.contains( "path" )
            || !args["path"].is_string() )
    {
        return false;
    }

    const std::string path = args["path"].get<std::string>();

    if( !isRunDir( path, sch::IMAGE_ROOT ) )
        return false;

    const wxString file = wxString::FromUTF8( path.c_str() );

    if( !wxFileExists( file ) )
        return false;

    {
        SCH_BITMAP probe( VECTOR2I( 0, 0 ) );

        if( !probe.GetReferenceImage().ReadImageFile( file ) )
            return false;
    }

    pcbjam_collab::runOnCoroutine( fr, [file]() {
        SCH_EDIT_FRAME* frame = schFrame();

        if( !frame )
            return;

        SCH_BITMAP* image = new SCH_BITMAP( VECTOR2I( 0, 0 ) );

        if( !image->GetReferenceImage().ReadImageFile( file ) )
        {
            delete image;
            return;
        }

        frame->GetToolManager()->RunAction( SCH_ACTIONS::placeImage, image );
    } );

    return true;
}

// The marker: this engine asks window.kicadHostSchItem before a schematic item's dialog.
bool kicadHostSchItems()
{
    return true;
}


// ── m9-e: sheet pins against hierarchical labels (cap syncPins) ───────────────────────────

namespace pins
{
namespace cs = cc_symedit;

constexpr size_t PAIRS_MAX = 4096;
constexpr size_t ACTIONS_MAX = 4096;
constexpr size_t NAMES_MAX = 64;
constexpr const char* PINS_ROOT = "/tmp/cc-sheet-pins/";

/** A sheet instance: the sheet, the path it sits on and its own instance path. */
struct SHEET_AT
{
    SCH_SHEET*     sheet = nullptr;
    SCH_SHEET_PATH parent;
    SCH_SHEET_PATH instance;
};

std::vector<SHEET_AT> sheetInstances( SCH_EDIT_FRAME* aFrame )
{
    std::vector<SHEET_AT> out;

    for( const SCH_SHEET_PATH& path : aFrame->Schematic().Hierarchy() )
    {
        if( path.size() < 2 || !path.Last() || !path.Last()->GetScreen() )
            continue;

        SHEET_AT at;
        at.sheet = path.Last();
        at.instance = path;
        at.parent = path;
        at.parent.pop_back();
        out.push_back( at );
    }

    return out;
}

/** The sheet's hierarchical labels, one per name (the first), in KiCad's natural order. */
std::vector<SCH_HIERLABEL*> uniqueLabels( SCH_SHEET* aSheet )
{
    std::vector<SCH_HIERLABEL*> labels;
    std::set<wxString>          names;

    for( SCH_ITEM* item : aSheet->GetScreen()->Items().OfType( SCH_HIER_LABEL_T ) )
    {
        SCH_HIERLABEL* label = static_cast<SCH_HIERLABEL*>( item );

        if( names.insert( label->GetText() ).second )
            labels.push_back( label );
    }

    std::sort( labels.begin(), labels.end(),
               []( const SCH_HIERLABEL* a, const SCH_HIERLABEL* b )
               {
                   return StrNumCmp( a->GetText(), b->GetText(), true ) < 0;
               } );

    return labels;
}

/**
 * Pins against labels by name: PANEL_SYNC_SHEET_PINS::UpdateForms' pairing (same name and
 * shape: associated), then a label and a pin of the same name whose shapes differ (one row,
 * the shapes shown apart), then the singles.
 */
json pairsOf( SCH_SHEET* aSheet, bool& aTruncated )
{
    std::vector<SCH_HIERLABEL*> labels = uniqueLabels( aSheet );
    std::vector<SCH_SHEET_PIN*> pins = aSheet->GetPins();
    std::vector<std::pair<SCH_HIERLABEL*, SCH_SHEET_PIN*>> rows;
    std::vector<SCH_HIERLABEL*> unmatched;

    for( SCH_HIERLABEL* label : labels )
    {
        auto it = std::find_if( pins.begin(), pins.end(),
                                [&]( SCH_SHEET_PIN* p )
                                {
                                    return p->GetText() == label->GetText()
                                           && p->GetShape() == label->GetShape();
                                } );

        if( it != pins.end() )
        {
            rows.emplace_back( label, *it );
            pins.erase( it );
        }
        else
        {
            unmatched.push_back( label );
        }
    }

    for( SCH_HIERLABEL* label : unmatched )
    {
        auto it = std::find_if( pins.begin(), pins.end(),
                                [&]( SCH_SHEET_PIN* p ) { return p->GetText() == label->GetText(); } );

        if( it != pins.end() )
        {
            rows.emplace_back( label, *it );
            pins.erase( it );
        }
        else
        {
            rows.emplace_back( label, nullptr );
        }
    }

    for( SCH_SHEET_PIN* pin : pins )
        rows.emplace_back( nullptr, pin );

    json out = json::array();

    for( const auto& [label, pin] : rows )
    {
        if( out.size() >= PAIRS_MAX )
        {
            aTruncated = true;
            break;
        }

        json r;
        r["name"] = cs::utf8( label ? label->GetText() : pin->GetText() );
        r["pin"] = pin ? json( cs::utf8( pin->m_Uuid.AsString() ) ) : json( nullptr );
        r["label"] = label ? json( cs::utf8( label->m_Uuid.AsString() ) ) : json( nullptr );
        r["pinShape"] = pin ? json( cs::nameOf( sch::PORT_SHAPES, pin->GetShape(), "passive" ) )
                            : json( nullptr );
        r["labelShape"] = label ? json( cs::nameOf( sch::PORT_SHAPES, label->GetShape(), "passive" ) )
                                : json( nullptr );
        out.push_back( r );
    }

    return out;
}

/** The sheet pin whose uuid is aUuid, anywhere in the design, with the path its sheet sits on. */
SCH_SHEET_PIN* findPin( SCH_EDIT_FRAME* aFrame, const std::string& aUuid, SCH_SHEET_PATH& aParent )
{
    if( !isLowerUuid( aUuid ) )
        return nullptr;

    const KIID id( aUuid );

    for( const SHEET_AT& at : sheetInstances( aFrame ) )
    {
        for( SCH_SHEET_PIN* pin : at.sheet->GetPins() )
        {
            if( pin->m_Uuid == id )
            {
                aParent = at.parent;
                return pin;
            }
        }
    }

    return nullptr;
}

/** The hierarchical label aUuid on the sheet path aSheetPath. */
SCH_HIERLABEL* findLabel( SCH_EDIT_FRAME* aFrame, const std::string& aUuid,
                          const std::string& aSheetPath, SCH_SHEET_PATH& aPath )
{
    sch::ITEM_HIT hit = sch::findItem( aFrame, aUuid, aSheetPath );

    if( !hit.item || hit.item->Type() != SCH_HIER_LABEL_T || aSheetPath.empty() )
        return nullptr;

    aPath = hit.path;
    return static_cast<SCH_HIERLABEL*>( hit.item );
}

struct SYNC_ACTION
{
    std::string                     op;
    std::string                     uuid;
    std::string                     sheetPath;
    std::optional<wxString>         to;
    std::optional<LABEL_FLAG_SHAPE> shape;
};

bool readAction( const json& aAction, SYNC_ACTION& aOut )
{
    if( !aAction.is_object() || !aAction.contains( "op" ) || !aAction["op"].is_string() )
        return false;

    aOut.op = aAction["op"].get<std::string>();

    const bool onPin = aOut.op == "renamePin" || aOut.op == "shapePin" || aOut.op == "removePin";
    const bool onLabel = aOut.op == "renameLabel" || aOut.op == "shapeLabel" || aOut.op == "removeLabel";

    if( !onPin && !onLabel )
        return false;

    const char* idKey = onPin ? "pin" : "label";
    const bool  hasTo = aOut.op != "removePin" && aOut.op != "removeLabel";

    std::vector<const char*> keys = { "op", idKey };

    if( onLabel )
        keys.push_back( "sheetPath" );

    if( hasTo )
        keys.push_back( "to" );

    if( aAction.size() != keys.size() )
        return false;

    for( const char* k : keys )
    {
        if( !aAction.contains( k ) )
            return false;
    }

    if( !aAction[idKey].is_string() || !isLowerUuid( aAction[idKey].get<std::string>() ) )
        return false;

    aOut.uuid = aAction[idKey].get<std::string>();

    if( onLabel )
    {
        if( !aAction["sheetPath"].is_string() || aAction["sheetPath"].get<std::string>().empty() )
            return false;

        aOut.sheetPath = aAction["sheetPath"].get<std::string>();
    }

    if( aOut.op == "renamePin" || aOut.op == "renameLabel" )
    {
        if( !( aOut.to = cs::textOf( aAction["to"], 1, cs::TEXT_MAX ) ) )
            return false;
    }
    else if( hasTo )
    {
        if( !( aOut.shape = cs::valueOf( sch::PORT_SHAPES, aAction["to"] ) ) )
            return false;
    }

    return true;
}

/** Every action as ONE commit "Synchronize sheet pins"; record sheet-pins.json last. */
void runApply( const std::string& aDir, const std::vector<SYNC_ACTION>& aActions )
{
    SCH_EDIT_FRAME* fr = schFrame();
    int             applied = 0;

    ensureRunDir( aDir );

    if( fr )
    {
        SCH_COMMIT commit( fr );
        std::vector<std::pair<SCH_SHEET*, SCH_SHEET_PIN*>> pinsToRemove;
        std::set<SCH_ITEM*> removed;

        for( const SYNC_ACTION& a : aActions )
        {
            if( a.op == "renamePin" || a.op == "shapePin" || a.op == "removePin" )
            {
                SCH_SHEET_PATH parent;
                SCH_SHEET_PIN* pin = findPin( fr, a.uuid, parent );

                if( !pin || removed.count( pin ) )
                    continue;

                SCH_SHEET* sheet = static_cast<SCH_SHEET*>( pin->GetParent() );
                commit.Modify( sheet, parent.LastScreen() );

                if( a.op == "renamePin" )
                    pin->SetText( *a.to );
                else if( a.op == "shapePin" )
                    pin->SetShape( *a.shape );
                else
                {
                    pinsToRemove.emplace_back( sheet, pin );
                    removed.insert( pin );
                }

                ++applied;
            }
            else
            {
                SCH_SHEET_PATH path;
                SCH_HIERLABEL* label = findLabel( fr, a.uuid, a.sheetPath, path );

                if( !label || removed.count( label ) )
                    continue;

                if( a.op == "removeLabel" )
                {
                    commit.Remove( label, path.LastScreen() );
                    removed.insert( label );
                }
                else
                {
                    commit.Modify( label, path.LastScreen() );

                    if( a.op == "renameLabel" )
                        label->SetText( *a.to );
                    else
                        label->SetShape( *a.shape );
                }

                ++applied;
            }
        }

        // SCH_EDIT_TOOL::DoDelete's way with a sheet pin: the sheet staged, the pin taken off it.
        for( auto& [sheet, pin] : pinsToRemove )
            sheet->RemovePin( pin );

        if( !commit.Empty() )
            commit.Push( _( "Synchronize sheet pins" ) );

        fr->GetCanvas()->Refresh();
    }

    json body;
    body["applied"] = applied;
    body["depth"] = fr ? (int) fr->GetUndoCommandCount() : 0;
    writeRunDone( aDir, "sheet-pins.json", body );
}

} // namespace pins

// syncPins: {"sheets"?: [{"uuid", "sheetPath"}]} (none: every sheet instance), sheetPath the
// path the sheet sits on. Answers {"sheets": SheetPinsRow[], "truncated"}: each instance's
// pins against its own file's hierarchical labels, paired by name. "" for any other JSON, no
// frame or an open in flight. A read.
std::string kicadSheetPinsGet( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return "";

    json args = json::parse( aArgsJson.empty() ? std::string( "{}" ) : aArgsJson, nullptr,
                             /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || !cc_symedit::onlyKeys( args, { "sheets" } ) )
        return "";

    std::set<std::pair<std::string, std::string>> wanted;

    if( args.contains( "sheets" ) )
    {
        if( !args["sheets"].is_array() || args["sheets"].empty() || args["sheets"].size() > 4096 )
            return "";

        for( const json& s : args["sheets"] )
        {
            if( !cc_symedit::onlyKeys( s, { "uuid", "sheetPath" } )
                    || !cc_symedit::hasKeys( s, { "uuid", "sheetPath" } ) || !s["uuid"].is_string()
                    || !s["sheetPath"].is_string() || !isLowerUuid( s["uuid"].get<std::string>() ) )
            {
                return "";
            }

            wanted.emplace( s["uuid"].get<std::string>(), s["sheetPath"].get<std::string>() );
        }
    }

    json rows = json::array();
    bool truncated = false;

    try
    {
        for( const pins::SHEET_AT& at : pins::sheetInstances( fr ) )
        {
            const std::string uuid = cc_symedit::utf8( at.sheet->m_Uuid.AsString() );
            const std::string parent = cc_symedit::utf8( at.parent.PathAsString() );

            if( !wanted.empty() && !wanted.count( { uuid, parent } ) )
                continue;

            json r;
            r["sheet"] = uuid;
            r["sheetPath"] = parent;
            r["name"] = cc_symedit::utf8( at.sheet->GetName() );
            r["file"] = cc_symedit::utf8( at.sheet->GetFileName() );
            r["pairs"] = pins::pairsOf( at.sheet, truncated );
            rows.push_back( r );
        }
    }
    catch( ... )
    {
        return "";
    }

    json out;
    out["sheets"] = rows;
    out["truncated"] = truncated;
    return out.dump( -1, ' ', false, json::error_handler_t::replace );
}

// syncPins: {"run", "dir": "/tmp/cc-sheet-pins/<n>", "actions": SyncAction[] (1 to 4,096)}.
// Every pin and label named must exist (a label on its sheetPath). False (nothing queued) for
// any other JSON; true once ONE commit "Synchronize sheet pins" is queued on the apply
// coroutine, which writes sheet-pins.json {applied, depth} last.
bool kicadSheetPinsApply( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 3 || !args.contains( "run" )
            || !args.contains( "dir" ) || !args.contains( "actions" )
            || !args["run"].is_number_integer() || args["run"].get<long long>() < 0
            || !args["dir"].is_string() || !args["actions"].is_array()
            || args["actions"].empty() || args["actions"].size() > pins::ACTIONS_MAX )
    {
        return false;
    }

    const std::string dir = args["dir"].get<std::string>();

    if( !isRunDir( dir, pins::PINS_ROOT ) )
        return false;

    std::vector<pins::SYNC_ACTION> actions;

    try
    {
        for( const json& a : args["actions"] )
        {
            pins::SYNC_ACTION action;

            if( !pins::readAction( a, action ) )
                return false;

            SCH_SHEET_PATH where;

            if( action.op.find( "Pin" ) != std::string::npos )
            {
                if( !pins::findPin( fr, action.uuid, where ) )
                    return false;
            }
            else if( !pins::findLabel( fr, action.uuid, action.sheetPath, where ) )
            {
                return false;
            }

            actions.push_back( action );
        }
    }
    catch( ... )
    {
        return false;
    }

    pcbjam_collab::runOnCoroutine( fr, [dir, actions]() { pins::runApply( dir, actions ); } );
    return true;
}

// syncPins: {"sheet": uuid, "sheetPath" (the path the sheet sits on), "place": "pin"|"label",
// "names": string[] (1 to 64)}. Arms K-SYNCPIN's template with the counterparts of the names
// (for a pin the sub sheet's hierarchical labels, for a label the sheet's pins; each name must
// have one), then, as doSyncSheetsPins' placement callback does, runs placeSheetPin over the
// sheet or enters the sheet and runs placeHierLabel: each press places the next name, Esc
// ends, KiCad's commit per placement. False for any other JSON, no frame or an open in flight.
bool kicadSheetPinsPlace( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 4
            || !cc_symedit::hasKeys( args, { "sheet", "sheetPath", "place", "names" } )
            || !args["sheet"].is_string() || !args["sheetPath"].is_string()
            || !args["place"].is_string() || !args["names"].is_array() || args["names"].empty()
            || args["names"].size() > pins::NAMES_MAX )
    {
        return false;
    }

    const std::string place = args["place"].get<std::string>();
    const std::string sheetId = args["sheet"].get<std::string>();
    const std::string sheetPath = args["sheetPath"].get<std::string>();

    if( place != "pin" && place != "label" )
        return false;

    std::vector<wxString> names;

    for( const json& n : args["names"] )
    {
        std::optional<wxString> name = cc_symedit::textOf( n, 1, cc_symedit::TEXT_MAX );

        if( !name )
            return false;

        names.push_back( *name );
    }

    auto counterparts =
            [place, names]( SCH_SHEET* aSheet, std::vector<std::unique_ptr<SCH_HIERLABEL>>& aOut )
            {
                for( const wxString& name : names )
                {
                    SCH_HIERLABEL* found = nullptr;

                    if( place == "pin" )
                    {
                        for( SCH_HIERLABEL* label : pins::uniqueLabels( aSheet ) )
                        {
                            if( label->GetText() == name )
                                found = label;
                        }
                    }
                    else
                    {
                        for( SCH_SHEET_PIN* pin : aSheet->GetPins() )
                        {
                            if( !found && pin->GetText() == name )
                                found = pin;
                        }
                    }

                    if( !found )
                        return false;

                    aOut.emplace_back( static_cast<SCH_HIERLABEL*>( found->Clone() ) );
                }

                return true;
            };

    sch::ITEM_HIT hit = sch::findItem( fr, sheetId, sheetPath );

    if( !hit.item || hit.item->Type() != SCH_SHEET_T || sheetPath.empty() )
        return false;

    {
        std::vector<std::unique_ptr<SCH_HIERLABEL>> probe;

        if( !counterparts( static_cast<SCH_SHEET*>( hit.item ), probe ) )
            return false;
    }

    pcbjam_collab::runOnCoroutine( fr, [sheetId, sheetPath, place, counterparts]() {
        SCH_EDIT_FRAME* frame = schFrame();
        sch::ITEM_HIT   live = sch::findItem( frame, sheetId, sheetPath );

        if( !live.item || live.item->Type() != SCH_SHEET_T )
            return;

        SCH_SHEET*        sheet = static_cast<SCH_SHEET*>( live.item );
        CC_SYNC_TEMPLATE& armed = CcSyncTemplate();

        armed.EndPlacement();

        if( !counterparts( sheet, armed.items ) )
        {
            armed.EndPlacement();
            return;
        }

        TOOL_MANAGER*  tools = frame->GetToolManager();
        SCH_SHEET_PATH parent = live.path;

        if( place == "label" )
        {
            SCH_SHEET_PATH instance = parent;
            instance.push_back( sheet );
            tools->RunAction<SCH_SHEET_PATH*>( SCH_ACTIONS::changeSheet, &instance );
            tools->RunAction( SCH_ACTIONS::placeHierLabel );
        }
        else
        {
            if( !( frame->GetCurrentSheet() == parent ) )
                tools->RunAction<SCH_SHEET_PATH*>( SCH_ACTIONS::changeSheet, &parent );

            tools->GetTool<SCH_SELECTION_TOOL>()->SyncSelection( {}, nullptr, { sheet } );
            tools->RunAction( SCH_ACTIONS::placeSheetPin );
        }
    } );

    return true;
}


// ── lo-e4-e: net names from the board back onto the schematic ─────────────────────────────

namespace nets
{
constexpr size_t PADS_MAX = 32768;
constexpr const char* RENAME_ROOT = "/tmp/cc-renameNets/";

/** BACK_ANNOTATE's net name pass alone (no relink, no other property), over the pads handed
 *  in; ONE commit "Update net names" unless a preview; renameNets.json written last. */
void runRename( const std::string& aDir, bool aPreview,
                const std::vector<BACK_ANNOTATE::CC_PAD_NET>& aPads, bool aTruncated )
{
    SCH_EDIT_FRAME* fr = schFrame();
    json            rows = json::array();

    ensureRunDir( aDir );

    if( fr )
    {
        // Fresh connectivity, as the Update Schematic from PCB dialog leaves it before a pass.
        fr->RecalculateConnections( nullptr, NO_CLEANUP );

        NULL_REPORTER reporter;
        BACK_ANNOTATE annotate( fr, reporter, /*aRelinkFootprints*/ false,
                                /*aProcessFootprints*/ false, /*aProcessValues*/ false,
                                /*aProcessReferences*/ false, /*aProcessNetNames*/ true,
                                /*aProcessAttributes*/ false, /*aProcessOtherFields*/ false,
                                /*aPreferUnitSwaps*/ false, /*aPreferPinSwaps*/ false,
                                /*aDryRun*/ aPreview );
        SCH_COMMIT    commit( fr );

        for( const BACK_ANNOTATE::CC_NET_ROW& r : annotate.CcProcessNetNames( &commit, aPads ) )
        {
            json row;
            row["ref"] = cc_symedit::utf8( r.ref );
            row["pin"] = cc_symedit::utf8( r.pin );
            row["from"] = cc_symedit::utf8( r.from );
            row["to"] = cc_symedit::utf8( r.to );
            row["sheetPath"] = cc_symedit::utf8( r.sheetPath );
            row["outcome"] = cc_symedit::utf8( r.outcome );

            if( r.label != niluuid )
                row["label"] = cc_symedit::utf8( r.label.AsString() );

            rows.push_back( row );
        }

        if( !aPreview && !commit.Empty() )
        {
            commit.Push( _( "Update net names" ) );
            fr->RecalculateConnections( nullptr, LOCAL_CLEANUP );
            fr->GetCanvas()->Refresh();
        }
    }

    int applied = 0;

    for( const json& r : rows )
    {
        if( r["outcome"] != "refused" )
            ++applied;
    }

    json body;
    body["preview"] = aPreview;
    body["rows"] = rows;
    body["applied"] = aPreview ? 0 : applied;
    body["truncated"] = aTruncated;
    body["depth"] = fr ? (int) fr->GetUndoCommandCount() : 0;
    writeRunDone( aDir, "renameNets.json", body );
}
} // namespace nets

// {"run", "dir": "/tmp/cc-renameNets/<n>", "preview": bool, "pads": [{ref, pad, net}] (1 to
// 32,768, kicadBoardPadNets' rows)}. KiCad's Update Schematic from PCB net name pass alone:
// each symbol pin whose schematic net name differs from its pad's has its driving label
// renamed (or a label added, or is refused when a power pin drives the net), symbols matched
// by reference. False (nothing queued) for any other JSON; true once queued on the apply
// coroutine: a preview changes nothing, else ONE commit "Update net names" (one undo step);
// renameNets.json {preview, rows: [{ref, pin, from, to, sheetPath, outcome, label?}],
// applied, truncated, depth} written last.
bool kicadSchRenameNets( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 4
            || !cc_symedit::hasKeys( args, { "run", "dir", "preview", "pads" } )
            || !args["run"].is_number_integer() || args["run"].get<long long>() < 0
            || !args["dir"].is_string() || !args["preview"].is_boolean()
            || !args["pads"].is_array() || args["pads"].empty()
            || args["pads"].size() > nets::PADS_MAX )
    {
        return false;
    }

    const std::string dir = args["dir"].get<std::string>();

    if( !isRunDir( dir, nets::RENAME_ROOT ) )
        return false;

    std::vector<BACK_ANNOTATE::CC_PAD_NET> pads;

    try
    {
        for( const json& p : args["pads"] )
        {
            if( !cc_symedit::onlyKeys( p, { "ref", "pad", "net" } )
                    || !cc_symedit::hasKeys( p, { "ref", "pad", "net" } ) )
            {
                return false;
            }

            std::optional<wxString> ref = cc_symedit::textOf( p["ref"], 1, cc_symedit::TEXT_MAX );
            std::optional<wxString> pad = cc_symedit::textOf( p["pad"], 1, cc_symedit::TEXT_MAX );
            std::optional<wxString> net = cc_symedit::textOf( p["net"], 1, cc_symedit::TEXT_MAX );

            if( !ref || !pad || !net )
                return false;

            pads.push_back( { *ref, *pad, *net } );
        }
    }
    catch( ... )
    {
        return false;
    }

    const bool preview = args["preview"].get<bool>();

    pcbjam_collab::runOnCoroutine( fr, [dir, preview, pads]() {
        nets::runRename( dir, preview, pads, false );
    } );

    return true;
}


// ── st-incanno-e: shift designators (KiCad's Increment Annotations From) ─────────────────

// incrementAnnotations: {"first": "R10", "by": int (1 to 100,000), "allSheets": bool}, those
// three keys only. SCH_EDITOR_CONTROL::IncrementAnnotations' body after its dialog: every
// symbol whose designator has first's prefix and a number at or above first's moves up by
// "by", on the shown sheet or every sheet; ONE commit "Increment Annotations" (none when
// nothing matched). False (nothing queued) for any other JSON, a first with no number, no
// frame or an open in flight. The host waits for kicadCollabBusy to clear, then reads back.
bool kicadSchIncrementAnnotations( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 3
            || !cc_symedit::hasKeys( args, { "first", "by", "allSheets" } )
            || !args["by"].is_number_integer() || !args["allSheets"].is_boolean() )
    {
        return false;
    }

    std::optional<wxString> first = cc_symedit::textOf( args["first"], 2, 64 );
    const long long         by = args["by"].get<long long>();

    if( !first || by < 1 || by > 100000 )
        return false;

    SCH_REFERENCE startRef;
    startRef.SetRef( *first );

    if( !startRef.IsSplitNeeded() )
        return false;

    const bool allSheets = args["allSheets"].get<bool>();
    const int  increment = static_cast<int>( by );
    const wxString firstRef = *first;

    pcbjam_collab::runOnCoroutine( fr, [firstRef, increment, allSheets]() {
        SCH_EDIT_FRAME* frame = schFrame();

        if( !frame )
            return;

        SCH_REFERENCE start;
        start.SetRef( firstRef );

        if( !start.IsSplitNeeded() )
            return;

        start.Split();

        int startNum = atoi( start.GetRefNumber().utf8_string().c_str() );

        SCH_COMMIT         commit( frame );
        SCHEMATIC&         schematic = frame->Schematic();
        SCH_REFERENCE_LIST references;

        if( allSheets )
            schematic.Hierarchy().GetSymbols( references, SYMBOL_FILTER_ALL );
        else
            schematic.CurrentSheet().GetSymbols( references, SYMBOL_FILTER_ALL );

        references.SplitReferences();

        for( SCH_REFERENCE& ref : references )
        {
            if( ref.GetRef() == start.GetRef() )
            {
                int num = atoi( ref.GetRefNumber().utf8_string().c_str() );

                if( num >= startNum )
                {
                    const SCH_SHEET_PATH& sheet = ref.GetSheetPath();
                    wxString              fullRef = ref.GetRef();

                    num += increment;
                    fullRef << num;

                    commit.Modify( ref.GetSymbol(), sheet.LastScreen(), RECURSE_MODE::NO_RECURSE );
                    ref.GetSymbol()->SetRef( &sheet, From_UTF8( fullRef.c_str() ) );
                }
            }
        }

        if( !commit.Empty() )
            commit.Push( _( "Increment Annotations" ) );
    } );

    return true;
}

} // namespace cc_e8

EMSCRIPTEN_BINDINGS( cc_e8_sch )
{
    function( "kicadSchItemGet", &cc_e8::kicadSchItemGet );
    function( "kicadSchItemSet", &cc_e8::kicadSchItemSet );
    function( "kicadSchPlaceImage", &cc_e8::kicadSchPlaceImage );
    function( "kicadHostSchItems", &cc_e8::kicadHostSchItems );
    function( "kicadSheetPinsGet", &cc_e8::kicadSheetPinsGet );
    function( "kicadSheetPinsApply", &cc_e8::kicadSheetPinsApply );
    function( "kicadSheetPinsPlace", &cc_e8::kicadSheetPinsPlace );
    function( "kicadSchRenameNets", &cc_e8::kicadSchRenameNets );
    function( "kicadSchIncrementAnnotations", &cc_e8::kicadSchIncrementAnnotations );
}
