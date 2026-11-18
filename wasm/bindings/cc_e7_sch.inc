// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e7, the schematic face: textually included by eeschema_embind.cpp after
// cc_rules_sch.inc, outside any KICAD_MERGED_EMBIND guard (include subdir eeschema; the
// merged image compiles that TU with -DKICAD_MERGED_EMBIND). It sees the TU's includes and
// helpers (schFrame(), the cc_output namespace) and calls them without editing them.
//
// Every definition of the lane lives in namespace cc_e7 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

#include <cc_host_design_block.h>
#include <cc_paste_special.h>
#include <cc_sch_design_block_write.h>
#include <design_block_io.h>
#include <tools/sch_actions.h>

namespace cc_e7
{

namespace sch
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
// keepAnnotations and asGroup (booleans). Then the block is read by KiCad's own reader
// (DESIGN_BLOCK_IO::DesignBlockLoad); nullptr for any other shape or a block it cannot read.
static std::unique_ptr<DESIGN_BLOCK> readBlockPlaceArgs( const std::string& aArgsJson,
                                                         CC_HOST_DESIGN_BLOCK_OPTIONS& aOptions )
{
    json args = json::parse( aArgsJson, nullptr, /*allow_exceptions*/ false );

    if( args.is_discarded() || !args.is_object() || args.size() != 5 )
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

    aOptions.asSheet = asSheet->get<bool>();
    aOptions.keepAnnotations = keep->get<bool>();
    aOptions.asGroup = asGroup->get<bool>();

    std::unique_ptr<DESIGN_BLOCK_IO> io(
            DESIGN_BLOCK_IO_MGR::FindPlugin( DESIGN_BLOCK_IO_MGR::KICAD_SEXP ) );

    if( !io )
        return nullptr;

    try
    {
        return std::unique_ptr<DESIGN_BLOCK>(
                io->DesignBlockLoad( wxString::FromUTF8( d.c_str() ), wname ) );
    }
    catch( const IO_ERROR& )
    {
        return nullptr;
    }
}

} // namespace sch

// e-block-save, the schematic half of kicadDesignBlockSave. Scopes: sheet (the sheet at
// sheetPath, a PathAsString of the hierarchy, as Save Sheet as Design Block) and selection (as
// Save Selection as Design Block). On the apply coroutine K-B3's body writes
// <dir>/<name>.kicad_block/<name>.kicad_sch, then the record. Nothing in the design changes:
// no commit, no undo step, nothing marked modified, no dialog.
bool blockSaveSch( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return false;

    sch::BlockSaveArgs a;

    if( !sch::readBlockSaveArgs( aArgsJson, { "sheet", "selection" }, a ) )
        return false;

    cc_output::EnsureDir( a.dir );

    pcbjam_collab::runOnCoroutine( fr, [a]() {
        std::vector<wxString> lines;
        SCH_EDIT_FRAME*       f = schFrame();
        const wxString        folder =
                wxString::FromUTF8( ( a.dir + "/" + a.name + ".kicad_block" ).c_str() );
        const wxString        name = wxString::FromUTF8( a.name.c_str() );
        bool                  ok = false;

        if( !f )
        {
            lines.push_back( wxS( "The schematic editor is gone." ) );
        }
        else if( a.scope == "sheet" )
        {
            const wxString wanted = wxString::FromUTF8( a.sheetPath.c_str() );
            SCH_SHEET_LIST list = f->Schematic().Hierarchy();
            auto           it = std::find_if( list.begin(), list.end(),
                                              [&]( const SCH_SHEET_PATH& p )
                                              {
                                                  return p.PathAsString() == wanted;
                                              } );

            if( it == list.end() )
                lines.push_back( wxS( "No sheet has that path." ) );
            else
                ok = CcWriteSheetDesignBlock( f, *it, folder, name, lines );
        }
        else
        {
            ok = CcWriteSchSelectionDesignBlock( f, folder, name, lines );
        }

        sch::writeBlockSaveRecord( a.dir, ok, lines );
    } );

    return true;
}

// e-block-place (DB1, K-B1), the schematic half of kicadDesignBlockPlace: the block read from
// dir by KiCad's reader must hold a schematic; it is handed with the host's options (as a sheet,
// keep designators) to Place Design Block (SCH_DRAWING_TOOLS takes it before the pane: the
// schematic hunk of K-B1, lane e8's e-block-place-sch) and the tool started on the apply
// coroutine: the block hangs off the pointer and the reader's press commits ONE undo step.
bool blockPlaceSch( std::string aArgsJson )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return false;

    CC_HOST_DESIGN_BLOCK_OPTIONS  options;
    std::unique_ptr<DESIGN_BLOCK> block = sch::readBlockPlaceArgs( aArgsJson, options );

    if( !block || block->GetSchematicFile().IsEmpty()
            || !wxFileName::FileExists( block->GetSchematicFile() ) )
    {
        return false;
    }

    auto held = std::make_shared<std::unique_ptr<DESIGN_BLOCK>>( std::move( block ) );

    pcbjam_collab::runOnCoroutine( fr, [held, options]() {
        SCH_EDIT_FRAME* f = schFrame();

        if( !f || !*held )
            return;

        ccSetHostDesignBlock( std::move( *held ), options );
        f->GetToolManager()->RunAction( SCH_ACTIONS::placeDesignBlock );

        // The tool took the block at its start; a tool that did not start leaves none behind.
        ccSetHostDesignBlock( nullptr );
    } );

    return true;
}


// st-paste-e (K-PASTE), the schematic half of kicadPasteSpecial: the choices armed for ONE Paste
// Special (cc_paste_special.h) and KiCad's paste started on the apply coroutine: the clipboard's
// items hang off the pointer with the chosen annotation and the reader's press commits them
// as one undo step, with no DIALOG_PASTE_SPECIAL. The slot is emptied after the action, so a
// paste that never reached the dialog's place leaves nothing armed for a later one.
bool pasteSch( int aMode, bool aClearNets )
{
    SCH_EDIT_FRAME* fr = schFrame();

    if( !fr || pcbjam_open::busy() )
        return false;

    pcbjam_collab::runOnCoroutine( fr, [aMode, aClearNets]() {
        SCH_EDIT_FRAME* f = schFrame();

        if( !f )
            return;

        ccPasteSpecialSlot() = CC_PASTE_SPECIAL{ aMode, aClearNets };
        f->GetToolManager()->RunAction( ACTIONS::pasteSpecial );
        ccPasteSpecialSlot().reset();
    } );

    return true;
}

} // namespace cc_e7

EMSCRIPTEN_BINDINGS( cc_e7_sch )
{
}
