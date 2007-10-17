// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#include "http.h"

#include "dict.h"
#include "link.h"
#include "codec.h"
#include "buffer.h"
#include "eventloop.h"
#include "httpsession.h"
#include "configuration.h"
#include "stringlist.h"
#include "webpage.h"
#include "scope.h"
#include "log.h"
#include "tls.h"


class HTTPData
    : public Garbage
{
public:
    HTTPData()
        : state( HTTP::Request ),
          status( 200 ),
          use11( false ), sendContents( true ), acceptsHtml( true ),
          acceptsPng( true ), acceptsLatin1( true ), acceptsUtf8( true ),
          acceptsIdentity( false ), connectionClose( true ),
          contentLength( 0 ),
          link( 0 ), session( 0 ), requestLog( 0 )
    {}

    HTTP::State state;

    uint status;
    String method;
    String message;
    String hostSupplied;

    bool use11;
    bool sendContents;
    bool acceptsHtml;
    bool acceptsPng;
    bool acceptsLatin1;
    bool acceptsUtf8;
    bool acceptsIdentity;
    bool connectionClose;

    String body;
    String path;
    String referer;
    StringList headers;
    StringList ignored;
    uint contentLength;
    Dict< UString > parameters;

    Codec * preferredCodec;
    uint codecQuality;

    Link *link;
    HttpSession *session;
    Log * requestLog;

    struct HeaderListItem
        : public Garbage
    {
        HeaderListItem(): q( 0 ) {}

        String n;
        uint q; // q*100
    };

};


/*! \class HTTP http.h

    The HTTP class is a HTTP 1.1 server, with a number of umimportant
    deficiencies. It parses incoming requests almost according to the
    protocol rules and hands out simple answers. The main problem is
    that it doesn't handle conditions at all (so far) and that it
    doesn't handle exclusions (e.g. clients saying "I accept all
    formats except image/tiff").
*/


/*! Constructs an HTTP server for file descriptor \a s. */

HTTP::HTTP( int s )
    : Connection( s, Connection::HttpServer ), d( new HTTPData )
{
    clear();
    EventLoop::global()->addConnection( this );
    setTimeoutAfter( 1800 );
}


void HTTP::react( Event e )
{
    switch ( e ) {
    case Read:
        process();
        break;

    case Timeout:
        log( "Idle timeout" );
        Connection::setState( Closing );
        break;

    case Connect:
    case Error:
    case Close:
        break;

    case Shutdown:
        enqueue( "505 Server must shut down\r\n" );
        break;
    }
}


/*! This function, which is called whenever the HTTP server might want
    to do something, decides what to do based on the server's state().

    Our request parsing is somewhat simpler than described in
    http://www.and.org/texts/server-http and RFC 2616.
*/

void HTTP::process()
{
    if ( d->state == Request ) {
        if ( canReadHTTPLine() )
            parseRequest( line().simplified() );
    }

    Scope s;
    if ( d->requestLog )
        s.setLog( d->requestLog );

    while ( d->state == Header && canReadHTTPLine() )
        parseHeader( line() );

    if ( d->state == Body ) {
        Buffer *r = readBuffer();

        if ( d->contentLength <= r->size() ) {
            String s = r->string( d->contentLength );
            r->remove( d->contentLength );
            if ( d->method == "POST" )
                d->body = s;
            if ( d->contentLength != 0 )
                ::log( "Received request-body of " + fn( d->contentLength ) +
                       " bytes for " + d->method, Log::Debug );
            d->state = Parsed;
            parseParameters();
        }
    }

    if ( d->state != Parsed )
        return;

    if ( d->link )
        d->link->webPage()->execute();
    else
        respond( "text/plain", "Goodbye.\n" );
}


/*! Sends \a response as a response of type \a type. */

void HTTP::respond( const String & type, const String & response )
{
    String srv( "Server: Archiveopteryx/" );
    srv.append( Configuration::compiledIn( Configuration::Version ) );
    srv.append( " (http://www.archiveopteryx.org)" );

    addHeader( srv );
    addHeader( "Content-Length: " + fn( response.length() ) );
    addHeader( "Content-Type: " + type );

    if ( d->session )
        addHeader( "Set-Cookie: session=\"" + d->session->key() + "\";"
                   "path=/" );

    if ( d->connectionClose )
        addHeader( "Connection: close" );

    if ( d->use11 )
        enqueue( "HTTP/1.1 " );
    else
        enqueue( "HTTP/1.0 " );

    enqueue( fn( d->status ) + " " + d->message + "\r\n" );
    enqueue( d->headers.join( "\r\n" ) );
    enqueue( "\r\n\r\n" );

    if ( d->sendContents ) {
        enqueue( response );
        write();
    }

    ::log( "Sent '" + fn( d->status ) + "/" + d->message + "' response "
           "of " + fn( response.length() ) + " bytes." );

    d->requestLog = 0;

    if ( d->connectionClose ) {
        setState( Closing );
        log( "Closing connection" );
    }
    else {
        log( "Keeping connection alive for 1800 more seconds." );
        setTimeoutAfter( 1800 );
    }

    clear();
}


/*! Returns the HTTP parser's current state. The state changes after
    parsing a byte, so the return value is bound to next incoming
    byte, not the last one.
*/

HTTP::State HTTP::state() const
{
    return d->state;
}


/*! Returns a pointer to the HttpSession object associated with this
    server, or 0 if no such session exists.
*/

HttpSession *HTTP::session() const
{
    return d->session;
}


/*! Sets this HTTP server's session to \a s.
*/

void HTTP::setSession( HttpSession *s )
{
    d->session = s;
}


/*! Returns a pointer to the user associated with this server, or 0 if
    there is no such user. (For archive mailboxes, 0 is usually
    returned.)
*/

User *HTTP::user() const
{
    if ( d->session && !d->session->expired() )
        return d->session->user();
    return 0;
}


/*! Returns a string containing the request-body, if any, supplied with
    the request. If there was none (or none was permitted), this string
    is empty.
*/

String HTTP::body() const
{
    return d->body;
}


/*! Returns the status code that the next response will use.
*/

uint HTTP::status() const
{
    return d->status;
}


/*! Returns the String value of the parameter named \a s,
    or an empty string if the parameter was not specified in the
    request.
*/

UString HTTP::parameter( const String & s ) const
{
    UString * v = d->parameters.find( s );
    if ( v )
    	return *v;
    UString empty;
    return empty;
}


static uint inputLength( Buffer * r )
{
    uint i = 0;
    while ( i < r->size() &&
            ( (*r)[i] != '\n' ||
              ( (*r)[i] == '\n' &&
                ( (*r)[i+1] == '\t' || (*r)[i+1] == ' ' ) ) ) )
        i++;
    if ( i < r->size() )
        return i;
    return UINT_MAX;
}


/*! Returns true if the readBuffer() contains a complete HTTP/1.1
    request or header line, taking account of escaped line feeds.
*/

bool HTTP::canReadHTTPLine() const
{
    Buffer * r = readBuffer();
    uint i = inputLength( r );
    if ( i < r->size() )
        return true;
    return false;
}


/*! Reads, removes and returns a line, including escaped line
    feeds. The trailing LF or CRLF is removed from the input stream,
    but not returned.
*/

String HTTP::line()
{
    Buffer * r = readBuffer();
    uint i = inputLength( r );
    String l;
    if ( i >= r->size() )
        return l;
    l = r->string( i );
    if ( l.endsWith( "\r" ) )
        l.truncate( l.length() - 1 );
    r->remove( i+1 ); // eat the LF too
    return l;
}



/*! Parses the original GET/HEAD request line \a l. */

void HTTP::parseRequest( String l )
{
    setTimeoutAfter( 1800 );
    d->state = Header;
    Scope s( Connection::log() );
    d->requestLog = new Log( Log::HTTP );
    s.setLog( d->requestLog );
    ::log( "Request: " + l, Log::Debug );
    int space = l.find( ' ' );
    if ( space < 0 ) {
        setStatus( 400, "Complete and utter parse error" );
        return;
    }

    String request = l.mid( 0, space );
    l = l.mid( space+1 );
    space = l.find( ' ' );
    if ( space < 0 ) {
        setStatus( 400, "Really total parse error" );
        return;
    }
    d->method = request;
    if ( request == "HEAD" ) {
        d->sendContents = false;
    }
    else if ( request == "GET" ) {
        d->sendContents = true;
    }
    else if ( request == "POST" ) {
        d->sendContents = true;
    }
    else {
       setStatus( 405, "Bad Request: " + request );
        addHeader( "Allow: GET, HEAD, POST" );
        return;
    }

    String path = l.mid( 0, space );
    l = l.mid( space+1 );

    // this is where String::stripWSP() would come in handy ;)
    space = l.find( ' ' );
    while ( space >= 0 ) {
        l = l.mid( 0, space ) + l.mid( space+1 );
        space = l.find( ' ' );
    }
    String protocol = l;

    if ( !protocol.startsWith( "HTTP/" ) ) {
       setStatus( 400,
                  "Bad protocol: " + protocol + ". Only HTTP supported." );
        return;
    }
    bool ok = false;
    uint dot = 5;
    while ( dot < protocol.length() && protocol[dot] != '.' )
        dot++;
    if ( protocol[dot] != '.' ) {
       setStatus( 400, "Bad version number: " + protocol.mid( 6 ) );
        return;
    }
    uint major = protocol.mid( 5, dot-5 ).number( &ok );
    uint minor = 0;
    if ( ok )
        minor = protocol.mid( dot+1 ).number( &ok );
    if ( major != 1 ) {
        setStatus( 400, "Only HTTP/1.0 and 1.1 are supported" );
        return;
    }
    if ( minor > 0 ) {
        d->use11 = true;
        d->connectionClose = false;
    }

    uint i = 0;
    d->path.truncate( 0 );
    while ( i < path.length() ) {
        if ( path[i] == '%' ) {
            bool ok = false;
            uint num = path.mid( i+1, 2 ).number( &ok, 16 );
            if ( !ok || path.length() < i + 3 ) {
                setStatus( 400, "Bad percent escape: " + path.mid( i, 3 ) );
                return;
            }
            d->path.append( (char)num );
            i += 3;
        }
        else {
            d->path.append( path[i] );
            i++;
        }
    }

    d->link = new Link( d->path, this );
    ::log( "Received: " + d->method + " " + d->path + " " + protocol );
}


/*! Parses a single HTTP header in the String \a h and stores its
    contents appropriately. May step on to the Done state.
*/

void HTTP::parseHeader( const String & h )
{
    if ( h.isEmpty() ) {
        d->state = Body;
        return;
    }

    uint i = h.find( ':' );
    if ( i < 1 ) {
       setStatus( 400, "Bad header: " + h.simplified() );
        return;
    }
    String n = h.mid( 0, i ).simplified().headerCased();
    String v = h.mid( i+1 ).simplified();

    ::log( "Received: '" + n + "' = '" + v + "'", Log::Debug );

    if ( n == "Accept" ) {
        d->acceptsHtml = false;
        d->acceptsPng = false;
        parseList( n, v );
    }
    else if ( n == "Accept-Charset" ) {
        d->acceptsLatin1 = true; // XXX: wrong.
        d->acceptsUtf8 = false;
        parseList( n, v );
    }
    else if ( n == "Accept-Encoding" ) {
        parseList( n, v );
    }
    else if ( n == "Connection" ) {
        parseConnection( v );
    }
    else if ( n == "Cookie" ) {
        parseList( n, v );
    }
    else if ( n == "Expect" ) {
       setStatus( 417, "Expectations not supported" );
    }
    else if ( n == "Host" ) {
        parseHost( v );
    }
    else if ( n == "If-Match" ) {
        parseIfMatch( v );
    }
    else if ( n == "If-Modified-Since" ) {
        parseIfModifiedSince( v );
    }
    else if ( n == "If-None-Match" ) {
        parseIfNoneMatch( v );
    }
    else if ( n == "If-Unmodified-Since" ) {
        parseIfUnmodifiedSince( v );
    }
    else if ( n == "Referer" ) {
        parseReferer( v );
    }
    else if ( n == "Transfer-Encoding" ) {
        parseTransferEncoding( v );
    }
    else if ( n == "User-Agent" ) {
        parseUserAgent( v );
    }
    else if ( n == "Content-Length" ) {
        parseContentLength( v );
    }
    else {
        d->ignored.append( n );
    }
}


/*! Records \a status and \a message as the status line to be sent,
    unless another non-200 message has already been set.
*/

void HTTP::setStatus( uint status, const String &message )
{
    if ( d->status && d->status != status )
        ::log( "Status changed to " + fn( status ) + "/" + message );
    if ( d->status == 200 ) {
        d->status = status;
        d->message = message;
        d->state = Parsed;
    }
}


/*! Clears the object so it's ready to parse a new request. */

void HTTP::clear()
{
    d->link = 0;
    d->body = 0;
    d->state = Request;
    d->contentLength = 0;
    d->status = 200;
    d->session = 0;
    d->message = "OK";
    d->headers.clear();
}


/*! Parses \a type as an "Accept" header item with quality \a q. Not
    quite compliant, since we don't handle exclusions using
    wildcards. */

void HTTP::parseAccept( const String & type, uint q )
{
    int i = type.find( '/' );
    if ( i < 0 )
        i = type.length();
    String major = type.mid( 0, i ).simplified().lower();
    int j = type.find( ';', i+1 );
    if ( j < 0 )
        j = type.length();
    String minor = type.mid( i, j-i ).simplified().lower();
    if ( !q ) {
        // if q is 0, we ignore this type.
    }
    else if ( major == "*" ) {
        d->acceptsHtml = true;
        d->acceptsPng = true;
    }
    else if ( major == "text" ) {
        if ( minor == "*" || minor == "html" )
            d->acceptsHtml = true;
    }
    else if ( major == "image" ) {
        if ( minor == "*" || minor == "png" )
            d->acceptsPng = true;
    }
}


/*! Records \a cs as an "Accept-Charset" lite item with quality \a
    q. We're not compliant: We record whether we can send unicode, and
    look for a highest-quality other charset, that's it. We don't
    support things like "Accept-Encoding: *; q=0.8, utf-8; q=0" to
    mean anything except unicode.
*/

void HTTP::parseAcceptCharset( const String & cs, uint q )
{
    if ( cs == "*" && q ) {
        d->acceptsUtf8 = true;
        return;
    }
    Codec * c = Codec::byName( cs );
    if ( !c || q <= d->codecQuality )
        return;
    d->codecQuality = q;
    d->preferredCodec = c;
}


/*! Parses \a encoding as an "Accept-Encoding" lite item with quality
    \a q. As for Accept-Charset, we don't support exclusions.

    For now, only Identity is supported. Anyone wanting compression
    have to use TLS.
*/

void HTTP::parseAcceptEncoding( const String & encoding, uint q )
{
    if ( q && ( encoding == "identity" || encoding == "*" ) )
        d->acceptsIdentity = true;
}


/*! Parses the "Connection" header \a v and records the parsed
    information.
*/

void HTTP::parseConnection( const String & v )
{
    String l = " " + v.lower() + " ";
    if ( v.contains( " close " ) )
        d->connectionClose = true;
}


/*! Parses the "Host" header \a v and records the parsed information.
*/

void HTTP::parseHost( const String & v )
{
    d->hostSupplied = v.lower();
    if ( d->hostSupplied.endsWith( "." ) )
        d->hostSupplied.truncate( d->hostSupplied.length() - 1 );
    uint i = 0;
    uint c = d->hostSupplied.length();
    while ( i < d->hostSupplied.length() ) {
        bool bad = false;
        if ( d->hostSupplied[i] == '.' ) {
            if ( i == 0 || i == d->hostSupplied.length() - 1 )
                bad = true;
            if ( d->hostSupplied[i+1] == '.' )
                bad = true;
        }
        else if ( d->hostSupplied[i] == ':' ) {
            if ( i == 0 || i == d->hostSupplied.length() - 1 )
                bad = true;
            c = i;
        }
        else if ( d->hostSupplied[i] == '[' ) {
            if ( i > 0 )
                bad = true;
        }
        else if ( d->hostSupplied[i] == ']' ) {
            if ( i < d->hostSupplied.length() - 1 )
                bad = true;
        }
        else if ( ( d->hostSupplied[i] >= 'a' && d->hostSupplied[i] <= 'z' ) ||
                  ( d->hostSupplied[i] >= '0' && d->hostSupplied[i] <= '9' ) ||
                  d->hostSupplied[i] == '/' ||
                  d->hostSupplied[i] == '-' ) {
            // ok
        }
        else {
            bad = true;
        }
        if ( bad ) {
            setStatus( 400, "Bad Host header: " + d->hostSupplied );
            return;
        }
        i++;
    }
    if ( c < d->hostSupplied.length() ) {
        bool ok = false;
        uint port = d->hostSupplied.mid( c + 1 ).number( &ok );
        if ( ok && port == self().port() )
            d->hostSupplied.truncate( c );
    }
    
    if ( Configuration::toggle( Configuration::AcceptAnyHttpHost ) )
        return;

    String correct = Configuration::text( Configuration::Hostname ).lower();
    if ( d->hostSupplied == correct )
        return;
   setStatus( 400, "No such host: " + d->hostSupplied +
                 ". Only " + correct + " allowed" );
}


/*! Parses the "If-Match" header and records the parsed information. */

void HTTP::parseIfMatch( const String & )
{
    // later
}


/*! Parses the "If-Modified-Since" header and records the parsed
    information.
*/

void HTTP::parseIfModifiedSince( const String & )
{
    // later
}


/*! Parses the "If-None-Match" header and records the parsed information. */

void HTTP::parseIfNoneMatch( const String & )
{
    // later
}


/*! Parses the "If-Unmodified-Since" header and records the parsed
    information.
*/

void HTTP::parseIfUnmodifiedSince( const String & )
{
    // later
}


/*! Parses the "Referer" header \a v and records the parsed information.
*/

void HTTP::parseReferer( const String & v )
{
    d->referer = v;
}


/*! Parses the "Transfer-Encoding" header and records the parsed
    information.
*/

void HTTP::parseTransferEncoding( const String & )
{

}


/*! Parses the "User-Agent" header and records the parsed information. */

void HTTP::parseUserAgent( const String & )
{
    // we ignore user-agent. thoroughly.
}


/*! Parses a single component of the Cookie header \a s. */

void HTTP::parseCookie( const String &s )
{
    int eq = s.find( '=' );
    if ( eq > 0 ) {
        String name = s.mid( 0, eq ).stripWSP().lower();
        String value = s.mid( eq+1 ).stripWSP();

        if ( name == "session" &&
             ( !d->session || d->session->expired() ) )
            d->session = HttpSession::find( value.unquoted() );
    }
}


/*! Parses the Content-Length header \a s. */

void HTTP::parseContentLength( const String &s )
{
    d->contentLength = s.number( 0 );
}


/*! Records \a s as a reply header to be sent. \a s should not contain
    CR or LF.
*/

void HTTP::addHeader( const String & s )
{
    d->headers.append( s );
}


/*! Parses \a value as a list header named \a name, and calls
    parseAccept() et al for each individual item.
*/

void HTTP::parseList( const String & name, const String & value )
{
    uint i = 0;
    while ( i < value.length() ) {
        uint start = i;
        while ( isTokenChar( value[i] ) ||
                value[i] == '/' || value[i] == '=' || value[i] == '"' )
            i++;
        String item = value.mid( start, i-start );
        uint q = 1000;
        skipValues( value, i, q );
        if ( i >= value.length() ) {
            parseListItem( name, item, q );
        }
        else if ( value[i] != ',' ) {
            setStatus( 400, "Expected comma at header " + name + " position " +
                       fn( i ) + ", saw " + value.mid( i ) );
            return;
        }
        else {
            i++;
            while ( value[i] == ' ' )
                i++;
            parseListItem( name, item, q );
        }
    }
}


/*! Parses the single list \a item as belonging to \a header. If
    there's a quality level, \a q represents the quality, if not, \a q
    is 1000.

*/

void HTTP::parseListItem( const String & header, const String & item, uint q )
{
    if ( header == "Accept" )
        parseAccept( item, q );
    else if ( header == "Accept-Charset" )
        parseAcceptCharset( item, q );
    else if ( header == "Cookie" )
        parseCookie( item );
}


/*! Skips past all arguments in \a value starting at \a i, moving \a i
    along. If one of the arguments is named q and has a legal value,
    \a q is changed.

    \a i is assumed to point to ';' on entry, and is left on ',' or at
    end of header on exit. If \a i already points to ',' or EOH,
    skipValues() is a noop.
*/

void HTTP::skipValues( const String & value, uint & i, uint & q )
{
    bool hasSeenQ = false;
    while ( true ) {
        while ( value[i] == ' ' )
            i++;
        if ( i >= value.length() || value[i] == ',' )
            return;
        expect( value, i, ';' );
        uint n = i;
        while ( isTokenChar( value[i] ) )
            i++;
        bool isQ = false;
        if ( value.mid( n, i-n ) == "q" && !hasSeenQ )
            isQ = true;
        expect( value, i, '=' );
        if ( value[i] == '"' ) {
            i++;
            while ( i < value.length() && value[i] != '"' ) {
                if ( value[i] == '\\' )
                    i++;
                i++;
            }
            expect( value, i, '"' );
            if ( isQ )
               setStatus( 400, "q cannot be quoted" );
        }
        else {
            n = i;
            if ( isQ ) {
                hasSeenQ = true;
                if ( value[i] >= '0' && value[i] <= '1' ) {
                    q = 1000 * ( value[i] - '0' );
                    i++;
                    if ( value[i] == '.' ) {
                        i++;
                        uint n = i;
                        while ( value[i] >= '0' && value[i] <= '9' )
                            i++;
                        String decimals = value.mid( n, i-n ) + "000";
                        bool ok;
                        q = q + decimals.mid( 0, 3 ).number( &ok );
                        if ( q > 1000 )
                           setStatus( 400, "Quality can be at most 1.000" );
                    }
                }
                else {
                   setStatus( 400, "Could not parse quality value: " +
                            value.mid( i ) );
                }
            }
            else {
                while ( isTokenChar( value[i] ) )
                    i++;
            }
        }
        while ( value[i] == ' ' )
            i++;
    }
}


/*! Checks that \a value has (optional) whitespace followed by \a c at
    position \a i, and reports an error if not. Advances \a i by one
    and skips past trailing whitespace.
*/

void HTTP::expect( const String & value, uint & i, char c )
{
    while ( value[i] == ' ' )
        i++;
    if ( value[i] != c ) {
        String e( "Expected '" );
        e.append( c );
        e.append( "' at position " +
                  fn( i ) +
                  ", saw " +
                  value.mid( i ) );
       setStatus( 400, e );
    }
    i++;
    while ( value[i] == ' ' )
        i++;
}


/*! Returns true if \a c is a HTTP/1.1 token char, and false if it is
    not. Notably, nulls aren't token chars.
*/

bool HTTP::isTokenChar( char c )
{
    if ( c < 32 || c > 126 )
        return false;
    if ( c == '(' | c == ')' | c == '<' | c == '>' | c == '@'
         | c == ',' | c == ';' | c == ':' | c == '\\' | c == '\''
         | c == '/' | c == '[' | c == ']' | c == '?' | c == '='
         | c == '{' | c == '}' | c == ' ' )
        return false;
    return true;
}


/*! This function parses parameter values specified in the request, so
    as to make them available for later use through parameter().

    Currently, we consider parameter data supplied in the request-body
    of POST requests, but not in the request URI ("?foo=bar").
*/

void HTTP::parseParameters()
{
    StringList *p = StringList::split( '&', d->body );
    StringList::Iterator it( p );

    while ( it ) {
        String s = *it;
        ++it;

        int i = s.find( '=' );
        if ( i > 0 ) {
            String n = s.mid( 0, i ).deURI();
            UString v = Link::decoded( s.mid( i+1 ) );
            if ( n.boring() && !v.isEmpty() )
                d->parameters.insert( n, new UString( v ) );
        }
    }
}


class HTTPSData
    : public Garbage
{
public:
    HTTPSData()
        : tlsServer( 0 ), helper( 0 )
    {}

    TlsServer * tlsServer;
    class HttpsHelper * helper;
};


class HttpsHelper
    : public EventHandler
{
public:
    HttpsHelper( HTTPS * connection )
        : c( connection )
    {}

    void execute()
    {
        c->finish();
    }

private:
    HTTPS * c;
};


/*! \class HTTPS http.h
    This class inherits from HTTP and adds a TlsServer.
*/

/*! Constructs an HTTPS server on fd \a s, and begins TLS negotiation
    immediately.
*/

HTTPS::HTTPS( int s )
    : HTTP( s ), d( new HTTPSData )
{
    d->helper = new HttpsHelper( this );
    d->tlsServer = new TlsServer( d->helper, peer(), "HTTPS" );
    EventLoop::global()->removeConnection( this );
}


/*! Completes TLS negotiation. */

void HTTPS::finish()
{
    if ( !d->tlsServer->done() )
        return;
    if ( !d->tlsServer->ok() ) {
        log( "Couldn't negotiate TLS with " + peer().string(), Log::Error );
        close();
        return;
    }

    startTls( d->tlsServer );
}


/*! Returns the value of the Host header, lowercased but otherwise
    unchanged.
*/

String HTTP::hostHeader() const
{
    return d->hostSupplied;
}
