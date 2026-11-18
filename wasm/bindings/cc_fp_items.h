// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15 (R1): the footprint item kinds, frame neutral. DECLARATIONS ONLY: the single
// definitions are lane e4's, in namespace cc_fpitems inside cc_e4_fpedit.inc (the fpedit
// translation unit); pcbnew_embind.cpp includes this header too, so the board face (lane e3's
// board item records) calls the same read and apply and links to them.
//
// A kind is one row of a table: its name (exactly pad, field, text, textbox, shape,
// dimension, zone, table, barcode, image, point), its read as JSON, its set as a patch inside
// one BOARD_COMMIT, and its add into a container. Positions in the read and the apply are
// relative to aOrigin; aBoardFrame says which frame the item lives in (the board editor, or
// the footprint editor's own).

#pragma once

#ifdef __EMSCRIPTEN__

#include <math/vector2d.h>
#include <nlohmann/json.hpp>

#include <string>

class BOARD_COMMIT;
class BOARD_ITEM;
class BOARD_ITEM_CONTAINER;

namespace cc_fpitems
{

using json = nlohmann::json;

/** One item kind: its wire name and its three bodies (lane e4 and the kind headers fill them). */
struct CcItemKind
{
    const char* name;
    json ( *get )( BOARD_ITEM* aItem );
    bool ( *set )( BOARD_ITEM* aItem, const json& aSet, BOARD_COMMIT& aCommit );
    BOARD_ITEM* ( *add )( BOARD_ITEM_CONTAINER* aParent, const json& aItem );
};

/** Adds a kind to the table (a kind registered twice keeps its first row). */
void ccRegisterItemKind( const CcItemKind& aKind );

/** The kind of that name, or nullptr. */
const CcItemKind* ccItemKind( const std::string& aName );

/** An item's record, positions relative to aOrigin. */
json ccFpItemJson( const BOARD_ITEM* aItem, const VECTOR2I& aOrigin, bool aBoardFrame );

/** A patch applied to an item inside aCommit; false when the patch is refused (nothing changed). */
bool ccFpItemApply( BOARD_ITEM* aItem, const json& aSet, BOARD_COMMIT& aCommit,
                    const VECTOR2I& aOrigin, bool aBoardFrame );

} // namespace cc_fpitems

#endif // __EMSCRIPTEN__
