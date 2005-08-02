// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#include "postgres.h"

#include "dict.h"
#include "list.h"
#include "string.h"
#include "buffer.h"
#include "allocator.h"
#include "configuration.h"
#include "transaction.h"
#include "stringlist.h"
#include "pgmessage.h"
#include "query.h"
#include "event.h"
#include "loop.h"
#include "md5.h"
#include "log.h"

// crypt
#define _XOPEN_SOURCE 4
#include <unistd.h>

// setreuid(), getpwnam()
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>


static bool hasMessage( Buffer * );


class PgData
    : public Garbage
{
public:
    PgData()
        : active( false ), startup( false ), authenticated( false ),
          unknownMessage( false ), identBreakageSeen( false ),
          sendingCopy( false ), keydata( 0 ), description( 0 ),
          transaction( 0 )
    {}

    bool active;
    bool startup;
    bool authenticated;
    bool unknownMessage;
    bool identBreakageSeen;
    bool sendingCopy;

    PgKeyData *keydata;
    Dict< String > params;
    PgRowDescription *description;
    Dict< int > prepared;

    List< Query > queries;
    Transaction *transaction;
};


/*! \class Postgres postgres.h
    Implements the PostgreSQL 3.0 Frontend-Backend protocol.

    This is our interface to PostgreSQL. As a subclass of Database, it
    accepts Query objects, sends queries to the database, and notifies
    callers about any resulting data. As a descendant of Connection, it
    is responsible for all network communications with the server.

    The network protocol is documented at <doc/src/sgml/protocol.sgml>
    and <http://www.postgresql.org/docs/current/static/protocol.html>.
    The version implemented here is used by PostgreSQL 7.4 and later.

    At the time of writing, there do not seem to be any other suitable
    PostgreSQL client libraries available. For example, libpqxx doesn't
    support asynchronous operation or prepared statements. Its interface
    would be difficult to wrap in a database-agnostic manner, and it
    depends on the untested libpq. The others aren't much better.
*/

/*! Creates a Postgres object, initiates a TCP connection to the server,
    registers with the main loop, and adds this Database to the list of
    available handles.
*/

Postgres::Postgres()
    : Database(), d( new PgData )
{
    log()->setFacility( Log::Database );
    log( "Connecting to PostgreSQL server at " + server().string(),
         Log::Debug );

    uint oryx = 0;
    if ( false && server().protocol() == Endpoint::Unix && !getuid() ) {
        // postgresql on suse and debian, and perhaps elsewhere, loves
        // to use SO_PEERCRED to check who's connecting.  since we
        // have to connect to a unix socket before we chroot(), and
        // have to chroot() before we can lose root privileges,
        // postgresql will probably reject us (unless some smart
        // sysadmin has fixed the configuration).
        //
        // fortunately, this postgresql security feature is easy to
        // defeat.  the code is a little ugly, that's all.

        String user( Configuration::text( Configuration::JailUser ) );
        struct passwd * pw = getpwnam( user.cstr() );
        if ( pw )
            oryx = pw->pw_uid;
    }

    if ( oryx ) {
        // to defeat postgresql's checking, we need to partly drop
        // privileges...
        setreuid( oryx, 0 );
        setBlocking( true );
        connect( server() );
        setBlocking( false );
        // ... but we can pick them up again afterwards
        setreuid( 0, 0 );
    }
    else {
        connect( server() );
    }
    setTimeoutAfter( 60 );
    Loop::addConnection( this );
    addHandle( this );
}


Postgres::~Postgres()
{
    Loop::removeConnection( this );
}


void Postgres::processQueue()
{
    Query *q;
    int n = 0;

    if ( d->sendingCopy )
        return;

    List< Query > *l = Database::queries;
    if ( d->transaction )
        l = d->transaction->queries();

    while ( ( q = l->firstElement() ) != 0 ) {
        if ( q->state() != Query::Submitted )
            break;

        l->shift();
        q->setState( Query::Executing );

        if ( !d->transaction && q->transaction() ) {
            d->transaction = q->transaction();
            d->transaction->setState( Transaction::Executing );
            d->transaction->setDatabase( this );
            l = d->transaction->queries();
        }

        d->queries.append( q );

        String s( "Sent " );
        if ( q->name() == "" ||
             !d->prepared.contains( q->name() ) )
        {
            PgParse a( q->string(), q->name() );
            a.enqueue( writeBuffer() );

            if ( q->name() != "" )
                d->prepared.insert( q->name(), 0 );
            s.append( "parse/" );
        }

        PgBind b( q->name() );
        b.bind( q->values() );
        b.enqueue( writeBuffer() );

        PgDescribe c;
        c.enqueue( writeBuffer() );

        PgExecute ex;
        ex.enqueue( writeBuffer() );

        PgSync e;
        e.enqueue( writeBuffer() );

        s.append( "execute for " );
        s.append( q->description() );
        log( s, Log::Debug );
        n++;

        if ( q->inputLines() ) {
            d->sendingCopy = true;
            break;
        }
    }

    if ( n > 0 ) {
        extendTimeout( 5 );
        write();
    }
}


void Postgres::react( Event e )
{
    switch ( e ) {
    case Connect:
        {
            PgStartup msg;
            msg.setOption( "user", user() );
            msg.setOption( "database", name() );
            msg.enqueue( writeBuffer() );

            d->active = true;
            d->startup = true;
        }
        break;

    case Read:
        while ( d->active && hasMessage( readBuffer() ) ) {
            /* We call a function to process every message we receive.
               This function is expected to parse and remove a message
               from the readBuffer, throwing an exception for malformed
               messages, and setting d->unknownMessage for messages that
               it can't or won't handle. */

            char msg = (*readBuffer())[0];
            try {
                if ( d->startup ) {
                    if ( !d->authenticated )
                        authentication( msg );
                    else
                        backendStartup( msg );
                }
                else {
                    process( msg );
                }

                if ( d->unknownMessage )
                    unknown( msg );
            }
            catch ( PgServerMessage::Error e ) {
                error( "Malformed '" + String( &msg, 1 ) +
                       "' message received." );
            }
        }
        break;

    case Error:
        error( "Couldn't connect to PostgreSQL." );
        break;

    case Close:
        error( "Connection terminated by the server." );
        break;

    case Timeout:
        if ( !d->active || d->startup )
            error( "Timeout negotiating connection to PostgreSQL." );
        else if ( d->queries.count() > 0 )
            error( "Request timeout." );
        else if ( numHandles() > 1 || server().protocol() != Endpoint::Unix )
            shutdown();
        break;

    case Shutdown:
        shutdown();
        break;
    }
}


/*! This function handles the authentication phase of the protocol. It
    expects and responds to an authentication request, and waits for a
    positive response before entering the backend startup phase. It is
    called by react with the \a type of the message to process.
*/

void Postgres::authentication( char type )
{
    switch ( type ) {
    case 'R':
        {
            PgAuthRequest r( readBuffer() );

            switch ( r.type() ) {
            case PgAuthRequest::Success:
                d->authenticated = true;
                break;

            case PgAuthRequest::Password:
            case PgAuthRequest::Crypt:
            case PgAuthRequest::MD5:
                {
                    String pass = password();

                    if ( r.type() == PgAuthRequest::Crypt )
                        pass = ::crypt( pass.cstr(), r.salt().cstr() );
                    else if ( r.type() == PgAuthRequest::MD5 )
                        pass = "md5" + MD5::hash(
                                           MD5::hash(
                                               pass + user()
                                           ).hex() + r.salt()
                                       ).hex();

                    PgPasswordMessage p( pass );
                    p.enqueue( writeBuffer() );
                }
                break;

            default:
                error( "Unsupported PgAuthRequest." );
                break;
            }
        }
        break;

    default:
        d->unknownMessage = true;
        break;
    }
}


/*! This function negotiates the backend startup phase of the protocol
    (storing any messages the server sends us), concluding the startup
    process when the server indicates that it is ready for queries. It
    is called by react() with the \a type of the message to process.
*/

void Postgres::backendStartup( char type )
{
    switch ( type ) {
    case 'Z':
        setTimeout( 0 );
        d->startup = false;

        // This successfully concludes connection startup. We'll leave
        // this message unparsed, so that process() can handle it like
        // any other PgReady.

        commit();
        break;

    case 'K':
        d->keydata = new PgKeyData( readBuffer() );
        break;

    default:
        d->unknownMessage = true;
        break;
    }
}


/*! This function handles interaction with the server once the startup
    phase is complete. It is called by react() with the \a type of the
    message to process.
*/

void Postgres::process( char type )
{
    Query *q = d->queries.firstElement();

    extendTimeout( 5 );

    switch ( type ) {
    case '1':
        {
            PgParseComplete msg( readBuffer() );
        }
        break;

    case '2':
        {
            PgBindComplete msg( readBuffer() );
        }
        break;

    case 'n':
        {
            PgNoData msg( readBuffer() );
        }
        break;

    case 't':
        (void)new PgParameterDescription( readBuffer() );
        break;

    case 'G':
        {
            PgCopyInResponse msg( readBuffer() );
            if ( q && q->inputLines() ) {
                PgCopyData cd( q );
                PgCopyDone e;

                cd.enqueue( writeBuffer() );
                e.enqueue( writeBuffer() );
            }
            else {
                PgCopyFail f;
                f.enqueue( writeBuffer() );
            }

            PgSync s;
            s.enqueue( writeBuffer() );
            d->sendingCopy = false;
            processQueue();
        }
        break;

    case 'T':
        d->description = new PgRowDescription( readBuffer() );
        break;

    case 'D':
        {
            if ( !q || !d->description ) {
                error( "Unexpected data row" );
                return;
            }

            // We could suppress this notification if we could somehow
            // infer that we will receive a completion message soon.

            PgDataRow msg( readBuffer(), d->description );
            q->addRow( msg.row() );
            q->notify();
        }
        break;

    case 'I':
    case 'C':
        {
            if ( type == 'C' )
                PgCommandComplete msg( readBuffer() );
            else
                PgEmptyQueryResponse msg( readBuffer() );

            if ( q ) {
                String s;
                s.append( "Dequeueing query " );
                s.append( q->description() );
                if ( q->rows() > 0 ) {
                    s.append( " (with " );
                    s.append( fn( q->rows() ) );
                    s.append( " rows)" );
                }
                log( s, Log::Debug );
                if ( !q->done() )
                    q->setState( Query::Completed );
                q->notify();
                d->queries.shift();
            }
        }
        break;

    case 'Z':
        {
            PgReady msg( readBuffer() );

            if ( state() == InTransaction ||
                 state() == FailedTransaction )
            {
                if ( msg.state() == FailedTransaction ) {
                    d->transaction->setState( Transaction::Failed );
                }
                else if ( msg.state() == Idle ) {
                    if ( !d->transaction->failed() )
                        d->transaction->setState( Transaction::Completed );
                    d->transaction->notify();
                    d->transaction = 0;
                }
            }

            setState( msg.state() );

            processQueue();
            if ( d->queries.isEmpty() ) {
                uint interval =
                    Configuration::scalar( Configuration::DbHandleInterval );
                setTimeoutAfter( interval );
            }
        }
        commit();
        break;

    default:
        d->unknownMessage = true;
        break;
    }
}


/*! This function handles unknown or unwanted messages that some other
    function declined to process (by setting d->unknownMessage). It is
    called by react() with the \a type of the unknown message.
*/

void Postgres::unknown( char type )
{
    String s;

    switch ( type ) {
    case 'S':
        {
            d->unknownMessage = false;
            PgParameterStatus msg( readBuffer() );
            d->params.insert( msg.name(), new String( msg.value() ) );
        }
        break;

    case 'N':
    case 'E':
        {
            d->unknownMessage = false;
            PgMessage msg( readBuffer() );
            Query *q = d->queries.firstElement();

            switch ( msg.severity() ) {
            case PgMessage::Panic:
            case PgMessage::Fatal:
                // special-case IDENT query failures since they can be
                // so off-putting to novices.
                if ( msg.message().startsWith( "IDENT authentication "
                                               "failed for user \"") ) {
                    String s = msg.message();
                    int b = s.find( '"' );
                    int e = s.find( '"', b+1 );
                    s = s.mid( b+1, e-b-1 ); // rest-of-string if e==-1 ;)
                    if ( s == Configuration::text(Configuration::JailUser) &&
                         self().protocol() != Endpoint::Unix &&
                         Configuration::toggle( Configuration::Security ) &&
                         !d->identBreakageSeen ) {
                        // If we connected via ipv4 or ipv6, and we
                        // did it so early that postgres had a chance
                        // to reject us, we can try again. We do that
                        // only once, and only if we believe it may
                        // succeed.
                        d->identBreakageSeen = true;
                        log( "PostgreSQL demanded IDENT, "
                             "which did not match during startup. Retrying.",
                             Log::Info );
                        Endpoint pg( peer() );
                        close();
                        connect( pg );
                    }
                    else {
                        log( "PostgreSQL refuses authentication because this "
                             "process is not running as user " + s,
                             Log::Disaster );
                        log( "See "
                             "http://www.oryx.com/faq/mailstore.html#ident",
                             Log::Info );
                    }
                }
                else {
                    error( msg.message() );
                }
                break;

            case PgMessage::Error:
            case PgMessage::Warning:
                if ( msg.severity() == PgMessage::Warning )
                    s.append( "WARNING: " );
                else
                    s.append( "ERROR: " );

                if ( q )
                    s.append( "Query " + q->description() + ": " );

                s.append( msg.message() );
                if ( msg.detail() != "" )
                    s.append( " (" + msg.detail() + ")" );

                if ( !q || !q->canFail() )
                    log( s, Log::Error );

                // Has the current query failed?
                if ( q && msg.severity() == PgMessage::Error ) {
                    d->queries.shift();
                    q->setError( msg.message() );
                    q->notify();
                }
                break;

            default:
                log( msg.message(), Log::Debug );
                break;
            }
        }
        break;

    default:
        {
            String err = "Unexpected message (";

            if ( type > 32 && type < 127 )
                err.append( type );
            else
                err.append( "%" + fn( (int)type, 16 ) );

            err.append( ") received" );
            if ( d->startup ) {
                if ( !d->authenticated )
                    err.append( " during authentication" );
                else
                    err.append( " during backend startup" );
            }
            err.append( "." );
            error( err );
        }
        break;
    }
}


/*! Handles all protocol/socket errors by logging the error message \a s
    and closing the connection after flushing the write buffer and
    notifying any pending queries of the failure.
*/

void Postgres::error( const String &s )
{
    d->active = false;
    log( s, Log::Error );

    List< Query >::Iterator q( d->queries );
    while ( q ) {
        q->setError( s );
        q->notify();
        ++q;
    }

    writeBuffer()->remove( writeBuffer()->size() );
    writeBuffer()->remove( writeBuffer()->size() );
    Connection::setState( Closing );
    removeHandle( this );
}


/*! Sends a termination message and takes this database handle out of
    circulation gracefully.
*/

void Postgres::shutdown()
{
    PgTerminate msg;
    msg.enqueue( writeBuffer() );
    removeHandle( this );

    d->active = false;
}


static bool hasMessage( Buffer *b )
{
    if ( b->size() < 5 ||
         b->size() < 1+( (uint)((*b)[1]<<24)|((*b)[2]<<16)|
                               ((*b)[3]<<8)|((*b)[4]) ) )
        return false;
    return true;
}
