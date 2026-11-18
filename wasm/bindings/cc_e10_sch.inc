// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e10, the schematic face: textually included by eeschema_embind.cpp after
// cc_rules_sch.inc, outside any KICAD_MERGED_EMBIND guard (include subdir eeschema; the
// merged image compiles that TU with -DKICAD_MERGED_EMBIND). It sees the TU's includes and
// helpers (schFrame(), the cc_output namespace) and calls them without editing them.
//
// Every definition of the lane lives in namespace cc_e10 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

#include <cc_diff_finding.h>
#include <project_sch.h>
#include <libraries/symbol_library_adapter.h>
#include <eda_search_data.h>
#include <sch_label.h>
#include <sch_sheet_pin.h>
#include <sch_text.h>
#include <sch_textbox.h>
#include <string_utils.h>

namespace cc_e10
{
namespace sch
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

/** One schematic hit: the item KiCad matched, the path it was matched on, its record. */
struct SCH_HIT_ROW
{
    SCH_ITEM*      item = nullptr;
    SCH_SHEET_PATH path;
    json           hit;
};

/** The sch face's flags of 7.1, read into KiCad's SCH_SEARCH_DATA and our own filters. */
struct SCH_FIND
{
    SCH_SEARCH_DATA             data;
    std::map<std::string, bool> flags;
};

bool readSchFind( const json& aArgs, SCH_FIND& aFind )
{
    if( !readFindArgs( aArgs, "sch", aFind.data ) )
        return false;

    if( !readFaceFlags( aArgs["sch"],
                        { "refs", "values", "fields", "hidden", "pins", "labels", "nets",
                          "thisSheet", "selected" },
                        aFind.flags ) )
    {
        return false;
    }

    aFind.data.searchAllFields = aFind.flags["hidden"];
    aFind.data.searchAllPins = aFind.flags["pins"];
    aFind.data.searchNetNames = aFind.flags["nets"];
    aFind.data.searchCurrentSheetOnly = aFind.flags["thisSheet"] || aFind.flags["selected"];
    aFind.data.searchSelectedOnly = aFind.flags["selected"];
    aFind.data.replaceReferences = aFind.flags["refs"];
    return true;
}

/**
 * Every hit of aFind over the live schematic, in SCH_FIND_REPLACE_TOOL's walk: each screen
 * once per sheet path holding it (a reference field on every path, any other item on the
 * first path only, as ReplaceAll does), each item with its fields and pins (the symbol itself
 * stands for its fields, so it is not listed twice), KiCad's own Matches deciding every hit.
 * At most aMax rows; aTruncated when more matched.
 */
std::vector<SCH_HIT_ROW> schFindRows( SCH_EDIT_FRAME* aFrame, SCH_FIND& aFind, size_t aMax,
                                      bool& aTruncated )
{
    std::vector<SCH_HIT_ROW>    rows;
    std::vector<SCH_SHEET_PATH> paths;
    std::map<std::string, bool>& f = aFind.flags;
    const SCH_SEARCH_DATA&      data = aFind.data;

    aTruncated = false;

    if( data.searchCurrentSheetOnly )
    {
        paths.push_back( aFrame->GetCurrentSheet() );
    }
    else
    {
        for( const SCH_SHEET_PATH& path : aFrame->Schematic().Hierarchy() )
            paths.push_back( path );
    }

    std::set<SCH_SCREEN*> seenScreens;

    for( SCH_SHEET_PATH& path : paths )
    {
        SCH_SCREEN* screen = path.LastScreen();

        if( !screen )
            continue;

        const bool firstPath = seenScreens.insert( screen ).second;

        auto add =
                [&]( SCH_ITEM* aItem, const char* aKind, const std::string& aUuid,
                     const wxString& aRef, const wxString& aField, const wxString& aText,
                     bool aDirect )
                {
                    if( rows.size() >= aMax )
                    {
                        aTruncated = true;
                        return;
                    }

                    SCH_HIT_ROW row;
                    row.item = aItem;
                    row.path = path;
                    row.hit = hitJson( aUuid, path.PathAsString(), aKind, aRef, aField, aText,
                                       data, aDirect );
                    rows.push_back( std::move( row ) );
                };

        auto selectedOk =
                [&]( const SCH_ITEM* aItem )
                {
                    if( !data.searchSelectedOnly )
                        return true;

                    if( aItem->IsSelected() )
                        return true;

                    const EDA_ITEM* parent = aItem->GetParent();
                    return parent && parent->IsSelected();
                };

        auto fieldHit =
                [&]( SCH_FIELD& aField, SCH_ITEM* aOwner, const wxString& aRef )
                {
                    const std::string owner = toUtf8( aOwner->m_Uuid.AsString() );

                    if( aField.GetId() == FIELD_T::REFERENCE && aOwner->Type() == SCH_SYMBOL_T )
                    {
                        if( !f["refs"] || ( !aField.IsVisible() && !data.searchAllFields ) )
                            return;

                        // The designator on this path, as SCH_FIELD::Matches reads it (that
                        // body first asks the whole symbol, which would list a designator
                        // for any other field's match).
                        SCH_SYMBOL* sym = static_cast<SCH_SYMBOL*>( aOwner );
                        wxString    ref = sym->GetRef( &path );

                        if( textMatches( &aField, ref, data ) )
                        {
                            add( &aField, "symbol", owner, ref, aField.GetName( false ), ref,
                                 true );
                            return;
                        }

                        if( sym->GetUnitCount() > 1 )
                        {
                            ref << sym->SubReference( sym->GetUnitSelection( &path ) );

                            if( textMatches( &aField, ref, data ) )
                                add( &aField, "symbol", owner, ref, aField.GetName( false ), ref,
                                     true );
                        }

                        return;
                    }

                    if( !firstPath )
                        return;

                    const bool isValue = aField.GetId() == FIELD_T::VALUE
                                         && aOwner->Type() == SCH_SYMBOL_T;

                    if( isValue ? !f["values"] : !( f["fields"] || f["hidden"] ) )
                        return;

                    // "fields" alone looks in the visible ones; "hidden" adds the others.
                    if( !isValue && !f["fields"] && aField.IsVisible() )
                        return;

                    if( !aField.Matches( data, &path ) )
                        return;

                    wxString ref;

                    if( aOwner->Type() == SCH_SYMBOL_T )
                        ref = static_cast<SCH_SYMBOL*>( aOwner )->GetRef( &path );

                    add( &aField, isValue ? "symbol" : "field", owner, ref,
                         aField.GetName( false ), UnescapeString( aField.GetText() ), true );
                };

        for( SCH_ITEM* item : screen->Items() )
        {
            if( !selectedOk( item ) )
                continue;

            if( item->Type() == SCH_SYMBOL_T )
            {
                SCH_SYMBOL*    sym = static_cast<SCH_SYMBOL*>( item );
                const wxString ref = sym->GetRef( &path );

                for( SCH_FIELD& field : sym->GetFields() )
                    fieldHit( field, sym, ref );

                if( !firstPath || !( f["pins"] || f["nets"] ) )
                    continue;

                for( SCH_PIN* pin : sym->GetPins( &path ) )
                {
                    if( !pin->Matches( data, &path ) )
                        continue;

                    const bool own = f["pins"]
                                     && ( textMatches( pin, pin->GetName(), data )
                                          || textMatches( pin, pin->GetNumber(), data ) );
                    wxString   text = textMatches( pin, pin->GetName(), data ) ? pin->GetName()
                                                                               : pin->GetNumber();

                    if( !own )
                    {
                        SCH_CONNECTION* conn = pin->Connection( &path );
                        text = conn ? conn->GetNetName() : wxString();
                    }

                    add( pin, own ? "pin" : "net", toUtf8( sym->m_Uuid.AsString() ), ref,
                         pin->GetNumber(), text, own );
                }
            }
            else if( item->Type() == SCH_SHEET_T )
            {
                SCH_SHEET* sheet = static_cast<SCH_SHEET*>( item );

                for( SCH_FIELD& field : sheet->GetFields() )
                    fieldHit( field, sheet, wxEmptyString );

                if( !firstPath )
                    continue;

                for( SCH_SHEET_PIN* pin : sheet->GetPins() )
                {
                    if( !( f["labels"] || f["nets"] ) || !pin->Matches( data, &path ) )
                        continue;

                    const wxString text = UnescapeString( pin->GetText() );
                    const bool     own = textMatches( pin, text, data );

                    if( own ? !f["labels"] : !f["nets"] )
                        continue;

                    add( pin, own ? "label" : "net", toUtf8( pin->m_Uuid.AsString() ),
                         wxEmptyString, wxEmptyString, text, own );
                }
            }
            else if( item->IsType( { SCH_LABEL_LOCATE_ANY_T } ) )
            {
                SCH_LABEL_BASE* label = static_cast<SCH_LABEL_BASE*>( item );

                for( SCH_FIELD& field : label->GetFields() )
                    fieldHit( field, label, wxEmptyString );

                if( !firstPath || !( f["labels"] || f["nets"] ) || !label->Matches( data, &path ) )
                    continue;

                const wxString text = UnescapeString( label->GetText() );
                const bool     own = textMatches( label, text, data );

                if( own ? !f["labels"] : !f["nets"] )
                    continue;

                add( label, own ? "label" : "net", toUtf8( label->m_Uuid.AsString() ),
                     wxEmptyString, wxEmptyString, text, own );
            }
            else if( item->Type() == SCH_TEXT_T || item->Type() == SCH_TEXTBOX_T )
            {
                if( !firstPath || !f["labels"] || !item->Matches( data, &path ) )
                    continue;

                const EDA_TEXT* text = dynamic_cast<const EDA_TEXT*>( item );

                add( item, "text", toUtf8( item->m_Uuid.AsString() ), wxEmptyString,
                     wxEmptyString, text ? UnescapeString( text->GetText() ) : wxString(), true );
            }
        }
    }

    return rows;
}
} // namespace sch

/**
 * kicadCompareWithSource, the schematic half (st-compare-e, K-DIFF): {uuid, sheetPath?} (those
 * keys only; symbol.get's grammar). The placed symbol against its source exactly as
 * SCH_INSPECTION_TOOL::DiffSymbol compares them: the instance's library copy flattened, the
 * instance's fields carried onto it, LIB_SYMBOL::Compare with ERC flags; the source loaded
 * through the project's own symbol libraries. Answers 7.4's CompareResult: status "same",
 * "differs" (KiCad's return value) or "noSource" (library absent, disabled, or the item gone),
 * source the LIB_ID text, one finding per line Compare reported. A read; "" for any other
 * shape, no frame, an open in flight, or no such symbol.
 */
std::string compareSch( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return "";

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || !args.contains( "uuid" )
            || !args["uuid"].is_string() )
    {
        return "";
    }

    for( auto it = args.begin(); it != args.end(); ++it )
    {
        if( it.key() != "uuid" && it.key() != "sheetPath" )
            return "";
    }

    std::string sheetPath;

    if( args.contains( "sheetPath" ) )
    {
        if( !args["sheetPath"].is_string() )
            return "";

        sheetPath = args["sheetPath"].get<std::string>();
    }

    SYMBOL_HIT hit = resolveSymbol( fr, args["uuid"].get<std::string>(), sheetPath,
                                    /*aStrict*/ false );

    if( !hit.symbol || !hit.symbol->GetLibSymbolRef() )
        return "";

    SCH_SYMBOL*    symbol = hit.symbol;
    const LIB_ID&  libId = symbol->GetLibId();
    const wxString libName = libId.GetLibNickname();
    const wxString itemName = libId.GetLibItemName();
    const wxString source = libId.Format().wx_str();

    SYMBOL_LIBRARY_ADAPTER* libs = PROJECT_SCH::SymbolLibAdapter( &fr->Prj() );

    if( !libs || !libs->HasLibrary( libName, /*aCheckEnabled*/ true ) )
        return sch::compareAnswer( "noSource", source, json::array() ).dump();

    std::unique_ptr<LIB_SYMBOL> flattenedLibSymbol;

    try
    {
        if( LIB_SYMBOL* libAlias = libs->LoadSymbol( libName, itemName ) )
            flattenedLibSymbol = libAlias->Flatten();
    }
    catch( const IO_ERROR& )
    {
    }

    if( !flattenedLibSymbol )
        return sch::compareAnswer( "noSource", source, json::array() ).dump();

    std::unique_ptr<LIB_SYMBOL> flattenedSchSymbol = symbol->GetLibSymbolRef()->Flatten();
    std::vector<SCH_FIELD>      fields;

    // DiffSymbol's own lines: the instance's fields, moved into the symbol's frame.
    for( SCH_FIELD& field : symbol->GetFields() )
    {
        fields.emplace_back( SCH_FIELD( flattenedLibSymbol.get(), field.GetId(),
                                        field.GetName( false ) ) );
        fields.back().CopyText( field );
        fields.back().SetAttributes( field );
        fields.back().Move( -symbol->GetPosition() );
    }

    flattenedSchSymbol->SetFields( fields );

    CC_DIFF_REPORTER reporter;
    const int        cmp = flattenedSchSymbol->Compare( *flattenedLibSymbol,
                                                        SCH_ITEM::COMPARE_FLAGS::ERC, &reporter );

    return sch::compareAnswer( cmp == 0 ? "same" : "differs", source,
                               sch::findingsJson( reporter ) )
            .dump();
}

/**
 * kicadFind, the schematic half (st-find-e-e, cap find): 7.1's FindArgs with the sch object
 * {refs, values, fields, hidden, pins, labels, nets, thisSheet, selected}. KiCad's own
 * matching (SCH_SEARCH_DATA, each item's Matches) over the live schematic, unsaved edits
 * included; answers {hits, truncated} (at most 4,096 hits). A read; "" for any other shape,
 * no frame or an open in flight.
 */
std::string findSch( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return "";

    json         args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );
    sch::SCH_FIND find;

    if( args.is_discarded() || !sch::readSchFind( args, find ) )
        return "";

    bool truncated = false;
    json hits = json::array();

    for( sch::SCH_HIT_ROW& row : sch::schFindRows( fr, find, sch::kMaxHits, truncated ) )
        hits.push_back( std::move( row.hit ) );

    return json( { { "hits", std::move( hits ) }, { "truncated", truncated } } ).dump();
}

namespace sch
{
/** The keys that name one hit: the page sends back what kicadFind answered. */
std::string hitKey( const json& aHit )
{
    return aHit.value( "uuid", "" ) + "\n" + aHit.value( "sheetPath", "" ) + "\n"
           + aHit.value( "kind", "" ) + "\n" + aHit.value( "field", "" );
}

/** A Hit of 7.1 as the page sends it back: those eight keys, the shapes kicadFind answers. */
bool isHit( const json& aHit )
{
    if( !aHit.is_object() || aHit.size() != 8 )
        return false;

    for( const char* key : { "uuid", "sheetPath", "kind", "ref", "field", "text" } )
    {
        if( !aHit.contains( key ) || !aHit[key].is_string()
                || aHit[key].get_ref<const std::string&>().size() > 4096 )
        {
            return false;
        }
    }

    return aHit.contains( "start" ) && aHit["start"].is_number_integer()
           && aHit.contains( "length" ) && aHit["length"].is_number_integer()
           && isLowerUuid( aHit["uuid"].get<std::string>() );
}

/** The replace, on the apply coroutine: every target re-found, ONE commit, the record last. */
void runReplace( SCH_EDIT_FRAME* aFrame, const std::string& aDir, SCH_FIND aFind,
                 const wxString& aWith, bool aAll, const std::set<std::string>& aTargets )
{
    cc_output::EnsureDir( aDir );

    aFind.data.searchAndReplace = true;
    aFind.data.replaceString = aWith;

    bool                     truncated = false;
    std::vector<SCH_HIT_ROW> rows = schFindRows( aFrame, aFind, kMaxHits, truncated );
    std::set<std::string>    found;
    SCH_COMMIT               commit( aFrame );
    int                      replaced = 0;
    bool                     refs = false;

    for( SCH_HIT_ROW& row : rows )
    {
        const std::string key = hitKey( row.hit );

        if( !aAll && !aTargets.count( key ) )
            continue;

        found.insert( key );
        commit.Modify( row.item, row.path.LastScreen(), RECURSE_MODE::NO_RECURSE );

        if( row.item->Replace( aFind.data, &row.path ) )
        {
            aFrame->UpdateItem( row.item, false, true );
            replaced++;

            if( row.item->Type() == SCH_FIELD_T
                    && static_cast<SCH_FIELD*>( row.item )->GetId() == FIELD_T::REFERENCE )
            {
                refs = true;
            }
        }
    }

    int skipped = 0;

    if( !aAll )
    {
        for( const std::string& key : aTargets )
        {
            if( !found.count( key ) )
                skipped++;
        }
    }

    if( replaced > 0 )
    {
        commit.Push( wxS( "Find and Replace" ) );

        if( refs )
            aFrame->GetCurrentSheet().UpdateAllScreenReferences();
    }
    else
    {
        commit.Revert();
    }

    json report = { { "replaced", replaced },
                    { "skipped", skipped },
                    { "depth", pcbjam_collab::testUndoDepth( aFrame ) } };
    cc_output::WriteRecord( aDir, "replace.json", report );

    json done = { { "error", replaced > 0 ? "" : "no_match" }, { "message", "" } };
    cc_output::WriteRecord( aDir, ".cc-replace.json", done );
}
} // namespace sch

/**
 * kicadSchReplace (st-find-e-e, cap replace): {run, dir: "/tmp/cc-replace/<n>", find:
 * FindArgs (sch), with: string (0 to 256), targets: "all" | Hit[] (1 to 4,096)}, those five
 * keys only. Queues on the apply coroutine: every target re-found by KiCad's own matching
 * with searchAndReplace (a hit that no longer matches is skipped and counted), each replaced
 * through its item's Replace (designators only when find.sch.refs is ticked), ONE SCH_COMMIT
 * "Find and Replace" (none when nothing was replaced); writes <dir>/replace.json {replaced,
 * skipped, depth} then <dir>/.cc-replace.json {error, message} last (error "no_match" when
 * nothing was replaced). False for any other shape, no frame or an open in flight.
 */
bool schReplace( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 5 )
        return false;

    for( const char* key : { "run", "dir", "find", "with", "targets" } )
    {
        if( !args.contains( key ) )
            return false;
    }

    if( !args["run"].is_number_integer() || args["run"].get<long long>() < 0
            || !args["dir"].is_string() )
    {
        return false;
    }

    const std::string dir = args["dir"].get<std::string>();

    if( !cc_output::IsRunDir( dir, "/tmp/cc-replace/" ) )
        return false;

    sch::SCH_FIND find;

    if( !sch::readSchFind( args["find"], find ) )
        return false;

    std::optional<wxString> with = boundedText( args["with"], 0, 256 );

    if( !with )
        return false;

    const json&           targets = args["targets"];
    bool                  all = false;
    std::set<std::string> keys;

    if( targets.is_string() && targets.get<std::string>() == "all" )
    {
        all = true;
    }
    else if( targets.is_array() && !targets.empty() && targets.size() <= sch::kMaxHits )
    {
        for( const json& hit : targets )
        {
            if( !sch::isHit( hit ) )
                return false;

            keys.insert( sch::hitKey( hit ) );
        }
    }
    else
    {
        return false;
    }

    const wxString replacement = *with;

    pcbjam_collab::runOnCoroutine( fr, [fr, dir, find, replacement, all, keys]() {
        sch::runReplace( fr, dir, find, replacement, all, keys );
    } );

    return true;
}
} // namespace cc_e10

EMSCRIPTEN_BINDINGS( cc_e10_sch )
{
    function( "kicadSchReplace", &cc_e10::schReplace );
}
