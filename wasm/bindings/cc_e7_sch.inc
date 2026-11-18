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

#include <cc_sch_design_block_write.h>

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

} // namespace cc_e7

EMSCRIPTEN_BINDINGS( cc_e7_sch )
{
}
