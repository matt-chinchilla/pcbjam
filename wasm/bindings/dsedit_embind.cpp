// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Embind bindings for the drawing sheet editor frame (PL_EDITOR_FRAME, --frame ds) of the merged
// kicad_editor WASM image: engine run 15, the island's dsedit.* ops. Its own TU, compiled by
// scripts/kicad/build-kicad-target.sh with the pagelayout_editor include set and WITHOUT
// -DKICAD_MERGED_EMBIND, linked into the merged image only. Lane e6: ds-e1 (the change hook)
// and ds-e2 (the exports, the drawing sheet addendum 5.2, records section 7), namespace cc_e6,
// its own bindings block cc_e6_dsedit.
//
// House rules (run A 3.4): closed JSON args read whole before anything is queued; false, or ""
// for a read, for any other shape, a missing frame, an open in flight or a dialog up; a change
// runs on the apply coroutine as ONE undo step in KiCad's own order (SaveCopyInUndoList, the
// change, OnModify, HardRedraw: PROPERTIES_FRAME::OnAcceptPrms); a read is synchronous; lengths
// and positions in millimeters as the sheet stores them, angles in degrees. No KiCad dialog,
// message box or info bar on any road.

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <emscripten/bind.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <wx/app.h>
#include <wx/base64.h>
#include <wx/buffer.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/string.h>
#include <wx/toplevel.h>
#include <wx/window.h>

#include <base_screen.h>
#include <base_units.h>
#include <bitmap_base.h>
#include <drawing_sheet/ds_data_item.h>
#include <drawing_sheet/ds_data_model.h>
#include <drawing_sheet/ds_draw_item.h>
#include <font/font.h>
#include <gal/color4d.h>
#include <geometry/eda_angle.h>
#include <kiid.h>
#include <page_info.h>
#include <title_block.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>
#include <view/view.h>

#include "collab_common.h"
#include "open_gate.h"

#include "ds_editor_host.h"
#include "pl_draw_panel_gal.h"
#include "pl_editor_frame.h"
#include "pl_editor_id.h"
#include "tools/pl_selection.h"
#include "tools/pl_selection_tool.h"

using namespace emscripten;

namespace cc_e6
{
// Bumped by the frame's change hook (PL_EDITOR_FRAME::OnModify, below): every change of the
// sheet, by a tool, an undo or redo, or an export, counts once. kicadDsEditState reads it.
static unsigned long long g_dsChangeCount = 0;

unsigned long long dsChangeCount()
{
    return g_dsChangeCount;
}

namespace ds
{
using json = nlohmann::json;
using KIGFX::COLOR4D;

constexpr size_t TEXT_MAX = 1024;
constexpr size_t ITEM_TEXT_MAX = 4096;
constexpr size_t DOC_MAX_BYTES = 8u * 1024u * 1024u;   // a kicad_wks document (spec 5.2)
constexpr size_t PNG_MAX_BYTES = 4u * 1024u * 1024u;   // an armed or added picture, decoded
constexpr size_t UUIDS_MAX = 4096;
constexpr size_t SET_UUIDS_MAX = 512;
constexpr size_t NEW_ITEMS_MAX = 64;
constexpr double POSITION_MAX_MM = 10000.0;   // well past MAX_PAGE_SIZE_EESCHEMA_MILS (3,048 mm)
constexpr int    REPEAT_MAX = 500;
constexpr double LINE_WIDTH_MAX_MM = 10.0;     // CopyPrmsFromPanelToItem's and ToGeneral's bound
constexpr double TEXT_SIZE_MIN_MM = 0.01;      // DLG_MIN_TEXTSIZE (properties_frame.cpp)
constexpr double TEXT_SIZE_MAX_MM = 100.0;     // DLG_MAX_TEXTSIZE
constexpr double TEXT_THICKNESS_MAX_MM = 5.0;
constexpr double MARGIN_MAX_MM = 1000.0;
constexpr double PAGE_MIN_MM = MIN_PAGE_SIZE_MM;
constexpr double PAGE_MAX_MM = MAX_PAGE_SIZE_EESCHEMA_MILS * 25.4 / 1000.0;


// ── the session (one per image: the page's open) ─────────────────────────────────────────────

struct SESSION
{
    bool        open = false;
    std::string token;
    wxString    name;
};

SESSION& session()
{
    static SESSION s;
    return s;
}


// ── the frame ─────────────────────────────────────────────────────────────────────────────────

/** The drawing sheet editor when it is the top window (a ds frame), else nullptr. */
PL_EDITOR_FRAME* dsFrame()
{
    return wxTheApp ? dynamic_cast<PL_EDITOR_FRAME*>( wxTheApp->GetTopWindow() ) : nullptr;
}

/** A shown dialog of any kind (KiCad's or wx's): the frame is not to be driven meanwhile. */
bool dialogUp()
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

/** The frame when it may be asked anything: none while an open is in flight or a dialog is up. */
PL_EDITOR_FRAME* readyFrame()
{
    PL_EDITOR_FRAME* fr = dsFrame();

    if( !fr || !fr->GetToolManager() || pcbjam_open::busy() || dialogUp() )
        return nullptr;

    return fr;
}

/** The ready frame with a sheet the page opened (every export but open and state needs one). */
PL_EDITOR_FRAME* openFrame()
{
    return session().open ? readyFrame() : nullptr;
}

/** The hidden page selector of the top toolbar (Page 1 or Other pages), or nullptr. */
wxChoice* pageChoice( PL_EDITOR_FRAME* aFrame )
{
    if( !aFrame )
        return nullptr;

    return wxDynamicCast( wxWindow::FindWindowById( ID_SELECT_PAGE_NUMBER, aFrame ), wxChoice );
}

PL_SELECTION_TOOL* selectionTool( PL_EDITOR_FRAME* aFrame )
{
    return aFrame && aFrame->GetToolManager()
                   ? aFrame->GetToolManager()->GetTool<PL_SELECTION_TOOL>()
                   : nullptr;
}

DS_DATA_MODEL& model()
{
    return DS_DATA_MODEL::GetTheInstance();
}

std::string dumpJson( const json& aValue )
{
    return aValue.dump( -1, ' ', false, json::error_handler_t::replace );
}

std::string utf8( const wxString& aText )
{
    return std::string( aText.utf8_str() );
}

std::string uuidOf( const DS_DATA_ITEM* aItem )
{
    return utf8( aItem->m_Uuid.AsString() );
}

/** The model's item holding aUuid, or nullptr. */
DS_DATA_ITEM* itemByUuid( const std::string& aUuid )
{
    for( DS_DATA_ITEM* item : model().GetItems() )
    {
        if( item && uuidOf( item ) == aUuid )
            return item;
    }

    return nullptr;
}


// ── JSON readers (closed shapes, bounded values) ──────────────────────────────────────────────

/** An object whose every key is one of aKeys (a key may be missing). */
bool onlyKeys( const json& aObj, std::initializer_list<const char*> aKeys )
{
    if( !aObj.is_object() )
        return false;

    for( auto it = aObj.begin(); it != aObj.end(); ++it )
    {
        bool known = false;

        for( const char* key : aKeys )
        {
            if( it.key() == key )
            {
                known = true;
                break;
            }
        }

        if( !known )
            return false;
    }

    return true;
}

/** An object that holds every one of aKeys. */
bool hasKeys( const json& aObj, std::initializer_list<const char*> aKeys )
{
    if( !aObj.is_object() )
        return false;

    for( const char* key : aKeys )
    {
        if( !aObj.contains( key ) )
            return false;
    }

    return true;
}

/**
 * A JSON string as text of aMin to aMax characters, or nothing: invalid UTF-8, a NUL or a
 * control character refused (a line break only where aLines allows it).
 */
std::optional<wxString> textOf( const json& aValue, size_t aMin, size_t aMax, bool aLines = false )
{
    if( !aValue.is_string() )
        return std::nullopt;

    const std::string& raw = aValue.get_ref<const std::string&>();

    if( raw.find( '\0' ) != std::string::npos )
        return std::nullopt;

    wxString text = wxString::FromUTF8( raw.c_str(), raw.size() );

    if( !raw.empty() && text.IsEmpty() )
        return std::nullopt;

    if( text.length() < aMin || text.length() > aMax )
        return std::nullopt;

    for( wxUniChar c : text )
    {
        if( aLines && c == '\n' )
            continue;

        if( c < 0x20 || c == 0x7f )
            return std::nullopt;
    }

    return text;
}

/** A finite number in [aMin, aMax]. */
std::optional<double> numberOf( const json& aValue, double aMin, double aMax )
{
    if( !aValue.is_number() )
        return std::nullopt;

    const double v = aValue.get<double>();

    if( !std::isfinite( v ) || v < aMin || v > aMax )
        return std::nullopt;

    return v;
}

/** An integer in [aMin, aMax]. */
std::optional<int> intOf( const json& aValue, int aMin, int aMax )
{
    if( !aValue.is_number_integer() )
        return std::nullopt;

    const long long v = aValue.get<long long>();

    if( v < aMin || v > aMax )
        return std::nullopt;

    return static_cast<int>( v );
}

std::optional<bool> boolOf( const json& aValue )
{
    if( !aValue.is_boolean() )
        return std::nullopt;

    return aValue.get<bool>();
}

/** A page token: 1 to 64 printable ASCII characters. */
std::optional<std::string> tokenOf( const json& aValue )
{
    if( !aValue.is_string() )
        return std::nullopt;

    const std::string& t = aValue.get_ref<const std::string&>();

    if( t.empty() || t.size() > 64 )
        return std::nullopt;

    for( char c : t )
    {
        if( c < 0x20 || c > 0x7e )
            return std::nullopt;
    }

    return t;
}

/** A lowercase 8-4-4-4-12 uuid. */
bool isLowerUuid( const std::string& aText )
{
    if( aText.size() != 36 )
        return false;

    for( size_t i = 0; i < aText.size(); ++i )
    {
        const char c = aText[i];

        if( i == 8 || i == 13 || i == 18 || i == 23 )
        {
            if( c != '-' )
                return false;
        }
        else if( !( ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'f' ) ) )
        {
            return false;
        }
    }

    return true;
}

/** A list of lowercase uuids, aMin to aMax of them, no repeats. */
std::optional<std::vector<std::string>> uuidsOf( const json& aValue, size_t aMin, size_t aMax )
{
    if( !aValue.is_array() || aValue.size() < aMin || aValue.size() > aMax )
        return std::nullopt;

    std::vector<std::string> out;
    std::set<std::string>    seen;

    for( const json& u : aValue )
    {
        if( !u.is_string() || !isLowerUuid( u.get<std::string>() ) )
            return std::nullopt;

        if( !seen.insert( u.get<std::string>() ).second )
            return std::nullopt;

        out.push_back( u.get<std::string>() );
    }

    return out;
}

/** A pair of numbers [a, b], each in [aMin, aMax]. */
std::optional<VECTOR2D> pairOf( const json& aValue, double aMin, double aMax )
{
    if( !aValue.is_array() || aValue.size() != 2 )
        return std::nullopt;

    std::optional<double> a = numberOf( aValue[0], aMin, aMax );
    std::optional<double> b = numberOf( aValue[1], aMin, aMax );

    if( !a || !b )
        return std::nullopt;

    return VECTOR2D( *a, *b );
}


// ── the records' words (section 7.2) ──────────────────────────────────────────────────────────

const char* kindName( DS_DATA_ITEM::DS_ITEM_TYPE aType )
{
    switch( aType )
    {
    case DS_DATA_ITEM::DS_TEXT:        return "text";
    case DS_DATA_ITEM::DS_SEGMENT:     return "line";
    case DS_DATA_ITEM::DS_RECT:        return "rect";
    case DS_DATA_ITEM::DS_POLYPOLYGON: return "polygon";
    case DS_DATA_ITEM::DS_BITMAP:      return "bitmap";
    }

    return "";
}

std::optional<DS_DATA_ITEM::DS_ITEM_TYPE> kindOf( const json& aValue )
{
    if( !aValue.is_string() )
        return std::nullopt;

    const std::string& k = aValue.get_ref<const std::string&>();

    if( k == "text" )    return DS_DATA_ITEM::DS_TEXT;
    if( k == "line" )    return DS_DATA_ITEM::DS_SEGMENT;
    if( k == "rect" )    return DS_DATA_ITEM::DS_RECT;
    if( k == "polygon" ) return DS_DATA_ITEM::DS_POLYPOLYGON;
    if( k == "bitmap" )  return DS_DATA_ITEM::DS_BITMAP;

    return std::nullopt;
}

// The file's own corner tokens (ds_data_model_io.cpp), rbcorner KiCad's default.
const char* cornerName( int aAnchor )
{
    switch( aAnchor )
    {
    case RT_CORNER: return "rtcorner";
    case LB_CORNER: return "lbcorner";
    case LT_CORNER: return "ltcorner";
    default:        return "rbcorner";
    }
}

std::optional<CORNER_ANCHOR> cornerOf( const json& aValue )
{
    if( !aValue.is_string() )
        return std::nullopt;

    const std::string& c = aValue.get_ref<const std::string&>();

    if( c == "rbcorner" ) return RB_CORNER;
    if( c == "rtcorner" ) return RT_CORNER;
    if( c == "lbcorner" ) return LB_CORNER;
    if( c == "ltcorner" ) return LT_CORNER;

    return std::nullopt;
}

const char* pagesName( PAGE_OPTION aOption )
{
    switch( aOption )
    {
    case FIRST_PAGE_ONLY:  return "page1only";
    case SUBSEQUENT_PAGES: return "notonpage1";
    default:               return "all";
    }
}

std::optional<PAGE_OPTION> pagesOf( const json& aValue )
{
    if( !aValue.is_string() )
        return std::nullopt;

    const std::string& p = aValue.get_ref<const std::string&>();

    if( p == "all" )        return ALL_PAGES;
    if( p == "page1only" )  return FIRST_PAGE_ONLY;
    if( p == "notonpage1" ) return SUBSEQUENT_PAGES;

    return std::nullopt;
}

const char* hJustifyName( GR_TEXT_H_ALIGN_T aAlign )
{
    switch( aAlign )
    {
    case GR_TEXT_H_ALIGN_CENTER: return "center";
    case GR_TEXT_H_ALIGN_RIGHT:  return "right";
    default:                     return "left";
    }
}

std::optional<GR_TEXT_H_ALIGN_T> hJustifyOf( const json& aValue )
{
    if( !aValue.is_string() )
        return std::nullopt;

    const std::string& j = aValue.get_ref<const std::string&>();

    if( j == "left" )   return GR_TEXT_H_ALIGN_LEFT;
    if( j == "center" ) return GR_TEXT_H_ALIGN_CENTER;
    if( j == "right" )  return GR_TEXT_H_ALIGN_RIGHT;

    return std::nullopt;
}

const char* vJustifyName( GR_TEXT_V_ALIGN_T aAlign )
{
    switch( aAlign )
    {
    case GR_TEXT_V_ALIGN_TOP:    return "top";
    case GR_TEXT_V_ALIGN_BOTTOM: return "bottom";
    default:                     return "center";
    }
}

std::optional<GR_TEXT_V_ALIGN_T> vJustifyOf( const json& aValue )
{
    if( !aValue.is_string() )
        return std::nullopt;

    const std::string& j = aValue.get_ref<const std::string&>();

    if( j == "top" )    return GR_TEXT_V_ALIGN_TOP;
    if( j == "center" ) return GR_TEXT_V_ALIGN_CENTER;
    if( j == "bottom" ) return GR_TEXT_V_ALIGN_BOTTOM;

    return std::nullopt;
}

/** "" for KiCad's unspecified color, else #rrggbbaa (the file stores r, g, b and an alpha). */
std::string colorName( const COLOR4D& aColor )
{
    if( aColor == COLOR4D::UNSPECIFIED )
        return std::string();

    auto byte = []( double v ) -> int
            {
                return std::max( 0, std::min( 255, (int) std::lround( v * 255.0 ) ) );
            };

    char buf[16];
    std::snprintf( buf, sizeof( buf ), "#%02x%02x%02x%02x", byte( aColor.r ), byte( aColor.g ),
                   byte( aColor.b ), byte( aColor.a ) );
    return std::string( buf );
}

std::optional<COLOR4D> colorOf( const json& aValue )
{
    if( !aValue.is_string() )
        return std::nullopt;

    const std::string& c = aValue.get_ref<const std::string&>();

    if( c.empty() )
        return COLOR4D::UNSPECIFIED;

    if( c.size() != 9 || c[0] != '#' )
        return std::nullopt;

    int part[4];

    for( int i = 0; i < 4; ++i )
    {
        int v = 0;

        for( int j = 0; j < 2; ++j )
        {
            const char h = c[1 + i * 2 + j];
            int        d;

            if( h >= '0' && h <= '9' )      d = h - '0';
            else if( h >= 'a' && h <= 'f' ) d = h - 'a' + 10;
            else                            return std::nullopt;

            v = v * 16 + d;
        }

        part[i] = v;
    }

    return COLOR4D( part[0] / 255.0, part[1] / 255.0, part[2] / 255.0, part[3] / 255.0 );
}

json pointJson( const POINT_COORD& aCoord )
{
    json p = json::object();
    p["x"] = aCoord.m_Pos.x;
    p["y"] = aCoord.m_Pos.y;
    p["corner"] = cornerName( aCoord.m_Anchor );
    return p;
}

/** A DsPoint {x, y, corner}, those three keys only. */
std::optional<POINT_COORD> pointOf( const json& aValue )
{
    if( !onlyKeys( aValue, { "x", "y", "corner" } ) || !hasKeys( aValue, { "x", "y", "corner" } ) )
        return std::nullopt;

    std::optional<double>        x = numberOf( aValue["x"], -POSITION_MAX_MM, POSITION_MAX_MM );
    std::optional<double>        y = numberOf( aValue["y"], -POSITION_MAX_MM, POSITION_MAX_MM );
    std::optional<CORNER_ANCHOR> corner = cornerOf( aValue["corner"] );

    if( !x || !y || !corner )
        return std::nullopt;

    return POINT_COORD( VECTOR2D( *x, *y ), *corner );
}

json pairJson( const VECTOR2D& aValue )
{
    json p = json::array();
    p.push_back( aValue.x );
    p.push_back( aValue.y );
    return p;
}


// ── items (section 7.2) ───────────────────────────────────────────────────────────────────────

json itemJson( DS_DATA_ITEM* aItem )
{
    json out = json::object();

    out["kind"] = kindName( aItem->GetType() );
    out["uuid"] = uuidOf( aItem );
    out["name"] = utf8( aItem->m_Name );
    out["comment"] = utf8( aItem->m_Info );
    out["pages"] = pagesName( aItem->GetPage1Option() );
    out["start"] = pointJson( aItem->m_Pos );
    out["repeat"] = aItem->m_RepeatCount;
    out["step"] = pairJson( aItem->m_IncrementVector );

    switch( aItem->GetType() )
    {
    case DS_DATA_ITEM::DS_SEGMENT:
    case DS_DATA_ITEM::DS_RECT:
        out["end"] = pointJson( aItem->m_End );
        out["lineWidth"] = aItem->m_LineWidth;
        break;

    case DS_DATA_ITEM::DS_TEXT:
    {
        DS_DATA_ITEM_TEXT* text = static_cast<DS_DATA_ITEM_TEXT*>( aItem );

        out["text"] = utf8( text->m_TextBase );
        out["labelStep"] = text->m_IncrementLabel;
        out["font"] = text->m_Font ? utf8( text->m_Font->GetName() ) : std::string();
        out["bold"] = text->m_Bold;
        out["italic"] = text->m_Italic;
        out["color"] = colorName( text->m_TextColor );
        out["hJustify"] = hJustifyName( text->m_Hjustify );
        out["vJustify"] = vJustifyName( text->m_Vjustify );
        out["rotation"] = text->m_Orient;
        out["size"] = pairJson( text->m_TextSize );
        out["maxLength"] = text->m_BoundingBoxSize.x;
        out["maxHeight"] = text->m_BoundingBoxSize.y;
        out["lineWidth"] = text->m_LineWidth;
        break;
    }

    case DS_DATA_ITEM::DS_POLYPOLYGON:
    {
        DS_DATA_ITEM_POLYGONS* poly = static_cast<DS_DATA_ITEM_POLYGONS*>( aItem );

        out["rotation"] = poly->m_Orient.AsDegrees();
        out["lineWidth"] = poly->m_LineWidth;
        out["outlines"] = poly->GetPolyCount();
        out["points"] = (int) poly->m_Corners.size();
        break;
    }

    case DS_DATA_ITEM::DS_BITMAP:
    {
        DS_DATA_ITEM_BITMAP* bitmap = static_cast<DS_DATA_ITEM_BITMAP*>( aItem );
        VECTOR2I             px( 0, 0 );

        if( bitmap->m_ImageBitmap )
            px = bitmap->m_ImageBitmap->GetSizePixels();

        out["ppi"] = bitmap->GetPPI();
        out["width"] = px.x;
        out["height"] = px.y;
        break;
    }
    }

    return out;
}

/** The keys a DsItemSet may carry for one kind (never kind, uuid, outlines, points, width, height). */
bool keyFitsKind( const std::string& aKey, DS_DATA_ITEM::DS_ITEM_TYPE aType )
{
    static const std::set<std::string> common = { "name", "comment", "pages", "start", "repeat",
                                                   "step" };
    static const std::set<std::string> line = { "end", "lineWidth" };
    static const std::set<std::string> text = { "text", "labelStep", "font", "bold", "italic",
                                                "color", "hJustify", "vJustify", "rotation",
                                                "size", "maxLength", "maxHeight", "lineWidth" };
    static const std::set<std::string> polygon = { "rotation", "lineWidth" };
    static const std::set<std::string> bitmap = { "ppi" };

    if( common.count( aKey ) )
        return true;

    switch( aType )
    {
    case DS_DATA_ITEM::DS_SEGMENT:
    case DS_DATA_ITEM::DS_RECT:        return line.count( aKey ) > 0;
    case DS_DATA_ITEM::DS_TEXT:        return text.count( aKey ) > 0;
    case DS_DATA_ITEM::DS_POLYPOLYGON: return polygon.count( aKey ) > 0;
    case DS_DATA_ITEM::DS_BITMAP:      return bitmap.count( aKey ) > 0;
    }

    return false;
}

/**
 * A DsItemSet read whole: every key known and of its shape, bounded as the properties pane
 * bounds it. The values are applied by applySet, on the coroutine.
 */
struct ITEM_SET
{
    std::optional<wxString>          name, comment, text, font;
    std::optional<PAGE_OPTION>       pages;
    std::optional<POINT_COORD>       start, end;
    std::optional<int>               repeat, labelStep, ppi;
    std::optional<VECTOR2D>          step, size;
    std::optional<double>            lineWidth, rotation, maxLength, maxHeight;
    std::optional<bool>              bold, italic;
    std::optional<COLOR4D>           color;
    std::optional<GR_TEXT_H_ALIGN_T> hJustify;
    std::optional<GR_TEXT_V_ALIGN_T> vJustify;
    std::vector<std::string>         keys;
};

std::optional<ITEM_SET> itemSetOf( const json& aSet )
{
    if( !aSet.is_object() || aSet.empty() )
        return std::nullopt;

    ITEM_SET s;

    for( auto it = aSet.begin(); it != aSet.end(); ++it )
    {
        const std::string& k = it.key();
        const json&        v = it.value();
        bool               ok = true;

        if( k == "name" )           ok = ( s.name = textOf( v, 0, TEXT_MAX ) ).has_value();
        else if( k == "comment" )   ok = ( s.comment = textOf( v, 0, TEXT_MAX ) ).has_value();
        else if( k == "pages" )     ok = ( s.pages = pagesOf( v ) ).has_value();
        else if( k == "start" )     ok = ( s.start = pointOf( v ) ).has_value();
        else if( k == "end" )       ok = ( s.end = pointOf( v ) ).has_value();
        else if( k == "repeat" )    ok = ( s.repeat = intOf( v, 1, REPEAT_MAX ) ).has_value();
        else if( k == "step" )
            ok = ( s.step = pairOf( v, -POSITION_MAX_MM, POSITION_MAX_MM ) ).has_value();
        else if( k == "lineWidth" )
            ok = ( s.lineWidth = numberOf( v, 0.0, LINE_WIDTH_MAX_MM ) ).has_value();
        else if( k == "text" )      ok = ( s.text = textOf( v, 0, ITEM_TEXT_MAX, true ) ).has_value();
        else if( k == "labelStep" ) ok = ( s.labelStep = intOf( v, -100000, 100000 ) ).has_value();
        else if( k == "font" )      ok = ( s.font = textOf( v, 0, TEXT_MAX ) ).has_value();
        else if( k == "bold" )      ok = ( s.bold = boolOf( v ) ).has_value();
        else if( k == "italic" )    ok = ( s.italic = boolOf( v ) ).has_value();
        else if( k == "color" )     ok = ( s.color = colorOf( v ) ).has_value();
        else if( k == "hJustify" )  ok = ( s.hJustify = hJustifyOf( v ) ).has_value();
        else if( k == "vJustify" )  ok = ( s.vJustify = vJustifyOf( v ) ).has_value();
        else if( k == "rotation" )  ok = ( s.rotation = numberOf( v, -360.0, 360.0 ) ).has_value();
        else if( k == "size" )
            ok = ( s.size = pairOf( v, 0.0, TEXT_SIZE_MAX_MM ) ).has_value();
        else if( k == "maxLength" )
            ok = ( s.maxLength = numberOf( v, 0.0, POSITION_MAX_MM ) ).has_value();
        else if( k == "maxHeight" )
            ok = ( s.maxHeight = numberOf( v, 0.0, POSITION_MAX_MM ) ).has_value();
        else if( k == "ppi" )       ok = ( s.ppi = intOf( v, 1, 100000 ) ).has_value();
        else                        ok = false;

        if( !ok )
            return std::nullopt;

        s.keys.push_back( k );
    }

    return s;
}

/**
 * The set on one item, in PROPERTIES_FRAME::CopyPrmsFromPanelToItem's order (comment, pages,
 * line width, start, end, repeat, step, then the text's, the polygon's and the bitmap's own),
 * the name (not on the pane: the file's own field) first. Only the keys the set carries.
 */
void applySet( DS_DATA_ITEM* aItem, const ITEM_SET& aSet )
{
    if( aSet.name )
        aItem->m_Name = *aSet.name;

    if( aSet.comment )
        aItem->m_Info = *aSet.comment;

    if( aSet.pages )
        aItem->SetPage1Option( *aSet.pages );

    if( aSet.lineWidth )
        aItem->m_LineWidth = *aSet.lineWidth;

    if( aSet.start )
        aItem->m_Pos = *aSet.start;

    if( aSet.end )
        aItem->m_End = *aSet.end;

    if( aSet.repeat )
        aItem->m_RepeatCount = *aSet.repeat;

    if( aSet.step )
        aItem->m_IncrementVector = *aSet.step;

    if( aItem->GetType() == DS_DATA_ITEM::DS_TEXT )
    {
        DS_DATA_ITEM_TEXT* item = static_cast<DS_DATA_ITEM_TEXT*>( aItem );

        if( aSet.text )
            item->m_TextBase = *aSet.text;

        if( aSet.labelStep )
            item->m_IncrementLabel = *aSet.labelStep;

        if( aSet.bold )
            item->m_Bold = *aSet.bold;

        if( aSet.italic )
            item->m_Italic = *aSet.italic;

        if( aSet.color )
            item->m_TextColor = *aSet.color;

        if( aSet.hJustify )
            item->m_Hjustify = *aSet.hJustify;

        if( aSet.vJustify )
            item->m_Vjustify = *aSet.vJustify;

        // The pane's font control: a face by name (bold and italic as now set), "" KiCad's own.
        if( aSet.font )
        {
            item->m_Font = aSet.font->IsEmpty()
                                   ? nullptr
                                   : KIFONT::FONT::GetFont( *aSet.font, item->m_Bold,
                                                            item->m_Italic );
        }
        else if( ( aSet.bold || aSet.italic ) && item->m_Font
                 && !item->m_Font->GetName().IsEmpty() )
        {
            item->m_Font = KIFONT::FONT::GetFont( item->m_Font->GetName(), item->m_Bold,
                                                  item->m_Italic );
        }

        if( aSet.rotation )
            item->m_Orient = *aSet.rotation;

        if( aSet.size )
            item->m_TextSize = *aSet.size;

        if( aSet.maxLength )
            item->m_BoundingBoxSize.x = *aSet.maxLength;

        if( aSet.maxHeight )
            item->m_BoundingBoxSize.y = *aSet.maxHeight;
    }

    if( aItem->GetType() == DS_DATA_ITEM::DS_POLYPOLYGON && aSet.rotation )
    {
        static_cast<DS_DATA_ITEM_POLYGONS*>( aItem )->m_Orient =
                EDA_ANGLE( *aSet.rotation, DEGREES_T );
    }

    if( aItem->GetType() == DS_DATA_ITEM::DS_BITMAP && aSet.ppi )
        static_cast<DS_DATA_ITEM_BITMAP*>( aItem )->SetPPI( *aSet.ppi );
}


// ── KiCad's own parser on a scratch model (the open's and the append's first step) ────────────

/**
 * A sheet's text parsed by KiCad's DRAWING_SHEET_PARSER into a model of its own, the frame's
 * untouched. DS_DATA_MODEL::SetPageLayout swallows every parse error ("best efforts"), so the
 * text goes through DS_DATA_MODEL::LoadDrawingSheet, which answers KiCad's IO_ERROR text (the
 * source and the line) and false, from a scratch file in the engine's memory file system named
 * after the sheet (the message names it). The file is removed after the read.
 */
struct PARSED
{
    bool                           ok = false;
    wxString                       message;
    std::unique_ptr<DS_DATA_MODEL> sheet;
};

PARSED parseScratch( const std::string& aText, const wxString& aName )
{
    PARSED out;

    wxString base = wxFileName( aName ).GetFullName();
    bool     plain = !base.IsEmpty() && base.length() <= 128;

    for( wxUniChar c : base )
    {
        if( !( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' )
               || c == '.' || c == '_' || c == '-' ) )
        {
            plain = false;
        }
    }

    if( !plain || base.StartsWith( wxS( "." ) ) )
        base = wxS( "sheet.kicad_wks" );

    const wxString dir = wxS( "/tmp/cc-dsedit" );

    if( !wxFileName::DirExists( dir ) && !wxFileName::Mkdir( dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL ) )
    {
        out.message = wxS( "The engine could not write its scratch copy of the sheet." );
        return out;
    }

    const wxString path = dir + wxS( "/" ) + base;

    {
        wxFFile file( path, wxS( "wb" ) );

        if( !file.IsOpened() || ( !aText.empty() && file.Write( aText.data(), aText.size() ) != aText.size() ) )
        {
            out.message = wxS( "The engine could not write its scratch copy of the sheet." );
            return out;
        }
    }

    out.sheet = std::make_unique<DS_DATA_MODEL>();
    out.ok = out.sheet->LoadDrawingSheet( path, &out.message, false );
    wxRemoveFile( path );

    if( !out.ok )
        out.sheet.reset();

    return out;
}

/** The model's defaults and margins as a new model holds them (a fresh pl_editor's). */
void resetSetup( DS_DATA_MODEL& aModel )
{
    DS_DATA_MODEL fresh;

    aModel.m_DefaultLineWidth = fresh.m_DefaultLineWidth;
    aModel.m_DefaultTextSize = fresh.m_DefaultTextSize;
    aModel.m_DefaultTextThickness = fresh.m_DefaultTextThickness;
    aModel.SetLeftMargin( fresh.GetLeftMargin() );
    aModel.SetRightMargin( fresh.GetRightMargin() );
    aModel.SetTopMargin( fresh.GetTopMargin() );
    aModel.SetBottomMargin( fresh.GetBottomMargin() );
}


// ── the state (section 7.1) ───────────────────────────────────────────────────────────────────

json stateJson( PL_EDITOR_FRAME* aFrame )
{
    wxChoice* choice = pageChoice( aFrame );
    json      out = json::object();

    out["token"] = session().token;
    out["open"] = session().open;
    out["name"] = utf8( session().name );
    out["modified"] = session().open && aFrame && aFrame->IsContentModified();
    out["depth"] = aFrame ? aFrame->GetUndoCommandCount() : 0;
    out["page"] = choice && choice->GetSelection() == 1 ? 2 : 1;
    out["values"] = !model().m_EditMode;
    out["items"] = (int) model().GetCount();
    return out;
}

/** The page shown: Page 1 or Other pages on the hidden selector, then OnSelectPage's body. */
void showPage( PL_EDITOR_FRAME* aFrame, int aPage )
{
    if( wxChoice* choice = pageChoice( aFrame ) )
    {
        choice->SetSelection( aPage == 2 ? 1 : 0 );

        wxCommandEvent evt( wxEVT_CHOICE, ID_SELECT_PAGE_NUMBER );
        aFrame->OnSelectPage( evt );
    }
}


// ── the edits: one undo step each, on the apply coroutine ─────────────────────────────────────

/**
 * Queues one edit of the open sheet. On the coroutine the frame and the page's open are
 * re-checked (a sheet opened since is never edited by an older call) and aStillFits re-reads
 * what the call validated (an undo since may have dropped an item); then KiCad's own order:
 * SaveCopyInUndoList, the change, OnModify, HardRedraw (PROPERTIES_FRAME::OnAcceptPrms).
 * False, nothing queued, with no open sheet on a ready frame.
 */
bool queueEdit( std::function<bool()> aStillFits, std::function<void( PL_EDITOR_FRAME* )> aChange )
{
    PL_EDITOR_FRAME* fr = openFrame();

    if( !fr || !aChange )
        return false;

    const std::string token = session().token;

    pcbjam_collab::runOnCoroutine( fr, [token, aStillFits, aChange]() {
        PL_EDITOR_FRAME* frame = dsFrame();   // re-resolved on the coroutine

        if( !frame || !session().open || session().token != token )
            return;

        if( aStillFits && !aStillFits() )
            return;

        frame->SaveCopyInUndoList();
        aChange( frame );
        frame->OnModify();
        frame->HardRedraw();
    } );

    return true;
}

/** Every uuid held by the model, and of a kind each of aKeys fits (aKeys empty: any kind). */
bool uuidsFit( const std::vector<std::string>& aUuids, const std::vector<std::string>& aKeys )
{
    for( const std::string& u : aUuids )
    {
        DS_DATA_ITEM* item = itemByUuid( u );

        if( !item )
            return false;

        for( const std::string& k : aKeys )
        {
            if( !keyFitsKind( k, item->GetType() ) )
                return false;
        }
    }

    return true;
}


// ── pictures (base64 PNG, the armed one and the added ones) ───────────────────────────────────

/** The decoded bytes of a base64 PNG of at most PNG_MAX_BYTES that KiCad's reader takes. */
std::optional<std::vector<uint8_t>> pngOf( const json& aValue )
{
    if( !aValue.is_string() )
        return std::nullopt;

    const std::string& b64 = aValue.get_ref<const std::string&>();

    if( b64.empty() || b64.size() > ( PNG_MAX_BYTES / 3 + 1 ) * 4 )
        return std::nullopt;

    size_t         posErr = 0;
    wxMemoryBuffer buf = wxBase64Decode( b64.c_str(), b64.size(), wxBase64DecodeMode_Strict,
                                         &posErr );

    static const uint8_t signature[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };

    if( buf.GetDataLen() < 8 || buf.GetDataLen() > PNG_MAX_BYTES
            || std::memcmp( buf.GetData(), signature, 8 ) != 0 )
    {
        return std::nullopt;
    }

    // KiCad's own reader must take it (the one Place Bitmaps and the add run).
    BITMAP_BASE trial;

    if( !trial.ReadImageFile( buf ) )
        return std::nullopt;

    const uint8_t* data = static_cast<const uint8_t*>( buf.GetData() );
    return std::vector<uint8_t>( data, data + buf.GetDataLen() );
}

/** A new DS_DATA_ITEM_BITMAP of aPng, scaled for the drawing sheet as AddDrawingSheetItem does. */
DS_DATA_ITEM_BITMAP* newBitmapItem( const std::vector<uint8_t>& aPng )
{
    wxMemoryBuffer buf;
    buf.AppendData( aPng.data(), aPng.size() );

    BITMAP_BASE* image = new BITMAP_BASE();

    if( !image->ReadImageFile( buf ) )
    {
        delete image;
        return nullptr;
    }

    image->SetPixelSizeIu( drawSheetIUScale.IU_PER_MILS * 1000.0 / image->GetPPI() );
    return new DS_DATA_ITEM_BITMAP( image );
}


// ── the exports (spec 5.2) ────────────────────────────────────────────────────────────────────

json errorJson( const char* aCode, const wxString& aMessage = wxEmptyString )
{
    json out = json::object();
    out["error"] = aCode;

    if( !aMessage.IsEmpty() )
        out["message"] = utf8( aMessage );

    return out;
}

// open: {source: "text" | "default" | "empty", text, name, discard, token}, text only (and
// required) for "text". The text is parsed first on a scratch model ({error: "unreadable",
// message} and nothing changes); then the frame's model takes it in place of what it held
// (SetPageLayout, not appended), the title names it (nothing is written there), and
// OnNewDrawingSheet's body runs: undo cleared, unmodified, page 1, values shown. Answers
// {error: ""} once queued; the state's token switches when it is opened.
std::string dsEditOpen( std::string aArgsJson )
{
    PL_EDITOR_FRAME* fr = readyFrame();

    if( !fr )
        return "";

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded()
            || !onlyKeys( args, { "source", "text", "name", "discard", "token" } )
            || !hasKeys( args, { "source", "name", "discard", "token" } )
            || !args["source"].is_string() )
    {
        return "";
    }

    const std::string          source = args["source"].get<std::string>();
    std::optional<wxString>    name = textOf( args["name"], 0, TEXT_MAX );
    std::optional<bool>        discard = boolOf( args["discard"] );
    std::optional<std::string> token = tokenOf( args["token"] );

    if( ( source != "text" && source != "default" && source != "empty" ) || !name || !discard
            || !token )
    {
        return "";
    }

    if( source == "text" ? !( args.contains( "text" ) && args["text"].is_string() )
                         : args.contains( "text" ) )
    {
        return "";
    }

    if( source == "text" && args["text"].get_ref<const std::string&>().size() > DOC_MAX_BYTES )
        return dumpJson( errorJson( "too_large" ) );

    if( !*discard && fr->IsContentModified() )
        return dumpJson( errorJson( "held_edits" ) );

    std::string text;

    if( source == "text" )
    {
        text = args["text"].get<std::string>();

        PARSED parsed = parseScratch( text, *name );

        if( !parsed.ok )
            return dumpJson( errorJson( "unreadable", parsed.message ) );
    }

    const wxString    sheetName = *name;
    const std::string t = *token;

    pcbjam_collab::runOnCoroutine( fr, [source, text, sheetName, t]() {
        PL_EDITOR_FRAME* frame = dsFrame();

        if( !frame )
            return;

        DS_DATA_MODEL& m = model();

        m.AllowVoidList( true );
        resetSetup( m );

        if( source == "default" )
            m.SetDefaultLayout();
        else if( source == "empty" )
            m.SetEmptyLayout();
        else
            m.SetPageLayout( text.c_str(), false, sheetName );

        frame->SetCurrentFileName( sheetName );

        SESSION& s = session();
        s.open = true;
        s.token = t;
        s.name = sheetName;

        m.m_EditMode = false;      // values shown (KiCad's preview mode)
        frame->OnNewDrawingSheet();
        showPage( frame, 1 );
        frame->HardRedraw();
    } );

    return dumpJson( errorJson( "" ) );
}

std::string dsEditState()
{
    PL_EDITOR_FRAME* fr = dsFrame();

    if( !fr )
        return "";

    return dumpJson( stateJson( fr ) );
}

// take: {name, text}, the frame's model by KiCad's own formatter (SaveInString: its header,
// version and generator). A pure read.
std::string dsEditTake()
{
    PL_EDITOR_FRAME* fr = openFrame();

    if( !fr )
        return "";

    wxString text;
    model().SaveInString( &text );

    json out = json::object();
    out["name"] = utf8( session().name );
    out["text"] = utf8( text );
    return dumpJson( out );
}

// markSaved: clears the modified flag when aToken is the open's. No undo step.
bool dsEditMarkSaved( std::string aToken )
{
    PL_EDITOR_FRAME* fr = openFrame();

    if( !fr || aToken.empty() || aToken != session().token || !fr->GetScreen() )
        return false;

    fr->GetScreen()->SetContentModified( false );
    fr->UpdateTitleAndInfo();
    return true;
}

// items.get: {uuids?: string[1..4096], kinds?: DsKind[1..5]}; {items} in the model's order.
std::string dsEditItemsGet( std::string aArgsJson )
{
    if( !openFrame() )
        return "";

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !onlyKeys( args, { "uuids", "kinds" } ) )
        return "";

    std::optional<std::vector<std::string>> uuids;
    std::set<int>                           kinds;

    if( args.contains( "uuids" ) )
    {
        uuids = uuidsOf( args["uuids"], 1, UUIDS_MAX );

        if( !uuids )
            return "";
    }

    if( args.contains( "kinds" ) )
    {
        if( !args["kinds"].is_array() || args["kinds"].empty() || args["kinds"].size() > 5 )
            return "";

        for( const json& k : args["kinds"] )
        {
            std::optional<DS_DATA_ITEM::DS_ITEM_TYPE> type = kindOf( k );

            if( !type || !kinds.insert( (int) *type ).second )
                return "";
        }
    }

    std::set<std::string> wanted;

    if( uuids )
        wanted.insert( uuids->begin(), uuids->end() );

    json items = json::array();

    for( DS_DATA_ITEM* item : model().GetItems() )
    {
        if( !item )
            continue;

        if( uuids && !wanted.count( uuidOf( item ) ) )
            continue;

        if( !kinds.empty() && !kinds.count( (int) item->GetType() ) )
            continue;

        items.push_back( itemJson( item ) );
    }

    json out = json::object();
    out["items"] = std::move( items );
    return dumpJson( out );
}

// items.set: {uuids: string[1..512], set: DsItemSet}: every uuid held, every key of the set
// one every item's kind has; the pane's setters in CopyPrmsFromPanelToItem's order; one undo
// step.
bool dsEditItemsSet( std::string aArgsJson )
{
    if( !openFrame() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !onlyKeys( args, { "uuids", "set" } )
            || !hasKeys( args, { "uuids", "set" } ) )
    {
        return false;
    }

    std::optional<std::vector<std::string>> uuids = uuidsOf( args["uuids"], 1, SET_UUIDS_MAX );
    std::optional<ITEM_SET>                 set = itemSetOf( args["set"] );

    if( !uuids || !set || !uuidsFit( *uuids, set->keys ) )
        return false;

    const std::vector<std::string> ids = *uuids;
    const ITEM_SET                 values = *set;

    return queueEdit(
            [ids, values]()
            {
                return uuidsFit( ids, values.keys );
            },
            [ids, values]( PL_EDITOR_FRAME* )
            {
                for( const std::string& u : ids )
                {
                    if( DS_DATA_ITEM* item = itemByUuid( u ) )
                        applySet( item, values );
                }
            } );
}

/** One DsNewItem read whole, ready to be built on the coroutine. */
struct NEW_ITEM
{
    DS_DATA_ITEM::DS_ITEM_TYPE type = DS_DATA_ITEM::DS_TEXT;
    std::string                uuid;
    ITEM_SET                   set;
    std::vector<uint8_t>       png;
};

std::optional<NEW_ITEM> newItemOf( const json& aItem )
{
    if( !aItem.is_object() || !aItem.contains( "kind" ) )
        return std::nullopt;

    std::optional<DS_DATA_ITEM::DS_ITEM_TYPE> type = kindOf( aItem["kind"] );

    if( !type || *type == DS_DATA_ITEM::DS_POLYPOLYGON )   // polygons arrive by append only
        return std::nullopt;

    bool whole = false;

    switch( *type )
    {
    case DS_DATA_ITEM::DS_SEGMENT:
    case DS_DATA_ITEM::DS_RECT:
        whole = onlyKeys( aItem, { "kind", "uuid", "name", "comment", "pages", "start", "repeat",
                                   "step", "end", "lineWidth" } )
                && aItem.size() == 10;
        break;

    case DS_DATA_ITEM::DS_TEXT:
        whole = onlyKeys( aItem, { "kind", "uuid", "name", "comment", "pages", "start", "repeat",
                                   "step", "text", "labelStep", "font", "bold", "italic",
                                   "color", "hJustify", "vJustify", "rotation", "size",
                                   "maxLength", "maxHeight", "lineWidth" } )
                && aItem.size() == 21;
        break;

    case DS_DATA_ITEM::DS_BITMAP:
        whole = onlyKeys( aItem, { "kind", "uuid", "name", "comment", "pages", "start", "repeat",
                                   "step", "ppi", "png" } )
                && aItem.size() == 10;
        break;

    default:
        break;
    }

    if( !whole || !aItem["uuid"].is_string() || !isLowerUuid( aItem["uuid"].get<std::string>() ) )
        return std::nullopt;

    json values = aItem;
    values.erase( "kind" );
    values.erase( "uuid" );
    values.erase( "png" );

    std::optional<ITEM_SET> set = itemSetOf( values );

    if( !set )
        return std::nullopt;

    NEW_ITEM out;
    out.type = *type;
    out.uuid = aItem["uuid"].get<std::string>();
    out.set = *set;

    if( *type == DS_DATA_ITEM::DS_BITMAP )
    {
        std::optional<std::vector<uint8_t>> png = pngOf( aItem["png"] );

        if( !png )
            return std::nullopt;

        out.png = std::move( *png );
    }

    return out;
}

// items.add: {items: DsNewItem[1..64]}, each with a page made lowercase uuid no item holds (set
// as the item's m_Uuid): AddDrawingSheetItem's body without its dialog (a bitmap from its png,
// ReadImageFile then SetPixelSizeIu as KiCad does), then the item's values; one undo step.
bool dsEditItemsAdd( std::string aArgsJson )
{
    if( !openFrame() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !onlyKeys( args, { "items" } ) || !hasKeys( args, { "items" } )
            || !args["items"].is_array() || args["items"].empty()
            || args["items"].size() > NEW_ITEMS_MAX )
    {
        return false;
    }

    auto                  items = std::make_shared<std::vector<NEW_ITEM>>();
    std::set<std::string> seen;

    for( const json& j : args["items"] )
    {
        std::optional<NEW_ITEM> item = newItemOf( j );

        if( !item || !seen.insert( item->uuid ).second || itemByUuid( item->uuid ) )
            return false;

        items->push_back( std::move( *item ) );
    }

    return queueEdit(
            [items]()
            {
                for( const NEW_ITEM& n : *items )
                {
                    if( itemByUuid( n.uuid ) )
                        return false;
                }

                return true;
            },
            [items]( PL_EDITOR_FRAME* aFrame )
            {
                for( const NEW_ITEM& n : *items )
                {
                    DS_DATA_ITEM* item = nullptr;

                    switch( n.type )
                    {
                    case DS_DATA_ITEM::DS_TEXT:
                        item = new DS_DATA_ITEM_TEXT( wxT( "Text" ) );
                        break;

                    case DS_DATA_ITEM::DS_SEGMENT:
                    case DS_DATA_ITEM::DS_RECT:
                        item = new DS_DATA_ITEM( n.type );
                        break;

                    case DS_DATA_ITEM::DS_BITMAP:
                        item = newBitmapItem( n.png );
                        break;

                    default:
                        break;
                    }

                    if( !item )
                        continue;

                    item->m_Uuid = KIID( n.uuid );
                    model().Append( item );
                    item->SyncDrawItems( nullptr, aFrame->GetCanvas()->GetView() );
                    applySet( item, n.set );
                }
            } );
}

/** No item of aUuids being placed by a tool (PL_EDIT_TOOL::DoDelete's guard against a crash). */
bool nonePlacing( const std::vector<std::string>& aUuids )
{
    for( const std::string& u : aUuids )
    {
        DS_DATA_ITEM* item = itemByUuid( u );

        if( !item )
            return false;

        for( DS_DRAW_ITEM_BASE* draw : item->GetDrawItems() )
        {
            if( draw && ( draw->GetFlags() & IS_NEW ) )
                return false;
        }
    }

    return true;
}

// items.remove: {uuids: string[1..4096]}, every one held: PL_EDIT_TOOL::DoDelete's body (each
// data item out of the model, its draw items out of the view and the selection); one undo step.
bool dsEditItemsRemove( std::string aArgsJson )
{
    if( !openFrame() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !onlyKeys( args, { "uuids" } ) || !hasKeys( args, { "uuids" } ) )
        return false;

    std::optional<std::vector<std::string>> uuids = uuidsOf( args["uuids"], 1, UUIDS_MAX );

    if( !uuids || !nonePlacing( *uuids ) )
        return false;

    const std::vector<std::string> ids = *uuids;

    return queueEdit(
            [ids]()
            {
                return nonePlacing( ids );
            },
            [ids]( PL_EDITOR_FRAME* aFrame )
            {
                PL_SELECTION_TOOL* tool = selectionTool( aFrame );
                KIGFX::VIEW*       view = aFrame->GetCanvas()->GetView();

                for( const std::string& u : ids )
                {
                    DS_DATA_ITEM* dataItem = itemByUuid( u );

                    if( !dataItem )
                        continue;

                    model().Remove( dataItem );

                    for( DS_DRAW_ITEM_BASE* item : dataItem->GetDrawItems() )
                    {
                        // Note: repeat items won't be selected but must be removed & deleted
                        if( item->IsSelected() && tool )
                            tool->RemoveItemFromSel( item );

                        view->Remove( item );
                    }

                    delete dataItem;
                }
            } );
}


// ── defaults (section 7.3) ────────────────────────────────────────────────────────────────────

std::string dsEditDefaultsGet()
{
    if( !openFrame() )
        return "";

    DS_DATA_MODEL& m = model();
    json           out = json::object();
    json           margins = json::object();

    out["lineWidth"] = m.m_DefaultLineWidth;
    out["textSize"] = pairJson( m.m_DefaultTextSize );
    out["textThickness"] = m.m_DefaultTextThickness;
    margins["left"] = m.GetLeftMargin();
    margins["right"] = m.GetRightMargin();
    margins["top"] = m.GetTopMargin();
    margins["bottom"] = m.GetBottomMargin();
    out["margins"] = std::move( margins );
    return dumpJson( out );
}

// defaults.set: {set: Partial<DsDefaults>} (margins whole when given), with
// CopyPrmsFromPanelToGeneral's bounds; one undo step.
bool dsEditDefaultsSet( std::string aArgsJson )
{
    if( !openFrame() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !onlyKeys( args, { "set" } ) || !hasKeys( args, { "set" } ) )
        return false;

    const json& set = args["set"];

    if( !onlyKeys( set, { "lineWidth", "textSize", "textThickness", "margins" } ) || set.empty() )
        return false;

    std::optional<double>   lineWidth, thickness;
    std::optional<VECTOR2D> textSize;
    std::optional<double>   left, right, top, bottom;

    if( set.contains( "lineWidth" )
            && !( lineWidth = numberOf( set["lineWidth"], 0.0, LINE_WIDTH_MAX_MM ) ) )
    {
        return false;
    }

    if( set.contains( "textSize" )
            && !( textSize = pairOf( set["textSize"], TEXT_SIZE_MIN_MM, TEXT_SIZE_MAX_MM ) ) )
    {
        return false;
    }

    if( set.contains( "textThickness" )
            && !( thickness = numberOf( set["textThickness"], 0.0, TEXT_THICKNESS_MAX_MM ) ) )
    {
        return false;
    }

    if( set.contains( "margins" ) )
    {
        const json& mg = set["margins"];

        if( !onlyKeys( mg, { "left", "right", "top", "bottom" } )
                || !hasKeys( mg, { "left", "right", "top", "bottom" } ) )
        {
            return false;
        }

        left = numberOf( mg["left"], 0.0, MARGIN_MAX_MM );
        right = numberOf( mg["right"], 0.0, MARGIN_MAX_MM );
        top = numberOf( mg["top"], 0.0, MARGIN_MAX_MM );
        bottom = numberOf( mg["bottom"], 0.0, MARGIN_MAX_MM );

        if( !left || !right || !top || !bottom )
            return false;
    }

    return queueEdit( nullptr,
            [=]( PL_EDITOR_FRAME* )
            {
                DS_DATA_MODEL& m = model();

                if( lineWidth )
                    m.m_DefaultLineWidth = *lineWidth;

                if( textSize )
                    m.m_DefaultTextSize = *textSize;

                if( thickness )
                    m.m_DefaultTextThickness = *thickness;

                if( left )
                {
                    m.SetRightMargin( *right );
                    m.SetBottomMargin( *bottom );
                    m.SetLeftMargin( *left );
                    m.SetTopMargin( *top );
                }
            } );
}


// ── preview (section 7.4): the frame's PAGE_INFO and TITLE_BLOCK ──────────────────────────────

std::string dsEditPreviewGet()
{
    PL_EDITOR_FRAME* fr = openFrame();

    if( !fr )
        return "";

    const PAGE_INFO&   page = fr->GetPageSettings();
    const TITLE_BLOCK& tb = fr->GetTitleBlock();
    json               out = json::object();
    json               comments = json::array();

    out["paper"] = utf8( page.GetTypeAsString() );
    out["portrait"] = page.IsPortrait();
    out["width"] = page.GetWidthMM();
    out["height"] = page.GetHeightMM();
    out["title"] = utf8( tb.GetTitle() );
    out["date"] = utf8( tb.GetDate() );
    out["revision"] = utf8( tb.GetRevision() );
    out["company"] = utf8( tb.GetCompany() );

    for( int i = 0; i < 9; ++i )
        comments.push_back( utf8( tb.GetComment( i ) ) );

    out["comments"] = std::move( comments );
    return dumpJson( out );
}

// preview.set: {set: Partial<DsPreview>}: DIALOG_PAGES_SETTINGS::SavePageSettings' body with no
// dialog and no reload of the sheet from a file (the frame's model is the sheet). width and
// height only for a User paper (both then), portrait only for the other papers (a User page's
// orientation follows its sizes); comments exactly nine. KiCad's PageSetup pushes the undo step
// before its dialog: one undo step, holding the page and title block too.
bool dsEditPreviewSet( std::string aArgsJson )
{
    PL_EDITOR_FRAME* fr = openFrame();

    if( !fr )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !onlyKeys( args, { "set" } ) || !hasKeys( args, { "set" } ) )
        return false;

    const json& set = args["set"];

    if( set.empty()
            || !onlyKeys( set, { "paper", "portrait", "width", "height", "title", "date",
                                 "revision", "company", "comments" } ) )
    {
        return false;
    }

    PAGE_INFO   page = fr->GetPageSettings();
    TITLE_BLOCK tb = fr->GetTitleBlock();

    if( set.contains( "paper" ) )
    {
        std::optional<wxString> paper = textOf( set["paper"], 1, 16 );

        if( !paper || !page.SetType( *paper, page.IsPortrait() )
                || page.GetTypeAsString() != *paper )
        {
            return false;
        }
    }

    const bool user = page.GetType() == PAGE_SIZE_TYPE::User;

    if( set.contains( "width" ) != set.contains( "height" ) )
        return false;

    if( set.contains( "width" ) )
    {
        std::optional<double> w = numberOf( set["width"], PAGE_MIN_MM, PAGE_MAX_MM );
        std::optional<double> h = numberOf( set["height"], PAGE_MIN_MM, PAGE_MAX_MM );

        if( !user || !w || !h )
            return false;

        page.SetWidthMM( *w );
        page.SetHeightMM( *h );
    }

    if( set.contains( "portrait" ) )
    {
        std::optional<bool> portrait = boolOf( set["portrait"] );

        if( !portrait || user )
            return false;

        page.SetPortrait( *portrait );
    }

    struct TB_FIELD
    {
        const char* key;
        void ( TITLE_BLOCK::*setter )( const wxString& );
    };

    static const TB_FIELD fields[] = { { "title", &TITLE_BLOCK::SetTitle },
                                       { "date", &TITLE_BLOCK::SetDate },
                                       { "revision", &TITLE_BLOCK::SetRevision },
                                       { "company", &TITLE_BLOCK::SetCompany } };

    for( const TB_FIELD& f : fields )
    {
        if( !set.contains( f.key ) )
            continue;

        std::optional<wxString> value = textOf( set[f.key], 0, TEXT_MAX );

        if( !value )
            return false;

        ( tb.*f.setter )( *value );
    }

    if( set.contains( "comments" ) )
    {
        const json& c = set["comments"];

        if( !c.is_array() || c.size() != 9 )
            return false;

        for( int i = 0; i < 9; ++i )
        {
            std::optional<wxString> value = textOf( c[i], 0, TEXT_MAX );

            if( !value )
                return false;

            tb.SetComment( i, *value );
        }
    }

    const PAGE_INFO   newPage = page;
    const TITLE_BLOCK newTb = tb;

    return queueEdit( nullptr,
            [newPage, newTb, user]( PL_EDITOR_FRAME* aFrame )
            {
                if( user )
                {
                    PAGE_INFO::SetCustomWidthMils( newPage.GetWidthMils() );
                    PAGE_INFO::SetCustomHeightMils( newPage.GetHeightMils() );
                }

                aFrame->SetPageSettings( newPage );
                aFrame->SetTitleBlock( newTb );
            } );
}


// ── show, selection ───────────────────────────────────────────────────────────────────────────

// show: {page: 1 | 2, values: bool}: OnSelectPage's body on the hidden selector and
// m_EditMode = !values (PL_EDITOR_CONTROL::TitleBlockDisplayMode), then HardRedraw. No undo
// step, not a modification.
bool dsEditShow( std::string aArgsJson )
{
    PL_EDITOR_FRAME* fr = openFrame();

    if( !fr )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !onlyKeys( args, { "page", "values" } )
            || !hasKeys( args, { "page", "values" } ) )
    {
        return false;
    }

    std::optional<int>  page = intOf( args["page"], 1, 2 );
    std::optional<bool> values = boolOf( args["values"] );

    if( !page || !values )
        return false;

    const int         p = *page;
    const bool        v = *values;
    const std::string token = session().token;

    pcbjam_collab::runOnCoroutine( fr, [p, v, token]() {
        PL_EDITOR_FRAME* frame = dsFrame();

        if( !frame || !session().open || session().token != token )
            return;

        model().m_EditMode = !v;
        showPage( frame, p );
        frame->HardRedraw();
    } );

    return true;
}

std::string dsEditSelection()
{
    PL_EDITOR_FRAME* fr = dsFrame();

    if( !fr )
        return "";

    json                  uuids = json::array();
    std::set<std::string> seen;

    if( PL_SELECTION_TOOL* tool = selectionTool( fr ) )
    {
        for( EDA_ITEM* item : tool->GetSelection() )
        {
            DS_DRAW_ITEM_BASE* draw = static_cast<DS_DRAW_ITEM_BASE*>( item );
            DS_DATA_ITEM*      peer = draw ? draw->GetPeer() : nullptr;

            if( peer && seen.insert( uuidOf( peer ) ).second )
                uuids.push_back( uuidOf( peer ) );
        }
    }

    json out = json::object();
    out["uuids"] = std::move( uuids );
    return dumpJson( out );
}

// select: {uuids: string[0..4096], zoom: bool}: every uuid held; the selection replaced by
// their first drawn copies (an empty list clears it), the view fitted to them with zoom.
bool dsEditSelect( std::string aArgsJson )
{
    PL_EDITOR_FRAME* fr = openFrame();

    if( !fr )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !onlyKeys( args, { "uuids", "zoom" } )
            || !hasKeys( args, { "uuids", "zoom" } ) )
    {
        return false;
    }

    std::optional<std::vector<std::string>> uuids = uuidsOf( args["uuids"], 0, UUIDS_MAX );
    std::optional<bool>                     zoom = boolOf( args["zoom"] );

    if( !uuids || !zoom || !uuidsFit( *uuids, {} ) )
        return false;

    const std::vector<std::string> ids = *uuids;
    const bool                     fit = *zoom;
    const std::string              token = session().token;

    pcbjam_collab::runOnCoroutine( fr, [ids, fit, token]() {
        PL_EDITOR_FRAME*   frame = dsFrame();
        PL_SELECTION_TOOL* tool = selectionTool( frame );

        if( !frame || !tool || !session().open || session().token != token )
            return;

        EDA_ITEMS picked;

        for( const std::string& u : ids )
        {
            DS_DATA_ITEM* item = itemByUuid( u );

            if( item && !item->GetDrawItems().empty() && item->GetDrawItems()[0] )
                picked.push_back( item->GetDrawItems()[0] );
        }

        tool->ClearSelection();

        if( !picked.empty() )
        {
            tool->AddItemsToSel( &picked );

            if( fit )
                frame->GetToolManager()->RunAction( ACTIONS::zoomFitSelection );
        }

        frame->GetCanvas()->Refresh();
    } );

    return true;
}


// ── append, the armed picture ─────────────────────────────────────────────────────────────────

// append: {text} (at most 8 MiB): InsertDrawingSheetFile's body from text (SaveCopyInUndoList,
// the scratch parse as open, then SetPageLayout( text, true ): KiCad's append, its setup taken
// too). The appended items keep their file's uuids; one a held item or an earlier appended item
// already has gets a fresh one, so ids stay unique. {error: "" | "unreadable" | "too_large",
// message, added: uuids}; one undo step.
std::string dsEditAppend( std::string aArgsJson )
{
    if( !openFrame() )
        return "";

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !onlyKeys( args, { "text" } ) || !hasKeys( args, { "text" } )
            || !args["text"].is_string() )
    {
        return "";
    }

    const std::string text = args["text"].get<std::string>();

    if( text.size() > DOC_MAX_BYTES )
        return dumpJson( errorJson( "too_large" ) );

    PARSED parsed = parseScratch( text, wxS( "appended.kicad_wks" ) );

    if( !parsed.ok )
        return dumpJson( errorJson( "unreadable", parsed.message ) );

    std::set<std::string> held;

    for( DS_DATA_ITEM* item : model().GetItems() )
    {
        if( item )
            held.insert( uuidOf( item ) );
    }

    auto added = std::make_shared<std::vector<std::string>>();

    for( DS_DATA_ITEM* item : parsed.sheet->GetItems() )
    {
        std::string u = item ? uuidOf( item ) : std::string();

        while( u.empty() || held.count( u ) )
            u = utf8( KIID().AsString() );

        held.insert( u );
        added->push_back( u );
    }

    const bool queued = queueEdit( nullptr,
            [text, added]( PL_EDITOR_FRAME* )
            {
                DS_DATA_MODEL& m = model();
                const size_t   before = m.GetCount();

                m.SetPageLayout( text.c_str(), true );

                // KiCad's parser is deterministic: the appended items are the scratch's, in
                // its order, so each takes the uuid the answer named.
                if( m.GetCount() - before == added->size() )
                {
                    for( size_t i = 0; i < added->size(); ++i )
                        m.GetItems()[before + i]->m_Uuid = KIID( ( *added )[i] );
                }
            } );

    if( !queued )
        return "";

    json out = errorJson( "" );
    out["message"] = "";
    out["added"] = *added;
    return dumpJson( out );
}

// image.arm: {png, name}: png base64 of a PNG of at most 4 MiB decoded, "" disarms. The next
// Place Bitmaps press takes it in place of KiCad's file dialog (K-DS1, ds_editor_host.h).
bool dsEditArmImage( std::string aArgsJson )
{
    if( !openFrame() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !onlyKeys( args, { "png", "name" } )
            || !hasKeys( args, { "png", "name" } ) || !args["png"].is_string() )
    {
        return false;
    }

    std::optional<wxString> name = textOf( args["name"], 0, TEXT_MAX );

    if( !name )
        return false;

    if( args["png"].get_ref<const std::string&>().empty() )
    {
        CcArmedImage() = CC_DS_ARMED_IMAGE();
        return true;
    }

    std::optional<std::vector<uint8_t>> png = pngOf( args["png"] );

    if( !png )
        return false;

    CC_DS_ARMED_IMAGE armed;
    armed.armed = true;
    armed.png = std::move( *png );
    armed.name = *name;
    CcArmedImage() = std::move( armed );
    return true;
}

// The marker: the engine asks the page (window.kicadHostDsEdit) before the frame's file dialog
// for a picture (K-DS1), so the island lists the cap dseditHeld only on an engine that asks.
bool dsHostDsEdit()
{
    return true;
}

} // namespace ds
} // namespace cc_e6


// The drawing sheet frame's change hook. The fork's pagelayout_editor/pl_editor_frame.cpp
// calls it from PL_EDITOR_FRAME::OnModify() under __EMSCRIPTEN__; the standalone pl_editor
// image defined it in pl_editor_embind.cpp (the collaboration bridge), which the merged image
// does not link. Here it only counts: no JS listener, no collaboration bridge.
extern "C" void kicadCollabOnModify()
{
    ++cc_e6::g_dsChangeCount;
}

EMSCRIPTEN_BINDINGS( cc_e6_dsedit )
{
    function( "kicadDsEditOpen", &cc_e6::ds::dsEditOpen );
    function( "kicadDsEditState", &cc_e6::ds::dsEditState );
    function( "kicadDsEditTake", &cc_e6::ds::dsEditTake );
    function( "kicadDsEditMarkSaved", &cc_e6::ds::dsEditMarkSaved );
    function( "kicadDsEditItemsGet", &cc_e6::ds::dsEditItemsGet );
    function( "kicadDsEditItemsSet", &cc_e6::ds::dsEditItemsSet );
    function( "kicadDsEditItemsAdd", &cc_e6::ds::dsEditItemsAdd );
    function( "kicadDsEditItemsRemove", &cc_e6::ds::dsEditItemsRemove );
    function( "kicadDsEditDefaultsGet", &cc_e6::ds::dsEditDefaultsGet );
    function( "kicadDsEditDefaultsSet", &cc_e6::ds::dsEditDefaultsSet );
    function( "kicadDsEditPreviewGet", &cc_e6::ds::dsEditPreviewGet );
    function( "kicadDsEditPreviewSet", &cc_e6::ds::dsEditPreviewSet );
    function( "kicadDsEditShow", &cc_e6::ds::dsEditShow );
    function( "kicadDsEditSelection", &cc_e6::ds::dsEditSelection );
    function( "kicadDsEditSelect", &cc_e6::ds::dsEditSelect );
    function( "kicadDsEditAppend", &cc_e6::ds::dsEditAppend );
    function( "kicadDsEditArmImage", &cc_e6::ds::dsEditArmImage );
    function( "kicadHostDsEdit", &cc_e6::ds::dsHostDsEdit );
}

#endif // __EMSCRIPTEN__
