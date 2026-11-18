// Circuit Center (engine run 14, island cc23): properties, numbering and rules, the
// schematic engine's side. Spec: circuits-com docs/superpowers/specs/2026-11-16-properties-
// rules-design.md, section 5 and the table "Engine exports for run 14".
//
// Textually included by eeschema_embind.cpp just before its EMSCRIPTEN_BINDINGS block (a
// separate file so four authors of run 14 do not edit the same lines): everything here
// sees that TU's includes and its anonymous namespace helpers (schFrame, toUtf8,
// isLowerUuid, boundedText, isRunDir, isFootprintId, fieldNamed, fieldNameTaken, unitsOf,
// sheetNameOf, readSymbolSet, SYMBOL_SET, FIELD_EDIT, writeRunDone, ensureRunDir,
// RUN_LINES_REPORTER).
//
// House rules (run A 3.4): closed JSON args read whole before anything is queued; a refusal
// at once with no frame or while an open is in flight; every WRITE answers bool (false
// queues nothing), runs on the apply coroutine, writes <dir>/<name>.json and then
// <dir>/.cc-<name>.json LAST, dir under /tmp/cc-<name>/; every READ is synchronous and
// answers JSON or "". One commit and one undo step per apply; a preview pushes nothing; a
// settings set pushes no undo step (KiCad's own rule). No KiCad dialog on any road.
//
// Modified by Circuit Center Inc on 2026-11-18: engine run 15's section registry (R7), the
// board registry's twin. A settings section an author adds is registered from that author's own
// inc (ccRegisterSchSection), consulted after the built-in sections by the get, the set
// (before Schematic Setup's OK steps, ccSchSettingsChanged) and the list (ccSchSections,
// schSettingsSections for the export kicadSettingsSections).

#include <bus_alias.h>
#include <font/font.h>
#include <project/net_settings.h>
#include <sch_pin.h>
#include <tool/tools_holder.h>
#include <project/project_file.h>
#include <refdes_utils.h>
#include <sch_connection.h>
#include <sch_render_settings.h>
#include <sch_sheet_pin.h>
#include <sch_textbox.h>
#include <string_utils.h>
#include <title_block.h>
#include <page_info.h>
#include <settings/nested_settings.h>
#include <settings/json_settings_internals.h>

#include <cctype>
#include <functional>

namespace
{
constexpr size_t CC_RULES_ROWS_MAX = 4096;
constexpr size_t CC_RULES_FIELDS_MAX = 256;
constexpr size_t CC_RULES_TEXT_MAX = 1024;

/** A string of the records: KiCad's text cut to aMax bytes, never inside a UTF-8 sequence. */
std::string ccRulesCut( const wxString& aText, size_t aMax = CC_RULES_TEXT_MAX )
{
    std::string s = toUtf8( aText );

    if( s.size() > aMax )
    {
        s.resize( aMax );

        while( !s.empty() && ( static_cast<unsigned char>( s.back() ) & 0xC0 ) == 0x80 )
            s.pop_back();

        if( !s.empty() && ( static_cast<unsigned char>( s.back() ) & 0x80 ) )
            s.pop_back();
    }

    return s;
}

double ccSchMm( int aIu )
{
    return std::round( schIUScale.IUTomm( aIu ) * 10000.0 ) / 10000.0;
}

/** One placed symbol on one sheet path (a reused sheet shares the SCH_SYMBOL). */
struct CC_INSTANCE
{
    SCH_SYMBOL*    symbol = nullptr;
    SCH_SHEET_PATH path;
};

std::string ccRulesInstanceKey( const wxString& aSheetPath, const wxString& aUuid )
{
    return toUtf8( aSheetPath.Lower() ) + "|" + toUtf8( aUuid.Lower() );
}

/** Every symbol instance in hierarchy order (power symbols included), with an index by key. */
void ccRulesInstances( SCH_EDIT_FRAME* aFrame, std::vector<CC_INSTANCE>& aOrdered,
                  std::map<std::string, size_t>& aIndex )
{
    aOrdered.clear();
    aIndex.clear();

    for( const SCH_SHEET_PATH& path : aFrame->Schematic().Hierarchy() )
    {
        SCH_SCREEN* screen = path.LastScreen();

        if( !screen )
            continue;

        const wxString sheetPath = path.PathAsString();

        for( SCH_ITEM* item : screen->Items().OfType( SCH_SYMBOL_T ) )
        {
            CC_INSTANCE inst;
            inst.symbol = static_cast<SCH_SYMBOL*>( item );
            inst.path = path;
            aIndex[ccRulesInstanceKey( sheetPath, item->m_Uuid.AsString() )] = aOrdered.size();
            aOrdered.push_back( std::move( inst ) );
        }
    }
}

/** The variant name read from args: "" (the default) or a name the design holds. */
std::optional<wxString> ccReadVariant( SCH_EDIT_FRAME* aFrame, const json& aValue )
{
    std::optional<wxString> name = boundedText( aValue, 0, 128 );

    if( !name )
        return std::nullopt;

    if( !name->IsEmpty() && !aFrame->Schematic().GetVariantNames().count( *name ) )
        return std::nullopt;

    return name;
}

json ccVariantNames( SCH_EDIT_FRAME* aFrame )
{
    json names = json::array();

    for( const wxString& name : aFrame->Schematic().GetVariantNames() )
        names.push_back( ccRulesCut( name, 128 ) );

    return names;
}

/** 7.1's SymbolRow for one instance under aVariant ("" the default). */
json ccSymbolRow( SCH_EDIT_FRAME* aFrame, SCH_SYMBOL* aSymbol, const SCH_SHEET_PATH& aPath,
                  const wxString& aVariant )
{
    SCHEMATIC& sch = aFrame->Schematic();
    json       row = json::object();

    row["uuid"] = toUtf8( aSymbol->m_Uuid.AsString() );
    row["sheetPath"] = toUtf8( aPath.PathAsString() );
    row["sheetName"] = ccRulesCut( sheetNameOf( aPath ) );
    row["ref"] = ccRulesCut( aSymbol->GetRef( &aPath, false ), 64 );
    row["unit"] = std::max( 1, aSymbol->GetUnitSelection( &aPath ) );
    row["units"] = unitsOf( aSymbol );
    row["libId"] = ccRulesCut( wxString::FromUTF8( aSymbol->GetLibId().Format().c_str() ) );
    row["value"] = ccRulesCut( aSymbol->GetValue( false, &aPath, false, aVariant ) );

    SCH_FIELD* fpField = aSymbol->GetField( FIELD_T::FOOTPRINT );
    row["footprint"] = fpField ? ccRulesCut( aSymbol->GetFieldText( fpField->GetName(), &aPath, aVariant ) )
                               : std::string();

    std::vector<SCH_FIELD*> ordered;
    aSymbol->GetFields( ordered, /*aVisibleOnly*/ false );

    json fields = json::array();

    for( SCH_FIELD* field : ordered )
    {
        if( field->GetId() == FIELD_T::REFERENCE || field->GetId() == FIELD_T::VALUE
                || field->GetId() == FIELD_T::FOOTPRINT || field->IsPrivate() )
        {
            continue;
        }

        if( fields.size() >= CC_RULES_FIELDS_MAX )
            break;

        json f = json::object();
        f["name"] = ccRulesCut( field->GetCanonicalName(), 128 );
        f["value"] = ccRulesCut( sch.ConvertKIIDsToRefs(
                aSymbol->GetFieldText( field->GetName(), &aPath, aVariant ) ) );
        f["shown"] = field->IsVisible();
        fields.push_back( std::move( f ) );
    }

    row["fields"] = std::move( fields );

    // Resolved: the symbol's own flag for the variant, a rule area's, and the enclosing
    // sheets' (the fields table's rule, fields_data_model.cpp); KiCad keeps no sheet flag
    // for the placement files.
    const bool ownBom = aSymbol->GetExcludedFromBOM( &aPath, aVariant );
    const bool ownBoard = aSymbol->GetExcludedFromBoard( &aPath, aVariant );
    const bool ownSim = aSymbol->GetExcludedFromSim( &aPath, aVariant );
    const bool ownPos = aSymbol->GetExcludedFromPosFiles( &aPath, aVariant );
    const bool ownDnp = aSymbol->GetDNP( &aPath, aVariant );

    const bool exBom = aSymbol->ResolveExcludedFromBOM( &aPath, aVariant )
                       || aPath.GetExcludedFromBOM( aVariant );
    const bool exBoard = aSymbol->ResolveExcludedFromBoard( &aPath, aVariant )
                         || aPath.GetExcludedFromBoard( aVariant );
    const bool exSim = aSymbol->ResolveExcludedFromSim( &aPath, aVariant )
                       || aPath.GetExcludedFromSim( aVariant );
    const bool exPos = aSymbol->ResolveExcludedFromPosFiles( &aPath, aVariant );
    const bool dnp = aSymbol->ResolveDNP( &aPath, aVariant ) || aPath.GetDNP( aVariant );

    row["inBom"] = !exBom;
    row["onBoard"] = !exBoard;
    row["inSim"] = !exSim;
    row["inPos"] = !exPos;
    row["dnp"] = dnp;

    json held = json::array();

    if( exBom && !ownBom )
        held.push_back( "inBom" );

    if( exBoard && !ownBoard )
        held.push_back( "onBoard" );

    if( exSim && !ownSim )
        held.push_back( "inSim" );

    if( exPos && !ownPos )
        held.push_back( "inPos" );

    if( dnp && !ownDnp )
        held.push_back( "dnp" );

    row["sheetHeld"] = std::move( held );

    // What the variant changes, by the file writer's own test (sch_io_kicad_sexpr.cpp: a
    // flag differing from the symbol's default, every field the variant names).
    json overrides = json::array();

    if( !aVariant.IsEmpty() )
    {
        if( std::optional<SCH_SYMBOL_VARIANT> v = aSymbol->GetVariant( aPath, aVariant ) )
        {
            const SCH_FIELD* valueField = aSymbol->GetField( FIELD_T::VALUE );

            for( const auto& [name, text] : v->m_Fields )
            {
                if( valueField && name == valueField->GetName() )
                    overrides.push_back( "value" );
                else if( fpField && name == fpField->GetName() )
                    overrides.push_back( "footprint" );
                else
                    overrides.push_back( ccRulesCut( name, 128 ) );
            }

            if( v->m_ExcludedFromBOM != aSymbol->GetExcludedFromBOM() )
                overrides.push_back( "inBom" );

            if( v->m_ExcludedFromBoard != aSymbol->GetExcludedFromBoard() )
                overrides.push_back( "onBoard" );

            if( v->m_ExcludedFromSim != aSymbol->GetExcludedFromSim() )
                overrides.push_back( "inSim" );

            if( v->m_ExcludedFromPosFiles != aSymbol->GetExcludedFromPosFiles() )
                overrides.push_back( "inPos" );

            if( v->m_DNP != aSymbol->GetDNP() )
                overrides.push_back( "dnp" );
        }
    }

    row["overrides"] = std::move( overrides );

    // The footprint filters of the copy the schematic holds (no library read), and the
    // number of distinct pin numbers across every unit (what a footprint's pads match).
    json filters = json::array();
    std::set<wxString> pinNumbers;

    if( const std::unique_ptr<LIB_SYMBOL>& lib = aSymbol->GetLibSymbolRef() )
    {
        for( const wxString& filter : lib->GetFPFilters() )
        {
            if( filters.size() >= CC_RULES_FIELDS_MAX )
                break;

            filters.push_back( ccRulesCut( filter, 256 ) );
        }
    }

    for( SCH_PIN* pin : aSymbol->GetAllLibPins() )
    {
        if( pin && !pin->GetNumber().IsEmpty() )
            pinNumbers.insert( pin->GetNumber() );
    }

    row["filters"] = std::move( filters );
    row["pins"] = static_cast<int>( pinNumbers.size() );
    return row;
}

// ───────────────────────────── symbols.set ─────────────────────────────

/** One instance's change of a symbols.set, read whole before anything is queued. */
struct CC_SYMBOLS_CHANGE
{
    std::string              key;        // ccRulesInstanceKey
    std::string              uuid;
    std::string              sheetPath;
    SYMBOL_SET               set;        // ref, value, footprint, fields, addField, removeField, flags
    bool                     hasSet = false;
    std::optional<bool>      inPos;
    std::vector<std::string> clear;      // 'value', 'footprint', a field name or a flag key
};

bool ccIsFlagKey( const std::string& aKey )
{
    return aKey == "inBom" || aKey == "onBoard" || aKey == "inSim" || aKey == "inPos" || aKey == "dnp";
}

/** 7.1's SymbolsSet against aSymbol under aVariant; false for anything outside it. */
bool ccReadSymbolsSet( const json& aSet, SCH_SYMBOL* aSymbol, const wxString& aVariant,
                       CC_SYMBOLS_CHANGE& aOut )
{
    static const std::set<std::string> allowed = { "ref", "value", "footprint", "fields",
                                                   "addField", "removeField", "inBom", "onBoard",
                                                   "inSim", "inPos", "dnp", "clear" };

    if( !aSet.is_object() || aSet.empty() )
        return false;

    json rest = json::object();

    for( const auto& [key, value] : aSet.items() )
    {
        if( !allowed.count( key ) )
            return false;

        if( key == "inPos" )
        {
            if( !value.is_boolean() )
                return false;

            aOut.inPos = value.get<bool>();
        }
        else if( key == "clear" )
        {
            if( aVariant.IsEmpty() || !value.is_array() || value.empty() || value.size() > 64 )
                return false;

            std::set<std::string> seen;

            for( const json& entry : value )
            {
                if( !entry.is_string() )
                    return false;

                const std::string name = entry.get<std::string>();

                if( !seen.insert( name ).second )
                    return false;

                if( name == "value" || name == "footprint" || ccIsFlagKey( name ) )
                {
                    aOut.clear.push_back( name );
                    continue;
                }

                std::optional<wxString> fieldName = boundedText( entry, 1, 128 );

                if( !fieldName )
                    return false;

                SCH_FIELD* field = fieldNamed( aSymbol, *fieldName );

                if( !field || field->GetId() == FIELD_T::REFERENCE )
                    return false;

                aOut.clear.push_back( name );
            }
        }
        else
        {
            // Under a variant the designator, the field list and a field's visibility are not
            // the variant's: KiCad's variants hold values and flags only (sch_sheet_path.h).
            if( !aVariant.IsEmpty() && ( key == "ref" || key == "addField" || key == "removeField" ) )
                return false;

            if( !aVariant.IsEmpty() && key == "fields" && value.is_array() )
            {
                for( const json& entry : value )
                {
                    if( entry.is_object() && entry.contains( "shown" ) )
                        return false;
                }
            }

            rest[key] = value;
        }
    }

    if( !rest.empty() )
    {
        if( !readSymbolSet( rest, aSymbol, aOut.set ) )
            return false;

        aOut.hasSet = true;
    }

    // A thing set and cleared in the same change is ambiguous.
    for( const std::string& name : aOut.clear )
    {
        if( ( name == "value" && aOut.set.value ) || ( name == "footprint" && aOut.set.footprint )
                || ( name == "inBom" && aOut.set.inBom ) || ( name == "onBoard" && aOut.set.onBoard )
                || ( name == "inSim" && aOut.set.inSim ) || ( name == "dnp" && aOut.set.dnp )
                || ( name == "inPos" && aOut.inPos ) )
        {
            return false;
        }

        for( const FIELD_EDIT& edit : aOut.set.fields )
        {
            if( toUtf8( edit.name ) == name )
                return false;
        }
    }

    return true;
}

/** One change into aCommit (no push), under aVariant; true when anything was asked of it. */
void ccApplySymbolsChange( SCH_EDIT_FRAME* aFrame, SCH_COMMIT& aCommit, SCH_SYMBOL* aSymbol,
                           const SCH_SHEET_PATH& aPath, const CC_SYMBOLS_CHANGE& aChange,
                           const wxString& aVariant )
{
    SCHEMATIC&        sch = aFrame->Schematic();
    SCH_SCREEN*       screen = aPath.LastScreen();
    const SYMBOL_SET& set = aChange.set;
    bool              shared = false;   // a change the other units of the part must share

    aCommit.Modify( aSymbol, screen );

    // Overrides dropped first (a variant only): the variant copied, the named entries
    // taken out or set back to the symbol's default, and put back in place.
    if( !aChange.clear.empty() && !aVariant.IsEmpty() )
    {
        if( std::optional<SCH_SYMBOL_VARIANT> v = aSymbol->GetVariant( aPath, aVariant ) )
        {
            SCH_SYMBOL_VARIANT copy = *v;

            for( const std::string& name : aChange.clear )
            {
                if( name == "value" )
                    copy.m_Fields.erase( aSymbol->GetField( FIELD_T::VALUE )->GetName() );
                else if( name == "footprint" )
                    copy.m_Fields.erase( aSymbol->GetField( FIELD_T::FOOTPRINT )->GetName() );
                else if( name == "inBom" )
                    copy.m_ExcludedFromBOM = aSymbol->GetExcludedFromBOM();
                else if( name == "onBoard" )
                    copy.m_ExcludedFromBoard = aSymbol->GetExcludedFromBoard();
                else if( name == "inSim" )
                    copy.m_ExcludedFromSim = aSymbol->GetExcludedFromSim();
                else if( name == "inPos" )
                    copy.m_ExcludedFromPosFiles = aSymbol->GetExcludedFromPosFiles();
                else if( name == "dnp" )
                    copy.m_DNP = aSymbol->GetDNP();
                else if( SCH_FIELD* field = fieldNamed( aSymbol, wxString::FromUTF8( name.c_str() ) ) )
                    copy.m_Fields.erase( field->GetName() );
            }

            aSymbol->DeleteVariant( aPath, aVariant );
            aSymbol->AddVariant( aPath, copy );
            shared = true;
        }
    }

    auto setFieldValue =
            [&]( SCH_FIELD* aField, const wxString& aText )
            {
                if( !aField )
                    return;

                if( aField->GetId() == FIELD_T::REFERENCE )
                {
                    aSymbol->SetRef( &aPath, aText );
                    return;
                }

                if( !aVariant.IsEmpty() )
                {
                    // The variant keeps the text only when it differs from the default
                    // (SCH_SYMBOL::SetFieldText drops an override equal to it).
                    aSymbol->SetFieldText( aField->GetName(), sch.ConvertRefsToKIIDs( aText ), &aPath,
                                           aVariant );
                }
                else if( aField->GetId() == FIELD_T::VALUE )
                {
                    aSymbol->SetValueFieldText( aText, &aPath, wxEmptyString );
                }
                else if( aField->GetId() == FIELD_T::FOOTPRINT )
                {
                    aSymbol->SetFootprintFieldText( aText );
                }
                else
                {
                    aField->SetText( sch.ConvertRefsToKIIDs( aText ) );
                }

                shared = true;
            };

    if( set.ref )
        aSymbol->SetRef( &aPath, *set.ref );

    if( set.value )
        setFieldValue( aSymbol->GetField( FIELD_T::VALUE ), *set.value );

    if( set.footprint )
        setFieldValue( aSymbol->GetField( FIELD_T::FOOTPRINT ), *set.footprint );

    for( const FIELD_EDIT& edit : set.fields )
    {
        SCH_FIELD* field = fieldNamed( aSymbol, edit.name );

        if( !field )
            continue;

        if( edit.value )
            setFieldValue( field, *edit.value );

        if( edit.shown && aVariant.IsEmpty() )
        {
            field->SetVisible( *edit.shown );
            shared = shared || field->GetId() != FIELD_T::REFERENCE;
        }
    }

    if( set.removeField )
    {
        aSymbol->RemoveField( *set.removeField );
        shared = true;
    }

    if( set.addField && !fieldNameTaken( aSymbol, set.addField->name ) )
    {
        // As the fields table adds a column cell: the symbol's anchor, the reference's
        // angle, the schematic's default text size, the next user ordinal.
        SCH_FIELD        added( aSymbol, FIELD_T::USER, set.addField->name );
        const int        size = sch.Settings().m_DefaultTextSize;
        const SCH_FIELD* reference = aSymbol->GetField( FIELD_T::REFERENCE );

        added.SetText( sch.ConvertRefsToKIIDs( *set.addField->value ) );
        added.SetVisible( *set.addField->shown );
        added.SetTextSize( VECTOR2I( size, size ) );

        if( reference )
            added.SetTextAngle( reference->GetTextAngle() );

        added.SetPosition( aSymbol->GetPosition() );
        added.SetOrdinal( aSymbol->GetNextFieldOrdinal() );

        if( SCH_FIELD* placed = aSymbol->AddField( added ) )
            placed->SetParent( aSymbol );

        shared = true;
    }

    // The flags: under a variant every setter takes the variant's name (the fields table's
    // own way, fields_data_model.cpp setAttributeValue).
    if( set.inBom )
    {
        aSymbol->SetExcludedFromBOM( !*set.inBom, &aPath, aVariant );
        shared = true;
    }

    if( set.onBoard )
    {
        aSymbol->SetExcludedFromBoard( !*set.onBoard, &aPath, aVariant );
        shared = true;
    }

    if( set.inSim )
    {
        aSymbol->SetExcludedFromSim( !*set.inSim, &aPath, aVariant );
        shared = true;
    }

    if( aChange.inPos )
    {
        aSymbol->SetExcludedFromPosFiles( !*aChange.inPos, &aPath, aVariant );
        shared = true;
    }

    if( set.dnp )
    {
        aSymbol->SetDNP( *set.dnp, &aPath, aVariant );
        shared = true;
    }

    // Every unit of a multi-unit part in step (fields bar the reference, the flags), same commit.
    if( shared && aSymbol->IsMultiUnit() )
        aSymbol->SyncOtherUnits( aPath, aCommit, nullptr, aVariant );
}

void ccRunSymbolsSet( const std::string& aDir, const wxString& aVariant,
                      const std::vector<CC_SYMBOLS_CHANGE>& aChanges )
{
    std::string error;
    std::string message;
    int         applied = 0;
    int         depth = -1;

    ensureRunDir( aDir );

    SCH_EDIT_FRAME* fr = schFrame();   // re-resolved on the coroutine (S4)

    if( !fr )
    {
        error = "island_error";
        message = "no schematic editor";
    }
    else
    {
        try
        {
            std::vector<CC_INSTANCE>      ordered;
            std::map<std::string, size_t> index;
            ccRulesInstances( fr, ordered, index );

            const bool variantLive = aVariant.IsEmpty()
                                     || fr->Schematic().GetVariantNames().count( aVariant );
            SCH_COMMIT commit( fr );

            if( variantLive )
            {
                for( const CC_SYMBOLS_CHANGE& change : aChanges )
                {
                    auto it = index.find( change.key );

                    if( it == index.end() )
                        continue;

                    const CC_INSTANCE& inst = ordered[it->second];
                    ccApplySymbolsChange( fr, commit, inst.symbol, inst.path, change, aVariant );
                    applied++;
                }
            }
            else
            {
                error = "no_variant";
                message = "the design has no such variant";
            }

            if( applied > 0 )
            {
                commit.Push( wxT( "Edit fields" ) );
                fr->GetCurrentSheet().UpdateAllScreenReferences();
                fr->GetCanvas()->Refresh();
            }
            else
            {
                commit.Revert();
            }

            depth = pcbjam_collab::testUndoDepth( fr );
        }
        catch( const std::exception& e )
        {
            error = "island_error";
            message = e.what();
        }
        catch( ... )
        {
            error = "island_error";
            message = "the fields were not set";
        }
    }

    json record = json::object();
    record["applied"] = applied;
    record["depth"] = depth;
    writeRunDone( aDir, "symbols.json", record );

    json done = json::object();
    done["error"] = error;
    done["message"] = message;
    writeRunDone( aDir, ".cc-symbols.json", done );
}

// ───────────────────────────── symbols.exchange ─────────────────────────────

struct CC_EXCHANGE_OPTIONS
{
    bool removeExtra = false;
    bool resetEmpty = false;
    bool resetText = false;
    bool resetVisibility = false;
    bool resetEffects = false;
    bool resetPositions = false;
    bool resetAttributes = false;
    bool resetPins = false;              // alternate pin assignments
    bool resetPinTextVisibility = false; // KiCad's "Reset pin name/number visibility"
    bool resetCustomPower = false;       // KiCad's "Reset custom power symbols"
};

struct CC_SCH_EXCHANGE_ARGS
{
    std::string              dir;
    bool                     change = false;   // mode "change", else "update"
    std::vector<std::string> targets;          // instance keys
    wxString                 libId;            // "" for update
    bool                     allFields = true;
    std::set<wxString>       fields;           // canonical names when not all
    CC_EXCHANGE_OPTIONS      options;
    bool                     preview = false;
};

bool ccReadExchangeOptions( const json& aValue, CC_EXCHANGE_OPTIONS& aOut )
{
    if( !aValue.is_object() )
        return false;

    for( const auto& [key, value] : aValue.items() )
    {
        if( !value.is_boolean() )
            return false;

        const bool on = value.get<bool>();

        if( key == "removeExtra" )
            aOut.removeExtra = on;
        else if( key == "resetEmpty" )
            aOut.resetEmpty = on;
        else if( key == "resetText" )
            aOut.resetText = on;
        else if( key == "resetVisibility" )
            aOut.resetVisibility = on;
        else if( key == "resetEffects" )
            aOut.resetEffects = on;
        else if( key == "resetPositions" )
            aOut.resetPositions = on;
        else if( key == "resetAttributes" )
            aOut.resetAttributes = on;
        else if( key == "resetPins" )
            aOut.resetPins = on;
        else if( key == "resetPinTextVisibility" )
            aOut.resetPinTextVisibility = on;
        else if( key == "resetCustomPower" )
            aOut.resetCustomPower = on;
        else
            return false;
    }

    return true;
}

/**
 * DIALOG_CHANGE_SYMBOLS::processSymbols for one symbol, its checkboxes as aOptions (KiCad
 * 10.0, dialog_change_symbols.cpp), the message panel replaced by the record's rows. The
 * symbol has already been taken off its screen and recorded in the commit.
 */
void ccExchangeOne( SCH_EDIT_FRAME* aFrame, SCH_SYMBOL* aSymbol, LIB_SYMBOL* aLibSymbol,
                    const LIB_ID& aNewId, const std::vector<SCH_SHEET_PATH>& aInstances,
                    const CC_SCH_EXCHANGE_ARGS& aArgs )
{
    const CC_EXCHANGE_OPTIONS& o = aArgs.options;
    SCH_SCREEN*                screen = aInstances.front().LastScreen();

    if( aNewId != aSymbol->GetLibId() )
        aSymbol->SetLibId( aNewId );

    std::unique_ptr<LIB_SYMBOL> flattened = aLibSymbol->Flatten();
    aSymbol->SetLibSymbol( flattened.release() );

    if( o.resetAttributes )
    {
        aSymbol->SetExcludedFromSim( aSymbol->GetLibSymbolRef()->GetExcludedFromSim() );
        aSymbol->SetExcludedFromBOM( aSymbol->GetLibSymbolRef()->GetExcludedFromBOM() );
        aSymbol->SetExcludedFromBoard( aSymbol->GetLibSymbolRef()->GetExcludedFromBoard() );
        aSymbol->SetExcludedFromPosFiles( aSymbol->GetLibSymbolRef()->GetExcludedFromPosFiles() );
    }

    if( o.resetPinTextVisibility )
    {
        aSymbol->SetShowPinNames( aSymbol->GetLibSymbolRef()->GetShowPinNames() );
        aSymbol->SetShowPinNumbers( aSymbol->GetLibSymbolRef()->GetShowPinNumbers() );
    }

    auto updates =
            [&]( const wxString& aCanonicalName ) -> bool
            {
                return aArgs.allFields || aArgs.fields.count( aCanonicalName ) > 0;
            };

    for( int ii = (int) aSymbol->GetFields().size() - 1; ii >= 0; ii-- )
    {
        SCH_FIELD& field = aSymbol->GetFields()[ii];
        SCH_FIELD* libField = nullptr;
        bool       doUpdate = field.IsPrivate() || updates( field.GetCanonicalName() );

        if( !doUpdate )
            continue;

        if( field.IsMandatory() )
            libField = aSymbol->GetLibSymbolRef()->GetField( field.GetId() );
        else
            libField = aSymbol->GetLibSymbolRef()->GetField( field.GetName() );

        if( libField )
        {
            field.SetPrivate( libField->IsPrivate() );

            const bool resetText = libField->GetText().IsEmpty() ? o.resetEmpty : o.resetText;

            if( resetText )
            {
                if( field.GetId() == FIELD_T::REFERENCE )
                {
                    wxString prefix = UTIL::GetRefDesPrefix( libField->GetText() );

                    for( const SCH_SHEET_PATH& instance : aInstances )
                    {
                        wxString ref = aSymbol->GetRef( &instance );
                        int      number = UTIL::GetRefDesNumber( ref );

                        if( number >= 0 )
                            ref.Printf( wxS( "%s%d" ), prefix, number );
                        else
                            ref = UTIL::GetRefDesUnannotated( prefix );

                        aSymbol->SetRef( &instance, ref );
                    }
                }
                else if( field.GetId() == FIELD_T::VALUE )
                {
                    if( !aSymbol->IsPower() || o.resetCustomPower )
                        aSymbol->SetValueFieldText( UnescapeString( libField->GetText() ) );
                }
                else
                {
                    field.SetText( libField->GetText() );
                }
            }

            if( o.resetVisibility )
                field.SetVisible( libField->IsVisible() );

            if( o.resetEffects )
            {
                bool     visible = field.IsVisible();
                VECTOR2I pos = field.GetPosition();

                field.SetAttributes( *libField );
                field.SetVisible( visible );
                field.SetPosition( pos );
                field.SetNameShown( libField->IsNameShown() );
                field.SetCanAutoplace( libField->CanAutoplace() );
            }

            if( o.resetPositions )
                field.SetTextPos( aSymbol->GetPosition() + libField->GetTextPos() );
        }
        else if( !field.IsMandatory() && o.removeExtra )
        {
            aSymbol->RemoveField( field.GetName() );
        }
    }

    std::vector<SCH_FIELD*> libFields;
    aSymbol->GetLibSymbolRef()->GetFields( libFields );

    for( SCH_FIELD* libField : libFields )
    {
        if( libField->IsMandatory() || !updates( libField->GetCanonicalName() ) )
            continue;

        if( !aSymbol->GetField( libField->GetName() ) )
        {
            SCH_FIELD* schField =
                    aSymbol->AddField( SCH_FIELD( aSymbol, FIELD_T::USER, libField->GetName() ) );

            schField->SetAttributes( *libField );
            schField->SetVisible( libField->IsVisible() );
            schField->SetText( libField->GetText() );
            schField->SetTextPos( aSymbol->GetPosition() + libField->GetTextPos() );
            schField->SetPrivate( libField->IsPrivate() );
        }
    }

    aSymbol->SetSchSymbolLibraryName( wxEmptyString );
    screen->Append( aSymbol );

    if( o.resetPositions && aFrame->eeconfig() && aFrame->eeconfig()->m_AutoplaceFields.enable )
    {
        AUTOPLACE_ALGO placed = aSymbol->GetFieldsAutoplaced();

        if( placed == AUTOPLACE_AUTO || placed == AUTOPLACE_MANUAL )
            aSymbol->AutoplaceFields( screen, placed );
    }

    for( const SCH_SHEET_PATH& instance : aInstances )
    {
        for( SCH_PIN* pin : aSymbol->GetPins( &instance ) )
        {
            if( !pin->GetAlt().IsEmpty()
                    && ( o.resetPins || pin->GetAlt() == pin->GetBaseName()
                         || pin->GetAlternates().count( pin->GetAlt() ) == 0 ) )
            {
                pin->SetAlt( wxEmptyString );
            }
        }
    }

    aFrame->GetCanvas()->GetView()->Update( aSymbol );
}

void ccRunSchExchange( const CC_SCH_EXCHANGE_ARGS& aArgs )
{
    std::string error;
    std::string message;
    json        rows = json::array();
    int         applied = 0;
    bool        truncated = false;
    int         depth = -1;

    ensureRunDir( aArgs.dir );

    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
    {
        error = "island_error";
        message = "no schematic editor";
    }
    else
    {
        try
        {
            std::vector<CC_INSTANCE>      ordered;
            std::map<std::string, size_t> index;
            ccRulesInstances( fr, ordered, index );

            // The targets grouped by symbol (a reused sheet's instances share one SCH_SYMBOL),
            // in the order asked.
            std::vector<SCH_SYMBOL*>                             symbols;
            std::map<SCH_SYMBOL*, std::vector<SCH_SHEET_PATH>>   instances;

            for( const std::string& key : aArgs.targets )
            {
                auto it = index.find( key );

                if( it == index.end() )
                    continue;

                const CC_INSTANCE& inst = ordered[it->second];

                if( !instances.count( inst.symbol ) )
                    symbols.push_back( inst.symbol );

                instances[inst.symbol].push_back( inst.path );
            }

            std::map<SCH_SYMBOL*, LIB_SYMBOL*> sources;
            std::map<SCH_SYMBOL*, LIB_ID>      newIds;

            for( SCH_SYMBOL* symbol : symbols )
            {
                LIB_ID id = symbol->GetLibId();

                if( aArgs.change )
                    id.Parse( UTF8( aArgs.libId ) );

                const std::string from = toUtf8( UnescapeString( symbol->GetLibId().Format() ) );
                const std::string to = toUtf8( UnescapeString( id.Format() ) );
                std::string       status = "changed";
                LIB_SYMBOL*       lib = id.IsValid() ? fr->GetLibSymbol( id ) : nullptr;

                if( !lib )
                {
                    status = "missing";
                }
                else
                {
                    std::unique_ptr<LIB_SYMBOL> flat = lib->Flatten();

                    if( flat->GetUnitCount() < symbol->GetUnit() )
                    {
                        status = "missing";
                    }
                    else if( id == symbol->GetLibId() && symbol->GetLibSymbolRef()
                             && flat->Compare( *symbol->GetLibSymbolRef(),
                                               SCH_ITEM::COMPARE_FLAGS::EQUALITY
                                                       | SCH_ITEM::COMPARE_FLAGS::ERC ) == 0 )
                    {
                        status = "same";   // still processed: the reset options may change it
                    }
                }

                if( status != "missing" )
                {
                    sources[symbol] = lib;
                    newIds[symbol] = id;
                    applied++;
                }

                for( const SCH_SHEET_PATH& path : instances[symbol] )
                {
                    if( rows.size() >= CC_RULES_ROWS_MAX )
                    {
                        truncated = true;
                        break;
                    }

                    json row = json::object();
                    row["uuid"] = toUtf8( symbol->m_Uuid.AsString() );
                    row["sheetPath"] = toUtf8( path.PathAsString() );
                    row["ref"] = ccRulesCut( symbol->GetRef( &path, false ), 64 );
                    row["from"] = ccRulesCut( wxString::FromUTF8( from.c_str() ) );
                    row["to"] = ccRulesCut( wxString::FromUTF8( to.c_str() ) );
                    row["status"] = status;
                    rows.push_back( std::move( row ) );
                }
            }

            if( !aArgs.preview && !sources.empty() )
            {
                SCH_COMMIT commit( fr );

                // Taken off the screen and recorded before the LIB_SYMBOL changes (the
                // dialog's order: no stale library symbol left in the file).
                for( SCH_SYMBOL* symbol : symbols )
                {
                    if( !sources.count( symbol ) )
                        continue;

                    SCH_SCREEN* screen = instances[symbol].front().LastScreen();
                    screen->Remove( symbol );
                    commit.Modified( symbol, static_cast<SCH_SYMBOL*>( symbol->Clone() ), screen );
                }

                for( SCH_SYMBOL* symbol : symbols )
                {
                    if( sources.count( symbol ) )
                        ccExchangeOne( fr, symbol, sources[symbol], newIds[symbol], instances[symbol], aArgs );
                }

                fr->GetCurrentSheet().UpdateAllScreenReferences();
                commit.Push( aArgs.change ? wxT( "Change symbols" ) : wxT( "Update symbols" ) );
                fr->GetCanvas()->Refresh();
            }

            depth = pcbjam_collab::testUndoDepth( fr );
        }
        catch( const std::exception& e )
        {
            error = "island_error";
            message = e.what();
        }
        catch( ... )
        {
            error = "island_error";
            message = "the symbols were not exchanged";
        }
    }

    json report = json::object();
    report["preview"] = aArgs.preview;
    report["rows"] = std::move( rows );
    report["applied"] = applied;
    report["truncated"] = truncated;
    report["depth"] = depth;
    writeRunDone( aArgs.dir, "exchange.json", report );

    json done = json::object();
    done["error"] = error;
    done["message"] = message;
    writeRunDone( aArgs.dir, ".cc-exchange.json", done );
}

// ───────────────────────────── sch.edit textGraphics ─────────────────────────────

/** 7.5's SchTextFilter, with KiCad's other filters as optional extras (BUILD-NOTES-rules.md). */
struct CC_SCH_TEXT_FILTER
{
    bool references = false, values = false, otherFields = false, labels = false;
    bool sheetTitles = false, sheetPins = false, texts = false, wires = false, buses = false;
    bool graphics = false;
    // KiCad's other targets and filters.
    bool sheetFields = false, sheetBorders = false, labelFields = false;
    std::optional<wxString> sheetPath, refGlob, symbolGlob, fieldGlob, netGlob;
    std::optional<bool>     power;      // true: power symbols only, false: other symbols only
    bool                    selected = false;
};

struct CC_SCH_TEXT_SET
{
    std::optional<int>                size;        // IU
    std::optional<bool>               bold, italic, visible, showFieldNames;
    std::optional<int>                lineWidth;   // IU
    std::optional<GR_TEXT_H_ALIGN_T>  hAlign;
    std::optional<GR_TEXT_V_ALIGN_T>  vAlign;
    std::optional<LINE_STYLE>         lineStyle;
    std::optional<int>                junctionSize;   // IU
};

std::optional<int> ccSchMmToIu( const json& aValue, double aMin, double aMax )
{
    if( !aValue.is_number() )
        return std::nullopt;

    const double mm = aValue.get<double>();

    if( !std::isfinite( mm ) || mm < aMin || mm > aMax )
        return std::nullopt;

    return KiROUND( schIUScale.mmToIU( mm ) );
}

bool ccReadSchTextFilter( const json& aValue, CC_SCH_TEXT_FILTER& aOut )
{
    if( !aValue.is_object() || aValue.empty() )
        return false;

    for( const auto& [key, value] : aValue.items() )
    {
        bool* flag = key == "references"    ? &aOut.references
                     : key == "values"      ? &aOut.values
                     : key == "otherFields" ? &aOut.otherFields
                     : key == "labels"      ? &aOut.labels
                     : key == "sheetTitles" ? &aOut.sheetTitles
                     : key == "sheetPins"   ? &aOut.sheetPins
                     : key == "texts"       ? &aOut.texts
                     : key == "wires"       ? &aOut.wires
                     : key == "buses"       ? &aOut.buses
                     : key == "graphics"    ? &aOut.graphics
                     : key == "sheetFields" ? &aOut.sheetFields
                     : key == "sheetBorders" ? &aOut.sheetBorders
                     : key == "labelFields" ? &aOut.labelFields
                     : key == "selected"    ? &aOut.selected
                                            : nullptr;

        if( flag )
        {
            if( !value.is_boolean() )
                return false;

            *flag = value.get<bool>();
            continue;
        }

        if( key == "power" )
        {
            if( !value.is_boolean() )
                return false;

            aOut.power = value.get<bool>();
            continue;
        }

        std::optional<wxString>* text = key == "sheetPath"    ? &aOut.sheetPath
                                        : key == "refGlob"    ? &aOut.refGlob
                                        : key == "symbolGlob" ? &aOut.symbolGlob
                                        : key == "fieldGlob"  ? &aOut.fieldGlob
                                        : key == "netGlob"    ? &aOut.netGlob
                                                              : nullptr;

        if( !text )
            return false;

        if( !( *text = boundedText( value, 1, 256 ) ) || hasControlChar( **text ) )
            return false;
    }

    return aOut.references || aOut.values || aOut.otherFields || aOut.labels || aOut.sheetTitles
           || aOut.sheetPins || aOut.texts || aOut.wires || aOut.buses || aOut.graphics
           || aOut.sheetFields || aOut.sheetBorders || aOut.labelFields;
}

bool ccReadSchTextSet( const json& aValue, CC_SCH_TEXT_SET& aOut )
{
    if( !aValue.is_object() || aValue.empty() )
        return false;

    for( const auto& [key, value] : aValue.items() )
    {
        if( key == "size" )
        {
            // The dialog's own bounds: 1 mil to 10 inches.
            if( !( aOut.size = ccSchMmToIu( value, 0.0254, 254.0 ) ) )
                return false;
        }
        else if( key == "lineWidth" )
        {
            if( !( aOut.lineWidth = ccSchMmToIu( value, 0.0, 25.4 ) ) )
                return false;
        }
        else if( key == "junctionSize" )
        {
            if( !( aOut.junctionSize = ccSchMmToIu( value, 0.0, 25.4 ) ) )
                return false;
        }
        else if( key == "bold" || key == "italic" || key == "visible" || key == "showFieldNames" )
        {
            if( !value.is_boolean() )
                return false;

            const bool on = value.get<bool>();

            if( key == "bold" )
                aOut.bold = on;
            else if( key == "italic" )
                aOut.italic = on;
            else if( key == "visible" )
                aOut.visible = on;
            else
                aOut.showFieldNames = on;
        }
        else if( key == "hAlign" )
        {
            const std::string a = value.is_string() ? value.get<std::string>() : "";

            if( a == "left" )
                aOut.hAlign = GR_TEXT_H_ALIGN_LEFT;
            else if( a == "center" )
                aOut.hAlign = GR_TEXT_H_ALIGN_CENTER;
            else if( a == "right" )
                aOut.hAlign = GR_TEXT_H_ALIGN_RIGHT;
            else
                return false;
        }
        else if( key == "vAlign" )
        {
            const std::string a = value.is_string() ? value.get<std::string>() : "";

            if( a == "top" )
                aOut.vAlign = GR_TEXT_V_ALIGN_TOP;
            else if( a == "center" )
                aOut.vAlign = GR_TEXT_V_ALIGN_CENTER;
            else if( a == "bottom" )
                aOut.vAlign = GR_TEXT_V_ALIGN_BOTTOM;
            else
                return false;
        }
        else if( key == "lineStyle" )
        {
            const std::string s = value.is_string() ? value.get<std::string>() : "";

            if( s == "default" )
                aOut.lineStyle = LINE_STYLE::DEFAULT;
            else if( s == "solid" )
                aOut.lineStyle = LINE_STYLE::SOLID;
            else if( s == "dash" )
                aOut.lineStyle = LINE_STYLE::DASH;
            else if( s == "dot" )
                aOut.lineStyle = LINE_STYLE::DOT;
            else if( s == "dashDot" )
                aOut.lineStyle = LINE_STYLE::DASHDOT;
            else if( s == "dashDotDot" )
                aOut.lineStyle = LINE_STYLE::DASHDOTDOT;
            else
                return false;
        }
        else
        {
            return false;
        }
    }

    return true;
}

/** The rows a run collects: the first CC_RULES_ROWS_MAX kept, every one counted. */
struct CC_EDIT_ROWS
{
    json rows = json::array();
    int  count = 0;
    bool truncated = false;

    void Add( json&& aRow )
    {
        count++;

        if( rows.size() < CC_RULES_ROWS_MAX )
            rows.push_back( std::move( aRow ) );
        else
            truncated = true;
    }
};

const char* ccSchItemKind( const SCH_ITEM* aItem )
{
    if( const SCH_FIELD* field = dynamic_cast<const SCH_FIELD*>( aItem ) )
    {
        switch( field->GetId() )
        {
        case FIELD_T::REFERENCE:  return "reference";
        case FIELD_T::VALUE:      return "value";
        case FIELD_T::SHEET_NAME: return "sheetName";
        default:                  return "field";
        }
    }

    switch( aItem->Type() )
    {
    case SCH_SHEET_PIN_T:       return "sheetPin";
    case SCH_SHEET_T:           return "sheet";
    case SCH_LABEL_T:
    case SCH_GLOBAL_LABEL_T:
    case SCH_HIER_LABEL_T:
    case SCH_DIRECTIVE_LABEL_T: return "label";
    case SCH_TEXT_T:
    case SCH_TEXTBOX_T:         return "text";
    case SCH_JUNCTION_T:        return "junction";
    case SCH_LINE_T:
        return aItem->GetLayer() == LAYER_WIRE  ? "wire"
               : aItem->GetLayer() == LAYER_BUS ? "bus"
                                                : "graphic";
    default:                    return "graphic";
    }
}

/**
 * DIALOG_GLOBAL_EDIT_TEXT_AND_GRAPHICS::processItem (eeschema) for one item, colors never
 * set (the viewer's palette draws every item). Each setting differing from the item's is
 * named in aWords; aApply false only reads (the preview).
 */
void ccSchProcessText( SCH_COMMIT* aCommit, const SCH_SHEET_PATH& aPath, SCH_ITEM* aItem,
                       const CC_SCH_TEXT_SET& aSet, bool aApply, CC_EDIT_ROWS& aRows )
{
    std::vector<std::string> words;
    std::string              from;
    std::string              to;

    EDA_TEXT*      text = dynamic_cast<EDA_TEXT*>( aItem );
    SCH_FIELD*     field = dynamic_cast<SCH_FIELD*>( aItem );
    SCH_JUNCTION*  junction = dynamic_cast<SCH_JUNCTION*>( aItem );
    SCH_SYMBOL*    parentSymbol = dynamic_cast<SCH_SYMBOL*>( aItem->GetParent() );

    // The alignments a mirrored symbol's field shows are the other way round.
    std::optional<GR_TEXT_H_ALIGN_T> hAlign = aSet.hAlign;
    std::optional<GR_TEXT_V_ALIGN_T> vAlign = aSet.vAlign;

    if( hAlign && parentSymbol && parentSymbol->GetTransform().x1 < 0 )
    {
        if( *hAlign == GR_TEXT_H_ALIGN_LEFT )
            hAlign = GR_TEXT_H_ALIGN_RIGHT;
        else if( *hAlign == GR_TEXT_H_ALIGN_RIGHT )
            hAlign = GR_TEXT_H_ALIGN_LEFT;
    }

    if( vAlign && parentSymbol && parentSymbol->GetTransform().y1 < 0 )
    {
        if( *vAlign == GR_TEXT_V_ALIGN_TOP )
            vAlign = GR_TEXT_V_ALIGN_BOTTOM;
        else if( *vAlign == GR_TEXT_V_ALIGN_BOTTOM )
            vAlign = GR_TEXT_V_ALIGN_TOP;
    }

    if( text )
    {
        if( aSet.size && ( text->GetTextWidth() != *aSet.size || text->GetTextHeight() != *aSet.size ) )
        {
            words.push_back( "size" );
            from = std::to_string( ccSchMm( text->GetTextHeight() ) );
            to = std::to_string( ccSchMm( *aSet.size ) );
        }

        if( hAlign && text->GetHorizJustify() != *hAlign )
            words.push_back( "hAlign" );

        if( vAlign && text->GetVertJustify() != *vAlign )
            words.push_back( "vAlign" );

        if( aSet.italic && text->IsItalic() != *aSet.italic )
            words.push_back( "italic" );

        if( aSet.bold && text->IsBold() != *aSet.bold )
            words.push_back( "bold" );
    }

    if( field )
    {
        if( aSet.visible && field->IsVisible() != *aSet.visible )
            words.push_back( "visible" );

        if( aSet.showFieldNames && field->IsNameShown() != *aSet.showFieldNames )
            words.push_back( "showFieldNames" );
    }

    if( aItem->HasLineStroke() )
    {
        const STROKE_PARAMS stroke = aItem->GetStroke();

        if( aSet.lineWidth && stroke.GetWidth() != *aSet.lineWidth )
        {
            words.push_back( "lineWidth" );

            if( from.empty() )
            {
                from = std::to_string( ccSchMm( stroke.GetWidth() ) );
                to = std::to_string( ccSchMm( *aSet.lineWidth ) );
            }
        }

        if( aSet.lineStyle && stroke.GetLineStyle() != *aSet.lineStyle )
            words.push_back( "lineStyle" );
    }

    if( junction && aSet.junctionSize && junction->GetDiameter() != *aSet.junctionSize )
        words.push_back( "junctionSize" );

    if( words.empty() )
        return;

    if( aApply )
    {
        aCommit->Modify( aItem, aPath.LastScreen() );

        if( text )
        {
            if( aSet.size )
                text->SetTextSize( VECTOR2I( *aSet.size, *aSet.size ) );

            if( hAlign )
                text->SetHorizJustify( *hAlign );

            if( vAlign )
                text->SetVertJustify( *vAlign );

            if( aSet.italic )
                text->SetItalic( *aSet.italic );

            // After the size (KiCad's order), and the font again after bold and italic.
            if( aSet.bold )
                text->SetBold( *aSet.bold );

            if( ( aSet.bold || aSet.italic ) && !text->GetFontName().IsEmpty() )
            {
                text->SetFont( KIFONT::FONT::GetFont( text->GetFontName(), text->IsBold(),
                                                      text->IsItalic() ) );
            }
        }

        if( field )
        {
            if( aSet.visible )
                field->SetVisible( *aSet.visible );

            if( aSet.showFieldNames )
                field->SetNameShown( *aSet.showFieldNames );
        }

        if( aItem->HasLineStroke() && ( aSet.lineWidth || aSet.lineStyle ) )
        {
            STROKE_PARAMS stroke = aItem->GetStroke();

            if( aSet.lineWidth )
                stroke.SetWidth( *aSet.lineWidth );

            if( aSet.lineStyle )
                stroke.SetLineStyle( *aSet.lineStyle );

            aItem->SetStroke( stroke );
        }

        if( junction && aSet.junctionSize )
            junction->SetDiameter( *aSet.junctionSize );
    }

    // A field reports its symbol or sheet (the row the page can select).
    const EDA_ITEM* owner = field && aItem->GetParent() ? aItem->GetParent() : aItem;
    json            row = json::object();
    std::string     joined;

    for( const std::string& w : words )
        joined += ( joined.empty() ? "" : " " ) + w;

    row["uuid"] = toUtf8( owner->m_Uuid.AsString() );
    row["kind"] = ccSchItemKind( aItem );

    wxString name;

    if( field && parentSymbol )
        name = parentSymbol->GetRef( &aPath, false ) + wxS( " " ) + field->GetName();
    else if( field )
        name = field->GetName();
    else if( SCH_LABEL_BASE* label = dynamic_cast<SCH_LABEL_BASE*>( aItem ) )
        name = label->GetText();

    row["words"] = ccRulesCut( name.IsEmpty() ? wxString::FromUTF8( joined.c_str() )
                                         : name + wxS( ": " ) + wxString::FromUTF8( joined.c_str() ),
                          256 );

    if( !from.empty() )
    {
        row["from"] = from;
        row["to"] = to;
    }

    aRows.Add( std::move( row ) );
}

/** DIALOG_GLOBAL_EDIT_TEXT_AND_GRAPHICS::visitItem (eeschema) with 7.5's filter. */
void ccSchVisit( SCH_COMMIT* aCommit, const SCH_SHEET_PATH& aPath, SCH_ITEM* aItem,
                 const CC_SCH_TEXT_FILTER& aFilter, const CC_SCH_TEXT_SET& aSet, bool aApply,
                 CC_EDIT_ROWS& aRows )
{
    auto process =
            [&]( SCH_ITEM* aTarget )
            {
                if( aFilter.selected && !aTarget->IsSelected()
                        && ( !aTarget->GetParent() || !aTarget->GetParent()->IsSelected() ) )
                {
                    return;
                }

                ccSchProcessText( aCommit, aPath, aTarget, aSet, aApply, aRows );
            };

    auto fieldPasses =
            [&]( const SCH_FIELD& aField )
            {
                return !aFilter.fieldGlob
                       || WildCompareString( *aFilter.fieldGlob, aField.GetName(), false );
            };

    if( aFilter.netGlob )
    {
        SCH_CONNECTION* connection = aItem->Connection( &aPath );

        if( !connection || !WildCompareString( *aFilter.netGlob, connection->Name(), false ) )
            return;
    }

    if( aItem->Type() == SCH_SYMBOL_T )
    {
        SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( aItem );

        if( aFilter.refGlob && !WildCompareString( *aFilter.refGlob, symbol->GetRef( &aPath ), false ) )
            return;

        if( aFilter.symbolGlob
                && !WildCompareString( *aFilter.symbolGlob,
                                       UnescapeString( symbol->GetLibId().Format() ), false ) )
        {
            return;
        }

        if( aFilter.power && symbol->GetLibSymbolRef()
                && symbol->GetLibSymbolRef()->IsPower() != *aFilter.power )
        {
            return;
        }
    }
    else if( aFilter.refGlob || aFilter.symbolGlob || aFilter.power )
    {
        // A designator, symbol or power filter names symbols only (the dialog's rule keeps
        // other items; ours keeps the reader's sentence: "the parts matching R*").
        return;
    }

    switch( aItem->Type() )
    {
    case SCH_SYMBOL_T:
    {
        SCH_SYMBOL* symbol = static_cast<SCH_SYMBOL*>( aItem );

        if( aFilter.references )
            process( symbol->GetField( FIELD_T::REFERENCE ) );

        if( aFilter.values )
            process( symbol->GetField( FIELD_T::VALUE ) );

        if( aFilter.otherFields )
        {
            for( SCH_FIELD& field : symbol->GetFields() )
            {
                if( field.GetId() != FIELD_T::REFERENCE && field.GetId() != FIELD_T::VALUE
                        && fieldPasses( field ) )
                {
                    process( &field );
                }
            }
        }

        break;
    }

    case SCH_SHEET_T:
    {
        SCH_SHEET* sheet = static_cast<SCH_SHEET*>( aItem );

        if( aFilter.sheetTitles )
            process( sheet->GetField( FIELD_T::SHEET_NAME ) );

        if( aFilter.sheetFields )
        {
            for( SCH_FIELD& field : sheet->GetFields() )
            {
                if( field.GetId() != FIELD_T::SHEET_NAME && fieldPasses( field ) )
                    process( &field );
            }
        }

        if( aFilter.sheetBorders && aSet.lineWidth && sheet->GetBorderWidth() != *aSet.lineWidth )
        {
            if( aApply )
            {
                aCommit->Modify( sheet, aPath.LastScreen() );
                sheet->SetBorderWidth( *aSet.lineWidth );
            }

            json row = json::object();
            row["uuid"] = toUtf8( sheet->m_Uuid.AsString() );
            row["kind"] = "sheet";
            row["words"] = ccRulesCut( sheet->GetName() + wxS( ": lineWidth" ), 256 );
            row["from"] = std::to_string( ccSchMm( sheet->GetBorderWidth() ) );
            row["to"] = std::to_string( ccSchMm( *aSet.lineWidth ) );
            aRows.Add( std::move( row ) );
        }

        if( aFilter.sheetPins )
        {
            for( SCH_SHEET_PIN* pin : sheet->GetPins() )
                process( pin );
        }

        break;
    }

    case SCH_LINE_T:
        if( ( aFilter.graphics && aItem->GetLayer() == LAYER_NOTES )
                || ( aFilter.wires && aItem->GetLayer() == LAYER_WIRE )
                || ( aFilter.buses && aItem->GetLayer() == LAYER_BUS ) )
        {
            process( aItem );
        }

        break;

    case SCH_LABEL_T:
    case SCH_GLOBAL_LABEL_T:
    case SCH_HIER_LABEL_T:
    case SCH_DIRECTIVE_LABEL_T:
        if( aFilter.labels )
            process( aItem );

        if( aFilter.labelFields )
        {
            for( SCH_FIELD& field : static_cast<SCH_LABEL_BASE*>( aItem )->GetFields() )
            {
                if( fieldPasses( field ) )
                    process( &field );
            }
        }

        break;

    case SCH_JUNCTION_T:
    {
        SCH_JUNCTION* junction = static_cast<SCH_JUNCTION*>( aItem );

        for( SCH_ITEM* item : junction->ConnectedItems( aPath ) )
        {
            if( ( item->GetLayer() == LAYER_BUS && aFilter.buses )
                    || ( item->GetLayer() == LAYER_WIRE && aFilter.wires ) )
            {
                process( aItem );
                break;
            }
        }

        break;
    }

    case SCH_TEXT_T:
    case SCH_TEXTBOX_T:
        if( aFilter.texts )
            process( aItem );

        break;

    case SCH_SHAPE_T:
        if( aFilter.graphics )
            process( aItem );

        break;

    default:
        break;
    }
}

void ccRunSchEdit( const std::string& aDir, bool aPreview, const CC_SCH_TEXT_FILTER& aFilter,
                   const CC_SCH_TEXT_SET& aSet )
{
    std::string  error;
    std::string  message;
    CC_EDIT_ROWS rows;
    int          depth = -1;

    ensureRunDir( aDir );

    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
    {
        error = "island_error";
        message = "no schematic editor";
    }
    else
    {
        try
        {
            SCH_COMMIT commit( fr );

            for( const SCH_SHEET_PATH& path : fr->Schematic().Hierarchy() )
            {
                SCH_SCREEN* screen = path.LastScreen();

                if( !screen )
                    continue;

                if( aFilter.sheetPath && path.PathAsString().CmpNoCase( *aFilter.sheetPath ) != 0 )
                    continue;

                // The items as a list first: a commit's Modify may reorder the screen's tree.
                std::vector<SCH_ITEM*> items;

                for( SCH_ITEM* item : screen->Items() )
                    items.push_back( item );

                for( SCH_ITEM* item : items )
                    ccSchVisit( &commit, path, item, aFilter, aSet, !aPreview, rows );
            }

            if( !aPreview && !commit.Empty() )
            {
                commit.Push( wxT( "Edit Text and Graphics" ) );
                fr->HardRedraw();
            }
            else
            {
                commit.Revert();
            }

            depth = pcbjam_collab::testUndoDepth( fr );
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
    report["op"] = "textGraphics";
    report["preview"] = aPreview;
    report["rows"] = std::move( rows.rows );
    report["count"] = rows.count;
    report["truncated"] = rows.truncated;
    report["depth"] = depth;
    writeRunDone( aDir, "edit.json", report );

    json done = json::object();
    done["error"] = error;
    done["message"] = message;
    writeRunDone( aDir, ".cc-edit.json", done );
}

// ───────────────────────────── settings (schematic frame) ─────────────────────────────

/** A project-file section: wire keys (KiCad's own JSON names) to JSON pointers in one document. */
struct CC_JSON_SECTION
{
    JSON_SETTINGS*                                   settings = nullptr;
    std::vector<std::pair<std::string, std::string>> keys;   // wire key, pointer in the document
};

/** Patch values must not delete keys: a null anywhere inside is refused. */
bool ccRulesHasNull( const json& aValue )
{
    if( aValue.is_null() )
        return true;

    if( aValue.is_object() || aValue.is_array() )
    {
        for( const json& child : aValue )
        {
            if( ccRulesHasNull( child ) )
                return true;
        }
    }

    return false;
}

/** The two JSON values hold the same kind (number counts as one kind). */
bool ccRulesSameKind( const json& aA, const json& aB )
{
    if( aA.is_number() && aB.is_number() )
        return true;

    return aA.type() == aB.type();
}

json ccJsonSectionGet( const CC_JSON_SECTION& aSection )
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

/**
 * Merges aPatch (wire keys only) into the document, then Load() reads the members back.
 * Objects merge (RFC 7386, never deleting), arrays and scalars replace, a named key must
 * keep its kind. False with nothing changed when the patch is refused.
 */
bool ccJsonSectionSet( const CC_JSON_SECTION& aSection, const json& aPatch )
{
    if( !aPatch.is_object() || aPatch.empty() || ccRulesHasNull( aPatch ) )
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

            if( !ccRulesSameKind( target, value ) )
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

    static_cast<nlohmann::json&>( doc ) = next;
    aSection.settings->Load();
    return true;
}

std::optional<CC_JSON_SECTION> ccSchJsonSection( SCH_EDIT_FRAME* aFrame, const std::string& aName )
{
    CC_JSON_SECTION section;

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
    else if( aName == "ercSeverities" )
    {
        section.settings = &aFrame->Schematic().ErcSettings();
        section.keys = { { "rule_severities", "/rule_severities" } };
    }
    else if( aName == "pinMap" )
    {
        section.settings = &aFrame->Schematic().ErcSettings();
        section.keys = { { "pin_map", "/pin_map" } };
    }
    else if( aName == "schFormatting" )
    {
        // The Formatting panel's keys (panel_setup_formatting.cpp) and the numbering keys;
        // drawing.field_names is the Field names section's.
        section.settings = &aFrame->Schematic().Settings();
        section.keys = { { "drawing", "/drawing" },
                         { "connection_grid_size", "/connection_grid_size" },
                         { "subpart_id_separator", "/subpart_id_separator" },
                         { "subpart_first_id", "/subpart_first_id" },
                         { "annotate_start_num", "/annotate_start_num" } };
    }
    else if( aName == "fieldTemplates" )
    {
        section.settings = &aFrame->Schematic().Settings();
        section.keys = { { "field_names", "/drawing/field_names" } };
    }
    else
    {
        return std::nullopt;
    }

    return section;
}

json ccPageJson( const PAGE_INFO& aPage, const TITLE_BLOCK& aTitle, const wxString& aSheetFile )
{
    json page = json::object();
    page["paper"] = toUtf8( aPage.GetTypeAsString() );
    page["width"] = std::round( aPage.GetWidthMM() * 1000.0 ) / 1000.0;
    page["height"] = std::round( aPage.GetHeightMM() * 1000.0 ) / 1000.0;
    page["portrait"] = aPage.IsPortrait();
    page["title"] = ccRulesCut( aTitle.GetTitle() );
    page["date"] = ccRulesCut( aTitle.GetDate() );
    page["revision"] = ccRulesCut( aTitle.GetRevision() );
    page["company"] = ccRulesCut( aTitle.GetCompany() );

    json comments = json::array();

    for( int i = 0; i < 9; ++i )
        comments.push_back( ccRulesCut( aTitle.GetComment( i ) ) );

    page["comments"] = std::move( comments );
    page["drawingSheet"] = ccRulesCut( aSheetFile );
    return page;
}

/**
 * A page patch read whole: paper (KiCad's names, "User" with width and height in mm),
 * portrait, the title block's texts, comments (an array of up to nine, null keeps one),
 * drawingSheet; allSheets and pageNumbers on the schematic.
 */
struct CC_PAGE_PATCH
{
    std::optional<wxString>                  paper;
    std::optional<double>                    width, height;
    std::optional<bool>                      portrait;
    std::optional<wxString>                  title, date, revision, company, drawingSheet;
    std::map<int, wxString>                  comments;
    bool                                     allSheets = false;
    std::vector<std::pair<wxString, wxString>> pageNumbers;   // sheet path, page
};

bool ccReadPagePatch( const json& aValue, bool aSchematic, CC_PAGE_PATCH& aOut )
{
    if( !aValue.is_object() || aValue.empty() )
        return false;

    for( const auto& [key, value] : aValue.items() )
    {
        if( key == "paper" )
        {
            std::optional<wxString> paper = boundedText( value, 1, 32 );
            PAGE_INFO               probe;

            if( !paper || !probe.SetType( *paper ) )
                return false;

            aOut.paper = paper;
        }
        else if( key == "width" || key == "height" )
        {
            if( !value.is_number() )
                return false;

            const double mm = value.get<double>();

            // KiCad's user page bounds (MIN_PAGE_SIZE_MILS to the schematic's maximum).
            if( !std::isfinite( mm ) || mm < 25.4 || mm > 3048.0 )
                return false;

            ( key == "width" ? aOut.width : aOut.height ) = mm;
        }
        else if( key == "portrait" || key == "allSheets" )
        {
            if( !value.is_boolean() || ( key == "allSheets" && !aSchematic ) )
                return false;

            if( key == "portrait" )
                aOut.portrait = value.get<bool>();
            else
                aOut.allSheets = value.get<bool>();
        }
        else if( key == "title" || key == "date" || key == "revision" || key == "company"
                 || key == "drawingSheet" )
        {
            std::optional<wxString> text = boundedText( value, 0, CC_RULES_TEXT_MAX );

            if( !text || hasControlChar( *text ) )
                return false;

            if( key == "title" )
                aOut.title = text;
            else if( key == "date" )
                aOut.date = text;
            else if( key == "revision" )
                aOut.revision = text;
            else if( key == "company" )
                aOut.company = text;
            else
                aOut.drawingSheet = text;
        }
        else if( key == "comments" )
        {
            if( !value.is_array() || value.empty() || value.size() > 9 )
                return false;

            for( size_t i = 0; i < value.size(); ++i )
            {
                if( value[i].is_null() )
                    continue;

                std::optional<wxString> text = boundedText( value[i], 0, CC_RULES_TEXT_MAX );

                if( !text || hasControlChar( *text ) )
                    return false;

                aOut.comments[(int) i] = *text;
            }
        }
        else if( key == "pageNumbers" && aSchematic )
        {
            if( !value.is_array() || value.empty() || value.size() > 4096 )
                return false;

            for( const json& entry : value )
            {
                if( !entry.is_object() || entry.size() != 2 || !entry.contains( "sheetPath" )
                        || !entry.contains( "page" ) )
                {
                    return false;
                }

                std::optional<wxString> path = boundedText( entry["sheetPath"], 1, 4096 );
                std::optional<wxString> page = boundedText( entry["page"], 1, 64 );

                if( !path || !page || hasControlChar( *page ) )
                    return false;

                aOut.pageNumbers.emplace_back( *path, *page );
            }
        }
        else
        {
            return false;
        }
    }

    return true;
}

void ccApplyPagePatch( const CC_PAGE_PATCH& aPatch, PAGE_INFO& aPage, TITLE_BLOCK& aTitle )
{
    if( aPatch.paper )
        aPage.SetType( *aPatch.paper, aPatch.portrait.value_or( aPage.IsPortrait() ) );
    else if( aPatch.portrait )
        aPage.SetPortrait( *aPatch.portrait );

    if( aPage.IsCustom() )
    {
        if( aPatch.width )
            aPage.SetWidthMM( *aPatch.width );

        if( aPatch.height )
            aPage.SetHeightMM( *aPatch.height );
    }

    if( aPatch.title )
        aTitle.SetTitle( *aPatch.title );

    if( aPatch.date )
        aTitle.SetDate( *aPatch.date );

    if( aPatch.revision )
        aTitle.SetRevision( *aPatch.revision );

    if( aPatch.company )
        aTitle.SetCompany( *aPatch.company );

    for( const auto& [index, text] : aPatch.comments )
        aTitle.SetComment( index, text );
}

json ccSchVariantsJson( SCH_EDIT_FRAME* aFrame )
{
    SCHEMATIC& sch = aFrame->Schematic();
    json       names = json::array();

    for( const wxString& name : sch.GetVariantNames() )
    {
        json entry = json::object();
        entry["name"] = ccRulesCut( name, 128 );
        entry["description"] = ccRulesCut( sch.GetVariantDescription( name ) );
        names.push_back( std::move( entry ) );
    }

    json out = json::object();
    out["names"] = std::move( names );
    out["current"] = ccRulesCut( sch.GetCurrentVariant(), 128 );
    return out;
}

// ───────────────── the section registry (engine run 15, R7) ─────────────────
//
// The board registry's twin (cc_rules_pcb.inc): a section an author adds registers from that
// author's own inc (cc_eN_sch.inc) with a static registrar in its own namespace:
//
//   namespace cc_e12
//   {
//   const bool kSchEditing = ccRegisterSchSection( { "schEditing", "sch", &schEditingGet,
//                                                    &schEditingSet, false } );
//   }
//
// Consulted AFTER the built-in sections: a built-in name, a name registered before, an empty
// get or set, a frame other than "sch" or a name that is not [A-Za-z][A-Za-z0-9]* of at most
// 64 characters is refused (false, one line on stderr).

struct CC_SCH_SECTION
{
    std::string name;    // the wire name, as the page's cap settings:<name> spells it
    std::string frame;   // "sch": the schematic editor, the only frame this registry serves

    // The read: an object, or null when the frame cannot answer (the export then answers "").
    std::function<json( SCH_EDIT_FRAME* aFrame )> get;

    // The set, on the apply coroutine: true when the value is taken (with aDryRun: would be,
    // changing nothing). A refusal may name itself in aError and aMessage (left empty, a false
    // answers bad_args). aRecord takes keys the record carries beside "value". No undo step
    // and no save (KiCad's rule for settings).
    std::function<bool( SCH_EDIT_FRAME* aFrame, const json& aValue, bool aDryRun,
                        std::string& aError, std::string& aMessage, json& aRecord )>
            set;

    // true: a taken set (not a dry run) is followed by Schematic Setup's OK steps
    // (ccSchSettingsChanged). false only for a section that holds no schematic state (an
    // editor preference), so the schematic is not marked modified.
    bool setupOk = true;

    // With setupOk: the connectivity is rebuilt too (a net class change).
    bool reconnect = false;
};

/** Run 14's sections, answered by the chains of ccSchSettingsGet and ccRunSchSettingsSet. */
const std::set<std::string>& ccSchBuiltinSections()
{
    static const std::set<std::string> sections = { "netClasses",    "textVariables", "variants",
                                                    "page",          "schFormatting", "fieldTemplates",
                                                    "busAliases",    "ercSeverities", "pinMap" };
    return sections;
}

std::vector<CC_SCH_SECTION>& ccSchSectionRegistry()
{
    static std::vector<CC_SCH_SECTION> registry;
    return registry;
}

bool ccSectionNameOk( const std::string& aName )
{
    if( aName.empty() || aName.size() > 64 || !std::isalpha( (unsigned char) aName[0] ) )
        return false;

    return std::all_of( aName.begin(), aName.end(),
                        []( char c ) { return std::isalnum( (unsigned char) c ) != 0; } );
}

const CC_SCH_SECTION* ccSchRegisteredSection( const std::string& aName )
{
    for( const CC_SCH_SECTION& section : ccSchSectionRegistry() )
    {
        if( section.name == aName )
            return &section;
    }

    return nullptr;
}

bool ccRegisterSchSection( CC_SCH_SECTION aSection )
{
    if( !ccSectionNameOk( aSection.name ) || aSection.frame != "sch" || !aSection.get
            || !aSection.set || ccSchBuiltinSections().count( aSection.name )
            || ccSchRegisteredSection( aSection.name ) )
    {
        std::fprintf( stderr, "settings section %s refused by the schematic registry\n",
                      aSection.name.c_str() );
        return false;
    }

    ccSchSectionRegistry().push_back( std::move( aSection ) );
    return true;
}

json ccSchSettingsGet( SCH_EDIT_FRAME* aFrame, const std::string& aSection )
{
    SCHEMATIC& sch = aFrame->Schematic();

    if( std::optional<CC_JSON_SECTION> section = ccSchJsonSection( aFrame, aSection ) )
    {
        json out = ccJsonSectionGet( *section );

        if( aSection == "schFormatting" && out.contains( "drawing" ) && out["drawing"].is_object() )
            out["drawing"].erase( "field_names" );

        return out;
    }

    if( aSection == "textVariables" )
    {
        json out = json::object();

        for( const auto& [name, text] : aFrame->Prj().GetTextVars() )
            out[toUtf8( name )] = ccRulesCut( text );

        return out;
    }

    if( aSection == "variants" )
        return ccSchVariantsJson( aFrame );

    if( aSection == "busAliases" )
    {
        json aliases = json::array();

        for( const std::shared_ptr<BUS_ALIAS>& alias : sch.GetAllBusAliases() )
        {
            json entry = json::object();
            json members = json::array();

            for( const wxString& member : alias->Members() )
                members.push_back( ccRulesCut( member, 256 ) );

            entry["name"] = ccRulesCut( alias->GetName(), 256 );
            entry["members"] = std::move( members );
            aliases.push_back( std::move( entry ) );
        }

        json out = json::object();
        out["aliases"] = std::move( aliases );
        return out;
    }

    if( aSection == "page" )
    {
        SCH_SCREEN* screen = aFrame->GetScreen();

        if( !screen )
            return json();

        json out = ccPageJson( screen->GetPageSettings(), screen->GetTitleBlock(),
                               sch.Settings().m_SchDrawingSheetFileName );
        json numbers = json::array();

        for( const SCH_SHEET_PATH& path : sch.Hierarchy() )
        {
            json entry = json::object();
            entry["sheetPath"] = toUtf8( path.PathAsString() );
            entry["page"] = ccRulesCut( path.GetPageNumber(), 64 );
            numbers.push_back( std::move( entry ) );
        }

        out["pageNumbers"] = std::move( numbers );
        return out;
    }

    if( const CC_SCH_SECTION* registered = ccSchRegisteredSection( aSection ) )
        return registered->get( aFrame );

    return json();
}

/** After a schematic settings change: the Schematic Setup dialog's OK, less its project save. */
void ccSchSettingsChanged( SCH_EDIT_FRAME* aFrame, bool aReconnect )
{
    SCHEMATIC& sch = aFrame->Schematic();

    aFrame->OnModify();
    aFrame->Kiway().CommonSettingsChanged( TEXTVARS_CHANGED );
    aFrame->Prj().IncrementTextVarsTicker();
    aFrame->Prj().IncrementNetclassesTicker();

    if( SCH_RENDER_SETTINGS* rs = aFrame->GetRenderSettings() )
    {
        rs->SetDefaultPenWidth( sch.Settings().m_DefaultLineWidth );
        rs->m_LabelSizeRatio = sch.Settings().m_LabelSizeRatio;
        rs->m_TextOffsetRatio = sch.Settings().m_TextOffsetRatio;
        rs->m_PinSymbolSize = sch.Settings().m_PinSymbolSize;
        rs->m_SymbolLineWidth = sch.Settings().m_DefaultLineWidth;
        rs->SetDashLengthRatio( sch.Settings().m_DashedLineDashRatio );
        rs->SetGapLengthRatio( sch.Settings().m_DashedLineGapRatio );
    }

    if( aReconnect )
        aFrame->RecalculateConnections( nullptr, GLOBAL_CLEANUP );

    aFrame->GetCanvas()->GetView()->MarkDirty();
    aFrame->GetCanvas()->GetView()->UpdateAllItems( KIGFX::REPAINT );
    aFrame->GetCanvas()->Refresh();
}

/** One variant action (7.4): the manager's way, SCHEMATIC's API, a commit when symbols change. */
bool ccSchVariantAction( SCH_EDIT_FRAME* aFrame, const json& aValue, bool aDryRun, json& aErrors )
{
    SCHEMATIC& sch = aFrame->Schematic();

    if( !aValue.is_object() || aValue.size() != 1 )
        return false;

    const auto        first = aValue.begin();
    const std::string action = first.key();
    const json&       value = first.value();

    auto nameOf = [&]( const json& aName ) -> std::optional<wxString>
                  {
                      std::optional<wxString> name = boundedText( aName, 1, 128 );

                      if( !name || hasControlChar( *name )
                              || name->CmpNoCase( GetDefaultVariantName() ) == 0 )
                      {
                          return std::nullopt;
                      }

                      return name;
                  };

    auto pairOf = [&]( std::optional<wxString>& aA, std::optional<wxString>& aB ) -> bool
                  {
                      if( !value.is_array() || value.size() != 2 )
                          return false;

                      aA = nameOf( value[0] );
                      aB = nameOf( value[1] );
                      return aA && aB;
                  };

    const std::set<wxString>& names = sch.GetVariantNames();

    if( action == "add" )
    {
        std::optional<wxString> name = nameOf( value );

        if( !name || names.count( *name ) )
            return false;

        if( !aDryRun )
            sch.AddVariant( *name );
    }
    else if( action == "copy" || action == "rename" )
    {
        std::optional<wxString> from, to;

        if( !pairOf( from, to ) || !names.count( *from ) || names.count( *to ) )
            return false;

        if( !aDryRun )
        {
            SCH_COMMIT commit( aFrame );

            if( action == "copy" )
                sch.CopyVariant( *from, *to, &commit );
            else
                sch.RenameVariant( *from, *to, &commit );

            if( !commit.Empty() )
                commit.Push( action == "copy" ? wxT( "Copy variant" ) : wxT( "Rename variant" ) );
        }
    }
    else if( action == "delete" )
    {
        std::optional<wxString> name = nameOf( value );

        if( !name || !names.count( *name ) )
            return false;

        if( !aDryRun )
        {
            const bool wasCurrent = sch.GetCurrentVariant() == *name;

            // SCH_EDIT_FRAME::RemoveVariant's way: one commit, pushed when any symbol or
            // sheet held the variant (an undo step; KiCad's own behavior).
            SCH_COMMIT commit( aFrame );
            sch.DeleteVariant( *name, &commit );

            if( !commit.Empty() )
                commit.Push( wxString::Format( wxS( "Delete variant '%s'" ), *name ) );

            if( wasCurrent )
                sch.SetCurrentVariant( wxEmptyString );
        }
    }
    else if( action == "describe" )
    {
        if( !value.is_array() || value.size() != 2 )
            return false;

        std::optional<wxString> name = nameOf( value[0] );
        std::optional<wxString> text = boundedText( value[1], 0, CC_RULES_TEXT_MAX );

        if( !name || !text || !names.count( *name ) || hasControlChar( *text ) )
            return false;

        if( !aDryRun )
            sch.SetVariantDescription( *name, *text );
    }
    else if( action == "current" )
    {
        std::optional<wxString> name = boundedText( value, 0, 128 );

        if( !name || ( !name->IsEmpty() && !names.count( *name ) ) )
            return false;

        if( !aDryRun )
            sch.SetCurrentVariant( *name );
    }
    else
    {
        return false;
    }

    if( !aDryRun )
    {
        aFrame->UpdateVariantSelectionCtrl( sch.GetVariantNamesForUI() );
        aFrame->OnModify();
        aFrame->HardRedraw();
    }

    return true;
}

struct CC_SETTINGS_ARGS
{
    std::string dir;
    std::string section;
    json        value;
    bool        dryRun = false;
};

/** Every section the schematic frame takes: run 14's built-ins and every registered one. */
std::set<std::string> ccSchSections()
{
    std::set<std::string> sections = ccSchBuiltinSections();

    for( const CC_SCH_SECTION& section : ccSchSectionRegistry() )
        sections.insert( section.name );

    return sections;
}

/** The set, on the apply coroutine. The record carries the section read back. */
void ccRunSchSettingsSet( const CC_SETTINGS_ARGS& aArgs )
{
    std::string error;
    std::string message;
    json        errors = json::array();
    json        value;
    json        extra = json::object();   // a registered section's own record keys

    ensureRunDir( aArgs.dir );

    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr )
    {
        error = "island_error";
        message = "no schematic editor";
    }
    else
    {
        try
        {
            bool ok = true;
            bool reconnect = false;

            if( std::optional<CC_JSON_SECTION> section = ccSchJsonSection( fr, aArgs.section ) )
            {
                if( aArgs.section == "schFormatting" && aArgs.value.contains( "drawing" )
                        && aArgs.value["drawing"].is_object()
                        && aArgs.value["drawing"].contains( "field_names" ) )
                {
                    ok = false;
                }
                else if( !aArgs.dryRun )
                {
                    ok = ccJsonSectionSet( *section, aArgs.value );
                    reconnect = aArgs.section == "netClasses";

                    if( ok )
                        ccSchSettingsChanged( fr, reconnect );
                }
            }
            else if( aArgs.section == "textVariables" )
            {
                // A patch: name to text, a null removes the variable.
                std::map<wxString, wxString> next = fr->Prj().GetTextVars();

                if( !aArgs.value.is_object() || aArgs.value.empty() )
                    ok = false;

                for( const auto& [name, text] : aArgs.value.items() )
                {
                    if( !ok )
                        break;

                    wxString key = wxString::FromUTF8( name.c_str() );

                    if( key.IsEmpty() || key.length() > 128 || hasControlChar( key ) )
                    {
                        ok = false;
                    }
                    else if( text.is_null() )
                    {
                        next.erase( key );
                    }
                    else if( std::optional<wxString> t = boundedText( text, 0, CC_RULES_TEXT_MAX ) )
                    {
                        next[key] = *t;
                    }
                    else
                    {
                        ok = false;
                    }
                }

                if( ok && !aArgs.dryRun )
                {
                    fr->Prj().GetTextVars() = next;
                    ccSchSettingsChanged( fr, false );
                }
            }
            else if( aArgs.section == "variants" )
            {
                ok = ccSchVariantAction( fr, aArgs.value, aArgs.dryRun, errors );
            }
            else if( aArgs.section == "busAliases" )
            {
                std::vector<std::shared_ptr<BUS_ALIAS>> aliases;

                if( !aArgs.value.is_object() || aArgs.value.size() != 1
                        || !aArgs.value.contains( "aliases" ) || !aArgs.value["aliases"].is_array()
                        || aArgs.value["aliases"].size() > 1024 )
                {
                    ok = false;
                }
                else
                {
                    std::set<wxString> seen;

                    for( const json& entry : aArgs.value["aliases"] )
                    {
                        if( !entry.is_object() || entry.size() != 2 || !entry.contains( "name" )
                                || !entry.contains( "members" ) || !entry["members"].is_array()
                                || entry["members"].size() > 1024 )
                        {
                            ok = false;
                            break;
                        }

                        std::optional<wxString> name = boundedText( entry["name"], 1, 256 );

                        if( !name || hasControlChar( *name ) || !seen.insert( *name ).second )
                        {
                            ok = false;
                            break;
                        }

                        std::vector<wxString> members;

                        for( const json& member : entry["members"] )
                        {
                            std::optional<wxString> m = boundedText( member, 1, 256 );

                            if( !m || hasControlChar( *m ) )
                            {
                                ok = false;
                                break;
                            }

                            members.push_back( *m );
                        }

                        if( !ok )
                            break;

                        auto alias = std::make_shared<BUS_ALIAS>();
                        alias->SetName( *name );
                        alias->SetMembers( members );
                        aliases.push_back( alias );
                    }
                }

                if( ok && !aArgs.dryRun )
                {
                    fr->Schematic().SetBusAliases( aliases );
                    ccSchSettingsChanged( fr, true );
                }
            }
            else if( aArgs.section == "page" )
            {
                CC_PAGE_PATCH patch;
                ok = ccReadPagePatch( aArgs.value, true, patch );

                if( ok && !aArgs.dryRun )
                {
                    SCH_SCREEN* shown = fr->GetScreen();
                    SCH_SCREENS screens( fr->Schematic().Root() );

                    // The shown sheet's page takes the patch; with allSheets every sheet
                    // takes the keys the patch names (the page dialog's export boxes).
                    for( SCH_SCREEN* screen = screens.GetFirst(); screen; screen = screens.GetNext() )
                    {
                        if( screen != shown && !patch.allSheets )
                            continue;

                        PAGE_INFO   page = screen->GetPageSettings();
                        TITLE_BLOCK title = screen->GetTitleBlock();
                        ccApplyPagePatch( patch, page, title );
                        screen->SetPageSettings( page );
                        screen->SetTitleBlock( title );
                    }

                    if( !patch.pageNumbers.empty() )
                    {
                        for( SCH_SHEET_PATH path : fr->Schematic().Hierarchy() )
                        {
                            for( const auto& [sheetPath, page] : patch.pageNumbers )
                            {
                                if( path.PathAsString().CmpNoCase( sheetPath ) == 0 )
                                    path.SetPageNumber( page );
                            }
                        }

                        fr->SetSheetNumberAndCount();
                    }

                    if( patch.drawingSheet )
                    {
                        fr->Schematic().Settings().m_SchDrawingSheetFileName = *patch.drawingSheet;
                        fr->LoadDrawingSheet();
                    }

                    fr->OnModify();
                    fr->HardRedraw();
                }
            }
            else if( const CC_SCH_SECTION* registered = ccSchRegisteredSection( aArgs.section ) )
            {
                // Engine run 15 (R7): a registered section, after every built-in one, before
                // Schematic Setup's OK steps.
                ok = registered->set( fr, aArgs.value, aArgs.dryRun, error, message, extra );

                if( ok && !aArgs.dryRun && registered->setupOk )
                    ccSchSettingsChanged( fr, registered->reconnect );
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

            value = ccSchSettingsGet( fr, aArgs.section );
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

    for( auto it = extra.begin(); it != extra.end(); ++it )
    {
        if( !record.contains( it.key() ) )
            record[it.key()] = it.value();
    }

    writeRunDone( aArgs.dir, "settings.json", record );

    json done = json::object();
    done["error"] = error;
    done["message"] = message;
    writeRunDone( aArgs.dir, ".cc-settings.json", done );
}

/** {run, dir, section, value, dryRun?} read whole; the dir under /tmp/cc-settings/. */
bool ccReadSettingsArgs( const std::string& aJson, CC_SETTINGS_ARGS& aOut )
{
    json args = json::parse( aJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() < 4 || args.size() > 5 )
        return false;

    auto run = args.find( "run" );
    auto dir = args.find( "dir" );
    auto section = args.find( "section" );
    auto value = args.find( "value" );
    auto dryRun = args.find( "dryRun" );

    if( run == args.end() || !run->is_number_integer() || run->get<long long>() < 0
            || dir == args.end() || !dir->is_string() || section == args.end()
            || !section->is_string() || value == args.end() || !value->is_object() )
    {
        return false;
    }

    if( args.size() == 5 && ( dryRun == args.end() || !dryRun->is_boolean() ) )
        return false;

    aOut.dir = dir->get<std::string>();
    aOut.section = section->get<std::string>();
    aOut.value = *value;
    aOut.dryRun = dryRun != args.end() && dryRun->get<bool>();

    // A value is at most 1 MB of JSON (a net class table, a pin map, a page).
    if( aOut.value.dump().size() > ( 1u << 20 ) )
        return false;

    return isRunDir( aOut.dir, "/tmp/cc-settings/" );
}
} // namespace

// ───────────────────────────── the exports ─────────────────────────────

// symbols.get (cap symbols): aArgsJson {"variant": string, "scope": "all"} or {"variant":
// string, "items": [{uuid, sheetPath}] (1 to 4,096)}, those two keys only. Answers 7.1's
// SymbolTable: every non-power instance in hierarchy order (or those asked, in the order
// asked; an item the design does not hold is left out), values and flags resolved for the
// variant ("" the default) with the sheet's own flags folded in. "" refuses (bad args, no
// frame, an open in flight, a variant the design lacks). A read: synchronous.
static std::string kicadSymbolsGet( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return "";

    try
    {
        json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

        if( args.is_discarded() || !args.is_object() || args.size() != 2 || !args.contains( "variant" ) )
            return "";

        std::optional<wxString> variant = ccReadVariant( fr, args["variant"] );

        if( !variant )
            return "";

        std::vector<CC_INSTANCE>      ordered;
        std::map<std::string, size_t> index;
        ccRulesInstances( fr, ordered, index );

        std::vector<size_t> picked;

        if( args.contains( "scope" ) )
        {
            if( !args["scope"].is_string() || args["scope"].get<std::string>() != "all" )
                return "";

            for( size_t i = 0; i < ordered.size(); ++i )
                picked.push_back( i );
        }
        else if( args.contains( "items" ) )
        {
            const json& items = args["items"];

            if( !items.is_array() || items.empty() || items.size() > CC_RULES_ROWS_MAX )
                return "";

            std::set<size_t> seen;

            for( const json& item : items )
            {
                if( !item.is_object() || item.size() != 2 || !item.contains( "uuid" )
                        || !item.contains( "sheetPath" ) || !item["uuid"].is_string()
                        || !item["sheetPath"].is_string()
                        || !isLowerUuid( item["uuid"].get<std::string>() ) )
                {
                    return "";
                }

                auto it = index.find( ccRulesInstanceKey(
                        wxString::FromUTF8( item["sheetPath"].get<std::string>().c_str() ),
                        wxString::FromUTF8( item["uuid"].get<std::string>().c_str() ) ) );

                if( it != index.end() && seen.insert( it->second ).second )
                    picked.push_back( it->second );
            }
        }
        else
        {
            return "";
        }

        json rows = json::array();
        int  power = 0;
        bool truncated = false;

        for( size_t i : picked )
        {
            const CC_INSTANCE& inst = ordered[i];

            if( inst.symbol->IsPower() )
            {
                power++;
                continue;
            }

            if( rows.size() >= CC_RULES_ROWS_MAX )
            {
                truncated = true;
                continue;
            }

            rows.push_back( ccSymbolRow( fr, inst.symbol, inst.path, *variant ) );
        }

        json out = json::object();
        out["variant"] = ccRulesCut( *variant, 128 );
        out["variants"] = ccVariantNames( fr );
        out["rows"] = std::move( rows );
        out["power"] = power;
        out["truncated"] = truncated;
        out["depth"] = pcbjam_collab::testUndoDepth( fr );
        return out.dump( -1, ' ', false, json::error_handler_t::replace );
    }
    catch( ... )
    {
        return "";
    }
}

// symbols.set (cap symbols): aArgsJson {"run": int >= 0, "dir": "/tmp/cc-symbols/<n>",
// "variant": string, "changes": [{uuid, sheetPath, set: SymbolsSet}] (1 to 4,096, one per
// instance)}, those four keys only. Each set as 7.1 (ref, value, footprint, fields,
// addField, removeField, inBom, onBoard, inSim, inPos, dnp, clear; at least one): under a
// variant ref, addField, removeField and a field's shown refuse the whole set, and clear
// is the variant's alone. False (nothing queued) for anything else; true once ONE
// SCH_COMMIT "Edit fields" is queued on the apply coroutine; it writes <dir>/symbols.json
// {applied, depth}, then <dir>/.cc-symbols.json {error, message} last.
static bool kicadSymbolsSet( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return false;

    try
    {
        json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

        if( args.is_discarded() || !args.is_object() || args.size() != 4 )
            return false;

        auto run = args.find( "run" );
        auto dir = args.find( "dir" );
        auto variantArg = args.find( "variant" );
        auto changes = args.find( "changes" );

        if( run == args.end() || !run->is_number_integer() || run->get<long long>() < 0
                || dir == args.end() || !dir->is_string() || variantArg == args.end()
                || changes == args.end() || !changes->is_array() || changes->empty()
                || changes->size() > CC_RULES_ROWS_MAX )
        {
            return false;
        }

        const std::string d = dir->get<std::string>();

        if( !isRunDir( d, "/tmp/cc-symbols/" ) )
            return false;

        std::optional<wxString> variant = ccReadVariant( fr, *variantArg );

        if( !variant )
            return false;

        std::vector<CC_INSTANCE>      ordered;
        std::map<std::string, size_t> index;
        ccRulesInstances( fr, ordered, index );

        std::vector<CC_SYMBOLS_CHANGE> read;
        std::set<std::string>          keys;

        for( const json& change : *changes )
        {
            if( !change.is_object() || change.size() != 3 || !change.contains( "uuid" )
                    || !change.contains( "sheetPath" ) || !change.contains( "set" )
                    || !change["uuid"].is_string() || !change["sheetPath"].is_string()
                    || !isLowerUuid( change["uuid"].get<std::string>() ) )
            {
                return false;
            }

            CC_SYMBOLS_CHANGE c;
            c.uuid = change["uuid"].get<std::string>();
            c.sheetPath = change["sheetPath"].get<std::string>();
            c.key = ccRulesInstanceKey( wxString::FromUTF8( c.sheetPath.c_str() ),
                                   wxString::FromUTF8( c.uuid.c_str() ) );

            auto it = index.find( c.key );

            if( it == index.end() || !keys.insert( c.key ).second )
                return false;

            if( !ccReadSymbolsSet( change["set"], ordered[it->second].symbol, *variant, c ) )
                return false;

            read.push_back( std::move( c ) );
        }

        const wxString v = *variant;

        pcbjam_collab::runOnCoroutine( fr, [d, v, read]() { ccRunSymbolsSet( d, v, read ); } );
        return true;
    }
    catch( ... )
    {
        return false;
    }
}

// symbols.exchange (cap symbolExchange): aArgsJson {"run", "dir": "/tmp/cc-exchange/<n>",
// "mode": "update" | "change", "targets": [{uuid, sheetPath}] (1 to 4,096), "libId": ""
// (update) or "lib:name" (change), "fields": "all" | [canonical names] (1 to 256),
// "options": {removeExtra, resetEmpty, resetText, resetVisibility, resetEffects,
// resetPositions, resetAttributes, resetPins, and KiCad's resetPinTextVisibility and
// resetCustomPower: each optional bool}, "preview": bool}, those eight keys only. The core is
// DIALOG_CHANGE_SYMBOLS::processSymbols. A preview lists the rows and changes nothing; an
// apply is ONE commit. Writes <dir>/exchange.json (7.3's ExchangeReport), then
// <dir>/.cc-exchange.json last.
static bool kicadSymbolsExchange( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return false;

    try
    {
        json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

        if( args.is_discarded() || !args.is_object() || args.size() != 8 )
            return false;

        for( const char* key : { "run", "dir", "mode", "targets", "libId", "fields", "options", "preview" } )
        {
            if( !args.contains( key ) )
                return false;
        }

        CC_SCH_EXCHANGE_ARGS a;

        if( !args["run"].is_number_integer() || args["run"].get<long long>() < 0
                || !args["dir"].is_string() || !args["mode"].is_string()
                || !args["targets"].is_array() || args["targets"].empty()
                || args["targets"].size() > CC_RULES_ROWS_MAX || !args["preview"].is_boolean() )
        {
            return false;
        }

        a.dir = args["dir"].get<std::string>();
        a.preview = args["preview"].get<bool>();

        const std::string mode = args["mode"].get<std::string>();

        if( !isRunDir( a.dir, "/tmp/cc-exchange/" ) || ( mode != "update" && mode != "change" ) )
            return false;

        a.change = mode == "change";

        std::optional<wxString> libId = boundedText( args["libId"], 0, 512 );

        if( !libId || ( a.change ? libId->IsEmpty() || !isFootprintId( *libId ) : !libId->IsEmpty() ) )
            return false;   // isFootprintId reads "lib:name" with LIB_ID: the same grammar

        a.libId = *libId;

        if( args["fields"].is_string() )
        {
            if( args["fields"].get<std::string>() != "all" )
                return false;

            a.allFields = true;
        }
        else if( args["fields"].is_array() && !args["fields"].empty() && args["fields"].size() <= 256 )
        {
            a.allFields = false;

            for( const json& name : args["fields"] )
            {
                std::optional<wxString> n = boundedText( name, 1, 128 );

                if( !n )
                    return false;

                a.fields.insert( *n );
            }
        }
        else
        {
            return false;
        }

        if( !ccReadExchangeOptions( args["options"], a.options ) )
            return false;

        std::vector<CC_INSTANCE>      ordered;
        std::map<std::string, size_t> index;
        ccRulesInstances( fr, ordered, index );
        std::set<std::string> seen;

        for( const json& target : args["targets"] )
        {
            if( !target.is_object() || target.size() != 2 || !target.contains( "uuid" )
                    || !target.contains( "sheetPath" ) || !target["uuid"].is_string()
                    || !target["sheetPath"].is_string()
                    || !isLowerUuid( target["uuid"].get<std::string>() ) )
            {
                return false;
            }

            const std::string key = ccRulesInstanceKey(
                    wxString::FromUTF8( target["sheetPath"].get<std::string>().c_str() ),
                    wxString::FromUTF8( target["uuid"].get<std::string>().c_str() ) );

            if( !index.count( key ) || !seen.insert( key ).second )
                return false;

            a.targets.push_back( key );
        }

        pcbjam_collab::runOnCoroutine( fr, [a]() { ccRunSchExchange( a ); } );
        return true;
    }
    catch( ... )
    {
        return false;
    }
}

// sch.edit (cap schEdit): aArgsJson {"run", "dir": "/tmp/cc-edit/<n>", "op": "textGraphics",
// "preview": bool, "filter": SchTextFilter, "set": SchTextSet}, those six keys only (7.5,
// with KiCad's other targets and filters as optional keys, BUILD-NOTES-rules.md). The core is
// DIALOG_GLOBAL_EDIT_TEXT_AND_GRAPHICS's visit and process; colors are never set. Writes
// <dir>/edit.json (7.5's EditReport), then <dir>/.cc-edit.json last.
static bool kicadSchEdit( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
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

        if( !args["run"].is_number_integer() || args["run"].get<long long>() < 0
                || !args["dir"].is_string() || !args["op"].is_string()
                || args["op"].get<std::string>() != "textGraphics" || !args["preview"].is_boolean() )
        {
            return false;
        }

        const std::string d = args["dir"].get<std::string>();
        const bool        preview = args["preview"].get<bool>();

        if( !isRunDir( d, "/tmp/cc-edit/" ) )
            return false;

        CC_SCH_TEXT_FILTER filter;
        CC_SCH_TEXT_SET    set;

        if( !ccReadSchTextFilter( args["filter"], filter ) || !ccReadSchTextSet( args["set"], set ) )
            return false;

        pcbjam_collab::runOnCoroutine( fr, [d, preview, filter, set]() {
            ccRunSchEdit( d, preview, filter, set );
        } );

        return true;
    }
    catch( ... )
    {
        return false;
    }
}

// kicadSettingsSections' schematic half (engine run 15, R7): every section the schematic
// frame takes, run 14's built-ins and every registered one, as a sorted JSON array of names.
std::string schSettingsSections()
{
    try
    {
        return json( ccSchSections() ).dump();
    }
    catch( ... )
    {
        return "[]";
    }
}

// settings.get (cap settings), the schematic frame: one section of the editor's in-memory
// state (7.4: netClasses, textVariables, variants, page, schFormatting, fieldTemplates,
// busAliases, ercSeverities, pinMap), as JSON; "" for any other section, no frame or an open
// in flight. A read: synchronous.
std::string schSettingsGet( std::string aSection )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() || !ccSchSections().count( aSection ) )
        return "";

    try
    {
        json out = ccSchSettingsGet( fr, aSection );

        if( out.is_null() )
            return "";

        return out.dump( -1, ' ', false, json::error_handler_t::replace );
    }
    catch( ... )
    {
        return "";
    }
}

// settings.set (cap settings), the schematic frame: aArgsJson {"run", "dir":
// "/tmp/cc-settings/<n>", "section", "value": object, "dryRun"?: bool}. False (nothing
// queued) for a section this frame does not hold or args of another shape; true once queued.
// A project-file section's value is a PATCH (objects merge, arrays and scalars replace, no
// null); variants take one action; no undo step but a variant copy, rename or delete that
// touched symbols (KiCad's own commit). Writes <dir>/settings.json {value, errors?}, then
// <dir>/.cc-settings.json {error, message} last.
bool schSettingsSet( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return false;

    try
    {
        CC_SETTINGS_ARGS a;

        if( !ccReadSettingsArgs( aArgsJson, a ) || !ccSchSections().count( a.section ) )
            return false;

        pcbjam_collab::runOnCoroutine( fr, [a]() { ccRunSchSettingsSet( a ); } );
        return true;
    }
    catch( ... )
    {
        return false;
    }
}
