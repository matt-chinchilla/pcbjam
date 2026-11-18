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
#include <project/board_project_settings.h>
#include <tools/pcb_selection_tool.h>
#include <widgets/panel_selection_filter.h>

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

// ---- m6-e3: the board half of kicadSelectionFilterGet, kicadSelectionFilterSet -----------------
// Merged image only (KICAD_MERGED_EMBIND): the dispatcher that calls these halves exists only
// there, and the pane's class is looked up by dynamic_cast.
#ifdef KICAD_MERGED_EMBIND
namespace pcb
{
struct FILTER_KEY
{
    const char* name;
    bool PCB_SELECTION_FILTER_OPTIONS::*member;
};

// PCB_SELECTION_FILTER_OPTIONS (kicad include/project/board_project_settings.h 39), in order.
static const FILTER_KEY kFilterKeys[] = {
    { "lockedItems", &PCB_SELECTION_FILTER_OPTIONS::lockedItems },
    { "footprints", &PCB_SELECTION_FILTER_OPTIONS::footprints },
    { "text", &PCB_SELECTION_FILTER_OPTIONS::text },
    { "tracks", &PCB_SELECTION_FILTER_OPTIONS::tracks },
    { "vias", &PCB_SELECTION_FILTER_OPTIONS::vias },
    { "pads", &PCB_SELECTION_FILTER_OPTIONS::pads },
    { "graphics", &PCB_SELECTION_FILTER_OPTIONS::graphics },
    { "zones", &PCB_SELECTION_FILTER_OPTIONS::zones },
    { "keepouts", &PCB_SELECTION_FILTER_OPTIONS::keepouts },
    { "dimensions", &PCB_SELECTION_FILTER_OPTIONS::dimensions },
    { "points", &PCB_SELECTION_FILTER_OPTIONS::points },
    { "otherItems", &PCB_SELECTION_FILTER_OPTIONS::otherItems },
};

// KiCad's Selection Filter pane (AUI chrome, hidden on the page), found among the frame's
// windows; null when the frame has none.
static PANEL_SELECTION_FILTER* filterPanel( wxWindow* aWindow )
{
    for( wxWindow* child : aWindow->GetChildren() )
    {
        if( PANEL_SELECTION_FILTER* panel = dynamic_cast<PANEL_SELECTION_FILTER*>( child ) )
            return panel;

        if( PANEL_SELECTION_FILTER* panel = filterPanel( child ) )
            return panel;
    }

    return nullptr;
}

static PCB_SELECTION_TOOL* selectionTool( PCB_EDIT_FRAME* aFrame )
{
    return aFrame && aFrame->GetToolManager()
                   ? aFrame->GetToolManager()->GetTool<PCB_SELECTION_TOOL>()
                   : nullptr;
}
} // namespace pcb

// The board's selection filter: {"lockedItems":bool,...} with the twelve keys of
// PCB_SELECTION_FILTER_OPTIONS; "" with no board editor. A read.
std::string selectionFilterGetPcb()
{
    PCB_SELECTION_TOOL* tool = pcb::selectionTool( pcbFrame() );

    if( !tool )
        return "";

    const PCB_SELECTION_FILTER_OPTIONS& filter = tool->GetFilter();
    json                                answer = json::object();

    for( const pcb::FILTER_KEY& key : pcb::kFilterKeys )
        answer[key.name] = filter.*key.member;

    return answer.dump();
}

// Any of the twelve keys with a boolean, the others kept; read whole first, an unknown key, a
// value not a boolean, an empty object, any other shape or an open in flight refuses (false,
// nothing changes). Writes PCB_SELECTION_TOOL::GetFilter() as KiCad's pane does and sets the
// pane's switches from it (SetCheckboxesFromFilter), so the two never disagree. Project local
// settings: no undo step, no modified flag.
bool selectionFilterSetPcb( std::string aArgs )
{
    PCB_EDIT_FRAME*     frame = pcbFrame();
    PCB_SELECTION_TOOL* tool = pcb::selectionTool( frame );

    if( !tool || pcbjam_open::busy() )
        return false;

    const json args = json::parse( aArgs, nullptr, false );

    if( args.is_discarded() || !args.is_object() || args.empty() )
        return false;

    PCB_SELECTION_FILTER_OPTIONS filter = tool->GetFilter();

    for( const auto& [name, value] : args.items() )
    {
        const pcb::FILTER_KEY* found = nullptr;

        for( const pcb::FILTER_KEY& key : pcb::kFilterKeys )
        {
            if( name == key.name )
                found = &key;
        }

        if( !found || !value.is_boolean() )
            return false;

        filter.*found->member = value.get<bool>();
    }

    tool->GetFilter() = filter;

    if( PANEL_SELECTION_FILTER* panel = pcb::filterPanel( frame ) )
        panel->SetCheckboxesFromFilter( tool->GetFilter() );

    return true;
}
#endif // KICAD_MERGED_EMBIND
} // namespace cc_e1

EMSCRIPTEN_BINDINGS( cc_e1_pcb )
{
}
