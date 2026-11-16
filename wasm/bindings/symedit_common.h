/*
 * Shared plumbing for the Symbol Editor's bindings (engine run 14, caps symedit,
 * symbolEdit and symbolConvert): a symbol read from a kicad_symbol_lib document and
 * written back as one, by KiCad's own reader and writer. Header-only (the
 * collab_common.h pattern), included by symedit_embind.cpp (the symbol editor frame's
 * exports) and eeschema_embind.cpp (the schematic frame's kicadSymbolForEditor and
 * kicadSymbolPushToSchematic), so the build needs no extra object for it.
 *
 * Created by Chirichella Inc. on 2026-11-16.
 */

#pragma once

#ifdef __EMSCRIPTEN__

#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/string.h>
#include <build_version.h>
#include <io/io_mgr.h>
#include <io/kicad/kicad_io_utils.h>
#include <lib_id.h>
#include <lib_symbol.h>
#include <richio.h>
#include <sch_file_versions.h>
#include <sch_io/sch_io.h>
#include <sch_io/sch_io_mgr.h>
#include <sch_io/kicad_sexpr/sch_io_kicad_sexpr_lib_cache.h>

namespace cc_symedit
{

/// The largest symbol document read or written (spec 5.1: 4 MB).
constexpr size_t DOC_MAX_BYTES = 4u * 1024u * 1024u;

/** A lowercase 8-4-4-4-12 uuid. */
inline bool isLowerUuid( const std::string& aText )
{
    if( aText.size() != 36 )
        return false;

    for( size_t i = 0; i < aText.size(); ++i )
    {
        const char c = aText[i];

        if( i == 8 || i == 13 || i == 18 || i == 23 )
        {
            if( c != '-' )
                return false;
        }
        else if( !( ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'f' ) ) )
        {
            return false;
        }
    }

    return true;
}

inline std::string utf8( const wxString& aText )
{
    return std::string( aText.utf8_str() );
}

/**
 * A symbol and the symbols it derives from, owned: [0] the symbol, then its parent, its
 * parent's parent, ... Each copy's parent link points at the next copy, never at a reader's
 * cache.
 */
struct SYMBOL_CHAIN
{
    std::vector<std::unique_ptr<LIB_SYMBOL>> links;

    LIB_SYMBOL* symbol() const { return links.empty() ? nullptr : links.front().get(); }
};

/** Copies aSymbol and every ancestor it links to, relinked among the copies. */
inline SYMBOL_CHAIN copyChain( const LIB_SYMBOL* aSymbol )
{
    SYMBOL_CHAIN chain;
    std::vector<const LIB_SYMBOL*> source;

    for( const LIB_SYMBOL* s = aSymbol; s && source.size() < 64; )
    {
        source.push_back( s );

        std::shared_ptr<LIB_SYMBOL> parent = s->GetParent().lock();
        s = parent.get();
    }

    for( const LIB_SYMBOL* s : source )
        chain.links.push_back( std::make_unique<LIB_SYMBOL>( *s ) );

    for( size_t i = 0; i < chain.links.size(); ++i )
    {
        if( i + 1 < chain.links.size() )
            chain.links[i]->SetParent( chain.links[i + 1].get() );
        else
            chain.links[i]->SetParent();   // a root: no argument, as SetParent( nullptr ) also matches EDA_ITEM::SetParent
    }

    return chain;
}

/** A scratch file under /tmp/cc-symedit/ (MEMFS), unique per call. */
inline wxString scratchPath( const char* aExt )
{
    static unsigned s_serial = 0;

    const wxString dir = wxS( "/tmp/cc-symedit" );

    if( !wxFileName::DirExists( dir ) )
        wxFileName::Mkdir( dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL );

    return wxString::Format( wxS( "%s/doc-%u.%s" ), dir, ++s_serial, aExt );
}

inline bool writeFile( const wxString& aPath, const std::string& aBytes )
{
    FILE* f = std::fopen( aPath.utf8_str(), "wb" );

    if( !f )
        return false;

    const bool whole = std::fwrite( aBytes.data(), 1, aBytes.size(), f ) == aBytes.size();
    std::fclose( f );
    return whole;
}

/**
 * The symbol named aName of a kicad_symbol_lib document, with its extends chain, read by
 * KiCad's own library reader (SCH_IO_KICAD_SEXPR: the version, every token and the parent
 * links exactly as a library file is read). Empty for a document KiCad cannot read, or with
 * no symbol of that name. The document goes through a scratch file of its own, removed after.
 */
inline SYMBOL_CHAIN readSymbol( const std::string& aText, const wxString& aName )
{
    SYMBOL_CHAIN chain;

    if( aText.empty() || aText.size() > DOC_MAX_BYTES || aName.IsEmpty() )
        return chain;

    const wxString path = scratchPath( "kicad_sym" );

    if( !writeFile( path, aText ) )
        return chain;

    try
    {
        IO_RELEASER<SCH_IO> pi( SCH_IO_MGR::FindPlugin( SCH_IO_MGR::SCH_KICAD ) );

        if( pi )
        {
            if( LIB_SYMBOL* found = pi->LoadSymbol( path, aName ) )
                chain = copyChain( found );
        }
    }
    catch( ... )
    {
        chain.links.clear();
    }

    wxRemoveFile( path );
    return chain;
}

/** KiCad's own library header (SCH_IO_KICAD_SEXPR_LIB_CACHE::formatLibraryHeader's words). */
inline void formatHeader( OUTPUTFORMATTER& aFormatter )
{
    aFormatter.Print( "(kicad_symbol_lib (version %d) (generator \"kicad_symbol_editor\") "
                      "(generator_version \"%s\")",
                      SEXPR_SYMBOL_LIB_FILE_VERSION,
                      GetMajorMinorVersion().c_str().AsChar() );
}

/**
 * aSymbol as a kicad_symbol_lib document by KiCad's own formatter, its ancestors written
 * first (a library is ordered by inheritance depth), the whole prettified as a library file
 * is. Copies are formatted (SaveSymbol touches a symbol's embedded fonts), so this is a pure
 * read. Empty when it cannot be written.
 */
inline std::string formatSymbol( const LIB_SYMBOL* aSymbol )
{
    if( !aSymbol )
        return std::string();

    try
    {
        SYMBOL_CHAIN     chain = copyChain( aSymbol );
        STRING_FORMATTER formatter;

        formatHeader( formatter );

        for( auto it = chain.links.rbegin(); it != chain.links.rend(); ++it )
            SCH_IO_KICAD_SEXPR_LIB_CACHE::SaveSymbol( it->get(), formatter );

        formatter.Print( ")" );

        std::string out = formatter.GetString();
        KICAD_FORMAT::Prettify( out, KICAD_FORMAT::FORMAT_MODE::NORMAL );
        return out;
    }
    catch( ... )
    {
        return std::string();
    }
}

/** The name of aSymbol's parent, "" for a root symbol. */
inline wxString parentName( const LIB_SYMBOL* aSymbol )
{
    if( !aSymbol )
        return wxString();

    std::shared_ptr<LIB_SYMBOL> parent = aSymbol->GetParent().lock();
    return parent ? parent->GetName() : wxString();
}

} // namespace cc_symedit

#endif // __EMSCRIPTEN__
