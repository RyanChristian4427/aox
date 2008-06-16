// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#ifndef FIELDNAME_H
#define FIELDNAME_H

#include "stringlist.h"

class EventHandler;


class FieldName {
public:
    static void setup();

    static void reload( EventHandler * = 0 );
    static void create( const StringList &, EventHandler * );

    static void add( const String &, uint );

    static String name( uint );
    static uint id( const String & );
};


#endif