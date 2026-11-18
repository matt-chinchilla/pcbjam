// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e7, the board face: textually included by pcbnew_embind.cpp after cc_rules_pcb.inc,
// outside any KICAD_MERGED_EMBIND guard (include subdir pcbnew; the merged image compiles
// that TU with -DKICAD_MERGED_EMBIND). It sees the TU's includes and helpers (pcbFrame(),
// the cc_output namespace, cc_fp_items.h's declarations in the merged image only) and calls them without editing them.
//
// Every definition of the lane lives in namespace cc_e7 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

#include <base_screen.h>
#include <cc_paste_special.h>
#include <drawing_sheet/ds_data_model.h>
#include <drawing_sheet/ds_proxy_view_item.h>
#include <filename_resolver.h>

#ifdef KICAD_MERGED_EMBIND
#include <cc_host_block_layout.h>
#include <cc_host_design_block.h>
#include <cc_pcb_design_block_write.h>
#include <design_block_io.h>
#include <tools/pcb_actions.h>
#endif

namespace cc_e7
{

// st-paste-e (K-PASTE), the board half of kicadPasteSpecial: the choices armed for ONE Paste
// Special (cc_paste_special.h) and KiCad's paste started on the apply coroutine: the clipboard's
// items hang off the pointer with the chosen annotation and Clear Nets (a board clip only) and the reader's press commits them
// as one undo step, with no DIALOG_PASTE_SPECIAL. The slot is emptied after the action, so a
// paste that never reached the dialog's place leaves nothing armed for a later one.
bool pastePcb( int aMode, bool aClearNets )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || pcbjam_open::busy() )
        return false;

    pcbjam_collab::runOnCoroutine( fr, [aMode, aClearNets]() {
        PCB_EDIT_FRAME* f = pcbFrame();

        if( !f )
            return;

        ccPasteSpecialSlot() = CC_PASTE_SPECIAL{ aMode, aClearNets };
        f->GetToolManager()->RunAction( ACTIONS::pasteSpecial );
        ccPasteSpecialSlot().reset();
    } );

    return true;
}

// e-sheetreload, the board half of kicadDrawingSheetReload: on the apply coroutine, the body of
// PCB_EDIT_FRAME::LoadDrawingSheet (pcbnew_config.cpp) with its message kept in place of its
// info bar (DS_DATA_MODEL falls back to KiCad's default sheet as it does there), the name kept
// in BASE_SCREEN as PCB_EDIT_FRAME::LoadProjectSettings keeps it (so a later project save writes
// the name the document holds), then the drawing sheet's view item repainted.
bool sheetReloadPcb()
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return false;

    pcbjam_collab::runOnCoroutine( fr, []() {
        PCB_EDIT_FRAME* f = pcbFrame();

        if( !f || !f->GetBoard() )
            return;

        PROJECT_FILE& project = f->Prj().GetProjectFile();

        BASE_SCREEN::m_DrawingSheetFileName = project.m_BoardDrawingSheetFile;

        FILENAME_RESOLVER resolver;
        resolver.SetProject( &f->Prj() );
        resolver.SetProgramBase( &Pgm() );

        wxString filename = resolver.ResolvePath( project.m_BoardDrawingSheetFile,
                                                  f->Prj().GetProjectPath(),
                                                  { f->GetBoard()->GetEmbeddedFiles() } );
        wxString msg;

        DS_DATA_MODEL::GetTheInstance().LoadDrawingSheet( filename, &msg );

        if( DS_PROXY_VIEW_ITEM* ds = f->GetCanvas()->GetDrawingSheet() )
            f->GetCanvas()->GetView()->Update( ds, KIGFX::REPAINT );

        f->GetCanvas()->Refresh();
    } );

    return true;
}

// e-block-layout, the marker kicadBoardUpdateGroups: this engine's kicadUpdateBoardFromSchematic
// takes Update PCB's two group options (transferGroups, applyDesignBlockLayouts), lays out the
// groups an apply added and answers their records under layouts in report.json, so the island
// lists the cap blockLayouts on the board update only when the engine does this.
bool boardUpdateGroups()
{
    return true;
}

#ifdef KICAD_MERGED_EMBIND

namespace pcb
{

// The arguments of kicadDesignBlockSave, read whole: exactly run (an integer >= 0), dir (a run
// folder under /tmp/cc-blocksave/), name (1 to 120 characters, none of / \\ : or NUL, not . or
// ..), scope (one of aScopes) and, for the scope sheet only, sheetPath (a string). False for any
// other shape.
struct BlockSaveArgs
{
    std::string dir;
    std::string name;
    std::string scope;
    std::string sheetPath;
};

static bool readBlockSaveArgs( const std::string& aArgsJson,
                               std::initializer_list<const char*> aScopes, BlockSaveArgs& aOut )
{
    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() )
        return false;

    auto run = args.find( "run" );
    auto dir = args.find( "dir" );
    auto name = args.find( "name" );
    auto scope = args.find( "scope" );

    if( run == args.end() || !run->is_number_integer() || run->get<long long>() < 0
            || dir == args.end() || !dir->is_string() || name == args.end()
            || !name->is_string() || scope == args.end() || !scope->is_string() )
    {
        return false;
    }

    aOut.dir = dir->get<std::string>();
    aOut.name = name->get<std::string>();
    aOut.scope = scope->get<std::string>();

    if( !cc_output::IsRunDir( aOut.dir, "/tmp/cc-blocksave/" ) )
        return false;

    const wxString wname = wxString::FromUTF8( aOut.name.c_str() );

    if( aOut.name.empty() || wname.empty() || wname.length() > 120 || aOut.name == "."
            || aOut.name == ".." || aOut.name.find_first_of( std::string( "/\\:\0", 4 ) )
                                            != std::string::npos )
    {
        return false;
    }

    bool known = false;

    for( const char* s : aScopes )
        known = known || aOut.scope == s;

    if( !known )
        return false;

    const bool wantsPath = aOut.scope == "sheet";
    auto       sheetPath = args.find( "sheetPath" );

    if( args.size() != ( wantsPath ? 5u : 4u ) )
        return false;

    if( wantsPath )
    {
        if( sheetPath == args.end() || !sheetPath->is_string() )
            return false;

        aOut.sheetPath = sheetPath->get<std::string>();
    }

    return true;
}

// <dir>/.cc-block.json, last: {"code": 0} when the block's file was written, {"code": 1} with
// KiCad's own message lines when it was refused or failed.
static void writeBlockSaveRecord( const std::string& aDir, bool aOk,
                                  const std::vector<wxString>& aLines )
{
    json lines = json::array();

    for( const wxString& line : aLines )
        lines.push_back( std::string( line.utf8_str() ) );

    json record = json::object();
    record["code"] = aOk ? 0 : 1;
    record["lines"] = lines;
    cc_output::WriteRecord( aDir, ".cc-block.json", record );
}


// The arguments of kicadDesignBlockPlace, read whole: exactly dir (a run folder under
// /tmp/cc-block/ holding <name>.kicad_block/), name (as kicadDesignBlockSave's), asSheet,
// keepAnnotations and asGroup (booleans), and optionally nickname (the block's library row, 1 to
// 120 characters LIB_ID takes as a nickname). Then the block is read by KiCad's own reader
// (DESIGN_BLOCK_IO::DesignBlockLoad) and, with a nickname, its LIB_ID given that nickname as
// DESIGN_BLOCK_LIBRARY_ADAPTER::LoadDesignBlock gives it on the Design Blocks pane's path, so a
// group placed as a group carries the design block link; nullptr for any other shape or a block
// it cannot read.
static std::unique_ptr<DESIGN_BLOCK> readBlockPlaceArgs( const std::string& aArgsJson,
                                                         CC_HOST_DESIGN_BLOCK_OPTIONS& aOptions )
{
    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || ( args.size() != 5 && args.size() != 6 ) )
        return nullptr;

    auto dir = args.find( "dir" );
    auto name = args.find( "name" );
    auto asSheet = args.find( "asSheet" );
    auto keep = args.find( "keepAnnotations" );
    auto asGroup = args.find( "asGroup" );

    if( dir == args.end() || !dir->is_string() || name == args.end() || !name->is_string()
            || asSheet == args.end() || !asSheet->is_boolean() || keep == args.end()
            || !keep->is_boolean() || asGroup == args.end() || !asGroup->is_boolean() )
    {
        return nullptr;
    }

    const std::string d = dir->get<std::string>();
    const std::string n = name->get<std::string>();
    const wxString    wname = wxString::FromUTF8( n.c_str() );

    if( !cc_output::IsRunDir( d, "/tmp/cc-block/" ) || n.empty() || wname.empty()
            || wname.length() > 120 || n == "." || n == ".."
            || n.find_first_of( std::string( "/\\:\0", 4 ) ) != std::string::npos )
    {
        return nullptr;
    }

    UTF8 nickname;

    if( args.size() == 6 )
    {
        auto nick = args.find( "nickname" );

        if( nick == args.end() || !nick->is_string() )
            return nullptr;

        const std::string nk = nick->get<std::string>();
        const wxString    wnick = wxString::FromUTF8( nk.c_str() );
        LIB_ID            probe;

        if( nk.empty() || wnick.empty() || wnick.length() > 120
                || nk.find( '\0' ) != std::string::npos
                || probe.SetLibNickname( UTF8( wnick ) ) != -1 )
        {
            return nullptr;
        }

        nickname = UTF8( wnick );
    }

    aOptions.asSheet = asSheet->get<bool>();
    aOptions.keepAnnotations = keep->get<bool>();
    aOptions.asGroup = asGroup->get<bool>();

    std::unique_ptr<DESIGN_BLOCK_IO> io(
            DESIGN_BLOCK_IO_MGR::FindPlugin( DESIGN_BLOCK_IO_MGR::KICAD_SEXP ) );

    if( !io )
        return nullptr;

    try
    {
        std::unique_ptr<DESIGN_BLOCK> block(
                io->DesignBlockLoad( wxString::FromUTF8( d.c_str() ), wname ) );

        if( block && !nickname.empty() && block->GetLibId().SetLibNickname( nickname ) != -1 )
            return nullptr;

        return block;
    }
    catch( const IO_ERROR& )
    {
        return nullptr;
    }
}

} // namespace pcb

// e-block-save, the board half of kicadDesignBlockSave (merged image only: its kicad half lives
// in pcbnew's kiface, cc_pcb_design_block_write.h). Scopes: board (the whole board, as Save Board
// as Design Block), selection (as Save Selection as Design Block), linkedGroup (the one selected
// group linked to a block, as Save to Linked Design Block). On the apply coroutine K-B3's body
// writes <dir>/<name>.kicad_block/<name>.kicad_pcb, then the record. Nothing in the design
// changes: no commit, no undo step, nothing marked modified, no dialog.
bool blockSavePcb( std::string aArgsJson )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return false;

    pcb::BlockSaveArgs a;

    if( !pcb::readBlockSaveArgs( aArgsJson, { "board", "selection", "linkedGroup" }, a ) )
        return false;

    cc_output::EnsureDir( a.dir );

    pcbjam_collab::runOnCoroutine( fr, [a]() {
        std::vector<wxString> lines;
        PCB_EDIT_FRAME*       f = pcbFrame();
        const wxString        folder =
                wxString::FromUTF8( ( a.dir + "/" + a.name + ".kicad_block" ).c_str() );
        const wxString        name = wxString::FromUTF8( a.name.c_str() );
        bool                  ok = false;

        if( !f )
            lines.push_back( wxS( "The board editor is gone." ) );
        else if( a.scope == "board" )
            ok = CcWriteBoardDesignBlock( f, folder, name, lines );
        else
            ok = CcWritePcbSelectionDesignBlock( f, a.scope == "linkedGroup", folder, name, lines );

        pcb::writeBlockSaveRecord( a.dir, ok, lines );
    } );

    return true;
}

// e-block-place (DB1, K-B1), the board half of kicadDesignBlockPlace (merged image only): the
// block read from dir by KiCad's reader must hold a layout; it is handed to Place Design Block
// (PCB_CONTROL::AppendDesignBlock takes it before the pane, with the host's asGroup) and the
// tool started on the apply coroutine: the layout hangs off the pointer and the reader's press
// commits it as ONE undo step (Esc cancels with none). asSheet and keepAnnotations are the
// schematic's options (KiCad's board placement reads neither).
bool blockPlacePcb( std::string aArgsJson )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return false;

    CC_HOST_DESIGN_BLOCK_OPTIONS  options;
    std::unique_ptr<DESIGN_BLOCK> block = pcb::readBlockPlaceArgs( aArgsJson, options );

    if( !block || block->GetBoardFile().IsEmpty()
            || !wxFileName::FileExists( block->GetBoardFile() ) )
    {
        return false;
    }

    auto held = std::make_shared<std::unique_ptr<DESIGN_BLOCK>>( std::move( block ) );

    pcbjam_collab::runOnCoroutine( fr, [held, options]() {
        PCB_EDIT_FRAME* f = pcbFrame();

        if( !f || !*held )
            return;

        ccSetHostDesignBlock( std::move( *held ), options );
        f->GetToolManager()->RunAction( PCB_ACTIONS::placeDesignBlock );

        // The tool took the block at its start; a tool that did not start leaves none behind.
        ccSetHostDesignBlock( nullptr );
    } );

    return true;
}

// e-block-layout (DB3, K-B2). kicadDesignBlockApplyLayout( {"run", "dir", "uuids"} ), the board
// frame only (merged image only): exactly run (an integer >= 0), dir (a run folder under
// /tmp/cc-blocklayout/) and uuids (1 to 256 lower case uuids, each a group of the board, no
// repeats); false for anything else, no board or an open in flight. On the apply coroutine those
// groups are selected (as KiCad's Update PCB selects the groups it lays out) and KiCad's Apply
// Design Block Layout runs with the host reporter set: ONE commit over every group it lays out,
// no message box, no progress window, no info bar. Then the selection is cleared and
// <dir>/.cc-block-layout.json is written last: {"applied": n, "failures": [{"uuid", "reason"}],
// "lines": [KiCad's info bar lines]}. A selected group with no design block link counts in
// neither (the tool skips it; the host offers the press only on a linked group).
bool blockApplyLayout( std::string aArgsJson )
{
    PCB_EDIT_FRAME* fr = pcbFrame();

    if( !fr || !fr->GetBoard() || pcbjam_open::busy() )
        return false;

    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 3 )
        return false;

    auto run = args.find( "run" );
    auto dir = args.find( "dir" );
    auto uuids = args.find( "uuids" );

    if( run == args.end() || !run->is_number_integer() || run->get<long long>() < 0
            || dir == args.end() || !dir->is_string() || uuids == args.end()
            || !uuids->is_array() || uuids->empty() || uuids->size() > 256 )
    {
        return false;
    }

    const std::string d = dir->get<std::string>();

    if( !cc_output::IsRunDir( d, "/tmp/cc-blocklayout/" ) )
        return false;

    std::vector<KIID>     wanted;
    std::set<std::string> seen;

    for( const json& u : *uuids )
    {
        if( !u.is_string() || !ccIsLowerUuid( u.get<std::string>() )
                || !seen.insert( u.get<std::string>() ).second )
        {
            return false;
        }

        const KIID id( u.get<std::string>() );
        bool       found = false;

        for( PCB_GROUP* g : fr->GetBoard()->Groups() )
            found = found || g->m_Uuid == id;

        if( !found )
            return false;

        wanted.push_back( id );
    }

    cc_output::EnsureDir( d );

    pcbjam_collab::runOnCoroutine( fr, [d, wanted]() {
        CC_HOST_BLOCK_LAYOUT_REPORT report;
        PCB_EDIT_FRAME*             f = pcbFrame();

        if( f && f->GetBoard() )
        {
            TOOL_MANAGER*       toolMgr = f->GetToolManager();
            PCB_SELECTION_TOOL* selTool = toolMgr->GetTool<PCB_SELECTION_TOOL>();

            toolMgr->RunAction( ACTIONS::selectionClear );

            for( PCB_GROUP* g : f->GetBoard()->Groups() )
            {
                if( std::find( wanted.begin(), wanted.end(), g->m_Uuid ) != wanted.end() )
                    selTool->select( g );
            }

            {
                CC_HOST_BLOCK_LAYOUT_SCOPE scope( &report );
                toolMgr->RunAction( PCB_ACTIONS::applyDesignBlockLayout );
            }

            toolMgr->RunAction( ACTIONS::selectionClear );
        }
        else
        {
            report.lines.push_back( wxS( "The board editor is gone." ) );
        }

        json failures = json::array();

        for( const CC_HOST_BLOCK_LAYOUT_FAILURE& failure : report.failures )
        {
            failures.push_back( { { "uuid", std::string( failure.uuid.AsString().utf8_str() ) },
                                  { "reason", std::string( failure.reason.utf8_str() ) } } );
        }

        json lines = json::array();

        for( const wxString& line : report.lines )
            lines.push_back( std::string( line.utf8_str() ) );

        json record = json::object();
        record["applied"] = report.applied;
        record["failures"] = failures;
        record["lines"] = lines;
        cc_output::WriteRecord( d, ".cc-block-layout.json", record );
    } );

    return true;
}

#endif // KICAD_MERGED_EMBIND

} // namespace cc_e7

EMSCRIPTEN_BINDINGS( cc_e7_pcb )
{
    emscripten::function( "kicadBoardUpdateGroups", &cc_e7::boardUpdateGroups );
#ifdef KICAD_MERGED_EMBIND
    emscripten::function( "kicadDesignBlockApplyLayout", &cc_e7::blockApplyLayout );
#endif
}
