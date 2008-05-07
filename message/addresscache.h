// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#ifndef ADDRESSCACHE_H
#define ADDRESSCACHE_H

#include "list.h"
#include "cachelookup.h"


class Address;
class Transaction;
class EventHandler;


class AddressCache
    : public Garbage
{
public:
    static void setup();
    static CacheLookup *lookup( Transaction *, List< Address > *,
                                EventHandler * );
};


#endif
