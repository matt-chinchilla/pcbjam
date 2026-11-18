/*
 * JS → editor library reload (r2-idb-sync realtime): drop a PCBJAM lib's
 * in-plugin cache and re-sync the open editor's library tree after a remote
 * (peer) edit landed in the page's SyncStack/IDB.
 *
 * The lib plugins (SCH_IO_PCBJAM_LIB / PCB_IO_PCBJAM_FP) cache parsed items for
 * the plugin instance's lifetime and pin GetModifyHash()/GetLibraryTimestamp()
 * to a constant, so nothing upstream ever re-reads the provider. The one full
 * invalidation is LIBRARY_MANAGER::ReloadLibraryEntry — it erases the LIB_DATA
 * entry INCLUDING the plugin instance (and with it the fat-load cache), the
 * exact pattern the remote-symbol import flows already use
 * (eeschema/widgets/panel_remote_symbol.cpp). LoadLibraryEntry then refetches
 * synchronously (the fat "bodies" crossing — served from the now-fresh IDB, no
 * network), and MAIL_RELOAD_LIB makes an open editor frame rebuild its tree.
 *
 * Header-only (the collab_common.h pattern); common-code includes only, so the
 * merged kicad_editor TU (deliberately eeschema/pcbnew-header-free) can use it.
 * Runs on the apply coroutine: LoadLibraryEntry suspends in the JS bridge, so
 * the reload must serialize with the collab applies and local edits.
 *
 * Modified by Circuit Center Inc on 2026-11-18: addLibraryEntry takes the kind design_block (a
 * KiCad row of the design block global table whose uri is a real folder), and the run time rows
 * it adds can be removed again (removeLibraryEntry); setPathVars sets path variables as KiCad's
 * Configure Paths does and reloads the rows that name them (engine run 15, e-librows).
 */

#pragma once

#ifdef __EMSCRIPTEN__

#include <algorithm>
#include <deque>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <wx/app.h>
#include <wx/utils.h>
#include <wx/string.h>
#include <kiway.h>
#include <kiway_player.h>
#include <libraries/library_manager.h>
#include <libraries/library_table.h>
#include <mail_type.h>
#include <pgm_base.h>
#include <project.h>
#include <env_vars.h>
#include <settings/environment.h>
#include <settings/settings_manager.h>

#include "collab_common.h"

namespace pcbjam_libs {

/**
 * Reload one library from its provider and refresh any open editor tree.
 * @param aKind "symbol" | "footprint" — which lib table the nickname lives in.
 * @param aNickname the lib-table row name (LibInfo.name on the JS side).
 */
inline void reloadLibrary( std::string aKind, std::string aNickname )
{
    KIWAY_PLAYER* top =
            wxTheApp ? dynamic_cast<KIWAY_PLAYER*>( wxTheApp->GetTopWindow() ) : nullptr;

    if( !top )
        return;

    const bool     fp = aKind == "footprint";
    const wxString nick = wxString::FromUTF8( aNickname.c_str() );

    pcbjam_collab::runOnCoroutine( top, [top, fp, nick]()
    {
        LIBRARY_MANAGER&         mgr = Pgm().GetLibraryManager();
        const LIBRARY_TABLE_TYPE type =
                fp ? LIBRARY_TABLE_TYPE::FOOTPRINT : LIBRARY_TABLE_TYPE::SYMBOL;

        // Drop the LIB_DATA entry (plugin instance + its fat-load cache)…
        mgr.ReloadLibraryEntry( type, nick );
        // …and bring the lib back to LOADED synchronously, so the tree sync
        // below reads fresh items instead of re-listing a LOADING stub.
        mgr.LoadLibraryEntry( type, nick );

        // An open editor frame re-syncs its tree from the (now fresh) adapter.
        // The payload names the lib to FORCE-refresh: the symbol tree gates
        // re-enumeration on a modify hash that is a pinned constant for PCBJAM
        // plugins (and its mtime component never moves for a remote edit), so a
        // plain sync would skip the lib; the named node is rebuilt instead.
        // ExpressMail only delivers to frames that exist — no frame is created.
        std::string payload( nick.utf8_str() );
        top->Kiway().ExpressMail( fp ? FRAME_FOOTPRINT_EDITOR : FRAME_SCH_SYMBOL_EDITOR,
                                  MAIL_RELOAD_LIB, payload );
    } );
}

/**
 * Cheap counterpart of reloadLibrary (libs 0019): drop the lib's LIB_DATA
 * entry (plugin instance + parsed cache) so the NEXT access re-reads the
 * provider — no eager LoadLibraryEntry fat-load, no tree mail. A remote edit
 * calls this; the fat-load happens lazily when the user looks at the lib,
 * or explicitly through reloadLibrary from "Update from library".
 * (pcbnew's FOOTPRINT_LIBRARY_ADAPTER::PreloadedFootprints is invalidated by
 * the pcbnew-side caller — this header stays common-code only.)
 */
inline void invalidateLibrary( std::string aKind, std::string aNickname )
{
    KIWAY_PLAYER* top =
            wxTheApp ? dynamic_cast<KIWAY_PLAYER*>( wxTheApp->GetTopWindow() ) : nullptr;

    if( !top )
        return;

    const bool     fp = aKind == "footprint";
    const wxString nick = wxString::FromUTF8( aNickname.c_str() );

    pcbjam_collab::runOnCoroutine( top, [fp, nick]()
    {
        Pgm().GetLibraryManager().ReloadLibraryEntry(
                fp ? LIBRARY_TABLE_TYPE::FOOTPRINT : LIBRARY_TABLE_TYPE::SYMBOL, nick );
    } );
}

/**
 * The table type of a kind the exports take: "symbol", "footprint" or "design_block"
 * (UNINITIALIZED for any other word).
 */
inline LIBRARY_TABLE_TYPE tableTypeOf( const std::string& aKind )
{
    if( aKind == "symbol" )
        return LIBRARY_TABLE_TYPE::SYMBOL;

    if( aKind == "footprint" )
        return LIBRARY_TABLE_TYPE::FOOTPRINT;

    if( aKind == "design_block" )
        return LIBRARY_TABLE_TYPE::DESIGN_BLOCK;

    return LIBRARY_TABLE_TYPE::UNINITIALIZED;
}

/**
 * The rows addLibraryEntry inserted in this session, by table type and nickname: the only rows
 * removeLibraryEntry removes (a row the boot wrote is never removed).
 */
inline std::set<std::pair<int, wxString>>& runTimeRows()
{
    static std::set<std::pair<int, wxString>> s_rows;
    return s_rows;
}

/**
 * The design block adapter is registered by the project on its first use
 * (PROJECT::DesignBlockLibs); a design block row added before any use makes it exist first.
 */
inline void ensureDesignBlockAdapter()
{
    if( !Pgm().GetLibraryManager().Adapter( LIBRARY_TABLE_TYPE::DESIGN_BLOCK ) )
        Pgm().GetSettingsManager().Prj().DesignBlockLibs();
}

/**
 * Add one PCBJAM lib-table row at RUNTIME and load it — the lib SET is
 * otherwise frozen at boot (sym/fp-lib-table are written once in preRun).
 * Used by the "a new team library appeared" flow: the JS side has already
 * created the `/mnt/pcbjam/<id>` MEMFS placeholder; this inserts the row into
 * the LIVE in-memory global table (the table FILE is only re-read by
 * LoadGlobalTables, which would drop every adapter cache — deliberately not
 * used), loads the entry through the provider bridge, and re-syncs any open
 * editor tree. Idempotent: an existing nickname is a no-op.
 *
 * @param aKind "symbol" | "footprint" | "design_block": which lib table gets the row.
 *   A design_block row is a KiCad row (KiCad's own DESIGN_BLOCK_IO reads the folder): its uri
 *   is a real `.kicad_blocks` folder in the engine's file system, not a PCBJAM mount.
 * @param aNickname the lib-table row name (LibInfo.name on the JS side).
 * @param aUri the virtual mount uri ("/mnt/pcbjam/<libId>"), or the folder of a design_block row.
 */
inline void addLibraryEntry( std::string aKind, std::string aNickname, std::string aUri )
{
    KIWAY_PLAYER* top =
            wxTheApp ? dynamic_cast<KIWAY_PLAYER*>( wxTheApp->GetTopWindow() ) : nullptr;

    if( !top )
        return;

    const LIBRARY_TABLE_TYPE type = tableTypeOf( aKind );

    if( type == LIBRARY_TABLE_TYPE::UNINITIALIZED )
        return;

    const bool     fp = type == LIBRARY_TABLE_TYPE::FOOTPRINT;
    const bool     block = type == LIBRARY_TABLE_TYPE::DESIGN_BLOCK;
    const wxString nick = wxString::FromUTF8( aNickname.c_str() );
    const wxString uri = wxString::FromUTF8( aUri.c_str() );

    pcbjam_collab::runOnCoroutine( top, [top, type, fp, block, nick, uri]()
    {
        LIBRARY_MANAGER& mgr = Pgm().GetLibraryManager();

        if( block )
            ensureDesignBlockAdapter();

        if( std::optional<LIBRARY_MANAGER_ADAPTER*> adapter = mgr.Adapter( type ); adapter )
        {
            LIBRARY_TABLE* table = ( *adapter )->GlobalTable();

            if( !table )
                return;

            // Idempotent-cheap: a nickname that already has a row is done —
            // content refresh is kicadLibsReload's job, and doing it here
            // would let a re-announce trigger a pointless re-fat-load.
            if( table->HasRow( nick ) )
                return;

            LIBRARY_TABLE_ROW& row = table->InsertRow();
            row.SetNickname( nick );
            row.SetURI( uri );
            row.SetType( block ? wxS( "KiCad" ) : fp ? wxS( "PCBJAM_FP" ) : wxS( "PCBJAM" ) );
            runTimeRows().emplace( static_cast<int>( type ), nick );
        }
        else
        {
            return;
        }

        // Bring the new row to LOADED through the JS provider bridge (GetRow
        // walks the table on a cache miss, so the inserted row is found); a
        // design block row through KiCad's own reader of its folder.
        mgr.LoadLibraryEntry( type, nick );

        // No editor tree of design blocks is ever shown (the page lists them).
        if( block )
            return;

        // Open editor frames re-sync their tree; the nickname payload forces
        // the named node's rebuild past the pinned-modify-hash gate (same
        // reasoning as reloadLibrary above).
        std::string payload( nick.utf8_str() );
        top->Kiway().ExpressMail( fp ? FRAME_FOOTPRINT_EDITOR : FRAME_SCH_SYMBOL_EDITOR,
                                  MAIL_RELOAD_LIB, payload );
    } );
}

/**
 * Remove a row addLibraryEntry added in this session and drop what was loaded from it (engine
 * run 15, kicadLibsRemoveEntry). The row is erased from the live global table, the table is
 * saved, and the global tables of that type are read again (LIBRARY_MANAGER::ReloadTables, the
 * road KiCad's own library table dialogs and design block pane take after a table edit): that
 * is the one road that drops every cached pointer to a table row (the adapter's LIB_DATA and the
 * manager's row cache) before the deque the rows live in moves. The other rows load again on
 * their next use, from their provider. An open editor tree of that kind is re-synced.
 *
 * @return false for a kind other than symbol, footprint or design_block, a nickname this
 *         session did not add (a boot row), or no frame; true once the removal is queued.
 */
inline bool removeLibraryEntry( std::string aKind, std::string aNickname )
{
    KIWAY_PLAYER* top =
            wxTheApp ? dynamic_cast<KIWAY_PLAYER*>( wxTheApp->GetTopWindow() ) : nullptr;
    const LIBRARY_TABLE_TYPE type = tableTypeOf( aKind );

    if( !top || type == LIBRARY_TABLE_TYPE::UNINITIALIZED )
        return false;

    const wxString nick = wxString::FromUTF8( aNickname.c_str() );
    auto           key = std::make_pair( static_cast<int>( type ), nick );

    if( !runTimeRows().contains( key ) )
        return false;

    runTimeRows().erase( key );

    pcbjam_collab::runOnCoroutine( top, [top, type, nick]()
    {
        LIBRARY_MANAGER& mgr = Pgm().GetLibraryManager();
        std::optional<LIBRARY_TABLE*> table = mgr.Table( type, LIBRARY_TABLE_SCOPE::GLOBAL );

        if( !table || !*table )
            return;

        std::deque<LIBRARY_TABLE_ROW>& rows = ( *table )->Rows();
        auto it = std::find_if( rows.begin(), rows.end(),
                                [&]( const LIBRARY_TABLE_ROW& aRow )
                                {
                                    return aRow.Nickname() == nick;
                                } );

        if( it == rows.end() )
            return;

        rows.erase( it );

        if( !( *table )->Save().has_value() )
            return;

        mgr.ReloadTables( LIBRARY_TABLE_SCOPE::GLOBAL, { type } );

        if( type == LIBRARY_TABLE_TYPE::DESIGN_BLOCK )
            return;

        std::string payload( nick.utf8_str() );
        top->Kiway().ExpressMail( type == LIBRARY_TABLE_TYPE::FOOTPRINT ? FRAME_FOOTPRINT_EDITOR
                                                                         : FRAME_SCH_SYMBOL_EDITOR,
                                  MAIL_RELOAD_LIB, payload );
    } );

    return true;
}

/**
 * Whether a path variable name may be set from the host: the island's form (an upper case
 * letter, then up to 63 upper case letters, digits or underscores), never KIPRJMOD and never
 * one of KiCad's predefined variables (ENV_VAR::IsEnvVarImmutable, the versioned library
 * folders among them).
 */
inline bool pathVarNameOk( const std::string& aName )
{
    if( aName.empty() || aName.size() > 64 || aName[0] < 'A' || aName[0] > 'Z' )
        return false;

    for( char c : aName )
    {
        if( !( ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_' ) )
            return false;
    }

    const wxString name = wxString::FromUTF8( aName.c_str() );

    if( name == PROJECT_VAR_NAME || ENV_VAR::IsEnvVarImmutable( name ) )
        return false;

    return true;
}

/**
 * Set path variables (engine run 15, kicadPathVarsSet) as KiCad's Configure Paths dialog does
 * on OK: each name's value in the common settings' variable map, then
 * PGM_BASE::SetLocalEnvVariables puts the map into the process environment. Then every row of
 * every table whose uri names one of them, ${NAME} or $(NAME), is reloaded as reloadLibrary
 * reloads one (its plugin dropped and loaded again), so a row that failed before the variable
 * existed now loads. A variable is never removed here.
 *
 * @param aVars name to absolute folder, already checked by the caller (pathVarNameOk, values
 *        starting with '/').
 *        The caller also drops pcbnew's preloaded footprints of the footprint rows named
 *        (footprintRowsNaming), which this common header cannot reach.
 * @return false with no frame; true once the work is queued.
 */
inline bool setPathVars( std::vector<std::pair<std::string, std::string>> aVars )
{
    KIWAY_PLAYER* top =
            wxTheApp ? dynamic_cast<KIWAY_PLAYER*>( wxTheApp->GetTopWindow() ) : nullptr;

    if( !top || !Pgm().GetCommonSettings() )
        return false;

    pcbjam_collab::runOnCoroutine( top, [top, aVars]()
    {
        ENV_VAR_MAP& envVarMap = Pgm().GetLocalEnvVariables();
        std::vector<wxString> names;

        for( const auto& [name, value] : aVars )
        {
            const wxString n = wxString::FromUTF8( name.c_str() );
            const wxString v = wxString::FromUTF8( value.c_str() );

            if( envVarMap.count( n ) )
                envVarMap.at( n ).SetValue( v );
            else
                envVarMap[n] = ENV_VAR_ITEM( n, v );

            names.push_back( n );
        }

        Pgm().SetLocalEnvVariables();

        LIBRARY_MANAGER& mgr = Pgm().GetLibraryManager();

        for( LIBRARY_TABLE_TYPE type : { LIBRARY_TABLE_TYPE::SYMBOL, LIBRARY_TABLE_TYPE::FOOTPRINT,
                                         LIBRARY_TABLE_TYPE::DESIGN_BLOCK } )
        {
            if( !mgr.Adapter( type ) )
                continue;

            std::vector<wxString> nicks;

            for( const LIBRARY_TABLE_ROW* row : mgr.Rows( type, LIBRARY_TABLE_SCOPE::BOTH, true ) )
            {
                for( const wxString& n : names )
                {
                    if( row->URI().Contains( wxS( "${" ) + n + wxS( "}" ) )
                        || row->URI().Contains( wxS( "$(" ) + n + wxS( ")" ) ) )
                    {
                        nicks.push_back( row->Nickname() );
                        break;
                    }
                }
            }

            for( const wxString& nick : nicks )
            {
                mgr.ReloadLibraryEntry( type, nick );
                mgr.LoadLibraryEntry( type, nick );

                if( type == LIBRARY_TABLE_TYPE::DESIGN_BLOCK )
                    continue;

                std::string payload( nick.utf8_str() );
                top->Kiway().ExpressMail( type == LIBRARY_TABLE_TYPE::FOOTPRINT
                                                  ? FRAME_FOOTPRINT_EDITOR
                                                  : FRAME_SCH_SYMBOL_EDITOR,
                                          MAIL_RELOAD_LIB, payload );
            }
        }
    } );

    return true;
}

/**
 * The nicknames of the footprint rows whose uri names one of aNames (read at once, for the
 * caller to drop pcbnew's preloaded footprints of those libraries).
 */
inline std::vector<std::string> footprintRowsNaming( const std::vector<std::string>& aNames )
{
    std::vector<std::string> out;
    LIBRARY_MANAGER&         mgr = Pgm().GetLibraryManager();

    if( !mgr.Adapter( LIBRARY_TABLE_TYPE::FOOTPRINT ) )
        return out;

    for( const LIBRARY_TABLE_ROW* row :
         mgr.Rows( LIBRARY_TABLE_TYPE::FOOTPRINT, LIBRARY_TABLE_SCOPE::BOTH, true ) )
    {
        for( const std::string& name : aNames )
        {
            const wxString n = wxString::FromUTF8( name.c_str() );

            if( row->URI().Contains( wxS( "${" ) + n + wxS( "}" ) )
                || row->URI().Contains( wxS( "$(" ) + n + wxS( ")" ) ) )
            {
                out.emplace_back( row->Nickname().utf8_str() );
                break;
            }
        }
    }

    return out;
}

} // namespace pcbjam_libs

#endif // __EMSCRIPTEN__
