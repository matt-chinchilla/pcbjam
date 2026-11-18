/*
 * Shared plumbing for the Symbol Editor's bindings (engine run 14, caps symedit,
 * symbolEdit and symbolConvert): a symbol read from a kicad_symbol_lib document and
 * written back as one, by KiCad's own reader and writer. Header-only (the
 * collab_common.h pattern), included by symedit_embind.cpp (the symbol editor frame's
 * exports) and eeschema_embind.cpp (the schematic frame's kicadSymbolForEditor and
 * kicadSymbolPushToSchematic), so the build needs no extra object for it.
 *
 * Created by Chirichella Inc. on 2026-11-16.
 * Modified by Circuit Center Inc on 2026-11-18: the symbol editor's JSON readers, record
 * tokens, text, text box, shape, stroke and fill readers and appliers lifted here out of
 * symedit_embind.cpp unchanged in behavior (a schematic face reuses them, Y down).
 */

#pragma once

#ifdef __EMSCRIPTEN__

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/string.h>
#include <nlohmann/json.hpp>
#include <base_units.h>
#include <build_version.h>
#include <eda_shape.h>
#include <eda_text.h>
#include <gal/color4d.h>
#include <pin_type.h>
#include <sch_pin.h>
#include <sch_shape.h>
#include <sch_text.h>
#include <sch_textbox.h>
#include <stroke_params.h>
#include <io/io_mgr.h>
#include <io/kicad/kicad_io_utils.h>
#include <lib_id.h>
#include <lib_symbol.h>
#include <richio.h>
#include <sch_file_versions.h>
#include <sch_io/sch_io.h>
#include <sch_io/sch_io_mgr.h>
#include <sch_io/kicad_sexpr/sch_io_kicad_sexpr_lib_cache.h>

namespace cc_symedit
{

/// The largest symbol document read or written (spec 5.1: 4 MB).
constexpr size_t DOC_MAX_BYTES = 4u * 1024u * 1024u;

/** A lowercase 8-4-4-4-12 uuid. */
inline bool isLowerUuid( const std::string& aText )
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

inline std::string utf8( const wxString& aText )
{
    return std::string( aText.utf8_str() );
}

/**
 * A symbol and the symbols it derives from, owned: [0] the symbol, then its parent, its
 * parent's parent, ... Each copy's parent link points at the next copy, never at a reader's
 * cache.
 */
struct SYMBOL_CHAIN
{
    std::vector<std::unique_ptr<LIB_SYMBOL>> links;

    LIB_SYMBOL* symbol() const { return links.empty() ? nullptr : links.front().get(); }
};

/** Copies aSymbol and every ancestor it links to, relinked among the copies. */
inline SYMBOL_CHAIN copyChain( const LIB_SYMBOL* aSymbol )
{
    SYMBOL_CHAIN chain;
    std::vector<const LIB_SYMBOL*> source;

    for( const LIB_SYMBOL* s = aSymbol; s && source.size() < 64; )
    {
        source.push_back( s );

        std::shared_ptr<LIB_SYMBOL> parent = s->GetParent().lock();
        s = parent.get();
    }

    for( const LIB_SYMBOL* s : source )
        chain.links.push_back( std::make_unique<LIB_SYMBOL>( *s ) );

    for( size_t i = 0; i < chain.links.size(); ++i )
    {
        if( i + 1 < chain.links.size() )
            chain.links[i]->SetParent( chain.links[i + 1].get() );
        else
            chain.links[i]->SetParent();   // a root: no argument, as SetParent( nullptr ) also matches EDA_ITEM::SetParent
    }

    return chain;
}

/** A scratch file under /tmp/cc-symedit/ (MEMFS), unique per call. */
inline wxString scratchPath( const char* aExt )
{
    static unsigned s_serial = 0;

    const wxString dir = wxS( "/tmp/cc-symedit" );

    if( !wxFileName::DirExists( dir ) )
        wxFileName::Mkdir( dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL );

    return wxString::Format( wxS( "%s/doc-%u.%s" ), dir, ++s_serial, aExt );
}

inline bool writeFile( const wxString& aPath, const std::string& aBytes )
{
    FILE* f = std::fopen( aPath.utf8_str(), "wb" );

    if( !f )
        return false;

    const bool whole = std::fwrite( aBytes.data(), 1, aBytes.size(), f ) == aBytes.size();
    std::fclose( f );
    return whole;
}

/**
 * The symbol named aName of a kicad_symbol_lib document, with its extends chain, read by
 * KiCad's own library reader (SCH_IO_KICAD_SEXPR: the version, every token and the parent
 * links exactly as a library file is read). Empty for a document KiCad cannot read, or with
 * no symbol of that name. The document goes through a scratch file of its own, removed after.
 */
inline SYMBOL_CHAIN readSymbol( const std::string& aText, const wxString& aName )
{
    SYMBOL_CHAIN chain;

    if( aText.empty() || aText.size() > DOC_MAX_BYTES || aName.IsEmpty() )
        return chain;

    const wxString path = scratchPath( "kicad_sym" );

    if( !writeFile( path, aText ) )
        return chain;

    try
    {
        IO_RELEASER<SCH_IO> pi( SCH_IO_MGR::FindPlugin( SCH_IO_MGR::SCH_KICAD ) );

        if( pi )
        {
            if( LIB_SYMBOL* found = pi->LoadSymbol( path, aName ) )
                chain = copyChain( found );
        }
    }
    catch( ... )
    {
        chain.links.clear();
    }

    wxRemoveFile( path );
    return chain;
}

/** KiCad's own library header (SCH_IO_KICAD_SEXPR_LIB_CACHE::formatLibraryHeader's words). */
inline void formatHeader( OUTPUTFORMATTER& aFormatter )
{
    aFormatter.Print( "(kicad_symbol_lib (version %d) (generator \"kicad_symbol_editor\") "
                      "(generator_version \"%s\")",
                      SEXPR_SYMBOL_LIB_FILE_VERSION,
                      GetMajorMinorVersion().c_str().AsChar() );
}

/**
 * aSymbol as a kicad_symbol_lib document by KiCad's own formatter, its ancestors written
 * first (a library is ordered by inheritance depth), the whole prettified as a library file
 * is. Copies are formatted (SaveSymbol touches a symbol's embedded fonts), so this is a pure
 * read. Empty when it cannot be written.
 */
inline std::string formatSymbol( const LIB_SYMBOL* aSymbol )
{
    if( !aSymbol )
        return std::string();

    try
    {
        SYMBOL_CHAIN     chain = copyChain( aSymbol );
        STRING_FORMATTER formatter;

        formatHeader( formatter );

        for( auto it = chain.links.rbegin(); it != chain.links.rend(); ++it )
            SCH_IO_KICAD_SEXPR_LIB_CACHE::SaveSymbol( it->get(), formatter );

        formatter.Print( ")" );

        std::string out = formatter.GetString();
        KICAD_FORMAT::Prettify( out, KICAD_FORMAT::FORMAT_MODE::NORMAL );
        return out;
    }
    catch( ... )
    {
        return std::string();
    }
}

/** The name of aSymbol's parent, "" for a root symbol. */
inline wxString parentName( const LIB_SYMBOL* aSymbol )
{
    if( !aSymbol )
        return wxString();

    std::shared_ptr<LIB_SYMBOL> parent = aSymbol->GetParent().lock();
    return parent ? parent->GetName() : wxString();
}

// ── the records' readers and appliers (lifted from symedit_embind.cpp, run 15) ─────────

using json = nlohmann::json;
using KIGFX::COLOR4D;

// ── JSON readers (closed shapes, bounded values) ──────────────────────────────────────

constexpr size_t TEXT_MAX = 1024;
constexpr double POSITION_MAX_MM = 50000.0;
constexpr double SIZE_MAX_MM = 1000.0;
constexpr int    UNITS_MAX = 702;          // KiCad's unit letters run A to ZZ
constexpr size_t UUIDS_MAX = 4096;
constexpr size_t NEW_ITEMS_MAX = 512;
constexpr size_t POINTS_MAX = 4096;
constexpr size_t ALTERNATES_MAX = 64;

inline bool onlyKeys( const json& aObj, std::initializer_list<const char*> aKeys )
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

inline bool hasKeys( const json& aObj, std::initializer_list<const char*> aKeys )
{
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
inline std::optional<wxString> textOf( const json& aValue, size_t aMin, size_t aMax, bool aLines = false )
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

inline std::optional<double> numberOf( const json& aValue, double aMin, double aMax )
{
    if( !aValue.is_number() )
        return std::nullopt;

    const double v = aValue.get<double>();

    if( !std::isfinite( v ) || v < aMin || v > aMax )
        return std::nullopt;

    return v;
}

inline std::optional<int> intOf( const json& aValue, int aMin, int aMax )
{
    if( !aValue.is_number_integer() )
        return std::nullopt;

    const long long v = aValue.get<long long>();

    if( v < aMin || v > aMax )
        return std::nullopt;

    return static_cast<int>( v );
}

inline std::optional<bool> boolOf( const json& aValue )
{
    if( !aValue.is_boolean() )
        return std::nullopt;

    return aValue.get<bool>();
}

/** A page token: 1 to 64 printable ASCII characters. */
inline std::optional<std::string> tokenOf( const json& aValue )
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

/** A list of lowercase uuids, aMin to aMax of them, no repeats. */
inline std::optional<std::vector<std::string>> uuidsOf( const json& aValue, size_t aMin, size_t aMax )
{
    if( !aValue.is_array() || aValue.size() < aMin || aValue.size() > aMax )
        return std::nullopt;

    std::vector<std::string> out;
    std::set<std::string>    seen;

    for( const json& u : aValue )
    {
        if( !u.is_string() || !cc_symedit::isLowerUuid( u.get<std::string>() ) )
            return std::nullopt;

        if( !seen.insert( u.get<std::string>() ).second )
            return std::nullopt;

        out.push_back( u.get<std::string>() );
    }

    return out;
}

// ── units and coordinates ─────────────────────────────────────────────────────────────

inline double mmOf( int aIu )
{
    return std::round( schIUScale.IUTomm( aIu ) * 10000.0 ) / 10000.0;
}

inline int iuOf( double aMm )
{
    return schIUScale.mmToIU( aMm );
}

/**
 * A stored point (Y down) as the symbol editor shows it (Y up), in mm; with aYUp false as a
 * schematic shows it (Y down, as stored).
 */
inline json pointJson( const VECTOR2I& aPoint, bool aYUp = true )
{
    return json::array( { mmOf( aPoint.x ), mmOf( aYUp ? -aPoint.y : aPoint.y ) } );
}

/** A shown point (Y up, mm; Y down with aYUp false) as stored (Y down, internal units). */
inline VECTOR2I storedPoint( double aX, double aY, bool aYUp = true )
{
    return VECTOR2I( iuOf( aX ), aYUp ? -iuOf( aY ) : iuOf( aY ) );
}

// ── KiCad's file tokens ───────────────────────────────────────────────────────────────

template <typename T>
struct TOKEN
{
    T           value;
    const char* name;
};

const TOKEN<ELECTRICAL_PINTYPE> PIN_TYPES[] = {
    { ELECTRICAL_PINTYPE::PT_INPUT, "input" },
    { ELECTRICAL_PINTYPE::PT_OUTPUT, "output" },
    { ELECTRICAL_PINTYPE::PT_BIDI, "bidirectional" },
    { ELECTRICAL_PINTYPE::PT_TRISTATE, "tri_state" },
    { ELECTRICAL_PINTYPE::PT_PASSIVE, "passive" },
    { ELECTRICAL_PINTYPE::PT_NIC, "free" },
    { ELECTRICAL_PINTYPE::PT_UNSPECIFIED, "unspecified" },
    { ELECTRICAL_PINTYPE::PT_POWER_IN, "power_in" },
    { ELECTRICAL_PINTYPE::PT_POWER_OUT, "power_out" },
    { ELECTRICAL_PINTYPE::PT_OPENCOLLECTOR, "open_collector" },
    { ELECTRICAL_PINTYPE::PT_OPENEMITTER, "open_emitter" },
    { ELECTRICAL_PINTYPE::PT_NC, "no_connect" },
};

const TOKEN<GRAPHIC_PINSHAPE> PIN_SHAPES[] = {
    { GRAPHIC_PINSHAPE::LINE, "line" },
    { GRAPHIC_PINSHAPE::INVERTED, "inverted" },
    { GRAPHIC_PINSHAPE::CLOCK, "clock" },
    { GRAPHIC_PINSHAPE::INVERTED_CLOCK, "inverted_clock" },
    { GRAPHIC_PINSHAPE::INPUT_LOW, "input_low" },
    { GRAPHIC_PINSHAPE::CLOCK_LOW, "clock_low" },
    { GRAPHIC_PINSHAPE::OUTPUT_LOW, "output_low" },
    { GRAPHIC_PINSHAPE::FALLING_EDGE_CLOCK, "edge_clock_high" },
    { GRAPHIC_PINSHAPE::NONLOGIC, "non_logic" },
};

const TOKEN<PIN_ORIENTATION> PIN_ORIENTATIONS[] = {
    { PIN_ORIENTATION::PIN_RIGHT, "right" },
    { PIN_ORIENTATION::PIN_LEFT, "left" },
    { PIN_ORIENTATION::PIN_UP, "up" },
    { PIN_ORIENTATION::PIN_DOWN, "down" },
};

const TOKEN<GR_TEXT_H_ALIGN_T> H_JUSTIFY[] = {
    { GR_TEXT_H_ALIGN_LEFT, "left" },
    { GR_TEXT_H_ALIGN_CENTER, "center" },
    { GR_TEXT_H_ALIGN_RIGHT, "right" },
};

const TOKEN<GR_TEXT_V_ALIGN_T> V_JUSTIFY[] = {
    { GR_TEXT_V_ALIGN_TOP, "top" },
    { GR_TEXT_V_ALIGN_CENTER, "center" },
    { GR_TEXT_V_ALIGN_BOTTOM, "bottom" },
};

const TOKEN<LINE_STYLE> LINE_STYLES[] = {
    { LINE_STYLE::DEFAULT, "default" },
    { LINE_STYLE::SOLID, "solid" },
    { LINE_STYLE::DASH, "dash" },
    { LINE_STYLE::DOT, "dot" },
    { LINE_STYLE::DASHDOT, "dash_dot" },
    { LINE_STYLE::DASHDOTDOT, "dash_dot_dot" },
};

// The record's four fill types, then the hatches KiCad 10 also stores (read and written
// under their own names; the record lists four, see BUILD-NOTES-symbol.md).
const TOKEN<FILL_T> FILLS[] = {
    { FILL_T::NO_FILL, "none" },
    { FILL_T::FILLED_SHAPE, "outline" },
    { FILL_T::FILLED_WITH_BG_BODYCOLOR, "background" },
    { FILL_T::FILLED_WITH_COLOR, "color" },
    { FILL_T::HATCH, "hatch" },
    { FILL_T::REVERSE_HATCH, "reverse_hatch" },
    { FILL_T::CROSS_HATCH, "cross_hatch" },
};

template <typename T, size_t N>
const char* nameOf( const TOKEN<T> ( &aTable )[N], T aValue, const char* aFallback )
{
    for( const TOKEN<T>& t : aTable )
    {
        if( t.value == aValue )
            return t.name;
    }

    return aFallback;
}

template <typename T, size_t N>
inline std::optional<T> valueOf( const TOKEN<T> ( &aTable )[N], const json& aValue )
{
    if( !aValue.is_string() )
        return std::nullopt;

    const std::string& name = aValue.get_ref<const std::string&>();

    for( const TOKEN<T>& t : aTable )
    {
        if( name == t.name )
            return t.value;
    }

    return std::nullopt;
}

// ── colors, strokes, fills ────────────────────────────────────────────────────────────

/** "" for KiCad's unspecified color, else #rrggbbaa. */
inline std::string colorText( const COLOR4D& aColor )
{
    if( aColor == COLOR4D::UNSPECIFIED )
        return std::string();

    auto byte = []( double v )
    {
        return static_cast<unsigned>( std::clamp( (int) std::lround( v * 255.0 ), 0, 255 ) );
    };

    char buf[10];
    std::snprintf( buf, sizeof( buf ), "#%02x%02x%02x%02x", byte( aColor.r ), byte( aColor.g ),
                   byte( aColor.b ), byte( aColor.a ) );
    return buf;
}

inline std::optional<COLOR4D> colorOf( const json& aValue )
{
    if( !aValue.is_string() )
        return std::nullopt;

    const std::string& t = aValue.get_ref<const std::string&>();

    if( t.empty() )
        return COLOR4D::UNSPECIFIED;

    if( t.size() != 9 || t[0] != '#' )
        return std::nullopt;

    unsigned channel[4];

    for( int i = 0; i < 4; ++i )
    {
        unsigned v = 0;

        for( int k = 0; k < 2; ++k )
        {
            const char c = t[1 + i * 2 + k];
            v <<= 4;

            if( c >= '0' && c <= '9' )
                v |= unsigned( c - '0' );
            else if( c >= 'a' && c <= 'f' )
                v |= unsigned( c - 'a' + 10 );
            else
                return std::nullopt;
        }

        channel[i] = v;
    }

    return COLOR4D( channel[0] / 255.0, channel[1] / 255.0, channel[2] / 255.0, channel[3] / 255.0 );
}

inline json strokeJson( const STROKE_PARAMS& aStroke )
{
    json s = json::object();
    s["width"] = mmOf( aStroke.GetWidth() );
    s["style"] = nameOf( LINE_STYLES, aStroke.GetLineStyle(), "default" );
    s["color"] = colorText( aStroke.GetColor() );
    return s;
}

inline std::optional<STROKE_PARAMS> strokeOf( const json& aValue )
{
    if( !onlyKeys( aValue, { "width", "style", "color" } )
            || !hasKeys( aValue, { "width", "style", "color" } ) )
    {
        return std::nullopt;
    }

    std::optional<double>     width = numberOf( aValue["width"], 0.0, 100.0 );
    std::optional<LINE_STYLE> style = valueOf( LINE_STYLES, aValue["style"] );
    std::optional<COLOR4D>    color = colorOf( aValue["color"] );

    if( !width || !style || !color )
        return std::nullopt;

    return STROKE_PARAMS( iuOf( *width ), *style, *color );
}

struct FILL_SPEC
{
    FILL_T  type = FILL_T::NO_FILL;
    COLOR4D color = COLOR4D::UNSPECIFIED;
};

inline json fillJson( const EDA_SHAPE& aShape )
{
    json f = json::object();
    f["type"] = nameOf( FILLS, aShape.GetFillMode(), "none" );
    f["color"] = colorText( aShape.GetFillColor() );
    return f;
}

inline std::optional<FILL_SPEC> fillOf( const json& aValue )
{
    if( !onlyKeys( aValue, { "type", "color" } ) || !hasKeys( aValue, { "type", "color" } ) )
        return std::nullopt;

    std::optional<FILL_T>  type = valueOf( FILLS, aValue["type"] );
    std::optional<COLOR4D> color = colorOf( aValue["color"] );

    if( !type || !color )
        return std::nullopt;

    return FILL_SPEC{ *type, *color };
}

/** The text keys every text-like record carries. */
inline void textJson( json& aOut, const EDA_TEXT& aText )
{
    aOut["text"] = utf8( aText.GetText() );
    aOut["angle"] = aText.GetTextAngle().IsVertical() ? 90 : 0;
    aOut["size"] = mmOf( aText.GetTextWidth() );
    aOut["bold"] = aText.IsBold();
    aOut["italic"] = aText.IsItalic();
    aOut["hJustify"] = nameOf( H_JUSTIFY, aText.GetHorizJustify(), "center" );
    aOut["vJustify"] = nameOf( V_JUSTIFY, aText.GetVertJustify(), "center" );
    aOut["visible"] = aText.IsVisible();
}

/** The points of a shape as the record lists them, per shape. */
inline json shapePoints( const SCH_SHAPE& aShape, const char*& aShapeName, bool aYUp = true )
{
    json pts = json::array();

    switch( aShape.GetShape() )
    {
    case SHAPE_T::RECTANGLE:
        aShapeName = "rectangle";
        pts.push_back( pointJson( aShape.GetStart(), aYUp ) );
        pts.push_back( pointJson( aShape.GetEnd(), aYUp ) );
        break;

    case SHAPE_T::CIRCLE:
        aShapeName = "circle";
        pts.push_back( pointJson( aShape.GetStart(), aYUp ) );   // the center
        pts.push_back( pointJson( aShape.GetEnd(), aYUp ) );     // a point on the circle
        break;

    case SHAPE_T::ARC:
        aShapeName = "arc";
        pts.push_back( pointJson( aShape.GetStart(), aYUp ) );
        pts.push_back( pointJson( aShape.GetArcMid(), aYUp ) );
        pts.push_back( pointJson( aShape.GetEnd(), aYUp ) );
        break;

    case SHAPE_T::BEZIER:
        aShapeName = "bezier";
        pts.push_back( pointJson( aShape.GetStart(), aYUp ) );
        pts.push_back( pointJson( aShape.GetBezierC1(), aYUp ) );
        pts.push_back( pointJson( aShape.GetBezierC2(), aYUp ) );
        pts.push_back( pointJson( aShape.GetEnd(), aYUp ) );
        break;

    case SHAPE_T::SEGMENT:
        aShapeName = "polyline";
        pts.push_back( pointJson( aShape.GetStart(), aYUp ) );
        pts.push_back( pointJson( aShape.GetEnd(), aYUp ) );
        break;

    case SHAPE_T::POLY:
    default:
        aShapeName = "polyline";

        for( const VECTOR2I& p : aShape.GetPolyPoints() )
            pts.push_back( pointJson( p, aYUp ) );

        break;
    }

    return pts;
}

/** The point count a shape takes ("polyline": 2 or more). */
inline bool pointsFit( const std::string& aShape, size_t aCount )
{
    if( aShape == "rectangle" || aShape == "circle" )
        return aCount == 2;

    if( aShape == "arc" )
        return aCount == 3;

    if( aShape == "bezier" )
        return aCount == 4;

    return aCount >= 2 && aCount <= POINTS_MAX;
}

inline std::string shapeNameOf( const SCH_SHAPE& aShape )
{
    const char* name = "polyline";
    shapePoints( aShape, name );
    return name;
}

/** One items.set (or a new item's keys), read and checked whole before anything is queued. */
struct ITEM_SET
{
    std::set<std::string> keys;

    std::optional<int>                unit, bodyStyle;
    std::optional<wxString>           number, name, text;
    std::optional<ELECTRICAL_PINTYPE> type;
    std::optional<GRAPHIC_PINSHAPE>   pinShape;
    std::optional<PIN_ORIENTATION>    orientation;
    std::optional<double>             x, y, x2, y2, length, nameSize, numberSize, size;
    std::optional<bool>               visible, bold, italic, showName;
    std::optional<int>                angle;
    std::optional<GR_TEXT_H_ALIGN_T>  hJustify;
    std::optional<GR_TEXT_V_ALIGN_T>  vJustify;
    std::optional<STROKE_PARAMS>      stroke;
    std::optional<FILL_SPEC>          fill;
    std::optional<std::array<double, 4>> margins;
    std::optional<std::vector<VECTOR2I>> points;
    std::optional<std::vector<SCH_PIN::ALT>> alternates;
};

/** The keys a set may carry for each kind (never kind or uuid). */
inline const std::set<std::string>& editableKeys( const std::string& aKind )
{
    static const std::set<std::string> pin = { "unit", "bodyStyle", "number", "name", "type",
                                               "shape", "x", "y", "orientation", "length",
                                               "nameSize", "numberSize", "visible",
                                               "alternates" };
    static const std::set<std::string> field = { "name", "text", "x", "y", "angle", "size",
                                                 "bold", "italic", "hJustify", "vJustify",
                                                 "visible", "showName" };
    static const std::set<std::string> text = { "unit", "bodyStyle", "text", "x", "y", "angle",
                                                "size", "bold", "italic", "hJustify",
                                                "vJustify", "visible" };
    static const std::set<std::string> textbox = { "unit", "bodyStyle", "text", "x", "y",
                                                   "angle", "size", "bold", "italic",
                                                   "hJustify", "vJustify", "visible", "x2",
                                                   "y2", "stroke", "fill", "margins" };
    static const std::set<std::string> shape = { "unit", "bodyStyle", "points", "stroke", "fill" };
    static const std::set<std::string> none;

    if( aKind == "pin" )
        return pin;

    if( aKind == "field" )
        return field;

    if( aKind == "text" )
        return text;

    if( aKind == "textbox" )
        return textbox;

    if( aKind == "shape" )
        return shape;

    return none;
}

/**
 * Reads the keys of aSet (an items.set's set, or a new item less kind, uuid and shape) into
 * aOut, every value checked against its bound, a unit against aUnits and a body style against
 * aBodies (a symbol's counts, at least 1; a schematic item's 0, which admits only 0). Points
 * are shown Y up (the symbol editor) or, with aYUp false, Y down (a schematic). False for any
 * unknown key, wrong type or value out of bounds.
 */
inline bool readItemSetBounded( const json& aSet, int aUnits, int aBodies, ITEM_SET& aOut,
                                bool aYUp = true )
{
    if( !aSet.is_object() )
        return false;

    const int units = aUnits;
    const int bodies = aBodies;

    for( auto it = aSet.begin(); it != aSet.end(); ++it )
    {
        const std::string& key = it.key();
        const json&        v = it.value();

        aOut.keys.insert( key );

        if( key == "unit" )
        {
            if( !( aOut.unit = intOf( v, 0, units ) ) )
                return false;
        }
        else if( key == "bodyStyle" )
        {
            if( !( aOut.bodyStyle = intOf( v, 0, bodies ) ) )
                return false;
        }
        else if( key == "number" )
        {
            if( !( aOut.number = textOf( v, 0, TEXT_MAX ) ) )
                return false;
        }
        else if( key == "name" )
        {
            if( !( aOut.name = textOf( v, 0, TEXT_MAX ) ) )
                return false;
        }
        else if( key == "text" )
        {
            if( !( aOut.text = textOf( v, 0, TEXT_MAX, /*aLines*/ true ) ) )
                return false;
        }
        else if( key == "type" )
        {
            if( !( aOut.type = valueOf( PIN_TYPES, v ) ) )
                return false;
        }
        else if( key == "shape" )
        {
            if( !( aOut.pinShape = valueOf( PIN_SHAPES, v ) ) )
                return false;
        }
        else if( key == "orientation" )
        {
            if( !( aOut.orientation = valueOf( PIN_ORIENTATIONS, v ) ) )
                return false;
        }
        else if( key == "x" || key == "y" || key == "x2" || key == "y2" )
        {
            std::optional<double> n = numberOf( v, -POSITION_MAX_MM, POSITION_MAX_MM );

            if( !n )
                return false;

            ( key == "x" ? aOut.x : key == "y" ? aOut.y : key == "x2" ? aOut.x2 : aOut.y2 ) = n;
        }
        else if( key == "length" )
        {
            if( !( aOut.length = numberOf( v, 0.0, SIZE_MAX_MM ) ) )
                return false;
        }
        else if( key == "nameSize" || key == "numberSize" || key == "size" )
        {
            std::optional<double> n = numberOf( v, 0.0, SIZE_MAX_MM );

            if( !n )
                return false;

            ( key == "nameSize" ? aOut.nameSize : key == "numberSize" ? aOut.numberSize : aOut.size ) = n;
        }
        else if( key == "visible" || key == "bold" || key == "italic" || key == "showName" )
        {
            std::optional<bool> b = boolOf( v );

            if( !b )
                return false;

            ( key == "visible" ? aOut.visible
                               : key == "bold" ? aOut.bold : key == "italic" ? aOut.italic
                                                                             : aOut.showName ) = b;
        }
        else if( key == "angle" )
        {
            std::optional<int> a = intOf( v, 0, 90 );

            if( !a || ( *a != 0 && *a != 90 ) )
                return false;

            aOut.angle = a;
        }
        else if( key == "hJustify" )
        {
            if( !( aOut.hJustify = valueOf( H_JUSTIFY, v ) ) )
                return false;
        }
        else if( key == "vJustify" )
        {
            if( !( aOut.vJustify = valueOf( V_JUSTIFY, v ) ) )
                return false;
        }
        else if( key == "stroke" )
        {
            if( !( aOut.stroke = strokeOf( v ) ) )
                return false;
        }
        else if( key == "fill" )
        {
            if( !( aOut.fill = fillOf( v ) ) )
                return false;
        }
        else if( key == "margins" )
        {
            if( !v.is_array() || v.size() != 4 )
                return false;

            std::array<double, 4> m{};

            for( size_t i = 0; i < 4; ++i )
            {
                std::optional<double> n = numberOf( v[i], 0.0, SIZE_MAX_MM );

                if( !n )
                    return false;

                m[i] = *n;
            }

            aOut.margins = m;
        }
        else if( key == "points" )
        {
            if( !v.is_array() || v.size() < 2 || v.size() > POINTS_MAX )
                return false;

            std::vector<VECTOR2I> pts;

            for( const json& p : v )
            {
                if( !p.is_array() || p.size() != 2 )
                    return false;

                std::optional<double> px = numberOf( p[0], -POSITION_MAX_MM, POSITION_MAX_MM );
                std::optional<double> py = numberOf( p[1], -POSITION_MAX_MM, POSITION_MAX_MM );

                if( !px || !py )
                    return false;

                pts.push_back( storedPoint( *px, *py, aYUp ) );
            }

            aOut.points = std::move( pts );
        }
        else if( key == "alternates" )
        {
            if( !v.is_array() || v.size() > ALTERNATES_MAX )
                return false;

            std::vector<SCH_PIN::ALT> alts;
            std::set<wxString>        names;

            for( const json& a : v )
            {
                if( !onlyKeys( a, { "name", "type", "shape" } )
                        || !hasKeys( a, { "name", "type", "shape" } ) )
                {
                    return false;
                }

                std::optional<wxString>           name = textOf( a["name"], 1, TEXT_MAX );
                std::optional<ELECTRICAL_PINTYPE> type = valueOf( PIN_TYPES, a["type"] );
                std::optional<GRAPHIC_PINSHAPE>   shape = valueOf( PIN_SHAPES, a["shape"] );

                if( !name || !type || !shape || !names.insert( *name ).second )
                    return false;

                SCH_PIN::ALT alt;
                alt.m_Name = *name;
                alt.m_Type = *type;
                alt.m_Shape = *shape;
                alts.push_back( alt );
            }

            aOut.alternates = std::move( alts );
        }
        else
        {
            return false;
        }
    }

    return !aOut.keys.empty();
}

inline void applyText( EDA_TEXT& aText, const ITEM_SET& aSet )
{
    if( aSet.text )
        aText.SetText( *aSet.text );

    if( aSet.angle )
        aText.SetTextAngle( *aSet.angle == 90 ? ANGLE_VERTICAL : ANGLE_HORIZONTAL );

    if( aSet.size )
        aText.SetTextSize( VECTOR2I( iuOf( *aSet.size ), iuOf( *aSet.size ) ) );

    if( aSet.bold )
        aText.SetBold( *aSet.bold );

    if( aSet.italic )
        aText.SetItalic( *aSet.italic );

    if( aSet.hJustify )
        aText.SetHorizJustify( *aSet.hJustify );

    if( aSet.vJustify )
        aText.SetVertJustify( *aSet.vJustify );

    if( aSet.visible )
        aText.SetVisible( *aSet.visible );
}

inline void applyShapeLook( EDA_SHAPE& aShape, SCH_SHAPE& aSchShape, const ITEM_SET& aSet )
{
    if( aSet.stroke )
        aSchShape.SetStroke( *aSet.stroke );

    if( aSet.fill )
    {
        aShape.SetFillMode( aSet.fill->type );
        aShape.SetFillColor( aSet.fill->color );
    }
}

inline void applyPoints( SCH_SHAPE& aShape, const std::vector<VECTOR2I>& aPoints )
{
    switch( aShape.GetShape() )
    {
    case SHAPE_T::RECTANGLE:
    case SHAPE_T::CIRCLE:
    case SHAPE_T::SEGMENT:
        aShape.SetStart( aPoints[0] );
        aShape.SetEnd( aPoints[1] );
        break;

    case SHAPE_T::ARC:
        aShape.SetArcGeometry( aPoints[0], aPoints[1], aPoints[2] );
        break;

    case SHAPE_T::BEZIER:
        aShape.SetStart( aPoints[0] );
        aShape.SetBezierC1( aPoints[1] );
        aShape.SetBezierC2( aPoints[2] );
        aShape.SetEnd( aPoints[3] );
        aShape.RebuildBezierToSegmentsPointsList( schIUScale.mmToIU( ARC_LOW_DEF_MM ) );
        break;

    case SHAPE_T::POLY:
    default:
        aShape.SetPolyPoints( aPoints );
        break;
    }
}

// ── text, text box and shape records (the symbol editor's, and a schematic's) ──────────

/** A text's record keys (SE 7.2's Text, less kind, uuid, unit and bodyStyle). */
inline void textItemJson( json& aOut, const SCH_TEXT& aText, bool aYUp = true )
{
    const VECTOR2I pos = aText.GetPosition();

    aOut["name"] = "";
    textJson( aOut, aText );
    aOut["x"] = mmOf( pos.x );
    aOut["y"] = mmOf( aYUp ? -pos.y : pos.y );
    aOut["showName"] = false;
}

/** A text box's record keys (SE 7.2's TextBox, less kind, uuid, unit and bodyStyle). */
inline void textBoxItemJson( json& aOut, const SCH_TEXTBOX& aBox, bool aYUp = true )
{
    const int ys = aYUp ? -1 : 1;

    textJson( aOut, static_cast<const EDA_TEXT&>( aBox ) );
    aOut["x"] = mmOf( aBox.GetStart().x );
    aOut["y"] = mmOf( ys * aBox.GetStart().y );
    aOut["x2"] = mmOf( aBox.GetEnd().x );
    aOut["y2"] = mmOf( ys * aBox.GetEnd().y );
    aOut["stroke"] = strokeJson( aBox.GetStroke() );
    aOut["fill"] = fillJson( aBox );
    aOut["margins"] = json::array( { mmOf( aBox.GetMarginLeft() ), mmOf( aBox.GetMarginTop() ),
                                     mmOf( aBox.GetMarginRight() ),
                                     mmOf( aBox.GetMarginBottom() ) } );
}

/** A shape's record keys (SE 7.2's Shape, less kind, uuid, unit and bodyStyle). */
inline void shapeItemJson( json& aOut, const SCH_SHAPE& aShape, bool aYUp = true )
{
    const char* shapeName = "polyline";

    aOut["points"] = shapePoints( aShape, shapeName, aYUp );
    aOut["shape"] = shapeName;
    aOut["stroke"] = strokeJson( aShape.GetStroke() );
    aOut["fill"] = fillJson( aShape );
}

/** Applies a read set's text keys and position to a text. */
inline void applyTextItem( SCH_TEXT& aText, const ITEM_SET& aSet, bool aYUp = true )
{
    VECTOR2I pos = aText.GetPosition();

    applyText( aText, aSet );

    if( aSet.x )
        pos.x = iuOf( *aSet.x );

    if( aSet.y )
        pos.y = aYUp ? -iuOf( *aSet.y ) : iuOf( *aSet.y );

    if( aSet.x || aSet.y )
        aText.SetPosition( pos );
}

/** Applies a read set's text, look, corners and margins to a text box. */
inline void applyTextBoxItem( SCH_TEXTBOX& aBox, const ITEM_SET& aSet, bool aYUp = true )
{
    VECTOR2I start = aBox.GetStart();
    VECTOR2I end = aBox.GetEnd();

    applyText( static_cast<EDA_TEXT&>( aBox ), aSet );
    applyShapeLook( aBox, aBox, aSet );

    if( aSet.x )
        start.x = iuOf( *aSet.x );

    if( aSet.y )
        start.y = aYUp ? -iuOf( *aSet.y ) : iuOf( *aSet.y );

    if( aSet.x2 )
        end.x = iuOf( *aSet.x2 );

    if( aSet.y2 )
        end.y = aYUp ? -iuOf( *aSet.y2 ) : iuOf( *aSet.y2 );

    if( aSet.x || aSet.y )
        aBox.SetStart( start );

    if( aSet.x2 || aSet.y2 )
        aBox.SetEnd( end );

    if( aSet.margins )
    {
        aBox.SetMarginLeft( iuOf( ( *aSet.margins )[0] ) );
        aBox.SetMarginTop( iuOf( ( *aSet.margins )[1] ) );
        aBox.SetMarginRight( iuOf( ( *aSet.margins )[2] ) );
        aBox.SetMarginBottom( iuOf( ( *aSet.margins )[3] ) );
    }
}

/** Applies a read set's look and points to a shape (points already in stored orientation). */
inline void applyShapeItem( SCH_SHAPE& aShape, const ITEM_SET& aSet )
{
    applyShapeLook( aShape, aShape, aSet );

    if( aSet.points )
        applyPoints( aShape, *aSet.points );
}

} // namespace cc_symedit

#endif // __EMSCRIPTEN__
