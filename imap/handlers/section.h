// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#ifndef SECTION_H
#define SECTION_H

#include "global.h"
#include "string.h"
#include "stringlist.h"


class Section
    : public Garbage
{
public:
    Section()
        : binary( false ),
          partial( false ), offset( 0 ), length( UINT_MAX ),
          needsAddresses( false ), needsHeader( false ), needsBody( false )
    {}

    String id;
    String item;
    String part;
    StringList fields;
    bool binary;
    bool partial;
    uint offset;
    uint length;

    bool needsAddresses;
    bool needsHeader;
    bool needsBody;
    String error;
};


#endif
