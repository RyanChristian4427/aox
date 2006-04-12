// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#include "sieve.h"

#include "log.h"
#include "user.h"
#include "query.h"
#include "string.h"
#include "buffer.h"
#include "eventloop.h"
#include "stringlist.h"
#include "sievecommand.h"
#include "configuration.h"


class SieveData
    : public Garbage
{
public:
    SieveData()
        : state( Sieve::Unauthorised ), user( 0 ),
          commands( new List< SieveCommand > ), reader( 0 ),
          reserved( false )
    {}

    Sieve::State state;

    User * user;

    List< SieveCommand > * commands;
    SieveCommand * reader;
    bool reserved;
};


static bool allowPlaintext = true;
static bool supportsPlain = true;
static bool supportsCramMd5 = true;
static bool supportsDigestMd5 = true;
static bool supportsAnonymous = true;


static void newCommand( List< SieveCommand > *, Sieve *,
                        SieveCommand::Command, StringList * = 0 );


/*! \class Sieve sieve.h
    This class implements a Sieve server.

    The Sieve protocol is defined in draft-martin-managesieve-06.txt.
*/

/*! Creates a Sieve server for the fd \a s, and sends the initial banner.
*/

Sieve::Sieve( int s )
    : Connection( s, Connection::SieveServer ),
      d( new SieveData )
{
    capabilities();
    setTimeoutAfter( 1800 );
    EventLoop::global()->addConnection( this );
}


/*! Sets this server's state to \a s, which may be either Unauthorised
    or Authorised (as defined in Sieve::State).
*/

void Sieve::setState( State s )
{
    d->state = s;
}


/*! Returns the server's current state. */

Sieve::State Sieve::state() const
{
    return d->state;
}


void Sieve::react( Event e )
{
    switch ( e ) {
    case Read:
        setTimeoutAfter( 600 );
        parse();
        break;

    case Timeout:
        log( "Idle timeout" );
        send( "BYE Idle timeout" );
        Connection::setState( Closing );
        break;

    case Connect:
    case Error:
    case Close:
        break;

    case Shutdown:
        send( "BYE Server shutdown" );
        break;
    }

    commit();
}


/*! Parses Sieve client commands. */

void Sieve::parse()
{
    Buffer *b = readBuffer();

    while ( b->size() > 0 ) {
        if ( !d->reader ) {
            if ( d->reserved )
                break;

            String *s = b->removeLine( 255 );

            if ( !s ) {
                log( "Connection closed due to overlong line (" +
                     fn( b->size() ) + " bytes)", Log::Error );
                send( "BYE Line too long. Closing connection." );
                Connection::setState( Closing );
                return;
            }

            bool unknown = false;

            StringList * args = StringList::split( ' ', *s );
            String cmd = args->take( args->first() )->lower();

            if ( cmd == "logout" && args->isEmpty() ) {
                newCommand( d->commands, this, SieveCommand::Logout );
            }
            else if ( cmd == "capability" && args->isEmpty() ) {
                newCommand( d->commands, this, SieveCommand::Capability );
            }
            else if ( d->state == Unauthorised ) {
                if ( cmd == "starttls" ) {
                    if ( hasTls() )
                        no( "Nested STARTTLS" );
                    else
                        newCommand( d->commands, this, SieveCommand::StartTls );
                }
                else if ( cmd == "authenticate" ) {
                    newCommand( d->commands, this, SieveCommand::Authenticate,
                                args );
                }
                else {
                    unknown = true;
                }
            }
            else if ( d->state == Authorised ) {
                if ( cmd == "havespace" && args->count() == 2 ) {
                    newCommand( d->commands, this, SieveCommand::HaveSpace,
                                args );
                }
                else if ( cmd == "putscript" && args->count() == 2 ) {
                    newCommand( d->commands, this, SieveCommand::PutScript,
                                args );
                }
                else if ( cmd == "setactive" && args->count() == 1 ) {
                    newCommand( d->commands, this, SieveCommand::SetActive,
                                args );
                }
                else if ( cmd == "listscripts" && args->isEmpty() ) {
                    newCommand( d->commands, this, SieveCommand::ListScripts );
                }
                else if ( cmd == "getscript" && args->count() == 1 ) {
                    newCommand( d->commands, this, SieveCommand::GetScript,
                                args );
                }
                else if ( cmd == "deletescript" && args->count() == 1 ) {
                    newCommand( d->commands, this, SieveCommand::DeleteScript,
                                args );
                }
                else {
                    unknown = true;
                }
            }
            else {
                unknown = true;
            }

            if ( unknown )
                no( "Unknown command" );
        }
        else {
            d->reader->read();
        }

        runCommands();
    }
}


/*! Sends \a s as a positive OK response. */

void Sieve::ok( const String &s )
{
    enqueue( "OK" );
    if ( !s.isEmpty() )
        enqueue( " " + s );
    enqueue( "\r\n" );
}


/*! Sends \a s as a negative NO response. */

void Sieve::no( const String &s )
{
    enqueue( "NO" );
    if ( !s.isEmpty() )
        enqueue( " " + s );
    enqueue( "\r\n" );
    setReader( 0 );
}


/*! Sends the literal response \a s without adding a tag. */

void Sieve::send( const String &s )
{
    enqueue( s );
    enqueue( "\r\n" );
}


/*! The Sieve server maintains a list of commands received from the
    client and processes them one at a time in the order they were
    received. This function executes the first command in the list,
    or if the first command has completed, removes it and executes
    the next one.

    It should be called when a new command has been created (i.e.,
    by Sieve::parse()) or when a running command finishes.
*/

void Sieve::runCommands()
{
    List< SieveCommand >::Iterator it( d->commands );
    if ( !it )
        return;
    if ( it->done() )
        d->commands->take( it );
    if ( it )
        it->execute();
}


static void newCommand( List< SieveCommand > * l, Sieve * sieve,
                        SieveCommand::Command cmd,
                        StringList * args )
{
    l->append( new SieveCommand( sieve, cmd, args ) );
}


/*! Sets the current user of this Sieve server to \a u. Called upon
    successful completion of an Authenticate command.
*/

void Sieve::setUser( User * u )
{
    d->user = u;
}


/*! Returns the current user of this Sieve server, or an empty string if
    setUser() has never been called after a successful authentication.
*/

User * Sieve::user() const
{
    return d->user;
}


/*! Reserves the input stream to inhibit parsing if \a r is true. If
    \a r is false, then the server processes input as usual. Used by
    STLS to inhibit parsing.
*/

void Sieve::setReserved( bool r )
{
    d->reserved = r;
}


/*! Reserves the input stream for processing by \a cmd, which may be 0
    to indicate that the input should be processed as usual. Used by
    AUTH to parse non-command input.
*/

void Sieve::setReader( SieveCommand * cmd )
{
    d->reader = cmd;
    d->reserved = d->reader;
}


/*! Returns true only if this Sieve server supports the authentication
    mechanism named \a s (which must be in lowercase).

    XXX: This is copied from IMAP. What to do about the duplication?
*/

bool Sieve::supports( const String &s ) const
{
    if ( ::supportsDigestMd5 && s == "digest-md5" )
        return true;

    if ( ::supportsCramMd5 && s == "cram-md5" )
        return true;

    if ( ::allowPlaintext || hasTls() ) {
        if ( ::supportsPlain && s == "plain" )
            return true;
        if ( ::supportsAnonymous && s == "anonymous" )
            return true;
        if ( s == "login" )
            return true;
    }

    return false;
}


/*! This setup function expects to be called from ::main().

    It reads and validates any relevant configuration variables, and
    logs a disaster if it encounters an error.
*/

void Sieve::setup()
{
    ::supportsPlain = Configuration::toggle( Configuration::AuthPlain );
    ::supportsCramMd5 =
          Configuration::toggle( Configuration::AuthCramMd5 );
    ::supportsDigestMd5 =
          Configuration::toggle( Configuration::AuthDigestMd5 );
    ::supportsAnonymous =
          Configuration::toggle( Configuration::AuthAnonymous );

    String s =
        Configuration::text( Configuration::AllowPlaintextPasswords ).lower();
    if ( s == "always" )
        ::allowPlaintext = true;
    else if ( s == "never" )
        ::allowPlaintext = false;
    else
        ::log( "Unknown value for allow-plaintext-passwords: " + s,
               Log::Disaster );
}


/*! Enqueues a suitably-formatted list of our capabilities. */

void Sieve::capabilities()
{
    String v( Configuration::compiledIn( Configuration::Version ) );
    enqueue( "\"IMPLEMENTATION\" \"Archiveopteryx " + v + "\"\r\n" );
    enqueue( "\"SASL\" \"\"\r\n" );
    enqueue( "\"SIEVE\" \"\"\r\n" );
    enqueue( "\"STARTTLS\"\r\n" );
    enqueue( "OK\r\n" );
}
