// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#include "managesieve.h"

#include "log.h"
#include "query.h"
#include "estring.h"
#include "buffer.h"
#include "mechanism.h"
#include "eventloop.h"
#include "estringlist.h"
#include "configuration.h"
#include "sieveproduction.h"
#include "managesievecommand.h"


class ManageSieveData
    : public Garbage
{
public:
    ManageSieveData()
        : state( ManageSieve::Unauthorised ),
          commands( new List< ManageSieveCommand > ), reader( 0 ),
          reserved( false ), readingLiteral( false ),
          literalSize( 0 )
        {}

    ManageSieve::State state;

    List< ManageSieveCommand > * commands;
    ManageSieveCommand * reader;
    bool reserved;

    EString arg;

    bool readingLiteral;
    uint literalSize;
};



/*! \class ManageSieve managesieve.h
    This class implements a ManageSieve server.

    The ManageSieve protocol is defined in draft-martin-managesieve-06.txt.
*/

/*! Creates a ManageSieve server for the fd \a s, and sends the initial banner.
*/

ManageSieve::ManageSieve( int s )
    : SaslConnection( s, Connection::ManageSieveServer ),
      d( new ManageSieveData )
{
    capabilities();
    enqueue( "OK\r\n" );
    setTimeoutAfter( 120 );
    EventLoop::global()->addConnection( this );
}


/*! Sets this server's state to \a s, which may be either Unauthorised
    or Authorised (as defined in ManageSieve::State).
*/

void ManageSieve::setState( State s )
{
    d->state = s;
}


/*! Returns the server's current state. */

ManageSieve::State ManageSieve::state() const
{
    return d->state;
}


void ManageSieve::react( Event e )
{
    switch ( e ) {
    case Read:
        if ( user() )
            setTimeoutAfter( 3600 );
        parse();
        break;

    case Timeout:
        log( "Idle timeout" );
        send( "BYE \"Idle timeout\"" );
        Connection::setState( Closing );
        break;

    case Connect:
    case Error:
    case Close:
        break;

    case Shutdown:
        send( "BYE \"Server shutdown\"" );
        break;
    }
}


/*! Parses ManageSieve client commands. */

void ManageSieve::parse()
{
    Buffer *b = readBuffer();

    while ( b->size() > 0 ) {
        if ( d->readingLiteral ) {
            if ( b->size() < d->literalSize )
                return;

            d->arg.append( b->string( d->literalSize ) );
            b->remove( d->literalSize );
            d->readingLiteral = false;
        }
        if ( !d->readingLiteral ) {
            if ( d->reserved )
                break;

            EString * s = b->removeLine( 3072 );

            if ( !s ) {
                log( "Connection closed due to overlong line (" +
                     fn( b->size() ) + " bytes)", Log::Error );
                send( "BYE \"Line too long\"" );
                Connection::setState( Closing );
                return;
            }

            d->arg.append( *s );

            if ( s->endsWith( "+}" ) ) {
                uint e = s->length() - 2;
                uint b = e - 1;
                while ( b > 0 && (*s)[b] >= '0' && (*s)[b] <= '9' )
                    b--;
                if ( (*s)[b] == '{' && b + 1 < e ) {
                    b++;
                    d->readingLiteral = true;
                    bool ok = true;
                    d->literalSize = s->mid( b, e-b ).number( &ok );
                    if ( !ok ) {
                        // what? we can't possibly read it. have to
                        // close the connection?
                        log( "Connection closed due to large literal (" +
                             s->mid( b, e-b ) + " bytes)", Log::Error );
                        send( "BYE \"Literal too large\"" );
                        Connection::setState( Closing );
                    }
                    d->arg.append( "\r\n" );
                }
            }

            if ( !d->readingLiteral ) {
                if ( d->reader ) {
                    d->reader->setArguments( d->arg );
                    d->arg.truncate();
                }
                else {
                    addCommand();
                }
            }
        }

        runCommands();
    }
}


/*! Creates a new ManageSieveCommand based on the arguments received
    from the client.
*/

void ManageSieve::addCommand()
{
    int i = d->arg.find( ' ' );
    if ( i < 0 )
        i = d->arg.length();

    EString cmd = d->arg.mid( 0, i ).lower();
    d->arg = d->arg.mid( i+1 );

    ManageSieveCommand::Command c = ManageSieveCommand::Unknown;

    if ( cmd == "logout" ) {
        c = ManageSieveCommand::Logout;
    }
    else if ( cmd == "capability" ) {
        c = ManageSieveCommand::Capability;
    }
    else if ( d->state == Unauthorised ) {
        if ( cmd == "starttls" )
            c = ManageSieveCommand::StartTls;
        else if ( cmd == "authenticate" )
            c = ManageSieveCommand::Authenticate;
    }
    else if ( d->state == Authorised ) {
        if ( cmd == "havespace" )
            c = ManageSieveCommand::HaveSpace;
        else if ( cmd == "putscript" )
            c = ManageSieveCommand::PutScript;
        else if ( cmd == "setactive" )
            c = ManageSieveCommand::SetActive;
        else if ( cmd == "listscripts" )
            c = ManageSieveCommand::ListScripts;
        else if ( cmd == "getscript" )
            c = ManageSieveCommand::GetScript;
        else if ( cmd == "deletescript" )
            c = ManageSieveCommand::DeleteScript;
        else if ( cmd == "x-aox-explain" )
            c = ManageSieveCommand::XAoxExplain;
    }

    ManageSieveCommand * n = new ManageSieveCommand( this, c );
    n->setArguments( d->arg );
    d->arg.truncate();
    d->commands->append( n );
}


/*! Sends \a s as a positive OK response. */

void ManageSieve::ok( const EString &s )
{
    enqueue( "OK" );
    if ( !s.isEmpty() )
        enqueue( " " + s.quoted() );
    enqueue( "\r\n" );
}


/*! Sends \a s as a negative NO response. */

void ManageSieve::no( const EString &s )
{
    enqueue( "NO" );
    if ( !s.isEmpty() )
        enqueue( " " + s.quoted() );
    enqueue( "\r\n" );
    setReader( 0 );
}


/*! Sends the literal response \a s without adding a tag. */

void ManageSieve::send( const EString &s )
{
    enqueue( s );
    enqueue( "\r\n" );
}


/*! The ManageSieve server maintains a list of commands received from the
    client and processes them one at a time in the order they were
    received. This function executes the first command in the list,
    or if the first command has completed, removes it and executes
    the next one.

    It should be called when a new command has been created (i.e.,
    by ManageSieve::parse()) or when a running command finishes.

    Because the managesieve specification forbids executing any
    commands sent after logout, runCommands() must take special care
    to avoid that.
*/

void ManageSieve::runCommands()
{
    List< ManageSieveCommand >::Iterator it( d->commands );
    do {
        while ( it && it->done() )
            d->commands->take( it );
        if ( it && Connection::state() == Connected )
            it->notify();
    } while ( it && it->done() );
}


/*! Reserves the input stream to inhibit parsing if \a r is true. If
    \a r is false, then the server processes input as usual. Used by
    STLS to inhibit parsing.
*/

void ManageSieve::setReserved( bool r )
{
    d->reserved = r;
}


/*! Reserves the input stream for processing by \a cmd, which may be 0
    to indicate that the input should be processed as usual. Used by
    AUTH to parse non-command input.
*/

void ManageSieve::setReader( ManageSieveCommand * cmd )
{
    d->reader = cmd;
}


/*! Enqueues a suitably-formatted list of our capabilities. */

void ManageSieve::capabilities()
{
    // Avelsieve buggily demands that IMPLEMENTATION be the first
    // advertise extension.
    EString v( Configuration::compiledIn( Configuration::Version ) );
    enqueue( "\"IMPLEMENTATION\" \"Archiveopteryx " + v + "\"\r\n" );

    enqueue( "\"SIEVE\" " +
             SieveProduction::supportedExtensions()->join( " " ).quoted() +
             "\r\n" );
    enqueue( "\"SASL\" \"" + SaslMechanism::allowedMechanisms( "", hasTls() ) +
             "\"\r\n" );
    if ( Configuration::toggle( Configuration::UseTls ) && !hasTls() )
        enqueue( "\"STARTTLS\"\r\n" );
    enqueue( "\"X-AOX-EXPLAIN\"\r\n" );
}


/*! Enqueues the SASL challenge \a s on this connection.

    This version of the function avoids sending challenges as quoted
    strings, to please the KDE. The KDE behaviour is a clear bug, but
    unlikely to be fixed in KDE 3.x, maybe it won't be fixed at all.

    http://launchpad.net/bugs/228214
*/


void ManageSieve::sendChallenge( const EString & s )
{
    enqueue( "{" );
    enqueue( fn( s.length() ) );
    enqueue( "+}\r\n" );
    enqueue( s );
    enqueue( "\r\n" );
}
