// Copyright 2009 The Archiveopteryx Developers <info@aox.org>

#include "logger.h"

#include "estring.h"
#include "allocator.h"


static Logger *logger = 0;


/*! \class Logger logger.h
    Abstract base class for things that log messages.

    All subclasses of Logger must implement the send() virtual function,
    and take responsibility for correctly logging the lines of text that
    are passed to it.

    A program creates one instance of a Logger subclass at startup and
    uses Logger::global() to process any messages sent to a Log object
    thereafter.
*/

/*! Stores the address of the newly-created Logger for global(). */

Logger::Logger()
{
    ::logger = this;
    Allocator::addEternal( this, "logger" );
}


/*! \fn void Logger::send( const EString &id,
                           Log::Severity s,
                           const EString & m )

    This virtual function logs the message \a m belonging to
    transaction \a id, whose severity is \a s, in a manner decided by
    the subclass.

    \a id uniquely identifies a Log object.
*/


/*! This virtual destructor exists only to ensure that global() doesn't
    return a bad pointer.
*/

Logger::~Logger()
{
    ::logger = 0;
}


/*! Returns a pointer to the global Logger. */

Logger *Logger::global()
{
    return ::logger;
}


/*! Returns an application name. Subclasses must provide this name.

    I don't like this mechanism. It's hacky. Only exists to let Log
    (in core) get at information held by the Server class (in server).
*/

EString Logger::name() const
{
    return "Archiveopteryx";
}
