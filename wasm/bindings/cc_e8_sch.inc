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

} // namespace cc_e8

EMSCRIPTEN_BINDINGS( cc_e8_sch )
{
    function( "kicadSchItemGet", &cc_e8::kicadSchItemGet );
    function( "kicadSchItemSet", &cc_e8::kicadSchItemSet );
    function( "kicadSchPlaceImage", &cc_e8::kicadSchPlaceImage );
    function( "kicadHostSchItems", &cc_e8::kicadHostSchItems );
}
