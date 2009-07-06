// Copyright 2009 The Archiveopteryx Developers <info@aox.org>

#include "entropy.h"

#include "configuration.h"
#include "estring.h"
#include "log.h"

// read()
#include <unistd.h>

// open()
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>



/*! \class Entropy entropy.h

    The Entropy class provides entropy.

    When someone in Archiveopteryx needs entropy, Entropy provides it, in
    the form of a string or a number.

    If true entropy isn't available, Entropy provides
    cryptographically strong pseudorandom numbers.
*/


static int fd = -1;


/*! Sets up the entropy gatherer. */

void Entropy::setup()
{
    if ( fd > -1 )
        ::close( fd );

    EString source( Configuration::text( Configuration::EntropySource ) );
    fd = ::open( source.cstr(), O_RDONLY );
}


/*! Returns the desired \a bytes of entropy as a string. Throws an
    exception if entropy isn't available.
*/

EString Entropy::asString( uint bytes )
{
    EString r;
    if ( bytes == 0 )
        return r;
    if ( fd < 0 ) {
        EString source( Configuration::text( Configuration::EntropySource ) );
        ::log( "Entropy requested, but " + source + " is not available",
               Log::Disaster );
        die( FD );
    }
    uint i = 0;
    while ( i < bytes ) {
        r.append( " " );
        i++;
    }
    i = ::read( fd, (void*)(r.data()), bytes );
    r.truncate( i );
    if ( i < bytes ) {
        log( "Wanted " + fn( bytes ) +
             " bytes of entropy, but received only " + fn( i ),
             i > 0 ? Log::Error : Log::Disaster );
        if ( i == 0 )
            die( FD );
    }
    return r;
}


/*! Returns the desired number of \a bytes of entropy as a number. \a
    bytes must be between 1 and 4 inclusive, since some supported
    architectures have only four-byte integers.
*/

uint Entropy::asNumber( uint bytes )
{
    EString e = asString( bytes );
    return e[0] | ( e[1] << 8 )  | ( e[2] << 16 )  | ( e[3] << 24 );
}
