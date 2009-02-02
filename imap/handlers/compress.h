// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#ifndef COMPRESS_H
#define COMPRESS_H

#include "command.h"
#include "estring.h"


class Compress: public Command {
public:
    Compress();
    void parse();
    void execute();

private:
    EString a;
};


#endif
