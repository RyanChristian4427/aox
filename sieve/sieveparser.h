// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#ifndef SIEVEPARSER_H
#define SIEVEPARSER_H

#include "abnfparser.h"

#include "ustring.h"
#include "list.h"


class SieveParser
    : public AbnfParser
{
public:
    SieveParser( const String & );

    // the unique functions in this class

    List<class SieveProduction> * bad( class SieveProduction * );
    void rememberBadProduction( class SieveProduction * );

    class StringList * extensionsNeeded() const;
    void rememberNeededExtension( const String & );

    // productions in RFC3028bis section 8.1

    void bracketComment();
    void comment();
    void hashComment();

    String identifier();

    UString multiLine();

    uint number();

    UString quotedString();

    String tag();

    void whitespace();

    // productions in RFC3028bis section 8.2

    class SieveArgument * argument();

    class SieveArgumentList * arguments();

    class SieveBlock * block();

    // conflict with sievecommand.h
    class SieveCommand * command();

    List<class SieveCommand> * commands();

    String comparator();

    UString string();

    class UStringList * stringList();

    class SieveTest * test();

    class SieveTestList * testList();

private:
    class SieveParserData * d;
};


#endif
