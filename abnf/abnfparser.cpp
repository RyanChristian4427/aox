// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#include "abnfparser.h"


class AbnfParserData
    : public Garbage
{
public:
    AbnfParserData( AbnfParserData * other = 0 )
        : at( 0 ), next( 0 ), mark( 1 )
    {
        if ( other ) {
            at = other->at;
            next = other;
            mark = other->mark + 1;
        }
    }

    uint at;
    EString err;

    AbnfParserData * next;
    uint mark;
};


/*! \class AbnfParser abnfparser.h
    Provides simple functions to parse ABNF productions.

    This class maintains a cursor for an input EString and provides
    functions to examine or extract tokens, advancing the cursor as
    required. These generic functions may be used by subclasses (e.g.
    ImapParser) to parse more complex productions.

    The functions usually return a token extracted from the input string
    at the cursor, and advance the cursor to point to the next token. If
    the input cannot be parsed, a function might return an invalid token
    (e.g. nextChar() returns 0 once the cursor has passed the end of the
    input) or signal an error (e.g. end() complains if any input is left
    over once we've finished parsing what we expected to). In the latter
    case, ok() is false and error() returns a suitable message (and the
    input cannot be parsed any further).

    In typical usage, one would create a new AbnfParser object for a
    string, step through its contents with functions like nextChar(),
    step(), and require(), then call end() when the string has been
    completely parsed.
*/

/*! Constructs an AbnfParser for the EString \a s. */

AbnfParser::AbnfParser( const EString & s )
    : str( s ), d( new AbnfParserData )
{
}


/*! Virtual destructor for the benefit of subclasses. */

AbnfParser::~AbnfParser()
{
}


/*! Returns false once this object has encountered an error during
    parsing (in which case error() will return a suitable message),
    or true if it's still usable.
*/

bool AbnfParser::ok() const
{
    return d->err.isEmpty();
}


/*! Returns a message describing the first parse error encountered, or
    an empty string if no errors have occurred (in which case ok() is
    also true).
*/

EString AbnfParser::error() const
{
    return d->err;
}


/*! Sets the object's error() to \a s, provided this is the first time
    setError() is called with a nonempty argument. (error() returns
    the argument to the first setError() call.)

    ok() will return false after this function has been called with a
    non-empty argument. This function is intended for use by the
    individual parser functions.
*/

void AbnfParser::setError( const EString & s )
{
    if ( d->err.isEmpty() || s.isEmpty() )
        d->err = s;
}


/*! Returns the current (0-indexed) position of the cursor in the input()
    string without changing anything.
*/

uint AbnfParser::pos() const
{
    return d->at;
}


/*! Returns the input string. */

EString AbnfParser::input() const
{
    return str;
}


/*! Returns the next character at the cursor without changing the cursor
    position. Returns 0 if there isn't a character available (e.g. when
    the cursor is past the end of the input string).
*/

char AbnfParser::nextChar() const
{
    return str[d->at];
}


/*! Advances the cursor past \a n characters (1 by default) of the
    input.
*/

void AbnfParser::step( uint n )
{
    d->at += n;
}


/*! Checks whether the next characters in the input match \a s. If so,
    present() steps past the matching characters and returns true. If
    not, it returns false without advancing the cursor. The match is
    case insensitive.
*/

bool AbnfParser::present( const EString & s )
{
    if ( s.isEmpty() )
        return true;

    EString l = str.mid( d->at, s.length() ).lower();
    if ( l != s.lower() )
        return false;

    step( s.length() );
    return true;
}


/*! Requires that the next characters in the input match \a s (case
    insensitively), and steps past the matching characters. If \a s
    is not present(), it is considered an error().
*/

void AbnfParser::require( const EString & s )
{
    if ( !present( s ) )
        setError( "Expected: " + s.quoted() + ", got: " + following() );
}


/*! Returns a string of between \a min and \a max digits at the cursor
    and advances the cursor past them. If fewer than \a min digits are
    available, it is an error().
*/

EString AbnfParser::digits( uint min, uint max )
{
    EString r;
    uint i = 0;
    char c = nextChar();
    while ( i < max && c >= '0' && c <= '9' ) {
        step();
        r.append( c );
        c = nextChar();
        i++;
    }
    if ( i < min )
        setError( "Expected at least " + fn( min-i ) + " more digits, "
                  "but saw: " + following() );
    return r;
}


/*! Returns a string of between \a min and \a max letters ([A-Za-z]) at
    the cursor and advances the cursor past them. If fewer than \a min
    letters are available, it is an error().
*/

EString AbnfParser::letters( uint min, uint max )
{
    EString r;
    uint i = 0;
    char c = nextChar();
    while ( i < max &&
            ( ( c >= 'A' && c <= 'Z' ) || ( c >= 'a' && c <= 'z' ) ) )
    {
        step();
        r.append( c );
        c = nextChar();
        i++;
    }
    if ( i < min )
        setError( "Expected at least " + fn( min-i ) + " more letters, "
                  "but saw: " + following() );
    return r;
}


/*! Returns the unsigned integer (0*|[1-9][0-9]*) at the cursor and
    advances the cursor past it. It is an error() if there isn't an
    integer at the cursor, or if a non-zero number is specified with
    a leading 0 digit.
*/

uint AbnfParser::number()
{
    EString s;
    char c = nextChar();

    bool zero = false;
    if ( c == '0' )
        zero = true;

    while ( c >= '0' && c <= '9' ) {
        s.append( c );
        step();
        c = nextChar();
    }

    bool ok = true;
    uint u = s.number( &ok );
    if ( !ok )
        setError( "Expected a number, but saw: " + s + following() );
    else if ( u > 0 && zero )
        setError( "Zero used as leading digit" );

    return u;
}


/*! Asserts that the input has been completely parsed. It is considered
    an error() if any input text remains when this function is called.
*/

void AbnfParser::end()
{
    if ( !atEnd() )
        setError( EString( "More text follows end of input: " ) + following() );
}


/*! Returns a string of no more than 15 characters containing the first
    unparsed bits of input. Meant for use in error messages.
*/

const EString AbnfParser::following() const
{
    return str.mid( d->at, 15 ).simplified();
}


/*! Returns true if we have parsed the entire input string, and false
    otherwise.
*/

bool AbnfParser::atEnd() const
{
    return d->at >= str.length();
}


/*! Saves the current cursor position and error state of the parser
    and returns an identifier of the current mark. The companion
    function restore() restores the last or a specified mark. The
    returned mark is never 0.
*/

uint AbnfParser::mark()
{
    d = new AbnfParserData( d );
    return d->next->mark;
}


/*! Restores the last mark()ed cursor position and error state of this
    parser object.
*/

void AbnfParser::restore()
{
    if ( d->next )
        d = d->next;
}


/*! Restores the cursor position and error state at the time when
    mark() returned \a m. Does nothing if \a m is not a valid mark. \a
    m remains valid, so it's possibble to restore( \a m ) again.
*/

void AbnfParser::restore( uint m )
{
    AbnfParserData * c = d;
    while ( c && c->mark != m && c->next )
        c = c->next;
    if ( c && c->mark == m )
        d = c;
}
