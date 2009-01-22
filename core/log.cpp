// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#include "log.h"

#include "scope.h"
#include "logger.h"
#include "string.h"

// sprintf, fprintf
#include <stdio.h>


static bool disasters = false;


void log( const String &m, Log::Severity s )
{
    Scope * cs = Scope::current();
    Log *l = 0;
    if ( cs )
        l = cs->log();
    if ( l )
        l->log( m, s );
}


/*! \class Log log.h
    The Log class sends log messages to the Log server.

    A Log object accepts messages via log() and sends them to the log
    server.
*/

/*! Constructs a Log object the parent() that's currently in Scope. */

Log::Log()
    : children( 1 ), p( 0 )
{
    Scope * cs = Scope::current();
    if ( cs )
        p = cs->log();
    if ( p )
        ide = p->id() + "/" + fn( p->children++ );
    else
        ide = "1";
}


/*! Constructs a Log object with parent() \a parent. */

Log::Log( Log * parent )
    : children( 0 ), p( parent )
{
}


/*! Logs \a m using severity \a s. What happens to the message depends
    on the type of Logger used, and the log server configuration.
*/

void Log::log( const String &m, Severity s )
{
    Logger * l = Logger::global();
    if ( s == Disaster ) {
        disasters = true;
        String n = "Archiveopteryx";
        if ( l )
            n = l->name();
        fprintf( stderr, "%s: %s\n", n.cstr(), m.simplified().cstr() );
    }

    if ( !l )
        return;

    l->send( ide, s, m );
}


/*! This static function returns a string describing \a s. */

const char * Log::severity( Severity s )
{
    const char *i = 0;

    switch ( s ) {
    case Log::Debug:
        i = "debug";
        break;
    case Log::Info:
        i = "info";
        break;
    case Log::Significant:
        i = "significant";
        break;
    case Log::Error:
        i = "error";
        break;
    case Log::Disaster:
        i = "disaster";
        break;
    }

    return i;
}


/*! Returns true if at least one disaster has been logged (on any Log
    object), and false if none have been.

    The disaster need not be committed - disastersYet() returns true as
    soon as log() has been called for a disastrous error.
*/

bool Log::disastersYet()
{
    return disasters;
}


/*! Returns the identity of this log; this is a string which can be
    used to locate log data in the logfile.
*/

String Log::id()
{
    return ide;
}


/*! Returns a pointer to the Log that was in effect when this object
    was created. This object's id() is based on the parent's id().

    The return value if parent() may be 0.
*/

Log * Log::parent() const
{
    return p;
}


/*! Returns true if this object is \a other or a child of \a other
    (through the parent() chain), and false if not.
*/

bool Log::isChildOf( Log * other ) const
{
    const Log * l = this;
    while ( l && l != other )
        l = l->parent();
    if ( l )
        return true;
    return false;
}
