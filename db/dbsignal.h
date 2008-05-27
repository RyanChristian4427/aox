// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#ifndef DBSIGNAL_H
#define DBSIGNAL_H

#include "stringlist.h"

class EventHandler;


class DatabaseSignal
    : public Garbage
{
public:
    DatabaseSignal( const String &, EventHandler * );

    static void notifyAll( const String & );

    static StringList * names();

private: // noone can destroy this
    ~DatabaseSignal();

private:
    class DatabaseSignalData * d;
};


#endif
