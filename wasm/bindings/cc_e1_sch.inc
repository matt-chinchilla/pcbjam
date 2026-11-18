// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e1, the schematic face: textually included by eeschema_embind.cpp after
// cc_rules_sch.inc, outside any KICAD_MERGED_EMBIND guard (include subdir eeschema; the
// merged image compiles that TU with -DKICAD_MERGED_EMBIND). It sees the TU's includes and
// helpers (schFrame(), the cc_output namespace) and calls them without editing them.
//
// Every definition of the lane lives in namespace cc_e1 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

#include <sch_table.h>
#include <sch_tablecell.h>
#include <tool/edit_table_tool_base.h>

namespace cc_e1
{
// ---- m12-e: the schematic half of kicadTableCsv -------------------------------------------------

// {"uuid"} of a schematic table or one of its cells, on any sheet -> {"name","text"}: the text
// KiCad's Export Table to CSV writes (CcTableCsvText, kicad include/tool/edit_table_tool_base.h,
// every cell's GetShownText( nullptr, <the table's sheet>, false, 0 ) as the action reads it on
// the sheet it shows), the name the project's with "-table" (the page saves <name>.csv). "" for
// any other argument, no schematic, an open in flight, or a uuid that is not a table. A read:
// nothing changes.
std::string tableCsvSch( std::string aArgs )
{
    SCH_EDIT_FRAME* frame = schFrame();

    if( !frame || pcbjam_open::busy() )
        return "";

    const json args = json::parse( aArgs, nullptr, false );

    if( args.is_discarded() || !args.is_object() || args.size() != 1 || !args.contains( "uuid" )
        || !args["uuid"].is_string() )
    {
        return "";
    }

    const KIID     id( wxString::FromUTF8( args["uuid"].get<std::string>() ) );
    SCH_SHEET_PATH path;
    SCH_ITEM*      item = frame->Schematic().ResolveItem( id, &path, /*allowNull*/ true );
    SCH_TABLE*     table = nullptr;

    if( item && item->Type() == SCH_TABLE_T )
        table = static_cast<SCH_TABLE*>( item );
    else if( item && item->Type() == SCH_TABLECELL_T )
        table = dynamic_cast<SCH_TABLE*>( item->GetParent() );

    if( !table )
        return "";

    const wxString text = CcTableCsvText( table,
            [&path]( SCH_TABLECELL* aCell )
            {
                return aCell->GetShownText( nullptr, &path, false, 0 );
            } );

    std::string name = frame->Prj().GetProjectName().ToStdString( wxConvUTF8 );
    name = ( name.empty() ? std::string( "table" ) : name + "-table" );

    json record;
    record["name"] = name;
    record["text"] = text.ToStdString( wxConvUTF8 );

    return record.dump( -1, ' ', false, json::error_handler_t::replace );
}
} // namespace cc_e1

EMSCRIPTEN_BINDINGS( cc_e1_sch )
{
}
