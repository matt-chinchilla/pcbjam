// Circuit Center (engine run 14, island cc23): properties, numbering and rules, the board
// engine's side. Spec: circuits-com docs/superpowers/specs/2026-11-16-properties-rules-
// design.md, section 5 and the table "Engine exports for run 14".
//
// Textually included by pcbnew_embind.cpp just before its EMSCRIPTEN_BINDINGS block (a
// separate file so four authors of run 14 do not edit the same lines): everything here sees
// that TU's includes and its anonymous namespace helpers (pcbFrame, toUtf8, ccIsRunDir,
// ccEnsureDir, ccWriteJson, ccText, ccNet, ccMm, ccLayer, ccLayerByName, ccIsLowerUuid,
// ccResolve, ccFindNet).
//
// The global edits' cores are KiCad 10.0's dialog bodies (pcbnew/dialogs/dialog_global_*,
// dialog_board_reannotate, dialog_exchange_footprints, dialog_unused_pad_layers, the two
// cleanup dialogs, GLOBAL_EDIT_TOOL::SwapLayers, BOARD_EDITOR_CONTROL::RepairBoard), carried
// here as free functions over our closed arguments, each preview reading only and each apply
// ONE BOARD_COMMIT. No KiCad dialog, message box or progress window on any road.

#include <board_stackup_manager/board_stackup.h>
#include <cleanup_item.h>
#include <collectors.h>
#include <common.h>
#include <drc/drc_rule_parser.h>
#include <eda_group.h>
#include <graphics_cleaner.h>
#include <page_info.h>
#include <pcb_generator.h>
#include <project/net_settings.h>
#include <refdes_utils.h>
#include <settings/json_settings_internals.h>
#include <teardrop/teardrop.h>
#include <title_block.h>
#include <tool/tools_holder.h>
#include <tracks_cleaner.h>
#include <font/font.h>
#include <wx/tokenzr.h>

namespace
{
constexpr size_t CC_PCB_ROWS_MAX = 4096;

/** A run's rows: the first CC_PCB_ROWS_MAX kept, every one counted. */
struct CC_PCB_ROWS
{
    json rows = json::array();
    int  count = 0;
    bool truncated = false;

    void Add( const BOARD_ITEM* aItem, const char* aKind, const wxString& aWords,
              const std::string& aFrom = "", const std::string& aTo = "",
              const std::string& aSymbolPath = "" )
    {
        count++;

        if( rows.size() >= CC_PCB_ROWS_MAX )
        {
            truncated = true;
            return;
        }

        json row = json::object();
        row["uuid"] = aItem ? toUtf8( aItem->m_Uuid.AsString() ) : std::string();
        row["kind"] = aKind;
        row["words"] = ccText( aWords, 256 );

        if( !aFrom.empty() || !aTo.empty() )
        {
            row["from"] = aFrom;
            row["to"] = aTo;
        }

        if( !aSymbolPath.empty() )
            row["symbolPath"] = aSymbolPath;

        rows.push_back( std::move( row ) );
    }
};

const char* ccPcbKind( const BOARD_ITEM* aItem )
{
    switch( aItem->Type() )
    {
    case PCB_TRACE_T:     return "track";
    case PCB_ARC_T:       return "arc";
    case PCB_VIA_T:       return "via";
    case PCB_PAD_T:       return "pad";
    case PCB_FOOTPRINT_T: return "footprint";
    case PCB_ZONE_T:      return "zone";
    case PCB_FIELD_T:     return "field";
    case PCB_TEXT_T:
    case PCB_TEXTBOX_T:
    case PCB_TABLECELL_T: return "text";
    case PCB_SHAPE_T:     return "shape";
    case PCB_MARKER_T:    return "marker";
    case PCB_GROUP_T:     return "group";
    default:
        if( BaseType( aItem->Type() ) == PCB_DIMENSION_T )
            return "dimension";

        return "item";
    }
}

std::string ccRulesMmText( int aIu )
{
    char buf[32];
    std::snprintf( buf, sizeof( buf ), "%g mm", ccMm( aIu ) );
    return buf;
}

/** A length in mm read into IU within [aMin, aMax] mm. */
std::optional<int> ccRulesMmToIu( const json& aValue, double aMin, double aMax )
{
    if( !aValue.is_number() )
        return std::nullopt;

    const double mm = aValue.get<double>();

    if( !std::isfinite( mm ) || mm < aMin || mm > aMax )
        return std::nullopt;

    return KiROUND( pcbIUScale.mmToIU( mm ) );
}

std::optional<bool> ccRulesBool( const json& aValue )
{
    if( !aValue.is_boolean() )
        return std::nullopt;

    return aValue.get<bool>();
}

std::optional<wxString> ccRulesString( const json& aValue, size_t aMin, size_t aMax )
{
    if( !aValue.is_string() )
        return std::nullopt;

    const std::string& raw = aValue.get_ref<const std::string&>();

    if( raw.find( '\0' ) != std::string::npos )
        return std::nullopt;

    wxString text = wxString::FromUTF8( raw.c_str(), raw.size() );

    if( ( !raw.empty() && text.IsEmpty() ) || text.length() < aMin || text.length() > aMax )
        return std::nullopt;

    for( wxUniChar c : text )
    {
        if( c < 0x20 || c == 0x7f )
            return std::nullopt;
    }

    return text;
}

/** An array of 1 to aMax lowercase uuids. */
bool ccRulesUuidSet( const json& aValue, std::set<KIID>& aOut, size_t aMax = CC_PCB_ROWS_MAX )
{
    if( !aValue.is_array() || aValue.empty() || aValue.size() > aMax )
        return false;

    for( const json& u : aValue )
    {
        if( !u.is_string() || !ccIsLowerUuid( u.get<std::string>() ) )
            return false;

        aOut.insert( KIID( wxString::FromUTF8( u.get<std::string>().c_str() ) ) );
    }

    return true;
}

/** An item named by aUuids, itself or its footprint, or (none named) the engine's selection. */
bool ccRulesPicked( const BOARD_ITEM* aItem, const std::set<KIID>& aUuids )
{
    if( aUuids.count( aItem->m_Uuid ) )
        return true;

    if( const FOOTPRINT* fp = aItem->GetParentFootprint() )
        return aUuids.count( fp->m_Uuid ) > 0;

    return false;
}

bool ccSelectedOrGrouped( const BOARD_ITEM* aItem )
{
    const EDA_ITEM* candidate = aItem;

    if( !candidate->IsSelected() && candidate->GetParent()
            && candidate->GetParent()->Type() == PCB_FOOTPRINT_T )
    {
        candidate = candidate->GetParent();
    }

    if( candidate->IsSelected() )
        return true;

    EDA_GROUP* group = candidate->GetParentGroup();

    while( group && !group->AsEdaItem()->IsSelected() )
        group = group->AsEdaItem()->GetParentGroup();

    return group != nullptr;
}

/** The connected-item filter the track dialogs share: nets, a net class, a layer, uuids. */
struct CC_NET_FILTER
{
    std::set<wxString>     nets;      // unescaped net names
    std::optional<wxString> netClass;
    PCB_LAYER_ID           layer = UNDEFINED_LAYER;
    std::set<KIID>         uuids;
    bool                   selected = false;

    bool Passes( BOARD_CONNECTED_ITEM* aItem ) const
    {
        if( !uuids.empty() && !ccRulesPicked( aItem, uuids ) )
            return false;

        if( selected && !ccSelectedOrGrouped( aItem ) )
            return false;

        if( !nets.empty() && !nets.count( UnescapeString( aItem->GetNetname() ) ) )
            return false;

        if( netClass )
        {
            NETCLASS* netclass = aItem->GetEffectiveNetClass();

            if( !netclass || !netclass->ContainsNetclassWithName( *netClass ) )
                return false;
        }

        if( layer != UNDEFINED_LAYER && aItem->GetLayer() != layer )
            return false;

        return true;
    }
};

/** Reads one key of a CC_NET_FILTER; false for a key it does not name or a bad value. */
bool ccReadNetFilterKey( BOARD* aBoard, const std::string& aKey, const json& aValue,
                         CC_NET_FILTER& aOut, bool& aKnown )
{
    aKnown = true;

    if( aKey == "nets" )
    {
        if( !aValue.is_array() || aValue.empty() || aValue.size() > 4096 )
            return false;

        for( const json& n : aValue )
        {
            std::optional<wxString> name = ccRulesString( n, 1, 512 );

            if( !name )
                return false;

            aOut.nets.insert( *name );
        }

        return true;
    }

    if( aKey == "netClass" )
        return ( aOut.netClass = ccRulesString( aValue, 1, 256 ) ).has_value();

    if( aKey == "layer" )
    {
        if( !aValue.is_string() )
            return false;

        aOut.layer = ccLayerByName( aBoard, aValue.get<std::string>() );
        return aOut.layer != UNDEFINED_LAYER;
    }

    if( aKey == "uuids" )
        return ccRulesUuidSet( aValue, aOut.uuids );

    if( aKey == "selected" )
    {
        std::optional<bool> on = ccRulesBool( aValue );
        aOut.selected = on.value_or( false );
        return on.has_value();
    }

    aKnown = false;
    return false;
}

// ───────────────────────────── board.edit: the ops ─────────────────────────────

/** A number in mm, or "netclass" (KiCad's "use the net class and rule values"). */
struct CC_SIZE
{
    bool netclass = false;
    int  iu = 0;
};

std::optional<CC_SIZE> ccRulesReadSize( const json& aValue, double aMin, double aMax )
{
    CC_SIZE size;

    if( aValue.is_string() && aValue.get<std::string>() == "netclass" )
    {
        size.netclass = true;
        return size;
    }

    std::optional<int> iu = ccRulesMmToIu( aValue, aMin, aMax );

    if( !iu )
        return std::nullopt;

    size.iu = *iu;
    return size;
}

struct CC_BOARD_EDIT
{
    std::string op;
    bool        preview = true;

    // tracksVias
    bool                   tracks = false, vias = false;
    bool                   throughVias = true, microVias = true, blindVias = true, buriedVias = true;
    std::optional<int>     trackWidthIs, viaSizeIs;   // KiCad's "filter by width / size"
    CC_NET_FILTER          net;
    std::optional<CC_SIZE> width, viaDiameter, viaDrill;
    PCB_LAYER_ID           setLayer = UNDEFINED_LAYER;
    std::optional<UNCONNECTED_LAYER_MODE> annularRings;

    // textGraphics
    bool references = false, values = false, otherFields = false, texts = false;
    bool graphics = false, dimensions = false;
    bool boardItems = true, footprintItems = true;
    std::optional<wxString> refGlob, footprintGlob;
    std::optional<bool>     visible, bold, italic, keepUpright, knockout, autoThickness, defaults;
    std::optional<int>      lineWidth, height, textWidth, thickness;

    // teardrops
    bool                  roundPads = false, rectPads = false, trackToTrack = false;
    bool                  pthPads = true, smdPads = true, existingOnly = false;
    std::optional<bool>   tdOn, curved, allowTwoTracks, preferZoneConnection;
    std::optional<double> lengthRatio, widthRatio;
    std::optional<int>    maxLength, maxWidth;

    // swapLayers
    std::map<PCB_LAYER_ID, PCB_LAYER_ID> layerMap;

    // deleteGlobal
    bool dZones = false, dTeardrops = false, dTexts = false, dGraphics = false, dFootprints = false;
    bool dTracks = false, dVias = false, dOutline = false, dMarkers = false, dAll = false;
    bool dLocked = false, dUnlocked = true;
    PCB_LAYER_ID dLayer = UNDEFINED_LAYER;

    // cleanupTracks
    bool mergeCollinear = false, removeDangling = false, removeShorts = false;
    bool redundantVias = false, tracksInPads = false, danglingVias = false, refillZones = false;

    // unusedPads
    bool upVias = false, upPads = false, keepOuter = false, upRemove = true;

    // cleanupGraphics
    bool mergeRects = false, fixOutline = false, deleteRedundant = false;
    int  tolerance = 0;

    // reannotate
    int                   order = 0;
    int                   roundIu = 0;
    bool                  byReference = false;
    int                   frontStart = 1, backStart = 0;
    wxString              frontPrefix, backPrefix;
    bool                  removeFrontPrefix = false, removeBackPrefix = false;
    bool                  skipLocked = false;
    std::string           scope = "all";
    std::vector<wxString> exclude;

    // zoneOrder
    std::vector<KIID> zoneOrder;
};

bool ccReadBoardEditArgs( BOARD* aBoard, const json& aFilter, const json& aSet, CC_BOARD_EDIT& a )
{
    if( !aFilter.is_object() || !aSet.is_object() )
        return false;

    const std::string& op = a.op;

    auto boolInto = [&]( const json& aValue, bool& aOut ) -> bool
                    {
                        std::optional<bool> on = ccRulesBool( aValue );

                        if( on )
                            aOut = *on;

                        return on.has_value();
                    };

    if( op == "tracksVias" )
    {
        for( const auto& [key, value] : aFilter.items() )
        {
            bool known = false;

            if( ccReadNetFilterKey( aBoard, key, value, a.net, known ) )
                continue;

            if( known )
                return false;

            if( key == "tracks" ) { if( !boolInto( value, a.tracks ) ) return false; }
            else if( key == "vias" ) { if( !boolInto( value, a.vias ) ) return false; }
            else if( key == "throughVias" ) { if( !boolInto( value, a.throughVias ) ) return false; }
            else if( key == "microVias" ) { if( !boolInto( value, a.microVias ) ) return false; }
            else if( key == "blindVias" ) { if( !boolInto( value, a.blindVias ) ) return false; }
            else if( key == "buriedVias" ) { if( !boolInto( value, a.buriedVias ) ) return false; }
            else if( key == "trackWidth" ) { if( !( a.trackWidthIs = ccRulesMmToIu( value, 0.001, 100 ) ) ) return false; }
            else if( key == "viaSize" ) { if( !( a.viaSizeIs = ccRulesMmToIu( value, 0.001, 100 ) ) ) return false; }
            else return false;
        }

        for( const auto& [key, value] : aSet.items() )
        {
            if( key == "width" ) { if( !( a.width = ccRulesReadSize( value, 0.001, 100 ) ) ) return false; }
            else if( key == "viaDiameter" ) { if( !( a.viaDiameter = ccRulesReadSize( value, 0.001, 100 ) ) ) return false; }
            else if( key == "viaDrill" ) { if( !( a.viaDrill = ccRulesReadSize( value, 0.001, 100 ) ) ) return false; }
            else if( key == "layer" )
            {
                if( !value.is_string() )
                    return false;

                a.setLayer = ccLayerByName( aBoard, value.get<std::string>() );

                if( a.setLayer == UNDEFINED_LAYER || !IsCopperLayer( a.setLayer ) )
                    return false;
            }
            else if( key == "annularRings" )
            {
                const std::string m = value.is_string() ? value.get<std::string>() : "";

                if( m == "keepAll" ) a.annularRings = UNCONNECTED_LAYER_MODE::KEEP_ALL;
                else if( m == "removeExceptStartAndEnd" ) a.annularRings = UNCONNECTED_LAYER_MODE::REMOVE_EXCEPT_START_AND_END;
                else if( m == "removeAll" ) a.annularRings = UNCONNECTED_LAYER_MODE::REMOVE_ALL;
                else if( m == "startEndOnly" ) a.annularRings = UNCONNECTED_LAYER_MODE::START_END_ONLY;
                else return false;
            }
            else return false;
        }

        return ( a.tracks || a.vias ) && !aSet.empty();
    }

    if( op == "textGraphics" )
    {
        for( const auto& [key, value] : aFilter.items() )
        {
            bool* flag = key == "references"       ? &a.references
                         : key == "values"         ? &a.values
                         : key == "otherFields"    ? &a.otherFields
                         : key == "texts"          ? &a.texts
                         : key == "graphics"       ? &a.graphics
                         : key == "dimensions"     ? &a.dimensions
                         : key == "boardItems"     ? &a.boardItems
                         : key == "footprintItems" ? &a.footprintItems
                         : key == "selected"       ? &a.net.selected
                                                   : nullptr;

            if( flag ) { if( !boolInto( value, *flag ) ) return false; }
            else if( key == "layer" )
            {
                if( !value.is_string() )
                    return false;

                a.net.layer = ccLayerByName( aBoard, value.get<std::string>() );

                if( a.net.layer == UNDEFINED_LAYER )
                    return false;
            }
            else if( key == "refGlob" ) { if( !( a.refGlob = ccRulesString( value, 1, 256 ) ) ) return false; }
            else if( key == "footprintGlob" ) { if( !( a.footprintGlob = ccRulesString( value, 1, 512 ) ) ) return false; }
            else if( key == "uuids" ) { if( !ccRulesUuidSet( value, a.net.uuids ) ) return false; }
            else return false;
        }

        for( const auto& [key, value] : aSet.items() )
        {
            if( key == "layer" )
            {
                if( !value.is_string() )
                    return false;

                a.setLayer = ccLayerByName( aBoard, value.get<std::string>() );

                if( a.setLayer == UNDEFINED_LAYER )
                    return false;
            }
            else if( key == "visible" ) { if( !( a.visible = ccRulesBool( value ) ) ) return false; }
            else if( key == "bold" ) { if( !( a.bold = ccRulesBool( value ) ) ) return false; }
            else if( key == "italic" ) { if( !( a.italic = ccRulesBool( value ) ) ) return false; }
            else if( key == "keepUpright" ) { if( !( a.keepUpright = ccRulesBool( value ) ) ) return false; }
            else if( key == "knockout" ) { if( !( a.knockout = ccRulesBool( value ) ) ) return false; }
            else if( key == "autoThickness" ) { if( !( a.autoThickness = ccRulesBool( value ) ) ) return false; }
            else if( key == "defaults" ) { if( !( a.defaults = ccRulesBool( value ) ) ) return false; }
            // TEXT_MIN_SIZE_MM and TEXT_MAX_SIZE_MM, the dialog's own bounds (0.001 to 250 mm).
            else if( key == "height" ) { if( !( a.height = ccRulesMmToIu( value, 0.001, 250 ) ) ) return false; }
            else if( key == "width" ) { if( !( a.textWidth = ccRulesMmToIu( value, 0.001, 250 ) ) ) return false; }
            else if( key == "thickness" ) { if( !( a.thickness = ccRulesMmToIu( value, 0.001, 50 ) ) ) return false; }
            else if( key == "lineWidth" ) { if( !( a.lineWidth = ccRulesMmToIu( value, 0.001, 50 ) ) ) return false; }
            else return false;
        }

        return ( a.references || a.values || a.otherFields || a.texts || a.graphics || a.dimensions )
               && !aSet.empty();
    }

    if( op == "teardrops" )
    {
        for( const auto& [key, value] : aFilter.items() )
        {
            bool known = false;

            if( ccReadNetFilterKey( aBoard, key, value, a.net, known ) )
                continue;

            if( known )
                return false;

            if( key == "roundPads" ) { if( !boolInto( value, a.roundPads ) ) return false; }
            else if( key == "rectPads" ) { if( !boolInto( value, a.rectPads ) ) return false; }
            else if( key == "vias" ) { if( !boolInto( value, a.vias ) ) return false; }
            else if( key == "trackToTrack" ) { if( !boolInto( value, a.trackToTrack ) ) return false; }
            else if( key == "pthPads" ) { if( !boolInto( value, a.pthPads ) ) return false; }
            else if( key == "smdPads" ) { if( !boolInto( value, a.smdPads ) ) return false; }
            else if( key == "existing" ) { if( !boolInto( value, a.existingOnly ) ) return false; }
            else return false;
        }

        for( const auto& [key, value] : aSet.items() )
        {
            if( key == "on" ) { if( !( a.tdOn = ccRulesBool( value ) ) ) return false; }
            else if( key == "curved" ) { if( !( a.curved = ccRulesBool( value ) ) ) return false; }
            else if( key == "allowTwoTracks" ) { if( !( a.allowTwoTracks = ccRulesBool( value ) ) ) return false; }
            else if( key == "preferZoneConnection" ) { if( !( a.preferZoneConnection = ccRulesBool( value ) ) ) return false; }
            else if( key == "lengthRatio" || key == "widthRatio" )
            {
                if( !value.is_number() )
                    return false;

                const double r = value.get<double>();

                if( !std::isfinite( r ) || r <= 0.0 || r > 10.0 )
                    return false;

                ( key == "lengthRatio" ? a.lengthRatio : a.widthRatio ) = r;
            }
            else if( key == "maxLength" ) { if( !( a.maxLength = ccRulesMmToIu( value, 0.0, 100 ) ) ) return false; }
            else if( key == "maxWidth" ) { if( !( a.maxWidth = ccRulesMmToIu( value, 0.0, 100 ) ) ) return false; }
            else return false;
        }

        return ( a.roundPads || a.rectPads || a.vias || a.trackToTrack ) && !aSet.empty();
    }

    if( op == "swapLayers" )
    {
        if( !aFilter.empty() || aSet.size() != 1 || !aSet.contains( "map" ) || !aSet["map"].is_object()
                || aSet["map"].empty() )
        {
            return false;
        }

        for( const auto& [from, to] : aSet["map"].items() )
        {
            if( !to.is_string() )
                return false;

            const PCB_LAYER_ID f = ccLayerByName( aBoard, from );
            const PCB_LAYER_ID t = ccLayerByName( aBoard, to.get<std::string>() );

            if( f == UNDEFINED_LAYER || t == UNDEFINED_LAYER )
                return false;

            a.layerMap[f] = t;
        }

        return true;
    }

    if( op == "deleteGlobal" )
    {
        for( const auto& [key, value] : aFilter.items() )
        {
            bool* flag = key == "zones"        ? &a.dZones
                         : key == "teardrops"  ? &a.dTeardrops
                         : key == "texts"      ? &a.dTexts
                         : key == "graphics"   ? &a.dGraphics
                         : key == "footprints" ? &a.dFootprints
                         : key == "tracks"     ? &a.dTracks
                         : key == "vias"       ? &a.dVias
                         : key == "outline"    ? &a.dOutline
                         : key == "markers"    ? &a.dMarkers
                         : key == "all"        ? &a.dAll
                         : key == "locked"     ? &a.dLocked
                         : key == "unlocked"   ? &a.dUnlocked
                                               : nullptr;

            if( flag ) { if( !boolInto( value, *flag ) ) return false; }
            else if( key == "layer" )
            {
                if( !value.is_string() )
                    return false;

                a.dLayer = ccLayerByName( aBoard, value.get<std::string>() );

                if( a.dLayer == UNDEFINED_LAYER )
                    return false;
            }
            else return false;
        }

        return aSet.empty()
               && ( a.dZones || a.dTeardrops || a.dTexts || a.dGraphics || a.dFootprints
                    || a.dTracks || a.dVias || a.dOutline || a.dMarkers || a.dAll );
    }

    if( op == "cleanupTracks" )
    {
        for( const auto& [key, value] : aFilter.items() )
        {
            bool known = false;

            if( !ccReadNetFilterKey( aBoard, key, value, a.net, known ) )
                return false;
        }

        for( const auto& [key, value] : aSet.items() )
        {
            bool* flag = key == "mergeCollinear"   ? &a.mergeCollinear
                         : key == "removeDangling" ? &a.removeDangling
                         : key == "removeShorts"   ? &a.removeShorts
                         : key == "redundantVias"  ? &a.redundantVias
                         : key == "tracksInPads"   ? &a.tracksInPads
                         : key == "danglingVias"   ? &a.danglingVias
                         : key == "refillZones"    ? &a.refillZones
                                                   : nullptr;

            if( !flag || !boolInto( value, *flag ) )
                return false;
        }

        return !aSet.empty();
    }

    if( op == "unusedPads" )
    {
        for( const auto& [key, value] : aFilter.items() )
        {
            if( key != "uuids" || !ccRulesUuidSet( value, a.net.uuids ) )
                return false;
        }

        for( const auto& [key, value] : aSet.items() )
        {
            bool* flag = key == "vias"        ? &a.upVias
                         : key == "pads"      ? &a.upPads
                         : key == "keepOuter" ? &a.keepOuter
                         : key == "remove"    ? &a.upRemove
                                              : nullptr;

            if( !flag || !boolInto( value, *flag ) )
                return false;
        }

        return a.upVias || a.upPads;
    }

    if( op == "cleanupGraphics" )
    {
        if( !aFilter.empty() )
            return false;

        a.tolerance = KiROUND( pcbIUScale.mmToIU( 0.01 ) );   // the dialog's default

        for( const auto& [key, value] : aSet.items() )
        {
            if( key == "tolerance" )
            {
                std::optional<int> iu = ccRulesMmToIu( value, 0.0, 10.0 );

                if( !iu )
                    return false;

                a.tolerance = *iu;
                continue;
            }

            bool* flag = key == "mergeRects"        ? &a.mergeRects
                         : key == "fixOutline"      ? &a.fixOutline
                         : key == "deleteRedundant" ? &a.deleteRedundant
                                                    : nullptr;

            if( !flag || !boolInto( value, *flag ) )
                return false;
        }

        return a.mergeRects || a.fixOutline || a.deleteRedundant;
    }

    if( op == "repair" )
        return aFilter.empty() && aSet.empty();

    if( op == "reannotate" )
    {
        if( !aFilter.empty() )
            return false;

        a.roundIu = KiROUND( pcbIUScale.mmToIU( 1.27 ) );

        for( const auto& [key, value] : aSet.items() )
        {
            if( key == "order" )
            {
                if( !value.is_number_integer() || value.get<long long>() < 0 || value.get<long long>() > 7 )
                    return false;

                a.order = (int) value.get<long long>();
            }
            else if( key == "round" )
            {
                std::optional<int> iu = ccRulesMmToIu( value, 0.001, 100 );

                if( !iu )
                    return false;

                a.roundIu = *iu;
            }
            else if( key == "by" )
            {
                const std::string by = value.is_string() ? value.get<std::string>() : "";

                if( by != "footprint" && by != "reference" )
                    return false;

                a.byReference = by == "reference";
            }
            else if( key == "frontStart" || key == "backStart" )
            {
                if( !value.is_number_integer() || value.get<long long>() < 0
                        || value.get<long long>() > 1000000 )
                {
                    return false;
                }

                ( key == "frontStart" ? a.frontStart : a.backStart ) = (int) value.get<long long>();
            }
            else if( key == "frontPrefix" || key == "backPrefix" )
            {
                std::optional<wxString> prefix = ccRulesString( value, 0, 32 );

                if( !prefix )
                    return false;

                ( key == "frontPrefix" ? a.frontPrefix : a.backPrefix ) = *prefix;
            }
            else if( key == "removePrefix" )
            {
                if( !boolInto( value, a.removeFrontPrefix ) )
                    return false;

                a.removeBackPrefix = a.removeFrontPrefix;
            }
            else if( key == "removeFrontPrefix" ) { if( !boolInto( value, a.removeFrontPrefix ) ) return false; }
            else if( key == "removeBackPrefix" ) { if( !boolInto( value, a.removeBackPrefix ) ) return false; }
            else if( key == "skipLocked" ) { if( !boolInto( value, a.skipLocked ) ) return false; }
            else if( key == "scope" )
            {
                a.scope = value.is_string() ? value.get<std::string>() : "";

                if( a.scope != "all" && a.scope != "selected" && a.scope != "front" && a.scope != "back" )
                    return false;
            }
            else if( key == "uuids" ) { if( !ccRulesUuidSet( value, a.net.uuids ) ) return false; }
            else if( key == "exclude" )
            {
                std::optional<wxString> list = ccRulesString( value, 0, 4096 );

                if( !list )
                    return false;

                wxStringTokenizer tokenizer( *list, ", \t", wxTOKEN_STRTOK );

                while( tokenizer.HasMoreTokens() )
                    a.exclude.push_back( tokenizer.GetNextToken() );
            }
            else return false;
        }

        return true;
    }

    if( op == "zoneOrder" )
    {
        if( !aFilter.empty() || aSet.size() != 1 || !aSet.contains( "order" )
                || !aSet["order"].is_array() || aSet["order"].empty() || aSet["order"].size() > 4096 )
        {
            return false;
        }

        std::set<KIID> seen;

        for( const json& u : aSet["order"] )
        {
            if( !u.is_string() || !ccIsLowerUuid( u.get<std::string>() ) )
                return false;

            KIID id( wxString::FromUTF8( u.get<std::string>().c_str() ) );
            BOARD_ITEM* item = aBoard->ResolveItem( id, true );

            if( !item || item->Type() != PCB_ZONE_T || !seen.insert( id ).second )
                return false;

            a.zoneOrder.push_back( id );
        }

        return true;
    }

    return false;
}

/** tracksVias: DIALOG_GLOBAL_EDIT_TRACKS_AND_VIAS's visit and process. */
void ccOpTracksVias( PCB_EDIT_FRAME* aFrame, BOARD_COMMIT& aCommit, const CC_BOARD_EDIT& a,
                     CC_PCB_ROWS& aRows )
{
    BOARD* board = aFrame->GetBoard();

    for( PCB_TRACK* track : board->Tracks() )
    {
        const bool isVia = track->Type() == PCB_VIA_T;
        PCB_VIA*   via = isVia ? static_cast<PCB_VIA*>( track ) : nullptr;

        if( isVia )
        {
            if( !a.vias )
                continue;

            const VIATYPE type = via->GetViaType();

            if( ( type == VIATYPE::THROUGH && !a.throughVias ) || ( type == VIATYPE::MICROVIA && !a.microVias )
                    || ( type == VIATYPE::BLIND && !a.blindVias ) || ( type == VIATYPE::BURIED && !a.buriedVias ) )
            {
                continue;
            }

            if( a.viaSizeIs && via->GetWidth() != *a.viaSizeIs )
                continue;
        }
        else
        {
            if( !a.tracks )
                continue;

            if( a.trackWidthIs && track->GetWidth() != *a.trackWidthIs )
                continue;
        }

        if( !a.net.Passes( track ) )
            continue;

        int  newWidth = track->GetWidth();
        int  newDrill = via ? via->GetDrillValue() : 0;
        bool layerChange = false;
        bool ringsChange = false;

        if( !isVia && a.width )
        {
            if( a.width->netclass )
            {
                MINOPTMAX<int> constraint = track->GetWidthConstraint();

                if( constraint.HasOpt() )
                    newWidth = constraint.Opt();
                else if( constraint.Min() > 0 )
                    newWidth = constraint.Min();
            }
            else
            {
                newWidth = a.width->iu;
            }
        }

        if( via && a.viaDiameter )
        {
            if( a.viaDiameter->netclass )
            {
                MINOPTMAX<int> constraint = via->GetWidthConstraint();

                if( constraint.HasOpt() )
                    newWidth = constraint.Opt();
                else if( constraint.Min() > 0 )
                    newWidth = constraint.Min();
            }
            else
            {
                newWidth = a.viaDiameter->iu;
            }
        }

        if( via && a.viaDrill )
        {
            if( a.viaDrill->netclass )
            {
                MINOPTMAX<int> constraint = via->GetDrillConstraint();

                if( constraint.HasOpt() )
                    newDrill = constraint.Opt();
                else if( constraint.Min() > 0 )
                    newDrill = constraint.Min();
            }
            else
            {
                newDrill = a.viaDrill->iu;
            }
        }

        if( !isVia && a.setLayer != UNDEFINED_LAYER && track->GetLayer() != a.setLayer )
            layerChange = true;

        if( via && a.annularRings && via->Padstack().UnconnectedLayerMode() != *a.annularRings )
            ringsChange = true;

        const bool widthChange = newWidth > 0 && newWidth != track->GetWidth();
        const bool drillChange = via && newDrill > 0 && newDrill != via->GetDrillValue();

        if( !widthChange && !drillChange && !layerChange && !ringsChange )
            continue;

        wxString words = wxString::Format( wxS( "%s of %s" ), isVia ? wxS( "Via" ) : wxS( "Track" ),
                                           UnescapeString( track->GetNetname() ) );
        std::string from;
        std::string to;

        if( widthChange )
        {
            from = ccRulesMmText( track->GetWidth() );
            to = ccRulesMmText( newWidth );
        }
        else if( drillChange )
        {
            from = ccRulesMmText( via->GetDrillValue() );
            to = ccRulesMmText( newDrill );
        }
        else if( layerChange )
        {
            from = ccLayer( track->GetLayer() );
            to = ccLayer( a.setLayer );
        }

        if( !a.preview )
        {
            aCommit.Modify( track );

            if( widthChange )
                track->SetWidth( newWidth );

            if( drillChange )
                via->SetDrill( newDrill );

            if( layerChange )
                track->SetLayer( a.setLayer );

            if( ringsChange )
                via->Padstack().SetUnconnectedLayerMode( *a.annularRings );
        }

        aRows.Add( track, ccPcbKind( track ), words, from, to );
    }
}

/** textGraphics: DIALOG_GLOBAL_EDIT_TEXT_AND_GRAPHICS (pcbnew) visit and process; colors never. */
void ccOpTextGraphics( PCB_EDIT_FRAME* aFrame, BOARD_COMMIT& aCommit, const CC_BOARD_EDIT& a,
                       CC_PCB_ROWS& aRows )
{
    BOARD*                       board = aFrame->GetBoard();
    const BOARD_DESIGN_SETTINGS& bds = board->GetDesignSettings();

    auto process =
            [&]( BOARD_ITEM* aItem )
            {
                if( !a.net.uuids.empty() && !ccRulesPicked( aItem, a.net.uuids ) )
                    return;

                if( a.net.selected && !ccSelectedOrGrouped( aItem ) )
                    return;

                if( a.net.layer != UNDEFINED_LAYER && aItem->GetLayer() != a.net.layer )
                    return;

                if( FOOTPRINT* fp = aItem->GetParentFootprint() )
                {
                    if( a.refGlob && !WildCompareString( *a.refGlob, fp->GetReference(), false ) )
                        return;

                    if( a.footprintGlob
                            && !WildCompareString( *a.footprintGlob,
                                                   wxString::FromUTF8( fp->GetFPID().Format().c_str() ),
                                                   false ) )
                    {
                        return;
                    }
                }
                else if( a.refGlob || a.footprintGlob )
                {
                    return;   // a designator or footprint filter names footprint items only
                }

                PCB_FIELD*          field = dynamic_cast<PCB_FIELD*>( aItem );
                EDA_TEXT*           text = dynamic_cast<EDA_TEXT*>( aItem );
                PCB_SHAPE*          shape = dynamic_cast<PCB_SHAPE*>( aItem );
                PCB_DIMENSION_BASE* dimension = dynamic_cast<PCB_DIMENSION_BASE*>( aItem );
                FOOTPRINT*          parentFP = aItem->GetParentFootprint();
                std::vector<std::string> words;
                std::string              from;
                std::string              to;

                if( a.defaults && *a.defaults )
                {
                    words.push_back( "defaults" );
                }
                else
                {
                    if( a.setLayer != UNDEFINED_LAYER && aItem->GetLayer() != a.setLayer )
                    {
                        words.push_back( "layer" );
                        from = ccLayer( aItem->GetLayer() );
                        to = ccLayer( a.setLayer );
                    }

                    if( text )
                    {
                        if( a.textWidth && text->GetTextSize().x != *a.textWidth )
                            words.push_back( "width" );

                        if( a.height && text->GetTextSize().y != *a.height )
                        {
                            words.push_back( "height" );

                            if( from.empty() )
                            {
                                from = ccRulesMmText( text->GetTextSize().y );
                                to = ccRulesMmText( *a.height );
                            }
                        }

                        if( a.autoThickness && *a.autoThickness && !text->GetAutoThickness() )
                            words.push_back( "thickness" );
                        else if( a.thickness && text->GetTextThickness() != *a.thickness )
                            words.push_back( "thickness" );

                        if( a.bold && text->IsBold() != *a.bold )
                            words.push_back( "bold" );

                        if( a.italic && text->IsItalic() != *a.italic )
                            words.push_back( "italic" );

                        if( parentFP && a.keepUpright && text->IsKeepUpright() != *a.keepUpright )
                            words.push_back( "keepUpright" );
                    }

                    if( a.knockout && aItem->IsKnockout() != *a.knockout )
                        words.push_back( "knockout" );

                    if( field && a.visible && field->IsVisible() != *a.visible )
                        words.push_back( "visible" );

                    if( a.lineWidth )
                    {
                        if( shape && shape->GetStroke().GetWidth() != *a.lineWidth )
                            words.push_back( "lineWidth" );
                        else if( dimension && dimension->GetLineThickness() != *a.lineWidth )
                            words.push_back( "lineWidth" );
                    }
                }

                if( words.empty() )
                    return;

                if( !a.preview )
                {
                    aCommit.Modify( aItem );

                    if( a.defaults && *a.defaults )
                    {
                        aItem->StyleFromSettings( bds, false );
                    }
                    else
                    {
                        if( a.setLayer != UNDEFINED_LAYER )
                            aItem->SetLayer( a.setLayer );

                        if( text )
                        {
                            if( a.textWidth )
                                text->SetTextSize( VECTOR2I( *a.textWidth, text->GetTextSize().y ) );

                            if( a.height )
                                text->SetTextSize( VECTOR2I( text->GetTextSize().x, *a.height ) );

                            if( a.autoThickness && *a.autoThickness )
                                text->SetAutoThickness( true );
                            else if( a.thickness )
                                text->SetTextThickness( *a.thickness );

                            if( a.bold )
                                text->SetBold( *a.bold );

                            if( a.italic )
                                text->SetItalic( *a.italic );

                            if( ( a.bold || a.italic ) && !text->GetFontName().IsEmpty() )
                            {
                                text->SetFont( KIFONT::FONT::GetFont( text->GetFontName(), text->IsBold(),
                                                                      text->IsItalic(),
                                                                      board->GetEmbeddedFiles()->GetFontFiles() ) );
                            }

                            if( parentFP && a.keepUpright )
                                text->SetKeepUpright( *a.keepUpright );
                        }

                        if( a.knockout )
                            aItem->SetIsKnockout( *a.knockout );

                        if( field && a.visible )
                            field->SetVisible( *a.visible );

                        if( a.lineWidth )
                        {
                            if( shape )
                            {
                                STROKE_PARAMS stroke = shape->GetStroke();
                                stroke.SetWidth( *a.lineWidth );
                                shape->SetStroke( stroke );
                            }

                            if( dimension )
                                dimension->SetLineThickness( *a.lineWidth );
                        }
                    }
                }

                wxString owner;

                if( parentFP )
                    owner = parentFP->GetReference() + wxS( " " );

                if( field )
                    owner += field->GetName();
                else if( text )
                    owner += wxS( "text" );
                else
                    owner += wxString::FromUTF8( ccPcbKind( aItem ) );

                std::string joined;

                for( const std::string& w : words )
                    joined += ( joined.empty() ? "" : " " ) + w;

                aRows.Add( aItem, ccPcbKind( aItem ), owner + wxS( ": " ) + wxString::FromUTF8( joined.c_str() ),
                           from, to );
            };

    auto visitGraphical =
            [&]( BOARD_ITEM* aItem )
            {
                const KICAD_T type = aItem->Type();

                if( type == PCB_TEXT_T || type == PCB_TEXTBOX_T )
                {
                    EDA_TEXT* textItem = dynamic_cast<EDA_TEXT*>( aItem );

                    if( a.texts )
                        process( aItem );
                    else if( a.references && textItem && textItem->GetText() == wxT( "${REFERENCE}" ) )
                        process( aItem );
                    else if( a.values && textItem && textItem->GetText() == wxT( "${VALUE}" ) )
                        process( aItem );
                }
                else if( type == PCB_TABLE_T )
                {
                    if( a.texts )
                    {
                        aItem->RunOnChildren(
                                [&]( BOARD_ITEM* aChild )
                                {
                                    if( aChild->Type() == PCB_TABLECELL_T )
                                        process( aChild );
                                },
                                RECURSE_MODE::NO_RECURSE );
                    }
                }
                else if( BaseType( type ) == PCB_DIMENSION_T )
                {
                    if( a.dimensions )
                        process( aItem );
                }
                else if( type == PCB_SHAPE_T || type == PCB_BARCODE_T )
                {
                    if( a.graphics )
                        process( aItem );
                }
            };

    if( a.footprintItems )
    {
        for( FOOTPRINT* fp : board->Footprints() )
        {
            if( a.references )
                process( &fp->Reference() );

            if( a.values )
                process( &fp->Value() );

            for( PCB_FIELD* field : fp->GetFields() )
            {
                if( !field || field->IsReference() || field->IsValue() )
                    continue;

                if( a.otherFields )
                    process( field );
                else if( a.references && field->GetText() == wxT( "${REFERENCE}" ) )
                    process( field );
                else if( a.values && field->GetText() == wxT( "${VALUE}" ) )
                    process( field );
            }

            for( BOARD_ITEM* item : fp->GraphicalItems() )
                visitGraphical( item );
        }
    }

    if( a.boardItems )
    {
        for( BOARD_ITEM* item : board->Drawings() )
            visitGraphical( item );
    }
}

/** teardrops: DIALOG_GLOBAL_EDIT_TEARDROPS's visit and process, pads and vias by our filter. */
void ccOpTeardrops( PCB_EDIT_FRAME* aFrame, BOARD_COMMIT& aCommit, const CC_BOARD_EDIT& a,
                    CC_PCB_ROWS& aRows )
{
    BOARD*                 board = aFrame->GetBoard();
    BOARD_DESIGN_SETTINGS& bds = board->GetDesignSettings();
    const bool             specified = a.lengthRatio || a.widthRatio || a.maxLength || a.maxWidth
                                       || a.curved || a.allowTwoTracks || a.preferZoneConnection;
    const bool             removing = a.tdOn && !*a.tdOn;

    if( !a.preview )
        board->SetLegacyTeardrops( false );

    auto process =
            [&]( BOARD_CONNECTED_ITEM* aItem )
            {
                if( !a.net.Passes( aItem ) )
                    return;

                const bool round = TEARDROP_MANAGER::IsRound( aItem, PADSTACK::ALL_LAYERS );

                if( aItem->Type() == PCB_PAD_T && !( ( round && a.roundPads ) || ( !round && a.rectPads ) ) )
                    return;

                TEARDROP_PARAMETERS& params = aItem->GetTeardropParams();

                if( a.existingOnly && !params.m_Enabled )
                    return;

                TEARDROP_PARAMETERS next = params;

                if( removing )
                {
                    next.m_Enabled = false;
                }
                else if( !specified )
                {
                    next = *bds.GetTeadropParamsList()->GetParameters( round ? TARGET_ROUND : TARGET_RECT );
                    next.m_Enabled = true;
                }
                else
                {
                    if( a.lengthRatio )
                        next.m_BestLengthRatio = *a.lengthRatio;

                    if( a.widthRatio )
                        next.m_BestWidthRatio = *a.widthRatio;

                    if( a.maxLength )
                        next.m_TdMaxLen = *a.maxLength;

                    if( a.maxWidth )
                        next.m_TdMaxWidth = *a.maxWidth;

                    if( a.curved )
                        next.m_CurvedEdges = *a.curved;

                    if( a.allowTwoTracks )
                        next.m_AllowUseTwoTracks = *a.allowTwoTracks;

                    if( a.preferZoneConnection )
                        next.m_TdOnPadsInZones = !*a.preferZoneConnection;

                    if( !a.existingOnly )
                        next.m_Enabled = true;
                }

                if( next.m_Enabled == params.m_Enabled && next.m_BestLengthRatio == params.m_BestLengthRatio
                        && next.m_BestWidthRatio == params.m_BestWidthRatio
                        && next.m_TdMaxLen == params.m_TdMaxLen && next.m_TdMaxWidth == params.m_TdMaxWidth
                        && next.m_CurvedEdges == params.m_CurvedEdges
                        && next.m_AllowUseTwoTracks == params.m_AllowUseTwoTracks
                        && next.m_TdOnPadsInZones == params.m_TdOnPadsInZones )
                {
                    return;
                }

                if( !a.preview )
                {
                    aCommit.Modify( aItem );
                    aItem->GetTeardropParams() = next;
                }

                wxString owner;

                if( aItem->Type() == PCB_PAD_T )
                {
                    const PAD* pad = static_cast<const PAD*>( aItem );
                    owner = wxString::Format( wxS( "Pad %s of %s" ), pad->GetNumber(),
                                              pad->GetParentFootprint() ? pad->GetParentFootprint()->GetReference()
                                                                        : wxString() );
                }
                else
                {
                    owner = wxString::Format( wxS( "Via of %s" ), UnescapeString( aItem->GetNetname() ) );
                }

                aRows.Add( aItem, ccPcbKind( aItem ),
                           owner + ( removing ? wxS( ": teardrop removed" )
                                              : ( params.m_Enabled ? wxS( ": teardrop changed" )
                                                                   : wxS( ": teardrop added" ) ) ) );
            };

    if( a.vias )
    {
        for( PCB_TRACK* track : board->Tracks() )
        {
            if( track->Type() == PCB_VIA_T )
                process( track );
        }
    }

    if( a.roundPads || a.rectPads )
    {
        for( FOOTPRINT* fp : board->Footprints() )
        {
            for( PAD* pad : fp->Pads() )
            {
                const bool pth = pad->GetAttribute() == PAD_ATTRIB::PTH;
                const bool smd = pad->GetAttribute() == PAD_ATTRIB::SMD || pad->GetAttribute() == PAD_ATTRIB::CONN;

                if( ( pth && a.pthPads ) || ( smd && a.smdPads ) )
                    process( pad );
            }
        }
    }

    if( a.trackToTrack )
    {
        TEARDROP_PARAMETERS* trackParams = bds.GetTeadropParamsList()->GetParameters( TARGET_TRACK );

        aRows.Add( nullptr, "track", removing ? wxS( "Track to track teardrops removed" )
                                              : wxS( "Track to track teardrops added" ) );

        if( !a.preview )
        {
            TEARDROP_MANAGER teardropManager( board, aFrame->GetToolManager() );
            teardropManager.DeleteTrackToTrackTeardrops( aCommit );
            teardropManager.BuildTrackCaches();

            if( removing )
            {
                trackParams->m_Enabled = false;
            }
            else
            {
                trackParams->m_Enabled = true;
                teardropManager.AddTeardropsOnTracks( aCommit, nullptr, true );
            }
        }
    }
}

/** swapLayers: GLOBAL_EDIT_TOOL::SwapLayers with the map given (layers not named stay). */
void ccOpSwapLayers( PCB_EDIT_FRAME* aFrame, BOARD_COMMIT& aCommit, const CC_BOARD_EDIT& a,
                     CC_PCB_ROWS& aRows )
{
    BOARD* board = aFrame->GetBoard();

    auto mapped = [&]( PCB_LAYER_ID aLayer ) -> PCB_LAYER_ID
                  {
                      auto it = a.layerMap.find( aLayer );
                      return it == a.layerMap.end() ? aLayer : it->second;
                  };

    auto swapItem =
            [&]( BOARD_ITEM* aItem )
            {
                LSET original = aItem->GetLayerSet();
                LSET next;

                for( PCB_LAYER_ID layer : original )
                    next.set( mapped( layer ) );

                if( original == next )
                    return;

                if( !a.preview )
                {
                    aCommit.Modify( aItem );
                    aItem->SetLayerSet( next );
                    aFrame->GetCanvas()->GetView()->Update( aItem, KIGFX::GEOMETRY );
                }

                aRows.Add( aItem, ccPcbKind( aItem ), wxS( "Moved to another layer" ),
                           ccLayer( aItem->GetLayer() ), ccLayer( mapped( aItem->GetLayer() ) ) );
            };

    for( PCB_TRACK* segm : board->Tracks() )
    {
        if( segm->Type() == PCB_VIA_T )
        {
            PCB_VIA* via = static_cast<PCB_VIA*>( segm );

            if( via->GetViaType() == VIATYPE::THROUGH )
                continue;

            PCB_LAYER_ID top, bottom;
            via->LayerPair( &top, &bottom );

            if( mapped( top ) == top && mapped( bottom ) == bottom )
                continue;

            if( !a.preview )
            {
                aCommit.Modify( via );
                via->SetLayerPair( mapped( top ), mapped( bottom ) );
                aFrame->GetCanvas()->GetView()->Update( via, KIGFX::GEOMETRY );
            }

            aRows.Add( via, "via", wxS( "Via layers moved" ),
                       ccLayer( top ) + "-" + ccLayer( bottom ),
                       ccLayer( mapped( top ) ) + "-" + ccLayer( mapped( bottom ) ) );
        }
        else
        {
            swapItem( segm );
        }
    }

    for( PCB_GENERATOR* generator : board->Generators() )
        swapItem( generator );

    for( ZONE* zone : board->Zones() )
        swapItem( zone );

    for( BOARD_ITEM* drawing : board->Drawings() )
        swapItem( drawing );
}

/** deleteGlobal: DIALOG_GLOBAL_DELETION::DoGlobalDeletions over our filter. */
void ccOpDeleteGlobal( PCB_EDIT_FRAME* aFrame, BOARD_COMMIT& aCommit, const CC_BOARD_EDIT& a,
                       CC_PCB_ROWS& aRows, bool& aRatsnest )
{
    BOARD* board = aFrame->GetBoard();
    LSET   allLayers = LSET().set();
    LSET   layersFilter = allLayers;

    if( a.dLayer != UNDEFINED_LAYER )
    {
        layersFilter = LSET();
        layersFilter.set( a.dLayer );
    }

    auto lockPasses = [&]( const BOARD_ITEM* aItem ) -> bool
                      {
                          return aItem->IsLocked() ? a.dLocked : a.dUnlocked;
                      };

    auto remove =
            [&]( BOARD_ITEM* aItem, const LSET& aMask, bool aConnected )
            {
                if( !( aItem->GetLayerSet() & aMask ).any() )
                    return;

                if( !a.preview )
                    aCommit.Remove( aItem );

                if( aConnected )
                    aRatsnest = true;

                wxString words = wxS( "Deleted" );

                if( aItem->Type() == PCB_FOOTPRINT_T )
                    words = static_cast<FOOTPRINT*>( aItem )->GetReference() + wxS( " deleted" );

                aRows.Add( aItem, ccPcbKind( aItem ), words );
            };

    if( !a.preview )
        aFrame->GetToolManager()->RunAction( ACTIONS::selectionClear );

    for( ZONE* zone : board->Zones() )
    {
        if( a.dAll )
            remove( zone, allLayers, true );
        else if( zone->IsTeardropArea() ? a.dTeardrops : a.dZones )
            remove( zone, layersFilter, true );
    }

    if( a.dAll || a.dGraphics || a.dOutline || a.dTexts )
    {
        LSET drawingLayers;

        if( a.dGraphics )
            drawingLayers = LSET::AllNonCuMask().set( Edge_Cuts, false );

        if( a.dOutline )
            drawingLayers.set( Edge_Cuts );

        drawingLayers &= layersFilter;

        for( BOARD_ITEM* item : board->Drawings() )
        {
            if( a.dAll )
                remove( item, allLayers, false );
            else if( item->Type() == PCB_SHAPE_T && ( a.dGraphics || a.dOutline ) )
            {
                if( lockPasses( item ) )
                    remove( item, drawingLayers, false );
            }
            else if( a.dTexts && ( item->Type() == PCB_TEXT_T || item->Type() == PCB_TEXTBOX_T ) )
                remove( item, layersFilter, false );
        }
    }

    if( a.dAll || a.dFootprints )
    {
        for( FOOTPRINT* fp : board->Footprints() )
        {
            if( a.dAll )
                remove( fp, allLayers, true );
            else if( lockPasses( fp ) )
                remove( fp, layersFilter, true );
        }
    }

    if( a.dAll || a.dTracks || a.dVias )
    {
        for( PCB_TRACK* track : board->Tracks() )
        {
            if( a.dAll )
                remove( track, allLayers, true );
            else if( ( track->Type() == PCB_VIA_T ? a.dVias : a.dTracks ) && lockPasses( track ) )
                remove( track, layersFilter, true );
        }
    }

    if( a.dMarkers || a.dAll )
    {
        for( PCB_MARKER* marker : board->Markers() )
            aRows.Add( marker, "marker", wxS( "Mark deleted" ) );
    }
}

void ccCleanupRows( const std::vector<std::shared_ptr<CLEANUP_ITEM>>& aItems, BOARD* aBoard,
                    CC_PCB_ROWS& aRows )
{
    for( const std::shared_ptr<CLEANUP_ITEM>& item : aItems )
    {
        if( !item )
            continue;

        BOARD_ITEM* main = aBoard->ResolveItem( item->GetMainItemID(), true );
        aRows.Add( main, main ? ccPcbKind( main ) : "item", item->GetErrorMessage( false ) );
    }
}

/** unusedPads: DIALOG_UNUSED_PAD_LAYERS::updatePadsAndVias over the board or the uuids named. */
void ccOpUnusedPads( PCB_EDIT_FRAME* aFrame, BOARD_COMMIT& aCommit, const CC_BOARD_EDIT& a,
                     CC_PCB_ROWS& aRows )
{
    BOARD* board = aFrame->GetBoard();

    auto viaHasUnused =
            [&]( PCB_VIA* via )
            {
                if( via->GetViaType() == VIATYPE::THROUGH )
                    return board->GetCopperLayerCount() > 2;

                PCB_LAYER_ID start = via->Padstack().StartLayer();
                PCB_LAYER_ID end = via->Padstack().EndLayer();

                if( start < 0 || end < 0 )
                    return board->GetCopperLayerCount() > 2;

                return board->LayerDepth( start, end ) > 1;
            };

    auto picked = [&]( const BOARD_ITEM* aItem ) -> bool
                  {
                      return a.net.uuids.empty() || ccRulesPicked( aItem, a.net.uuids );
                  };

    if( a.upPads )
    {
        for( FOOTPRINT* fp : board->Footprints() )
        {
            for( PAD* pad : fp->Pads() )
            {
                if( pad->GetAttribute() != PAD_ATTRIB::PTH || !picked( pad ) )
                    continue;

                const bool keep = a.upRemove ? a.keepOuter : pad->GetKeepTopBottom();

                if( pad->GetRemoveUnconnected() == a.upRemove && pad->GetKeepTopBottom() == keep )
                    continue;

                if( !a.preview )
                {
                    aCommit.Modify( pad );
                    pad->SetRemoveUnconnected( a.upRemove );

                    if( a.upRemove )
                        pad->SetKeepTopBottom( a.keepOuter );
                }

                aRows.Add( pad, "pad", wxString::Format( wxS( "Pad %s of %s" ), pad->GetNumber(),
                                                         fp->GetReference() ) );
            }
        }
    }

    if( a.upVias )
    {
        for( PCB_TRACK* track : board->Tracks() )
        {
            if( track->Type() != PCB_VIA_T || !picked( track ) )
                continue;

            PCB_VIA* via = static_cast<PCB_VIA*>( track );

            if( !viaHasUnused( via ) )
                continue;

            const bool keep = a.upRemove ? a.keepOuter : via->GetKeepStartEnd();

            if( via->GetRemoveUnconnected() == a.upRemove && via->GetKeepStartEnd() == keep )
                continue;

            if( !a.preview )
            {
                aCommit.Modify( via );
                via->SetRemoveUnconnected( a.upRemove );

                if( a.upRemove )
                    via->SetKeepStartEnd( a.keepOuter );
            }

            aRows.Add( via, "via", wxString::Format( wxS( "Via of %s" ), UnescapeString( via->GetNetname() ) ) );
        }
    }
}

/** repair: BOARD_EDITOR_CONTROL::RepairBoard (duplicate ids, orphaned nets) with no message box. */
void ccOpRepair( PCB_EDIT_FRAME* aFrame, bool aPreview, CC_PCB_ROWS& aRows )
{
    BOARD*         board = aFrame->GetBoard();
    std::set<KIID> ids;
    int            errors = 0;

    auto processItem =
            [&]( EDA_ITEM* aItem )
            {
                if( ids.count( aItem->m_Uuid ) )
                {
                    aRows.Add( dynamic_cast<BOARD_ITEM*>( aItem ), "item", wxS( "Duplicate id replaced" ) );
                    errors++;

                    if( !aPreview )
                        const_cast<KIID&>( aItem->m_Uuid ) = KIID();
                }

                ids.insert( aItem->m_Uuid );

                BOARD_CONNECTED_ITEM* cItem = dynamic_cast<BOARD_CONNECTED_ITEM*>( aItem );

                if( cItem && cItem->GetNetCode() )
                {
                    NETINFO_ITEM* netinfo = cItem->GetNet();

                    if( netinfo && !board->FindNet( netinfo->GetNetname() ) )
                    {
                        if( !aPreview )
                            board->Add( netinfo );

                        aRows.Add( cItem, ccPcbKind( cItem ),
                                   wxString::Format( wxS( "Orphaned net %s re-parented" ),
                                                     UnescapeString( netinfo->GetNetname() ) ) );
                        errors++;
                    }
                }
            };

    for( FOOTPRINT* fp : board->Footprints() )
        processItem( fp );

    for( FOOTPRINT* fp : board->Footprints() )
    {
        for( PAD* pad : fp->Pads() )
            processItem( pad );
    }

    for( PCB_TRACK* track : board->Tracks() )
        processItem( track );

    for( FOOTPRINT* fp : board->Footprints() )
    {
        processItem( &fp->Reference() );
        processItem( &fp->Value() );

        for( BOARD_ITEM* item : fp->GraphicalItems() )
            processItem( item );

        for( ZONE* zone : fp->Zones() )
            processItem( zone );

        for( PCB_GROUP* group : fp->Groups() )
            processItem( group );
    }

    for( BOARD_ITEM* item : board->GetItemSet() )
    {
        switch( item->Type() )
        {
        case PCB_FOOTPRINT_T:
        case PCB_TRACE_T:
        case PCB_ARC_T:
        case PCB_VIA_T:
            break;

        default:
            processItem( item );
            break;
        }
    }

    if( errors && !aPreview )
        aFrame->OnModify();
}

// ── reannotate: DIALOG_BOARD_REANNOTATE's BuildFootprintList, BuildChangeArray and apply ──

struct CC_REFDES_INFO
{
    FOOTPRINT* fp = nullptr;
    bool       front = true;
    wxString   ref;
    wxString   prefix;
    int        x = 0, y = 0, rx = 0, ry = 0;
    int        action = 0;   // 0 update, 1 empty, 2 invalid, 3 excluded
};

struct CC_PREFIX_INFO
{
    wxString               prefix;
    unsigned int           lastUsed = 0;
    std::set<unsigned int> unavailable;
};

int ccRoundToGrid( int aCoord, int aGrid )
{
    if( aGrid == 0 )
        aGrid = 1000;

    int rounder = aCoord % aGrid;
    aCoord -= rounder;

    if( std::abs( rounder ) > aGrid / 2 )
        aCoord += ( aCoord < 0 ? -aGrid : aGrid );

    return aCoord;
}

// The dialog's sort codes (SORTYFIRST 0b100, DESCENDINGFIRST 0b010, DESCENDINGSECOND 0b001),
// front and mirrored for the back.
const int CC_FRONT_DIRECTIONS[8] = { 0b100, 0b101, 0b110, 0b111, 0b000, 0b001, 0b010, 0b011 };
const int CC_BACK_DIRECTIONS[8] = { 0b101, 0b100, 0b111, 0b110, 0b010, 0b011, 0b000, 0b001 };

void ccSortFootprints( std::vector<CC_REFDES_INFO>& aList, int aCode )
{
    const bool yFirst = ( aCode & 0b100 ) != 0;
    const bool descFirst = ( aCode & 0b010 ) != 0;
    const bool descSecond = ( aCode & 0b001 ) != 0;

    std::stable_sort( aList.begin(), aList.end(),
                      [&]( const CC_REFDES_INFO& aA, const CC_REFDES_INFO& aB )
                      {
                          int x0 = aA.rx, x1 = aB.rx, y0 = aA.ry, y1 = aB.ry;

                          if( yFirst )
                          {
                              std::swap( x0, y0 );
                              std::swap( x1, y1 );
                          }

                          if( descFirst )
                              std::swap( x0, x1 );

                          if( descSecond )
                              std::swap( y0, y1 );

                          if( x0 != x1 )
                              return x0 < x1;

                          return y0 < y1;
                      } );
}

void ccOpReannotate( PCB_EDIT_FRAME* aFrame, BOARD_COMMIT& aCommit, const CC_BOARD_EDIT& a,
                     CC_PCB_ROWS& aRows, std::vector<std::pair<SEVERITY, wxString>>& aLines, bool& aBlocked )
{
    BOARD*                      board = aFrame->GetBoard();
    std::vector<CC_REFDES_INFO> front, back;
    std::vector<CC_PREFIX_INFO> prefixes;

    for( FOOTPRINT* fp : board->Footprints() )
    {
        CC_REFDES_INFO info;
        info.fp = fp;
        info.ref = fp->GetReference();
        info.x = a.byReference ? fp->Reference().GetPosition().x : fp->GetPosition().x;
        info.y = a.byReference ? fp->Reference().GetPosition().y : fp->GetPosition().y;
        info.rx = ccRoundToGrid( info.x, a.roundIu );
        info.ry = ccRoundToGrid( info.y, a.roundIu );
        info.front = fp->GetLayer() == F_Cu;

        size_t firstnum = 0;

        if( info.ref.IsEmpty() )
        {
            info.action = 1;
        }
        else
        {
            firstnum = info.ref.find_first_of( wxT( "0123456789" ) );

            if( firstnum == std::string::npos )
                info.action = 2;
        }

        info.prefix = info.ref.substr( 0, firstnum );

        for( const wxString& excluded : a.exclude )
        {
            if( excluded.EndsWith( '*' ) ? info.ref.Matches( excluded ) : excluded == info.prefix )
            {
                info.action = 3;
                break;
            }
        }

        if( fp->IsLocked() && a.skipLocked )
            info.action = 3;
        else if( a.scope == "selected"
                 && !( a.net.uuids.empty() ? fp->IsSelected() : a.net.uuids.count( fp->m_Uuid ) > 0 ) )
            info.action = 3;
        else if( a.scope == "front" )
            info.action = info.front ? info.action : 3;
        else if( a.scope == "back" )
            info.action = info.front ? 3 : info.action;

        ( info.front ? front : back ).push_back( info );
    }

    ccSortFootprints( front, CC_FRONT_DIRECTIONS[a.order] );
    ccSortFootprints( back, CC_BACK_DIRECTIONS[a.order] );

    auto prefixInfo = [&]( const wxString& aPrefix, int aStart ) -> CC_PREFIX_INFO&
                      {
                          for( CC_PREFIX_INFO& info : prefixes )
                          {
                              if( info.prefix == aPrefix )
                                  return info;
                          }

                          CC_PREFIX_INFO info;
                          info.prefix = aPrefix;
                          info.lastUsed = (unsigned int) std::max( aStart - 1, 0 );
                          prefixes.push_back( info );
                          return prefixes.back();
                      };

    // The excluded designators stay taken.
    for( const std::vector<CC_REFDES_INFO>* list : { &front, &back } )
    {
        for( const CC_REFDES_INFO& info : *list )
        {
            if( info.action == 3 )
                prefixInfo( info.prefix, 0 ).unavailable.insert( UTIL::GetRefDesNumber( info.ref ) );
        }
    }

    std::map<FOOTPRINT*, wxString> next;

    auto build =
            [&]( std::vector<CC_REFDES_INFO>& aList, int aStart, const wxString& aPrefix, bool aRemove )
            {
                const bool havePrefix = !aPrefix.IsEmpty();
                const bool addPrefix = havePrefix && !aRemove;
                const bool removePrefix = aRemove && havePrefix;

                if( aStart != 0 )
                {
                    for( CC_PREFIX_INFO& info : prefixes )
                        info.lastUsed = aStart - 1;
                }

                for( CC_REFDES_INFO info : aList )
                {
                    if( info.action == 1 || info.action == 2 )
                    {
                        aLines.emplace_back( RPT_SEVERITY_WARNING,
                                             wxString::Format( wxS( "%s is not numbered and keeps its designator" ),
                                                               info.ref.IsEmpty() ? wxString( wxS( "A footprint" ) )
                                                                                  : info.ref ) );
                        continue;
                    }

                    if( info.action != 0 )
                        continue;

                    const bool present = info.prefix.find( aPrefix ) == 0;

                    if( addPrefix && !present )
                        info.prefix.insert( 0, aPrefix );

                    if( removePrefix && present )
                        info.prefix.erase( 0, aPrefix.size() );

                    CC_PREFIX_INFO& pi = prefixInfo( info.prefix, aStart );
                    unsigned int    number = pi.lastUsed + 1;

                    while( pi.unavailable.count( number ) )
                        number++;

                    next[info.fp] = pi.prefix + std::to_string( number );
                    pi.lastUsed = number;
                }
            };

    build( front, a.frontStart, a.frontPrefix, a.removeFrontPrefix );
    build( back, a.backStart, a.backPrefix, a.removeBackPrefix );

    // Two parts ending with one designator stops the run (the dialog's duplicate check).
    std::map<wxString, int> uses;

    for( FOOTPRINT* fp : board->Footprints() )
    {
        auto it = next.find( fp );
        uses[it == next.end() ? fp->GetReference() : it->second]++;
    }

    for( const auto& [ref, count] : uses )
    {
        if( count > 1 && !ref.IsEmpty() )
        {
            aLines.emplace_back( RPT_SEVERITY_ERROR, wxString::Format( wxS( "Duplicate instances of %s" ), ref ) );
            aBlocked = true;
        }
    }

    // Rows in the old designator's natural order (the dialog's change log order).
    std::vector<std::pair<FOOTPRINT*, wxString>> changes( next.begin(), next.end() );

    std::sort( changes.begin(), changes.end(),
               []( const auto& aA, const auto& aB )
               {
                   return StrNumCmp( aA.first->GetReference(), aB.first->GetReference() ) < 0;
               } );

    for( const auto& [fp, ref] : changes )
    {
        const wxString oldRef = fp->GetReference();

        if( oldRef == ref )
            continue;

        if( !a.preview && !aBlocked )
        {
            aCommit.Modify( fp );
            fp->SetReference( ref );
            aFrame->GetCanvas()->GetView()->Update( fp );
        }

        aRows.Add( fp, "footprint", oldRef + wxS( " is now " ) + ref, toUtf8( oldRef ), toUtf8( ref ),
                   toUtf8( fp->GetPath().AsString() ) );
    }
}

/** zoneOrder: the zone manager's priorities, the first uuid the highest. */
void ccOpZoneOrder( PCB_EDIT_FRAME* aFrame, BOARD_COMMIT& aCommit, const CC_BOARD_EDIT& a,
                    CC_PCB_ROWS& aRows )
{
    BOARD*         board = aFrame->GetBoard();
    const unsigned n = (unsigned) a.zoneOrder.size();

    for( unsigned i = 0; i < n; ++i )
    {
        ZONE* zone = dynamic_cast<ZONE*>( board->ResolveItem( a.zoneOrder[i], true ) );

        if( !zone )
            continue;

        const unsigned priority = n - 1 - i;

        if( zone->GetAssignedPriority() == priority )
            continue;

        if( !a.preview )
        {
            aCommit.Modify( zone );
            zone->SetAssignedPriority( priority );
        }

        aRows.Add( zone, "zone", zone->GetZoneName().IsEmpty() ? wxString( wxS( "Copper area" ) ) : zone->GetZoneName(),
                   std::to_string( zone->GetAssignedPriority() ), std::to_string( priority ) );
    }
}

void ccRunBoardEdit( const std::string& aDir, const CC_BOARD_EDIT& a )
{
    std::string                                error;
    std::string                                message;
    CC_PCB_ROWS                                rows;
    std::vector<std::pair<SEVERITY, wxString>> lines;
    int                                        depth = -1;

    ccEnsureDir( aDir );

    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() )
    {
        error = "island_error";
        message = "no board editor";
    }
    else
    {
        wxLogNull noLog;

        try
        {
            BOARD*       board = fr->GetBoard();
            BOARD_COMMIT commit( fr );
            bool         ratsnest = false;
            bool         blocked = false;
            bool         pushed = false;
            const char*  name = "Edit";

            if( a.op == "tracksVias" )
            {
                ccOpTracksVias( fr, commit, a, rows );
                name = "Edit Track and Via Properties";
            }
            else if( a.op == "textGraphics" )
            {
                ccOpTextGraphics( fr, commit, a, rows );
                name = "Edit Text and Graphics";
            }
            else if( a.op == "teardrops" )
            {
                ccOpTeardrops( fr, commit, a, rows );
                name = "Edit Teardrops";
            }
            else if( a.op == "swapLayers" )
            {
                ccOpSwapLayers( fr, commit, a, rows );
                name = "Swap Layers";
            }
            else if( a.op == "deleteGlobal" )
            {
                ccOpDeleteGlobal( fr, commit, a, rows, ratsnest );
                name = "Global Delete";
            }
            else if( a.op == "cleanupTracks" )
            {
                std::vector<std::shared_ptr<CLEANUP_ITEM>> items;
                TRACKS_CLEANER                             cleaner( board, commit );
                CC_NET_FILTER                              filter = a.net;

                cleaner.SetFilter(
                        [filter]( BOARD_CONNECTED_ITEM* aItem ) -> bool
                        {
                            return !filter.Passes( aItem );   // true: leave the item alone
                        } );

                if( !a.preview )
                    fr->GetToolManager()->RunAction( ACTIONS::selectionClear );

                fr->Compile_Ratsnest( false );
                cleaner.CleanupBoard( a.preview, &items, a.removeShorts, a.redundantVias, a.mergeCollinear,
                                      a.removeDangling, a.tracksInPads, a.danglingVias, nullptr );
                ccCleanupRows( items, board, rows );
                name = "Board Cleanup";
            }
            else if( a.op == "unusedPads" )
            {
                ccOpUnusedPads( fr, commit, a, rows );
                name = "Remove Unused Pads";
            }
            else if( a.op == "cleanupGraphics" )
            {
                std::vector<std::shared_ptr<CLEANUP_ITEM>> items;
                GRAPHICS_CLEANER cleaner( board->Drawings(), nullptr, commit, fr->GetToolManager() );

                if( !a.preview )
                    fr->GetToolManager()->RunAction( ACTIONS::selectionClear );

                fr->Compile_Ratsnest( false );
                cleaner.CleanupBoard( a.preview, &items, a.mergeRects, a.deleteRedundant,
                                      /*aMergePads*/ false, a.fixOutline, a.tolerance );
                ccCleanupRows( items, board, rows );
                name = "Cleanup Graphics";
            }
            else if( a.op == "repair" )
            {
                ccOpRepair( fr, a.preview, rows );
                name = "";
            }
            else if( a.op == "reannotate" )
            {
                ccOpReannotate( fr, commit, a, rows, lines, blocked );
                name = "Annotation";

                if( blocked )
                {
                    error = "duplicate_designator";
                    message = "two parts would share a designator";
                }
            }
            else if( a.op == "zoneOrder" )
            {
                ccOpZoneOrder( fr, commit, a, rows );
                name = "Copper area priorities";
            }

            if( !a.preview && !blocked && !commit.Empty() )
            {
                commit.Push( wxString::FromUTF8( name ) );
                pushed = true;
            }
            else if( !commit.Empty() )
            {
                commit.Revert();
            }

            if( !a.preview && ( a.op == "deleteGlobal" ) && ( a.dMarkers || a.dAll ) )
                board->DeleteMARKERs();

            if( pushed && ratsnest )
                fr->Compile_Ratsnest( false );

            if( pushed && a.op == "zoneOrder" )
            {
                fr->OnModify();
                board->BuildConnectivity();
                fr->GetCanvas()->RedrawRatsnest();
            }

            // KiCad's own option: the fill is its own undo step, as the dialog's.
            if( !a.preview && a.op == "cleanupTracks" && a.refillZones )
            {
                if( ZONE_FILLER_TOOL* filler = fr->GetToolManager()->GetTool<ZONE_FILLER_TOOL>() )
                {
                    if( !filler->IsBusy() )
                    {
                        filler->FillAllZones( nullptr, nullptr, /*aHeadless*/ true );
                        fr->m_ZoneFillsDirty = false;
                    }
                }
            }

            fr->GetCanvas()->Refresh();
            depth = fr->GetUndoCommandCount();
        }
        catch( const std::exception& e )
        {
            error = "island_error";
            message = e.what();
        }
        catch( ... )
        {
            error = "island_error";
            message = "the edit did not run";
        }
    }

    json report = json::object();
    report["op"] = a.op;
    report["preview"] = a.preview;
    report["rows"] = std::move( rows.rows );
    report["count"] = rows.count;
    report["truncated"] = rows.truncated;
    report["depth"] = depth;

    json lineList = json::array();

    for( const auto& [severity, text] : lines )
    {
        json line = json::object();
        line["severity"] = severity == RPT_SEVERITY_ERROR     ? "error"
                           : severity == RPT_SEVERITY_WARNING ? "warning"
                                                              : "info";
        line["text"] = ccText( text );
        lineList.push_back( std::move( line ) );
    }

    if( !lineList.empty() )
        report["lines"] = std::move( lineList );

    ccWriteJson( aDir, "edit.json", report );

    json done = json::object();
    done["error"] = error;
    done["message"] = message;
    ccWriteJson( aDir, ".cc-edit.json", done );
}

// ───────────────────────────── footprints.exchange ─────────────────────────────

struct CC_FP_EXCHANGE
{
    std::string    dir;
    bool           change = false;
    std::string    matchKind;        // all, uuids, ref, value, libId
    std::set<KIID> uuids;
    wxString       glob;
    LIB_ID         matchId;
    LIB_ID         newId;
    bool           removeExtraTexts = false, resetTextLayers = false, resetTextEffects = false;
    bool           resetTextPositions = false, resetTextContent = false, resetFabrication = false;
    bool           resetClearances = false, reset3D = false;
    bool           preview = true;
};

bool ccFpMatches( const CC_FP_EXCHANGE& a, FOOTPRINT* aFootprint )
{
    if( a.matchKind == "all" )
        return true;

    if( a.matchKind == "uuids" )
        return a.uuids.count( aFootprint->m_Uuid ) > 0;

    if( a.matchKind == "ref" )
        return WildCompareString( a.glob, aFootprint->GetReference(), false );

    if( a.matchKind == "value" )
        return WildCompareString( a.glob, aFootprint->GetValue(), false );

    return aFootprint->GetFPID() == a.matchId;
}

void ccRunFootprintsExchange( const CC_FP_EXCHANGE& a )
{
    std::string error;
    std::string message;
    json        rows = json::array();
    int         applied = 0;
    bool        truncated = false;
    int         depth = -1;

    ccEnsureDir( a.dir );

    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() )
    {
        error = "island_error";
        message = "no board editor";
    }
    else
    {
        wxLogNull noLog;

        try
        {
            BOARD_COMMIT            commit( fr );
            std::vector<FOOTPRINT*> targets;

            for( FOOTPRINT* fp : fr->GetBoard()->Footprints() )
            {
                if( ccFpMatches( a, fp ) )
                    targets.push_back( fp );
            }

            // From the last: ExchangeFootprint appends the replacement at the end of the list.
            for( auto it = targets.rbegin(); it != targets.rend(); ++it )
            {
                FOOTPRINT*  fp = *it;
                const LIB_ID newId = a.change ? a.newId : fp->GetFPID();
                std::string status = "changed";
                FOOTPRINT*  fresh = fr->LoadFootprint( newId );

                if( !fresh )
                {
                    status = "missing";
                }
                else if( !a.change && !fp->FootprintNeedsUpdate( fresh ) )
                {
                    status = "same";
                }
                else if( fp->IsLocked() )
                {
                    status = "locked";   // KiCad exchanges a locked footprint too
                }

                if( rows.size() < CC_PCB_ROWS_MAX )
                {
                    json row = json::object();
                    row["uuid"] = toUtf8( fp->m_Uuid.AsString() );
                    row["ref"] = ccText( fp->GetReference(), 64 );
                    row["from"] = ccText( wxString::FromUTF8( fp->GetFPID().Format().c_str() ) );
                    row["to"] = ccText( wxString::FromUTF8( newId.Format().c_str() ) );
                    row["status"] = status;
                    rows.push_back( std::move( row ) );
                }
                else
                {
                    truncated = true;
                }

                if( !fresh )
                    continue;

                if( a.preview )
                {
                    delete fresh;

                    if( status != "same" )
                        applied++;

                    continue;
                }

                bool updated = a.change || status != "same";
                fr->ExchangeFootprint( fp, fresh, commit, a.removeExtraTexts, a.resetTextLayers,
                                       a.resetTextEffects, a.resetTextPositions, a.resetTextContent,
                                       a.resetFabrication, a.resetClearances, a.reset3D, &updated );

                if( updated )
                    applied++;
            }

            if( !a.preview && !commit.Empty() )
            {
                commit.Push( a.change ? _( "Change Footprint" ) : _( "Update Footprint" ) );
                fr->GetCanvas()->Refresh();
            }
            else if( !commit.Empty() )
            {
                commit.Revert();
            }

            depth = fr->GetUndoCommandCount();
        }
        catch( const std::exception& e )
        {
            error = "island_error";
            message = e.what();
        }
        catch( ... )
        {
            error = "island_error";
            message = "the footprints were not exchanged";
        }
    }

    // The rows were gathered from the last footprint; the record lists them in board order.
    std::reverse( rows.begin(), rows.end() );

    json report = json::object();
    report["preview"] = a.preview;
    report["rows"] = std::move( rows );
    report["applied"] = applied;
    report["truncated"] = truncated;
    report["depth"] = depth;
    ccWriteJson( a.dir, "exchange.json", report );

    json done = json::object();
    done["error"] = error;
    done["message"] = message;
    ccWriteJson( a.dir, ".cc-exchange.json", done );
}

// ───────────────────────────── settings (board frame) ─────────────────────────────

struct CC_PCB_JSON_SECTION
{
    JSON_SETTINGS*                                   settings = nullptr;
    std::vector<std::pair<std::string, std::string>> keys;
};

bool ccPcbHasNull( const json& aValue )
{
    if( aValue.is_null() )
        return true;

    if( aValue.is_object() || aValue.is_array() )
    {
        for( const json& child : aValue )
        {
            if( ccPcbHasNull( child ) )
                return true;
        }
    }

    return false;
}

json ccPcbJsonGet( const CC_PCB_JSON_SECTION& aSection )
{
    aSection.settings->Store();
    const nlohmann::json& doc = *aSection.settings->Internals();
    json                  out = json::object();

    for( const auto& [wire, pointer] : aSection.keys )
    {
        const json::json_pointer ptr( pointer );

        if( doc.contains( ptr ) )
            out[wire] = doc.at( ptr );
    }

    return out;
}

bool ccPcbJsonSet( const CC_PCB_JSON_SECTION& aSection, const json& aPatch )
{
    if( !aPatch.is_object() || aPatch.empty() || ccPcbHasNull( aPatch ) )
        return false;

    aSection.settings->Store();
    nlohmann::json& doc = *aSection.settings->Internals();
    nlohmann::json  next = doc;

    for( const auto& [key, value] : aPatch.items() )
    {
        auto it = std::find_if( aSection.keys.begin(), aSection.keys.end(),
                                [&]( const auto& aPair ) { return aPair.first == key; } );

        if( it == aSection.keys.end() )
            return false;

        const json::json_pointer ptr( it->second );

        if( next.contains( ptr ) )
        {
            json& target = next[ptr];

            if( !( target.is_number() && value.is_number() ) && target.type() != value.type() )
                return false;

            if( target.is_object() )
                target.merge_patch( value );
            else
                target = value;
        }
        else
        {
            next[ptr] = value;
        }
    }

    doc = next;
    aSection.settings->Load();
    return true;
}

std::optional<CC_PCB_JSON_SECTION> ccPcbJsonSection( PCB_EDIT_FRAME* aFrame, const std::string& aName )
{
    CC_PCB_JSON_SECTION    section;
    BOARD_DESIGN_SETTINGS& bds = aFrame->GetBoard()->GetDesignSettings();

    if( aName == "netClasses" )
    {
        std::shared_ptr<NET_SETTINGS>& net = aFrame->Prj().GetProjectFile().NetSettings();

        if( !net )
            return std::nullopt;

        section.settings = net.get();
        section.keys = { { "classes", "/classes" },
                         { "netclass_patterns", "/netclass_patterns" },
                         { "netclass_assignments", "/netclass_assignments" },
                         { "net_colors", "/net_colors" } };
    }
    else if( aName == "textDefaults" )
    {
        section.settings = &bds;
        section.keys = { { "defaults", "/defaults" } };
    }
    else if( aName == "constraints" )
    {
        section.settings = &bds;
        section.keys = { { "rules", "/rules" } };
    }
    else if( aName == "predefinedSizes" )
    {
        section.settings = &bds;
        section.keys = { { "track_widths", "/track_widths" },
                         { "via_dimensions", "/via_dimensions" },
                         { "diff_pair_dimensions", "/diff_pair_dimensions" } };
    }
    else if( aName == "teardrops" )
    {
        section.settings = &bds;
        section.keys = { { "teardrop_options", "/teardrop_options" },
                         { "teardrop_parameters", "/teardrop_parameters" } };
    }
    else if( aName == "checkSeverities" )
    {
        section.settings = &bds;
        section.keys = { { "rule_severities", "/rule_severities" } };
    }
    else
    {
        return std::nullopt;
    }

    return section;
}

const char* ccStackupKind( BOARD_STACKUP_ITEM_TYPE aType )
{
    switch( aType )
    {
    case BS_ITEM_TYPE_COPPER:      return "copper";
    case BS_ITEM_TYPE_DIELECTRIC:  return "dielectric";
    case BS_ITEM_TYPE_SOLDERPASTE: return "solderpaste";
    case BS_ITEM_TYPE_SOLDERMASK:  return "soldermask";
    case BS_ITEM_TYPE_SILKSCREEN:  return "silkscreen";
    default:                       return "undefined";
    }
}

json ccPcbPageJson( PCB_EDIT_FRAME* aFrame )
{
    BOARD*             board = aFrame->GetBoard();
    const PAGE_INFO&   page = board->GetPageSettings();
    const TITLE_BLOCK& title = board->GetTitleBlock();
    json               out = json::object();

    out["paper"] = toUtf8( page.GetTypeAsString() );
    out["width"] = std::round( page.GetWidthMM() * 1000.0 ) / 1000.0;
    out["height"] = std::round( page.GetHeightMM() * 1000.0 ) / 1000.0;
    out["portrait"] = page.IsPortrait();
    out["title"] = ccText( title.GetTitle() );
    out["date"] = ccText( title.GetDate() );
    out["revision"] = ccText( title.GetRevision() );
    out["company"] = ccText( title.GetCompany() );

    json comments = json::array();

    for( int i = 0; i < 9; ++i )
        comments.push_back( ccText( title.GetComment( i ) ) );

    out["comments"] = std::move( comments );
    out["drawingSheet"] = ccText( aFrame->Prj().GetProjectFile().m_BoardDrawingSheetFile );
    return out;
}

json ccPcbSettingsGet( PCB_EDIT_FRAME* aFrame, const std::string& aSection )
{
    BOARD*                 board = aFrame->GetBoard();
    BOARD_DESIGN_SETTINGS& bds = board->GetDesignSettings();

    if( std::optional<CC_PCB_JSON_SECTION> section = ccPcbJsonSection( aFrame, aSection ) )
        return ccPcbJsonGet( *section );

    if( aSection == "textVariables" )
    {
        json out = json::object();

        for( const auto& [name, text] : aFrame->Prj().GetTextVars() )
            out[toUtf8( name )] = ccText( text );

        return out;
    }

    if( aSection == "variants" )
    {
        json names = json::array();

        for( const wxString& name : board->GetVariantNames() )
        {
            json entry = json::object();
            entry["name"] = ccText( name, 128 );
            entry["description"] = ccText( board->GetVariantDescription( name ) );
            names.push_back( std::move( entry ) );
        }

        json out = json::object();
        out["names"] = std::move( names );
        out["current"] = ccText( board->GetCurrentVariant(), 128 );
        return out;
    }

    if( aSection == "page" )
        return ccPcbPageJson( aFrame );

    if( aSection == "layers" )
    {
        json layers = json::array();

        for( PCB_LAYER_ID layer : LSET::AllLayersMask().Seq() )
        {
            json entry = json::object();
            entry["name"] = ccLayer( layer );
            entry["userName"] = ccText( board->GetLayerName( layer ), 256 );
            entry["type"] = LAYER::ShowType( board->GetLayerType( layer ) );
            entry["enabled"] = board->IsLayerEnabled( layer );
            layers.push_back( std::move( entry ) );
        }

        json out = json::object();
        out["copperCount"] = board->GetCopperLayerCount();
        out["layers"] = std::move( layers );
        return out;
    }

    if( aSection == "stackup" )
    {
        BOARD_STACKUP& stackup = bds.GetStackupDescriptor();
        json           rows = json::array();
        int            index = 0;

        for( BOARD_STACKUP_ITEM* item : stackup.GetList() )
        {
            for( int sub = 0; sub < item->GetSublayersCount(); ++sub )
            {
                json row = json::object();
                row["index"] = index;
                row["sub"] = sub;
                row["kind"] = ccStackupKind( item->GetType() );
                row["layer"] = item->GetBrdLayerId() != UNDEFINED_LAYER ? ccLayer( item->GetBrdLayerId() )
                                                                        : std::string();
                row["name"] = ccText( item->GetLayerName(), 256 );
                row["enabled"] = item->IsEnabled();
                row["material"] = ccText( item->GetMaterial( sub ), 256 );
                row["thickness"] = ccMm( item->GetThickness( sub ) );
                row["epsilon"] = item->GetEpsilonR( sub );
                row["lossTangent"] = item->GetLossTangent( sub );
                row["color"] = ccText( item->GetColor( sub ), 64 );
                rows.push_back( std::move( row ) );
            }

            index++;
        }

        json out = json::object();
        out["rows"] = std::move( rows );
        out["boardThickness"] = ccMm( bds.GetBoardThickness() );
        return out;
    }

    if( aSection == "finish" )
    {
        BOARD_STACKUP& stackup = bds.GetStackupDescriptor();
        json           out = json::object();

        out["finish"] = ccText( stackup.m_FinishType, 128 );
        out["edgePlating"] = stackup.m_EdgePlating;
        out["castellated"] = board->GetPadWithCastellatedAttrCount() > 0;   // derived, read only
        out["edgeConnector"] = stackup.m_EdgeConnectorConstraints == BS_EDGE_CONNECTOR_BEVELLED ? "bevelled"
                               : stackup.m_EdgeConnectorConstraints == BS_EDGE_CONNECTOR_IN_USE ? "inUse"
                                                                                                : "none";
        out["impedanceControlled"] = stackup.m_HasDielectricConstrains;
        out["maskExpansion"] = ccMm( bds.m_SolderMaskExpansion );
        out["maskMinWidth"] = ccMm( bds.m_SolderMaskMinWidth );
        out["pasteClearance"] = ccMm( bds.m_SolderPasteMargin );
        out["pasteRatio"] = bds.m_SolderPasteMarginRatio;
        return out;
    }

    if( aSection == "customRules" )
    {
        json     out = json::object();
        wxString path = aFrame->GetDesignRulesPath();
        std::string text;

        if( !path.IsEmpty() && wxFileName::FileExists( path ) )
        {
            if( FILE* f = std::fopen( path.utf8_str(), "rb" ) )
            {
                char   buf[8192];
                size_t n;

                while( ( n = std::fread( buf, 1, sizeof( buf ), f ) ) > 0 && text.size() <= ( 256u << 10 ) )
                    text.append( buf, n );

                std::fclose( f );
            }
        }

        if( text.size() > ( 256u << 10 ) )
            text.resize( 256u << 10 );

        out["text"] = text;
        return out;
    }

    return json();
}

/** After a board settings change: the Board Setup dialog's OK (pcb_edit_frame.cpp). */
void ccPcbSettingsChanged( PCB_EDIT_FRAME* aFrame )
{
    BOARD* board = aFrame->GetBoard();

    board->SynchronizeTuningProfileProperties();
    board->SynchronizeNetsAndNetClasses( true );
    board->SynchronizeComponentClasses( std::unordered_set<wxString>() );

    aFrame->OnModify();
    aFrame->Kiway().CommonSettingsChanged( TEXTVARS_CHANGED );
    aFrame->Prj().IncrementTextVarsTicker();
    aFrame->Prj().IncrementNetclassesTicker();

    if( DRC_TOOL* drcTool = aFrame->GetToolManager()->GetTool<DRC_TOOL>() )
    {
        try
        {
            drcTool->GetDRCEngine()->InitEngine( aFrame->GetDesignRulesPath() );
        }
        catch( const PARSE_ERROR& )
        {
            // KiCad's own way: a rules file that does not compile is reported by the check.
        }
    }

    board->InitializeClearanceCache();
    aFrame->GetCanvas()->GetView()->UpdateAllItems( KIGFX::REPAINT );
    aFrame->GetCanvas()->Refresh();
    aFrame->UpdateUserInterface();
}

/** The rules text parsed as Board Setup's Check rules does; each error a {line, column, message}. */
json ccParseRules( PCB_EDIT_FRAME* aFrame, const wxString& aText )
{
    json errors = json::array();

    class RULES_REPORTER : public REPORTER
    {
    public:
        using REPORTER::Report;

        REPORTER& Report( const wxString& aMsg, SEVERITY aSeverity = RPT_SEVERITY_UNDEFINED ) override
        {
            REPORTER::Report( aMsg, aSeverity );

            if( aSeverity == RPT_SEVERITY_ERROR )
                m_errors.push_back( aMsg );

            return *this;
        }

        bool HasMessage() const override { return !m_errors.empty(); }

        std::vector<wxString> m_errors;
    };

    RULES_REPORTER reporter;

    try
    {
        std::vector<std::shared_ptr<DRC_RULE>> rules;

        std::function<bool( wxString* )> resolver =
                [&]( wxString* token ) -> bool
                {
                    return aFrame->GetBoard()->ResolveTextVar( token, 0 );
                };

        wxString text = aFrame->GetBoard()->ConvertCrossReferencesToKIIDs( aText );
        text = ExpandTextVars( text, &resolver );

        DRC_RULES_PARSER parser( text, wxS( "DRC rules" ) );
        parser.Parse( rules, &reporter );
    }
    catch( PARSE_ERROR& pe )
    {
        json e = json::object();
        e["line"] = pe.lineNumber;
        e["column"] = pe.byteIndex;
        e["message"] = ccText( pe.ParseProblem(), 512 );
        errors.push_back( std::move( e ) );
    }

    // The reporter's lines read "ERROR: <a href='L:C'>message</a>rest" (reportError).
    for( const wxString& line : reporter.m_errors )
    {
        json     e = json::object();
        long     lineNo = 0, column = 0;
        wxString msg = line;
        int      href = line.Find( wxS( "href='" ) );

        if( href != wxNOT_FOUND )
        {
            wxString rest = line.Mid( href + 6 );
            rest.BeforeFirst( ':' ).ToLong( &lineNo );
            rest.AfterFirst( ':' ).BeforeFirst( '\'' ).ToLong( &column );
            msg = rest.AfterFirst( '>' );
            msg.Replace( wxS( "</a>" ), wxEmptyString );
        }

        e["line"] = (int) lineNo;
        e["column"] = (int) column;
        e["message"] = ccText( msg, 512 );
        errors.push_back( std::move( e ) );
    }

    return errors;
}

/** The items a removed layer holds (Board Setup's warning list), counted. */
int ccItemsOnLayers( BOARD* aBoard, const LSEQ& aLayers )
{
    int count = 0;

    for( PCB_LAYER_ID layer : aLayers )
    {
        PCB_LAYER_COLLECTOR collector;
        collector.SetLayerId( layer );
        collector.Collect( aBoard, GENERAL_COLLECTOR::BoardLevelItems );

        for( int i = 0; i < collector.GetCount(); i++ )
        {
            BOARD_ITEM* item = collector[i];

            if( item->Type() != PCB_FOOTPRINT_T && !item->GetParentFootprint() )
                count++;
        }

        for( FOOTPRINT* fp : aBoard->Footprints() )
        {
            for( PAD* pad : fp->Pads() )
            {
                if( pad->HasExplicitDefinitionForLayer( layer ) )
                    count++;
            }
        }
    }

    return count;
}

const std::set<std::string>& ccPcbSections()
{
    static const std::set<std::string> sections = { "netClasses",  "textVariables", "variants",
                                                    "page",        "layers",        "stackup",
                                                    "finish",      "textDefaults",  "constraints",
                                                    "predefinedSizes", "teardrops", "checkSeverities",
                                                    "customRules" };
    return sections;
}

struct CC_PCB_SETTINGS_ARGS
{
    std::string dir;
    std::string section;
    json        value;
    bool        dryRun = false;
};

void ccRunPcbSettingsSet( const CC_PCB_SETTINGS_ARGS& a )
{
    std::string        error;
    std::string        message;
    json               errors = json::array();
    json               value;
    std::optional<int> wouldRemove;

    ccEnsureDir( a.dir );

    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() )
    {
        error = "island_error";
        message = "no board editor";
    }
    else
    {
        wxLogNull noLog;

        try
        {
            BOARD*                 board = fr->GetBoard();
            BOARD_DESIGN_SETTINGS& bds = board->GetDesignSettings();
            bool                   ok = true;

            if( std::optional<CC_PCB_JSON_SECTION> section = ccPcbJsonSection( fr, a.section ) )
            {
                if( !a.dryRun )
                {
                    ok = ccPcbJsonSet( *section, a.value );

                    if( ok )
                        ccPcbSettingsChanged( fr );
                }
            }
            else if( a.section == "textVariables" )
            {
                std::map<wxString, wxString> next = fr->Prj().GetTextVars();

                ok = a.value.is_object() && !a.value.empty();

                for( const auto& [name, text] : a.value.items() )
                {
                    if( !ok )
                        break;

                    const wxString key = wxString::FromUTF8( name.c_str() );

                    if( key.IsEmpty() || key.length() > 128 )
                        ok = false;
                    else if( text.is_null() )
                        next.erase( key );
                    else if( std::optional<wxString> t = ccRulesString( text, 0, 1024 ) )
                        next[key] = *t;
                    else
                        ok = false;
                }

                if( ok && !a.dryRun )
                {
                    fr->Prj().GetTextVars() = next;
                    ccPcbSettingsChanged( fr );
                }
            }
            else if( a.section == "variants" )
            {
                ok = false;   // the board takes variants from the schematic (Update the board)
            }
            else if( a.section == "page" )
            {
                PAGE_INFO   page = board->GetPageSettings();
                TITLE_BLOCK title = board->GetTitleBlock();
                bool        hasSheet = false;
                wxString    sheet;

                ok = a.value.is_object() && !a.value.empty();

                for( const auto& [key, v] : a.value.items() )
                {
                    if( !ok )
                        break;

                    if( key == "paper" )
                    {
                        std::optional<wxString> paper = ccRulesString( v, 1, 32 );
                        ok = paper && page.SetType( *paper, page.IsPortrait() );
                    }
                    else if( key == "portrait" )
                    {
                        ok = v.is_boolean();

                        if( ok )
                            page.SetPortrait( v.get<bool>() );
                    }
                    else if( key == "width" || key == "height" )
                    {
                        ok = v.is_number() && std::isfinite( v.get<double>() ) && v.get<double>() >= 25.4
                             && v.get<double>() <= 3048.0;
                    }
                    else if( key == "title" || key == "date" || key == "revision" || key == "company" )
                    {
                        std::optional<wxString> text = ccRulesString( v, 0, 1024 );
                        ok = text.has_value();

                        if( ok && key == "title" )
                            title.SetTitle( *text );
                        else if( ok && key == "date" )
                            title.SetDate( *text );
                        else if( ok && key == "revision" )
                            title.SetRevision( *text );
                        else if( ok )
                            title.SetCompany( *text );
                    }
                    else if( key == "comments" )
                    {
                        ok = v.is_array() && !v.empty() && v.size() <= 9;

                        for( size_t i = 0; ok && i < v.size(); ++i )
                        {
                            if( v[i].is_null() )
                                continue;

                            std::optional<wxString> text = ccRulesString( v[i], 0, 1024 );
                            ok = text.has_value();

                            if( ok )
                                title.SetComment( (int) i, *text );
                        }
                    }
                    else if( key == "drawingSheet" )
                    {
                        std::optional<wxString> text = ccRulesString( v, 0, 1024 );
                        ok = text.has_value();
                        hasSheet = ok;

                        if( ok )
                            sheet = *text;
                    }
                    else
                    {
                        ok = false;
                    }
                }

                // A user size after the paper (the order a reader's patch may not keep).
                if( ok && page.IsCustom() )
                {
                    if( a.value.contains( "width" ) )
                        page.SetWidthMM( a.value["width"].get<double>() );

                    if( a.value.contains( "height" ) )
                        page.SetHeightMM( a.value["height"].get<double>() );
                }

                if( ok && !a.dryRun )
                {
                    board->SetPageSettings( page );
                    board->SetTitleBlock( title );

                    if( hasSheet )
                    {
                        fr->Prj().GetProjectFile().m_BoardDrawingSheetFile = sheet;
                        fr->LoadDrawingSheet();
                    }

                    fr->OnModify();
                    fr->GetCanvas()->Refresh();
                }
            }
            else if( a.section == "layers" )
            {
                // {copperCount?, layers?: [{name, userName?, type?, enabled?}]}
                LSET                                  enabled = board->GetEnabledLayers();
                int                                   copper = board->GetCopperLayerCount();
                std::map<PCB_LAYER_ID, wxString>      names;
                std::map<PCB_LAYER_ID, LAYER_T>       types;

                ok = a.value.is_object() && !a.value.empty();

                for( const auto& [key, v] : a.value.items() )
                {
                    if( !ok )
                        break;

                    if( key == "copperCount" )
                    {
                        ok = v.is_number_integer() && v.get<long long>() >= 2 && v.get<long long>() <= 32
                             && v.get<long long>() % 2 == 0;

                        if( ok )
                            copper = (int) v.get<long long>();
                    }
                    else if( key == "layers" )
                    {
                        ok = v.is_array() && !v.empty() && v.size() <= PCB_LAYER_ID_COUNT;

                        for( size_t i = 0; ok && i < v.size(); ++i )
                        {
                            const json& entry = v[i];
                            ok = entry.is_object() && entry.contains( "name" ) && entry["name"].is_string();

                            if( !ok )
                                break;

                            PCB_LAYER_ID layer = UNDEFINED_LAYER;

                            for( PCB_LAYER_ID candidate : LSET::AllLayersMask().Seq() )
                            {
                                if( ccLayer( candidate ) == entry["name"].get<std::string>() )
                                    layer = candidate;
                            }

                            ok = layer != UNDEFINED_LAYER;

                            for( const auto& [k, x] : entry.items() )
                            {
                                if( !ok || k == "name" )
                                    continue;

                                if( k == "userName" )
                                {
                                    std::optional<wxString> name = ccRulesString( x, 1, 64 );
                                    wxString                bad = wxFileName::GetForbiddenChars( wxPATH_DOS ) + wxS( "%" );
                                    ok = name && name->Find( ' ' ) == wxNOT_FOUND
                                         && name->Find( '"' ) == wxNOT_FOUND
                                         && name->CmpNoCase( wxS( "signal" ) ) != 0;

                                    for( size_t c = 0; ok && c < bad.length(); ++c )
                                        ok = name->Find( bad[c] ) == wxNOT_FOUND;

                                    if( ok )
                                        names[layer] = *name;
                                }
                                else if( k == "type" )
                                {
                                    ok = x.is_string();

                                    if( ok )
                                    {
                                        LAYER_T t = LAYER::ParseType( x.get<std::string>().c_str() );
                                        ok = t != LAYER_T( -1 );

                                        if( ok )
                                            types[layer] = t;
                                    }
                                }
                                else if( k == "enabled" )
                                {
                                    ok = x.is_boolean() && !IsCopperLayer( layer );   // copper follows copperCount

                                    if( ok )
                                        enabled.set( layer, x.get<bool>() );
                                }
                                else
                                {
                                    ok = false;
                                }
                            }
                        }
                    }
                    else
                    {
                        ok = false;
                    }
                }

                if( ok )
                {
                    enabled &= ~LSET::AllCuMask();
                    enabled |= LSET::AllCuMask( copper );

                    const LSET removed = board->GetEnabledLayers() & ~enabled;
                    const int  count = ccItemsOnLayers( board, removed.Seq() );

                    wouldRemove = count;

                    if( !a.dryRun )
                    {
                        // Board Setup's way: the items on a removed layer are deleted and the
                        // undo list is cleared (KiCad: "cannot be undone").
                        if( removed.any() )
                        {
                            fr->GetToolManager()->RunAction( ACTIONS::selectionClear );

                            for( PCB_LAYER_ID layer : removed.Seq() )
                                board->RemoveAllItemsOnLayer( layer );

                            if( count > 0 )
                                fr->ClearUndoRedoList();
                        }

                        const LSET previous = board->GetEnabledLayers();

                        if( enabled != previous )
                        {
                            board->SetEnabledLayers( enabled );
                            board->SetVisibleLayers( board->GetVisibleLayers() | ( enabled ^ previous ) );

                            for( FOOTPRINT* fp : board->Footprints() )
                            {
                                for( PAD* pad : fp->Pads() )
                                {
                                    if( pad->HasHole() && pad->IsOnCopperLayer() )
                                        pad->SetLayerSet( pad->GetLayerSet() | LSET::InternalCuMask() );
                                }
                            }
                        }

                        board->SetCopperLayerCount( copper );

                        for( const auto& [layer, name] : names )
                            board->SetLayerName( layer, name );

                        for( const auto& [layer, type] : types )
                            board->SetLayerType( layer, type );

                        bds.GetStackupDescriptor().SynchronizeWithBoard( &bds );
                        ccPcbSettingsChanged( fr );
                    }
                }
            }
            else if( a.section == "stackup" )
            {
                // {rows?: [{index, sub?, material?, thickness?, epsilon?, lossTangent?, color?}]}
                BOARD_STACKUP& stackup = bds.GetStackupDescriptor();
                const auto&    list = stackup.GetList();

                ok = a.value.is_object() && a.value.size() == 1 && a.value.contains( "rows" )
                     && a.value["rows"].is_array() && !a.value["rows"].empty()
                     && a.value["rows"].size() <= 256;

                std::vector<std::function<void()>> edits;

                for( size_t i = 0; ok && i < a.value["rows"].size(); ++i )
                {
                    const json& row = a.value["rows"][i];
                    ok = row.is_object() && row.contains( "index" ) && row["index"].is_number_integer();

                    if( !ok )
                        break;

                    const long long index = row["index"].get<long long>();
                    ok = index >= 0 && index < (long long) list.size();

                    if( !ok )
                        break;

                    BOARD_STACKUP_ITEM* item = list[index];
                    int                 sub = 0;

                    if( row.contains( "sub" ) )
                    {
                        ok = row["sub"].is_number_integer() && row["sub"].get<long long>() >= 0
                             && row["sub"].get<long long>() < item->GetSublayersCount();

                        if( ok )
                            sub = (int) row["sub"].get<long long>();
                    }

                    for( const auto& [k, x] : row.items() )
                    {
                        if( !ok || k == "index" || k == "sub" )
                            continue;

                        if( k == "material" || k == "color" )
                        {
                            std::optional<wxString> text = ccRulesString( x, 0, 128 );
                            ok = text && ( k == "material" ? item->IsMaterialEditable() : item->IsColorEditable() );

                            if( ok && k == "material" )
                                edits.push_back( [item, sub, t = *text]() { item->SetMaterial( t, sub ); } );
                            else if( ok )
                                edits.push_back( [item, sub, t = *text]() { item->SetColor( t, sub ); } );
                        }
                        else if( k == "thickness" )
                        {
                            std::optional<int> iu = ccRulesMmToIu( x, 0.0, 100.0 );
                            ok = iu && item->IsThicknessEditable();

                            if( ok )
                                edits.push_back( [item, sub, t = *iu]() { item->SetThickness( t, sub ); } );
                        }
                        else if( k == "epsilon" || k == "lossTangent" )
                        {
                            ok = x.is_number() && std::isfinite( x.get<double>() ) && x.get<double>() >= 0.0
                                 && x.get<double>() <= 1000.0
                                 && ( k == "epsilon" ? item->HasEpsilonRValue() : item->HasLossTangentValue() );

                            if( ok && k == "epsilon" )
                                edits.push_back( [item, sub, d = x.get<double>()]() { item->SetEpsilonR( d, sub ); } );
                            else if( ok )
                                edits.push_back( [item, sub, d = x.get<double>()]() { item->SetLossTangent( d, sub ); } );
                        }
                        else
                        {
                            ok = false;
                        }
                    }
                }

                if( ok && !a.dryRun )
                {
                    for( const std::function<void()>& edit : edits )
                        edit();

                    bds.SetBoardThickness( stackup.BuildBoardThicknessFromStackup() );
                    fr->OnModify();
                    fr->GetCanvas()->Refresh();
                }
            }
            else if( a.section == "finish" )
            {
                BOARD_STACKUP& stackup = bds.GetStackupDescriptor();
                ok = a.value.is_object() && !a.value.empty();

                std::vector<std::function<void()>> edits;

                for( const auto& [key, v] : a.value.items() )
                {
                    if( !ok )
                        break;

                    if( key == "finish" )
                    {
                        std::optional<wxString> text = ccRulesString( v, 0, 128 );
                        ok = text.has_value();

                        if( ok )
                            edits.push_back( [&stackup, t = *text]() { stackup.m_FinishType = t; } );
                    }
                    else if( key == "edgePlating" || key == "impedanceControlled" )
                    {
                        ok = v.is_boolean();

                        if( ok && key == "edgePlating" )
                            edits.push_back( [&stackup, b = v.get<bool>()]() { stackup.m_EdgePlating = b; } );
                        else if( ok )
                            edits.push_back( [&stackup, b = v.get<bool>()]() { stackup.m_HasDielectricConstrains = b; } );
                    }
                    else if( key == "edgeConnector" )
                    {
                        const std::string c = v.is_string() ? v.get<std::string>() : "";
                        BS_EDGE_CONNECTOR_CONSTRAINTS e = c == "bevelled" ? BS_EDGE_CONNECTOR_BEVELLED
                                                          : c == "inUse"  ? BS_EDGE_CONNECTOR_IN_USE
                                                                          : BS_EDGE_CONNECTOR_NONE;
                        ok = c == "bevelled" || c == "inUse" || c == "none";

                        if( ok )
                            edits.push_back( [&stackup, e]() { stackup.m_EdgeConnectorConstraints = e; } );
                    }
                    else if( key == "maskExpansion" || key == "maskMinWidth" || key == "pasteClearance" )
                    {
                        std::optional<int> iu = ccRulesMmToIu( v, key == "maskMinWidth" ? 0.0 : -10.0, 10.0 );
                        ok = iu.has_value();

                        if( ok && key == "maskExpansion" )
                            edits.push_back( [&bds, i = *iu]() { bds.m_SolderMaskExpansion = i; } );
                        else if( ok && key == "maskMinWidth" )
                            edits.push_back( [&bds, i = *iu]() { bds.m_SolderMaskMinWidth = i; } );
                        else if( ok )
                            edits.push_back( [&bds, i = *iu]() { bds.m_SolderPasteMargin = i; } );
                    }
                    else if( key == "pasteRatio" )
                    {
                        ok = v.is_number() && std::isfinite( v.get<double>() ) && v.get<double>() >= -1.0
                             && v.get<double>() <= 1.0;

                        if( ok )
                            edits.push_back( [&bds, d = v.get<double>()]() { bds.m_SolderPasteMarginRatio = d; } );
                    }
                    else
                    {
                        ok = false;   // castellated is read only: KiCad derives it from the pads
                    }
                }

                if( ok && !a.dryRun )
                {
                    for( const std::function<void()>& edit : edits )
                        edit();

                    fr->OnModify();
                    fr->GetCanvas()->GetView()->UpdateAllItems( KIGFX::REPAINT );
                    fr->GetCanvas()->Refresh();
                }
            }
            else if( a.section == "customRules" )
            {
                ok = a.value.is_object() && a.value.size() == 1 && a.value.contains( "text" )
                     && a.value["text"].is_string() && a.value["text"].get_ref<const std::string&>().size()
                                                               <= ( 256u << 10 );

                if( ok )
                {
                    const std::string& raw = a.value["text"].get_ref<const std::string&>();
                    const wxString     text = wxString::FromUTF8( raw.c_str(), raw.size() );

                    ok = raw.empty() || !text.IsEmpty();

                    if( ok )
                        errors = ccParseRules( fr, text );

                    if( ok && !errors.empty() && !a.dryRun )
                    {
                        error = "rules_invalid";
                        message = errors[0]["message"].get<std::string>();
                    }
                    else if( ok && !a.dryRun )
                    {
                        const wxString path = fr->GetDesignRulesPath();

                        if( path.IsEmpty() || fr->Prj().IsNullProject() )
                        {
                            ok = false;
                        }
                        else if( FILE* f = std::fopen( path.utf8_str(), "wb" ) )
                        {
                            std::fwrite( raw.data(), 1, raw.size(), f );
                            std::fclose( f );
                            ccPcbSettingsChanged( fr );   // re-reads the rules (InitEngine)
                        }
                        else
                        {
                            error = "island_error";
                            message = "the rules file was not written";
                        }
                    }
                }
            }
            else
            {
                ok = false;
            }

            if( !ok && error.empty() )
            {
                error = "bad_args";
                message = "the section did not take this value";
            }

            value = ccPcbSettingsGet( fr, a.section );
        }
        catch( const std::exception& e )
        {
            error = "island_error";
            message = e.what();
        }
        catch( ... )
        {
            error = "island_error";
            message = "the setting was not applied";
        }
    }

    json record = json::object();
    record["value"] = value.is_null() ? json::object() : value;

    if( !errors.empty() )
        record["errors"] = errors;

    if( wouldRemove )
        record["wouldRemove"] = *wouldRemove;

    ccWriteJson( a.dir, "settings.json", record );

    json done = json::object();
    done["error"] = error;
    done["message"] = message;
    ccWriteJson( a.dir, ".cc-settings.json", done );
}
} // namespace

// ───────────────────────────── the exports ─────────────────────────────

// board.footprints (cap boardFootprints): every footprint (at most 8,192) as 7.2's
// FootprintRow under the board's current variant, {"footprints": [...], "variant": name,
// "truncated": bool}; "" with no board or an open in flight. A read: synchronous.
std::string pcbBoardFootprints()
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return "";

    try
    {
        BOARD*         board = fr->GetBoard();
        const wxString variant = board->GetCurrentVariant();
        json           rows = json::array();
        bool           truncated = false;

        for( FOOTPRINT* fp : board->Footprints() )
        {
            if( rows.size() >= 8192 )
            {
                truncated = true;
                break;
            }

            json row = json::object();
            row["uuid"] = toUtf8( fp->m_Uuid.AsString() );
            row["ref"] = ccText( fp->GetReference(), 64 );
            row["value"] = ccText( variant.IsEmpty() ? fp->GetValue()
                                                     : fp->GetFieldValueForVariant( variant, wxS( "Value" ) ) );
            row["libId"] = ccText( wxString::FromUTF8( fp->GetFPID().Format().c_str() ) );
            row["symbolPath"] = toUtf8( fp->GetPath().AsString() );
            row["side"] = fp->IsFlipped() ? "back" : "front";
            row["locked"] = fp->IsLocked();
            row["dnp"] = fp->GetDNPForVariant( variant );
            row["inBom"] = !fp->GetExcludedFromBOMForVariant( variant );
            row["inPos"] = !fp->GetExcludedFromPosFilesForVariant( variant );
            row["boardOnly"] = fp->IsBoardOnly();

            json fields = json::array();

            for( PCB_FIELD* field : fp->GetFields() )
            {
                if( !field || field->IsMandatory() || fields.size() >= 256 )
                    continue;

                json f = json::object();
                f["name"] = ccText( field->GetName(), 128 );
                f["value"] = ccText( variant.IsEmpty() ? field->GetText()
                                                       : fp->GetFieldValueForVariant( variant, field->GetName() ) );
                f["shown"] = field->IsVisible();
                fields.push_back( std::move( f ) );
            }

            row["fields"] = std::move( fields );
            rows.push_back( std::move( row ) );
        }

        json out = json::object();
        out["footprints"] = std::move( rows );
        out["variant"] = ccText( variant, 128 );
        out["truncated"] = truncated;
        return out.dump( -1, ' ', false, json::error_handler_t::replace );
    }
    catch( ... )
    {
        return "";
    }
}

// board.edit (cap boardEdit): aArgsJson exactly {"run", "dir": "/tmp/cc-edit/<n>", "op",
// "preview": bool, "filter": object, "set": object}, op one of tracksVias, textGraphics,
// teardrops, swapLayers, deleteGlobal, cleanupTracks, unusedPads, cleanupGraphics, repair,
// reannotate, zoneOrder (7.5; the keys KiCad's dialogs hold beyond 7.5 are listed in
// BUILD-NOTES-rules.md). A preview reads only; an apply is ONE BOARD_COMMIT named for the op.
// Writes <dir>/edit.json (7.5's EditReport, reannotate rows with symbolPath), then
// <dir>/.cc-edit.json last.
bool pcbBoardEdit( std::string aArgsJson )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return false;

    try
    {
        json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

        if( args.is_discarded() || !args.is_object() || args.size() != 6 )
            return false;

        for( const char* key : { "run", "dir", "op", "preview", "filter", "set" } )
        {
            if( !args.contains( key ) )
                return false;
        }

        if( !args["run"].is_number_integer() || args["run"].get<long long>() < 0 || !args["dir"].is_string()
                || !args["op"].is_string() || !args["preview"].is_boolean() )
        {
            return false;
        }

        const std::string d = args["dir"].get<std::string>();

        if( !ccIsRunDir( d, "/tmp/cc-edit/" ) )
            return false;

        CC_BOARD_EDIT a;
        a.op = args["op"].get<std::string>();
        a.preview = args["preview"].get<bool>();

        if( !ccReadBoardEditArgs( fr->GetBoard(), args["filter"], args["set"], a ) )
            return false;

        pcbjam_collab::runOnCoroutine( fr, [d, a]() { ccRunBoardEdit( d, a ); } );
        return true;
    }
    catch( ... )
    {
        return false;
    }
}

// footprints.exchange (cap footprintExchange): aArgsJson exactly {"run", "dir":
// "/tmp/cc-exchange/<n>", "mode": "update" | "change", "match": {"all": true} | {"uuids":
// [...]} | {"ref": glob} | {"value": glob} | {"libId": "lib:name"}, "libId": "" (update) or
// "lib:name" (change), "options": {removeExtraTexts, resetTextLayers, resetTextEffects,
// resetTextPositions, resetTextContent, resetFabrication, resetClearances, reset3D: each an
// optional bool}, "preview": bool}. The core is PCB_EDIT_FRAME::ExchangeFootprint as the
// dialog calls it. Writes <dir>/exchange.json (7.3), then <dir>/.cc-exchange.json last.
bool pcbFootprintsExchange( std::string aArgsJson )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return false;

    try
    {
        json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

        if( args.is_discarded() || !args.is_object() || args.size() != 7 )
            return false;

        for( const char* key : { "run", "dir", "mode", "match", "libId", "options", "preview" } )
        {
            if( !args.contains( key ) )
                return false;
        }

        CC_FP_EXCHANGE a;

        if( !args["run"].is_number_integer() || args["run"].get<long long>() < 0 || !args["dir"].is_string()
                || !args["mode"].is_string() || !args["preview"].is_boolean() || !args["match"].is_object()
                || args["match"].size() != 1 || !args["options"].is_object() || !args["libId"].is_string() )
        {
            return false;
        }

        a.dir = args["dir"].get<std::string>();
        a.preview = args["preview"].get<bool>();

        const std::string mode = args["mode"].get<std::string>();

        if( !ccIsRunDir( a.dir, "/tmp/cc-exchange/" ) || ( mode != "update" && mode != "change" ) )
            return false;

        a.change = mode == "change";

        auto parseId = []( const json& aValue, LIB_ID& aOut ) -> bool
                       {
                           std::optional<wxString> text = ccRulesString( aValue, 3, 512 );

                           return text && aOut.Parse( UTF8( *text ) ) == -1 && !aOut.GetLibNickname().empty()
                                  && !aOut.GetLibItemName().empty();
                       };

        if( a.change )
        {
            if( !parseId( args["libId"], a.newId ) )
                return false;
        }
        else if( !args["libId"].get<std::string>().empty() )
        {
            return false;
        }

        const auto        match = args["match"].begin();
        const std::string kind = match.key();

        if( kind == "all" )
        {
            if( !match.value().is_boolean() || !match.value().get<bool>() )
                return false;
        }
        else if( kind == "uuids" )
        {
            if( !ccRulesUuidSet( match.value(), a.uuids, 8192 ) )
                return false;
        }
        else if( kind == "ref" || kind == "value" )
        {
            std::optional<wxString> glob = ccRulesString( match.value(), 1, 256 );

            if( !glob )
                return false;

            a.glob = *glob;
        }
        else if( kind == "libId" )
        {
            if( !parseId( match.value(), a.matchId ) )
                return false;
        }
        else
        {
            return false;
        }

        a.matchKind = kind;

        for( const auto& [key, value] : args["options"].items() )
        {
            if( !value.is_boolean() )
                return false;

            const bool on = value.get<bool>();

            if( key == "removeExtraTexts" ) a.removeExtraTexts = on;
            else if( key == "resetTextLayers" ) a.resetTextLayers = on;
            else if( key == "resetTextEffects" ) a.resetTextEffects = on;
            else if( key == "resetTextPositions" ) a.resetTextPositions = on;
            else if( key == "resetTextContent" ) a.resetTextContent = on;
            else if( key == "resetFabrication" ) a.resetFabrication = on;
            else if( key == "resetClearances" ) a.resetClearances = on;
            else if( key == "reset3D" ) a.reset3D = on;
            else return false;
        }

        pcbjam_collab::runOnCoroutine( fr, [a]() { ccRunFootprintsExchange( a ); } );
        return true;
    }
    catch( ... )
    {
        return false;
    }
}

// settings.get (cap settings), the board frame: one section (7.4: netClasses, textVariables,
// variants (read only), page, layers, stackup, finish, textDefaults, constraints,
// predefinedSizes, teardrops, checkSeverities, customRules) as JSON; "" for any other.
std::string pcbSettingsGet( std::string aSection )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() || !ccPcbSections().count( aSection ) )
        return "";

    try
    {
        json out = ccPcbSettingsGet( fr, aSection );
        return out.is_null() ? "" : out.dump( -1, ' ', false, json::error_handler_t::replace );
    }
    catch( ... )
    {
        return "";
    }
}

// settings.set (cap settings), the board frame: {"run", "dir": "/tmp/cc-settings/<n>",
// "section", "value": object, "dryRun"?: bool}. Project-file sections are PATCHES merged into
// KiCad's JSON then Load(), with Board Setup's sync after; layers, stackup, finish, page and
// customRules by their own setters. A dry run changes nothing: customRules answers its parse
// errors, layers the items a removed layer holds (wouldRemove). No undo step (KiCad's rule);
// removing a layer that holds items clears the undo list, as Board Setup does. Writes
// <dir>/settings.json {value, errors?, wouldRemove?}, then <dir>/.cc-settings.json last.
bool pcbSettingsSet( std::string aArgsJson )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return false;

    try
    {
        json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

        if( args.is_discarded() || !args.is_object() || args.size() < 4 || args.size() > 5 )
            return false;

        auto run = args.find( "run" );
        auto dir = args.find( "dir" );
        auto section = args.find( "section" );
        auto value = args.find( "value" );
        auto dryRun = args.find( "dryRun" );

        if( run == args.end() || !run->is_number_integer() || run->get<long long>() < 0 || dir == args.end()
                || !dir->is_string() || section == args.end() || !section->is_string()
                || value == args.end() || !value->is_object() )
        {
            return false;
        }

        if( args.size() == 5 && ( dryRun == args.end() || !dryRun->is_boolean() ) )
            return false;

        CC_PCB_SETTINGS_ARGS a;
        a.dir = dir->get<std::string>();
        a.section = section->get<std::string>();
        a.value = *value;
        a.dryRun = dryRun != args.end() && dryRun->get<bool>();

        if( !ccIsRunDir( a.dir, "/tmp/cc-settings/" ) || !ccPcbSections().count( a.section )
                || a.value.dump().size() > ( 1u << 20 ) )
        {
            return false;
        }

        pcbjam_collab::runOnCoroutine( fr, [a]() { ccRunPcbSettingsSet( a ); } );
        return true;
    }
    catch( ... )
    {
        return false;
    }
}
