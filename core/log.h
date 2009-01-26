// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#ifndef LOG_H
#define LOG_H

#include "global.h"
#include "string.h"

class String;


class Log
    : public Garbage
{
public:
    enum Facility {
        General,
        Configuration, Database, Authentication, IMAP, SMTP, POP, HTTP, Server
    };
    enum Severity { Debug, Info, Significant, Error, Disaster };

    Log( Facility );
    Log( Facility, Log * );
    void setFacility( Facility );
    void log( const String &, Severity = Info );
    String id();

    Log * parent() const;
    bool isChildOf( Log * ) const;

    static const char * severity( Severity );
    static const char * facility( Facility );
    static bool disastersYet();

private:
    String ide;
    Facility fc;
    uint children;
    Log * p;
};


void log( const String &, Log::Severity = Log::Info );


#endif
