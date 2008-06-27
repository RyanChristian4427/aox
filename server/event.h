// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#ifndef EVENT_H
#define EVENT_H

#include "log.h"

class String;


class EventHandler
    : public Garbage
{
public:
    EventHandler();
    virtual ~EventHandler();

    void setLog( Log * );
    Log *log() const;

    void notify();
    virtual void execute() = 0;
    virtual void log( const String &, Log::Severity = Log::Info ) const;

private:
    Log *l;
};


#endif
