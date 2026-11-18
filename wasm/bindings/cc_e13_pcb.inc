// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e13, the board face: textually included by pcbnew_embind.cpp after cc_rules_pcb.inc,
// outside any KICAD_MERGED_EMBIND guard (include subdir pcbnew; the merged image compiles
// that TU with -DKICAD_MERGED_EMBIND). It sees the TU's includes and helpers (pcbFrame(),
// the cc_output namespace, cc_fp_items.h's declarations in the merged image only) and calls them without editing them.
//
// Every definition of the lane lives in namespace cc_e13 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

#ifdef KICAD_MERGED_EMBIND
#include <board_commit.h>
#include <footprint.h>
#include <pcb_group.h>
#include <zone.h>
#include <string_utils.h>
#include <connectivity/topo_match.h>
#include <geometry/convex_hull.h>
#include <geometry/shape_utils.h>
#include <tools/multichannel_tool.h>
#endif

namespace cc_e13
{

#ifdef KICAD_MERGED_EMBIND

// st-mc-e: Repeat a layout (ledger PE37), KiCad's multichannel tool without its two dialogs,
// its progress window or its info bars. The public pieces of MULTICHANNEL_TOOL are called as
// they are (FindExistingRuleAreas, GeneratePotentialRuleAreas, RepeatLayout with an external
// commit and an error string); the private pieces the menu path needs are copied below from
// pcbnew/tools/multichannel_tool.cpp word for word in behavior, each named with its source.
namespace pcb
{

static const char* mcSourceName( PLACEMENT_SOURCE_T aType )
{
    switch( aType )
    {
    case PLACEMENT_SOURCE_T::SHEETNAME:       return "sheet";
    case PLACEMENT_SOURCE_T::COMPONENT_CLASS: return "componentClass";
    case PLACEMENT_SOURCE_T::GROUP_PLACEMENT: return "group";
    case PLACEMENT_SOURCE_T::DESIGN_BLOCK:    return "designBlock";
    }

    return "sheet";
}

// What a generated area is named after (AutogenerateRuleAreas' SetZoneName and
// SetPlacementAreaSource): the sheet path, the component class or the group name.
static wxString mcSourceOf( const RULE_AREA& aArea )
{
    if( aArea.m_sourceType == PLACEMENT_SOURCE_T::SHEETNAME )
        return aArea.m_sheetPath;
    else if( aArea.m_sourceType == PLACEMENT_SOURCE_T::COMPONENT_CLASS )
        return aArea.m_componentClass;

    return aArea.m_groupName;
}

// A candidate's key: "sheet:<path>", "class:<name>" or "group:<name>".
static std::string mcKeyOf( const RULE_AREA& aArea )
{
    const char* prefix = aArea.m_sourceType == PLACEMENT_SOURCE_T::SHEETNAME         ? "sheet:"
                         : aArea.m_sourceType == PLACEMENT_SOURCE_T::COMPONENT_CLASS ? "class:"
                                                                                     : "group:";
    return std::string( prefix ) + toUtf8( mcSourceOf( aArea ) );
}

static std::string mcUuidOf( const ZONE* aZone )
{
    return aZone ? aZone->m_Uuid.AsStdString() : std::string();
}

// The board's placement rule areas with their parts, as KiCad's Repeat Layout reads them.
static std::vector<RULE_AREA> mcExisting( MULTICHANNEL_TOOL* aTool )
{
    aTool->FindExistingRuleAreas();
    return aTool->GetData()->m_areas;
}

static RULE_AREA* mcFind( std::vector<RULE_AREA>& aAreas, const std::string& aUuid )
{
    for( RULE_AREA& ra : aAreas )
    {
        if( mcUuidOf( ra.m_zone ) == aUuid )
            return &ra;
    }

    return nullptr;
}

// FormatComponentList, multichannel_tool.cpp 68 to 118.
static wxString mcFormatComponentList( const std::set<FOOTPRINT*>& aComponents )
{
    std::vector<wxString> refs;

    for( FOOTPRINT* fp : aComponents )
    {
        if( !fp )
            continue;

        refs.push_back( fp->GetReferenceAsString() );
    }

    std::sort( refs.begin(), refs.end(),
               []( const wxString& aLhs, const wxString& aRhs )
               {
                   return aLhs.CmpNoCase( aRhs ) < 0;
               } );

    if( refs.empty() )
        return _( "(none)" );

    wxString result;
    wxString line;
    size_t   componentsOnLine = 0;

    for( const wxString& ref : refs )
    {
        if( componentsOnLine == 10 )
        {
            if( !result.IsEmpty() )
                result += wxT( "\n" );

            result += line;
            line.clear();
            componentsOnLine = 0;
        }

        AccumulateDescription( line, ref );
        componentsOnLine++;
    }

    if( !line.IsEmpty() )
    {
        if( !result.IsEmpty() )
            result += wxT( "\n" );

        result += line;
    }

    return result;
}

// MULTICHANNEL_TOOL::resolveConnectionTopology, multichannel_tool.cpp 1717 to 1808 (private
// there; the trace lines and timers left out). The verdict CheckRACompatibility keeps per target.
static bool mcResolveTopology( RULE_AREA* aRefArea, RULE_AREA* aTargetArea,
                               RULE_AREA_COMPAT_DATA& aMatches )
{
    using namespace TMATCH;

    if( aRefArea->m_sourceType == PLACEMENT_SOURCE_T::DESIGN_BLOCK && aRefArea->m_components.empty()
        && aTargetArea->m_components.empty() )
    {
        aMatches.m_matchingComponents.clear();
        aMatches.m_isOk = true;
        aMatches.m_errorMsg = _( "OK" );
        aMatches.m_mismatchReasons.clear();
        return true;
    }

    std::unique_ptr<CONNECTION_GRAPH> cgRef( CONNECTION_GRAPH::BuildFromFootprintSet(
            aRefArea->m_components, aTargetArea->m_components ) );
    std::unique_ptr<CONNECTION_GRAPH> cgTarget( CONNECTION_GRAPH::BuildFromFootprintSet(
            aTargetArea->m_components, aRefArea->m_components ) );

    std::vector<TMATCH::TOPOLOGY_MISMATCH_REASON> mismatchReasons;

    bool status = cgRef->FindIsomorphism( cgTarget.get(), aMatches.m_matchingComponents,
                                          mismatchReasons );

    aMatches.m_isOk = status;

    if( status )
    {
        aMatches.m_errorMsg = _( "OK" );
        aMatches.m_mismatchReasons.clear();
        return true;
    }

    aMatches.m_mismatchReasons.clear();

    for( const auto& reason : mismatchReasons )
    {
        if( reason.m_reason.IsEmpty() )
            continue;

        if( !reason.m_reference.IsEmpty() && !reason.m_candidate.IsEmpty() )
        {
            aMatches.m_mismatchReasons.push_back( wxString::Format( wxT( "%s -> %s: %s" ),
                                                                    reason.m_reference,
                                                                    reason.m_candidate,
                                                                    reason.m_reason ) );
        }
        else if( !reason.m_reference.IsEmpty() )
        {
            aMatches.m_mismatchReasons.push_back( wxString::Format( wxT( "%s: %s" ),
                                                                    reason.m_reference,
                                                                    reason.m_reason ) );
        }
        else
            aMatches.m_mismatchReasons.push_back( reason.m_reason );
    }

    if( aMatches.m_mismatchReasons.empty() )
        aMatches.m_mismatchReasons.push_back( _( "Topology mismatch" ) );

    if( aRefArea->m_components.size() != aTargetArea->m_components.size() )
    {
        aMatches.m_mismatchReasons.push_back( wxString::Format(
                _( "Reference area total components: %d" ), (int) aRefArea->m_components.size() ) );
        aMatches.m_mismatchReasons.push_back( wxString::Format(
                _( "Reference area components:\n%s" ),
                mcFormatComponentList( aRefArea->m_components ) ) );
        aMatches.m_mismatchReasons.push_back( wxString::Format(
                _( "Target area total components: %d" ), (int) aTargetArea->m_components.size() ) );
        aMatches.m_mismatchReasons.push_back( wxString::Format(
                _( "Target area components:\n%s" ),
                mcFormatComponentList( aTargetArea->m_components ) ) );
    }

    aMatches.m_errorMsg = aMatches.m_mismatchReasons.front();

    return status;
}

// MULTICHANNEL_TOOL::queryBoardItemsInGroup, multichannel_tool.cpp 435 to 452.
static std::set<BOARD_ITEM*> mcBoardItemsInGroup( BOARD* aBoard, const wxString& aGroupName )
{
    std::set<BOARD_ITEM*> rv;

    for( PCB_GROUP* group : aBoard->Groups() )
    {
        if( group->GetName() != aGroupName )
            continue;

        for( EDA_ITEM* item : group->GetItems() )
        {
            if( item->IsBOARD_ITEM() )
                rv.insert( static_cast<BOARD_ITEM*>( item ) );
        }
    }

    return rv;
}

// MULTICHANNEL_TOOL::buildRAOutline (footprints), multichannel_tool.cpp 455 to 472.
static SHAPE_LINE_CHAIN mcOutline( const std::set<FOOTPRINT*>& aFootprints, int aMargin )
{
    std::vector<VECTOR2I> bbCorners;
    bbCorners.reserve( aFootprints.size() * 4 );

    for( FOOTPRINT* fp : aFootprints )
    {
        const BOX2I bb = fp->GetBoundingBox( false ).GetInflated( aMargin );
        KIGEOM::CollectBoxCorners( bb, bbCorners );
    }

    std::vector<VECTOR2I> hullVertices;
    BuildConvexHull( hullVertices, bbCorners );

    SHAPE_LINE_CHAIN hull( hullVertices );

    return KIGEOM::RectifyPolygon( hull );
}

// MULTICHANNEL_TOOL::buildRAOutline (board items), multichannel_tool.cpp 475 to 497.
static SHAPE_LINE_CHAIN mcOutline( const std::set<BOARD_ITEM*>& aItems, int aMargin )
{
    std::vector<VECTOR2I> bbCorners;
    bbCorners.reserve( aItems.size() * 4 );

    for( BOARD_ITEM* item : aItems )
    {
        BOX2I bb = item->GetBoundingBox();

        if( item->Type() == PCB_FOOTPRINT_T )
            bb = static_cast<FOOTPRINT*>( item )->GetBoundingBox( false );

        KIGEOM::CollectBoxCorners( bb.GetInflated( aMargin ), bbCorners );
    }

    std::vector<VECTOR2I> hullVertices;
    BuildConvexHull( hullVertices, bbCorners );

    SHAPE_LINE_CHAIN hull( hullVertices );

    return KIGEOM::RectifyPolygon( hull );
}

// MULTICHANNEL_TOOL::pruneExistingGroups, multichannel_tool.cpp 1811 to 1842.
static void mcPruneExistingGroups( BOARD* aBoard, COMMIT& aCommit,
                                   const std::unordered_set<BOARD_ITEM*>& aItemsToRemove )
{
    for( PCB_GROUP* group : aBoard->Groups() )
    {
        std::vector<EDA_ITEM*> pruneList;

        for( EDA_ITEM* refItem : group->GetItems() )
        {
            for( BOARD_ITEM* testItem : aItemsToRemove )
            {
                if( refItem->m_Uuid == testItem->m_Uuid )
                    pruneList.push_back( refItem );
            }
        }

        if( !pruneList.empty() )
        {
            aCommit.Modify( group );

            for( EDA_ITEM* item : pruneList )
                group->RemoveItem( item );

            if( group->GetItems().size() < 2 )
                aCommit.Remove( group );
        }
    }
}

// The arguments of kicadMultichannelApply, read whole.
struct McApplyArgs
{
    std::string              dir;
    std::string              op;
    std::vector<std::string> areas;   // generate: candidate keys
    bool                     replaceExisting = false;
    bool                     groupItems = false;
    std::string              ref;     // repeat: the reference area's zone uuid
    std::vector<std::string> targets; // repeat: target zone uuids
    REPEAT_LAYOUT_OPTIONS    options;
    std::string              anchor;  // repeat: a footprint uuid of the reference area, or ""
    bool                     preview = false;
};

static bool mcReadStrings( const json& aValue, bool aUuids, std::vector<std::string>& aOut )
{
    if( !aValue.is_array() || aValue.empty() )
        return false;

    for( const json& v : aValue )
    {
        if( !v.is_string() )
            return false;

        std::string s = v.get<std::string>();

        if( s.empty() || ( aUuids && !ccIsLowerUuid( s ) ) )
            return false;

        if( std::find( aOut.begin(), aOut.end(), s ) != aOut.end() )
            return false;

        aOut.push_back( s );
    }

    return true;
}

// {run, dir, op: "generate", areas: string[], replaceExisting: bool, groupItems?: bool} or
// {run, dir, op: "repeat", ref, targets: string[], options: {...}, preview: bool}; false for
// any other shape. options holds any of copyRouting, connectedRoutingOnly, copyPlacement,
// copyOtherItems, includeLockedItems (booleans, KiCad's defaults when absent) and anchor (a
// footprint uuid or ""). groupItems is not an option of repeat: KiCad's per target RepeatLayout
// keeps the items it would group to itself (see the notes).
static bool mcReadApplyArgs( const std::string& aArgsJson, McApplyArgs& aOut )
{
    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() )
        return false;

    auto run = args.find( "run" );
    auto dir = args.find( "dir" );
    auto op = args.find( "op" );

    if( run == args.end() || !run->is_number_integer() || run->get<long long>() < 0
            || dir == args.end() || !dir->is_string() || op == args.end() || !op->is_string() )
    {
        return false;
    }

    aOut.dir = dir->get<std::string>();
    aOut.op = op->get<std::string>();

    if( !ccIsRunDir( aOut.dir, "/tmp/cc-multichannel/" ) )
        return false;

    if( aOut.op == "generate" )
    {
        auto areas = args.find( "areas" );
        auto replace = args.find( "replaceExisting" );
        auto group = args.find( "groupItems" );

        if( areas == args.end() || replace == args.end() || !replace->is_boolean() )
            return false;

        if( group != args.end() && !group->is_boolean() )
            return false;

        if( args.size() != ( group != args.end() ? 6u : 5u ) )
            return false;

        if( !mcReadStrings( *areas, false, aOut.areas ) )
            return false;

        aOut.replaceExisting = replace->get<bool>();
        aOut.groupItems = group != args.end() && group->get<bool>();
        return true;
    }

    if( aOut.op != "repeat" || args.size() != 7 )
        return false;

    auto ref = args.find( "ref" );
    auto targets = args.find( "targets" );
    auto options = args.find( "options" );
    auto preview = args.find( "preview" );

    if( ref == args.end() || !ref->is_string() || targets == args.end()
            || options == args.end() || !options->is_object() || preview == args.end()
            || !preview->is_boolean() )
    {
        return false;
    }

    aOut.ref = ref->get<std::string>();
    aOut.preview = preview->get<bool>();

    if( !ccIsLowerUuid( aOut.ref ) || !mcReadStrings( *targets, true, aOut.targets ) )
        return false;

    if( std::find( aOut.targets.begin(), aOut.targets.end(), aOut.ref ) != aOut.targets.end() )
        return false;

    for( auto it = options->begin(); it != options->end(); ++it )
    {
        const std::string& key = it.key();

        if( key == "anchor" )
        {
            if( !it->is_string() )
                return false;

            aOut.anchor = it->get<std::string>();

            if( !aOut.anchor.empty() && !ccIsLowerUuid( aOut.anchor ) )
                return false;

            continue;
        }

        if( !it->is_boolean() )
            return false;

        const bool v = it->get<bool>();

        if( key == "copyRouting" )
            aOut.options.m_copyRouting = v;
        else if( key == "connectedRoutingOnly" )
            aOut.options.m_connectedRoutingOnly = v;
        else if( key == "copyPlacement" )
            aOut.options.m_copyPlacement = v;
        else if( key == "copyOtherItems" )
            aOut.options.m_copyOtherItems = v;
        else if( key == "includeLockedItems" )
            aOut.options.m_includeLockedItems = v;
        else
            return false;
    }

    return true;
}

static json mcLines( const std::vector<wxString>& aLines )
{
    json out = json::array();

    for( const wxString& line : aLines )
        out.push_back( toUtf8( line ) );

    return out;
}

// <dir>/multichannel.json (MultichannelReport with op, error and message beside it), then
// <dir>/.cc-multichannel.json last: {"code": 0} or {"code": 1, "error": <code>}.
static void mcWriteRecord( const std::string& aDir, json& aReport )
{
    const std::string error = aReport.value( "error", std::string() );

    ccWriteJson( aDir, "multichannel.json", aReport );

    json done = json::object();
    done["code"] = error.empty() ? 0 : 1;
    done["error"] = error;
    ccWriteJson( aDir, ".cc-multichannel.json", done );
}

// The generate half: AutogenerateRuleAreas, multichannel_tool.cpp 1845 to 2043, past its
// dialog (the dialog's choices come from the args), as ONE commit.
static void mcGenerate( PCB_EDIT_FRAME* aFrame, MULTICHANNEL_TOOL* aTool, const McApplyArgs& aArgs,
                        json& aReport )
{
    BOARD*                 board = aFrame->GetBoard();
    std::vector<RULE_AREA> existing = mcExisting( aTool );

    aTool->GeneratePotentialRuleAreas();
    std::vector<RULE_AREA> areas = aTool->GetData()->m_areas;

    if( areas.size() <= 1 )
    {
        aReport["error"] = "too_few";
        aReport["message"] = toUtf8( _( "Cannot auto-generate any placement areas because the "
                                        "schematic has only one or no hierarchical sheets, "
                                        "groups, or component classes." ) );
        return;
    }

    for( const std::string& key : aArgs.areas )
    {
        bool found = false;

        for( RULE_AREA& ra : areas )
        {
            if( mcKeyOf( ra ) == key )
            {
                ra.m_generateEnabled = true;
                found = true;
            }
        }

        if( !found )
        {
            aReport["error"] = "not_found";
            aReport["message"] = key;
            return;
        }
    }

    for( RULE_AREA& zoneRA : existing )
    {
        if( zoneRA.m_components.empty() )
            continue;

        for( RULE_AREA& ra : areas )
        {
            if( zoneRA.m_components == ra.m_components )
            {
                ra.m_oldZone = zoneRA.m_zone;
                ra.m_existsAlready = true;
            }
        }
    }

    BOARD_COMMIT commit( aFrame->GetToolManager(), true, false );
    json         rows = json::array();
    int          applied = 0;

    for( RULE_AREA& ra : areas )
    {
        if( !ra.m_generateEnabled )
            continue;

        json row = json::object();
        row["key"] = mcKeyOf( ra );
        row["name"] = toUtf8( wxString::Format( wxT( "auto-placement-area-%s" ), mcSourceOf( ra ) ) );
        row["parts"] = (int) ra.m_components.size();
        row["reasons"] = json::array();
        row["ok"] = false;
        row["skipped"] = "";

        if( ra.m_existsAlready && !aArgs.replaceExisting )
        {
            row["skipped"] = "exists";
            rows.push_back( row );
            continue;
        }

        if( ra.m_components.empty() )
        {
            row["skipped"] = "empty";
            rows.push_back( row );
            continue;
        }

        SHAPE_LINE_CHAIN raOutline;

        if( ra.m_sourceType == PLACEMENT_SOURCE_T::GROUP_PLACEMENT )
        {
            std::set<BOARD_ITEM*> groupItems = mcBoardItemsInGroup( board, ra.m_groupName );

            if( groupItems.empty() )
            {
                row["skipped"] = "empty";
                rows.push_back( row );
                continue;
            }

            raOutline = mcOutline( groupItems, 100000 );
        }
        else
        {
            raOutline = mcOutline( ra.m_components, 100000 );
        }

        std::unique_ptr<ZONE> newZone( new ZONE( board ) );

        newZone->SetZoneName( wxString::Format( wxT( "auto-placement-area-%s" ), mcSourceOf( ra ) ) );
        newZone->SetIsRuleArea( true );
        newZone->SetLayerSet( LSET::AllCuMask() );
        newZone->SetPlacementAreaEnabled( true );
        newZone->SetDoNotAllowZoneFills( false );
        newZone->SetDoNotAllowVias( false );
        newZone->SetDoNotAllowTracks( false );
        newZone->SetDoNotAllowPads( false );
        newZone->SetDoNotAllowFootprints( false );

        if( ra.m_sourceType == PLACEMENT_SOURCE_T::SHEETNAME )
            newZone->SetPlacementAreaSourceType( PLACEMENT_SOURCE_T::SHEETNAME );
        else if( ra.m_sourceType == PLACEMENT_SOURCE_T::COMPONENT_CLASS )
            newZone->SetPlacementAreaSourceType( PLACEMENT_SOURCE_T::COMPONENT_CLASS );
        else
            newZone->SetPlacementAreaSourceType( PLACEMENT_SOURCE_T::GROUP_PLACEMENT );

        newZone->SetPlacementAreaSource( mcSourceOf( ra ) );
        newZone->AddPolygon( raOutline );
        newZone->SetHatchStyle( ZONE_BORDER_DISPLAY_STYLE::NO_HATCH );

        if( ra.m_existsAlready )
            commit.Remove( ra.m_oldZone );

        ra.m_zone = newZone.release();
        commit.Add( ra.m_zone );

        row["ok"] = true;
        row["zone"] = mcUuidOf( ra.m_zone );
        rows.push_back( row );
        applied++;
    }

    if( aArgs.groupItems )
    {
        for( RULE_AREA& ra : areas )
        {
            if( !ra.m_generateEnabled )
                continue;

            if( ra.m_existsAlready && !aArgs.replaceExisting )
                continue;

            if( ra.m_components.empty() )
                continue;

            // AutogenerateRuleAreas would group an area whose zone it skipped (a source group
            // with no board items) through a zone pointer it never set; such an area is passed.
            if( !ra.m_zone )
                continue;

            std::unordered_set<BOARD_ITEM*> toPrune;

            std::copy( ra.m_components.begin(), ra.m_components.end(),
                       std::inserter( toPrune, toPrune.begin() ) );

            if( ra.m_existsAlready )
                toPrune.insert( ra.m_zone );

            mcPruneExistingGroups( board, commit, toPrune );

            PCB_GROUP* group = new PCB_GROUP( board );

            commit.Add( group );

            commit.Modify( ra.m_zone );
            group->AddItem( ra.m_zone );

            for( FOOTPRINT* fp : ra.m_components )
            {
                commit.Modify( fp );
                group->AddItem( fp );
            }
        }
    }

    if( applied > 0 )
        commit.Push( _( "Auto-generate placement rule areas" ) );
    else
        commit.Revert();

    aReport["areas"] = rows;
    aReport["applied"] = applied;
}

// The repeat half: per target KiCad's own verdict (the copied topology check), then
// MULTICHANNEL_TOOL::RepeatLayout( event, ref, target, options, &commit, &error ) into ONE
// external commit: reverted for a preview or a failed copy (as KiCad's own Repeat Layout
// reverts on a failed copy), else pushed as "Repeat layout".
static void mcRepeat( PCB_EDIT_FRAME* aFrame, MULTICHANNEL_TOOL* aTool, const McApplyArgs& aArgs,
                      json& aReport )
{
    std::vector<RULE_AREA> areas = mcExisting( aTool );
    RULE_AREA*             ref = mcFind( areas, aArgs.ref );
    std::vector<RULE_AREA*> targets;

    if( !ref )
    {
        aReport["error"] = "not_found";
        aReport["message"] = aArgs.ref;
        return;
    }

    for( const std::string& uuid : aArgs.targets )
    {
        RULE_AREA* t = mcFind( areas, uuid );

        if( !t || t->m_zone == ref->m_zone )
        {
            aReport["error"] = "not_found";
            aReport["message"] = uuid;
            return;
        }

        targets.push_back( t );
    }

    REPEAT_LAYOUT_OPTIONS options = aArgs.options;

    if( !aArgs.anchor.empty() )
    {
        for( FOOTPRINT* fp : ref->m_components )
        {
            if( fp->m_Uuid.AsStdString() == aArgs.anchor )
                options.m_anchorFp = fp;
        }

        if( !options.m_anchorFp )
        {
            aReport["error"] = "not_found";
            aReport["message"] = aArgs.anchor;
            return;
        }
    }

    BOARD_COMMIT commit( aFrame->GetToolManager(), true, false );
    json         rows = json::array();
    int          applied = 0;
    bool         failed = false;

    for( RULE_AREA* target : targets )
    {
        RULE_AREA_COMPAT_DATA compat;
        mcResolveTopology( ref, target, compat );

        json row = json::object();
        row["zone"] = mcUuidOf( target->m_zone );
        row["name"] = toUtf8( target->m_ruleName );
        row["parts"] = (int) target->m_components.size();
        row["ok"] = compat.m_isOk;
        row["reasons"] = mcLines( compat.m_mismatchReasons );

        if( compat.m_isOk )
        {
            REPEAT_LAYOUT_OPTIONS each = options;
            wxString              error;

            if( aTool->RepeatLayout( TOOL_EVENT(), *ref, *target, each, &commit, &error ) != 0 )
            {
                row["ok"] = false;
                row["reasons"] = mcLines( { error } );
                rows.push_back( row );
                aReport["message"] = toUtf8( error );
                failed = true;
                break;
            }

            applied++;
        }

        rows.push_back( row );
    }

    if( failed )
    {
        commit.Revert();
        applied = 0;
        aReport["error"] = "copy_failed";
    }
    else if( aArgs.preview || applied == 0 )
    {
        commit.Revert();
    }
    else
    {
        commit.Push( _( "Repeat layout" ) );
    }

    aReport["areas"] = rows;
    aReport["applied"] = applied;
}

} // namespace pcb

// st-mc-e, the read (merged image only). {op: "areas"}: {"areas": [{key, name, source,
// sheetPath, parts, exists, zone}], "ruleAreas": [{zone, name, source, sourceName, parts}],
// "canGenerate": bool}: every candidate GeneratePotentialRuleAreas finds (named as
// AutogenerateRuleAreas would name its zone; exists and zone when a placement rule area holds
// the same parts, its rule) and every placement rule area FindExistingRuleAreas finds.
// {op: "check", ref}: {"checks": [{zone, name, parts, matched, ok, message, reasons}]}, KiCad's
// verdict for every other placement rule area against the reference (CheckRACompatibility's
// per target body, without its progress window), or {"checks": [], "error": "not_found"}.
// "" for any other shape, with no board or while an open is in flight. Synchronous; nothing in
// the design changes.
std::string multichannel( std::string aArgsJson )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return "";

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() )
        return "";

    auto op = args.find( "op" );

    if( op == args.end() || !op->is_string() )
        return "";

    const std::string which = op->get<std::string>();
    std::string       refUuid;

    if( which == "areas" )
    {
        if( args.size() != 1 )
            return "";
    }
    else if( which == "check" )
    {
        auto ref = args.find( "ref" );

        if( args.size() != 2 || ref == args.end() || !ref->is_string() )
            return "";

        refUuid = ref->get<std::string>();

        if( !ccIsLowerUuid( refUuid ) )
            return "";
    }
    else
    {
        return "";
    }

    MULTICHANNEL_TOOL* tool = fr->GetToolManager()->GetTool<MULTICHANNEL_TOOL>();

    if( !tool )
        return "";

    wxLogNull noLog;

    try
    {
        std::vector<RULE_AREA> existing = pcb::mcExisting( tool );
        json                   out = json::object();

        if( which == "check" )
        {
            RULE_AREA* ref = pcb::mcFind( existing, refUuid );
            json       checks = json::array();

            if( !ref )
            {
                out["checks"] = checks;
                out["error"] = "not_found";
                return out.dump( -1, ' ', false, json::error_handler_t::replace );
            }

            for( RULE_AREA& target : existing )
            {
                if( target.m_zone == ref->m_zone )
                    continue;

                RULE_AREA_COMPAT_DATA compat;
                pcb::mcResolveTopology( ref, &target, compat );

                json row = json::object();
                row["zone"] = pcb::mcUuidOf( target.m_zone );
                row["name"] = toUtf8( target.m_ruleName );
                row["parts"] = (int) target.m_components.size();
                row["matched"] = (int) compat.m_matchingComponents.size();
                row["ok"] = compat.m_isOk;
                row["message"] = toUtf8( compat.m_errorMsg );
                row["reasons"] = pcb::mcLines( compat.m_mismatchReasons );
                checks.push_back( row );
            }

            out["checks"] = checks;
            out["error"] = "";
            return out.dump( -1, ' ', false, json::error_handler_t::replace );
        }

        tool->GeneratePotentialRuleAreas();
        std::vector<RULE_AREA> candidates = tool->GetData()->m_areas;
        json                   rows = json::array();
        json                   ruleAreas = json::array();

        for( const RULE_AREA& ra : candidates )
        {
            std::string zone;

            // AutogenerateRuleAreas' rule: an existing placement rule area holding exactly the
            // same parts (the last such zone wins, as there).
            for( const RULE_AREA& ex : existing )
            {
                if( !ex.m_components.empty() && ex.m_components == ra.m_components )
                    zone = pcb::mcUuidOf( ex.m_zone );
            }

            json row = json::object();
            row["key"] = pcb::mcKeyOf( ra );
            row["name"] = toUtf8(
                    wxString::Format( wxT( "auto-placement-area-%s" ), pcb::mcSourceOf( ra ) ) );
            row["source"] = pcb::mcSourceName( ra.m_sourceType );
            row["sheetPath"] = ra.m_sourceType == PLACEMENT_SOURCE_T::SHEETNAME
                                       ? toUtf8( ra.m_sheetPath )
                                       : std::string();
            row["parts"] = (int) ra.m_components.size();
            row["exists"] = !zone.empty();
            row["zone"] = zone;
            rows.push_back( row );
        }

        for( const RULE_AREA& ex : existing )
        {
            json row = json::object();
            row["zone"] = pcb::mcUuidOf( ex.m_zone );
            row["name"] = toUtf8( ex.m_ruleName );
            row["source"] = pcb::mcSourceName( ex.m_zone->GetPlacementAreaSourceType() );
            row["sourceName"] = toUtf8( ex.m_zone->GetPlacementAreaSource() );
            row["parts"] = (int) ex.m_components.size();
            ruleAreas.push_back( row );
        }

        out["areas"] = rows;
        out["ruleAreas"] = ruleAreas;
        out["canGenerate"] = candidates.size() > 1;
        return out.dump( -1, ' ', false, json::error_handler_t::replace );
    }
    catch( ... )
    {
        return "";
    }
}

// st-mc-e, the apply (merged image only). {run, dir, op: "generate", ...} makes the chosen
// candidates' placement rule areas (AutogenerateRuleAreas' write half, its dialog's choices
// from the args); {run, dir, op: "repeat", ...} repeats the reference area's layout onto the
// targets through RepeatLayout. Each as ONE commit (one undo step) on the apply coroutine, a
// preview reverted; <dir>/multichannel.json then <dir>/.cc-multichannel.json last. False for
// args of another shape, with no board or while an open is in flight.
bool multichannelApply( std::string aArgsJson )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return false;

    pcb::McApplyArgs a;

    if( !pcb::mcReadApplyArgs( aArgsJson, a ) )
        return false;

    cc_output::EnsureDir( a.dir );

    pcbjam_collab::runOnCoroutine( fr, [a]() {
        json report = json::object();
        report["op"] = a.op;
        report["preview"] = a.op == "repeat" && a.preview;
        report["areas"] = json::array();
        report["applied"] = 0;
        report["error"] = "";
        report["message"] = "";

        PCB_EDIT_FRAME*    f = pcbFrame();
        MULTICHANNEL_TOOL* tool = f && f->GetBoard()
                                          ? f->GetToolManager()->GetTool<MULTICHANNEL_TOOL>()
                                          : nullptr;

        if( !tool )
        {
            report["error"] = "no_board";
        }
        else
        {
            wxLogNull noLog;

            try
            {
                if( a.op == "generate" )
                    pcb::mcGenerate( f, tool, a, report );
                else
                    pcb::mcRepeat( f, tool, a, report );
            }
            catch( ... )
            {
                report["error"] = "failed";
            }
        }

        report["depth"] = pcbjam_collab::testUndoDepth( f );
        pcb::mcWriteRecord( a.dir, report );
    } );

    return true;
}

#endif // KICAD_MERGED_EMBIND

} // namespace cc_e13

EMSCRIPTEN_BINDINGS( cc_e13_pcb )
{
#ifdef KICAD_MERGED_EMBIND
    function( "kicadMultichannel", &cc_e13::multichannel );
    function( "kicadMultichannelApply", &cc_e13::multichannelApply );
#endif
}
