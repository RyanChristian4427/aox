// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#ifndef SMTPCLIENT_H
#define SMTPCLIENT_H

#include "connection.h"
#include "event.h"


class DSN;
class String;
class Message;
class Address;
class Recipient;


class SmtpClient
    : public Connection
{
public:
    SmtpClient( const Endpoint &, EventHandler * );

    void react( Event );

    bool ready() const;
    void send( DSN *, EventHandler * );

    void logout( uint );

    String error() const;

private:
    class SmtpClientData * d;

    void parse();
    void sendCommand();
    void handleFailure( const String & );
    void finish( const char * status = 0 );
    void recordExtension( const String & );

    static String dotted( const String & );
};


#endif
