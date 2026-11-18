/*
 * Embind bindings for the Symbol Editor frame (SYMBOL_EDIT_FRAME) of the merged
 * kicad_editor WASM image: engine run 14, the island's symedit.* ops (caps symedit and
 * symbolConvert). Compiled as its own TU beside eeschema_embind.cpp by
 * scripts/kicad/build-kicad-target.sh and registered OUTSIDE any KICAD_MERGED_EMBIND
 * guard: every name here is new and registered once.
 *
 * The house pattern: closed JSON args read whole before anything is queued (false, or ""
 * for a read, for any other shape, a missing frame, an open in flight or a dialog up); a
 * change runs on the apply coroutine as ONE commit (SCH_COMMIT with the symbol Modify'd on
 * the frame's screen, one Push: one undo step); a read is synchronous; lengths in mm,
 * angles in degrees, positions in the symbol's own frame with Y up (as KiCad's symbol
 * editor shows them; stored Y down). No KiCad dialog ever opens from here.
 *
 * Created by Chirichella Inc. on 2026-11-16 (spec docs/superpowers/specs/
 * 2026-11-16-symbol-editor-design.md, section 5.1, of the Circuit Center site).
 * Modified by Circuit Center Inc on 2026-11-18: the JSON readers, tokens and the text, text
 * box, shape, stroke and fill readers and appliers moved to symedit_common.h (cc_symedit).
 * Modified by Circuit Center Inc on 2026-11-18: sy4-e, a placed part's reference prefix is read,
 * set and checked as its prefix (never its designator), and a new De Morgan symbol has two
 * body styles.
 */

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <emscripten/bind.h>
#include <refdes_utils.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <wx/app.h>
#include <wx/dialog.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/string.h>
#include <wx/toplevel.h>
#include <wx/window.h>

#include <nlohmann/json.hpp>

#include <base_units.h>
#include <eda_item.h>
#include <eda_shape.h>
#include <eda_text.h>
#include <gal/color4d.h>
#include <gal/graphics_abstraction_layer.h>
#include <geometry/eda_angle.h>
#include <io/io_mgr.h>
#include <ki_exception.h>
#include <kiid.h>
#include <layer_ids.h>
#include <lib_id.h>
#include <lib_symbol.h>
#include <lib_symbol_library_manager.h>
#include <math/util.h>
#include <pin_type.h>
#include <sch_base_frame.h>
#include <sch_commit.h>
#include <sch_draw_panel.h>
#include <sch_field.h>
#include <sch_io/sch_io.h>
#include <sch_io/sch_io_mgr.h>
#include <sch_pin.h>
#include <sch_screen.h>
#include <sch_shape.h>
#include <sch_text.h>
#include <sch_textbox.h>
#include <sch_view.h>
#include <string_utils.h>
#include <stroke_params.h>
#include <symbol_check_finding.h>
#include <symbol_edit_frame.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>
#include <tools/sch_selection.h>
#include <tools/sch_selection_tool.h>
#include <view/view.h>

#include "collab_common.h"
#include "open_gate.h"
#include "symedit_common.h"

using namespace emscripten;
using json = nlohmann::json;
using cc_symedit::utf8;
using KIGFX::COLOR4D;
using namespace cc_symedit;   // the records' readers and appliers (symedit_common.h)

namespace
{

// ── the frame, the gates, the session ─────────────────────────────────────────────────

SYMBOL_EDIT_FRAME* symEditFrame()
{
    return wxTheApp ? dynamic_cast<SYMBOL_EDIT_FRAME*>( wxTheApp->GetTopWindow() ) : nullptr;
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

/** The symbol editor frame when it may be asked anything: none while an open is in flight or a dialog is up. */
SYMBOL_EDIT_FRAME* readyFrame()
{
    SYMBOL_EDIT_FRAME* fr = symEditFrame();

    if( !fr || pcbjam_open::busy() || dialogUp() )
        return nullptr;

    return fr;
}

/** What the page last opened: its token (echoed by the state) and the door it came through. */
struct SESSION
{
    std::string token;
    std::string mode = "library";
};

SESSION& session()
{
    static SESSION s;
    return s;
}

std::string dumpJson( const json& aValue )
{
    return aValue.dump( -1, ' ', false, json::error_handler_t::replace );
}



// ── items (spec 7.2) ──────────────────────────────────────────────────────────────────

/** The record kind of an item of a symbol, or null for one the records do not name. */
const char* kindOf( const SCH_ITEM& aItem )
{
    switch( aItem.Type() )
    {
    case SCH_PIN_T:     return "pin";
    case SCH_FIELD_T:   return "field";
    case SCH_TEXT_T:    return "text";
    case SCH_TEXTBOX_T: return "textbox";
    case SCH_SHAPE_T:   return "shape";
    default:            return nullptr;
    }
}

/** A field's record name: KiCad's canonical name for the five mandatory ones, its own else. */
wxString fieldName( const SCH_FIELD& aField )
{
    return aField.IsMandatory() ? aField.GetCanonicalName() : aField.GetName();
}


json itemJson( const SCH_ITEM& aItem )
{
    json out = json::object();
    const char* kind = kindOf( aItem );

    if( !kind )
        return out;

    out["kind"] = kind;
    out["uuid"] = utf8( aItem.m_Uuid.AsString() );
    out["unit"] = aItem.GetUnit();
    out["bodyStyle"] = aItem.GetBodyStyle();

    switch( aItem.Type() )
    {
    case SCH_PIN_T:
    {
        const SCH_PIN& pin = static_cast<const SCH_PIN&>( aItem );
        const VECTOR2I pos = pin.GetPosition();

        out["number"] = utf8( pin.GetNumber() );
        out["name"] = utf8( pin.GetName() );
        out["type"] = nameOf( PIN_TYPES, pin.GetType(), "unspecified" );
        out["shape"] = nameOf( PIN_SHAPES, pin.GetShape(), "line" );
        out["x"] = mmOf( pos.x );
        out["y"] = mmOf( -pos.y );
        out["orientation"] = nameOf( PIN_ORIENTATIONS, pin.GetOrientation(), "right" );
        out["length"] = mmOf( pin.GetLength() );
        out["nameSize"] = mmOf( pin.GetNameTextSize() );
        out["numberSize"] = mmOf( pin.GetNumberTextSize() );
        out["visible"] = pin.IsVisible();

        json alts = json::array();

        for( const auto& [name, alt] : pin.GetAlternates() )
        {
            json a = json::object();
            a["name"] = utf8( name );
            a["type"] = nameOf( PIN_TYPES, alt.m_Type, "unspecified" );
            a["shape"] = nameOf( PIN_SHAPES, alt.m_Shape, "line" );
            alts.push_back( std::move( a ) );
        }

        out["alternates"] = std::move( alts );
        break;
    }

    case SCH_FIELD_T:
    {
        const SCH_FIELD& field = static_cast<const SCH_FIELD&>( aItem );
        const VECTOR2I   pos = field.GetPosition();

        out["name"] = utf8( fieldName( field ) );
        textJson( out, field );
        out["x"] = mmOf( pos.x );
        out["y"] = mmOf( -pos.y );
        out["showName"] = field.IsNameShown();
        break;
    }

    case SCH_TEXT_T:
        textItemJson( out, static_cast<const SCH_TEXT&>( aItem ) );
        break;

    case SCH_TEXTBOX_T:
        textBoxItemJson( out, static_cast<const SCH_TEXTBOX&>( aItem ) );
        break;

    case SCH_SHAPE_T:
        shapeItemJson( out, static_cast<const SCH_SHAPE&>( aItem ) );
        break;

    default:
        break;
    }

    return out;
}

/** Every item of the symbol the records name, by uuid. */
std::map<std::string, SCH_ITEM*> itemsByUuid( LIB_SYMBOL* aSymbol )
{
    std::map<std::string, SCH_ITEM*> out;

    for( SCH_ITEM& item : aSymbol->GetDrawItems() )
    {
        if( kindOf( item ) )
            out[utf8( item.m_Uuid.AsString() )] = &item;
    }

    return out;
}


/** readItemSetBounded with aSymbol's unit and body style counts (at least 1 each). */
bool readItemSet( const json& aSet, const LIB_SYMBOL* aSymbol, ITEM_SET& aOut )
{
    return readItemSetBounded( aSet, std::max( 1, aSymbol->GetUnitCount() ),
                               std::max( 1, aSymbol->GetBodyStyleCount() ), aOut );
}

/** Does every key of aSet fit aItem's kind (and its shape's point count)? */
bool setFits( const ITEM_SET& aSet, const SCH_ITEM& aItem )
{
    const char* kind = kindOf( aItem );

    if( !kind )
        return false;

    const std::set<std::string>& allowed = editableKeys( kind );

    for( const std::string& key : aSet.keys )
    {
        if( !allowed.count( key ) )
            return false;
    }

    if( aItem.Type() == SCH_FIELD_T && aSet.name
            && static_cast<const SCH_FIELD&>( aItem ).IsMandatory() )
    {
        return false;   // the five mandatory fields keep KiCad's names
    }

    if( aItem.Type() == SCH_FIELD_T && aSet.name && aSet.name->IsEmpty() )
        return false;

    if( aItem.Type() == SCH_SHAPE_T && aSet.points
            && !pointsFit( shapeNameOf( static_cast<const SCH_SHAPE&>( aItem ) ),
                           aSet.points->size() ) )
    {
        return false;
    }

    return true;
}

/** Is aName taken by a field of aSymbol other than aExcept (any casing, KiCad's mandatory names included)? */
bool fieldNameTaken( LIB_SYMBOL* aSymbol, const wxString& aName, const SCH_FIELD* aExcept )
{
    std::vector<SCH_FIELD*> fields;
    aSymbol->GetFields( fields );

    for( const SCH_FIELD* f : fields )
    {
        if( f == aExcept )
            continue;

        if( f->GetName().CmpNoCase( aName ) == 0 || f->GetCanonicalName().CmpNoCase( aName ) == 0 )
            return true;
    }

    static const char* const mandatory[] = { "Reference", "Value", "Footprint", "Datasheet",
                                             "Description" };

    for( const char* name : mandatory )
    {
        if( aName.CmpNoCase( wxString::FromUTF8( name ) ) == 0 )
            return true;
    }

    return false;
}


/**
 * The pin dialog's synchronized-pins rule (SYMBOL_EDITOR_PIN_TOOL::EditPinProperties): with
 * KiCad's synced pins mode on, the same pin of the other units (one per unit, matched on the
 * pin as it was: position, orientation, type, visibility and name) takes aPin's new
 * properties, and a pin made common to every unit or body style replaces its twins. A twin
 * removed is not touched again (KiCad's own loop reads it after removing it).
 */
void syncPins( SYMBOL_EDIT_FRAME* aFrame, LIB_SYMBOL* aSymbol, SCH_PIN* aPin, const SCH_PIN& aOriginal )
{
    if( !aFrame->SynchronizePins() || aPin->IsNew() )
        return;

    std::vector<bool> got_unit( aSymbol->GetUnitCount() + 1 );

    if( aPin->GetUnit() < 0 || static_cast<size_t>( aPin->GetUnit() ) >= got_unit.size() )
        return;

    got_unit[static_cast<size_t>( aPin->GetUnit() )] = true;

    for( SCH_PIN* other : aSymbol->GetPins() )
    {
        if( other == aPin )
            continue;

        if( other->GetUnit() < 0 || static_cast<size_t>( other->GetUnit() ) >= got_unit.size()
                || got_unit[static_cast<size_t>( other->GetUnit() )] )
        {
            continue;
        }

        if( other->GetPosition() == aOriginal.GetPosition()
                && other->GetOrientation() == aOriginal.GetOrientation()
                && other->GetType() == aOriginal.GetType()
                && other->IsVisible() == aOriginal.IsVisible()
                && other->GetName() == aOriginal.GetName() )
        {
            const size_t otherUnit = static_cast<size_t>( other->GetUnit() );

            if( aPin->GetBodyStyle() == 0 && ( !aPin->GetUnit() || other->GetUnit() == aPin->GetUnit() ) )
            {
                got_unit[otherUnit] = true;
                aSymbol->RemoveDrawItem( other );
                continue;
            }

            if( aPin->GetUnit() == 0
                    && ( !aPin->GetBodyStyle() || other->GetBodyStyle() == aPin->GetBodyStyle() ) )
            {
                got_unit[otherUnit] = true;
                aSymbol->RemoveDrawItem( other );
                continue;
            }

            if( other->GetBodyStyle() == aPin->GetBodyStyle() )
            {
                other->ChangeLength( aPin->GetLength() );

                // Must be done after ChangeLength(), which can alter the position
                other->SetPosition( aPin->GetPosition() );
                other->SetShape( aPin->GetShape() );
            }

            other->SetOrientation( aPin->GetOrientation() );
            other->SetType( aPin->GetType() );
            other->SetVisible( aPin->IsVisible() );
            other->SetName( aPin->GetName() );
            other->SetNameTextSize( aPin->GetNameTextSize() );
            other->SetNumberTextSize( aPin->GetNumberTextSize() );

            got_unit[otherUnit] = true;
        }
    }
}

/** Applies aSet to one item (kinds checked already). */
void applyItemSet( SYMBOL_EDIT_FRAME* aFrame, LIB_SYMBOL* aSymbol, SCH_ITEM* aItem,
                   const ITEM_SET& aSet, bool aSync )
{
    if( aSet.unit )
        aItem->SetUnit( *aSet.unit );

    if( aSet.bodyStyle )
        aItem->SetBodyStyle( *aSet.bodyStyle );

    switch( aItem->Type() )
    {
    case SCH_PIN_T:
    {
        SCH_PIN*      pin = static_cast<SCH_PIN*>( aItem );
        const SCH_PIN original( *pin );
        VECTOR2I      pos = pin->GetPosition();

        if( aSet.number )
            pin->SetNumber( *aSet.number );

        if( aSet.name )
            pin->SetName( *aSet.name );

        if( aSet.type )
            pin->SetType( *aSet.type );

        if( aSet.pinShape )
            pin->SetShape( *aSet.pinShape );

        if( aSet.orientation )
            pin->SetOrientation( *aSet.orientation );

        if( aSet.length )
            pin->SetLength( iuOf( *aSet.length ) );

        if( aSet.x )
            pos.x = iuOf( *aSet.x );

        if( aSet.y )
            pos.y = -iuOf( *aSet.y );

        if( aSet.x || aSet.y )
            pin->SetPosition( pos );

        if( aSet.nameSize )
            pin->SetNameTextSize( iuOf( *aSet.nameSize ) );

        if( aSet.numberSize )
            pin->SetNumberTextSize( iuOf( *aSet.numberSize ) );

        if( aSet.visible )
            pin->SetVisible( *aSet.visible );

        if( aSet.alternates )
        {
            std::map<wxString, SCH_PIN::ALT>& alts = pin->GetAlternates();
            alts.clear();

            for( const SCH_PIN::ALT& alt : *aSet.alternates )
                alts[alt.m_Name] = alt;
        }

        if( aSync )
            syncPins( aFrame, aSymbol, pin, original );

        break;
    }

    case SCH_FIELD_T:
    {
        SCH_FIELD* field = static_cast<SCH_FIELD*>( aItem );
        VECTOR2I   pos = field->GetPosition();

        if( aSet.name )
            field->SetName( *aSet.name );

        applyText( *field, aSet );

        if( aSet.showName )
            field->SetNameShown( *aSet.showName );

        if( aSet.x )
            pos.x = iuOf( *aSet.x );

        if( aSet.y )
            pos.y = -iuOf( *aSet.y );

        if( aSet.x || aSet.y )
            field->SetPosition( pos );

        break;
    }

    case SCH_TEXT_T:
        applyTextItem( *static_cast<SCH_TEXT*>( aItem ), aSet );
        break;

    case SCH_TEXTBOX_T:
        applyTextBoxItem( *static_cast<SCH_TEXTBOX*>( aItem ), aSet );
        break;

    case SCH_SHAPE_T:
        applyShapeItem( *static_cast<SCH_SHAPE*>( aItem ), aSet );
        break;

    default:
        break;
    }
}

/** A new item (spec 7.2's NewItem): its kind, its page-made uuid, every key of its kind. */
struct NEW_ITEM
{
    std::string kind;
    std::string uuid;
    std::string shape;   // a shape's: rectangle, circle, arc, polyline, bezier
    ITEM_SET    set;
};

/** The keys a new item of aKind must carry in full (spec 7.2). */
bool hasFullKeys( const NEW_ITEM& aItem )
{
    std::set<std::string> need = editableKeys( aItem.kind );

    if( aItem.kind == "field" )
    {
        need.insert( "unit" );
        need.insert( "bodyStyle" );
    }
    else if( aItem.kind == "text" )
    {
        need.insert( "name" );
        need.insert( "showName" );
    }

    for( const std::string& key : need )
    {
        if( !aItem.set.keys.count( key ) )
            return false;
    }

    for( const std::string& key : aItem.set.keys )
    {
        if( !need.count( key ) )
            return false;
    }

    return true;
}

SCH_ITEM* makeItem( LIB_SYMBOL* aSymbol, const NEW_ITEM& aNew )
{
    SCH_ITEM* item = nullptr;

    if( aNew.kind == "pin" )
    {
        item = new SCH_PIN( aSymbol );
    }
    else if( aNew.kind == "field" )
    {
        SCH_FIELD* field = new SCH_FIELD( aSymbol, FIELD_T::USER, *aNew.set.name );
        field->SetOrdinal( aSymbol->GetNextFieldOrdinal() );
        item = field;
    }
    else if( aNew.kind == "text" )
    {
        item = new SCH_TEXT( VECTOR2I( 0, 0 ), wxEmptyString, LAYER_DEVICE );
    }
    else if( aNew.kind == "textbox" )
    {
        item = new SCH_TEXTBOX( LAYER_DEVICE );
    }
    else if( aNew.kind == "shape" )
    {
        SHAPE_T type = SHAPE_T::POLY;

        if( aNew.shape == "rectangle" )
            type = SHAPE_T::RECTANGLE;
        else if( aNew.shape == "circle" )
            type = SHAPE_T::CIRCLE;
        else if( aNew.shape == "arc" )
            type = SHAPE_T::ARC;
        else if( aNew.shape == "bezier" )
            type = SHAPE_T::BEZIER;

        item = new SCH_SHAPE( type, LAYER_DEVICE );
    }

    if( item )
        const_cast<KIID&>( item->m_Uuid ) = KIID( wxString::FromUTF8( aNew.uuid.c_str() ) );

    return item;
}

// ── the symbol (spec 7.3) ─────────────────────────────────────────────────────────────

json bodyStylesJson( const LIB_SYMBOL* aSymbol )
{
    json b = json::object();
    json names = json::array();

    for( const wxString& n : aSymbol->GetBodyStyleNames() )
        names.push_back( utf8( n ) );

    if( aSymbol->HasDeMorganBodyStyles() )
        b["mode"] = "demorgan";
    else if( aSymbol->GetBodyStyleNames().size() > 1 )
        b["mode"] = "named";
    else
        b["mode"] = "one";

    b["names"] = std::move( names );
    return b;
}

const char* fieldKindOf( const SCH_FIELD& aField )
{
    switch( aField.GetId() )
    {
    case FIELD_T::REFERENCE:   return "reference";
    case FIELD_T::VALUE:       return "value";
    case FIELD_T::FOOTPRINT:   return "footprint";
    case FIELD_T::DATASHEET:   return "datasheet";
    case FIELD_T::DESCRIPTION: return "description";
    default:                   return "user";
    }
}

/**
 * sy4-e: a placed part opened in the editor carries its designator in the reference field
 * (LoadSymbolFromSchematic's copy of the instance's fields, "U1"); its prefix is the field's
 * text less the designator's number (KiCad's UTIL::GetRefDesPrefix, "U"). A library symbol's
 * field is its prefix already.
 */
wxString refPrefixOf( const LIB_SYMBOL* aSymbol, bool aPlaced )
{
    const wxString text = aSymbol->GetReferenceField().GetText();
    return aPlaced ? UTIL::GetRefDesPrefix( text ) : text;
}

/** The designator's number a placed part keeps when its prefix changes ("1" of "U1"). */
wxString refSuffixOf( const LIB_SYMBOL* aSymbol )
{
    const wxString text = aSymbol->GetReferenceField().GetText();
    return text.Mid( UTIL::GetRefDesPrefix( text ).length() );
}

json symbolPropsJson( LIB_SYMBOL* aSymbol, bool aPlaced = false )
{
    json out = json::object();

    out["name"] = utf8( aSymbol->GetName() );
    out["refPrefix"] = utf8( refPrefixOf( aSymbol, aPlaced ) );
    out["value"] = utf8( aSymbol->GetValueField().GetText() );
    out["footprint"] = utf8( aSymbol->GetFootprintField().GetText() );
    out["datasheet"] = utf8( aSymbol->GetDatasheetField().GetText() );
    out["description"] = utf8( aSymbol->GetDescriptionField().GetText() );
    out["keywords"] = utf8( aSymbol->GetKeyWords() );

    json filters = json::array();

    for( const wxString& f : aSymbol->GetFPFilters() )
        filters.push_back( utf8( f ) );

    out["fpFilters"] = std::move( filters );

    std::vector<SCH_FIELD*> ordered;
    aSymbol->GetFields( ordered );

    json fields = json::array();

    for( const SCH_FIELD* f : ordered )
    {
        json j = json::object();
        j["name"] = utf8( fieldName( *f ) );
        j["value"] = utf8( f->GetText() );
        j["visible"] = f->IsVisible();
        j["kind"] = fieldKindOf( *f );
        fields.push_back( std::move( j ) );
    }

    out["fields"] = std::move( fields );
    out["unitCount"] = std::max( 1, aSymbol->GetUnitCount() );
    out["unitsInterchangeable"] = !aSymbol->UnitsLocked();

    json unitNames = json::object();

    for( const auto& [unit, name] : aSymbol->GetUnitDisplayNames() )
        unitNames[std::to_string( unit )] = utf8( name );

    out["unitNames"] = std::move( unitNames );
    out["bodyStyles"] = bodyStylesJson( aSymbol );
    out["power"] = aSymbol->IsGlobalPower() ? "global" : ( aSymbol->IsLocalPower() ? "local" : "none" );
    out["pinNameOffset"] = mmOf( aSymbol->GetPinNameOffset() );
    out["showPinNames"] = aSymbol->GetShowPinNames();
    out["showPinNumbers"] = aSymbol->GetShowPinNumbers();
    out["pinNamesInside"] = aSymbol->GetPinNameOffset() != 0;
    out["inBom"] = !aSymbol->GetExcludedFromBOM();
    out["onBoard"] = !aSymbol->GetExcludedFromBoard();
    out["inSim"] = !aSymbol->GetExcludedFromSim();
    out["inPosFiles"] = !aSymbol->GetExcludedFromPosFiles();

    json groups = json::array();

    for( const std::set<wxString>& group : aSymbol->JumperPinGroups() )
    {
        json g = json::array();

        for( const wxString& pin : group )
            g.push_back( utf8( pin ) );

        groups.push_back( std::move( g ) );
    }

    json jumpers = json::object();
    jumpers["duplicateNumbersAreJumpers"] = aSymbol->GetDuplicatePinNumbersAreJumpers();
    jumpers["groups"] = std::move( groups );
    out["jumpers"] = std::move( jumpers );
    out["parent"] = utf8( cc_symedit::parentName( aSymbol ) );
    return out;
}

struct FIELD_EDIT
{
    wxString                name;
    std::optional<wxString> value;
    std::optional<bool>     visible;
};

struct FROM_PARENT
{
    bool               all = false;
    std::set<wxString> fields;
    bool               resetEmpty = false, resetVisibility = false, resetEffects = false;
    bool               resetPositions = false, removeExtra = false;
};

struct BODY_STYLES
{
    std::string           mode;
    std::vector<wxString> names;
};

/** One symbol.set (spec 7.3's SymbolSet), read whole before anything is queued. */
struct SYMBOL_SET
{
    std::optional<wxString> name, refPrefix, value, footprint, datasheet, description, keywords;
    std::optional<std::vector<wxString>> fpFilters;
    std::vector<FIELD_EDIT>       fields;
    std::optional<FIELD_EDIT>     addField;
    std::optional<wxString>       removeField;
    std::optional<int>            unitCount;
    std::optional<bool>           unitsInterchangeable;
    std::optional<std::map<int, wxString>> unitNames;
    std::optional<BODY_STYLES>    bodyStyles;
    std::optional<std::string>    power;
    std::optional<double>         pinNameOffset;
    std::optional<bool>           showPinNames, showPinNumbers, pinNamesInside;
    std::optional<bool>           inBom, onBoard, inSim, inPosFiles;
    std::optional<bool>           duplicateNumbersAreJumpers;
    std::optional<std::vector<std::set<wxString>>> jumperGroups;
    std::optional<FROM_PARENT>    fromParent;
    bool                          flatten = false;
};

bool readSymbolSet( const json& aSet, LIB_SYMBOL* aSymbol, SYMBOL_SET& aOut )
{
    if( !aSet.is_object() || aSet.empty() )
        return false;

    std::set<wxString> named;   // fields named by a key of their own or in fields[]

    for( auto it = aSet.begin(); it != aSet.end(); ++it )
    {
        const std::string& key = it.key();
        const json&        v = it.value();

        if( key == "name" )
        {
            if( !( aOut.name = textOf( v, 1, TEXT_MAX ) ) )
                return false;
        }
        else if( key == "refPrefix" || key == "value" || key == "footprint" || key == "datasheet"
                 || key == "description" || key == "keywords" )
        {
            std::optional<wxString> t = textOf( v, 0, TEXT_MAX );

            if( !t )
                return false;

            if( key == "refPrefix" )
            {
                aOut.refPrefix = t;
                named.insert( wxS( "Reference" ) );
            }
            else if( key == "value" )
            {
                aOut.value = t;
                named.insert( wxS( "Value" ) );
            }
            else if( key == "footprint" )
            {
                aOut.footprint = t;
                named.insert( wxS( "Footprint" ) );
            }
            else if( key == "datasheet" )
            {
                aOut.datasheet = t;
                named.insert( wxS( "Datasheet" ) );
            }
            else if( key == "description" )
            {
                aOut.description = t;
                named.insert( wxS( "Description" ) );
            }
            else
            {
                aOut.keywords = t;
            }
        }
        else if( key == "fpFilters" )
        {
            if( !v.is_array() || v.size() > 256 )
                return false;

            std::vector<wxString> filters;

            for( const json& f : v )
            {
                std::optional<wxString> t = textOf( f, 1, TEXT_MAX );

                if( !t )
                    return false;

                filters.push_back( *t );
            }

            aOut.fpFilters = std::move( filters );
        }
        else if( key == "fields" )
        {
            if( !v.is_array() || v.size() > 256 )
                return false;

            for( const json& f : v )
            {
                if( !onlyKeys( f, { "name", "value", "visible" } ) || !f.contains( "name" )
                        || f.size() < 2 )
                {
                    return false;
                }

                FIELD_EDIT              edit;
                std::optional<wxString> name = textOf( f["name"], 1, TEXT_MAX );

                if( !name )
                    return false;

                edit.name = *name;

                if( f.contains( "value" ) && !( edit.value = textOf( f["value"], 0, TEXT_MAX ) ) )
                    return false;

                if( f.contains( "visible" ) && !( edit.visible = boolOf( f["visible"] ) ) )
                    return false;

                // The field exists (by KiCad's canonical name or its own).
                bool found = false;
                std::vector<SCH_FIELD*> fields;
                aSymbol->GetFields( fields );

                for( const SCH_FIELD* field : fields )
                {
                    if( fieldName( *field ) == edit.name )
                        found = true;
                }

                if( !found || !named.insert( edit.name ).second )
                    return false;

                aOut.fields.push_back( std::move( edit ) );
            }
        }
        else if( key == "addField" )
        {
            if( !onlyKeys( v, { "name", "value", "visible" } )
                    || !hasKeys( v, { "name", "value", "visible" } ) )
            {
                return false;
            }

            FIELD_EDIT add;
            std::optional<wxString> name = textOf( v["name"], 1, TEXT_MAX );

            if( !name || fieldNameTaken( aSymbol, *name, nullptr ) )
                return false;

            add.name = *name;

            if( !( add.value = textOf( v["value"], 0, TEXT_MAX ) )
                    || !( add.visible = boolOf( v["visible"] ) ) )
            {
                return false;
            }

            aOut.addField = std::move( add );
        }
        else if( key == "removeField" )
        {
            std::optional<wxString> name = textOf( v, 1, TEXT_MAX );

            if( !name )
                return false;

            SCH_FIELD* field = aSymbol->GetField( *name );

            if( !field || field->IsMandatory() )
                return false;

            aOut.removeField = name;
        }
        else if( key == "unitCount" )
        {
            if( !( aOut.unitCount = intOf( v, 1, UNITS_MAX ) ) )
                return false;
        }
        else if( key == "unitsInterchangeable" || key == "showPinNames" || key == "showPinNumbers"
                 || key == "pinNamesInside" || key == "inBom" || key == "onBoard" || key == "inSim"
                 || key == "inPosFiles" )
        {
            std::optional<bool> b = boolOf( v );

            if( !b )
                return false;

            if( key == "unitsInterchangeable" )
                aOut.unitsInterchangeable = b;
            else if( key == "showPinNames" )
                aOut.showPinNames = b;
            else if( key == "showPinNumbers" )
                aOut.showPinNumbers = b;
            else if( key == "pinNamesInside" )
                aOut.pinNamesInside = b;
            else if( key == "inBom" )
                aOut.inBom = b;
            else if( key == "onBoard" )
                aOut.onBoard = b;
            else if( key == "inSim" )
                aOut.inSim = b;
            else
                aOut.inPosFiles = b;
        }
        else if( key == "unitNames" )
        {
            if( !v.is_object() || v.size() > UNITS_MAX )
                return false;

            std::map<int, wxString> names;

            for( auto n = v.begin(); n != v.end(); ++n )
            {
                const std::string& k = n.key();

                if( k.empty() || k.size() > 3 || k[0] == '0'
                        || k.find_first_not_of( "0123456789" ) != std::string::npos )
                {
                    return false;
                }

                std::optional<wxString> t = textOf( n.value(), 1, TEXT_MAX );

                if( !t )
                    return false;

                names[std::stoi( k )] = *t;
            }

            aOut.unitNames = std::move( names );
        }
        else if( key == "bodyStyles" )
        {
            if( !onlyKeys( v, { "mode", "names" } ) || !hasKeys( v, { "mode", "names" } )
                    || !v["mode"].is_string() || !v["names"].is_array() || v["names"].size() > 64 )
            {
                return false;
            }

            BODY_STYLES styles;
            styles.mode = v["mode"].get<std::string>();

            for( const json& n : v["names"] )
            {
                std::optional<wxString> t = textOf( n, 1, TEXT_MAX );

                if( !t )
                    return false;

                styles.names.push_back( *t );
            }

            if( styles.mode == "named" )
            {
                if( styles.names.size() < 2 )
                    return false;
            }
            else if( styles.mode == "one" || styles.mode == "demorgan" )
            {
                if( !styles.names.empty() )
                    return false;
            }
            else
            {
                return false;
            }

            aOut.bodyStyles = std::move( styles );
        }
        else if( key == "power" )
        {
            if( !v.is_string() )
                return false;

            const std::string p = v.get<std::string>();

            if( p != "none" && p != "global" && p != "local" )
                return false;

            aOut.power = p;
        }
        else if( key == "pinNameOffset" )
        {
            if( !( aOut.pinNameOffset = numberOf( v, 0.0, 50.0 ) ) )
                return false;
        }
        else if( key == "jumpers" )
        {
            if( !onlyKeys( v, { "duplicateNumbersAreJumpers", "groups" } ) || v.empty() )
                return false;

            if( v.contains( "duplicateNumbersAreJumpers" )
                    && !( aOut.duplicateNumbersAreJumpers = boolOf( v["duplicateNumbersAreJumpers"] ) ) )
            {
                return false;
            }

            if( v.contains( "groups" ) )
            {
                const json& g = v["groups"];

                if( !g.is_array() || g.size() > 256 )
                    return false;

                std::set<wxString> pinNumbers;

                for( const SCH_PIN* pin : aSymbol->GetGraphicalPins( 0, 0 ) )
                    pinNumbers.insert( pin->GetNumber() );

                std::vector<std::set<wxString>> groups;

                for( const json& group : g )
                {
                    if( !group.is_array() || group.size() > 256 )
                        return false;

                    std::set<wxString>& out = groups.emplace_back();

                    for( const json& p : group )
                    {
                        std::optional<wxString> t = textOf( p, 1, TEXT_MAX );

                        // The dialog's rule: every pin of a group exists in the symbol.
                        if( !t || !pinNumbers.count( *t ) )
                            return false;

                        out.insert( *t );
                    }
                }

                aOut.jumperGroups = std::move( groups );
            }
        }
        else if( key == "fromParent" )
        {
            if( !onlyKeys( v, { "fields", "resetEmpty", "resetVisibility", "resetEffects",
                                "resetPositions", "removeExtra" } )
                    || !hasKeys( v, { "fields", "resetEmpty", "resetVisibility", "resetEffects",
                                      "resetPositions", "removeExtra" } )
                    || !aSymbol->IsDerived() )
            {
                return false;
            }

            FROM_PARENT fp;
            const json& f = v["fields"];

            if( f.is_string() && f.get<std::string>() == "all" )
            {
                fp.all = true;
            }
            else if( f.is_array() && !f.empty() && f.size() <= 256 )
            {
                for( const json& n : f )
                {
                    std::optional<wxString> t = textOf( n, 1, TEXT_MAX );

                    if( !t )
                        return false;

                    fp.fields.insert( *t );
                }
            }
            else
            {
                return false;
            }

            std::optional<bool> a = boolOf( v["resetEmpty"] ), b = boolOf( v["resetVisibility"] ),
                                c = boolOf( v["resetEffects"] ), d = boolOf( v["resetPositions"] ),
                                e = boolOf( v["removeExtra"] );

            if( !a || !b || !c || !d || !e )
                return false;

            fp.resetEmpty = *a;
            fp.resetVisibility = *b;
            fp.resetEffects = *c;
            fp.resetPositions = *d;
            fp.removeExtra = *e;
            aOut.fromParent = std::move( fp );
        }
        else if( key == "flatten" )
        {
            if( !v.is_boolean() || !v.get<bool>() || !aSymbol->IsDerived() )
                return false;

            aOut.flatten = true;
        }
        else
        {
            return false;   // "parent" and every other key
        }
    }

    // One change per field: a field named twice (by its key and in fields[]) is refused
    // above; a removed field may not also be edited or added again.
    if( aOut.removeField && named.count( *aOut.removeField ) )
        return false;

    if( aOut.removeField && aOut.addField && aOut.removeField->CmpNoCase( aOut.addField->name ) == 0 )
        return false;

    if( aOut.unitNames )
    {
        const int units = aOut.unitCount ? *aOut.unitCount : std::max( 1, aSymbol->GetUnitCount() );

        for( const auto& [unit, name] : *aOut.unitNames )
        {
            if( unit < 1 || unit > units )
                return false;
        }
    }

    return true;
}

/** DIALOG_UPDATE_SYMBOL_FIELDS's body (Take fields from the parent), on the symbol in a commit. */
void takeFieldsFromParent( LIB_SYMBOL* aSymbol, const FROM_PARENT& aFrom )
{
    std::shared_ptr<LIB_SYMBOL> parent = aSymbol->GetParent().lock();

    if( !parent )
        return;

    std::unique_ptr<LIB_SYMBOL> flattenedParent = parent->Flatten();

    if( !flattenedParent )
        return;

    std::set<wxString> update = aFrom.fields;

    if( aFrom.all )
    {
        std::vector<SCH_FIELD*> mine, theirs;
        aSymbol->GetFields( mine );
        flattenedParent->GetFields( theirs );

        for( const SCH_FIELD* f : mine )
            update.insert( f->GetName() );

        for( const SCH_FIELD* f : theirs )
            update.insert( f->GetName() );
    }

    std::vector<SCH_FIELD> fields;
    std::vector<SCH_FIELD> result;
    aSymbol->CopyFields( fields );

    for( SCH_FIELD& field : fields )
    {
        bool       copy = true;
        SCH_FIELD* parentField = nullptr;

        if( update.count( field.GetName() ) )
        {
            if( field.IsMandatory() )
                parentField = flattenedParent->GetField( field.GetId() );
            else
                parentField = flattenedParent->GetField( field.GetName() );

            if( parentField )
            {
                // Taking a field from the parent takes its text; an empty one only when asked.
                bool resetText = parentField->GetText().IsEmpty() ? aFrom.resetEmpty : true;

                if( resetText )
                    field.SetText( parentField->GetText() );

                if( aFrom.resetVisibility )
                {
                    field.SetVisible( parentField->IsVisible() );
                    field.SetNameShown( parentField->IsNameShown() );
                }

                if( aFrom.resetEffects )
                {
                    // Careful: the visible bit and position are also set by SetAttributes()
                    bool     visible = field.IsVisible();
                    VECTOR2I pos = field.GetPosition();

                    field.SetAttributes( *parentField );

                    field.SetVisible( visible );
                    field.SetPosition( pos );
                }

                if( aFrom.resetPositions )
                    field.SetTextPos( parentField->GetTextPos() );
            }
            else if( aFrom.removeExtra )
            {
                copy = false;
            }
        }

        if( copy )
            result.emplace_back( std::move( field ) );
    }

    std::vector<SCH_FIELD*> parentFields;
    flattenedParent->GetFields( parentFields );

    for( SCH_FIELD* parentField : parentFields )
    {
        if( !update.count( parentField->GetName() ) )
            continue;

        if( !aSymbol->GetField( parentField->GetName() ) )
        {
            result.emplace_back( aSymbol, FIELD_T::USER );
            SCH_FIELD* newField = &result.back();

            newField->SetName( parentField->GetCanonicalName() );
            newField->SetText( parentField->GetText() );
            newField->SetAttributes( *parentField );   // Includes visible bit and position
        }
    }

    aSymbol->SetFields( result );
}

/** A symbol.set on the symbol, in DIALOG_LIB_SYMBOL_PROPERTIES::TransferDataFromWindow's order. */
void applySymbolSet( LIB_SYMBOL* aSymbol, const SYMBOL_SET& aSet )
{
    auto setField =
            [&]( FIELD_T aId, const std::optional<wxString>& aText )
            {
                if( aText )
                {
                    if( SCH_FIELD* f = aSymbol->GetField( aId ) )
                        f->SetText( *aText );
                }
            };

    setField( FIELD_T::REFERENCE, aSet.refPrefix );
    setField( FIELD_T::VALUE, aSet.value );
    setField( FIELD_T::FOOTPRINT, aSet.footprint );
    setField( FIELD_T::DATASHEET, aSet.datasheet );
    setField( FIELD_T::DESCRIPTION, aSet.description );

    for( const FIELD_EDIT& edit : aSet.fields )
    {
        std::vector<SCH_FIELD*> fields;
        aSymbol->GetFields( fields );

        for( SCH_FIELD* field : fields )
        {
            if( fieldName( *field ) != edit.name )
                continue;

            if( edit.value )
                field->SetText( *edit.value );

            if( edit.visible )
                field->SetVisible( *edit.visible );
        }
    }

    if( aSet.removeField )
    {
        if( SCH_FIELD* field = aSymbol->GetField( *aSet.removeField ) )
            aSymbol->RemoveField( field );
    }

    if( aSet.addField && !fieldNameTaken( aSymbol, aSet.addField->name, nullptr ) )
    {
        // As the properties dialog adds a row: at the reference's place and size, the next ordinal.
        SCH_FIELD* added = new SCH_FIELD( aSymbol, FIELD_T::USER, aSet.addField->name );
        const SCH_FIELD& reference = aSymbol->GetReferenceField();

        added->SetText( *aSet.addField->value );
        added->SetVisible( *aSet.addField->visible );
        added->SetTextSize( reference.GetTextSize() );
        added->SetPosition( reference.GetPosition() );
        added->SetOrdinal( aSymbol->GetNextFieldOrdinal() );
        aSymbol->AddField( added );
    }

    if( aSet.name )
        aSymbol->SetName( *aSet.name );

    if( aSet.keywords )
        aSymbol->SetKeyWords( *aSet.keywords );

    if( aSet.unitCount )
        aSymbol->SetUnitCount( *aSet.unitCount, true );

    if( aSet.unitsInterchangeable || aSet.unitCount )
    {
        const bool interchangeable =
                aSet.unitsInterchangeable ? *aSet.unitsInterchangeable : !aSymbol->UnitsLocked();
        aSymbol->LockUnits( aSymbol->GetUnitCount() > 1 && !interchangeable );
    }

    if( aSet.unitNames )
    {
        aSymbol->GetUnitDisplayNames().clear();

        for( const auto& [unit, name] : *aSet.unitNames )
        {
            if( unit <= aSymbol->GetUnitCount() )
                aSymbol->GetUnitDisplayNames()[unit] = name;
        }
    }

    if( aSet.bodyStyles )
    {
        if( aSet.bodyStyles->mode == "one" )
        {
            aSymbol->SetHasDeMorganBodyStyles( false );
            aSymbol->SetBodyStyleCount( 1, false, false );
            aSymbol->SetBodyStyleNames( {} );
        }
        else if( aSet.bodyStyles->mode == "demorgan" )
        {
            aSymbol->SetHasDeMorganBodyStyles( true );
            aSymbol->SetBodyStyleCount( 2, false, true );
            aSymbol->SetBodyStyleNames( {} );
        }
        else
        {
            aSymbol->SetHasDeMorganBodyStyles( false );
            aSymbol->SetBodyStyleCount( (int) aSet.bodyStyles->names.size(), true, true );
            aSymbol->SetBodyStyleNames( aSet.bodyStyles->names );
        }
    }

    if( aSet.power )
    {
        if( *aSet.power == "local" )
            aSymbol->SetLocalPower();
        else if( *aSet.power == "global" )
            aSymbol->SetGlobalPower();
        else
            aSymbol->SetNormal();
    }

    // The dialog's rule: a power symbol's value is its name.
    if( ( aSet.power || aSet.name ) && aSymbol->IsPower() )
        aSymbol->GetValueField().SetText( aSymbol->GetName() );

    if( aSet.inSim )
        aSymbol->SetExcludedFromSim( !*aSet.inSim );

    if( aSet.inBom )
        aSymbol->SetExcludedFromBOM( !*aSet.inBom );

    if( aSet.onBoard )
        aSymbol->SetExcludedFromBoard( !*aSet.onBoard );

    if( aSet.inPosFiles )
        aSymbol->SetExcludedFromPosFiles( !*aSet.inPosFiles );

    if( aSet.showPinNumbers )
        aSymbol->SetShowPinNumbers( *aSet.showPinNumbers );

    if( aSet.showPinNames )
        aSymbol->SetShowPinNames( *aSet.showPinNames );

    if( aSet.pinNamesInside || aSet.pinNameOffset )
    {
        const bool inside =
                aSet.pinNamesInside ? *aSet.pinNamesInside : aSymbol->GetPinNameOffset() != 0;

        if( inside )
        {
            int offset = aSet.pinNameOffset ? iuOf( *aSet.pinNameOffset ) : aSymbol->GetPinNameOffset();

            // An offset of 0 reads as "outside", so it is never 0 inside (the dialog's 20 IU).
            aSymbol->SetPinNameOffset( offset == 0 ? 20 : offset );
        }
        else
        {
            aSymbol->SetPinNameOffset( 0 );
        }
    }

    if( aSet.fpFilters )
    {
        wxArrayString filters;

        for( const wxString& f : *aSet.fpFilters )
            filters.Add( f );

        aSymbol->SetFPFilters( filters );
    }

    if( aSet.duplicateNumbersAreJumpers )
        aSymbol->SetDuplicatePinNumbersAreJumpers( *aSet.duplicateNumbersAreJumpers );

    if( aSet.jumperGroups )
        aSymbol->JumperPinGroups() = *aSet.jumperGroups;

    if( aSet.fromParent )
        takeFieldsFromParent( aSymbol, *aSet.fromParent );

    if( aSet.flatten )
    {
        // flattenSymbol's body: the symbol with its parent's drawing folded in, as a root.
        if( std::unique_ptr<LIB_SYMBOL> flat = aSymbol->Flatten() )
        {
            const LIB_ID id = aSymbol->GetLibId();

            *aSymbol = *flat;
            aSymbol->SetParent();   // a root: no argument, as SetParent( nullptr ) also matches EDA_ITEM::SetParent
            aSymbol->SetLibId( id );
        }
    }
}

// ── the state (spec 7.1) ──────────────────────────────────────────────────────────────

json stateJson( SYMBOL_EDIT_FRAME* aFrame )
{
    LIB_SYMBOL* symbol = aFrame ? aFrame->GetCurSymbol() : nullptr;
    json        out = json::object();

    out["token"] = session().token;
    out["open"] = symbol != nullptr;
    out["name"] = symbol ? utf8( symbol->GetName() ) : std::string();
    out["lib"] = aFrame && aFrame->CcHostMode() ? utf8( aFrame->CcHostLib() )
                                                 : ( symbol ? utf8( symbol->GetLibId().GetLibNickname() )
                                                            : std::string() );
    out["mode"] = session().mode;
    out["modified"] = symbol && aFrame->GetScreen() && aFrame->GetScreen()->IsContentModified();
    out["depth"] = aFrame ? aFrame->GetUndoCommandCount() : 0;
    out["unit"] = aFrame ? std::max( 1, aFrame->GetUnit() ) : 1;
    out["unitCount"] = symbol ? std::max( 1, symbol->GetUnitCount() ) : 1;
    out["bodyStyle"] = aFrame ? std::max( 1, aFrame->GetBodyStyle() ) : 1;

    if( symbol )
    {
        out["bodyStyles"] = bodyStylesJson( symbol );
    }
    else
    {
        json b = json::object();
        b["mode"] = "one";
        b["names"] = json::array();
        out["bodyStyles"] = std::move( b );
    }

    out["parent"] = utf8( cc_symedit::parentName( symbol ) );
    out["syncedPins"] = aFrame && aFrame->SynchronizePins();
    return out;
}

/** The open symbol's current object still the one validated, and the page's open unchanged. */
LIB_SYMBOL* liveSymbol( SYMBOL_EDIT_FRAME* aFrame, const std::string& aToken )
{
    if( !aFrame || session().token != aToken )
        return nullptr;

    return aFrame->GetCurSymbol();
}

/** Writes the done file last and whole (a part file renamed into place). */
void writeDone( const std::string& aDir, const char* aName, const json& aBody )
{
    const std::string body = dumpJson( aBody );
    const std::string part = aDir + "/" + aName + ".part";
    const std::string fin = aDir + "/" + aName;

    if( FILE* f = std::fopen( part.c_str(), "wb" ) )
    {
        std::fwrite( body.data(), 1, body.size(), f );
        std::fclose( f );
        std::rename( part.c_str(), fin.c_str() );
    }
}

/** A folder under aRoot (the plot export's rule): no "..", no trailing slash, nothing else. */
bool isRunDir( const std::string& aDir, const std::string& aRoot )
{
    return aDir.size() > aRoot.size() && aDir.compare( 0, aRoot.size(), aRoot ) == 0
           && aDir.find( ".." ) == std::string::npos && aDir.back() != '/'
           && aDir.find( '\0' ) == std::string::npos;
}

std::shared_ptr<cc_symedit::SYMBOL_CHAIN> shareChain( cc_symedit::SYMBOL_CHAIN&& aChain )
{
    return std::make_shared<cc_symedit::SYMBOL_CHAIN>( std::move( aChain ) );
}

/** Hands a chain to the frame's host mode on the apply coroutine (the open's and the new's tail). */
void openChain( std::shared_ptr<cc_symedit::SYMBOL_CHAIN> aChain, const wxString& aLib,
                bool aPlaced, int aUnit, int aBodyStyle, const std::string& aToken,
                const std::string& aMode )
{
    SYMBOL_EDIT_FRAME* frame = symEditFrame();   // re-resolved on the coroutine (S4)

    if( !frame || !aChain || aChain->links.empty() )
        return;

    LIB_SYMBOL*              symbol = aChain->links.front().release();
    std::vector<LIB_SYMBOL*> ancestors;

    for( size_t i = 1; i < aChain->links.size(); ++i )
    {
        LIB_SYMBOL* a = aChain->links[i].release();
        a->SetLibId( LIB_ID( aLib, a->GetName() ) );
        ancestors.push_back( a );
    }

    aChain->links.clear();
    symbol->SetLibId( LIB_ID( aLib, symbol->GetName() ) );

    frame->CcHostOpen( symbol, ancestors, aLib, aPlaced, aUnit, aBodyStyle );

    session().token = aToken;
    session().mode = aMode;
}

} // namespace

// ── exports ───────────────────────────────────────────────────────────────────────────

// symedit.open: {text, name, lib, mode: "library"|"placed", unit, bodyStyle, discard, token},
// those eight keys only. text a kicad_symbol_lib document of at most 4 MB holding name and
// its extends chain, read by KiCad's own reader; opened in the frame's host mode with no
// library row (placed: KiCad's from-schematic title and undo). False (nothing queued) for
// any other shape, a document KiCad cannot read, or a modified symbol open without discard.
static bool kicadSymEditOpen( std::string aArgsJson )
{
    SYMBOL_EDIT_FRAME* fr = readyFrame();

    if( !fr )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded()
            || !onlyKeys( args, { "text", "name", "lib", "mode", "unit", "bodyStyle", "discard", "token" } )
            || !hasKeys( args, { "text", "name", "lib", "mode", "unit", "bodyStyle", "discard", "token" } ) )
    {
        return false;
    }

    std::optional<wxString>    name = textOf( args["name"], 1, TEXT_MAX );
    std::optional<wxString>    lib = textOf( args["lib"], 1, TEXT_MAX );
    std::optional<int>         unit = intOf( args["unit"], 0, 100000 );
    std::optional<int>         bodyStyle = intOf( args["bodyStyle"], 0, 100000 );
    std::optional<bool>        discard = boolOf( args["discard"] );
    std::optional<std::string> token = tokenOf( args["token"] );

    if( !args["text"].is_string() || !args["mode"].is_string() || !name || !lib || !unit
            || !bodyStyle || !discard || !token )
    {
        return false;
    }

    const std::string mode = args["mode"].get<std::string>();

    if( mode != "library" && mode != "placed" )
        return false;

    const std::string& text = args["text"].get_ref<const std::string&>();

    if( text.size() > cc_symedit::DOC_MAX_BYTES )
        return false;

    if( !*discard && fr->GetCurSymbol() && fr->GetScreen() && fr->GetScreen()->IsContentModified() )
        return false;

    cc_symedit::SYMBOL_CHAIN chain = cc_symedit::readSymbol( text, *name );

    if( !chain.symbol() )
        return false;

    auto           held = shareChain( std::move( chain ) );
    const wxString nick = *lib;
    const bool     placed = mode == "placed";
    const int      u = *unit, b = *bodyStyle;
    const std::string t = *token;

    pcbjam_collab::runOnCoroutine( fr, [held, nick, placed, u, b, t, mode]() {
        openChain( held, nick, placed, u, b, t, mode );
    } );

    return true;
}

// symedit.new: the New Symbol dialog's body (SYMBOL_EDIT_FRAME::CreateNewSymbol without
// DIALOG_LIB_NEW_SYMBOL, through LIB_SYMBOL_LIBRARY_MANAGER::CreateSymbol): {name, lib,
// parentText, parent, refPrefix, unitCount, unitsInterchangeable, bodyStyles: "one"|"demorgan",
// power: "none"|"global"|"local", pinNameOffset, showPinNames, showPinNumbers, pinNamesInside,
// inBom, onBoard, inSim, keepParentFields, discard, token}, those nineteen keys only. parent ""
// or a name inside parentText (then required); a derived symbol takes its parent's units and
// body styles. Opened in the host mode as symedit.open opens one, its mode "new".
static bool kicadSymEditNew( std::string aArgsJson )
{
    SYMBOL_EDIT_FRAME* fr = readyFrame();

    if( !fr )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded()
            || !onlyKeys( args, { "name", "lib", "parentText", "parent", "refPrefix", "unitCount",
                                  "unitsInterchangeable", "bodyStyles", "power", "pinNameOffset",
                                  "showPinNames", "showPinNumbers", "pinNamesInside", "inBom",
                                  "onBoard", "inSim", "keepParentFields", "discard", "token" } )
            || args.size() != 19 )
    {
        return false;
    }

    std::optional<wxString>    name = textOf( args["name"], 1, TEXT_MAX );
    std::optional<wxString>    lib = textOf( args["lib"], 1, TEXT_MAX );
    std::optional<wxString>    parent = textOf( args["parent"], 0, TEXT_MAX );
    std::optional<wxString>    refPrefix = textOf( args["refPrefix"], 0, TEXT_MAX );
    std::optional<int>         unitCount = intOf( args["unitCount"], 1, UNITS_MAX );
    std::optional<bool>        interchangeable = boolOf( args["unitsInterchangeable"] );
    std::optional<double>      pinNameOffset = numberOf( args["pinNameOffset"], 0.0, 50.0 );
    std::optional<bool>        showPinNames = boolOf( args["showPinNames"] );
    std::optional<bool>        showPinNumbers = boolOf( args["showPinNumbers"] );
    std::optional<bool>        pinNamesInside = boolOf( args["pinNamesInside"] );
    std::optional<bool>        inBom = boolOf( args["inBom"] );
    std::optional<bool>        onBoard = boolOf( args["onBoard"] );
    std::optional<bool>        inSim = boolOf( args["inSim"] );
    std::optional<bool>        keep = boolOf( args["keepParentFields"] );
    std::optional<bool>        discard = boolOf( args["discard"] );
    std::optional<std::string> token = tokenOf( args["token"] );

    if( !name || !lib || !parent || !refPrefix || !unitCount || !interchangeable || !pinNameOffset
            || !showPinNames || !showPinNumbers || !pinNamesInside || !inBom || !onBoard || !inSim
            || !keep || !discard || !token || !args["parentText"].is_string()
            || !args["bodyStyles"].is_string() || !args["power"].is_string() )
    {
        return false;
    }

    const std::string bodies = args["bodyStyles"].get<std::string>();
    const std::string power = args["power"].get<std::string>();

    if( ( bodies != "one" && bodies != "demorgan" )
            || ( power != "none" && power != "global" && power != "local" ) )
    {
        return false;
    }

    const std::string& parentText = args["parentText"].get_ref<const std::string&>();

    if( parentText.size() > cc_symedit::DOC_MAX_BYTES || ( parent->IsEmpty() != parentText.empty() ) )
        return false;

    if( !*discard && fr->GetCurSymbol() && fr->GetScreen() && fr->GetScreen()->IsContentModified() )
        return false;

    cc_symedit::SYMBOL_CHAIN parentChain;

    if( !parent->IsEmpty() )
    {
        parentChain = cc_symedit::readSymbol( parentText, *parent );

        if( !parentChain.symbol() )
            return false;
    }

    NEW_SYMBOL_PROPERTIES props;
    props.name = *name;
    props.parentSymbolName = *parent;
    props.reference = *refPrefix;
    props.unitCount = *unitCount;
    props.pinNameInside = *pinNamesInside;
    props.pinTextPosition = iuOf( *pinNameOffset );
    props.powerSymbol = power != "none";
    props.showPinNumber = *showPinNumbers;
    props.showPinName = *showPinNames;
    props.unitsInterchangeable = *interchangeable;
    props.includeInBom = *inBom;
    props.includeOnBoard = *onBoard;
    props.alternateBodyStyle = bodies == "demorgan";
    props.keepFootprint = *keep;
    props.keepDatasheet = *keep;
    props.transferUserFields = *keep;
    props.keepContentUserFields = *keep;

    std::unique_ptr<LIB_SYMBOL> created;

    try
    {
        created = LIB_SYMBOL_LIBRARY_MANAGER::CreateSymbol( props, parentChain.symbol() );
    }
    catch( ... )
    {
        return false;
    }

    if( !created )
        return false;

    // sy4-e: CreateSymbol duplicates the pins for a second body style but leaves the De Morgan
    // flag off, so the symbol counted one body style; set it as KiCad's symbol properties
    // dialog does beside the same SetBodyStyleCount( 2, false, true ).
    if( !parentChain.symbol() && bodies == "demorgan" )
        created->SetHasDeMorganBodyStyles( true );

    if( !parentChain.symbol() )
    {
        if( power == "local" )
            created->SetLocalPower();

        created->SetExcludedFromSim( !*inSim );
    }

    cc_symedit::SYMBOL_CHAIN chain;
    chain.links.push_back( std::move( created ) );

    for( std::unique_ptr<LIB_SYMBOL>& link : parentChain.links )
        chain.links.push_back( std::move( link ) );

    auto              held = shareChain( std::move( chain ) );
    const wxString    nick = *lib;
    const std::string t = *token;

    pcbjam_collab::runOnCoroutine( fr, [held, nick, t]() {
        openChain( held, nick, /*aPlaced*/ false, 1, 1, t, "new" );
    } );

    return true;
}

// symedit.state: SymEditState (spec 7.1); "" with no symbol editor frame.
static std::string kicadSymEditState()
{
    SYMBOL_EDIT_FRAME* fr = symEditFrame();

    if( !fr )
        return "";

    return dumpJson( stateJson( fr ) );
}

// symedit.take: the open symbol as a kicad_symbol_lib document by KiCad's own formatter, its
// parent included when derived: {name, text, parent} (parent "" for a root). A pure read; ""
// with nothing open.
static std::string kicadSymEditTake()
{
    SYMBOL_EDIT_FRAME* fr = readyFrame();
    LIB_SYMBOL*        symbol = fr ? fr->GetCurSymbol() : nullptr;

    if( !symbol )
        return "";

    const std::string text = cc_symedit::formatSymbol( symbol );

    if( text.empty() )
        return "";

    json out = json::object();
    out["name"] = utf8( symbol->GetName() );
    out["text"] = text;
    out["parent"] = utf8( cc_symedit::parentName( symbol ) );
    return dumpJson( out );
}

// symedit.markSaved: clears the modified flag when aToken is the state's (the page wrote what
// it took). No undo step.
static bool kicadSymEditMarkSaved( std::string aToken )
{
    SYMBOL_EDIT_FRAME* fr = readyFrame();

    if( !fr || !fr->GetCurSymbol() || !fr->GetScreen() || session().token.empty()
            || aToken != session().token )
    {
        return false;
    }

    fr->GetScreen()->SetContentModified( false );
    fr->UpdateTitle();
    return true;
}

// symedit.items.get: {uuids?: string[1..4096], kinds?: ItemKind[], unit: int >= 0,
// bodyStyle: int >= 0} (0 = all); {items: SymItem[]} in KiCad's draw order; "" for any
// other shape or with nothing open.
static std::string kicadSymEditItemsGet( std::string aArgsJson )
{
    SYMBOL_EDIT_FRAME* fr = readyFrame();
    LIB_SYMBOL*        symbol = fr ? fr->GetCurSymbol() : nullptr;

    if( !symbol )
        return "";

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !onlyKeys( args, { "uuids", "kinds", "unit", "bodyStyle" } )
            || !hasKeys( args, { "unit", "bodyStyle" } ) )
    {
        return "";
    }

    std::optional<int> unit = intOf( args["unit"], 0, 1000000 );
    std::optional<int> body = intOf( args["bodyStyle"], 0, 1000000 );

    if( !unit || !body )
        return "";

    std::optional<std::set<std::string>> wantedUuids;
    std::optional<std::set<std::string>> wantedKinds;

    if( args.contains( "uuids" ) )
    {
        std::optional<std::vector<std::string>> u = uuidsOf( args["uuids"], 1, UUIDS_MAX );

        if( !u )
            return "";

        wantedUuids = std::set<std::string>( u->begin(), u->end() );
    }

    if( args.contains( "kinds" ) )
    {
        const json& k = args["kinds"];

        if( !k.is_array() || k.size() > 5 )
            return "";

        std::set<std::string> kinds;

        for( const json& kind : k )
        {
            if( !kind.is_string() || editableKeys( kind.get<std::string>() ).empty() )
                return "";

            kinds.insert( kind.get<std::string>() );
        }

        wantedKinds = std::move( kinds );
    }

    json items = json::array();

    for( const SCH_ITEM& item : symbol->GetDrawItems() )
    {
        const char* kind = kindOf( item );

        if( !kind )
            continue;

        if( wantedKinds && !wantedKinds->count( kind ) )
            continue;

        if( wantedUuids && !wantedUuids->count( utf8( item.m_Uuid.AsString() ) ) )
            continue;

        if( *unit != 0 && item.GetUnit() != 0 && item.GetUnit() != *unit )
            continue;

        if( *body != 0 && item.GetBodyStyle() != 0 && item.GetBodyStyle() != *body )
            continue;

        items.push_back( itemJson( item ) );
    }

    json out = json::object();
    out["items"] = std::move( items );
    return dumpJson( out );
}

// symedit.items.set: {uuids: string[1..4096], set: ItemSet}, those two keys only: every uuid
// an item of a kind the set's keys fit, read whole; ONE commit "Edit". A pin key follows
// KiCad's synced pins mode (the pin dialog's own rule).
static bool kicadSymEditItemsSet( std::string aArgsJson )
{
    SYMBOL_EDIT_FRAME* fr = readyFrame();
    LIB_SYMBOL*        symbol = fr ? fr->GetCurSymbol() : nullptr;

    if( !symbol )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !onlyKeys( args, { "uuids", "set" } ) || !hasKeys( args, { "uuids", "set" } ) )
        return false;

    std::optional<std::vector<std::string>> uuids = uuidsOf( args["uuids"], 1, UUIDS_MAX );
    ITEM_SET                                set;

    if( !uuids || !readItemSet( args["set"], symbol, set ) )
        return false;

    std::map<std::string, SCH_ITEM*> byUuid = itemsByUuid( symbol );

    for( const std::string& u : *uuids )
    {
        auto it = byUuid.find( u );

        if( it == byUuid.end() || !setFits( set, *it->second ) )
            return false;
    }

    // A field's new name: one field at a time, never a name another field holds.
    if( set.name )
    {
        SCH_ITEM* first = byUuid[uuids->front()];

        if( first->Type() == SCH_FIELD_T
                && ( uuids->size() != 1
                     || fieldNameTaken( symbol, *set.name, static_cast<SCH_FIELD*>( first ) ) ) )
        {
            return false;
        }
    }

    const std::string token = session().token;
    const std::vector<std::string> ids = *uuids;

    pcbjam_collab::runOnCoroutine( fr, [token, ids, set]() {
        SYMBOL_EDIT_FRAME* frame = symEditFrame();
        LIB_SYMBOL*        live = liveSymbol( frame, token );

        if( !live )
            return;

        std::map<std::string, SCH_ITEM*> byId = itemsByUuid( live );

        for( const std::string& u : ids )
        {
            if( !byId.count( u ) )
                return;
        }

        SCH_COMMIT commit( frame );
        commit.Modify( live, frame->GetScreen() );

        for( const std::string& u : ids )
        {
            // A twin removed by the synced pins rule of an earlier pin is not set again.
            std::map<std::string, SCH_ITEM*> now = itemsByUuid( live );
            auto                             it = now.find( u );

            if( it != now.end() )
                applyItemSet( frame, live, it->second, set, /*aSync*/ true );
        }

        commit.Push( wxS( "Edit" ) );
        frame->RebuildView();
    } );

    return true;
}

// symedit.items.add: {items: NewItem[1..512]}, each a full item of its kind with a page-made
// lowercase uuid no item holds; ONE commit "Add".
static bool kicadSymEditItemsAdd( std::string aArgsJson )
{
    SYMBOL_EDIT_FRAME* fr = readyFrame();
    LIB_SYMBOL*        symbol = fr ? fr->GetCurSymbol() : nullptr;

    if( !symbol )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !onlyKeys( args, { "items" } ) || !args.contains( "items" )
            || !args["items"].is_array() || args["items"].empty()
            || args["items"].size() > NEW_ITEMS_MAX )
    {
        return false;
    }

    std::map<std::string, SCH_ITEM*> byUuid = itemsByUuid( symbol );
    std::set<std::string>            fresh;
    std::set<wxString>               newFieldNames;
    std::vector<NEW_ITEM>            items;

    for( const json& j : args["items"] )
    {
        if( !j.is_object() || !j.contains( "kind" ) || !j.contains( "uuid" ) || !j["kind"].is_string()
                || !j["uuid"].is_string() )
        {
            return false;
        }

        NEW_ITEM item;
        item.kind = j["kind"].get<std::string>();
        item.uuid = j["uuid"].get<std::string>();

        if( editableKeys( item.kind ).empty() || !cc_symedit::isLowerUuid( item.uuid )
                || byUuid.count( item.uuid ) || !fresh.insert( item.uuid ).second )
        {
            return false;
        }

        json keys = j;
        keys.erase( "kind" );
        keys.erase( "uuid" );

        if( item.kind == "shape" )
        {
            if( !keys.contains( "shape" ) || !keys["shape"].is_string() )
                return false;

            item.shape = keys["shape"].get<std::string>();

            if( item.shape != "rectangle" && item.shape != "circle" && item.shape != "arc"
                    && item.shape != "polyline" && item.shape != "bezier" )
            {
                return false;
            }

            keys.erase( "shape" );
        }

        if( !readItemSet( keys, symbol, item.set ) || !hasFullKeys( item ) )
            return false;

        if( item.kind == "shape" && !pointsFit( item.shape, item.set.points->size() ) )
            return false;

        if( item.kind == "field" )
        {
            // A new field is a user field: unit and body style 0, a name no field holds.
            if( *item.set.unit != 0 || *item.set.bodyStyle != 0 || item.set.name->IsEmpty()
                    || fieldNameTaken( symbol, *item.set.name, nullptr )
                    || !newFieldNames.insert( item.set.name->Lower() ).second )
            {
                return false;
            }
        }

        if( item.kind == "text" && ( !item.set.name->IsEmpty() || *item.set.showName ) )
            return false;

        items.push_back( std::move( item ) );
    }

    const std::string token = session().token;

    pcbjam_collab::runOnCoroutine( fr, [token, items]() {
        SYMBOL_EDIT_FRAME* frame = symEditFrame();
        LIB_SYMBOL*        live = liveSymbol( frame, token );

        if( !live )
            return;

        std::map<std::string, SCH_ITEM*> byId = itemsByUuid( live );

        for( const NEW_ITEM& n : items )
        {
            if( byId.count( n.uuid ) )
                return;
        }

        SCH_COMMIT commit( frame );
        commit.Modify( live, frame->GetScreen() );

        for( const NEW_ITEM& n : items )
        {
            SCH_ITEM* item = makeItem( live, n );

            if( !item )
                continue;

            ITEM_SET keys = n.set;

            // A field's name is set at birth; a text has neither a name nor a shown name.
            keys.name.reset();

            if( n.kind == "text" )
                keys.showName.reset();

            applyItemSet( frame, live, item, keys, /*aSync*/ false );

            if( n.kind == "field" )
                live->AddField( static_cast<SCH_FIELD*>( item ) );
            else
                live->AddDrawItem( item );
        }

        commit.Push( wxS( "Add" ) );
        frame->RebuildView();
    } );

    return true;
}

// symedit.items.remove: {uuids: string[1..4096]}; a field of the five mandatory kinds is
// refused; ONE commit "Delete".
static bool kicadSymEditItemsRemove( std::string aArgsJson )
{
    SYMBOL_EDIT_FRAME* fr = readyFrame();
    LIB_SYMBOL*        symbol = fr ? fr->GetCurSymbol() : nullptr;

    if( !symbol )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !onlyKeys( args, { "uuids" } ) || !args.contains( "uuids" ) )
        return false;

    std::optional<std::vector<std::string>> uuids = uuidsOf( args["uuids"], 1, UUIDS_MAX );

    if( !uuids )
        return false;

    std::map<std::string, SCH_ITEM*> byUuid = itemsByUuid( symbol );

    for( const std::string& u : *uuids )
    {
        auto it = byUuid.find( u );

        if( it == byUuid.end() )
            return false;

        if( it->second->Type() == SCH_FIELD_T && static_cast<SCH_FIELD*>( it->second )->IsMandatory() )
            return false;
    }

    const std::string token = session().token;
    const std::vector<std::string> ids = *uuids;

    pcbjam_collab::runOnCoroutine( fr, [token, ids]() {
        SYMBOL_EDIT_FRAME* frame = symEditFrame();
        LIB_SYMBOL*        live = liveSymbol( frame, token );

        if( !live )
            return;

        std::map<std::string, SCH_ITEM*> byId = itemsByUuid( live );

        for( const std::string& u : ids )
        {
            if( !byId.count( u ) )
                return;
        }

        // Nothing may keep pointing at an item about to go.
        frame->GetToolManager()->RunAction( ACTIONS::selectionClear );

        SCH_COMMIT commit( frame );
        commit.Modify( live, frame->GetScreen() );

        for( const std::string& u : ids )
            live->RemoveDrawItem( byId[u] );

        commit.Push( wxS( "Delete" ) );
        frame->RebuildView();
    } );

    return true;
}

// symedit.symbol.get: SymbolProps (spec 7.3); "" with nothing open.
static std::string kicadSymEditSymbolGet()
{
    SYMBOL_EDIT_FRAME* fr = readyFrame();
    LIB_SYMBOL*        symbol = fr ? fr->GetCurSymbol() : nullptr;

    if( !symbol )
        return "";

    return dumpJson( symbolPropsJson( symbol, fr->IsSymbolFromSchematic() ) );
}

// symedit.symbol.set: {set: SymbolSet}, that key only; ONE commit "Symbol properties", with
// the properties dialog's own setters in its order; fromParent runs DIALOG_UPDATE_SYMBOL_FIELDS'
// body, flatten flattenSymbol's.
static bool kicadSymEditSymbolSet( std::string aArgsJson )
{
    SYMBOL_EDIT_FRAME* fr = readyFrame();
    LIB_SYMBOL*        symbol = fr ? fr->GetCurSymbol() : nullptr;

    if( !symbol )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !onlyKeys( args, { "set" } ) || !args.contains( "set" ) )
        return false;

    SYMBOL_SET set;

    try
    {
        if( !readSymbolSet( args["set"], symbol, set ) )
            return false;
    }
    catch( ... )
    {
        return false;
    }

    const std::string token = session().token;

    pcbjam_collab::runOnCoroutine( fr, [token, set]() {
        SYMBOL_EDIT_FRAME* frame = symEditFrame();
        LIB_SYMBOL*        live = liveSymbol( frame, token );

        if( !live )
            return;

        // Units, body styles and a flatten rebuild the drawing: no selection may outlive it.
        frame->GetToolManager()->RunAction( ACTIONS::selectionClear );

        SCH_COMMIT commit( frame );
        commit.Modify( live, frame->GetScreen() );

        // sy4-e: a placed part's prefix is set before its designator's number, which it keeps.
        SYMBOL_SET placedSet = set;

        if( frame->IsSymbolFromSchematic() && placedSet.refPrefix )
            placedSet.refPrefix = *placedSet.refPrefix + refSuffixOf( live );

        applySymbolSet( live, placedSet );

        commit.Push( wxS( "Symbol properties" ) );

        frame->RebuildSymbolUnitAndBodyStyleLists();
        frame->UpdateTitle();
        frame->RebuildView();
    } );

    return true;
}

// symedit.show: {unit: int >= 1, bodyStyle: int >= 1}: the shown unit and body style (within
// the symbol's); no undo step, not a modification.
static bool kicadSymEditShow( std::string aArgsJson )
{
    SYMBOL_EDIT_FRAME* fr = readyFrame();
    LIB_SYMBOL*        symbol = fr ? fr->GetCurSymbol() : nullptr;

    if( !symbol )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !onlyKeys( args, { "unit", "bodyStyle" } )
            || !hasKeys( args, { "unit", "bodyStyle" } ) )
    {
        return false;
    }

    std::optional<int> unit = intOf( args["unit"], 1, std::max( 1, symbol->GetUnitCount() ) );
    std::optional<int> body = intOf( args["bodyStyle"], 1, std::max( 1, symbol->GetBodyStyleCount() ) );

    if( !unit || !body )
        return false;

    const std::string token = session().token;
    const int         u = *unit, b = *body;

    pcbjam_collab::runOnCoroutine( fr, [token, u, b]() {
        SYMBOL_EDIT_FRAME* frame = symEditFrame();
        LIB_SYMBOL*        live = liveSymbol( frame, token );

        if( !live )
            return;

        if( u <= live->GetUnitCount() )
            frame->SetUnit( u );

        if( b <= live->GetBodyStyleCount() )
            frame->SetBodyStyle( b );
    } );

    return true;
}

// The selection's uuids in KiCad's order: {"uuids": [...]}; "" with no symbol editor frame.
static std::string kicadSymEditSelection()
{
    SYMBOL_EDIT_FRAME* fr = symEditFrame();

    if( !fr || !fr->GetToolManager() )
        return "";

    SCH_SELECTION_TOOL* tool = fr->GetToolManager()->GetTool<SCH_SELECTION_TOOL>();
    json                uuids = json::array();

    if( tool )
    {
        for( EDA_ITEM* item : tool->GetSelection() )
        {
            if( item )
                uuids.push_back( utf8( item->m_Uuid.AsString() ) );
        }
    }

    json out = json::object();
    out["uuids"] = std::move( uuids );
    return dumpJson( out );
}

// select: {uuids: string[0..4096], zoom: bool}, those two keys only: every uuid an item of
// the open symbol; the selection replaced by them (an empty list clears it), the view fitted
// to them with zoom.
static bool kicadSymEditSelect( std::string aArgsJson )
{
    SYMBOL_EDIT_FRAME* fr = readyFrame();
    LIB_SYMBOL*        symbol = fr ? fr->GetCurSymbol() : nullptr;

    if( !symbol )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !onlyKeys( args, { "uuids", "zoom" } ) || !hasKeys( args, { "uuids", "zoom" } ) )
        return false;

    std::optional<std::vector<std::string>> uuids = uuidsOf( args["uuids"], 0, UUIDS_MAX );
    std::optional<bool>                     zoom = boolOf( args["zoom"] );

    if( !uuids || !zoom )
        return false;

    std::map<std::string, SCH_ITEM*> byUuid = itemsByUuid( symbol );

    for( const std::string& u : *uuids )
    {
        if( !byUuid.count( u ) )
            return false;
    }

    const std::string              token = session().token;
    const std::vector<std::string> ids = *uuids;
    const bool                     fit = *zoom;

    pcbjam_collab::runOnCoroutine( fr, [token, ids, fit]() {
        SYMBOL_EDIT_FRAME* frame = symEditFrame();
        LIB_SYMBOL*        live = liveSymbol( frame, token );

        if( !live )
            return;

        SCH_SELECTION_TOOL* tool = frame->GetToolManager()->GetTool<SCH_SELECTION_TOOL>();

        if( !tool )
            return;

        std::map<std::string, SCH_ITEM*> byId = itemsByUuid( live );
        EDA_ITEMS                        picked;

        for( const std::string& u : ids )
        {
            auto it = byId.find( u );

            if( it != byId.end() )
                picked.push_back( it->second );
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

// symedit.check: {grid, findings: SymFinding[]} (spec 7.4) through K2 (CheckLibSymbol with its
// findings), on the grid KiCad's own Symbol Checker uses (the view's, at least 25 mil), in mm.
// A pure read; "" with nothing open.
static std::string kicadSymEditCheck()
{
    SYMBOL_EDIT_FRAME* fr = readyFrame();
    LIB_SYMBOL*        symbol = fr ? fr->GetCurSymbol() : nullptr;

    if( !symbol )
        return "";

    const int minGrid = schIUScale.MilsToIU( 25 );
    int       grid = minGrid;

    if( fr->GetCanvas() && fr->GetCanvas()->GetView() && fr->GetCanvas()->GetView()->GetGAL() )
        grid = KiROUND( fr->GetCanvas()->GetView()->GetGAL()->GetGridSize().x );

    std::vector<wxString>             messages;
    std::vector<SYMBOL_CHECK_FINDING> findings;

    // sy4-e: a placed part is checked with its prefix in the reference field, as the library
    // symbol it came from carries it; the designator is put back at once (a read, no commit).
    const bool     placed = fr->IsSymbolFromSchematic();
    const wxString designator = symbol->GetReferenceField().GetText();

    if( placed )
        symbol->GetReferenceField().SetText( refPrefixOf( symbol, true ) );

    try
    {
        CheckLibSymbol( symbol, messages, grid, fr, &findings );
    }
    catch( ... )
    {
        if( placed )
            symbol->GetReferenceField().SetText( designator );

        return "";
    }

    if( placed )
        symbol->GetReferenceField().SetText( designator );

    json list = json::array();

    for( const SYMBOL_CHECK_FINDING& f : findings )
    {
        json j = json::object();
        json pins = json::array();
        json uuids = json::array();

        for( const wxString& p : f.pins )
            pins.push_back( utf8( p ) );

        for( const KIID& id : f.items )
            uuids.push_back( utf8( id.AsString() ) );

        j["code"] = f.code;
        j["severity"] = f.info ? "info" : "warning";
        j["pins"] = std::move( pins );
        j["unit"] = f.unit;
        j["bodyStyle"] = f.bodyStyle;
        j["x"] = f.hasPosition ? json( mmOf( f.position.x ) ) : json( nullptr );
        j["y"] = f.hasPosition ? json( mmOf( -f.position.y ) ) : json( nullptr );
        j["uuids"] = std::move( uuids );
        list.push_back( std::move( j ) );
    }

    json out = json::object();
    out["grid"] = mmOf( std::max( grid, minGrid ) );
    out["findings"] = std::move( list );
    return dumpJson( out );
}

// symedit.svg: {dir: "/tmp/cc-svg/<n>"} (the isRunDir rule), that key only. On the apply
// coroutine: SVGPlotSymbol(<dir>/symbol.svg, offset 0) for the shown unit and body style, then
// <dir>/.cc-svg.json last ({"error": "" | "plot_failed"}).
static bool kicadSymEditPlotSvg( std::string aArgsJson )
{
    SYMBOL_EDIT_FRAME* fr = readyFrame();

    if( !fr || !fr->GetCurSymbol() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !onlyKeys( args, { "dir" } ) || !args.contains( "dir" )
            || !args["dir"].is_string() )
    {
        return false;
    }

    const std::string dir = args["dir"].get<std::string>();

    if( !isRunDir( dir, "/tmp/cc-svg/" ) )
        return false;

    pcbjam_collab::runOnCoroutine( fr, [dir]() {
        const wxString wxDir = wxString::FromUTF8( dir.c_str() );
        const wxString file = wxDir + wxS( "/symbol.svg" );

        if( !wxFileName::DirExists( wxDir ) )
            wxFileName::Mkdir( wxDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL );

        if( wxFileName::FileExists( file ) )
            wxRemoveFile( file );

        SYMBOL_EDIT_FRAME* frame = symEditFrame();   // re-resolved on the coroutine (S4)
        bool               plotted = false;

        if( frame && frame->GetCurSymbol() )
        {
            try
            {
                frame->SVGPlotSymbol( file, VECTOR2I( 0, 0 ) );

                if( FILE* f = std::fopen( file.utf8_str(), "rb" ) )
                {
                    plotted = std::fgetc( f ) != EOF;
                    std::fclose( f );
                }
            }
            catch( ... )
            {
                plotted = false;
            }
        }

        json done = json::object();
        done["error"] = plotted ? "" : "plot_failed";
        writeDone( dir, ".cc-svg.json", done );
    } );

    return true;
}

// symbol.convert: {path: "/tmp/cc-import/<n>/<file>"}, that key only, from any eeschema frame
// (schematic or symbol editor). The reader SCH_IO_MGR guesses for the file reads every symbol,
// each formatted as symedit.take formats one: {format, symbols: [{name, text}]} (at most 2,000
// symbols and 4 MB of text, else {"error": "too_large"}); {"error": "unreadable", "message"}
// when no reader takes it. "" for any other shape.
static std::string kicadSymbolLibConvert( std::string aArgsJson )
{
    SCH_BASE_FRAME* fr =
            wxTheApp ? dynamic_cast<SCH_BASE_FRAME*>( wxTheApp->GetTopWindow() ) : nullptr;

    if( !fr || pcbjam_open::busy() || dialogUp() )
        return "";

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !onlyKeys( args, { "path" } ) || !args.contains( "path" )
            || !args["path"].is_string() )
    {
        return "";
    }

    const std::string root = "/tmp/cc-import/";
    const std::string path = args["path"].get<std::string>();

    if( path.size() <= root.size() || path.compare( 0, root.size(), root ) != 0
            || path.find( ".." ) != std::string::npos || path.find( '\0' ) != std::string::npos
            || path.back() == '/' )
    {
        return "";
    }

    const wxString file = wxString::FromUTF8( path.c_str() );
    json           out = json::object();

    auto unreadable =
            [&]( const wxString& aMessage )
            {
                json e = json::object();
                e["error"] = "unreadable";
                e["message"] = utf8( aMessage );
                return dumpJson( e );
            };

    if( !wxFileName::FileExists( file ) )
        return unreadable( wxS( "no such file" ) );

    SCH_IO_MGR::SCH_FILE_T type = SCH_IO_MGR::SCH_FILE_UNKNOWN;

    try
    {
        type = SCH_IO_MGR::GuessPluginTypeFromLibPath( file );
    }
    catch( ... )
    {
        type = SCH_IO_MGR::SCH_FILE_UNKNOWN;
    }

    // Only file readers: a library table, the pcbjam bridge and the live backends are not files.
    if( type == SCH_IO_MGR::SCH_FILE_UNKNOWN || type == SCH_IO_MGR::SCH_NESTED_TABLE
            || type == SCH_IO_MGR::SCH_PCBJAM || type == SCH_IO_MGR::SCH_DATABASE
            || type == SCH_IO_MGR::SCH_HTTP )
    {
        return unreadable( wxS( "no reader takes this file" ) );
    }

    constexpr size_t SYMBOLS_MAX = 2000;
    json             symbols = json::array();
    size_t           bytes = 0;

    try
    {
        IO_RELEASER<SCH_IO> pi( SCH_IO_MGR::FindPlugin( type ) );

        if( !pi )
            return unreadable( wxS( "no reader takes this file" ) );

        wxArrayString names;
        pi->EnumerateSymbolLib( names, file );

        if( names.size() > SYMBOLS_MAX )
        {
            json e = json::object();
            e["error"] = "too_large";
            return dumpJson( e );
        }

        for( const wxString& name : names )
        {
            LIB_SYMBOL* found = pi->LoadSymbol( file, name );   // the reader's own, never freed here

            if( !found )
                continue;

            // KiCad's import names a symbol as a library id may hold it.
            cc_symedit::SYMBOL_CHAIN chain = cc_symedit::copyChain( found );
            chain.symbol()->SetName( EscapeString( found->GetName(), CTX_LIBID ) );

            const std::string text = cc_symedit::formatSymbol( chain.symbol() );

            if( text.empty() )
                continue;

            bytes += text.size();

            if( bytes > cc_symedit::DOC_MAX_BYTES )
            {
                json e = json::object();
                e["error"] = "too_large";
                return dumpJson( e );
            }

            json s = json::object();
            s["name"] = utf8( chain.symbol()->GetName() );
            s["text"] = text;
            symbols.push_back( std::move( s ) );
        }
    }
    catch( const IO_ERROR& e )
    {
        return unreadable( e.What() );
    }
    catch( ... )
    {
        return unreadable( wxS( "the file could not be read" ) );
    }

    out["format"] = utf8( SCH_IO_MGR::ShowType( type ) );
    out["symbols"] = std::move( symbols );
    return dumpJson( out );
}

// The symbol editor frame's exports (cap symedit) and the symbol reader (cap symbolConvert).
// The schematic frame's two (kicadSymbolForEditor, kicadSymbolPushToSchematic) live in
// eeschema_embind.cpp beside kicadSymbolGet; the marker kicadHostSymEdit in
// kicad_editor_embind.cpp beside the other host markers.
EMSCRIPTEN_BINDINGS( symedit )
{
    function( "kicadSymEditOpen", &kicadSymEditOpen );
    function( "kicadSymEditNew", &kicadSymEditNew );
    function( "kicadSymEditState", &kicadSymEditState );
    function( "kicadSymEditTake", &kicadSymEditTake );
    function( "kicadSymEditMarkSaved", &kicadSymEditMarkSaved );
    function( "kicadSymEditItemsGet", &kicadSymEditItemsGet );
    function( "kicadSymEditItemsSet", &kicadSymEditItemsSet );
    function( "kicadSymEditItemsAdd", &kicadSymEditItemsAdd );
    function( "kicadSymEditItemsRemove", &kicadSymEditItemsRemove );
    function( "kicadSymEditSymbolGet", &kicadSymEditSymbolGet );
    function( "kicadSymEditSymbolSet", &kicadSymEditSymbolSet );
    function( "kicadSymEditShow", &kicadSymEditShow );
    function( "kicadSymEditSelection", &kicadSymEditSelection );
    function( "kicadSymEditSelect", &kicadSymEditSelect );
    function( "kicadSymEditCheck", &kicadSymEditCheck );
    function( "kicadSymEditPlotSvg", &kicadSymEditPlotSvg );
    function( "kicadSymbolLibConvert", &kicadSymbolLibConvert );
}

#endif // __EMSCRIPTEN__
