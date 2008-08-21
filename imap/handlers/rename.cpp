// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#include "rename.h"

#include "user.h"
#include "query.h"
#include "entropy.h"
#include "mailbox.h"
#include "session.h"
#include "occlient.h"
#include "permissions.h"
#include "transaction.h"


class RenameData
    : public Garbage
{
public:
    RenameData()
        : c( 0 ), t( 0 ), ready( false ) {}
public:
    Mailbox * from;
    UString toName;

    Rename * c;
    Transaction * t;
    bool ready;

    class MailboxPair
        : public Garbage
    {
    public:
        MailboxPair()
            : from( 0 ), toParent( 0 ),
              toUidvalidity( 0 ) {}
    public:
        Mailbox * from;
        UString toName;
        Mailbox * toParent;
        uint toUidvalidity;
    };

    List<MailboxPair> renames;

    void process( MailboxPair * p, MailboxPair * parent );
};


/*! \class Rename rename.h
    Renames a mailbox (RFC 3501 section 6.3.5) and its children.

    If the mailbox is the user's inbox, we create create a new inbox
    after moving the old one, and ensure that mail is delivered to the
    new inbox henceforth, not to the renamed old one. This is more or
    less what RFC 3501 section 6.3.5 says.

    It's not clear what should happen if someone has inbox selected
    while it's being renamed. In our code, the renamed mailbox remains
    selected, and the new inbox is not selected.

    There is a race condition here: we check that the user has
    permission to carry out the transaction, but the permission
    checking is not within the transaction that does the move.  This
    seems to be insignificant - it can't be used to achieve anything.
*/


Rename::Rename()
    : d( new RenameData )
{
    d->c = this;
}


void Rename::parse()
{
    space();
    d->from = mailbox();
    space();
    d->toName = mailboxName();
    end();
    if ( ok() )
        log( "Rename from " + d->from->name().ascii() +
             " to " + d->toName.ascii() );
}


void RenameData::process( MailboxPair * p, MailboxPair * parent )
{
    c->requireRight( p->from, Permissions::DeleteMailbox );
    if ( !parent || parent->toParent != p->toParent )
        c->requireRight( p->toParent, Permissions::CreateMailboxes );
    renames.append( p );

    Mailbox * to = Mailbox::obtain( p->toName, false );
    if ( to && !( to->synthetic() || to->deleted() ) ) {
        c->error( Rename::No,
                  "Destination mailbox exists: " + p->toName.ascii() );
        c->setRespTextCode( "ALREADYEXISTS" );
        t->rollback();
        return;
    }

    p->toUidvalidity = p->from->uidvalidity();

    // if an old mailbox is in the way, move it aside
    Query * q = 0;
    if ( to && !to->synthetic() ) {
        q = new Query( "update mailboxes set name=$1 where id=$2", 0 );
        q->bind( 1, Entropy::asString( 16 ).hex() );
        q->bind( 2, to->id() );
        t->enqueue( q );
        // and bump uidvalidity to inform any caches
        if ( to->uidvalidity() > p->toUidvalidity || to->uidnext() > 1 )
            p->toUidvalidity = to->uidvalidity() + 1;
    }

    // move the mailbox
    q = new Query( "update mailboxes set name=$1,uidvalidity=$2 "
                   "where id=$3", 0 );
    q->bind( 1, p->toName );
    q->bind( 2, p->toUidvalidity );
    q->bind( 3, p->from->id() );
    t->enqueue( q );

    // insert a deleted placeholder to ensure that uidnext/uidvalidity
    // will be okay if a new mailbox is created with the same name as
    // this one used to have
    if ( to && !to->synthetic() ) {
        // if we have the old mailbox, use it
        q = new Query( "update mailboxes "
                       "set name=$1,uidnext=$2,uidvalidity=$3,deleted='t' "
                       "where id=$4", 0 );
        q->bind( 4, to->id() );
    }
    else {
        // else, create a new one
        q = new Query( "insert into mailboxes "
                       "(name,uidnext,uidvalidity,deleted) "
                       "values ($1,$2,$3,'t')", 0 );
    }
    q->bind( 1, p->from->name() );
    q->bind( 2, p->from->uidnext() );
    q->bind( 3, p->from->uidvalidity() );
    t->enqueue( q );
}


void Rename::execute()
{
    if ( state() != Executing )
        return;

    if ( !d->t ) {
        UString inboxName;
        d->t = new Transaction( this );
        if ( d->from == imap()->user()->inbox() ) {
            // ensure that nothing's delivered to the renamed inbox,
            // only to the newly created mailbox of the same name.
            Query * q = new Query( "select mailbox from aliases "
                                   "where mailbox=$1 "
                                   "for update", 0 );
            q->bind( 1, imap()->user()->inbox()->id() );
            d->t->enqueue( q );
            inboxName = d->from->name();
        }

        // 1. the first mailbox
        RenameData::MailboxPair * p = new RenameData::MailboxPair;
        p->from = d->from;
        p->toName = imap()->user()->mailboxName( d->toName );
        p->toParent = Mailbox::closestParent( p->toName );
        d->process( p, 0 );

        if ( !ok() ) {
            d->t->rollback();
            return;
        }

        // 2. for each mailbox, any children it may have.
        List<RenameData::MailboxPair>::Iterator it( d->renames );
        while ( it ) {
            Mailbox * m = it->from;
            List<Mailbox>::Iterator c( m->children() );
            while ( c ) {
                p = new RenameData::MailboxPair;
                p->from = c;
                p->toName =
                    it->toName + c->name().mid( it->from->name().length() );
                p->toParent = Mailbox::closestParent( p->toName );
                if ( !( c->synthetic() || c->deleted() ) )
                    d->process( p, it );
                if ( !ok() ) {
                    d->t->rollback();
                    return;
                }
                ++c;
            }
            ++it;
        }

        if ( ok() && d->from == imap()->user()->inbox() ) {
            Query * q =
                new Query( "update aliases set "
                           "mailbox=(select id from mailboxes where name=$1) "
                           "where mailbox=$2", 0 );

            q->bind( 1, inboxName );
            q->bind( 2, d->from->id() );
            d->t->enqueue( q );
            q = new Query( "update mailboxes set deleted='f',owner=$2 "
                           "where name=$1", 0 );
            q->bind( 1, inboxName );
            q->bind( 2, imap()->user()->id() );
            d->t->enqueue( q );
        }
    }

    if ( !ok() )
        return;

    if ( !permitted() ) {
        if ( !ok() )
            d->t->rollback();
        return;
    }

    if ( !permissionChecked() )
        return;

    if ( !d->ready ) {
        List< RenameData::MailboxPair >::Iterator it( d->renames );
        while ( it ) {
            if ( it->from->sessions() ) {
                error( No, "Mailbox is in use: " + it->from->name().ascii() );
                setRespTextCode( "INUSE" );
                break;
            }
            ++it;
        }

        if ( !ok() )
            d->t->rollback();
        else
            d->t->commit();
        d->ready = true;
    }

    if ( !ok() || !d->t->done() )
        return;

    if ( d->t->failed() ) {
        error( No, "Database failure: " + d->t->error() );
        return;
    }

    List< RenameData::MailboxPair >::Iterator it( d->renames );
    while ( it ) {
        Mailbox * to = Mailbox::obtain( it->toName, true );
        Mailbox * from = it->from;
        to->setId( from->id() );
        to->setDeleted( false );
        to->setUidnext( from->uidnext() );
        to->setUidvalidity( it->toUidvalidity );
        from->setId( 0 );
        from->refresh()->execute();
        OCClient::send( "mailbox " + to->name().utf8().quoted() + " new" );
        if ( from == imap()->user()->inbox() ) {
            OCClient::send( "mailbox " +
                            from->name().utf8().quoted() + " new" );
        }
        else {
            from->setDeleted( true );
            OCClient::send( "mailbox " +
                            from->name().utf8().quoted() + " deleted" );
        }
        ++it;
        if ( !it ) {
            d->renames.clear();
            imap()->user()->refresh( this );
        }
    }
    if ( !imap()->user()->exists() )
        return;

    finish();
}
