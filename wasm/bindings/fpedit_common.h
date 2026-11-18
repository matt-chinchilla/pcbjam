// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e4: the shared plumbing of the footprint editor frame's exports
// (FOOTPRINT_EDIT_FRAME, --frame fpedit). Header only, inline only (the collab_common.h
// pattern), taken by fpedit_embind.cpp through its __has_include slot, so every lane's
// footprint editor face (cc_e4_fpedit.inc, cc_e5_fpedit.inc, cc_e6_fpedit.inc) sees it.
//
// What it gives (namespace cc_fpedit; fpEditFrame() also at global scope):
//   fpEditFrame()       the top window as the footprint editor, or nullptr;
//   dialogUp()          a shown dialog of any kind (KiCad's or wx's);
//   readyFrame()        the frame when it may be driven: none while an open is in flight
//                       (pcbjam_open::busy()) or a dialog is up;
//   currentFootprint()  the footprint the frame edits, or nullptr;
//   the closed JSON readers (onlyKeys, hasKeys, textOf, numberOf, intOf, boolOf, tokenOf,
//                       isLowerUuid, uuidsOf), the units (mmOf, iuOf, pointJson, pointOf,
//                       degreesOf), the layers by KiCad's canonical names (layerName,
//                       layerOf), dumpJson;
//   commitOnCoroutine() the one commit helper: the body runs on the apply coroutine with a
//                       BOARD_COMMIT on the re-resolved frame, and is pushed as ONE undo
//                       step under its label, or reverted when the body answers false.
//
// The house rules of every export (run A 3.4): closed JSON args read whole before anything
// is queued; false or "" for any other shape, a missing frame, an open in flight or a dialog
// up; a write runs on the apply coroutine as ONE commit; a read is synchronous; lengths in
// mm, angles in degrees, positions in the footprint's own frame (KiCad's footprint editor
// axes, Y down); no KiCad dialog, message box, progress box or info bar on any road.

#pragma once

#ifdef __EMSCRIPTEN__

#include <cmath>
#include <functional>
#include <initializer_list>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <wx/app.h>
#include <wx/dialog.h>
#include <wx/string.h>
#include <wx/toplevel.h>
#include <wx/window.h>

#include <nlohmann/json.hpp>

#include <base_units.h>
#include <board.h>
#include <board_commit.h>
#include <footprint.h>
#include <footprint_edit_frame.h>
#include <geometry/eda_angle.h>
#include <layer_ids.h>
#include <lset.h>
#include <math/vector2d.h>

#include "collab_common.h"
#include "open_gate.h"

namespace cc_fpedit
{

using json = nlohmann::json;

// ── bounds (FP spec 7: strings at most 1,024 characters unless named) ─────────────────

constexpr size_t TEXT_MAX = 1024;
constexpr size_t FOOTPRINT_TEXT_MAX = 8u * 1024u * 1024u;   // one (footprint ...) (spec 5.1)
constexpr double POSITION_MAX_MM = 50000.0;
constexpr double SIZE_MAX_MM = 1000.0;
constexpr size_t UUIDS_MAX = 4096;
constexpr size_t NEW_ITEMS_MAX = 512;
constexpr size_t POINTS_MAX = 4096;

// ── the frame and the gates ───────────────────────────────────────────────────────────

/** The footprint editor when it is the top window (an fpedit frame), else nullptr. */
inline FOOTPRINT_EDIT_FRAME* fpEditFrame()
{
    return wxTheApp ? dynamic_cast<FOOTPRINT_EDIT_FRAME*>( wxTheApp->GetTopWindow() ) : nullptr;
}

/** A shown dialog of any kind (KiCad's or wx's): the frame is not to be driven meanwhile. */
inline bool dialogUp()
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
inline FOOTPRINT_EDIT_FRAME* readyFrame()
{
    FOOTPRINT_EDIT_FRAME* fr = fpEditFrame();

    if( !fr || pcbjam_open::busy() || dialogUp() )
        return nullptr;

    return fr;
}

/** The footprint the frame edits (the first and only footprint of its board), or nullptr. */
inline FOOTPRINT* currentFootprint( const FOOTPRINT_EDIT_FRAME* aFrame )
{
    if( !aFrame || !aFrame->GetBoard() )
        return nullptr;

    return aFrame->GetBoard()->GetFirstFootprint();
}

inline std::string dumpJson( const json& aValue )
{
    return aValue.dump( -1, ' ', false, json::error_handler_t::replace );
}

inline std::string utf8( const wxString& aText )
{
    return std::string( aText.utf8_str() );
}

// ── JSON readers (closed shapes, bounded values) ──────────────────────────────────────

/** An object whose every key is one of aKeys (a key may be missing). */
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

/** An object that holds every one of aKeys. */
inline bool hasKeys( const json& aObj, std::initializer_list<const char*> aKeys )
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
inline std::optional<wxString> textOf( const json& aValue, size_t aMin, size_t aMax,
                                       bool aLines = false )
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
inline std::optional<double> numberOf( const json& aValue, double aMin, double aMax )
{
    if( !aValue.is_number() )
        return std::nullopt;

    const double v = aValue.get<double>();

    if( !std::isfinite( v ) || v < aMin || v > aMax )
        return std::nullopt;

    return v;
}

/** An integer in [aMin, aMax]. */
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

/** A list of lowercase uuids, aMin to aMax of them, no repeats. */
inline std::optional<std::vector<std::string>> uuidsOf( const json& aValue, size_t aMin,
                                                        size_t aMax )
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

// ── units, points, angles (mm, degrees, the footprint's own frame, Y down) ────────────

/** Internal units as mm, rounded to 0.1 um. */
inline double mmOf( long long aIu )
{
    return std::round( static_cast<double>( aIu ) / pcbIUScale.IU_PER_MM * 10000.0 ) / 10000.0;
}

/** mm as internal units (the caller bounds aMm first). */
inline int iuOf( double aMm )
{
    return pcbIUScale.mmToIU( aMm );
}

/** A point relative to aOrigin, in mm. */
inline json pointJson( const VECTOR2I& aPoint, const VECTOR2I& aOrigin = VECTOR2I( 0, 0 ) )
{
    return json::array( { mmOf( (long long) aPoint.x - aOrigin.x ),
                          mmOf( (long long) aPoint.y - aOrigin.y ) } );
}

/** [x, y] in mm, each within POSITION_MAX_MM, as a point relative to aOrigin; or nothing. */
inline std::optional<VECTOR2I> pointOf( const json& aValue,
                                        const VECTOR2I& aOrigin = VECTOR2I( 0, 0 ) )
{
    if( !aValue.is_array() || aValue.size() != 2 )
        return std::nullopt;

    std::optional<double> x = numberOf( aValue[0], -POSITION_MAX_MM, POSITION_MAX_MM );
    std::optional<double> y = numberOf( aValue[1], -POSITION_MAX_MM, POSITION_MAX_MM );

    if( !x || !y )
        return std::nullopt;

    return VECTOR2I( iuOf( *x ) + aOrigin.x, iuOf( *y ) + aOrigin.y );
}

/** An angle in degrees, normalized to [0, 360) and rounded to 0.001 degree. */
inline double degreesOf( const EDA_ANGLE& aAngle )
{
    EDA_ANGLE a = aAngle;
    a.Normalize();
    return std::round( a.AsDegrees() * 1000.0 ) / 1000.0;
}

// ── layers by KiCad's canonical names (F.Cu, In1.Cu, F.SilkS, User.1) ─────────────────

inline std::string layerName( PCB_LAYER_ID aLayer )
{
    return utf8( LSET::Name( aLayer ) );
}

/** A canonical layer name as its layer, or nothing (UNDEFINED and the special layers refused). */
inline std::optional<PCB_LAYER_ID> layerOf( const json& aValue )
{
    if( !aValue.is_string() )
        return std::nullopt;

    const wxString name = wxString::FromUTF8( aValue.get_ref<const std::string&>().c_str() );

    if( name.IsEmpty() )
        return std::nullopt;

    for( PCB_LAYER_ID id : LSET::AllLayersMask().Seq() )
    {
        if( LSET::Name( id ) == name )
            return id;
    }

    return std::nullopt;
}

/** A list of canonical layer names as a layer set, 1 to aMax of them, no repeats; or nothing. */
inline std::optional<LSET> layersOf( const json& aValue, size_t aMin = 1, size_t aMax = 256 )
{
    if( !aValue.is_array() || aValue.size() < aMin || aValue.size() > aMax )
        return std::nullopt;

    LSET out;

    for( const json& n : aValue )
    {
        std::optional<PCB_LAYER_ID> id = layerOf( n );

        if( !id || out.Contains( *id ) )
            return std::nullopt;

        out.set( *id );
    }

    return out;
}

inline json layersJson( const LSET& aLayers )
{
    json out = json::array();

    for( PCB_LAYER_ID id : aLayers.Seq() )
        out.push_back( layerName( id ) );

    return out;
}

// ── the one commit helper ─────────────────────────────────────────────────────────────

/**
 * Queues aBody on the apply coroutine. There the frame is re-resolved (it may have gone),
 * a BOARD_COMMIT is made on it and handed to the body with the footprint the frame edits;
 * a body that answers true is pushed as ONE undo step under aLabel, one that answers false is
 * reverted (nothing changes, no undo step). False, nothing queued, with no ready frame.
 */
inline bool commitOnCoroutine( const wxString& aLabel,
                               std::function<bool( FOOTPRINT_EDIT_FRAME*, FOOTPRINT*,
                                                   BOARD_COMMIT& )> aBody )
{
    FOOTPRINT_EDIT_FRAME* fr = readyFrame();

    if( !fr || !aBody )
        return false;

    pcbjam_collab::runOnCoroutine( fr, [aLabel, aBody]() {
        FOOTPRINT_EDIT_FRAME* frame = fpEditFrame();   // re-resolved on the coroutine
        FOOTPRINT*            footprint = currentFootprint( frame );

        if( !frame || !footprint )
            return;

        BOARD_COMMIT commit( frame );

        if( aBody( frame, footprint, commit ) && !commit.Empty() )
            commit.Push( aLabel );
        else
            commit.Revert();
    } );

    return true;
}

} // namespace cc_fpedit

using cc_fpedit::fpEditFrame;

#endif // __EMSCRIPTEN__
