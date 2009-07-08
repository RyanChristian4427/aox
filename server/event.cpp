// Copyright 2009 The Archiveopteryx Developers <info@aox.org>

#include "event.h"

#include "scope.h"


/*! \class EventHandler event.h
    An abstract base class for anything that wants event notifications.

    Classes that want to be notified of events (e.g. the completion of
    a database query) must inherit from EventHandler and implement the
    execute() function.

    Objects of that class may then pass their "this" pointers to code
    that promises to execute() them when the relevant even occurs.

    There is currently no way to indicate the type or originator of an
    event. Furthermore, the Loop/Connection framework uses an entirely
    different scheme for event notifications.
*/


/*! Creates a new EventHandler object, and sets its arena to the current
    arena.
*/

EventHandler::EventHandler()
    : l( 0 )
{
    if ( Scope::current() )
        l = Scope::current()->log();
}


/*! Exists only to avoid compiler warnings. */

EventHandler::~EventHandler()
{
}


/*! Sets this object's Log to \a log. */

void EventHandler::setLog( Log *log )
{
    l = log;
}


/*! Returns a pointer to this EventHandler's Log object, as inferred by
    the constructor or set with setLog().
*/

Log *EventHandler::log() const
{
    return l;
}


/*! \fn void EventHandler::execute()

    This pure virtual function is called when there's something the
    EventHandler needs to do to process an event.
*/


/*! Logs the message \a m with severity \a s using this EventHandler's
    Log, as specified with setLog().
*/

void EventHandler::log( const EString &m, Log::Severity s ) const
{
    if ( l )
        l->log( m, s );
}


/*! This is just a wrapper around execute() which sets a Scope with
    the event hander's own Log.
*/

void EventHandler::notify()
{
    Scope x( log() );
    execute();
}
