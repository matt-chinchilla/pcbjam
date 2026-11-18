// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e5, the footprint editor face: textually included by fpedit_embind.cpp (its own
// TU, merged image only, include subdir pcbnew, no KICAD_MERGED_EMBIND). It sees that TU's
// includes, fpedit_common.h once it lands, cc_fp_items.h and cc_output_common.h.
//
// Every definition of the lane lives in namespace cc_e5 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

#include <sys/stat.h>

#include <memory>
#include <optional>
#include <set>
#include <vector>

#include <wx/filefn.h>
#include <wx/filename.h>

#include <board_design_settings.h>
#include <cleanup_item.h>
#include <convert_shape_list_to_polygon.h>
#include <core/mirror.h>
#include <drc/drc_item.h>
#include <embedded_files.h>
#include <graphics_cleaner.h>
#include <jobs/job_export_pcb_svg.h>
#include <layer_utils.h>
#include <pad.h>
#include <pcb_draw_panel_gal.h>
#include <pcb_field.h>
#include <pcb_group.h>
#include <pcb_plot_params.h>
#include <pcb_plotter.h>
#include <pcbnew_settings.h>
#include <reporter.h>
#include <template_fieldnames.h>
#include <tool/actions.h>
#include <tool/tool_manager.h>
#include <view/view.h>
#include <wildcards_and_files_ext.h>
#include <zone.h>

namespace cc_e5
{
// ── fp-e7: footprint files to KiCad's text (kicadFootprintConvert, the footprint editor half) ──

// The one body, defined in this lane's board face (cc_e5_pcb.inc): the merged image links both.
std::string footprintConvertBody( const std::string& aArgsJson, FLIP_DIRECTION aFlip );

/** The footprint editor half of kicadFootprintConvert (cc_e5_dispatch.inc picks it first). */
std::string footprintConvertFp( std::string aArgsJson )
{
    FOOTPRINT_EDIT_FRAME* fr = cc_fpedit::readyFrame();

    if( !fr )
        return "";

    PCBNEW_SETTINGS* cfg = fr->GetPcbNewSettings();
    return footprintConvertBody( aArgsJson,
                                 cfg ? cfg->m_FlipDirection : FLIP_DIRECTION::TOP_BOTTOM );
}

// ── fp-e5: the footprint's own properties and its embedded files (FP spec 7.3) ─────────────

namespace fp
{
using namespace cc_fpedit;

constexpr size_t MODELS_MAX = 32;                         // FootprintProps.models
constexpr size_t FIELDS_MAX = 256;
constexpr size_t GROUPS_MAX = 256;                        // net tie and jumper groups
constexpr size_t PATH_MAX_CHARS = 1024;
constexpr size_t EMBED_NAME_MAX = 255;
constexpr long long EMBED_BYTES_MAX = 25ll * 1024ll * 1024ll;   // kicadFpEditEmbed's bound
constexpr double MARGIN_MAX_MM = 100.0;
constexpr double MODEL_OFFSET_MAX_MM = 1000.0;

/**
 * FOOTPRINT_EDIT_FRAME::updateEnabledLayers is protected; editFootprintProperties
 * (footprint_editor_utils.cpp 148) calls it after the properties dialog. A pointer to the
 * member formed through a derived class's using declaration reaches KiCad's own body (the
 * class is never made).
 */
struct FRAME_LAYERS : public FOOTPRINT_EDIT_FRAME
{
    using FOOTPRINT_EDIT_FRAME::updateEnabledLayers;
};

void updateEnabledLayers( FOOTPRINT_EDIT_FRAME* aFrame )
{
    void ( FOOTPRINT_EDIT_FRAME::*update )() = &FRAME_LAYERS::updateEnabledLayers;
    ( aFrame->*update )();
}

const char* fieldKind( const PCB_FIELD* aField )
{
    switch( aField->GetId() )
    {
    case FIELD_T::REFERENCE:   return "reference";
    case FIELD_T::VALUE:       return "value";
    case FIELD_T::DATASHEET:   return "datasheet";
    case FIELD_T::DESCRIPTION: return "description";
    default:                   return "user";
    }
}

const char* embeddedType( EMBEDDED_FILES::EMBEDDED_FILE::FILE_TYPE aType )
{
    switch( aType )
    {
    case EMBEDDED_FILES::EMBEDDED_FILE::FILE_TYPE::MODEL:     return "model";
    case EMBEDDED_FILES::EMBEDDED_FILE::FILE_TYPE::DATASHEET: return "datasheet";
    case EMBEDDED_FILES::EMBEDDED_FILE::FILE_TYPE::FONT:      return "font";
    default:                                                  return "other";
    }
}

std::optional<EMBEDDED_FILES::EMBEDDED_FILE::FILE_TYPE> embeddedTypeOf( const json& aValue )
{
    if( !aValue.is_string() )
        return std::nullopt;

    const std::string& t = aValue.get_ref<const std::string&>();

    if( t == "model" )
        return EMBEDDED_FILES::EMBEDDED_FILE::FILE_TYPE::MODEL;

    if( t == "datasheet" )
        return EMBEDDED_FILES::EMBEDDED_FILE::FILE_TYPE::DATASHEET;

    if( t == "font" )
        return EMBEDDED_FILES::EMBEDDED_FILE::FILE_TYPE::FONT;

    if( t == "other" )
        return EMBEDDED_FILES::EMBEDDED_FILE::FILE_TYPE::OTHER;

    return std::nullopt;
}

const char* zoneConnectionName( ZONE_CONNECTION aConnection )
{
    // The dialog's four choices; any other value reads as its first, Inherited.
    switch( aConnection )
    {
    case ZONE_CONNECTION::FULL:    return "solid";
    case ZONE_CONNECTION::THERMAL: return "thermal";
    case ZONE_CONNECTION::NONE:    return "none";
    default:                       return "inherit";
    }
}

std::optional<ZONE_CONNECTION> zoneConnectionOf( const json& aValue )
{
    if( !aValue.is_string() )
        return std::nullopt;

    const std::string& t = aValue.get_ref<const std::string&>();

    if( t == "inherit" )
        return ZONE_CONNECTION::INHERITED;

    if( t == "solid" )
        return ZONE_CONNECTION::FULL;

    if( t == "thermal" )
        return ZONE_CONNECTION::THERMAL;

    if( t == "none" )
        return ZONE_CONNECTION::NONE;

    return std::nullopt;
}

json optionalMm( const std::optional<int>& aValue )
{
    return aValue ? json( mmOf( *aValue ) ) : json( nullptr );
}

json vector3Json( const VECTOR3D& aValue )
{
    auto r = []( double v ) { return std::round( v * 1000000.0 ) / 1000000.0; };
    return json::array( { r( aValue.x ), r( aValue.y ), r( aValue.z ) } );
}

/** The user layers the frame's board enables, by canonical name, with the names it shows. */
json userLayerNamesJson( const BOARD* aBoard )
{
    json out = json::object();

    for( PCB_LAYER_ID id : LSET::UserDefinedLayersMask().Seq() )
    {
        if( aBoard->IsLayerEnabled( id ) )
            out[layerName( id )] = utf8( aBoard->GetLayerName( id ) );
    }

    return out;
}

/** The layers the dialog's private layers grid refuses (its forbiddenLayers). */
LSET privateForbidden()
{
    LSET forbidden = LSET::AllCuMask() | LSET::AllTechMask();
    forbidden.set( Edge_Cuts );
    forbidden.set( Margin );
    return forbidden;
}

/** FootprintProps (FP spec 7.3), read as DIALOG_FOOTPRINT_PROPERTIES_FP_EDITOR shows it. */
json footprintJson( FOOTPRINT_EDIT_FRAME* aFrame, const FOOTPRINT* aFootprint )
{
    const int attrs = aFootprint->GetAttributes();
    json      out = json::object();

    out["name"] = utf8( aFootprint->GetFPID().GetUniStringLibItemName() );
    out["description"] = utf8( aFootprint->GetLibDescription() );
    out["keywords"] = utf8( aFootprint->GetKeywords() );

    json fields = json::array();

    for( const PCB_FIELD* field : aFootprint->GetFields() )
    {
        if( !field )
            continue;

        json f = json::object();
        f["name"] = utf8( field->GetCanonicalName() );
        f["value"] = utf8( field->GetText() );
        f["visible"] = field->IsVisible();
        f["layer"] = layerName( field->GetLayer() );
        f["kind"] = fieldKind( field );
        fields.push_back( f );
    }

    out["fields"] = fields;
    out["type"] = ( attrs & FP_THROUGH_HOLE ) ? "through_hole"
                                              : ( ( attrs & FP_SMD ) ? "smd" : "unspecified" );
    out["boardOnly"] = ( attrs & FP_BOARD_ONLY ) != 0;
    out["excludeFromPos"] = ( attrs & FP_EXCLUDE_FROM_POS_FILES ) != 0;
    out["excludeFromBom"] = ( attrs & FP_EXCLUDE_FROM_BOM ) != 0;
    out["dnp"] = ( attrs & FP_DNP ) != 0;
    out["allowMissingCourtyard"] = aFootprint->AllowMissingCourtyard();
    out["allowSolderMaskBridges"] = aFootprint->AllowSolderMaskBridges();
    out["clearance"] = optionalMm( aFootprint->GetLocalClearance() );
    out["maskMargin"] = optionalMm( aFootprint->GetLocalSolderMaskMargin() );
    out["pasteMargin"] = optionalMm( aFootprint->GetLocalSolderPasteMargin() );

    const std::optional<double> ratio = aFootprint->GetLocalSolderPasteMarginRatio();
    out["pasteRatio"] = ratio ? json( std::round( *ratio * 1000000.0 ) / 1000000.0 )
                              : json( nullptr );
    out["zoneConnection"] = zoneConnectionName( aFootprint->GetLocalZoneConnection() );

    json netTies = json::array();

    for( const wxString& group : aFootprint->GetNetTiePadGroups() )
    {
        if( !group.IsEmpty() )
            netTies.push_back( utf8( group ) );
    }

    out["netTieGroups"] = netTies;

    json groups = json::array();

    for( const std::set<wxString>& group : aFootprint->JumperPadGroups() )
    {
        json g = json::array();

        for( const wxString& number : group )
            g.push_back( utf8( number ) );

        groups.push_back( g );
    }

    out["jumpers"] = json::object( { { "duplicateNumbersAreJumpers",
                                       aFootprint->GetDuplicatePadNumbersAreJumpers() },
                                     { "groups", groups } } );
    out["privateLayers"] = layersJson( aFootprint->GetPrivateLayers() );

    const bool custom = aFootprint->GetStackupMode() == FOOTPRINT_STACKUP::CUSTOM_LAYERS;
    // The dialog's copper count: its first choice (2) unless the stackup is custom.
    const int  copper = custom
                       ? static_cast<int>( ( aFootprint->GetStackupLayers() & LSET::AllCuMask() ).count() )
                       : 2;
    out["customStackup"] = json::object( { { "on", custom }, { "copperLayers", copper } } );
    out["userLayerNames"] = userLayerNamesJson( aFrame->GetBoard() );

    json models = json::array();

    for( const FP_3DMODEL& model : aFootprint->Models() )
    {
        json m = json::object();
        m["path"] = utf8( model.m_Filename );
        m["offset"] = vector3Json( model.m_Offset );
        m["rotate"] = vector3Json( model.m_Rotation );
        m["scale"] = vector3Json( model.m_Scale );
        m["opacity"] = std::round( model.m_Opacity * 1000000.0 ) / 1000000.0;
        m["show"] = model.m_Show;
        models.push_back( m );
    }

    out["models"] = models;

    json embedded = json::array();

    if( const EMBEDDED_FILES* files = aFootprint->GetEmbeddedFiles() )
    {
        for( const auto& [name, file] : files->EmbeddedFileMap() )
        {
            if( !file )
                continue;

            json e = json::object();
            e["name"] = utf8( name );
            e["type"] = embeddedType( file->type );
            e["bytes"] = static_cast<long long>( file->decompressedData.size() );
            embedded.push_back( e );
        }
    }

    out["embedded"] = embedded;
    return out;
}

// ── the set, read whole and closed before anything is queued ──────────────────────────

struct FIELD_PATCH
{
    wxString                    name;
    std::optional<wxString>     value;
    std::optional<bool>         visible;
    std::optional<PCB_LAYER_ID> layer;
};

struct NEW_FIELD
{
    wxString     name;
    wxString     value;
    bool         visible = true;
    PCB_LAYER_ID layer = F_Fab;
};

struct FOOTPRINT_SET
{
    std::optional<wxString>               name;
    std::optional<wxString>               description;
    std::optional<wxString>               keywords;
    std::vector<FIELD_PATCH>              fields;
    std::optional<NEW_FIELD>              addField;
    std::optional<wxString>               removeField;
    std::optional<int>                    type;           // FP_THROUGH_HOLE, FP_SMD or 0
    std::optional<bool>                   boardOnly;
    std::optional<bool>                   excludeFromPos;
    std::optional<bool>                   excludeFromBom;
    std::optional<bool>                   dnp;
    std::optional<bool>                   allowMissingCourtyard;
    std::optional<bool>                   allowSolderMaskBridges;
    bool                                  hasClearance = false;
    std::optional<int>                    clearance;
    bool                                  hasMaskMargin = false;
    std::optional<int>                    maskMargin;
    bool                                  hasPasteMargin = false;
    std::optional<int>                    pasteMargin;
    bool                                  hasPasteRatio = false;
    std::optional<double>                 pasteRatio;
    std::optional<ZONE_CONNECTION>        zoneConnection;
    std::optional<std::vector<wxString>>  netTieGroups;
    std::optional<bool>                   duplicateNumbersAreJumpers;
    std::optional<std::vector<std::set<wxString>>> jumperGroups;
    std::optional<LSET>                   privateLayers;
    std::optional<bool>                   customOn;
    int                                   copperLayers = 2;
    std::optional<json>                   userLayerNames;   // read only: must equal the board's
    std::optional<std::vector<FP_3DMODEL>> models;
};

/** A nullable length in mm within [aMin, aMax]: aHas tells a key that was sent. */
bool readNullableMm( const json& aSet, const char* aKey, double aMin, double aMax, bool& aHas,
                     std::optional<int>& aOut )
{
    if( !aSet.contains( aKey ) )
        return true;

    aHas = true;

    if( aSet[aKey].is_null() )
    {
        aOut.reset();
        return true;
    }

    std::optional<double> mm = numberOf( aSet[aKey], aMin, aMax );

    if( !mm )
        return false;

    aOut = iuOf( *mm );
    return true;
}

bool readBool( const json& aSet, const char* aKey, std::optional<bool>& aOut )
{
    if( !aSet.contains( aKey ) )
        return true;

    aOut = boolOf( aSet[aKey] );
    return aOut.has_value();
}

std::optional<VECTOR3D> vector3Of( const json& aValue, double aMin, double aMax )
{
    if( !aValue.is_array() || aValue.size() != 3 )
        return std::nullopt;

    std::optional<double> x = numberOf( aValue[0], aMin, aMax );
    std::optional<double> y = numberOf( aValue[1], aMin, aMax );
    std::optional<double> z = numberOf( aValue[2], aMin, aMax );

    if( !x || !y || !z )
        return std::nullopt;

    return VECTOR3D( *x, *y, *z );
}

/** A field name: 1 to 1,024 characters, no line break. */
std::optional<wxString> fieldNameOf( const json& aValue )
{
    return textOf( aValue, 1, TEXT_MAX );
}

/** {set: FootprintSet} read whole; false for any other shape or an empty set. */
bool readFootprintSet( const json& aArgs, FOOTPRINT_SET& aOut )
{
    if( !onlyKeys( aArgs, { "set" } ) || !hasKeys( aArgs, { "set" } ) )
        return false;

    const json& s = aArgs["set"];

    if( !onlyKeys( s, { "name", "description", "keywords", "fields", "addField", "removeField",
                        "type", "boardOnly", "excludeFromPos", "excludeFromBom", "dnp",
                        "allowMissingCourtyard", "allowSolderMaskBridges", "clearance",
                        "maskMargin", "pasteMargin", "pasteRatio", "zoneConnection",
                        "netTieGroups", "jumpers", "privateLayers", "customStackup",
                        "userLayerNames", "models" } )
        || s.empty() )
    {
        return false;
    }

    if( s.contains( "name" ) )
    {
        aOut.name = textOf( s["name"], 1, TEXT_MAX );

        // checkFootprintName: a name, and none of the characters a library name refuses.
        if( !aOut.name || !FOOTPRINT::IsLibNameValid( *aOut.name ) )
            return false;
    }

    if( s.contains( "description" ) )
    {
        aOut.description = textOf( s["description"], 0, TEXT_MAX, true );

        if( !aOut.description )
            return false;
    }

    if( s.contains( "keywords" ) )
    {
        aOut.keywords = textOf( s["keywords"], 0, TEXT_MAX );

        if( !aOut.keywords )
            return false;
    }

    if( s.contains( "fields" ) )
    {
        const json& fields = s["fields"];

        if( !fields.is_array() || fields.size() > FIELDS_MAX )
            return false;

        std::set<wxString> seen;

        for( const json& f : fields )
        {
            if( !onlyKeys( f, { "name", "value", "visible", "layer" } )
                || !hasKeys( f, { "name" } ) )
            {
                return false;
            }

            FIELD_PATCH patch;
            std::optional<wxString> name = fieldNameOf( f["name"] );

            if( !name || !seen.insert( *name ).second )
                return false;

            patch.name = *name;

            if( f.contains( "value" ) )
            {
                patch.value = textOf( f["value"], 0, TEXT_MAX );

                if( !patch.value )
                    return false;
            }

            if( f.contains( "visible" ) )
            {
                patch.visible = boolOf( f["visible"] );

                if( !patch.visible )
                    return false;
            }

            if( f.contains( "layer" ) )
            {
                patch.layer = layerOf( f["layer"] );

                if( !patch.layer )
                    return false;
            }

            aOut.fields.push_back( patch );
        }
    }

    if( s.contains( "addField" ) )
    {
        const json& f = s["addField"];

        if( !onlyKeys( f, { "name", "value", "visible", "layer" } )
            || !hasKeys( f, { "name", "value", "visible", "layer" } ) )
        {
            return false;
        }

        std::optional<wxString>     name = fieldNameOf( f["name"] );
        std::optional<wxString>     value = textOf( f["value"], 0, TEXT_MAX );
        std::optional<bool>         visible = boolOf( f["visible"] );
        std::optional<PCB_LAYER_ID> layer = layerOf( f["layer"] );

        if( !name || !value || !visible || !layer )
            return false;

        aOut.addField = NEW_FIELD{ *name, *value, *visible, *layer };
    }

    if( s.contains( "removeField" ) )
    {
        aOut.removeField = fieldNameOf( s["removeField"] );

        if( !aOut.removeField )
            return false;
    }

    if( s.contains( "type" ) )
    {
        if( !s["type"].is_string() )
            return false;

        const std::string& t = s["type"].get_ref<const std::string&>();

        if( t == "through_hole" )
            aOut.type = FP_THROUGH_HOLE;
        else if( t == "smd" )
            aOut.type = FP_SMD;
        else if( t == "unspecified" )
            aOut.type = 0;
        else
            return false;
    }

    if( !readBool( s, "boardOnly", aOut.boardOnly )
        || !readBool( s, "excludeFromPos", aOut.excludeFromPos )
        || !readBool( s, "excludeFromBom", aOut.excludeFromBom )
        || !readBool( s, "dnp", aOut.dnp )
        || !readBool( s, "allowMissingCourtyard", aOut.allowMissingCourtyard )
        || !readBool( s, "allowSolderMaskBridges", aOut.allowSolderMaskBridges ) )
    {
        return false;
    }

    // The dialog's m_netClearance.Validate( 0, INT_MAX ): a clearance is never negative.
    if( !readNullableMm( s, "clearance", 0.0, MARGIN_MAX_MM, aOut.hasClearance, aOut.clearance )
        || !readNullableMm( s, "maskMargin", -MARGIN_MAX_MM, MARGIN_MAX_MM, aOut.hasMaskMargin,
                            aOut.maskMargin )
        || !readNullableMm( s, "pasteMargin", -MARGIN_MAX_MM, MARGIN_MAX_MM, aOut.hasPasteMargin,
                            aOut.pasteMargin ) )
    {
        return false;
    }

    if( s.contains( "pasteRatio" ) )
    {
        aOut.hasPasteRatio = true;

        if( !s["pasteRatio"].is_null() )
        {
            // KiCad's own ratio (the file's solder_paste_ratio), -1 to 1; the dialog shows it
            // as a percentage.
            aOut.pasteRatio = numberOf( s["pasteRatio"], -1.0, 1.0 );

            if( !aOut.pasteRatio )
                return false;
        }
    }

    if( s.contains( "zoneConnection" ) )
    {
        aOut.zoneConnection = zoneConnectionOf( s["zoneConnection"] );

        if( !aOut.zoneConnection )
            return false;
    }

    if( s.contains( "netTieGroups" ) )
    {
        const json& groups = s["netTieGroups"];

        if( !groups.is_array() || groups.size() > GROUPS_MAX )
            return false;

        std::vector<wxString> out;

        for( const json& g : groups )
        {
            std::optional<wxString> group = textOf( g, 0, TEXT_MAX );

            if( !group )
                return false;

            out.push_back( *group );
        }

        aOut.netTieGroups = out;
    }

    if( s.contains( "jumpers" ) )
    {
        const json& j = s["jumpers"];

        if( !onlyKeys( j, { "duplicateNumbersAreJumpers", "groups" } )
            || !hasKeys( j, { "duplicateNumbersAreJumpers", "groups" } ) )
        {
            return false;
        }

        aOut.duplicateNumbersAreJumpers = boolOf( j["duplicateNumbersAreJumpers"] );

        if( !aOut.duplicateNumbersAreJumpers || !j["groups"].is_array()
            || j["groups"].size() > GROUPS_MAX )
        {
            return false;
        }

        std::vector<std::set<wxString>> groups;

        for( const json& g : j["groups"] )
        {
            if( !g.is_array() || g.size() > UUIDS_MAX )
                return false;

            std::set<wxString>& group = groups.emplace_back();

            for( const json& n : g )
            {
                std::optional<wxString> number = textOf( n, 1, TEXT_MAX );

                if( !number )
                    return false;

                group.insert( *number );
            }
        }

        aOut.jumperGroups = groups;
    }

    if( s.contains( "privateLayers" ) )
    {
        aOut.privateLayers = layersOf( s["privateLayers"], 0 );

        if( !aOut.privateLayers || ( *aOut.privateLayers & privateForbidden() ).any() )
            return false;
    }

    if( s.contains( "customStackup" ) )
    {
        const json& c = s["customStackup"];

        if( !onlyKeys( c, { "on", "copperLayers" } ) || !hasKeys( c, { "on", "copperLayers" } ) )
            return false;

        aOut.customOn = boolOf( c["on"] );
        std::optional<int> copper = intOf( c["copperLayers"], 2, 32 );

        // The dialog's copper count choices: 2, 4, ... 32.
        if( !aOut.customOn || !copper || ( *copper % 2 ) != 0 )
            return false;

        aOut.copperLayers = *copper;
    }

    if( s.contains( "userLayerNames" ) )
    {
        if( !s["userLayerNames"].is_object() )
            return false;

        aOut.userLayerNames = s["userLayerNames"];
    }

    if( s.contains( "models" ) )
    {
        const json& models = s["models"];

        if( !models.is_array() || models.size() > MODELS_MAX )
            return false;

        std::vector<FP_3DMODEL> out;

        for( const json& m : models )
        {
            if( !onlyKeys( m, { "path", "offset", "rotate", "scale", "opacity", "show" } )
                || !hasKeys( m, { "path", "offset", "rotate", "scale", "opacity", "show" } ) )
            {
                return false;
            }

            std::optional<wxString> path = textOf( m["path"], 0, PATH_MAX_CHARS );
            std::optional<VECTOR3D> offset = vector3Of( m["offset"], -MODEL_OFFSET_MAX_MM,
                                                        MODEL_OFFSET_MAX_MM );
            std::optional<VECTOR3D> rotate = vector3Of( m["rotate"], -360.0, 360.0 );
            std::optional<VECTOR3D> scale = vector3Of( m["scale"], 0.0001, 10000.0 );
            std::optional<double>   opacity = numberOf( m["opacity"], 0.0, 1.0 );
            std::optional<bool>     show = boolOf( m["show"] );

            if( !path || !offset || !rotate || !scale || !opacity || !show )
                return false;

            FP_3DMODEL model;
            model.m_Filename = *path;
            model.m_Offset = *offset;
            model.m_Rotation = *rotate;
            model.m_Scale = *scale;
            model.m_Opacity = *opacity;
            model.m_Show = *show;
            out.push_back( model );
        }

        aOut.models = out;
    }

    return true;
}

PCB_FIELD* fieldNamed( const FOOTPRINT* aFootprint, const wxString& aName )
{
    for( PCB_FIELD* field : aFootprint->GetFields() )
    {
        if( field && field->GetCanonicalName() == aName )
            return field;
    }

    return nullptr;
}

/** The custom stackup's layers as getCustomLayersFromControls makes them. */
LSET customLayersOf( const FOOTPRINT* aFootprint, bool aOn, int aCopperLayers )
{
    LSET layers;

    if( aOn )
    {
        layers |= LSET::AllCuMask( aCopperLayers );

        // The user layers of the grid: the stackup's own when it is custom already.
        if( aFootprint->GetStackupMode() == FOOTPRINT_STACKUP::CUSTOM_LAYERS )
            layers |= aFootprint->GetStackupLayers() & LSET::UserDefinedLayersMask();
    }
    else
    {
        layers |= LSET{ F_Cu, B_Cu };
        layers |= LSET::InternalCuMask();
        layers |= LSET::UserDefinedLayersMask( 4 );
    }

    return layers;
}

/**
 * What the dialog's Validate and TransferDataFromWindow refuse against THIS footprint: a field
 * patch naming no field, a removal of a mandatory or missing field, an added field whose name
 * is taken, a jumper pad that does not exist, a stackup that orphans a layer in use, a user
 * layer name change (the editor's preferences hold those, not the footprint).
 */
bool fits( const FOOTPRINT_EDIT_FRAME* aFrame, const FOOTPRINT* aFootprint,
           const FOOTPRINT_SET& aSet )
{
    for( const FIELD_PATCH& patch : aSet.fields )
    {
        if( !fieldNamed( aFootprint, patch.name ) )
            return false;

        if( aSet.removeField && *aSet.removeField == patch.name )
            return false;
    }

    if( aSet.removeField )
    {
        PCB_FIELD* field = fieldNamed( aFootprint, *aSet.removeField );

        if( !field || field->IsMandatory() )
            return false;
    }

    if( aSet.addField )
    {
        PCB_FIELD* taken = fieldNamed( aFootprint, aSet.addField->name );

        if( taken && !( aSet.removeField && *aSet.removeField == aSet.addField->name ) )
            return false;

        for( FIELD_T id : MANDATORY_FIELDS )
        {
            if( GetCanonicalFieldName( id ) == aSet.addField->name )
                return false;
        }
    }

    if( aSet.jumperGroups )
    {
        std::set<wxString> numbers;

        for( const PAD* pad : aFootprint->Pads() )
            numbers.insert( pad->GetNumber() );

        for( const std::set<wxString>& group : *aSet.jumperGroups )
        {
            for( const wxString& number : group )
            {
                if( !numbers.count( number ) )
                    return false;
            }
        }
    }

    if( aSet.customOn )
    {
        const LSET custom = customLayersOf( aFootprint, *aSet.customOn, aSet.copperLayers );

        if( LAYER_UTILS::GetOrphanedFootprintLayers( *aFootprint, custom ).any() )
            return false;
    }

    if( aSet.userLayerNames && *aSet.userLayerNames != userLayerNamesJson( aFrame->GetBoard() ) )
        return false;

    return true;
}

/**
 * The set onto the footprint inside aCommit, in TransferDataFromWindow's order
 * (dialog_footprint_properties_fp_editor.cpp 673 to 880): the footprint Modify'd first, the
 * embedded files no field names any more removed, name, description, keywords, fields,
 * private layers, stackup, attributes, courtyard and bridges, clearances, zone connection,
 * net tie groups, jumpers, models.
 */
bool applyFootprintSet( FOOTPRINT_EDIT_FRAME* aFrame, FOOTPRINT* aFootprint,
                        BOARD_COMMIT& aCommit, const FOOTPRINT_SET& aSet )
{
    if( !fits( aFrame, aFootprint, aSet ) )
        return false;

    KIGFX::VIEW* view = aFrame->GetCanvas()->GetView();

    aCommit.Modify( aFootprint );

    // The fields' texts before and after, for the embedded file cleanup.
    std::vector<wxString> oldTexts;

    for( PCB_FIELD* field : aFootprint->GetFields() )
    {
        if( field )
            oldTexts.push_back( field->GetText() );
    }

    if( aSet.name )
    {
        LIB_ID fpID = aFootprint->GetFPID();
        fpID.SetLibItemName( *aSet.name );
        aFootprint->SetFPID( fpID );
    }

    if( aSet.description )
        aFootprint->SetLibDescription( *aSet.description );

    if( aSet.keywords )
        aFootprint->SetKeywords( *aSet.keywords );

    if( !aSet.fields.empty() || aSet.addField || aSet.removeField )
    {
        aFrame->GetToolManager()->RunAction( ACTIONS::selectionClear );

        if( aSet.removeField )
        {
            if( PCB_FIELD* field = fieldNamed( aFootprint, *aSet.removeField ) )
            {
                view->Remove( field );
                aFootprint->Remove( field );
                delete field;
            }
        }

        for( const FIELD_PATCH& patch : aSet.fields )
        {
            PCB_FIELD* field = fieldNamed( aFootprint, patch.name );

            if( !field )
                return false;

            if( patch.value )
                field->SetText( *patch.value );

            if( patch.visible )
                field->SetVisible( *patch.visible );

            if( patch.layer )
                field->SetLayer( *patch.layer );

            view->Update( field );
        }

        if( aSet.addField )
        {
            // OnAddField: a user field, its text size, thickness and italics from the
            // design settings for its layer.
            const BOARD_DESIGN_SETTINGS& settings = aFrame->GetDesignSettings();
            PCB_FIELD* field = new PCB_FIELD( aFootprint, FIELD_T::USER, aSet.addField->name );

            field->SetLayer( aSet.addField->layer );
            field->SetTextSize( settings.GetTextSize( aSet.addField->layer ) );
            field->SetTextThickness( settings.GetTextThickness( aSet.addField->layer ) );
            field->SetItalic( settings.GetTextItalic( aSet.addField->layer ) );
            field->SetText( aSet.addField->value );
            field->SetVisible( aSet.addField->visible );
            field->SetPosition( aFootprint->GetPosition() );
            aFootprint->Add( field );
            view->Add( field );
        }
    }

    // Clear out embedded files no field names any more (the dialog's own step).
    {
        std::set<wxString> files;

        for( PCB_FIELD* field : aFootprint->GetFields() )
        {
            if( field && field->GetText().StartsWith( FILEEXT::KiCadUriPrefix ) )
                files.insert( field->GetText() );
        }

        for( const wxString& text : oldTexts )
        {
            if( text.StartsWith( FILEEXT::KiCadUriPrefix ) && !files.count( text ) )
                aFootprint->RemoveFile( text.Mid( FILEEXT::KiCadUriPrefix.size() + 3 ) );
        }
    }

    if( aSet.privateLayers )
        aFootprint->SetPrivateLayers( *aSet.privateLayers );

    if( aSet.customOn )
    {
        if( *aSet.customOn )
        {
            aFootprint->SetStackupMode( FOOTPRINT_STACKUP::CUSTOM_LAYERS );
            aFootprint->SetStackupLayers( customLayersOf( aFootprint, true, aSet.copperLayers ) );
        }
        else
        {
            aFootprint->SetStackupMode( FOOTPRINT_STACKUP::EXPAND_INNER_LAYERS );
        }
    }

    if( aSet.type || aSet.boardOnly || aSet.excludeFromPos || aSet.excludeFromBom || aSet.dnp )
    {
        const int old = aFootprint->GetAttributes();
        int       attributes = aSet.type ? *aSet.type : ( old & ( FP_THROUGH_HOLE | FP_SMD ) );
        auto      flag = [&]( const std::optional<bool>& aSent, int aBit )
        {
            if( aSent ? *aSent : ( old & aBit ) != 0 )
                attributes |= aBit;
        };

        flag( aSet.boardOnly, FP_BOARD_ONLY );
        flag( aSet.excludeFromPos, FP_EXCLUDE_FROM_POS_FILES );
        flag( aSet.excludeFromBom, FP_EXCLUDE_FROM_BOM );
        flag( aSet.dnp, FP_DNP );
        aFootprint->SetAttributes( attributes );
    }

    if( aSet.allowMissingCourtyard )
        aFootprint->SetAllowMissingCourtyard( *aSet.allowMissingCourtyard );

    if( aSet.allowSolderMaskBridges )
        aFootprint->SetAllowSolderMaskBridges( *aSet.allowSolderMaskBridges );

    if( aSet.hasClearance )
        aFootprint->SetLocalClearance( aSet.clearance );

    if( aSet.hasMaskMargin )
        aFootprint->SetLocalSolderMaskMargin( aSet.maskMargin );

    if( aSet.hasPasteMargin )
        aFootprint->SetLocalSolderPasteMargin( aSet.pasteMargin );

    if( aSet.hasPasteRatio )
        aFootprint->SetLocalSolderPasteMarginRatio( aSet.pasteRatio );

    if( aSet.zoneConnection )
        aFootprint->SetLocalZoneConnection( *aSet.zoneConnection );

    if( aSet.netTieGroups )
    {
        aFootprint->ClearNetTiePadGroups();

        for( const wxString& group : *aSet.netTieGroups )
        {
            if( !group.IsEmpty() )
                aFootprint->AddNetTiePadGroup( group );
        }
    }

    if( aSet.duplicateNumbersAreJumpers )
        aFootprint->SetDuplicatePadNumbersAreJumpers( *aSet.duplicateNumbersAreJumpers );

    if( aSet.jumperGroups )
        aFootprint->JumperPadGroups() = *aSet.jumperGroups;

    if( aSet.models )
    {
        std::vector<FP_3DMODEL>* fpList = &aFootprint->Models();
        fpList->clear();
        fpList->insert( fpList->end(), aSet.models->begin(), aSet.models->end() );
    }

    return true;
}

/** Then what editFootprintProperties does after the dialog (footprint_editor_utils.cpp 155). */
void afterProperties( FOOTPRINT_EDIT_FRAME* aFrame )
{
    updateEnabledLayers( aFrame );
    aFrame->UpdateTitle();
    aFrame->UpdateMsgPanel();
    aFrame->UpdateUserInterface();
    aFrame->GetCanvas()->Refresh();
}

/** An embed source: /tmp/cc-embed/<n>/<file>, a regular file of at most 25 MB. */
bool isEmbedSource( const std::string& aPath )
{
    if( aPath.empty() || aPath.size() > PATH_MAX_CHARS || aPath.find( '\0' ) != std::string::npos
        || aPath.find( ".." ) != std::string::npos )
    {
        return false;
    }

    const size_t slash = aPath.rfind( '/' );

    if( slash == std::string::npos || slash + 1 >= aPath.size()
        || !cc_output::IsEmbedRunDir( aPath.substr( 0, slash ) ) )
    {
        return false;
    }

    struct stat st;

    return ::stat( aPath.c_str(), &st ) == 0 && S_ISREG( st.st_mode )
           && static_cast<long long>( st.st_size ) <= EMBED_BYTES_MAX;
}

/** An embedded file's name: 1 to 255 characters, no path separator. */
std::optional<wxString> embedNameOf( const json& aValue )
{
    std::optional<wxString> name = textOf( aValue, 1, EMBED_NAME_MAX );

    if( !name || name->Contains( wxT( "/" ) ) || name->Contains( wxT( "\\" ) ) )
        return std::nullopt;

    return name;
}

/**
 * The file read by EMBEDDED_FILES::AddFile (its own read, ZSTD compression and hash) into a
 * collection of its own, then taken out of it under aName and aType: nullptr when KiCad
 * cannot read or compress it.
 */
EMBEDDED_FILES::EMBEDDED_FILE* readEmbeddedFile( const std::string& aPath, const wxString& aName,
                                                 EMBEDDED_FILES::EMBEDDED_FILE::FILE_TYPE aType )
{
    EMBEDDED_FILES staging;
    wxFileName     source( wxString::FromUTF8( aPath.c_str() ) );
    EMBEDDED_FILES::EMBEDDED_FILE* file = staging.AddFile( source, true );

    if( !file )
        return nullptr;

    staging.RemoveFile( source.GetFullName(), false );   // ownership leaves the staging set
    file->name = aName;
    file->type = aType;
    return file;
}

} // namespace fp

/**
 * fpedit.footprint.get (cap fpedit): FootprintProps (FP spec 7.3) of the footprint the frame
 * edits, as DIALOG_FOOTPRINT_PROPERTIES_FP_EDITOR::TransferDataToWindow reads it. "" with no
 * ready frame or no footprint. A pure read.
 */
std::string fpEditFootprintGet()
{
    FOOTPRINT_EDIT_FRAME* fr = cc_fpedit::readyFrame();
    FOOTPRINT*            footprint = cc_fpedit::currentFootprint( fr );

    if( !fr || !footprint )
        return "";

    return cc_fpedit::dumpJson( fp::footprintJson( fr, footprint ) );
}

/**
 * fpedit.footprint.set (cap fpedit): aArgsJson exactly {"set": FootprintSet}, read whole and
 * checked against the footprint before anything is queued; on the apply coroutine
 * TransferDataFromWindow's setters in its order on ONE BOARD_COMMIT pushed as "Footprint
 * properties" (one undo step), then editFootprintProperties' follow up (the enabled layers,
 * the title, the message panel). A name set renames the footprint in the editor only. False
 * for any other shape, no frame or footprint, an open in flight or a dialog up.
 */
bool fpEditFootprintSet( std::string aArgsJson )
{
    json args = json::parse( aArgsJson, nullptr, false );

    if( args.is_discarded() )
        return false;

    auto set = std::make_shared<fp::FOOTPRINT_SET>();

    if( !fp::readFootprintSet( args, *set ) )
        return false;

    FOOTPRINT_EDIT_FRAME* fr = cc_fpedit::readyFrame();
    FOOTPRINT*            footprint = cc_fpedit::currentFootprint( fr );

    if( !fr || !footprint || !fp::fits( fr, footprint, *set ) )
        return false;

    pcbjam_collab::runOnCoroutine( fr, [set]() {
        FOOTPRINT_EDIT_FRAME* frame = cc_fpedit::fpEditFrame();   // re-resolved on the coroutine
        FOOTPRINT*            fpNow = cc_fpedit::currentFootprint( frame );

        if( !frame || !fpNow )
            return;

        BOARD_COMMIT commit( frame );

        if( fp::applyFootprintSet( frame, fpNow, commit, *set ) && !commit.Empty() )
        {
            commit.Push( _( "Footprint properties" ) );
            fp::afterProperties( frame );
        }
        else
        {
            commit.Revert();
        }
    } );

    return true;
}

/**
 * fpedit.embed (cap fpedit): aArgsJson exactly {"path": "/tmp/cc-embed/<n>/<file>", "name",
 * "type": "model" | "datasheet" | "font" | "other"} adds that file (at most 25 MB) to the
 * footprint's embedded files by EMBEDDED_FILES::AddFile's read, under name (a file of that name
 * is replaced), or exactly {"remove": name} removes one that is there. ONE commit each,
 * "Embed a file" or "Remove an embedded file". False for any other shape, a path outside an
 * embed run folder, a file too large or missing, a name not there to remove, no frame, an open
 * in flight or a dialog up.
 */
bool fpEditEmbed( std::string aArgsJson )
{
    using cc_fpedit::hasKeys;
    using cc_fpedit::onlyKeys;

    json args = json::parse( aArgsJson, nullptr, false );

    if( args.is_discarded() )
        return false;

    FOOTPRINT_EDIT_FRAME* fr = cc_fpedit::readyFrame();
    FOOTPRINT*            footprint = cc_fpedit::currentFootprint( fr );

    if( !fr || !footprint )
        return false;

    if( onlyKeys( args, { "remove" } ) && hasKeys( args, { "remove" } ) )
    {
        std::optional<wxString> name = fp::embedNameOf( args["remove"] );

        if( !name || !footprint->GetEmbeddedFiles()->HasFile( *name ) )
            return false;

        const wxString removed = *name;

        return cc_fpedit::commitOnCoroutine( _( "Remove an embedded file" ),
                [removed]( FOOTPRINT_EDIT_FRAME*, FOOTPRINT* aFootprint, BOARD_COMMIT& aCommit )
                {
                    if( !aFootprint->GetEmbeddedFiles()->HasFile( removed ) )
                        return false;

                    aCommit.Modify( aFootprint );
                    aFootprint->GetEmbeddedFiles()->RemoveFile( removed );
                    return true;
                } );
    }

    if( !onlyKeys( args, { "path", "name", "type" } ) || !hasKeys( args, { "path", "name", "type" } )
        || !args["path"].is_string() )
    {
        return false;
    }

    const std::string path = args["path"].get<std::string>();
    std::optional<wxString> name = fp::embedNameOf( args["name"] );
    std::optional<EMBEDDED_FILES::EMBEDDED_FILE::FILE_TYPE> type = fp::embeddedTypeOf( args["type"] );

    if( !name || !type || !fp::isEmbedSource( path ) )
        return false;

    const wxString                                added = *name;
    const EMBEDDED_FILES::EMBEDDED_FILE::FILE_TYPE kind = *type;

    return cc_fpedit::commitOnCoroutine( _( "Embed a file" ),
            [path, added, kind]( FOOTPRINT_EDIT_FRAME*, FOOTPRINT* aFootprint, BOARD_COMMIT& aCommit )
            {
                std::unique_ptr<EMBEDDED_FILES::EMBEDDED_FILE> file(
                        fp::readEmbeddedFile( path, added, kind ) );

                if( !file )
                    return false;

                aCommit.Modify( aFootprint );

                EMBEDDED_FILES* files = aFootprint->GetEmbeddedFiles();

                if( files->HasFile( added ) )
                    files->RemoveFile( added );

                files->AddFile( file.release() );
                return true;
            } );
}

// ── fp-e6: the footprint checker, Tidy (cleanup and repair) and SVG (FP spec 5.1, 7.4) ──────

namespace fp
{
using namespace cc_fpedit;

/** Cleanup Graphics' tolerance as its dialog first shows it (dialog_cleanup_graphics.cpp 34). */
constexpr double CLEANUP_TOLERANCE_MM = 2.0;

/** Where a footprint SVG is plotted: /tmp/cc-svg/<n> (kicadSymEditPlotSvg's root). */
const std::string& svgRoot()
{
    static const std::string root = "/tmp/cc-svg/";
    return root;
}

/** A cleanup item's code: Cleanup Graphics' four kinds (CLEANUP_ITEM keeps no settings key). */
const char* cleanupCode( int aCode )
{
    switch( aCode )
    {
    case CLEANUP_NULL_GRAPHIC:      return "null_graphic";
    case CLEANUP_DUPLICATE_GRAPHIC: return "duplicate_graphic";
    case CLEANUP_LINES_TO_RECT:     return "lines_to_rect";
    case CLEANUP_MERGE_PAD:         return "merge_pad";
    default:                        return "other";
    }
}

/**
 * RepairFootprint's body (footprint_editor_control.cpp 909): a duplicate uuid is renewed, in
 * KiCad's claiming order (the footprint, its pads, Reference and Value, the graphics, zones,
 * groups). aApply false only counts. Returns how many it renewed (or would).
 */
int repairUuids( FOOTPRINT* aFootprint, bool aApply )
{
    std::set<KIID> ids;
    int            duplicates = 0;

    auto processItem =
            [&]( EDA_ITEM* aItem )
            {
                if( ids.count( aItem->m_Uuid ) )
                {
                    duplicates++;

                    if( aApply )
                        const_cast<KIID&>( aItem->m_Uuid ) = KIID();
                    else
                        return;
                }

                ids.insert( aItem->m_Uuid );
            };

    processItem( aFootprint );

    for( PAD* pad : aFootprint->Pads() )
        processItem( pad );

    processItem( &aFootprint->Reference() );
    processItem( &aFootprint->Value() );

    for( BOARD_ITEM* item : aFootprint->GraphicalItems() )
        processItem( item );

    for( ZONE* zone : aFootprint->Zones() )
        processItem( zone );

    for( PCB_GROUP* group : aFootprint->Groups() )
        processItem( group );

    return duplicates;
}

struct CLEANUP_ARGS
{
    bool mergeRects = false;
    bool deleteRedundant = false;
    bool mergePads = false;
    bool repair = false;
    bool dryRun = true;
};

/** {mergeRects, deleteRedundant, mergePads, repair, dryRun}, every key, booleans only. */
bool readCleanupArgs( const json& aArgs, CLEANUP_ARGS& aOut )
{
    if( !onlyKeys( aArgs, { "mergeRects", "deleteRedundant", "mergePads", "repair", "dryRun" } )
        || !hasKeys( aArgs, { "mergeRects", "deleteRedundant", "mergePads", "repair", "dryRun" } ) )
    {
        return false;
    }

    std::optional<bool> mergeRects = boolOf( aArgs["mergeRects"] );
    std::optional<bool> deleteRedundant = boolOf( aArgs["deleteRedundant"] );
    std::optional<bool> mergePads = boolOf( aArgs["mergePads"] );
    std::optional<bool> repair = boolOf( aArgs["repair"] );
    std::optional<bool> dryRun = boolOf( aArgs["dryRun"] );

    if( !mergeRects || !deleteRedundant || !mergePads || !repair || !dryRun )
        return false;

    aOut = CLEANUP_ARGS{ *mergeRects, *deleteRedundant, *mergePads, *repair, *dryRun };
    return true;
}

/**
 * DIALOG_CLEANUP_GRAPHICS::doCleanup's body (dialog_cleanup_graphics.cpp 99) on the footprint's
 * graphics into aCommit (the board outline fix off: a footprint has none), the items it found
 * or did as {code, uuids}.
 */
json runCleaner( FOOTPRINT_EDIT_FRAME* aFrame, FOOTPRINT* aFootprint, BOARD_COMMIT& aCommit,
                 const CLEANUP_ARGS& aArgs, bool aDryRun )
{
    std::vector<std::shared_ptr<CLEANUP_ITEM>> items;
    GRAPHICS_CLEANER cleaner( aFootprint->GraphicalItems(), aFootprint, aCommit,
                              aFrame->GetToolManager() );

    if( !aDryRun )
        aFrame->GetToolManager()->RunAction( ACTIONS::selectionClear );

    aFrame->Compile_Ratsnest( false );
    cleaner.CleanupBoard( aDryRun, &items, aArgs.mergeRects, aArgs.deleteRedundant,
                          aArgs.mergePads, false, iuOf( CLEANUP_TOLERANCE_MM ) );

    json out = json::array();

    for( const std::shared_ptr<CLEANUP_ITEM>& item : items )
    {
        if( !item )
            continue;

        json uuids = json::array();

        for( const KIID& id : item->GetIDs() )
        {
            if( id != niluuid )
                uuids.push_back( utf8( id.AsString() ) );
        }

        out.push_back( json::object( { { "code", cleanupCode( item->GetErrorCode() ) },
                                       { "uuids", uuids } } ) );
    }

    return out;
}

/** The layers the footprint draws on that the editor shows, in KiCad's plotting order. */
LSEQ svgLayers( FOOTPRINT_EDIT_FRAME* aFrame, const FOOTPRINT* aFootprint )
{
    const LSET   used = LAYER_UTILS::GetAllFootprintLayers( *aFootprint );
    KIGFX::VIEW* view = aFrame->GetCanvas()->GetView();
    LSET         shown;

    for( PCB_LAYER_ID layer : used.Seq() )
    {
        if( aFrame->GetBoard()->IsLayerEnabled( layer ) && view->IsLayerVisible( layer ) )
            shown.set( layer );
    }

    return shown.SeqStackupForPlotting();
}

/**
 * The footprint plotted to aFile as SVG by KiCad's own plotter, with what
 * PCBNEW_JOBS_HANDLER::doFpExportSvg fixes for a footprint (no frame, the page fitted to the
 * footprint, no mirror, NPTH pads plotted) over KiCad's SVG job's defaults with the footprint
 * SVG job's own (no sketch pads on fab layers, DNP sketched and crossed out, color). The
 * editor's own board is plotted: it holds the footprint alone, at the origin.
 */
bool plotFootprintSvg( FOOTPRINT_EDIT_FRAME* aFrame, const wxString& aFile )
{
    FOOTPRINT* footprint = currentFootprint( aFrame );

    if( !footprint )
        return false;

    const LSEQ layers = svgLayers( aFrame, footprint );

    if( layers.empty() )
        return false;

    JOB_EXPORT_PCB_SVG job;
    job.m_blackAndWhite = false;
    job.m_sketchPadsOnFabLayers = false;
    job.m_hideDNPFPsOnFabLayers = false;
    job.m_sketchDNPFPsOnFabLayers = true;
    job.m_crossoutDNPFPsOnFabLayers = true;
    job.m_plotDrawingSheet = false;
    job.m_fitPageToBoard = true;
    job.m_plotLayerSequence = layers;

    REPORTER&       reporter = NULL_REPORTER::GetInstance();
    PCB_PLOT_PARAMS opts;
    PCB_PLOTTER::PlotJobToPlotOpts( opts, &job, reporter );

    opts.SetPlotFrameRef( false );
    opts.SetSvgFitPageToBoard( true );
    opts.SetMirror( false );
    opts.SetSkipPlotNPTH_Pads( false );

    if( opts.GetSketchPadsOnFabLayers() )
        opts.SetPlotPadNumbers( true );

    PCB_PLOTTER plotter( aFrame->GetBoard(), &reporter, opts );

    return plotter.Plot( aFile, layers, LSEQ(), false, true, wxEmptyString, wxEmptyString,
                         wxEmptyString );
}

} // namespace fp

/**
 * fpedit.check (cap fpedit): DIALOG_FOOTPRINT_CHECKER::runChecks (dialog_footprint_checker.cpp
 * 88) in its order with collecting callbacks in place of markers: courtyard caches, missing
 * courtyard, attributes, pads, shorting pads, net tie groups and net ties (a net tie footprint
 * only), clipped silk. {findings: FpFinding[]}: code = DRC_ITEM::GetSettingsKey(), severity by
 * the board's settings (an ignored code left out), message KiCad's, uuids of the items, pad
 * numbers, the point in mm in the footprint's frame (null for a finding on the footprint
 * itself). A pure read; "" with no ready frame or no footprint.
 */
std::string fpEditCheck()
{
    using namespace cc_fpedit;

    FOOTPRINT_EDIT_FRAME* fr = readyFrame();
    FOOTPRINT*            footprint = currentFootprint( fr );

    if( !fr || !footprint )
        return "";

    BOARD_DESIGN_SETTINGS& settings = fr->GetBoard()->GetDesignSettings();
    const VECTOR2I         origin = footprint->GetPosition();
    json                   findings = json::array();

    auto errorHandler =
            [&]( const BOARD_ITEM* aItemA, const BOARD_ITEM* aItemB, const BOARD_ITEM* aItemC,
                 int aErrorCode, const wxString& aMsg, const std::optional<VECTOR2I>& aPt )
            {
                const SEVERITY severity = settings.GetSeverity( aErrorCode );

                if( severity == RPT_SEVERITY_IGNORE )
                    return;

                std::shared_ptr<DRC_ITEM> drcItem = DRC_ITEM::Create( aErrorCode );

                if( !drcItem )
                    return;

                if( !aMsg.IsEmpty() )
                    drcItem->SetErrorDetail( aMsg );

                json uuids = json::array();
                json pads = json::array();

                for( const BOARD_ITEM* item : { aItemA, aItemB, aItemC } )
                {
                    if( !item )
                        continue;

                    uuids.push_back( utf8( item->m_Uuid.AsString() ) );

                    if( item->Type() == PCB_PAD_T )
                        pads.push_back( utf8( static_cast<const PAD*>( item )->GetNumber() ) );
                }

                json f = json::object();
                f["code"] = utf8( drcItem->GetSettingsKey() );
                f["severity"] = severity == RPT_SEVERITY_ERROR ? "error" : "warning";
                f["message"] = utf8( drcItem->GetErrorMessage( true ) ).substr( 0, 4096 );
                f["uuids"] = uuids;
                f["pads"] = pads;
                f["x"] = aPt ? json( mmOf( (long long) aPt->x - origin.x ) ) : json( nullptr );
                f["y"] = aPt ? json( mmOf( (long long) aPt->y - origin.y ) ) : json( nullptr );
                findings.push_back( f );
            };

    OUTLINE_ERROR_HANDLER outlineErrorHandler =
            [&]( const wxString& aMsg, BOARD_ITEM* aItemA, BOARD_ITEM* aItemB, const VECTOR2I& aPt )
            {
                if( !aItemA )   // If we only have a single item, make sure it's A
                    std::swap( aItemA, aItemB );

                errorHandler( aItemA, aItemB, nullptr, DRCE_MALFORMED_COURTYARD, aMsg, aPt );
            };

    footprint->BuildCourtyardCaches( &outlineErrorHandler );

    if( !footprint->AllowMissingCourtyard()
            && footprint->GetCourtyard( F_CrtYd ).OutlineCount() == 0
            && footprint->GetCourtyard( B_CrtYd ).OutlineCount() == 0 )
    {
        errorHandler( footprint, nullptr, nullptr, DRCE_MISSING_COURTYARD, wxEmptyString,
                      std::nullopt );
    }

    footprint->CheckFootprintAttributes(
            [&]( const wxString& aMsg )
            {
                errorHandler( footprint, nullptr, nullptr, DRCE_FOOTPRINT_TYPE_MISMATCH, aMsg,
                              std::nullopt );
            } );

    footprint->CheckPads( fr,
            [&]( const PAD* aPad, int aErrorCode, const wxString& aMsg )
            {
                errorHandler( aPad, nullptr, nullptr, aErrorCode, aMsg, aPad->GetPosition() );
            } );

    footprint->CheckShortingPads(
            [&]( const PAD* aPadA, const PAD* aPadB, int aErrorCode, const VECTOR2I& aPosition )
            {
                errorHandler( aPadA, aPadB, nullptr, aErrorCode, wxEmptyString, aPosition );
            } );

    if( footprint->IsNetTie() )
    {
        footprint->CheckNetTiePadGroups(
                [&]( const wxString& aMsg )
                {
                    errorHandler( footprint, nullptr, nullptr, DRCE_FOOTPRINT, aMsg,
                                  std::nullopt );
                } );

        footprint->CheckNetTies(
                [&]( const BOARD_ITEM* aItemA, const BOARD_ITEM* aItemB, const BOARD_ITEM* aItemC,
                     const VECTOR2I& aPt )
                {
                    errorHandler( aItemA, aItemB, aItemC, DRCE_SHORTING_ITEMS, wxEmptyString, aPt );
                } );
    }

    footprint->CheckClippedSilk(
            [&]( BOARD_ITEM* aItemA, BOARD_ITEM* aItemB, const VECTOR2I& aPt )
            {
                errorHandler( aItemA, aItemB, nullptr, DRCE_SILK_MASK_CLEARANCE, wxEmptyString,
                              aPt );
            } );

    json out = json::object();
    out["findings"] = findings;
    return dumpJson( out );
}

/**
 * fpedit.cleanup (cap fpedit): aArgsJson exactly {mergeRects, deleteRedundant, mergePads,
 * repair, dryRun}. Cleanup Graphics (GRAPHICS_CLEANER::CleanupBoard on the footprint's
 * graphics, the dialog's tolerance, board outlines off) and, with repair, RepairFootprint's
 * body (duplicate uuids renewed). The answer {items: [{code, uuids}], repaired} is read at once
 * by a dry run of both; without dryRun the same runs on the apply coroutine in ONE commit
 * "Tidy footprint" (none when there is nothing to do). "" for any other shape, no frame or
 * footprint, an open in flight or a dialog up.
 */
std::string fpEditCleanup( std::string aArgsJson )
{
    json args = json::parse( aArgsJson, nullptr, false );

    if( args.is_discarded() )
        return "";

    fp::CLEANUP_ARGS cleanup;

    if( !fp::readCleanupArgs( args, cleanup ) )
        return "";

    FOOTPRINT_EDIT_FRAME* fr = cc_fpedit::readyFrame();
    FOOTPRINT*            footprint = cc_fpedit::currentFootprint( fr );

    if( !fr || !footprint )
        return "";

    json out = json::object();

    {
        BOARD_COMMIT dry( fr );   // a dry run stages nothing
        out["items"] = fp::runCleaner( fr, footprint, dry, cleanup, true );
    }

    out["repaired"] = cleanup.repair ? fp::repairUuids( footprint, false ) : 0;

    if( !cleanup.dryRun )
    {
        cc_fpedit::commitOnCoroutine( _( "Tidy footprint" ),
                [cleanup]( FOOTPRINT_EDIT_FRAME* aFrame, FOOTPRINT* aFootprint,
                           BOARD_COMMIT& aCommit )
                {
                    fp::runCleaner( aFrame, aFootprint, aCommit, cleanup, false );

                    if( cleanup.repair && fp::repairUuids( aFootprint, false ) > 0 )
                    {
                        aCommit.Modify( aFootprint );
                        fp::repairUuids( aFootprint, true );
                    }

                    if( !aCommit.Empty() )
                        aFrame->GetCanvas()->Refresh( true );

                    return true;
                } );
    }

    return cc_fpedit::dumpJson( out );
}

/**
 * fpedit.svg (cap fpedit): aArgsJson exactly {"dir": "/tmp/cc-svg/<n>"} (the run folder rule).
 * On the apply coroutine the footprint's shown layers are plotted by KiCad's SVG plotter into
 * <dir>/footprint.svg, then <dir>/.cc-svg.json last ({"error": "" | "plot_failed"}),
 * kicadSymEditPlotSvg's shape. False for any other shape, no frame or footprint, an open in
 * flight or a dialog up.
 */
bool fpEditPlotSvg( std::string aArgsJson )
{
    json args = json::parse( aArgsJson, nullptr, false );

    if( args.is_discarded() || !cc_fpedit::onlyKeys( args, { "dir" } )
        || !cc_fpedit::hasKeys( args, { "dir" } ) || !args["dir"].is_string() )
    {
        return false;
    }

    const std::string dir = args["dir"].get<std::string>();

    if( dir.size() > fp::PATH_MAX_CHARS || !cc_output::IsRunDir( dir, fp::svgRoot() ) )
        return false;

    FOOTPRINT_EDIT_FRAME* fr = cc_fpedit::readyFrame();

    if( !fr || !cc_fpedit::currentFootprint( fr ) )
        return false;

    pcbjam_collab::runOnCoroutine( fr, [dir]() {
        const wxString file = wxString::FromUTF8( dir.c_str() ) + wxS( "/footprint.svg" );

        cc_output::EnsureDir( dir );

        if( wxFileName::FileExists( file ) )
            wxRemoveFile( file );

        FOOTPRINT_EDIT_FRAME* frame = cc_fpedit::fpEditFrame();   // re-resolved on the coroutine
        bool                  plotted = false;

        if( frame )
        {
            try
            {
                if( fp::plotFootprintSvg( frame, file ) )
                {
                    if( FILE* f = std::fopen( file.utf8_str(), "rb" ) )
                    {
                        plotted = std::fgetc( f ) != EOF;
                        std::fclose( f );
                    }
                }
            }
            catch( ... )
            {
                plotted = false;
            }
        }

        json done = json::object();
        done["error"] = plotted ? "" : "plot_failed";
        cc_output::WriteRecord( dir, ".cc-svg.json", done );
    } );

    return true;
}
} // namespace cc_e5

EMSCRIPTEN_BINDINGS( cc_e5_fpedit )
{
    function( "kicadFpEditFootprintGet", &cc_e5::fpEditFootprintGet );
    function( "kicadFpEditFootprintSet", &cc_e5::fpEditFootprintSet );
    function( "kicadFpEditEmbed", &cc_e5::fpEditEmbed );
    function( "kicadFpEditCheck", &cc_e5::fpEditCheck );
    function( "kicadFpEditCleanup", &cc_e5::fpEditCleanup );
    function( "kicadFpEditPlotSvg", &cc_e5::fpEditPlotSvg );
}
