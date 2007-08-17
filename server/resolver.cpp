// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <arpa/nameser_compat.h>
#include <resolv.h>
#include <netdb.h>
#include <errno.h>

#include "resolver.h"

#include "dict.h"
#include "endpoint.h"
#include "allocator.h"
#include "configuration.h"


class ResolverData
    : public Garbage
{
public:
    StringList errors;
    Dict<StringList> names;
    String reply;
    String host;
    bool bad;
};


/*! \class Resolver resolver.h

    The Resolver class performs DNS lookups and caches the results
    until the process exits. It does not consider the TTLs on the DNS
    results.

    The only public functions are resolve(), which does a cache lookup
    and failing that, a DNS lookup, and errors(), which returns a list
    of all errors seen so far. A server can ensure that it calls
    resolve() at startup time for all required names, and if errors()
    remains empty, all is well and remains well until the end of the
    process.

    We need a class called Revolver.
*/


/*! Constructs an empty Resolver. This constructor is private; in
    general, Resolver is used via the static function resolve().
*/

Resolver::Resolver()
    : Garbage(), d( new ResolverData )
{

}


/*! Resolves \a name and returns a list of results, or returns a
    cached list of results if resolve() has been called for \a name
    already.

    \a name is assumed to be case-insensitive.

    Any errors are added to an internal list and can be retrieved with
    errors().
*/

StringList Resolver::resolve( const String & name )
{
    Resolver * r = resolver();
    r->d->host = name.lower();
    if ( r->d->names.contains( r->d->host ) )
        return *r->d->names.find( r->d->host );

    StringList * results = new StringList;
    if ( r->d->host == "localhost" ) {
        results->append( "::1" );
        results->append( "127.0.0.1" );
    }
    else if ( r->d->host.contains( ':' ) ) {
        // it's an ipv6 address
        Endpoint * e = new Endpoint( name, 1 );
        if ( e->valid() )
            results->append( e->address() );
    }
    else if ( r->d->host.contains( '.' ) &&
              r->d->host[r->d->host.length()-1] <= '9' ) {
        // it's an ipv4 address
        Endpoint * e = new Endpoint( name, 1 );
        if ( e->valid() )
            results->append( e->address() );
    }
    else if ( r->d->host.startsWith( "/" ) ) {
        // it's a unix pipe
        results->append( name );
    }
    else {
        // it's a domain name. we use res_search() since getnameinfo()
        // had such bad karma when we tried it.
        if ( Configuration::toggle( Configuration::UseIPv6 ) )
            r->query( T_AAAA, results );
        if ( Configuration::toggle( Configuration::UseIPv4 ) )
            r->query( T_A, results );
        r->d->names.insert( r->d->host, results );
    }
    return *results;
}


/*! Returns a list of one-line error messages concerning all
    resolution errors since startup.
*/

StringList Resolver::errors()
{
    resolver()->d->errors.removeDuplicates( false );
    return resolver()->d->errors;
}


static Resolver * resolver = 0;


/*! This private helper ensures that there is a resolver, and returns
    a pointer to it.
*/

Resolver * Resolver::resolver()
{
    if ( ::resolver )
        return ::resolver;

    ::resolver = new Resolver;
    Allocator::addEternal( ::resolver, "name resolver" );
    return ::resolver;
}


/*! Reads and returns a single string from the stored DNS reply at
    offset \a i, modifying \a i. If errors occur, an error is
    logged. If the parser runs off the end of the reply, readString()
    returns an empty string, but logs no error.
*/

String Resolver::readString( uint & i )
{
    bool ok = true;
    bool bad = false;
    String r;
    uint c = d->reply[i];
    if ( i >= d->reply.length() ) {
        ok = false;
    }
    if ( c == 0 ) {
        // all is in perfect order
        i++;
    }
    else if ( c < 64 ) {
        i++;
        r.append( d->reply.mid( i, c ) );
        i += c;
        // and just in case that wasn't all, do a spot of tail recursion
        String domain = readString( i );
        if ( !domain.isEmpty() ) {
            r.append( "." );
            r.append( domain );
        }
    }
    else if ( c >= 192 ) {
        uint qi = ( ( d->reply[i] & 0x3f ) << 8 ) + d->reply[i+1];
        if ( qi < i )
            r.append( readString( qi ) );
        else
            bad = true;
        i += 2;
    }
    else {
        bad = true;
    }
    if ( bad )
        d->errors.append( "Parse error in response packet for " + d->host );
    else if ( ok )
        return r;
    return "";
}



/*! This private function issues a DNS query of \a type and appends
    the results to \a results. Truncated packets are silently accepted
    (the partial RR is ignored).  \a type is passed through to
    ::res_query() unchanged.
*/

void Resolver::query( uint type, StringList * results )
{
    d->bad = false;
    d->reply.reserve( 4096 );
    int len = res_query( d->host.cstr(), C_IN, type,
                         (u_char*)d->reply.data(), d->reply.capacity() );
    if ( len <= 0 ) {
        char * name = "IPv4";
        if ( type == T_AAAA )
            name = "IPv6";
        if ( errno == HOST_NOT_FOUND )
            d->errors.append( String("Found no ") + name +
                              " address for " + d->host );
        else
            d->errors.append( String("DNS error while looking up ") + name +
                              " address for " + d->host );
        return;
    }

    d->reply.setLength( len );

    uint p = 12;

    if ( len < 12 )
        return;

    uint qdcount = (  d->reply[4] << 8 ) +  d->reply[5];
    uint ancount = (  d->reply[6] << 8 ) +  d->reply[7];

    // skip the query section
    while ( p < d->reply.length() && qdcount && !d->bad ) {
        (void)readString( p );
        p += 4;
        qdcount--;
    }

    // parse each A and AAAA in the answer section
    while ( p < d->reply.length() && ancount && !d->bad  ) {
        String n = readString( p );
        String a;
        uint type = ( d->reply[p] << 8 ) + d->reply[p+1];
        uint rdlength = ( d->reply[p+8] << 8 ) + d->reply[p+9];
        p += 10;
        if ( type == T_A ) {
            if ( rdlength == 4 ) {
                uint i = 0;
                while ( i < rdlength ) {
                    if ( !a.isEmpty() )
                        a.append( '.' );
                    a.append( fn( d->reply[p+i] ) );
                    i++;
                }
            }
        }
        else if ( type == T_AAAA ) {
            if ( rdlength == 16 ) {
                uint i = 0;
                while ( i < rdlength ) {
                    if ( !a.isEmpty() )
                        a.append( ':' );
                    a.append( fn( ( d->reply[p+i] << 8 ) + d->reply[p+i+1],
                                  16 ) );
                    i += 2;
                }
            }
        }
        else if ( type == T_CNAME ) {
            // hm.
        }
        p += rdlength;
        if ( p <= d->reply.length() && !d->bad && !a.isEmpty() ) {
            Endpoint * e = new Endpoint( a, 1 );
            if ( e->valid() )
                results->append( e->address() );
            // if not, we received an illegal reply from the DNS
            // server. let's ignore that silently for now.
        }
        ancount--;
    }

    // we don't care about the NS and AD sections, so we're done
}
