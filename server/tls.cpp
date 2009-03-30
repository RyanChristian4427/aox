// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#include "tls.h"

#include "allocator.h"
#include "connection.h"
#include "string.h"
#include "configuration.h"
#include "buffer.h"
#include "event.h"
#include "log.h"
#include "eventloop.h"


static Endpoint * tlsProxy = 0;


class TlsServerData
    : public Garbage
{
public:
    TlsServerData()
        : handler( 0 ),
          userside( 0 ), serverside( 0 ),
          done( false ), ok( false ), connected( false ) {}

    EventHandler * handler;

    class Client: public Connection
    {
    public:
        Client( TlsServerData * );
        void react( Event );

        class TlsServerData * d;

        String tag;
        bool done;
        bool connected;
    };

    Client * userside;
    Client * serverside;

    Endpoint client;
    String protocol;

    bool done;
    bool ok;
    bool connected;
};


TlsServerData::Client::Client( TlsServerData * data )
    : Connection(),
      d( data ), done( false ), connected( false )
{
    setTimeoutAfter( 10 );
    connect( *tlsProxy );
    EventLoop::global()->addConnection( this );
}


void TlsServerData::Client::react( Event e )
{
    if ( e == Read ) {
        if ( !connected )
            log( "TlsServer: Read before Connect? Strange" );
        connected = true;
    }
    else if ( e == Connect ) {
        connected = true;
        return;
    }
    else if ( e != Read ) {
        if ( e == Error && !connected ) {
            log( "TlsServer: Error while connecting to tlsproxy. "
                 "Shutting down TLS.",
                 Log::Significant );
            ::tlsAvailable = false;
            EventLoop::global()->shutdownSSL();
        }
        else {
            log( "TlsServer: Unexpected event of type " + fn( e ), Log::Error );
        }
        d->done = true;
        d->handler->execute();
        d->serverside->close();
        d->userside->close();
        EventLoop::global()->removeConnection( d->serverside );
        EventLoop::global()->removeConnection( d->userside );
        return;
    }

    String * s = readBuffer()->removeLine();
    if ( !s )
        return;

    done = true;

    String l = s->simplified();
    if ( l.startsWith( "tlsproxy " ) ) {
        tag = l.mid( 9 );
        connected = true;
        if ( !d->serverside->connected || !d->userside->connected )
            return;

        d->userside->enqueue( d->serverside->tag + " " +
                              d->protocol + " " +
                              d->client.address() + " " +
                              fn( d->client.port() ) +
                              "\r\n" );
    }
    else if ( l == "ok" ) {
        d->done = true;
        d->ok = true;
        d->handler->execute();
    }
}


/*! \class TlsServer tls.h
  The TlsServer class provides an interface to server-side TLS.

  On construction, it connects to a TlsProxy, and eventually verifies
  that the proxy is available to work as a server. Once its
  availability has been probed, done() returns true and ok() returns
  either a meaningful result.
*/


/*! Constructs a TlsServer and starts setting up the proxy server. It
    returns quickly, and later notifies \a handler when setup has
    completed. In the log files, the TlsServer will refer to \a client
    as client using \a protocol.

    If use-tls is set to false in the configuration, this TlsServer
    object will be done() and not ok() immediately after construction.
    It will not call its owner back in that case.
*/

TlsServer::TlsServer( EventHandler * handler, const Endpoint & client,
                      const String & protocol )
    : d( new TlsServerData )
{
    d->handler = handler;
    d->protocol = protocol;
    d->client = client;

    if ( Configuration::toggle( Configuration::UseTls ) ) {
        d->serverside = new TlsServerData::Client( d );
        d->userside = new TlsServerData::Client( d );
    }
    else {
        d->done = true;
        d->ok = false;
    }
}


/*! Returns true if setup has finished, and false if it's still going on. */

bool TlsServer::done() const
{
    return d->done;
}


static bool tlsAvailable;


/*! Returns true if the TLS proxy is available for use, and false is
    an error happened or setup is still going on.
*/

bool TlsServer::ok() const
{
    if ( !d->done )
        return false;
    if ( d->ok )
        return true;
    return false;
}


/*! Initializes the TLS subsystem. */

void TlsServer::setup()
{
    ::tlsAvailable = Configuration::toggle( Configuration::UseTls );
    if ( !tlsAvailable )
        return;

    Endpoint * e = new Endpoint( Configuration::TlsProxyAddress,
                                 Configuration::TlsProxyPort );
    if ( !e->valid() ) {
        ::log( "TLS Support disabled" );
        return;
    }
    ::tlsAvailable = true;
    ::tlsProxy = e;
    Allocator::addEternal( ::tlsProxy, "tls proxy name" );
}


/*! Returns true if the server is convigured to support TLS, and false
    if it isn't, or if there's something wrong about the configuration.
*/

bool TlsServer::available()
{
    return ::tlsAvailable;
}


/*! Returns the Configuration to be used for the server (plaintext) side. */

Connection * TlsServer::serverSide() const
{
    return d->serverside;
}


/*! Returns the Connection to be used for the user (encrypted) side. */

Connection * TlsServer::userSide() const
{
    return d->userside;
}
