// Copyright 2009 The Archiveopteryx Developers <info@aox.org>

#include "codec.h"

#include "estring.h"
#include "ustring.h"
#include "estringlist.h"

#include "cp.h"
#include "koi.h"
#include "iso8859.h"
#include "mac.h"
#include "utf.h"
#include "iso2022jp.h"
#include "hproman8.h"
#include "unknown.h"
#include "gb2312.h"
#include "viscii.h"
#include "cp932.h"
#include "cp949.h"
#include "cp950.h"
#include "eucjp.h"
#include "euckr.h"
#include "gbk.h"


/*! \class Codec codec.h
    The Codec class describes a mapping between UString and anything else.

    Unicode is used as the native character set and encoding in
    Warehouse. All other encodings are mapped to or from that: To
    unicode when e.g. parsing a mail message, from when storing data
    in the database (as utf-8).

    A Codec is responsible for one such mapping. The Codec class also
    contains a factory to create an instance of the right subclass based
    on a name.

    The source code for the codecs includes a number of generated files,
    e.g. the list of MIME character set names and map from Unicode to
    ISO-8859-2. We choose to regard them as source files, because we may
    want to sever the link between the source and our version. For
    example, if the source is updated, we may or may not want to follow
    along.
*/

/*! Constructs an empty Codec for character set \a cs, setting its
    state to Valid.

    The construction of a codec sets it to its default state, whatever
    that is for each codec.
*/

Codec::Codec( const char * cs )
    : s( Valid ), n( cs )
{
}


/*! Destroys the Codec. */

Codec::~Codec()
{
}


/*! \fn EString Codec::name() const

    Returns the name of the codec, as supplied to the constructor.
*/



/*! \fn EString Codec::fromUnicode( const UString & u )

    This pure virtual function maps \a u from Unicode to the codec's
    other encoding, and returns a EString containing the result.

    Each reimplementation must decide how to handle codepoints that
    cannot be represented in the target encoding.
*/


/*! \fn UString Codec::toUnicode( const EString & s )

    This pure virtual function maps \a s from codec's encoding to
    Uncode, and returns a UString containing the result.

    Reimplementations are expected to handle errors only by calling
    setState(). Each reimplementation is free to recover as seems
    suitable for its encoding.
*/


/*! \fn void Codec::setState( State st )

    Sets the codec's state to \a st, which is one of Valid,
    BadlyFormed and Invalid.

    Valid is the initial setting, and means that the Codec has seen
    only valid input. BadlyFormed means that the Codec has seen
    something it did not like, but was able to determine the meaning
    of that input. Invalid means that the Codec has seen input whose
    meaning could not be determined.
*/


/*! \fn Codec::State Codec::state() const
    Returns the current state of the codec, reflecting the codec's input
    up to this point.
*/


/*! Returns an error message describing why the codec is in Invalid
    state. If the codec is in Valid or BadlyFormed states, error()
    returns an empty string.
*/

EString Codec::error() const
{
    if ( state() != Invalid )
        return "";
    return e;
}


/*! Records that at octet index \a pos, an error happened
    and no code point could be found. This also sets the state() to
    Invalid.
*/

void Codec::recordError( uint pos )
{
    setState( Invalid );
    if ( e.isEmpty() )
        e = "Parse error at index " + fn( pos ) +
            ": Could not find a valid " + name() + " code point";
}


/*! Records that at octet index \a pos in \a input, an error happened
    and no code point could be found. This also sets the state() to
    Invalid.
*/

void Codec::recordError( uint pos, const EString & input )
{
    setState( Invalid );
    if ( e.isEmpty() )
        e = "Parse error at index " + fn( pos ) +
            ": Could not find a valid " + name() + " code point "
            "(next few bytes: 0x" + fn( input[pos], 16 ) +
            " 0x" + fn( input[pos+1], 16 ) +
            " 0x" + fn( input[pos+2], 16 ) +
            " 0x" + fn( input[pos+3], 16 ) + ")";
}


/*! Records that \a codepoint (at octet index \a pos) is not valid and
    could not be converted to Unicode. This also sets the state() to
    Invalid.
*/

void Codec::recordError( uint pos, uint codepoint )
{
    setState( Invalid );
    if ( e.isEmpty() )
        e = "Parse error at index " + fn( pos ) +
            ": Code point " + fn( codepoint ) +
            " is undefined in " + name();
}


/*! Records that the error \a s occurred. This is meant for errors other
    than invalid or undefined codepoints, and should be needed only by a
    stateful Codec. Also sets the state() to Invalid.
*/

void Codec::recordError( const EString &s )
{
    setState( Invalid );
    if ( e.isEmpty() )
        e = s;
}


static struct {
    const char * alias;
    const char * name;
} codecaliases[] = {
#include "codec-aliases.inc"
    { 0, 0 }
};


/*! Returns a list of all canonical codec names. Aliases are not
    included in the list.
*/

EStringList * Codec::allCodecNames()
{
    EStringList * r = new EStringList;
    int i = 0;
    while ( codecaliases[i].alias ) {
        EString s = codecaliases[i].name;
        s = s.lower();
        if ( s == codecaliases[i].alias )
            r->append( s );
        i++;
    }
    return r;
}


/*! Looks up \a s in our list of MIME character set names and returns
    a Codec suitable for mapping that to/from Unicode.

    If \a s is unknown, byName() returns 0.
*/

Codec * Codec::byName( const EString & s )
{
    if ( s.isEmpty() )
        return 0;

    EString name = s.lower();

    int i = 0;
    // next loop can be replaced by a binary search - codecaliases is
    // sorted by alias.
    while ( codecaliases[i].alias &&
            name != codecaliases[i].alias )
        i++;
    if ( codecaliases[i].alias )
        name = codecaliases[i].name;
    else if ( name == "macroman" )
        name = "macintosh";

    Codec * codec = 0;
#include "codec-map.inc"

    if ( codec )
        return codec;

    // some people use "iso 8859 1", "iso_8859-1", etc.
    i = 0;
    name = "";
    while ( i < (int)s.length() ) {
        if ( s[i] == '_' || s[i] == ' ' )
            name.append( '-' );
        else
            name.append( s[i] );
        i++;
    }
    if ( name != s )
        codec = byName( name );
    if ( codec )
        return codec;

    // if that didn't help, let's also insert a hyphen at all
    // letter/number transitions, and see whether that
    // helps. (also, because the recursive call does the
    // above.)
    i = 0;
    name = "";
    while ( i < (int)s.length() ) {
        if ( s[i] == '_' || s[i] == ' ' )
            name.append( '-' );
        else
            name.append( s[i] );
        if ( ( ( s[i] >= 'a' && s[i] <= 'z' ) ||
               ( s[i] >= 'A' && s[i] <= 'Z' ) ) &&
             ( s[i+1] >= '0' && s[i+1] <= '9' ) )
            name.append( '-' );
        else if ( ( s[i] >= '0' && s[i] <= '9' ) &&
                  ( ( s[i+1] >= 'a' && s[i+1] <= 'z' ) ||
                    ( s[i+1] >= 'A' && s[i+1] <= 'Z' ) ) )
            name.append( '-' );
        i++;
    }
    if ( name != s )
        codec = byName( name );

    if ( codec )
        return codec;

    // at this point, name has lost some of its chaos, so these four
    // easy tests should find all the variations of unknown-8bit.
    name = name.lower();
    if ( name == "unknown" ||
         name.startsWith( "unknown-" ) ||
         name.endsWith( "-unknown" ) ||
         name.contains( "-unknown-" ) )
        codec = new Unknown8BitCodec;

    return codec;
}


#include "charset-support.inc"

/*! Returns a codec suitable for encoding the unicode string \a u in
    such a way that the largest possible number of mail readers will
    understand the message.
*/

Codec * Codec::byString( const UString & u )
{
    uint i = 0;
    uint s = 0xffff;
    while ( i < u.length() && s > 0 ) {
        if ( (uint)u[i] < lastSupportedChar )
            s = s & charsetSupport[u[i]];
        else
            s = 0;
        i++;
    }
    Codec * c = 0;
    if ( s ) {
        i = 0;
        while ( ((s>>i) & 1 ) == 0 )
            i++;
        c = Codec::byName( charsetValues[i].n );
    }
    if ( !c )
        c = new Utf8Codec;
    return c;
}


#include "wordlist.inc"


/*! Returns a codec likely to describe the encoding for \a s. This
    uses words lists and many other strategies.

    Its assumptions:

    If \a s contains a Unicode Byte Order Mark, it probably is a
    UTF-16BE or UTF-16LE string.

    If \a s is a Russian string, it probably contains lots of common
    Russian words, and we have can identify the character encoding by
    scanning for KOI8-R and ISO-8859-5 forms of some common
    words. Ditto for other languages.

    If \a s uses typical Windows punctation and is mostly ASCII, it's
    in a typical Windows encoding.

    This function is a little slower than it could be, since it
    creates a largish number of short EString objects.
*/

Codec * Codec::byString( const EString & s )
{
    if ( s[0] == 0xFF && s[1] == 0xFE && (s.length() % 2) == 0 )
        return new Utf16LeCodec;
    else if ( s[0] == 0xFE && s[1] == 0xFF && (s.length() % 2) == 0 )
        return new Utf16BeCodec;

    uint b = 0;
    uint e = 0;
    uint occurences[NumEncodings];
    uint i = 0;
    while ( i < NumEncodings )
        occurences[i++] = 0;
    while ( b < s.length() ) {
        while ( b < s.length() && s[b] < 'A' )
            b++;
        e = b;
        while ( e < s.length() &&
                ( s[e] >= 128 ||
                  ( s[e] >= 'a' && s[e] <= 'z' ) ||
                  ( s[e] >= 'A' && s[e] <= 'Z' ) ) )
            e++;
        if ( e > b ) {
            uint i = b;
            while ( i < e && s[i] < 128 )
                i++;
            if ( i < e ) {
                EString w( s.mid( b, e-b ).lower() );
                uint top = NumForms-1;
                uint bottom = 0;
                while ( top >= bottom ) {
                    i = (bottom + top)/2;
                    if ( w < forms[i].encodedForm ) {
                        if ( i == 0 )
                            break;
                        top = i-1;
                    }
                    else if ( w == forms[i].encodedForm ) {
                        occurences[forms[i].encoding]++;
                        bottom = NumForms + 1;
                    }
                    else {
                        bottom = i+1;
                    }
                }
            }
        }
        b++;
        if ( e > b )
            b = e;
    }
    i = 0;
    uint max = 0;
    while ( i < NumEncodings ) {
        if ( occurences[i] > occurences[max] )
            max = i;
        i++;
    }
    if ( occurences[max] ) {
        switch( (Encoding)max ) {
        case Iso88592:
            return new Iso88592Codec;
            break;
        case Iso885915:
            return new Iso885915Codec;
            break;
        case MacRoman:
            return new MacRomanCodec;
            break;
        case Cp437:
            return new Cp437Codec;
            break;
        case Cp865:
            return new Cp865Codec;
            break;
        case NumEncodings:
            // nothing found...
            break;
        }
    }

    // Sometimes, people use 8-bit punctation or the pound/euro signs
    // in otherwise ASCII text. Let's look for the common cases.
    bool latin1 = true;
    bool latin9 = true;
    bool windows1252 = true;
    b = 0;
    while ( b < s.length() ) {
        while ( b < s.length() && s[b] < 128 )
            b++; // just for ease of single-stepping
        char c = s[b];
        b++;
        if ( c >= 160 ) {
            if ( c == 0xA4 /* euro */ ) {
                latin1 = false;
                windows1252 = false;
            }
            else if ( c == 0xAB /* laquo */ ||
                      c == 0xBB /* raquo */ ||
                      c == 0xA3 /* pound */ ||
                      c == 0xB4 /* acute accent - like ' */ ||
                      c == 0xA9 /* copyright */ ||
                      c == 0xAE /* registered trademark */ ||
                      c == 0xB0 /* degree sign */ ) {
                ; // can be any of the three character sets
            }
            else {
                latin9 = false;
                latin1 = false;
                windows1252 = false;
            }
        }
        else if ( c >= 128 ) {
            latin1 = false;
            latin9 = false;
            if ( ! (c == 0x80 /* euro */ ||
                    c == 0x96 /* dash */ ||
                    // the rest are all quotes
                    c == 0x82 || c == 0x84 || c == 0x8B || c == 0x9b ||
                    ( c >= 0x91 && c <= 0x94 ) ) )
                windows1252 = false;
        }
    }
    if ( latin1 )
        return new Iso88591Codec;
    if ( latin9 )
        return new Iso885915Codec;
    if ( windows1252 )
        return new Cp1252Codec;
    return 0;
}


/*! \class TableCodec codec.h
  The TableCodec provides a codec for simple 256-entry character sets.

  A great many characters sets, such as ISO 8859-2, fit in one byte
  and have a fixed known mapping to Unicode. This class provides a map
  to and from Uncode for such character sets. Each character set must
  subclass this, but no reimplementation is necessary.

  Codecs which map 0x80-0x9F to U+0080-0x009F consider any strings
  which contain 0x80-0x9F badly formed.

  At the moment, the fromUnicode function is rather slow. This may
  need fixing later.
*/


/*! \fn TableCodec::TableCodec( const uint * table, const char * name )
    Creates an empty TableCodec mapping to/from Unicode using \a table
    and named \a name.
*/


/*! Converts \a u from Unicode to the subclass' character encoding. All
    Unicode code points which cannot be representated in that encoding
    are converted to '?'.
*/

EString TableCodec::fromUnicode( const UString & u )
{
    EString s;
    s.reserve( u.length() );
    uint i = 0;
    while ( i < u.length() ) {
        uint j = 0;
        while ( j < 256 && t[j] != u[i] )
            j++;
        if ( j < 256 )
            s.append( (char)j );
        else
            s.append( '?' );
        i++;
    }

    return s;
}


/*! Converts \a s from the subclass' character encoding to Unicode. */

UString TableCodec::toUnicode( const EString & s )
{
    UString u;
    u.reserve( s.length() );
    uint i = 0;
    while ( i < s.length() ) {
        uint c = s[i];
        if ( !t[c] ) {
            recordError( i, c );
            u.append( 0xFFFD );
        }
        else if ( t[c] == c && c >= 0x80 && c < 0xA0 ) {
            u.append( t[c] );
            if ( wellformed() )
                setState( BadlyFormed );
        }
        else {
            append( u, t[c] );
        }
        i++;
    }
    mangleTrailingSurrogate( u );
    return u;
}

/*! \fn bool Codec::wellformed() const

Returns true if this codec's input has so far been well-formed, and
false if not. The definition of wellformedness is left to each
subclass. As general guidance, to be wellformed, the input must avoid
features that are discouraged or obsoleted by the relevant standard.
*/


/*! \fn bool Codec::valid() const

Returns true if this codec's input has not yet seen any syntax errors,
and false if it has.
*/


/*! This virtual function resets the codec. After calling reset(), the
    codec again reports that the input was wellformed() and valid(),
    and any codec state must have been set to the default state.
*/

void Codec::reset()
{
    setState( Valid );
}


/*! Appends \a c to \a u. If \a c isn't a legal codepoint or there are
    other errors, this codec's state is modifed appropriately.
*/

void Codec::append( UString & u, uint c )
{
    if ( c >= 0x110000 ) {
        u.append( 0xFFFD );
        recordError( "Codepoint U+" + fn( c, 16 ) +
                     " is not valid in Unicode" );
    }
    else if ( c >= 0xDC00 && c <= 0xDFFF ) {
        uint h = u[u.length()-1];
        if ( u.length() > 0 && h >= 0xD800 && h <= 0xDBFF ) {
            u.truncate( u.length()-1 );
            u.append( ( h - 0xD800 ) * 0x400 +
                      ( c - 0xDC00 ) +
                      0x10000 );
        }
        else {
            u.append( 0xFFFD );
            recordError( "Lone surrogate U+" + fn( c, 16 ) +
                         " is not valid in Unicode" );
        }
    }
    else {
        mangleTrailingSurrogate( u );
        u.append( c );
    }
}


/*! Checks whether the last codepoint in \a u is a leading surrogate,
    and flags an error if so.
*/

void Codec::mangleTrailingSurrogate( UString & u )
{
    if ( u.isEmpty() )
        return;
    uint c = u[u.length()-1];
    if ( c < 0xD800 || c > 0xDBFF )
        return;

    u.truncate( u.length()-1 );
    u.append( 0xFFFD );
    recordError( "Lone surrogate U+" + fn( c, 16 ) +
                 " is not valid in Unicode" );
}


/*! \class AsciiCodec codec.h
  The AsciiCodec class maps between US-ASCII and Unicode.

  US-ASCII Character codes 1 to 127 are accepted, all other byte
  values trigger invalidity (see valid() for details).
*/


/*!  Constructs an empty US-ASCII Codec. */

AsciiCodec::AsciiCodec()
    : Codec( "US-ASCII" )
{
    // nothing
}


/*! Maps \a u to US-ASCII. Code point 0 and all codecpoints greater
    than 127 are mapped to '?'.
*/

EString AsciiCodec::fromUnicode( const UString & u )
{
    EString r;
    r.reserve( u.length() );
    uint i = 0;
    while ( i < u.length() ) {
        if ( u[i] >0 && u[i] < 128 )
            r.append( (char)u[i] );
        else
            r.append( '?' );
        i++;
    }
    return r;
}


UString AsciiCodec::toUnicode( const EString & s )
{
    UString u;
    u.reserve( s.length() );
    uint i = 0;
    while ( i < s.length() ) {
        if ( s[i] == 0 || s[i] > 127 ) {
            recordError( i, s[i] );
            append( u, 0xFFFD );
        }
        else {
            append( u, s[i] );
            if ( s[i] < 32 &&
                 s[i] != 10 && s[i] != 13 && s[i] != 9 &&
                 state() == Valid )
                setState( BadlyFormed );
        }
        i++;
    }
    return u;
}

/*! \chapter codecs

    \introduces AsciiCodec Codec Cp1250Codec Cp1251Codec Cp1252Codec
    Cp1253Codec Cp1254Codec Cp1255Codec Cp1256Codec Cp1257Codec
    Cp1258Codec Cp437Codec Cp737Codec Cp775Codec Cp850Codec Cp852Codec
    Cp855Codec Cp857Codec Cp860Codec Cp861Codec Cp862Codec Cp863Codec
    Cp864Codec Cp865Codec Cp866Codec Cp869Codec Cp874Codec
    Iso885910Codec Iso885911Codec Iso885913Codec Iso885914Codec
    Iso885915Codec Iso885916Codec Iso88591Codec Iso88592Codec
    Iso88593Codec Iso88594Codec Iso88595Codec Iso88596Codec
    Iso88597Codec Iso88598Codec Iso88599Codec Koi8RCodec Koi8UCodec
    MacRomanCodec TableCodec Utf8Codec

    The Codec classes provide mapping between Unicode and 8-bit
    character encodings. The base class, Codec, provides the entire
    interface, and a variety of subclasses provide implementation for
    each encoding, for example, Iso88595Codec implements ISO 8859-5,
    currently using the 1999 revision of 8859-5.

    The selection of encodings is too small still. To extend it, we
    need test data for more encodings. Please write to info@oryx.com
    if you can provide some.
*/

//codec US-ASCII AsciiCodec
