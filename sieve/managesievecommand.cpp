// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#include "managesievecommand.h"

#include "tls.h"
#include "utf.h"
#include "date.h"
#include "dict.h"
#include "user.h"
#include "query.h"
#include "scope.h"
#include "sieve.h"
#include "buffer.h"
#include "address.h"
#include "mailbox.h"
#include "message.h"
#include "allocator.h"
#include "mechanism.h"
#include "stringlist.h"
#include "sieveaction.h"
#include "sievescript.h"
#include "transaction.h"
#include "ustringlist.h"


class ManageSieveCommandData
    : public Garbage
{
public:
    ManageSieveCommandData()
        : sieve( 0 ), pos( 0 ), done( false ),
          tlsServer( 0 ), m( 0 ), r( 0 ),
          user( 0 ), t( 0 ), query( 0 ), step( 0 )
    {}

    ManageSieve * sieve;
    ManageSieveCommand::Command cmd;
    String arg;
    uint pos;

    bool done;

    TlsServer * tlsServer;
    SaslMechanism * m;
    String * r;
    User * user;

    Transaction * t;
    Query * query;
    String no;
    String ok;
    uint step;

    // for putscript. I think we need subclasses here too.
    Dict<Mailbox> create;
    String name;
    String script;
};


/*! \class ManageSieveCommand managesievecommand.h
    This class represents a single ManageSieve command. It is analogous to a
    POP Command. Almost identical, in fact.
*/


/*! Creates a new ManageSieveCommand object representing the command
    \a cmd with arguments \a args for the ManageSieve server \a sieve.
*/

ManageSieveCommand::ManageSieveCommand( ManageSieve * sieve,
                                        Command cmd, const String & args )
    : d( new ManageSieveCommandData )
{
    d->sieve = sieve;
    d->cmd = cmd;
    d->arg = args;
    setLog( new Log( Log::Server ) );
    Scope x( log() );
    switch( cmd ) {
    case Authenticate:
        log( "Executing authenticate command" );
        break;
    case StartTls:
        log( "Executing starttls command" );
        break;
    case Logout:
        log( "Executing logout command" );
        break;
    case Capability:
        log( "Executing capability command" );
        break;
    case HaveSpace:
        log( "Executing havespace command" );
        break;
    case PutScript:
        log( "Executing putscript command" );
        break;
    case ListScripts:
        log( "Executing listscripts command" );
        break;
    case SetActive:
        log( "Executing setactive command" );
        break;
    case GetScript:
        log( "Executing getscript command" );
        break;
    case DeleteScript:
        log( "Executing deletescript command" );
        break;
    case XAoxExplain:
        log( "Executing xaoxexplain command" );
        break;
    case Unknown:
        log( "Executing unknown command" );
        break;
    }
}


/*! Returns true if this ManageSieveCommand has finished executing:
  and false if
    execute() hasn't been called, or if it has work left to do.
*/

bool ManageSieveCommand::done()
{
    return d->done;
}


/*! Tries to read a single response line from the client. Upon return,
    d->r points to the response, or is 0 if no response could be read.
*/

void ManageSieveCommand::read()
{
    d->r = d->sieve->readBuffer()->removeLine();
}


void ManageSieveCommand::execute()
{
    if ( d->done )
        return;

    Scope x( log() );
    bool ok = true;
    switch ( d->cmd ) {
    case Logout:
        log( "Received LOGOUT command", Log::Debug );
        d->sieve->Connection::setState( Connection::Closing );
        break;

    case Capability:
        end();
        if ( d->no.isEmpty() )
            d->sieve->capabilities();
        break;

    case StartTls:
        ok = startTls();
        break;

    case Authenticate:
        ok = authenticate();
        break;

    case HaveSpace:
        ok = haveSpace();
        break;

    case PutScript:
        ok = putScript();
        break;

    case ListScripts:
        ok = listScripts();
        break;

    case SetActive:
        ok = setActive();
        break;

    case GetScript:
        ok = getScript();
        break;

    case DeleteScript:
        ok = deleteScript();
        break;

    case XAoxExplain:
        ok = explain();
        break;

    case Unknown:
        no( "Unknown command" );
        break;
    }

    if ( d->query && d->query->failed() && d->no.isEmpty() )
        no( "Database failed: " + d->query->error() );
    else if ( d->t && d->t->failed() && d->no.isEmpty() )
        no( "Database failed: " + d->t->error() ); // XXX need to rollback?

    if ( !d->no.isEmpty() )
        ok = true;

    if ( !ok )
        return;

    d->done = true;
    if ( d->no.isEmpty() ) {
        d->sieve->enqueue( "OK" );
        if ( !d->ok.isEmpty() ) {
            d->sieve->enqueue( " " );
            d->sieve->enqueue( encoded( d->ok ) );
        }
        d->sieve->enqueue( "\r\n" );
    }
    else {
        d->sieve->enqueue( "NO" );
        if ( !d->no.isEmpty() ) {
            d->sieve->enqueue( " " );
            d->sieve->enqueue( encoded( d->no ) );
        }
        d->sieve->enqueue( "\r\n" );
    };
    d->sieve->write();
    d->sieve->runCommands();
}


/*! Handles the STARTTLS command. */

bool ManageSieveCommand::startTls()
{
    if ( d->sieve->hasTls() ) {
        no( "STARTTLS once = good. STARTTLS twice = bad." );
        return true;
    }

    if ( !d->tlsServer ) {
        end();
        if ( !d->no.isEmpty() )
            return true;
        d->tlsServer = new TlsServer( this, d->sieve->peer(), "ManageSieve" );
        d->sieve->setReserved( true );
    }

    if ( !d->tlsServer->done() )
        return false;

    if ( !d->sieve->hasTls() ) {
        d->sieve->setReserved( false );

        if ( !d->tlsServer->ok() ) {
            no( "Internal error starting TLS engine" );
            return true;
        }

        d->sieve->enqueue( "OK\r\n" );
        d->sieve->write();
        d->sieve->startTls( d->tlsServer );
        d->sieve->capabilities();
        return true;
    }

    return false;
}


/*! Handles the AUTHENTICATE command. */

bool ManageSieveCommand::authenticate()
{
    if ( !d->m ) {
        String t = string().lower();
        String * r = 0;
        if ( d->arg[d->pos] == ' ' ) {
            whitespace();
            r = new String( string() );
        }
        end();

        if ( !d->no.isEmpty() )
            return true;

        d->m = SaslMechanism::create( t, this, d->sieve );
        if ( !d->m ) {
            no( "SASL mechanism " + t + " not available" );
            return true;
        }

        d->sieve->setReader( this );
        d->m->readInitialResponse( r );
    }

    if ( d->m->state() == SaslMechanism::AwaitingResponse && d->r )
        d->m->readResponse( new String( d->r->unquoted() ) );

    if ( !d->m->done() )
        return false;

    if ( d->m->state() == SaslMechanism::Succeeded ) {
        d->sieve->setUser( d->m->user(), d->m->name() );
        d->sieve->setState( ManageSieve::Authorised );
    }
    else if ( d->m->state() == SaslMechanism::Terminated ) {
        no( "Authentication terminated" );
    }
    else {
        no( "Authentication failed" );
    }
    d->sieve->setReader( 0 );

    return true;
}


/*! Handles the HAVESPACE command. Accepts any name and size, then
    reports OK: We don't do hard quotas. */

bool ManageSieveCommand::haveSpace()
{
    (void)string();
    whitespace();
    (void)number();
    end();
    return true;
}


/*! Handles the PUTSCRIPT command.

    Silently creates any mailboxes referred to by fileinto commands,
    provided they're in the user's own namespace.

    This solves the major problem caused fileinto commands that refer
    to unknown mailbox names. People can still delete or rename
    mailboxes while a script refers to them, and it's possible to
    fileinto "/users/someoneelse/inbox", but those are much smaller
    problem by comparison.

    I also like the timing of this: Uploading a script containing
    fileinto "x" creates x at once (instead of later, which sendmail
    does).
*/

bool ManageSieveCommand::putScript()
{
    if ( !d->t ) {
        d->name = string();
        whitespace();
        d->script = string();
        end();
        if ( d->script.isEmpty() ) {
            no( "Script cannot be empty" );
            return true;
        }
        SieveScript script;
        script.parse( d->script.crlf() );
        String e = script.parseErrors();
        if ( !e.isEmpty() ) {
            no( e );
            return true;
        }
        if ( d->name.isEmpty() ) {
            log( "Syntax checking only" );
            // Our very own syntax-checking hack.
            return true;
        }

        // look for fileinto calls. if any refer to nonexistent
        // mailboxes in the user's namespace, create those. if any
        // refer to mailboxes not owned by the user, deny the command.
        List<SieveCommand> stack;
        stack.append( script.topLevelCommands() );
        while ( !stack.isEmpty() ) {
            SieveCommand * c = stack.shift();
            if ( c->block() )
                stack.append( c->block()->commands() );
            if ( c->error().isEmpty() && c->identifier() == "fileinto" ) {
                SieveArgumentList * l = c->arguments();
                List<SieveArgument>::Iterator a( l->arguments() );
                while ( a ) {
                    UString n = *a->stringList()->first();
                    Mailbox * home = d->sieve->user()->home();
                    Mailbox * m = 0;
                    if ( n.startsWith( "/" ) )
                        m = Mailbox::obtain( n, true );
                    else
                        m = Mailbox::obtain( home->name() + "/" + n, true );
                    Mailbox * p = m;
                    while ( p && p != home )
                        p = p->parent();
                    if ( !m->synthetic() && !m->deleted() ) {
                        // no action needed
                    }
                    else if ( p == home ) {
                        log( "Creating mailbox " + m->name().ascii() +
                             " (used in fileinto and did not exist)" );
                        d->create.insert( m->name().utf8(), m );
                    }
                    else {
                        no( "Script refers to mailbox " +
                            m->name().ascii().quoted() +
                            ", which does not exist and is outside your"
                            " home directory (" +
                            home->name().ascii().quoted() + ")" );
                        return true;
                    }
                    ++a;
                }
            }
        }

        // at this point, nothing can prevent us from completing.

        d->t = new Transaction( this );

        d->query = new Query( "select * from scripts "
                              "where name=$1 and owner=$2 "
                              "for update", this );
        d->query->bind( 1, d->name );
        d->query->bind( 2, d->sieve->user()->id() );
        d->t->enqueue( d->query );
        d->t->execute();

        StringList::Iterator i( d->create.keys() );
        while ( i ) {
            Mailbox * m = d->create.find( *i );
            if ( m )
                (void)m->create( d->t, d->sieve->user() );
            ++i;
        }
    }

    if ( !d->query->done() )
        return false;

    if ( d->step == 0 ) {
        if ( d->query->nextRow() ) {
            d->query = new Query( "update scripts set script=$3 where "
                                  "owner=$1 and name=$2", 0 );
            log( "Updating script: " + d->name );
        }
        else {
            d->query = new Query( "insert into scripts "
                                  "(owner,name,script,active) "
                                  "values($1,$2,$3,false)", 0 );
            log( "Storing new script: " + d->name );
        }
        d->query->bind( 1, d->sieve->user()->id() );
        d->query->bind( 2, d->name );
        d->query->bind( 3, d->script );
        d->t->enqueue( d->query );

        d->step = 1;
        d->t->commit();
        return false;
    }

    if ( !d->t->done() )
        return false;

    StringList::Iterator i( d->create.keys() );
    while ( i ) {
        d->ok.append( "Created mailbox " + i->quoted() + "." );
        ++i;
        if ( i )
            d->ok.append( "\r\n" );
    }

    return true;
}


/*! Handles the LISTSCRIPTS command. */

bool ManageSieveCommand::listScripts()
{
    if ( !d->query ) {
        end();
        d->query =
            new Query( "select * from scripts where owner=$1 order by name",
                       this );
        d->query->bind( 1, d->sieve->user()->id() );
        if ( d->no.isEmpty() )
            d->query->execute();
    }

    while ( d->query->hasResults() ) {
        Row * r = d->query->nextRow();
        String line = encoded( r->getString( "name" ) );
        if ( r->getBoolean( "active" ) )
            line.append( " ACTIVE" );
        d->sieve->send( line );
    }

    if ( !d->query->done() )
        return false;

    return true;
}


/*! Handles the SETACTIVE command. */

bool ManageSieveCommand::setActive()
{
    if ( !d->t ) {
        String name = string();
        end();
        if ( !d->no.isEmpty() )
            return true;

        d->t = new Transaction( this );

        if ( name.isEmpty() ) {
            Query * q = new Query( "update scripts set active='f' "
                                   "where owner=$1 and active='t'",
                                   0 );
            q->bind( 1, d->sieve->user()->id() );
            d->t->enqueue( q );
            d->query = new Query( "select ''::text as name", this );
            log( "Deactivating all scripts" );
        }
        else {
            d->query = new Query( "select name from scripts "
                                  "where owner=$1 and name=$2 "
                                  "for update", this );
            d->query->bind( 1, d->sieve->user()->id() );
            d->query->bind( 2, name );
        }
        d->t->enqueue( d->query );
        d->t->execute();
    }

    if ( d->query && !d->query->done() )
        return false;

    if ( d->query ) {
        Row * r = d->query->nextRow();
        if ( !r ) {
            d->t->rollback();
            no( "No such script" );
            return true;
        }
        d->query = 0;
        if ( !r->getString( "name" ).isEmpty() ) {
            Query * q = new Query( "update scripts set active=(name=$2) "
                                   "where owner=$1 and "
                                   "(name=$2 or active='t')",
                                   this );
            q->bind( 1, d->sieve->user()->id() );
            q->bind( 2, r->getString( "name" ) );
            d->t->enqueue( q );
            log( "Activating script " + r->getString( "name" ) );
        }
        d->t->commit();
    }

    if ( !d->t->done() )
        return false;

    if ( d->t->failed() )
        no( "Couldn't activate script: " + d->t->error() );

    return true;
}


/*! Handles the GETSCRIPT command. */

bool ManageSieveCommand::getScript()
{
    if ( !d->query ) {
        String name = string();
        end();
        d->query =
            new Query( "select script from scripts where owner=$1 and name=$2",
                       this );
        d->query->bind( 1, d->sieve->user()->id() );
        d->query->bind( 2, name );
        if ( d->no.isEmpty() )
            d->query->execute();
    }

    if ( !d->query->done() )
        return false;

    Row * r = d->query->nextRow();

    if ( !r )
        no( "No such script" );
    else if ( !d->query->failed() )
        d->sieve->enqueue( encoded( r->getString( "script" ) ) + "\r\n" );

    return true;
}


/*! Handles the DELETESCRIPT command. */

bool ManageSieveCommand::deleteScript()
{
    if ( !d->t ) {
        d->name = string();
        end();
        d->t = new Transaction( this );
        // select first, so the no() calls below work
        d->query =
            new Query( "select active from scripts "
                       "where owner=$1 and name=$2",
                       this );
        d->query->bind( 1, d->sieve->user()->id() );
        d->query->bind( 2, d->name );
        d->t->enqueue( d->query );
        // then delete
        Query * q = new Query( "delete from scripts where owner=$1 and "
                               "name=$2 and active='f'", this );
        q->bind( 1, d->sieve->user()->id() );
        q->bind( 2, d->name );
        d->t->enqueue( q );
        if ( d->no.isEmpty() )
            d->t->commit();
    }

    if ( !d->t->done() )
        return false;

    if ( d->t->failed() ) {
        no( "Couldn't delete script: " + d->t->error() );
    }
    else {
        Row * r = d->query->nextRow();
        if ( !r )
            no( "No such script" );
        else if ( r->getBoolean( "active" ) )
            no( "Can't delete active script" );
        else
            log( "Deleted script " + d->name );
    }

    return true;
}


/*! Returns the next argument from the client, which must be a string,
    or sends a NO.
*/

String ManageSieveCommand::string()
{
    String r;
    if ( d->arg[d->pos] == '"' ) {
        uint i = d->pos + 1;
        while ( i < d->arg.length() && d->arg[i] != '"' ) {
            if ( d->arg[i] == '\\' )
                i++;
            r.append( d->arg[i] );
            i++;
        }
        if ( d->arg[i] == '"' )
            i++;
        d->pos = i;
    }
    else if ( d->arg[d->pos] == '{' ) {
        uint pos = d->pos;
        d->pos++;
        uint len = number();
        if ( d->arg.mid( d->pos, 4 ) != "+}\r\n" )
            no( "Could not parse literal at position " + fn( pos ) + ": " +
                d->arg.mid( pos, d->pos + 4 - pos ) );
        d->pos += 4;
        r = d->arg.mid( d->pos, len );
        d->pos += len;
    }
    else {
        no( "Could not parse string at position " + fn( d->pos ) + ": " +
            d->arg.mid( d->pos, 10 ) );
    }

    if ( d->no.isEmpty() )
        log( "String argument: " + r, Log::Debug );

    return r;
}


/*! Returns the next number from the client, or sends a NO if there
    isn't a number (in the 32-bit range).
*/

uint ManageSieveCommand::number()
{
    uint i = d->pos;
    while ( d->arg[i] >= '0' && d->arg[i] <= '9' )
        i++;
    if ( i == d->pos )
        no( "Could not find a number at at position " + fn( d->pos ) + ": " +
            d->arg.mid( d->pos, 10 ) );
    bool ok = true;
    uint n = d->arg.mid( d->pos, i-d->pos ).number( &ok );
    if ( !ok )
        no( "Could not parse the number at position " + fn( d->pos ) + ": " +
            d->arg.mid( d->pos, i-d->pos ) );
    d->pos = i;
    if ( d->no.isEmpty() )
        log( "Numeric argument: " + fn( n ), Log::Debug );
    return n;
}


/*! Skips whitespace in the argument list. Should perhaps report an
    error if there isn't any? Let's keep it as it is, though.
*/

void ManageSieveCommand::whitespace()
{
    while ( d->arg[d->pos] == ' ' )
        d->pos++;
}


/*! Verifies that parsing has reached the end of the argument list,
    and logs an error else.
*/

void ManageSieveCommand::end()
{
    whitespace();
    if ( d->pos >= d->arg.length() )
        return;
    no( "Garbage at end of argument list (pos " + fn( d->pos ) + "): " +
        d->arg.mid( d->pos, 20 ) );
}


/*! Records that this command is to be rejected, optionally with \a
    message.
*/

void ManageSieveCommand::no( const String & message )
{
    if ( d->no.isEmpty() ) {
        d->no = message;
        log( "Returning NO: " + d->no );
    }
}


/*! Returns \a input encoded either as a managesieve quoted or literal
    string. Quoted is preferred, if possible.
*/

String ManageSieveCommand::encoded( const String & input )
{
    bool q = true;
    if ( input.length() > 1024 )
        q = false;
    uint i = 0;
    while ( q && i < input.length() ) {
        if ( input[i] == 0 || input[i] == 13 || input[i] == 10 )
            q = false;
        i++;
    }

    if ( q )
        return input.quoted();

    String r( "{" );
    r.append( String::fromNumber( input.length() ) );
    r.append( "+}\r\n" );
    r.append( input );
    return r;
}


class ExplainStuff
    : public Garbage
{
public:
    ExplainStuff()
        : from( 0 ),
          to( 0 ), keep( 0 ),
          script( 0 ), message( 0 )
        {
            Allocator::addEternal( this, "managesieve explain status" );
        }
    Address * from;
    Address * to;
    Mailbox * keep;
    SieveScript * script;
    Message * message;
};

static ExplainStuff * x = 0;


/*! This Archiveopteryx extension explains what a sieve script (the
    first argument) does with a given message. It is intended for
    automated testing at Oryx.

    The command takes a number of name-value pairs as aguments. The
    possible names are from, to, keep, script and message. The
    arguments are syntactically valid addresses, mailbox name, sieve
    scripts and messages.

    It runs the script on the rest of the data and reports what
    actions would be performed, if any, and whether the script
    completed. (If the message is not available, the script may or may
    not be able to complete.)

    NOTE: This command uses static storage. If two managesieve clients
    use it at the same time, they'll overwrite each other's data.
*/

bool ManageSieveCommand::explain()
{
    if ( !::x )
        ::x = new ExplainStuff;

    whitespace();
    while ( d->no.isEmpty() && d->pos < d->arg.length() ) {
        String name = string();
        whitespace();
        String value = string();
        whitespace();
        if ( name == "from" || name == "to" ) {
            if ( value.isEmpty() ) {
                if ( name == "from" )
                    ::x->from = 0;
                else
                    ::x->to = 0;
            }
            else {
                AddressParser ap( value );
                if ( ap.addresses()->count() != 1 )
                    no( "Need exactly one address for " + name );
                else if ( name == "from" )
                    ::x->from = ap.addresses()->first();
                else
                    ::x->to = ap.addresses()->first();
            }
        }
        else if ( name == "keep" ) {
            if ( value.isEmpty() ) {
                ::x->keep = 0;
            }
            else {
                Utf8Codec u;
                ::x->keep = Mailbox::find( u.toUnicode( value ) );
                if ( !::x->keep )
                    no( "No such mailbox: " + value );
            }
        }
        else if ( name == "script" ) {
            if ( value.isEmpty() ) {
                ::x->script = 0;
            }
            else {
                if ( !::x->script )
                    ::x->script = new SieveScript;
                ::x->script->parse( value );
                if ( ::x->script->isEmpty() )
                    no( "Script cannot be empty" );
                String e = ::x->script->parseErrors();
                if ( !e.isEmpty() )
                    no( e );
            }
        }
        else if ( name == "message" ) {
            if ( value.isEmpty() ) {
                ::x->message = 0;
            }
            else {
                ::x->message = new Message( value );
                ::x->message->setHeadersFetched();
                ::x->message->setBodiesFetched();
                ::x->message->setAddressesFetched();
                ::x->message->setRfc822Size( ::x->message->rfc822().length() );
                if ( !::x->message->error().isEmpty() )
                    no( "Message parsing: " + ::x->message->error() );
            }
        }
        else {
            no( "Unknown name: " + name );
        }
    }

    if ( !::x->script )
        no( "No sieve (yet)" );
    if ( !::x->from )
        no( "No sender address (yet)" );
    if ( !::x->to )
        no( "No recipient address (yet)" );
    if ( !::x->keep )
        no( "No keep mailbox (yet)" );

    if ( !d->no.isEmpty() )
        return true;

    Sieve s;
    s.setSender( ::x->from );
    s.addRecipient( ::x->to, ::x->keep, d->sieve->user(), ::x->script );
    s.evaluate();
    uint a = s.actions( ::x->to )->count();
    bool m = false;
    if ( ::x->message && !s.done() ) {
        s.setMessage( ::x->message, new Date );
        s.evaluate();
        m = true;
    }
    if ( ::x->message && !m )
        d->sieve->send( "Script did not need the message" );
    else if ( !s.done() )
        d->sieve->send( "Script did not complete" );

    uint n = 0;
    List<SieveAction>::Iterator sa( s.actions( ::x->to ) );
    while ( sa ) {
        String r( "Action: " );
        switch ( sa->type() ) {
        case SieveAction::Reject:
            r.append( "reject" );
            break;
        case SieveAction::FileInto:
            r.append( "fileinto " );
            r.append( sa->mailbox()->name().utf8() );
            break;
        case SieveAction::Redirect:
            r.append( "redirect " );
            r.append( sa->recipientAddress()->localpart() );
            r.append( "@" );
            r.append( sa->recipientAddress()->domain() );
            break;
        case SieveAction::Discard:
            r.append( "discard" );
            break;
        case SieveAction::Vacation:
            r.append( "send vacation message to " );
            r.append( sa->recipientAddress()->lpdomain() );
            r.append( " with subject " );
            r.append( sa->message()->header()->subject() );
            break;
        case SieveAction::Error:
            r = "Error: ";
            r.append( sa->errorMessage().simplified() );
            break;
        }
        if ( m && a && n<a )
            r.append( " (before seeing the message text)" );
        d->sieve->send( r );
        ++sa;
        n++;
    }

    return true;
}
