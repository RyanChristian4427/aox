/*! \class Noop noop.h
    \brief NOOP does nothing (RFC 3501, �6.1.2)

    One might surmise that this function is a true noop, but it's not.
    The side effects need to be handled somehow.

    This class is currently serving as a testing ground for the database
    interface. AMS 20040412
*/

#include "noop.h"

// XXX: We should only need to include one header (database) here.
#include "imap.h"
#include "cache.h"
#include "database.h"

#include "string.h"

void Noop::execute()
{
    if ( !q ) {
        q = new Query( "select foo,bar from test" );
        Database::query( q );
        return;
    }

    while ( q->hasResults() ) {
        // Snarf and process a single row.
        n++;
    }
    
    if ( q->state() == Query::Completed ) {
        if ( n == 0 )
            respond( "NO results obtained" );

        setState( Finished );
    }
}
