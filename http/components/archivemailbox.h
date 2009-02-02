// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#ifndef ARCHIVEMAILBOX_H
#define ARCHIVEMAILBOX_H

#include "pagecomponent.h"


class IntegerSet;


class ArchiveMailbox
    : public PageComponent
{
public:
    ArchiveMailbox( class Link * );

    void execute();

private:
    EString threadRendering( class SubjectThread * );
    EString timespan( const IntegerSet & ) const;

private:
    class ArchiveMailboxData * d;
};


#endif
