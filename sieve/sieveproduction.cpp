// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#include "sieveproduction.h"

#include "ustringlist.h"
#include "sieveparser.h"
#include "stringlist.h"
#include "mailbox.h"
#include "address.h"
#include "field.h"
#include "utf.h"


class SieveProductionData
    : public Garbage
{
public:
    SieveProductionData( const char * n )
        : parent( 0 ), parser( 0 ), start( 0 ), end( 0 ), name( n ) {}

    SieveProduction * parent;
    SieveParser * parser;
    uint start;
    uint end;
    const char * name;
    String error;
};


/*! \class SieveProduction sieveproduction.h

    The SieveProduction class is the common base class for
    SieveArgument, SieveCommand and the other classes that describe a
    single production in the Sieve grammar (or lexer). The "start"
    symbol is represented by SieveScript.

    SieveProduction does very little except remember where in the
    source it comes from, so errors can be reported well.
*/


/*! Constructs a SieveProduction for a production whose sieve name is
    \a name.
*/

SieveProduction::SieveProduction( const char * name )
    : Garbage(), d( new SieveProductionData( name ) )
{
}


/*! Notifies this SieveProduction that it's a child of \a parent. The
    parent() is used to construct error messages.
*/

void SieveProduction::setParent( SieveProduction * parent )
{
    d->parent = parent;
}


/*! Returns a pointer to this object's parent, or a null pointer if
    none has been set.
*/

SieveProduction * SieveProduction::parent() const
{
    return d->parent;
}


/*! This slightly hacky function records that the production was
    parsed by \a p, and \a p should also be used to report any errors
    this object might have. Could have been done as a constructor
    argument, but I've already written the constructors and I don't
    want to do all the editing.

    The initial value is 0.
*/

void SieveProduction::setParser( class SieveParser * p )
{
    d->parser = p;
    if ( d->parser && !d->error.isEmpty() )
        d->parser->rememberBadProduction( this );
}


/*! Returns the name of this production as defined in RFC 3028 section
    8.
*/

String SieveProduction::name() const
{
    return d->name;
}


/*! Notifies this SieveProduction that its parsing started at position
    \a p. The first character in the source is 0, and we count bytes,
    not lines.
*/

void SieveProduction::setStart( uint p )
{
    d->start = p;
}


/*! Returns what setStart() set, or 0. */

uint SieveProduction::start() const
{
    return d->start;
}


/*! Notifies this SieveProduction that its parsing ended at position
    \a p. The first character in the source is 0, and we count bytes,
    not lines.
*/

void SieveProduction::setEnd( uint p )
{
    d->end = p;
}


/*! Returns what setEnd() set, or 0. */

uint SieveProduction::end() const
{
    return d->end;
}


/*! Records that this production suffers from error \a e. Does nothing
    if setError has been called already. */

void SieveProduction::setError( const String & e )
{
    if ( d->error.isEmpty() || e.isEmpty() )
        d->error = e;
    if ( !d->error.isEmpty() && d->parser )
        d->parser->rememberBadProduction( this );
}


/*! Records that the sieve script requires \a extension. Should be
    called whenever a part of the parser sees that the input depends
    on a given extension. SieveScript::parse() checks that the
    "require" names this set of extensions.
*/

void SieveProduction::require( const String & extension )
{
    if ( d->parser )
        d->parser->rememberNeededExtension( extension );
}


/*! Returns what setError() set, or an empty string if no error has
    occured.
*/

String SieveProduction::error() const
{
    return d->error;
}


/*! Returns a list of all supported sieve extensions. The list is
    allocated for the purpose, so the caller can modify it at will.
*/

StringList * SieveProduction::supportedExtensions()
{
    StringList * r = new StringList;
    r->append( "envelope" );
    r->append( "fileinto" );
    r->append( "reject" );
    r->append( "body" );
    r->append( "subaddress" );
    return r;
}


class SieveArgumentData
    : public Garbage
{
public:
    SieveArgumentData(): number( 0 ), list( 0 ), calls( 0 ), parsed( false ) {}
    String tag;
    uint number;
    UStringList * list;
    uint calls;
    bool parsed;
};


/*! \class SieveArgument sieveproduction.h

    The SieveArgument class models the RFC 3028 "argument" production.

    Nothing prevents the user from setting both tag(), number() and
    stringList(), even though in theory exactly one should be set.
*/


SieveArgument::SieveArgument()
    : SieveProduction( "argument" ), d( new SieveArgumentData )
{
    // nothing needed
}


/*! Notifies this object that it has a tag, and that its tag is \a
    t. \a t should start with ':'.

*/

void SieveArgument::setTag( const String & t )
{
    d->tag = t;
    d->calls++;
}


/*! Returns the object's tag, which always starts with ':', or an
    empty string if this object doesn't have a tag.
*/

String SieveArgument::tag() const
{
    return d->tag;
}


/*! Notifies this object that has a number, and that its number is \a
    n.
*/

void SieveArgument::setNumber( uint n )
{
    d->number = n;
    d->calls++;
}


/*! Returns the object's number, or 0 if this object doesn't have a
    number (in which case it has a stringList(), tag() or a nonempty
    error()).
*/

uint SieveArgument::number() const
{
    return d->number;
}


/*! Notifies this object that it has a string list, and that its
    string list is \a s. If \a s is a null pointer, this function does
    nothing.
*/

void SieveArgument::setStringList( class UStringList * s )
{
    if ( !s )
        return;
    d->list = s;
    d->calls++;
}


/*! Returns the object's string list, or a null pointer if this object
    doesn't have a string list (in which case it has a number(), tag()
    or a nonempty error()).

*/

class UStringList * SieveArgument::stringList() const
{
    return d->list;
}


/*! Notifies this argument that it has been parsed if \a p is true,
    and that it hasn't if \a p is false. The initial value is
    false. This is only used by SieveTest for the moment, to keep
    track of which arguments have been parsed and which still need
    parsing.
*/

void SieveArgument::setParsed( bool p )
{
    d->parsed = p;
}


/*! Returns what setParsed() set, and false if setParsed() has never
    been called.
*/

bool SieveArgument::parsed() const
{
    return d->parsed;
}


/*! Records an error if this argument isn't a number. */

void SieveArgument::assertNumber()
{
    if ( !d->tag.isEmpty() )
        setError( "Expected a number here, not a tag" );
    else if ( d->list )
        setError( "Expected a number here, not a string or string list" );
}


/*! Records an error if this argument isn't a single string. */

void SieveArgument::assertString()
{
    if ( !d->tag.isEmpty() )
        setError( "Expected a string here, not a tag" );
    else if ( d->number )
        setError( "Expected a string here, not a number" );
    else if ( !d->list || d->list->isEmpty() )
        setError( "Expected a single string here" );
    else if ( d->list->count() != 1 )
        setError( "Expected a single string here, not a string list" );
}


/*! Records an error if this argument isn't a string list. */

void SieveArgument::assertStringList()
{
    if ( !d->tag.isEmpty() )
        setError( "Expected a string list here, not a tag" );
    else if ( d->number )
        setError( "Expected a string list here, not a number" );
    else if ( !d->list || d->list->isEmpty() )
        setError( "Expected a single string here" );
}


/*! Records an error if this argument isn't a tag. */

void SieveArgument::assertTag()
{
    if ( d->number )
        setError( "Expected a tag here, not a number" );
    else if ( d->list )
        setError( "Expected a tag here, not a string or string list" );
}


class SieveArgumentListData
    : public Garbage
{
public:
    SieveArgumentListData() {}

    List<SieveArgument> a;
    List<SieveTest> t;
};


/*! \class SieveArgumentList sieveproduction.h

    The SieveArgumentList class models the arguments production.
*/

SieveArgumentList::SieveArgumentList()
    : SieveProduction( "arguments" ), d( new SieveArgumentListData )
{
}


/*! Appands \a a to the list of arguments() kept by this object. Does
    nothing if \a a is 0.
*/

void SieveArgumentList::append( SieveArgument * a )
{
    if ( !a )
        return;
    d->a.append( a );
    a->setParent( this );
}


/*! Returns a pointer to this object's list of SieveArgument objects.
    The returned list may be empty, but the pointer is never 0.
*/

List<SieveArgument> * SieveArgumentList::arguments() const
{
    return &d->a;
}


/*! Appands \a t to the list of tests() kept by this object. Does
    nothing if \a t is 0.
*/

void SieveArgumentList::append( SieveTest * t )
{
    if ( !t )
        return;

    d->t.append( t );
    t->setParent( this );
}


/*! Returns a pointer to this object's list of SieveTest objects.
    The returned list may be empty, but the pointer is never 0.
*/

List<SieveTest> * SieveArgumentList::tests() const
{
    return &d->t;
}


/*! Makes sure that \a tag occurs either zero or one times in the
    argument list, and returns the following argument. Records an
    error if \a tag occurs more than once or occurs as the last
    argument.

    Returns a null pointer if \a tag doesn't occur or occurs as the
    last argument.
e*/

SieveArgument * SieveArgumentList::argumentFollowingTag( const String & tag )
{
    SieveArgument * firstTag = 0;
    SieveArgument * result = 0;
    List<SieveArgument>::Iterator i( arguments() );
    while ( i ) {
        String t = i->tag();
        if ( t == tag ) {
            if ( firstTag ) {
                firstTag->setError( "Tag used twice: " + tag );
                i->setError( "Tag used twice: " + tag );
            }
            else {
                firstTag = i;
                firstTag->setParsed( true );
            }
        }
        ++i;
        if ( firstTag && !result ) {
            if ( i ) {
                result = i;
                result->setParsed( true );
            }
            else {
                firstTag->setError( "Tag not followed by argument: " + tag );
            }
        }
    }
    return result;
}


/*! Looks for the \a tag and returns the value of the following
    string. Records an error if anything looks wrong.

    If \a tag doesn't occur, takeTaggedString() returns an empty
    string.

    Marks both arguments as parsed.
*/

UString SieveArgumentList::takeTaggedString( const String & tag )
{
    SieveArgument * a = argumentFollowingTag( tag );
    UString r;
    if ( !a )
        return r;
    
    a->assertString();
    if ( a->stringList() )
        r = *a->stringList()->firstElement();
    return r;
}


/*! Looks for the \a tag and returns the value of the following
    number. Records an error if anything looks wrong.

    If \a tag doesn't occur, takeTaggedNumber() returns 0.

    Marks both arguments as parsed.
*/

uint SieveArgumentList::takeTaggedNumber( const String & tag )
{
    SieveArgument * a = argumentFollowingTag( tag );
    if ( !a )
        return 0;
    a->assertNumber();
    return a->number();
}


/*! Finds the argument tagged \a tag and returns a pointer to it. If
    \a tag ocurs mor than once, all occurences are flagged as bad and
    the first occurence returned.

    Returns a null pointer if \a tag does not occur anywhere.

    Marks the returned argument as parsed.
*/

SieveArgument * SieveArgumentList::findTag( const String & tag ) const
{
    List<SieveArgument>::Iterator a( arguments() );
    while ( a && a->tag() != tag )
        ++a;
    SieveArgument * r = a;
    if ( a ) {
        ++a;
        while ( a ) {
            if ( a->tag() == tag ) {
                r->setError( "Tag occurs twice: " + tag );
                a->setError( "Tag occurs twice: " + tag );
            }
            ++a;
        }
    }
    if ( r )
        r->setParsed( true );
    return r;
}


class SieveBlockData
    : public Garbage
{
public:
    SieveBlockData() {}

    List<SieveCommand> c;
};


/*! \class SieveBlock sieveproduction.h

    The SieveBlock class models the RFC 3028 block.
*/


SieveBlock::SieveBlock()
    : SieveProduction( "block" ), d( new SieveBlockData )
{
}


/*! Appends \a c to this block. Does nothing if \a c is 0. */

void SieveBlock::append( class SieveCommand * c )
{
    if ( !c )
        return;

    d->c.append( c );
    c->setParent( this );
}


/*! Returns a pointer the list of commands held in this block. The
    returned pointer is never 0, but the list may be empty.
*/

List<SieveCommand> * SieveBlock::commands() const
{
    return &d->c;
}


class SieveCommandData
    : public Garbage
{
public:
    SieveCommandData(): arguments( 0 ), block( 0 ), require( false ) {}

    String identifier;
    SieveArgumentList * arguments;
    SieveBlock * block;
    bool require;
};


/*! \class SieveCommand sieveproduction.h

    The SieveCommand class models the RFC 3028 "command" production.
*/

SieveCommand::SieveCommand()
    : SieveProduction( "command" ), d( new SieveCommandData )
{
}


/*! Notifies this command that its identifier if \a i. The initial
    value is an empty string, which is not valid.
*/

void SieveCommand::setIdentifier( const String & i )
{
    d->identifier = i.lower();
}


/*! Returns what setIdentifier() set, or an empty string if
    setIdentifier() has not been called.
*/

String SieveCommand::identifier() const
{
    return d->identifier;
}


/*! Notifies this command that \a l is a list of its arguments. Does
    nothing if \a l is a null pointer.
*/

void SieveCommand::setArguments( SieveArgumentList * l )
{
    if ( !l )
        return;

    d->arguments = l;
    l->setParent( this );
}


/*! Returns what setArguments() set, or a null pointer if
    setArguments() has not been called.
*/

SieveArgumentList * SieveCommand::arguments() const
{
    return d->arguments;
}


/*! Notifies this command that \a b is its subsidiary block. Does
    nothing if \a b is 0.
*/

void SieveCommand::setBlock( SieveBlock * b )
{
    if ( !b )
        return;

    d->block = b;
    b->setParent( this );
}


/*! Returns what setBlock() set, or 0 if setBlock() has not been
    called.

*/

SieveBlock * SieveCommand::block() const
{
    return d->block;
}


/*! Notifies this command that in this position, "require" is either
    permitted or not, depending on \a p. The initial value is false.
*/

void SieveCommand::setRequirePermitted( bool p )
{
    d->require = p;
}


class SieveTestData
    : public Garbage
{
public:
    SieveTestData()
        : arguments( 0 ), block( 0 ),
          matchType( SieveTest::Is ),
          addressPart( SieveTest::NoAddressPart ),
          comparator( SieveTest::IAsciiCasemap ),
          bodyMatchType( SieveTest::Text ),
          headers( 0 ), envelopeParts( 0 ), keys( 0 ),
          contentTypes( 0 ),
          sizeOver( false ), sizeLimit( 0 )
    {}

    String identifier;
    SieveArgumentList * arguments;
    SieveBlock * block;

    SieveTest::MatchType matchType;
    SieveTest::AddressPart addressPart;
    SieveTest::Comparator comparator;
    SieveTest::BodyMatchType bodyMatchType;

    UStringList * headers;
    UStringList * envelopeParts;
    UStringList * keys;
    UStringList * contentTypes;
    bool sizeOver;
    uint sizeLimit;
};


/*! \class SieveTest sieveproduction.h

    The SieveTest class models the RFC 3028 "test" production.
*/

SieveTest::SieveTest()
    : SieveProduction( "test" ), d( new SieveTestData )
{
}


/*! Notifies this command that its identifier if \a i. The initial
    value is an empty string, which is not valid.
*/

void SieveTest::setIdentifier( const String & i )
{
    d->identifier = i.lower();
}


/*! Returns what setIdentifier() set, or an empty string if
    setIdentifier() has not been called.
*/

String SieveTest::identifier() const
{
    return d->identifier;
}


/*! Notifies this command that \a l is a list of its arguments. Does
    nothing if \a l is a null pointer.
*/

void SieveTest::setArguments( SieveArgumentList * l )
{
    if ( !l )
        return;

    d->arguments = l;
    l->setParent( this );
}


/*! Returns what setArguments() set, or a null pointer if
    setArguments() has not been called.
*/

SieveArgumentList * SieveTest::arguments() const
{
    return d->arguments;
}


/*! Performs second-phase parsing of this command. Checks that its
    name is supported and that the arguments fit the command. Assumes
    that the \a previous command is, well, \a previous and uses that
    to verify that there's no if/elsif/else mismatch.
*/

void SieveCommand::parse( const String & previous )
{
    if ( identifier().isEmpty() )
        setError( "Command name is empty" );

    uint maxargs = 0;
    uint minargs = 0;
    bool addrs = false;
    bool mailboxes = false;
    bool extensions = false;
    bool test = false;
    bool blk = false;

    String i = identifier();
    if ( i == "if" || i == "elsif" ) {
        test = true;
        blk = true;
        if ( i == "elsif" && previous != "if" && previous != "elsif" )
            setError( "elsif is only permitted after if/elsif" );
    }
    else if ( i == "else" ) {
        blk = true;
        if ( previous != "if" && previous != "elsif" )
            setError( "else is only permitted after if/elsif" );
    }
    else if ( i == "require" ) {
        extensions = true;
        minargs = 1;
        if ( !d->require )
            setError( "require is only permitted as the first command." );
    }
    else if ( i == "stop" ) {
        // nothing needed
    }
    else if ( i == "reject" ) {
        // nothing needed
    }
    else if ( i == "fileinto" ) {
        mailboxes = true;
        minargs = 1;
        maxargs = 1;
        require( "fileinto" );
    }
    else if ( i == "redirect" ) {
        addrs = true;
        minargs = 1;
        maxargs = 1;
    }
    else if ( i == "keep" ) {
        // nothing needed
    }
    else if ( i == "discard" ) {
        // nothing needed
    }
    else {
        setError( "Command unknown: " + identifier() );
    }

    if ( maxargs < minargs )
        maxargs = UINT_MAX;

    // test each condition in the same order as the variables declared
    // above

    if ( minargs &&
         ( !arguments() ||
           arguments()->arguments()->count() < minargs ) )
        setError( i + ": Too few arguments (" +
                  fn ( arguments()->arguments()->count() ) +
                  ", minimum required is " +
                  fn ( minargs ) + ")" );

    if ( maxargs < UINT_MAX &&
         arguments() &&
         arguments()->arguments()->count() > maxargs )
        setError( i + ": Too many arguments (" +
                  fn ( arguments()->arguments()->count() ) +
                  ", maximum allowed is " +
                  fn ( maxargs ) + ")" );

    if ( arguments() &&
         ( addrs || mailboxes || extensions ) ) {
        List<SieveArgument>::Iterator i( arguments()->arguments() );
        while ( i ) {
            SieveArgument * a = i;
            ++i;
            if ( a->number() ) {
                a->setError( "Number not permitted as argument to command " +
                             identifier() );
            }
            else if ( !a->tag().isEmpty() ) {
                a->setError( "Tag not permitted as argument to command " +
                             identifier() );
            }
            else if ( addrs ) {
                String s;
                if ( a->stringList() && a->stringList()->count() > 1 )
                    a->setError( "Only one address may be specified" );
                else
                    s = a->stringList()->firstElement()->utf8();
                AddressParser ap( s );
                if ( !ap.error().isEmpty() )
                    a->setError( "The argument must be an email address. "
                                 "This one is not: " + s );
                else if ( ap.addresses()->count() != 1 )
                    a->setError( "The string must be 1 email address. "
                                 "This one represents " +
                                 fn ( ap.addresses()->count() ) + ": " +
                                 s );
                else if ( ap.addresses()->first()->type() !=
                          Address::Normal )
                    a->setError( "The string must be an ordinary "
                                 "email address (localpart@domain). "
                                 "This one is not: " + s +
                                 " (it represents " +
                                 ap.addresses()->first()->toString() +
                                 ")" );
            }
            else if ( mailboxes ) {
                if ( !a->stringList() || a->stringList()->count() != 1 )
                    a->setError( "Must have exactly one mailbox name" );
                UStringList::Iterator i( a->stringList() );
                while ( i ) {
                    UString p;
                    p.append( "/" );
                    p.append( *i );

                    if ( !Mailbox::validName( *i ) &&
                         !Mailbox::validName( p ) )
                    {
                        a->setError( "Each string must be a mailbox name. "
                                     "This one is not: " + i->utf8() );
                    }
                    else if ( i->startsWith( "INBOX." ) ) {
                        // a sieve script which wants to reference a
                        // mailbox called INBOX.X must use lower case
                        // (inbox.x).
                        UString aox = i->mid( 6 );
                        aox = UStringList::split( '.', aox )->join( "/" );
                        a->setError( i->utf8().quoted() +
                                     " is Cyrus syntax. "
                                     "Archiveopteryx uses " +
                                     aox.utf8().quoted() );
                    }
                    ++i;
                }
            }
            else if ( extensions ) {
                UStringList::Iterator i( a->stringList() );
                StringList e;
                while ( i ) {
                    if ( !supportedExtensions()->contains( i->ascii() ) )
                        e.append( i->ascii().quoted() );
                    ++i;

                }
                if ( !e.isEmpty() )
                    a->setError( "Each string must be a supported "
                                 "sieve extension. "
                                 "These are not: " + e.join( ", " ) );
            }
        }
    }

    if ( test ) {
        // we must have a test
        if ( !arguments() || arguments()->tests()->count() != 1 )
            setError( "Command " + identifier() + " requires one test" );
        if ( arguments() ) {
            List<SieveTest>::Iterator i( arguments()->tests() );
            while ( i ) {
                i->parse();
                ++i;
            }
        }
    }
    else {
        // we cannot have a test
        if ( arguments() && arguments()->tests()->isEmpty() ) {
            List<SieveTest>::Iterator i( arguments()->tests() );
            while ( i ) {
                i->setError( "Command " + identifier() +
                             " does not use tests" );
                ++i;
            }
        }
    }

    if ( blk ) {
        // we must have a subsidiary block
        if ( !block() ) {
            setError( "Command " + identifier() +
                      " requires a subsidiary {..} block" );
        }
        else {
            String prev;
            List<SieveCommand>::Iterator i( block()->commands() );
            while ( i ) {
                i->parse( prev );
                prev = i->identifier();
                ++i;
            }
        }
    }
    else {
        // we cannot have a subsidiary block
        if ( block() )
            block()->setError( "Command " + identifier() +
                               " does not use a subsidiary command block" );
        // in this case we don't even bother syntax-checking the test
        // or block
    }
}


/*! Does semantic analysis and second-level parsing of sieve
    tests. Checks that the test is supported, etc.

*/

void SieveTest::parse()
{
    SieveArgument * ca = 0;
    SieveArgument * mta = 0;
    SieveArgument * apa = 0;
    bool cok = false;
    bool mtok = false;
    bool apok = false;
    if ( arguments()->arguments() ) {
        // first, if we can, look for :comparator, :is and others, and
        // parse those. (if those aren't applicable we'll later
        // discover it and flag them as errors.)
        List<SieveArgument>::Iterator i( arguments()->arguments() );
        while ( i ) {
            String t = i->tag();
            if ( t == ":comparator" ) {
                if ( ca ) {
                    ca->setError( ":comparator specified twice" );
                    i->setError( ":comparator specified twice" );
                }
                ca = i;
                i->setParsed( true );
                ++i;
                if ( i )
                    i->setParsed( true );

                if ( !i ) {
                    setError( ":comparator cannot be the least argument" );
                }
                else if ( !i->stringList() ) {
                    i->setError( "Need a comparator name after :comparator" );
                }
                else if ( i->stringList()->count() != 1 ) {
                    i->setError( "Need exactly one comparator name, not " +
                                 fn( i->stringList()->count() ) );
                }
                else {
                    UString c = i->stringList()->first()->simplified();
                    if ( c.isEmpty() )
                        i->setError( "Comparator name is empty" );
                    if ( c == "i;octet" )
                        d->comparator = IOctet;
                    else if ( c == "i;ascii-casemap" )
                        d->comparator = IAsciiCasemap;
                    else
                        setError( "Unknown comparator: " + c.utf8() );
                }
            }
            else if ( t == ":is" ||
                      t == ":contains" ||
                      t == ":matches" ) {
                if ( mta ) {
                    mta->setError( "Match type specified twice" );
                    i->setError( "Match type specified twice" );
                }
                mta = i;
                i->setParsed( true );
                if ( t == ":is" )
                    d->matchType = Is;
                if ( t == ":contains" )
                    d->matchType = Contains;
                if ( t == ":matches" )
                    d->matchType = Matches;
            }
            else if ( t == ":localpart" ||
                      t == ":domain" ||
                      t == ":user" ||
                      t == ":detail" ||
                      t == ":all" )
            {
                if ( apa ) {
                    apa->setError( "Address part specified twice" );
                    i->setError( "Address part specified twice" );
                }
                apa = i;
                i->setParsed( true );
                if ( t == ":localpart" ) {
                    d->addressPart = Localpart;
                }
                else if ( t == ":domain" ) {
                    d->addressPart = Domain;
                }
                else if ( t == ":all" ) {
                    d->addressPart = All;
                }
                else if ( t == ":user" || t == ":detail" ) {
                    require( "subaddress" );
                    if ( t == ":user" )
                        d->addressPart = User;
                    else
                        d->addressPart = Detail;
                }
            }
            ++i;
        }
    }

    if ( identifier() == "address" ) {
        cok = true;
        mtok = true;
        apok = true;
        d->headers = takeHeaderFieldList();
        d->keys = takeStringList();
    }
    else if ( identifier() == "allof" ||
              identifier() == "anyof" ) {
        if ( !arguments()->arguments()->isEmpty() )
            setError( "Test '" +
                      identifier() +
                      "' does not accept arguments, only a list of tests" );
        bool any = false;
        List<SieveTest>::Iterator i( arguments()->tests() );
        while ( i ) {
            any = true;
            i->parse();
            ++i;
        }
        if ( !any )
            setError( "Need at least one subsidiary test" );
    }
    else if ( identifier() == "envelope" ) {
        require( "envelope" );
        cok = true;
        mtok = true;
        apok = true;
        d->envelopeParts = takeStringList();
        d->keys = takeStringList();
        UStringList::Iterator i( d->envelopeParts );
        while ( i ) {
            String s = i->utf8().lower();
            if ( s == "from" || s == "to" ) {
                Utf8Codec c;
                *i = c.toUnicode( s );
            }
            // else if and blah for extensions - extensions are only
            // valid after the right require
            else {
                // better if we could setError on the right item, but it's gone
                setError( "Unsupported envelope part: " + i->utf8() );
            }
            ++i;
        }
    }
    else if ( identifier() == "exists" ) {
        d->headers = takeHeaderFieldList();
    }
    else if ( identifier() == "false" ) {
        // I wish all the tests were this easy
    }
    else if ( identifier() == "header" ) {
        cok = true;
        mtok = true;
        d->headers = takeHeaderFieldList();
        d->keys = takeStringList();
    }
    else if ( identifier() == "not" ) {
        if ( !arguments()->arguments()->isEmpty() )
            setError( "Test 'not' does not accept arguments, only a test" );
        if ( !arguments()->tests() ||
             arguments()->tests()->count() != 1 )
            setError( "Test 'not' needs exactly one subsidiary test" );
        else
            arguments()->tests()->first()->parse();
    }
    else if ( identifier() == "size" ) {
        List<SieveArgument>::Iterator i( arguments()->arguments() );
        while ( i && i->parsed() )
            ++i;
        if ( !i ) {
            setError( ":over/:under and number not supplied" );
        }
        else {
            String t = i->tag();
            if ( t == ":over" )
                d->sizeOver = true;
            else if ( t != ":under" )
                i->setError( "Expected tag :over/:under" );
            i->setParsed( true );
            ++i;
            if ( !i ) {
                setError( "Number not supplied" );
            }
            else {
                if ( !i->tag().isEmpty() || i->stringList() )
                    i->setError( "Need a number" );
                d->sizeLimit = i->number();
                i->setParsed( true );
            }
        }
    }
    else if ( identifier() == "true" ) {
        // much like false.
    }
    else if ( identifier() == "body" ) {
        require( "body" );
        cok = true;
        mtok = true;
        List<SieveArgument>::Iterator i( arguments()->arguments() );
        while ( i && i->parsed() )
            ++i;
        if ( !i )
            setError( "Need a string to look for" );
        String t;
        if ( i )
            t = i->tag();
        if ( t == ":raw" ) {
            d->bodyMatchType = Rfc822;
            i->setParsed( true );
            ++i;
        }
        else if ( t == ":text" ) {
            d->bodyMatchType = Text;
            i->setParsed( true );
            ++i;
        }
        else if ( t == ":content" ) {
            d->bodyMatchType = SpecifiedTypes;
            i->setParsed( true );
            SieveArgument * c = i;
            ++i;
            if ( i->stringList() ) {
                d->contentTypes = i->stringList();
                i->setParsed( true );
                ++i;
            }
            else {
                c->setError( ":content must be followed "
                             "by a string list" );
            }
        }
        while ( i && i->parsed() )
            ++i;
        if ( i ) {
            d->keys = i->stringList();
            i->setParsed( true );
            ++i;
        }
    }
    else {
        setError( "Unknown test: " + identifier() );
    }

    // any tagged things out of place?
    if ( ca && !cok )
        ca->setError( "Comparator cannot be specified in test '" +
                      identifier() + "'" );
    if ( mta && !mtok )
        mta->setError( "Match type cannot be specified in test '" +
                       identifier() + "'" );
    if ( apa && !apok )
        apa->setError( "Address-part cannot be specified in test '" +
                       identifier() + "'" );

    // any arguments we didn't parse?
    List<SieveArgument>::Iterator i( arguments()->arguments() );
    while ( i ) {
        if ( i->parsed() ) {
            // it's okay
        }
        else if ( i->number() ) {
            i->setError( "Why is this number here?" );
        }
        else if ( i->stringList() ) {
            i->setError( "Why is this string/list here?" );
        }
        else if ( !i->tag().isEmpty() ) {
            i->setError( "Unknown tag: " + i->tag() );
        }
        else {
            i->setError( "What happened? I'm dazed and confused" );
        }
        ++i;
    }
}


/*! Returns the match type specified, or Is if none has been
    explicitly specified.
*/

SieveTest::MatchType SieveTest::matchType() const
{
    return d->matchType;
}


/*! Returns the address part specified, or NoAddressPart if none has
    been expiclitly specified.

*/

SieveTest::AddressPart SieveTest::addressPart() const
{
    return d->addressPart;
}


/*! Returns the comparator specified, or SieveTest::IAsciiCasemap if
    none has been.
*/

SieveTest::Comparator SieveTest::comparator() const
{
    return d->comparator;
}


/*! Takes the first unparsed string list from the list of arguments,
    calls SieveArgument::setParsed() on it and returns a pointer to
    it. Calls setError() and returns a null pointer if no unparsed
    string lists are available.
*/

UStringList * SieveTest::takeStringList()
{
    List<SieveArgument>::Iterator i( arguments()->arguments() );
    while ( i && ( i->parsed() || !i->stringList() ) )
        ++i;
    if ( !i ) {
        setError( "Missing string/list argument" );
        return 0;
    }
    i->setParsed( true );
    return i->stringList();
}


/*! As takeStringList(), and additionally checks that each string is a
    valid header field name according to RFC 2822 section 3.6.8, and
    if identifier() is "address", that each refers to an address
    field. The result is filtered through String::headerCased().
*/

UStringList * SieveTest::takeHeaderFieldList()
{
    List<SieveArgument>::Iterator a( arguments()->arguments() );
    while ( a && ( a->parsed() || !a->stringList() ) )
        ++a;
    if ( !a ) {
        setError( "Missing string/list argument" );
        return 0;
    }
    a->setParsed( true );

    UStringList::Iterator h( a->stringList() );
    while ( h ) {
        UString s = *h;
        if ( s.isEmpty() )
            a->setError( "Empty header field names are not allowed" );
        uint i = 0;
        while ( i < s.length() ) {
            if ( s[i] < 33 || s[i] == 58 || s[i] > 126 )
                a->setError( "Illegal character (ASCII " + fn( s[i] ) + ") "
                             "seen in header field name: " + s.utf8() );
            ++i;
        }
        if ( identifier() == "address" ) {
            uint t = HeaderField::fieldType( s.ascii() );
            if ( t == 0 || t > HeaderField::LastAddressField )
                a->setError( "Not an address field: " + s.ascii() );
        }
        s.truncate();
        s.append( h->ascii().headerCased().cstr() ); // eeek
        if ( s != *h )
            *h = s;
        ++h;
    }

    return a->stringList();
}


/*! Takes the first tag and returns it. Returns an empty string if
    there aren't any tags. Marks an error if there's an unparsed
    argument before the first unparsed tag.
*/

String SieveTest::takeTag()
{
    List<SieveArgument>::Iterator a( arguments()->arguments() );
    while ( a && a->parsed() )
        ++a;
    while ( a && a->parsed() && a->tag().isEmpty() ) {
        a->setError( "Could not parse this argument (was looking for a tag)" );
        ++a;
    }
    if ( !a )
        return "";
    a->setParsed( true );
    return a->tag();
}



/*! Returns a list of the headers to which the identifier() pertains,
    or a null pointer if the identifier() is of a type that doesn't
    use any header fields.

    Each string in the list is header-cased (see String::headerCased()).
*/

UStringList * SieveTest::headers() const
{
    return d->headers;
}


/*! Returns a list of the keys to be searched for, or a null pointer
    if none are known (which is the case e.g. if identifier() is
    "exists" or "true").
*/

UStringList * SieveTest::keys() const
{
    return d->keys;
}


/*! Returns a list of the envelope parts the test "envelope" should
    look at, or a null pointer if identifier() is not "envelope".
*/

UStringList * SieveTest::envelopeParts() const
{
    return d->envelopeParts;
}


/*! Returns the body match type for this test, or Text for the
    default. The result is meaningful only when identifier() is
    "body".
*/

SieveTest::BodyMatchType SieveTest::bodyMatchType() const
{
    return d->bodyMatchType;
}


/*! Returns a pointer to a list of the content types to be used for
    the "body" test, assuming that bodyMatchType() returns
    SpecifiedTypes. May return a null pointer.
*/

UStringList * SieveTest::contentTypes() const
{
    return d->contentTypes;
}


/*! Returns true if the test identifier() is "size" and the tag :over
    is active, and false in all other cases.
*/

bool SieveTest::sizeOverLimit() const
{
    return d->sizeOver;
}


/*! Returns the size limit for identifier() "test", and 0 for all
    other tests.
*/

uint SieveTest::sizeLimit() const
{
    return d->sizeLimit;
}
