// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#ifndef HEADERFILE_H
#define HEADERFILE_H

#include "file.h"


class HeaderFile: public File
{
public:
    HeaderFile( const EString & );

    static HeaderFile * find( const EString & );

    void parse();
};


#endif
