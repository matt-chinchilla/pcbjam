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
#include <project/sch_project_settings.h>
#include <tools/sch_selection_tool.h>
#include <widgets/panel_sch_selection_filter.h>

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

// ---- m6-e3: the schematic half of kicadSelectionFilterGet, kicadSelectionFilterSet -----------------
// Merged image only (KICAD_MERGED_EMBIND): the dispatcher that calls these halves exists only
// there, and the pane's class is looked up by dynamic_cast.
#ifdef KICAD_MERGED_EMBIND
namespace sch
{
struct FILTER_KEY
{
    const char* name;
    bool SCH_SELECTION_FILTER_OPTIONS::*member;
};

// SCH_SELECTION_FILTER_OPTIONS (kicad include/project/sch_project_settings.h 26), in order.
static const FILTER_KEY kFilterKeys[] = {
    { "lockedItems", &SCH_SELECTION_FILTER_OPTIONS::lockedItems },
    { "symbols", &SCH_SELECTION_FILTER_OPTIONS::symbols },
    { "text", &SCH_SELECTION_FILTER_OPTIONS::text },
    { "wires", &SCH_SELECTION_FILTER_OPTIONS::wires },
    { "labels", &SCH_SELECTION_FILTER_OPTIONS::labels },
    { "pins", &SCH_SELECTION_FILTER_OPTIONS::pins },
    { "graphics", &SCH_SELECTION_FILTER_OPTIONS::graphics },
    { "images", &SCH_SELECTION_FILTER_OPTIONS::images },
    { "ruleAreas", &SCH_SELECTION_FILTER_OPTIONS::ruleAreas },
    { "otherItems", &SCH_SELECTION_FILTER_OPTIONS::otherItems },
};

// KiCad's Selection Filter pane (AUI chrome, hidden on the page), found among the frame's
// windows; null when the frame has none.
static PANEL_SCH_SELECTION_FILTER* filterPanel( wxWindow* aWindow )
{
    for( wxWindow* child : aWindow->GetChildren() )
    {
        if( PANEL_SCH_SELECTION_FILTER* panel = dynamic_cast<PANEL_SCH_SELECTION_FILTER*>( child ) )
            return panel;

        if( PANEL_SCH_SELECTION_FILTER* panel = filterPanel( child ) )
            return panel;
    }

    return nullptr;
}

static SCH_SELECTION_TOOL* selectionTool( SCH_EDIT_FRAME* aFrame )
{
    return aFrame && aFrame->GetToolManager()
                   ? aFrame->GetToolManager()->GetTool<SCH_SELECTION_TOOL>()
                   : nullptr;
}
} // namespace sch

// The schematic's selection filter: {"lockedItems":bool,...} with the ten keys of
// SCH_SELECTION_FILTER_OPTIONS; "" with no schematic editor. A read.
std::string selectionFilterGetSch()
{
    SCH_SELECTION_TOOL* tool = sch::selectionTool( schFrame() );

    if( !tool )
        return "";

    const SCH_SELECTION_FILTER_OPTIONS& filter = tool->GetFilter();
    json                                answer = json::object();

    for( const sch::FILTER_KEY& key : sch::kFilterKeys )
        answer[key.name] = filter.*key.member;

    return answer.dump();
}

// Any of the ten keys with a boolean, the others kept; read whole first, an unknown key, a
// value not a boolean, an empty object, any other shape or an open in flight refuses (false,
// nothing changes). Writes SCH_SELECTION_TOOL::GetFilter() as KiCad's pane does and sets the
// pane's switches from it (SetCheckboxesFromFilter), so the two never disagree. Project local
// settings: no undo step, no modified flag.
bool selectionFilterSetSch( std::string aArgs )
{
    SCH_EDIT_FRAME*     frame = schFrame();
    SCH_SELECTION_TOOL* tool = sch::selectionTool( frame );

    if( !tool || pcbjam_open::busy() )
        return false;

    const json args = json::parse( aArgs, nullptr, false );

    if( args.is_discarded() || !args.is_object() || args.empty() )
        return false;

    SCH_SELECTION_FILTER_OPTIONS filter = tool->GetFilter();

    for( const auto& [name, value] : args.items() )
    {
        const sch::FILTER_KEY* found = nullptr;

        for( const sch::FILTER_KEY& key : sch::kFilterKeys )
        {
            if( name == key.name )
                found = &key;
        }

        if( !found || !value.is_boolean() )
            return false;

        filter.*found->member = value.get<bool>();
    }

    tool->GetFilter() = filter;

    if( PANEL_SCH_SELECTION_FILTER* panel = sch::filterPanel( frame ) )
        panel->SetCheckboxesFromFilter( tool->GetFilter() );

    return true;
}
#endif // KICAD_MERGED_EMBIND
} // namespace cc_e1

EMSCRIPTEN_BINDINGS( cc_e1_sch )
{
}
