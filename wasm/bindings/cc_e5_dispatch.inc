// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Engine run 15, lane e5, the dispatcher face: textually included by kicad_editor_embind.cpp just
// before EMSCRIPTEN_BINDINGS( kicad_editor ). That TU is header light (include subdir
// common): no pcbnew or eeschema header here. An export of both frames declares its
// halves as externs in namespace cc_eN and picks one on pcbEditorActive(); the halves
// are defined (external linkage, never static) in this lane's own pcb, sch or fpedit inc.
//
// Every definition of the lane lives in namespace cc_e5 (never an anonymous namespace: one
// translation unit holds every lane's face) and every name registers in the block below,
// never in a shared block. The rules: BUILD-NOTES-run15.md.

namespace cc_e5
{
// The halves, defined in this lane's board face (cc_e5_pcb.inc) and footprint editor face
// (cc_e5_fpedit.inc): external linkage, merged image only.
std::string footprintConvertPcb( std::string aArgsJson );
std::string footprintConvertFp( std::string aArgsJson );

/** kicadFootprintConvert (fp-e7): the footprint editor's half first, then the board's. */
std::string footprintConvert( std::string aArgsJson )
{
    if( fpEditorActive() )
        return footprintConvertFp( aArgsJson );

    if( pcbEditorActive() )
        return footprintConvertPcb( aArgsJson );

    return "";
}

// m13-e: the document's embedded files, both frames (dispatched as kicadSettingsGet is): the
// halves are defined in cc_e5_pcb.inc and cc_e5_sch.inc. The footprint and symbol editors
// read false on both probes and answer "" or false (their own exports serve them).
std::string embeddedFilesGetPcb();
std::string embeddedFilesGetSch();
bool        embeddedFilesSetPcb( std::string aArgsJson );
bool        embeddedFilesSetSch( std::string aArgsJson );
bool        embeddedFileExtractPcb( std::string aArgsJson );
bool        embeddedFileExtractSch( std::string aArgsJson );

/** kicadEmbeddedFilesGet (m13-e): the open document's embedded files. */
std::string embeddedFilesGet()
{
    if( pcbEditorActive() )
        return embeddedFilesGetPcb();

    if( schEditorActive() )
        return embeddedFilesGetSch();

    return "";
}

/** kicadEmbeddedFilesSet (m13-e): add, remove or the fonts switch on the open document. */
bool embeddedFilesSet( std::string aArgsJson )
{
    if( pcbEditorActive() )
        return embeddedFilesSetPcb( aArgsJson );

    if( schEditorActive() )
        return embeddedFilesSetSch( aArgsJson );

    return false;
}

/** kicadEmbeddedFileExtract (m13-e): one file's bytes into an extract run folder. */
bool embeddedFileExtract( std::string aArgsJson )
{
    if( pcbEditorActive() )
        return embeddedFileExtractPcb( aArgsJson );

    if( schEditorActive() )
        return embeddedFileExtractSch( aArgsJson );

    return false;
}
// st-placefile-e: Import > A sheet or Another board (sch or pcb), the halves in the board and
// schematic faces; the footprint and symbol editors read false on both probes.
bool placeFromFilePcb( std::string aArgsJson );
bool placeFromFileSch( std::string aArgsJson );

/** kicadPlaceFromFile (st-placefile-e): a staged sheet or board onto the pointer. */
bool placeFromFile( std::string aArgsJson )
{
    if( pcbEditorActive() )
        return placeFromFilePcb( aArgsJson );

    if( schEditorActive() )
        return placeFromFileSch( aArgsJson );

    return false;
}
} // namespace cc_e5

EMSCRIPTEN_BINDINGS( cc_e5_dispatch )
{
    function( "kicadFootprintConvert", &cc_e5::footprintConvert );
    function( "kicadEmbeddedFilesGet", &cc_e5::embeddedFilesGet );
    function( "kicadEmbeddedFilesSet", &cc_e5::embeddedFilesSet );
    function( "kicadEmbeddedFileExtract", &cc_e5::embeddedFileExtract );
    function( "kicadPlaceFromFile", &cc_e5::placeFromFile );
}
