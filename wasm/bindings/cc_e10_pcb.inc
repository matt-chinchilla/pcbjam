// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e10, the board face: textually included by pcbnew_embind.cpp after cc_rules_pcb.inc,
// outside any KICAD_MERGED_EMBIND guard (include subdir pcbnew; the merged image compiles
// that TU with -DKICAD_MERGED_EMBIND). It sees the TU's includes and helpers (pcbFrame(),
// the cc_output namespace, cc_fp_items.h's declarations in the merged image only) and calls them without editing them.
//
// Every definition of the lane lives in namespace cc_e10 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

#include <cc_diff_finding.h>
#include <eda_search_data.h>
#include <netinfo.h>
#include <pcb_marker.h>
#include <pcb_text.h>
#include <string_utils.h>
#include <base_units.h>
#include <bitset>
#include <connectivity/connectivity_algo.h>
#include <connectivity/connectivity_data.h>
#include <length_delay_calculation/length_delay_calculation.h>
#include <length_delay_calculation/length_delay_calculation_item.h>
#include <netclass.h>
#include <cmath>
#include <limits>
#include <pcb_shape.h>
#include <zone.h>

namespace cc_e10
{
namespace pcb
{
/** At most this many findings in a CompareResult (the spec's 7.4). */
constexpr size_t kMaxFindings = 512;

/** A CC_DIFF_REPORTER's findings as 7.4's records, at most kMaxFindings. */
json findingsJson( const CC_DIFF_REPORTER& aReporter )
{
    json out = json::array();

    for( const CC_DIFF_FINDING& f : aReporter.Findings() )
    {
        if( out.size() >= kMaxFindings )
            break;

        out.push_back( { { "code", f.code },
                         { "uuid", f.hasUuid ? json( toUtf8( f.uuid.AsString() ) ) : json() },
                         { "pin", toUtf8( f.pin ) },
                         { "field", toUtf8( f.field ) } } );
    }

    return out;
}

json compareAnswer( const char* aStatus, const wxString& aSource, json aFindings )
{
    return { { "status", aStatus }, { "source", toUtf8( aSource ) },
             { "findings", std::move( aFindings ) } };
}

/** At most this many hits in a FindResult (the spec's 7.1). */
constexpr size_t kMaxHits = 4096;

/** EDA_ITEM's protected text matcher, named through a derived type (never instantiated). */
struct CcTextMatcher : public EDA_ITEM
{
    using EDA_ITEM::Matches;
};

/** KiCad's own Matches( aText, aData ) on aItem's behalf. */
bool textMatches( const EDA_ITEM* aItem, const wxString& aText, const EDA_SEARCH_DATA& aData )
{
    using MATCHER = bool ( EDA_ITEM::* )( const wxString&, const EDA_SEARCH_DATA& ) const;
    const MATCHER matcher = static_cast<MATCHER>( &CcTextMatcher::Matches );

    return ( aItem->*matcher )( aText, aData );
}

/**
 * Where in aText the search matched, by EDA_ITEM::Matches' own rules (case folded unless
 * matchCase; whole words; a regular expression's first match). A wildcard match, or text the
 * search did not match directly (a net name), lights the whole text.
 */
std::pair<int, int> matchSpan( const wxString& aText, const EDA_SEARCH_DATA& aData )
{
    wxString text = aText;
    wxString searchText = aData.findString;
    const std::pair<int, int> whole{ 0, static_cast<int>( aText.length() ) };

    if( !aData.matchCase )
    {
        text.MakeUpper();
        searchText.MakeUpper();
    }

    auto isWordChar =
            []( const wxUniChar& c )
            {
                return wxIsalnum( c ) || c == '_';
            };

    switch( aData.matchMode )
    {
    case EDA_SEARCH_MATCH_MODE::WHOLEWORD:
    {
        int ii = 0;

        while( ii < (int) text.length() )
        {
            int next = text.find( searchText, ii );

            if( next == wxNOT_FOUND )
                return whole;

            ii = next;
            next += searchText.length();

            if( ( ii == 0 || !isWordChar( text.GetChar( ii - 1 ) ) )
                    && ( next == (int) text.length() || !isWordChar( text.GetChar( next ) ) ) )
            {
                return { ii, static_cast<int>( searchText.length() ) };
            }

            ii++;
        }

        return whole;
    }

    case EDA_SEARCH_MATCH_MODE::REGEX:
    {
        wxLogNull noLogs;
        wxRegEx   re;
        size_t    start = 0;
        size_t    len = 0;

        if( re.Compile( searchText, aData.matchCase ? 0 : wxRE_ICASE ) && re.Matches( text )
                && re.GetMatch( &start, &len, 0 ) )
        {
            return { static_cast<int>( start ), static_cast<int>( len ) };
        }

        return whole;
    }

    case EDA_SEARCH_MATCH_MODE::PLAIN:
    {
        const int at = text.Find( searchText );

        if( at == wxNOT_FOUND )
            return whole;

        return { at, static_cast<int>( searchText.length() ) };
    }

    default:
        return whole;
    }
}

/**
 * 7.1's FindArgs common part into aData: {text (1 to 256), matchCase, wholeWord, wildcards,
 * regex} and exactly one face object named aFace (its keys checked by the caller). False for
 * any other shape. A regular expression that does not compile is refused.
 */
bool readFindArgs( const json& aArgs, const char* aFace, EDA_SEARCH_DATA& aData )
{
    if( !aArgs.is_object() || aArgs.size() != 6 )
        return false;

    for( const char* key : { "text", "matchCase", "wholeWord", "wildcards", "regex" } )
    {
        if( !aArgs.contains( key ) )
            return false;
    }

    if( !aArgs.contains( aFace ) || !aArgs[aFace].is_object() || !aArgs["text"].is_string() )
        return false;

    for( const char* key : { "matchCase", "wholeWord", "wildcards", "regex" } )
    {
        if( !aArgs[key].is_boolean() )
            return false;
    }

    const std::string& raw = aArgs["text"].get_ref<const std::string&>();

    if( raw.empty() || raw.find( '\0' ) != std::string::npos )
        return false;

    const wxString text = wxString::FromUTF8( raw.c_str(), raw.size() );

    if( text.IsEmpty() || text.length() > 256 )
        return false;

    aData.findString = text;
    aData.matchCase = aArgs["matchCase"].get<bool>();

    // The find dialogs' order: a regular expression, then whole words, then wildcards.
    if( aArgs["regex"].get<bool>() )
        aData.matchMode = EDA_SEARCH_MATCH_MODE::REGEX;
    else if( aArgs["wholeWord"].get<bool>() )
        aData.matchMode = EDA_SEARCH_MATCH_MODE::WHOLEWORD;
    else if( aArgs["wildcards"].get<bool>() )
        aData.matchMode = EDA_SEARCH_MATCH_MODE::WILDCARD;
    else
        aData.matchMode = EDA_SEARCH_MATCH_MODE::PLAIN;

    if( aData.matchMode == EDA_SEARCH_MATCH_MODE::REGEX )
    {
        wxLogNull noLogs;
        wxRegEx   probe;

        if( !probe.Compile( aData.findString, aData.matchCase ? 0 : wxRE_ICASE ) )
            return false;
    }

    return true;
}

/** The face object's booleans: exactly aKeys, each a boolean. */
bool readFaceFlags( const json& aFace, std::initializer_list<const char*> aKeys,
                    std::map<std::string, bool>& aOut )
{
    if( aFace.size() != aKeys.size() )
        return false;

    for( const char* key : aKeys )
    {
        if( !aFace.contains( key ) || !aFace[key].is_boolean() )
            return false;

        aOut[key] = aFace[key].get<bool>();
    }

    return true;
}

json hitJson( const std::string& aUuid, const wxString& aSheetPath, const char* aKind,
              const wxString& aRef, const wxString& aField, const wxString& aText,
              const EDA_SEARCH_DATA& aData, bool aDirect )
{
    const std::pair<int, int> span =
            aDirect ? matchSpan( aText, aData )
                    : std::pair<int, int>{ 0, static_cast<int>( aText.length() ) };

    return { { "uuid", aUuid },
             { "sheetPath", toUtf8( aSheetPath ) },
             { "kind", aKind },
             { "ref", toUtf8( aRef.Left( 1024 ) ) },
             { "field", toUtf8( aField.Left( 1024 ) ) },
             { "text", toUtf8( aText.Left( 1024 ) ) },
             { "start", span.first },
             { "length", span.second } };
}
} // namespace pcb

/**
 * kicadCompareWithSource, the board half (st-compare-e, K-DIFF): {uuid, sheetPath?} with
 * sheetPath absent or "" (a footprint has none). The footprint against its source exactly as
 * BOARD_INSPECTION_TOOL::DiffFootprint compares them: the source loaded through the project's
 * own footprint libraries, FOOTPRINT::FootprintNeedsUpdate with no flags (report everything).
 * Answers 7.4's CompareResult: status "same", "differs" (KiCad's return value) or "noSource"
 * (library absent, disabled, or the item gone), source the LIB_ID text, one finding per line
 * the comparison reported (the board stackup notes KiCad calls informational included, code
 * "boardStackup"). A read; "" for any other shape, no frame, an open in flight, or no such
 * footprint.
 */
std::string comparePcb( std::string aArgsJson )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return "";

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || !args.contains( "uuid" )
            || !args["uuid"].is_string() || !ccIsLowerUuid( args["uuid"].get<std::string>() ) )
    {
        return "";
    }

    for( auto it = args.begin(); it != args.end(); ++it )
    {
        if( it.key() == "sheetPath" )
        {
            if( !it->is_string() || !it->get<std::string>().empty() )
                return "";
        }
        else if( it.key() != "uuid" )
        {
            return "";
        }
    }

    BOARD_ITEM* item = ccResolve( fr->GetBoard(), args["uuid"].get<std::string>() );

    if( !item || item->Type() != PCB_FOOTPRINT_T )
        return "";

    FOOTPRINT*     footprint = static_cast<FOOTPRINT*>( item );
    const LIB_ID&  fpId = footprint->GetFPID();
    const wxString libName = fpId.GetLibNickname();
    const wxString fpName = fpId.GetLibItemName();
    const wxString source = fpId.Format().wx_str();

    FOOTPRINT_LIBRARY_ADAPTER* adapter =
            PROJECT_PCB::FootprintLibAdapter( fr->GetBoard()->GetProject() );

    if( !adapter || !adapter->HasLibrary( libName, /*aCheckEnabled*/ true ) )
        return pcb::compareAnswer( "noSource", source, json::array() ).dump();

    std::unique_ptr<FOOTPRINT> libFootprint;

    try
    {
        libFootprint.reset( adapter->LoadFootprint( libName, fpName, /*aKeepUUID*/ true ) );
    }
    catch( const IO_ERROR& )
    {
    }

    if( !libFootprint )
        return pcb::compareAnswer( "noSource", source, json::array() ).dump();

    CC_DIFF_REPORTER reporter;
    const bool       differs = footprint->FootprintNeedsUpdate( libFootprint.get(), 0, &reporter );

    return pcb::compareAnswer( differs ? "differs" : "same", source,
                               pcb::findingsJson( reporter ) )
            .dump();
}

/**
 * kicadFind, the board half (st-find-e-e, cap find): 7.1's FindArgs with the pcb object
 * {refs, values, texts, nets, markers}. DIALOG_FIND::search's own walk and KiCad's own
 * matching: footprints by designator, value, then their texts and other fields (one hit per
 * footprint, the first that matched), the board's texts and zone names, markers, nets.
 * Answers {hits, truncated} (at most 4,096 hits). A read; "" for any other shape, no frame or
 * an open in flight.
 */
std::string findPcb( std::string aArgsJson )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return "";

    json                        args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );
    EDA_SEARCH_DATA             data;
    std::map<std::string, bool> f;

    if( args.is_discarded() || !pcb::readFindArgs( args, "pcb", data )
            || !pcb::readFaceFlags( args["pcb"], { "refs", "values", "texts", "nets", "markers" },
                                    f ) )
    {
        return "";
    }

    BOARD* board = fr->GetBoard();
    json   hits = json::array();
    bool   truncated = false;

    auto add =
            [&]( const BOARD_ITEM* aItem, const char* aKind, const wxString& aRef,
                 const wxString& aField, const wxString& aText, bool aDirect )
            {
                if( hits.size() >= pcb::kMaxHits )
                {
                    truncated = true;
                    return;
                }

                hits.push_back( pcb::hitJson( toUtf8( aItem->m_Uuid.AsString() ), wxEmptyString,
                                              aKind, aRef, aField, aText, data, aDirect ) );
            };

    if( f["texts"] || f["values"] || f["refs"] )
    {
        for( FOOTPRINT* fp : board->Footprints() )
        {
            const wxString ref = fp->GetReference();

            if( f["refs"] && fp->Reference().Matches( data, nullptr ) )
            {
                add( fp, "footprint", ref, fp->Reference().GetName(),
                     UnescapeString( fp->Reference().GetText() ), true );
                continue;
            }

            if( f["values"] && fp->Value().Matches( data, nullptr ) )
            {
                add( fp, "footprint", ref, fp->Value().GetName(),
                     UnescapeString( fp->Value().GetText() ), true );
                continue;
            }

            if( !f["texts"] )
                continue;

            bool found = false;

            for( BOARD_ITEM* item : fp->GraphicalItems() )
            {
                if( item->Type() == PCB_TEXT_T && item->Matches( data, nullptr ) )
                {
                    add( fp, "text", ref, wxEmptyString,
                         UnescapeString( static_cast<PCB_TEXT*>( item )->GetText() ), true );
                    found = true;
                    break;
                }
            }

            if( found )
                continue;

            for( PCB_FIELD* field : fp->GetFields() )
            {
                if( field && field->Matches( data, nullptr ) )
                {
                    add( fp, "field", ref, field->GetName(),
                         UnescapeString( field->GetText() ), true );
                    break;
                }
            }
        }

        if( f["texts"] )
        {
            for( BOARD_ITEM* item : board->Drawings() )
            {
                if( item->Type() == PCB_TEXT_T && item->Matches( data, nullptr ) )
                {
                    add( item, "text", wxEmptyString, wxEmptyString,
                         UnescapeString( static_cast<PCB_TEXT*>( item )->GetText() ), true );
                }
            }

            for( ZONE* zone : board->Zones() )
            {
                if( zone->Matches( data, nullptr ) )
                    add( zone, "other", wxEmptyString, wxEmptyString, zone->GetZoneName(), true );
            }
        }
    }

    if( f["markers"] )
    {
        for( PCB_MARKER* marker : board->Markers() )
        {
            if( marker->Matches( data, nullptr ) )
            {
                add( marker, "marker", wxEmptyString, wxEmptyString,
                     marker->GetRCItem()->GetErrorMessage( true ), true );
            }
        }
    }

    if( f["nets"] )
    {
        for( NETINFO_ITEM* net : board->GetNetInfo() )
        {
            if( net && net->Matches( data, nullptr ) )
                add( net, "net", wxEmptyString, wxEmptyString, net->GetNetname(), true );
        }
    }

    return json( { { "hits", std::move( hits ) }, { "truncated", truncated } } ).dump();
}

/**
 * kicadNetReport (st-len-e-e, cap netReport): {nets?: string[] (1 to 8,192)}, that key only
 * or {}. Per net the net inspector's own figures, PCB_NET_INSPECTOR_PANEL::calculateNets'
 * lines: the connectivity items of its tracks, arcs, vias and pads, LENGTH_DELAY_CALCULATION
 * with the panel's optimisations (vias, merged tracks, traces in pads) and the layer and delay
 * detail. Answers {nets: NetLengthRow[]} (7.5: name as the board stores it, netClass, length
 * the routed track length and byLayer per copper layer, vias, viaLength, pads, dieLength, all
 * in mm; delay the total in picoseconds, null when the board gives no delay). Without nets,
 * every net but the unconnected one, in net code order; a named net the board lacks is left
 * out. A read; "" for any other shape, no frame or an open in flight.
 */
std::string netReport( std::string aArgsJson )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return "";

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() > 1 )
        return "";

    BOARD*                     board = fr->GetBoard();
    std::vector<NETINFO_ITEM*> nets;

    if( args.size() == 1 )
    {
        if( !args.contains( "nets" ) || !args["nets"].is_array() || args["nets"].empty()
                || args["nets"].size() > 8192 )
        {
            return "";
        }

        std::set<NETINFO_ITEM*> seen;

        for( const json& name : args["nets"] )
        {
            if( !name.is_string() || name.get_ref<const std::string&>().empty()
                    || name.get_ref<const std::string&>().size() > 1024 )
            {
                return "";
            }

            const wxString wanted = wxString::FromUTF8( name.get<std::string>().c_str() );

            for( NETINFO_ITEM* net : board->GetNetInfo() )
            {
                if( net && net->GetNetCode() > 0
                        && ( net->GetNetname() == wanted
                             || UnescapeString( net->GetNetname() ) == wanted )
                        && seen.insert( net ).second )
                {
                    nets.push_back( net );
                }
            }
        }
    }
    else
    {
        for( NETINFO_ITEM* net : board->GetNetInfo() )
        {
            if( net && net->GetNetCode() > 0 )
                nets.push_back( net );
        }
    }

    // relevantConnectivityItems: tracks, arcs, vias and pads, grouped by net code.
    const auto type_bits = std::bitset<MAX_STRUCT_TYPE_ID>().set( PCB_TRACE_T )
                                                            .set( PCB_ARC_T )
                                                            .set( PCB_VIA_T )
                                                            .set( PCB_PAD_T );

    LENGTH_DELAY_CALCULATION* calc = board->GetLengthCalculation();
    std::map<int, std::vector<LENGTH_DELAY_CALCULATION_ITEM>> byNet;
    std::set<int> wanted;

    for( NETINFO_ITEM* net : nets )
        wanted.insert( net->GetNetCode() );

    if( !calc || !board->GetConnectivity() )
        return "";

    for( CN_ITEM* cnItem : board->GetConnectivity()->GetConnectivityAlgo()->ItemList() )
    {
        if( cnItem->Valid() && type_bits[cnItem->Parent()->Type()] && wanted.count( cnItem->Net() ) )
            byNet[cnItem->Net()].emplace_back( calc->GetLengthCalculationItem( cnItem->Parent() ) );
    }

    std::sort( nets.begin(), nets.end(),
               []( const NETINFO_ITEM* a, const NETINFO_ITEM* b )
               {
                   return a->GetNetCode() < b->GetNetCode();
               } );

    constexpr PATH_OPTIMISATIONS opts = { .OptimiseVias = true,
                                          .MergeTracks = true,
                                          .OptimiseTracesInPads = true,
                                          .InferViaInPad = false };

    const double mmPerIu = 1.0 / pcbIUScale.IU_PER_MM;
    json         rows = json::array();

    for( NETINFO_ITEM* net : nets )
    {
        std::vector<LENGTH_DELAY_CALCULATION_ITEM>& items = byNet[net->GetNetCode()];

        LENGTH_DELAY_STATS stats = calc->CalculateLengthDetails(
                items, opts, nullptr, nullptr, LENGTH_DELAY_LAYER_OPT::WITH_LAYER_DETAIL,
                LENGTH_DELAY_DOMAIN_OPT::WITH_DELAY_DETAIL );

        json byLayer = json::array();

        if( stats.LayerLengths )
        {
            for( const auto& [layer, length] : *stats.LayerLengths )
            {
                if( length == 0 )
                    continue;

                byLayer.push_back( { { "layer", toUtf8( LSET::Name( layer ) ) },
                                     { "length", static_cast<double>( length ) * mmPerIu } } );
            }
        }

        const int64_t totalDelay = stats.TotalDelay();
        NETCLASS*     nc = net->GetNetClass();

        rows.push_back(
                { { "name", toUtf8( net->GetNetname() ) },
                  { "netClass", nc ? toUtf8( nc->GetHumanReadableName() ) : std::string() },
                  { "length", static_cast<double>( stats.TrackLength ) * mmPerIu },
                  { "byLayer", std::move( byLayer ) },
                  { "vias", stats.NumVias },
                  { "viaLength", static_cast<double>( stats.ViaLength ) * mmPerIu },
                  { "pads", stats.NumPads },
                  { "dieLength", static_cast<double>( stats.PadToDieLength ) * mmPerIu },
                  { "delay", totalDelay != 0
                                     ? json( static_cast<double>( totalDelay ) / pcbIUScale.IU_PER_PS )
                                     : json() } } );
    }

    return json( { { "nets", std::move( rows ) } } ).dump();
}

namespace pcb
{
/**
 * The frame the vertex exports serve: the board editor or the footprint editor, whichever is
 * the top window, else nullptr. KiCad's own vertex editor pane serves both (it is
 * PCB_BASE_EDIT_FRAME's, opened by EDIT_TOOL::EditVertices in either frame), so the exports
 * read and commit on the same base frame; BOARD_COMMIT takes the footprint editor's rules from
 * the frame type, as it does for the pane. The footprint editor is matched by its frame type,
 * never by its class, so the board TU names no footprint editor symbol (occ_service links it
 * without the footprint editor and simply never meets that frame).
 */
PCB_BASE_EDIT_FRAME* vertexFrame()
{
    if( PCB_EDIT_FRAME* board = pcbFrame() )
        return board;

    PCB_BASE_EDIT_FRAME* frame =
            wxTheApp ? dynamic_cast<PCB_BASE_EDIT_FRAME*>( wxTheApp->GetTopWindow() ) : nullptr;

    return frame && frame->IsType( FRAME_FOOTPRINT_EDITOR ) ? frame : nullptr;
}

/** The polygon the vertex editor pane edits: a zone's outline (any kind) or a polygon shape's. */
SHAPE_POLY_SET* vertexPoly( BOARD_ITEM* aItem, const char*& aKind )
{
    if( ZONE* zone = dynamic_cast<ZONE*>( aItem ) )
    {
        aKind = zone->GetIsRuleArea() ? "ruleArea"
                : zone->IsOnCopperLayer() ? "copper"
                                          : "nonCopper";
        return zone->Outline();
    }

    if( PCB_SHAPE* shape = dynamic_cast<PCB_SHAPE*>( aItem ) )
    {
        if( shape->GetShape() == SHAPE_T::POLY )
        {
            aKind = "polygon";
            return &shape->GetPolyShape();
        }
    }

    return nullptr;
}

/**
 * board.vertices.get's record: {uuid, kind, outlines: [{outline, hole, points: [{x, y}]}]},
 * every corner in the pane's own order (CIterateWithHoles), hole -1 for an outline itself,
 * board coordinates in mm.
 */
json verticesJson( BOARD_ITEM* aItem, const std::string& aUuid )
{
    const char*     kind = "";
    SHAPE_POLY_SET* poly = vertexPoly( aItem, kind );
    json            outlines = json::array();
    const double    mmPerIu = 1.0 / pcbIUScale.IU_PER_MM;
    int             lastPolygon = -1;
    int             lastContour = -1;

    for( auto it = poly->CIterateWithHoles(); it; ++it )
    {
        const SHAPE_POLY_SET::VERTEX_INDEX idx = it.GetIndex();

        if( idx.m_polygon != lastPolygon || idx.m_contour != lastContour )
        {
            outlines.push_back( { { "outline", idx.m_polygon },
                                  { "hole", idx.m_contour - 1 },
                                  { "points", json::array() } } );
            lastPolygon = idx.m_polygon;
            lastContour = idx.m_contour;
        }

        const VECTOR2I& pt = *it;
        outlines.back()["points"].push_back( { { "x", pt.x * mmPerIu }, { "y", pt.y * mmPerIu } } );
    }

    return { { "uuid", aUuid }, { "kind", kind }, { "outlines", std::move( outlines ) } };
}

/** The uuid argument: {"uuid": "<lowercase uuid>"} (that key only), or the bare uuid. */
std::string verticesUuid( const std::string& aArgs )
{
    if( ccIsLowerUuid( aArgs ) )
        return aArgs;

    json args = json::parse( aArgs, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 1 || !args.contains( "uuid" )
            || !args["uuid"].is_string() || !ccIsLowerUuid( args["uuid"].get<std::string>() ) )
    {
        return "";
    }

    return args["uuid"].get<std::string>();
}

/**
 * The corner (outline, hole, index) of a vertex editable item, as boardVerticesSet names it:
 * hole -1 for the outline itself. Fills aPoly and aIdx and answers true when the item has
 * corners and that corner exists; false otherwise (aItem null included).
 */
bool vertexAt( BOARD_ITEM* aItem, long long aOutline, long long aHole, long long aIndex,
               SHAPE_POLY_SET*& aPoly, SHAPE_POLY_SET::VERTEX_INDEX& aIdx )
{
    const char*     kind = "";
    SHAPE_POLY_SET* poly = aItem ? vertexPoly( aItem, kind ) : nullptr;

    if( !poly || aOutline < 0 || aOutline >= poly->OutlineCount() || aHole < -1
            || aHole >= poly->HoleCount( static_cast<int>( aOutline ) ) || aIndex < 0 )
    {
        return false;
    }

    const SHAPE_LINE_CHAIN& contour =
            aHole < 0 ? poly->COutline( static_cast<int>( aOutline ) )
                      : poly->CHole( static_cast<int>( aOutline ), static_cast<int>( aHole ) );

    if( aIndex >= contour.PointCount() )
        return false;

    aPoly = poly;
    aIdx.m_polygon = static_cast<int>( aOutline );
    aIdx.m_contour = static_cast<int>( aHole + 1 );
    aIdx.m_vertex = static_cast<int>( aIndex );
    return true;
}
} // namespace pcb

/**
 * kicadBoardVerticesGet (m3-e3, cap vertices): {uuid} (or the bare uuid) of a zone (copper,
 * non copper or rule area) or a polygon shape on the board, in the board editor or the
 * footprint editor (pcb::vertexFrame, fix e10-3). The vertex editor pane's grid
 * (pcbnew/widgets/vertex_editor_pane.cpp) as a record: pcb::verticesJson. A read; "" for any
 * other shape, no frame, an open in flight, or an item that has no corners to edit.
 */
std::string boardVerticesGet( std::string aArgsJson )
{
    PCB_BASE_EDIT_FRAME* fr = pcb::vertexFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return "";

    const std::string uuid = pcb::verticesUuid( aArgsJson );
    BOARD_ITEM*       item = uuid.empty() ? nullptr : ccResolve( fr->GetBoard(), uuid );
    const char*       kind = "";

    if( !item || !pcb::vertexPoly( item, kind ) )
        return "";

    return pcb::verticesJson( item, uuid ).dump();
}

/**
 * kicadBoardVerticesSet (m3-e3, cap vertices): {uuid, outline, hole, index, x, y}, those six
 * keys only; outline the polygon, hole -1 for the outline itself else the hole's number,
 * index the corner, x and y in mm (board coordinates). One corner moved exactly as the pane's
 * OnGridCellChange moves it: BOARD_COMMIT Modify, SHAPE_POLY_SET::SetVertex, a zone unfilled,
 * marked for refill and its hatch redrawn, Push "Edit Vertex" (one undo step). The arguments,
 * the item and the corner are checked at once. When the corner is already there nothing is
 * committed and the read back record (board.vertices.get's shape) is answered at once.
 * Otherwise the move is queued on the apply coroutine and {"queued":true} is answered: Push
 * dispatches SelectedItemsModified when the item is selected, that dispatch resumes a tool
 * coroutine, and a plain export cannot park (it trapped with SuspendError after the change was
 * already applied, t17-1). The queued body resolves the item by uuid and checks the corner
 * again (S4); the host reads the corner back once the apply queue is empty. "" for any other
 * shape, no frame, an open in flight, an item with no corners or a corner that does not exist.
 */
std::string boardVerticesSet( std::string aArgsJson )
{
    PCB_BASE_EDIT_FRAME* fr = pcb::vertexFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return "";

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 6 )
        return "";

    for( const char* key : { "uuid", "outline", "hole", "index", "x", "y" } )
    {
        if( !args.contains( key ) )
            return "";
    }

    if( !args["uuid"].is_string() || !ccIsLowerUuid( args["uuid"].get<std::string>() )
            || !args["outline"].is_number_integer() || !args["hole"].is_number_integer()
            || !args["index"].is_number_integer() || !args["x"].is_number()
            || !args["y"].is_number() )
    {
        return "";
    }

    const double x = args["x"].get<double>();
    const double y = args["y"].get<double>();

    if( !std::isfinite( x ) || !std::isfinite( y ) )
        return "";

    const double ix = x * pcbIUScale.IU_PER_MM;
    const double iy = y * pcbIUScale.IU_PER_MM;

    if( std::fabs( ix ) > std::numeric_limits<int>::max() / 2.0
            || std::fabs( iy ) > std::numeric_limits<int>::max() / 2.0 )
    {
        return "";
    }

    const std::string uuid = args["uuid"].get<std::string>();
    const long long   outline = args["outline"].get<long long>();
    const long long   hole = args["hole"].get<long long>();
    const long long   index = args["index"].get<long long>();
    const VECTOR2I    vertex( KiROUND( ix ), KiROUND( iy ) );

    BOARD_ITEM*                  item = ccResolve( fr->GetBoard(), uuid );
    SHAPE_POLY_SET*              poly = nullptr;
    SHAPE_POLY_SET::VERTEX_INDEX idx;

    if( !pcb::vertexAt( item, outline, hole, index, poly, idx ) )
        return "";

    if( poly->CVertex( idx ) == vertex )
        return pcb::verticesJson( item, uuid ).dump();

    pcbjam_collab::runOnCoroutine( fr, [fr, uuid, outline, hole, index, vertex]() {
        // Re-resolve (S4): the item may be gone, or its corners changed, by the time this runs.
        if( pcbjam_open::busy() || pcb::vertexFrame() != fr || !fr->GetBoard() )
            return;

        BOARD_ITEM*                  live = ccResolve( fr->GetBoard(), uuid );
        SHAPE_POLY_SET*              livePoly = nullptr;
        SHAPE_POLY_SET::VERTEX_INDEX liveIdx;

        if( !pcb::vertexAt( live, outline, hole, index, livePoly, liveIdx )
                || livePoly->CVertex( liveIdx ) == vertex )
        {
            return;
        }

        BOARD_COMMIT commit( fr );
        commit.Modify( live );

        livePoly->SetVertex( liveIdx, vertex );

        if( ZONE* zone = dynamic_cast<ZONE*>( live ) )
        {
            zone->UnFill();
            zone->SetNeedRefill( true );
            zone->HatchBorder();
        }

        commit.Push( _( "Edit Vertex" ) );

        if( fr->GetCanvas() )
        {
            fr->GetCanvas()->GetView()->Update( live );
            fr->GetCanvas()->Refresh();
        }
    } );

    return "{\"queued\":true}";
}
} // namespace cc_e10

EMSCRIPTEN_BINDINGS( cc_e10_pcb )
{
    function( "kicadNetReport", &cc_e10::netReport );
    function( "kicadBoardVerticesGet", &cc_e10::boardVerticesGet );
    function( "kicadBoardVerticesSet", &cc_e10::boardVerticesSet );
}
