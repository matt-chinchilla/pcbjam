// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e1, the board face: textually included by pcbnew_embind.cpp after cc_rules_pcb.inc,
// outside any KICAD_MERGED_EMBIND guard (include subdir pcbnew; the merged image compiles
// that TU with -DKICAD_MERGED_EMBIND). It sees the TU's includes and helpers (pcbFrame(),
// the cc_output namespace, cc_fp_items.h's declarations in the merged image only) and calls them without editing them.
//
// Every definition of the lane lives in namespace cc_e1 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

#include <pcb_table.h>
#include <pcb_tablecell.h>
#include <tool/edit_table_tool_base.h>

namespace cc_e1
{
// ---- m12-e: the board half of kicadTableCsv -----------------------------------------------------

// {"uuid"} of a board table or one of its cells -> {"name","text"}: the text KiCad's Export
// Table to CSV writes (CcTableCsvText, kicad include/tool/edit_table_tool_base.h, every cell's
// GetShownText( false, 0 ) as the action reads it), the name the project's with "-table"
// (the page saves <name>.csv). "" for any other argument, no board, an open in flight, or a
// uuid that is not a table of this board. A read: nothing changes.
std::string tableCsvPcb( std::string aArgs )
{
    PCB_EDIT_FRAME* frame = pcbFrame();

    if( !frame || !frame->GetBoard() || pcbjam_open::busy() )
        return "";

    const json args = json::parse( aArgs, nullptr, false );

    if( args.is_discarded() || !args.is_object() || args.size() != 1 || !args.contains( "uuid" )
        || !args["uuid"].is_string() )
    {
        return "";
    }

    const KIID  id( wxString::FromUTF8( args["uuid"].get<std::string>() ) );
    BOARD_ITEM* item = frame->GetBoard()->ResolveItem( id, /*allowNullptr*/ true );
    PCB_TABLE*  table = nullptr;

    if( item && item->Type() == PCB_TABLE_T )
        table = static_cast<PCB_TABLE*>( item );
    else if( item && item->Type() == PCB_TABLECELL_T )
        table = dynamic_cast<PCB_TABLE*>( item->GetParent() );

    if( !table )
        return "";

    const wxString text = CcTableCsvText( table,
            []( PCB_TABLECELL* aCell )
            {
                return aCell->GetShownText( false, 0 );
            } );

    std::string name = frame->Prj().GetProjectName().ToStdString( wxConvUTF8 );
    name = ( name.empty() ? std::string( "table" ) : name + "-table" );

    json record;
    record["name"] = name;
    record["text"] = text.ToStdString( wxConvUTF8 );

    return record.dump( -1, ' ', false, json::error_handler_t::replace );
}
} // namespace cc_e1

EMSCRIPTEN_BINDINGS( cc_e1_pcb )
{
}
