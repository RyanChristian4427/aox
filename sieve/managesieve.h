// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#ifndef MANAGESIEVE_H
#define MANAGESIEVE_H

#include "connection.h"

class User;
class String;


class ManageSieve
    : public SaslConnection
{
public:
    ManageSieve( int );

    enum State { Unauthorised, Authorised };
    void setState( State );
    State state() const;

    void setUser( User * );
    User * user() const;

    void parse();
    void react( Event );

    void runCommands();

    void ok( const String & );
    void no( const String & );
    void send( const String & );

    void setReserved( bool );
    void setReader( class ManageSieveCommand * );

    void capabilities();

    virtual void sendChallenge( const String & );

private:
    class ManageSieveData *d;

    void addCommand();
};


#endif
