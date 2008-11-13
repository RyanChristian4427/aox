// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#include "selector.h"

#include "utf.h"
#include "dict.h"
#include "flag.h"
#include "date.h"
#include "session.h"
#include "mailbox.h"
#include "stringlist.h"
#include "annotation.h"
#include "fieldname.h"
#include "dbsignal.h"
#include "field.h"
#include "user.h"
#include "configuration.h"
#include "allocator.h"

#include <time.h> // whereAge() calls time()


static bool tsearchAvailable = false;
static bool retunerCreated = false;

static String * tsconfig;


class TuningDetector
    : public EventHandler
{
public:
    TuningDetector(): q( 0 ) {
        ::tsearchAvailable = false;
        q = new Query(
            "select indexdef from pg_indexes where "
            "indexdef ilike '% USING gin (to_tsvector%'"
            "and tablename='bodyparts' and schemaname=$1",
            this
        );
        q->bind( 1, Configuration::text( Configuration::DbSchema ) );
        q->execute();
    }
    void execute() {
        if ( !q->done() )
            return;
        ::tsearchAvailable = q->hasResults();
        Row * r = q->nextRow();
        if ( r ) {
            String def( r->getString( "indexdef" ) );

            uint n = 12 + def.find( "to_tsvector(" );
            def = def.mid( n, def.length()-n-1 ).section( ",", 1 );

            if ( def[0] == '\'' && def.endsWith( "::regconfig" ) ) {
                tsconfig = new String( def );
                Allocator::addEternal( tsconfig, "tsearch configuration" );
            }
            else {
                ::tsearchAvailable = false;
            }
        }
    }
    Query * q;
};


class RetuningDetector
    : public EventHandler
{
public:
    RetuningDetector(): EventHandler() {
        ::retunerCreated = true;
        setLog( new Log( Log::Server ) );
        (void)new DatabaseSignal( "database_retuned", this );
        (void)new TuningDetector();
    }
    void execute() {
        (void)new TuningDetector();
    }
};


static uint lmatch( const String &, uint, const String &, uint );


class SelectorData
    : public Garbage
{
public:
    SelectorData()
        : f( Selector::NoField ), a( Selector::None ), mboxId( 0 ),
          placeholder( 0 ), join( 0 ), query( 0 ), parent( 0 ),
          children( new List< Selector > ), mm( 0 ),
          session( 0 ),
          needDateFields( false ),
          needAnnotations( false ),
          needBodyparts( false ),
          needMessages( false )
    {}

    void copy( SelectorData * o ) {
        f = o->f;
        a = o->a;
        s8 = o->s8;
        s8b = o->s8b;
        s16 = o->s16;
        s = o->s;
        n = o->n;
        children = o->children;
    }

    Selector::Field f;
    Selector::Action a;

    String error;

    String s8;
    String s8b;
    UString s16;
    MessageSet s;
    uint n;

    uint mboxId;
    int placeholder;
    int join;
    Query * query;

    Selector * parent;
    List< Selector > * children;
    String * mm;
    Session * session;
    User * user;

    StringList extraJoins;

    bool needDateFields;
    bool needAnnotations;
    bool needBodyparts;
    bool needMessages;
};


/*! \class Selector selector.h

    This class represents a set of conditions to select messages from
    a mailbox.

    The Selector class represents a single condition in a search,
    which is either a leaf condition or an AND/OR operator.

    The class can simplify() and regularize itself, such that all
    equivalent search inputs give the same result, and and it can
    express itself in a form amenable to testing. Rather simple.
*/


/*! Creates a new root "And" selector. */

Selector::Selector()
    : d( new SelectorData )
{
    d->a = And;
}


/*! Creates a selector with Field \a f, Action \a a, and the integer
    value \a n.
*/

Selector::Selector( Field f, Action a, uint n )
    : d( new SelectorData )
{
    d->f = f;
    d->a = a;
    d->n = n;
}


/*! Creates a selector with Field \a f, Action \a a, and the string
    value \a s.
*/

Selector::Selector( Field f, Action a, const String &s )
    : d( new SelectorData )
{
    d->f = f;
    d->a = a;
    d->s8 = s;
}


/*! Creates a selector with Field \a f, Action \a a, and the UString
    value \a u.
*/

Selector::Selector( Field f, Action a, const UString &u )
    : d( new SelectorData )
{
    d->f = f;
    d->a = a;
    d->s16 = u;
}


/*! Creates a selector with Field \a f, Action \a a, the String value
    \a s, and the UString value \a u.
*/

Selector::Selector( Field f, Action a, const String &s, const UString &u )
    : d( new SelectorData )
{
    d->f = f;
    d->a = a;
    d->s8 = s;
    d->s16 = u;
}


/*! Creates a selector with Field \a f, Action \a a, the String values
    \a s and \a t, and the UString value \a u.
*/

Selector::Selector( Field f, Action a, const String &s,
                    const String &t, const UString &u )
    : d( new SelectorData )
{
    d->f = f;
    d->a = a;
    d->s8 = s;
    d->s8b = t;
    d->s16 = u;
}


/*! Creates a selector from the MessageSet \a s. */

Selector::Selector( const MessageSet &s )
    : d( new SelectorData )
{
    d->f = Uid;
    d->a = Contains;
    d->s = s;
}


/*! Creates a selector with Action \a a. */

Selector::Selector( Action a )
    : d( new SelectorData )
{
    d->f = NoField;
    d->a = a;
}


/*! Returns the ultimate parent of this Selector. */

const Selector * Selector::root() const
{
    const Selector * p = this;

    while ( p->parent() )
        p = p->parent();

    return p;
}


/*! Returns the parent of this Selector, or 0 if it is the root. */

const Selector * Selector::parent() const
{
    return d->parent;
}


/*! Adds the Selector \a s to the list of this Selector's children. */

void Selector::add( Selector * s )
{
    s->d->parent = this;
    d->children->append( s );
}


/*! Returns the next integer from a monotonically increasing sequence on
    each call. The first value is 1. (This is used to construct the $n
    placeholder names in queries.)
*/

uint Selector::placeHolder()
{
    root()->d->placeholder++;
    return root()->d->placeholder;
}


/*! Records \a s as an error during the construction of this Selector
    tree. Only the first error in a tree is recorded, and it is recorded
    at the root of the tree (rather than the node where it occurred).
*/

void Selector::setError( const String &s )
{
    if ( root()->d->error.isEmpty() )
        root()->d->error = s;
}


/*! This helper transforms this search conditions and all its children
    into a simpler form, if possible. There are three goals to this:

    1. Provide a regular search expression, so that we can eventually
    detect and prepare statements for often-repeated searches.

    2. Ditto, so that we can test that equivalent input gives
    identical output.

    3. Avoid search expressions which would be horribly inefficient or
    just plain impossible for the RDBMS.
*/

void Selector::simplify()
{
    // not (not x) -> x
    if ( d->a == Not && d->children->first()->d->a == Not ) {
        Selector * child = d->children->first()->d->children->first();
        d->copy( child->d );
    }

    if ( d->a == Larger ) {
        if ( d->n == 0 || // > 0 matches everything
             ( d->n == 1 && d->f == Modseq ) ) // all messages have modseq >= 1
            d->a = All;
    }
    else if ( d->a == Contains && d->f == Uid ) {
        if ( d->s.isEmpty() )
            d->a = None; // contains d->a set of nonexistent messages
    }
    else if ( d->a == Contains ) {
        // x contains y may match everything
        switch ( d->f ) {
        case InternalDate:
        case Sent:
            d->a = None;
            break;
        case Header:
            if ( d->s16.isEmpty() && d->s8.isEmpty() )
                d->a = All;
            break;
        case Body:
            if ( d->s16.isEmpty() )
                d->a = All;
            break;
        case Rfc822Size:
            break;
        case Flags:
            if ( d->s8 != "\\recent" && !Flag::id( d->s8 ) )
                d->a = None;
            break;
        case Uid:
            // if s contains all messages or is empty...
            if ( d->s.isEmpty() )
                d->a = None;
            // the All Messages case is harder.
            break;
        case Annotation:
            // can't simplify this
            break;
        case Modseq:
            // contains modseq shouldn't happen, and certainly cannot
            // be simplified
            break;
        case Age:
            // cannot be simplified, should not happen
        case NoField:
            // contains is orthogonal to nofield, so this we cannot
            // simplify
            break;
        }
        // contains empty string too
    }
    else if ( d->a == And ) {
        // zero-element and becomes all, "none and x" becomes none
        List< Selector >::Iterator i( d->children );
        while ( i && d->a == And ) {
            List< Selector >::Iterator p( i );
            ++i;
            p->simplify();
            if ( p->d->a == All )
                d->children->take( p );
            else if ( p->d->a == None )
                d->a = None;
        }
        if ( d->a == And && d->children->isEmpty() )
            d->a = All;

        if ( d->a != And )
            d->children->clear();
    }
    else if ( d->a == Or ) {
        // zero-element or becomes all, "all or x" becomes all
        List< Selector >::Iterator i( d->children );
        while ( i && d->a == Or ) {
            List< Selector >::Iterator p( i );
            ++i;
            p->simplify();
            if ( p->d->a == None )
                d->children->take( p );
            else if ( p->d->a == All )
                d->a = All;
        }
        if ( d->a == And && d->children->isEmpty() )
            d->a = All;

        if ( d->a != Or )
            d->children->clear();
    }
    if ( d->a == All || d->a == None )
        d->f = NoField;

    if ( d->a != And && d->a != Or )
        return;

    // an empty and/or means everything matches
    if ( d->children->isEmpty() ) {
        d->a = All;
        return;
    }

    // or (a or (b c)) -> or (a b c). ditto and.
    if ( d->children ) {
        List< Selector >::Iterator i( d->children );
        while ( i ) {
            List< Selector >::Iterator p( i );
            ++i;
            if ( p->d->a == d->a ) {
                List<Selector>::Iterator c( p->d->children );
                while ( c ) {
                    d->children->prepend( c );
                    ++c;
                }
                d->children->take( p );
            }
        }
    }

    // a single-element and/or can be removed and its argument substituted
    if ( d->children->count() == 1 ) {
        d->copy( d->children->first()->d );
        return;
    }

    // at this point, for proper uniqueness, we ought to sort the
    // children, killing any duplicates in the process. then we'll
    // have a single query for each job. but that can wait. this will
    // do for testing.
}


/*! Returns a query representing this Selector or 0 if anything goes
    wrong, in which case error() contains a description of the problem.
    The Selector is expressed as SQL in the context of the specified
    \a user and \a session. The \a owner will be notified of query
    results.

    The \a mailbox to search is passed in separately, because we can't
    use the Session's mailbox while building views. If \a mailbox is a
    null pointer, the query will search either the entire database or
    the part that's visible to \a user.

    If \a deleted is supplied and true (the default is false), then
    the Query looks at the deleted_messages table instead of the
    mailbox_messages one.

    The search results will be ordered if \a order is true (this is
    the default). The order is ascending and uses whatever is
    specified in \a wanted of mailbox, uid, message and idate.

    Each Query Row will have the result columns named in \a wanted, or
    "uid", "modseq" and "message" if \a wanted is left at the default
    value.
*/

Query * Selector::query( User * user, Mailbox * mailbox,
                         Session * session, EventHandler * owner,
                         bool order, StringList * wanted, bool deleted )
{
    if ( !::retunerCreated && Database::numHandles() )
        (void)new RetuningDetector;

    d->query = new Query( owner );
    d->user = user;
    d->session = session;
    d->placeholder = 0;
    if ( mailbox ) {
        d->mboxId = placeHolder();
        d->query->bind( d->mboxId, mailbox->id() );
    }
    if ( deleted )
        d->mm = new String( "dm" );
    else
        d->mm = new String( "mm" );
    String q = "select " + mm() + ".";
    if ( wanted )
        q.append( wanted->join( ", " + mm() + "." ) );
    else
        q.append( "uid, " + mm() + ".modseq, " + mm() + ".message" );
    if ( deleted )
        q.append( " from deleted_messages " + mm() );
    else
        q.append( " from mailbox_messages " + mm() );
    String w = where();
    if ( d->a == And && w.startsWith( "(" ) && w.endsWith( ")" ) )
        w = w.mid( 1, w.length() - 2 );

    q.append( d->extraJoins.join( "" ) );

    if ( d->needDateFields )
        q.append( " join date_fields df on "
                  "(df.message=" + mm() + ".message)" );
    if ( d->needAnnotations )
        q.append( " join annotations a on (" + mm() + ".mailbox=a.mailbox"
                  " and " + mm() + ".uid=a.uid)" );
    if ( d->needBodyparts )
        q.append( " join part_numbers pn on (pn.message=" + mm() + ".message)"
                  " join bodyparts bp on (bp.id=pn.bodypart)" );
    if ( d->needMessages )
        q.append( " join messages m on (" + mm() + ".message=m.id)" );

    String mboxClause;
    if ( d->mboxId ) {
        // normal case: search one mailbox
        mboxClause = mm() + ".mailbox=$" + fn( d->mboxId );
    }
    else if ( user ) {
        // search all mailboxes accessible to user
        uint owner = placeHolder();
        d->query->bind( owner, user->id() );
        q.append( " join mailboxes mb on (" + mm() + ".mailbox=mb.id)" );
        uint n = placeHolder();
        d->query->bind( n, user->login() );
        mboxClause =
            // I think this one needs commentary.
            "exists "
            // this subselect returns true if either anyone or the named user
            // has the r right for subsubmailbox...
            "(select rights "
            " from permissions"
            " where (identifier='anyone' or identifier=$"+fn(n)+") and"
            "  rights='%r%' and"
            "  mailbox=("
            // this selects the mailbox whose permissions rows
            // applies. that's either the mailbox itself, or the
            // closest parent which has a permissions row.
            "   select mp.id"
            "    from mailboxes mp"
            "    join permssions p on (mp.id=p.mailbox)"
            "    where (p.identifier='anyone' or p.identifier=$"+fn(n)+") and"
            "    (mp.id=mb.id or"
            "     lower(mp.name)||'/'="
            "     lower(substring(mb.name from 0 for length(mp.name)+1)))"
            // use the mailbox which has permissions rows and has the
            // longest name.
            "    order by length(mp.name) desc limit 1))";
    }
    else {
        // search all mailboxes
    }

    if ( mboxClause.isEmpty() && w == "true" ) {
        // no mailbox, no condition. this will result in a large
        // result set. can it be correct?
    }
    else if ( mboxClause.isEmpty() ) {
        // a condition that applies to all mailboxes
        q.append( " where " );
        q.append( w );
    }
    else if ( w == "true" ) {
        // a mailbox clause, but no condition
        q.append( " where " );
        q.append( mboxClause );
    }
    else {
        // both.
        q.append( " where " );
        q.append( mboxClause );
        q.append( " and " );
        q.append( w );
    }

    if ( order ) {
        if ( wanted->contains( "uid" ) && wanted->contains( "mailbox" ) )
            q.append( " order by " + mm() + ".mailbox, " + mm() + ".uid" );
        else if ( wanted->contains( "uid" ) || !wanted )
            q.append( " order by " + mm() + ".uid" );
        else if ( wanted->contains( "message" ) )
            q.append( " order by " + mm() + ".message" );
        else if ( wanted->contains( "idate" ) )
            q.append( " order by " + mm() + ".idate" );
    }

    if ( d->needBodyparts )
        d->query->allowSlowness();

    d->query->setString( q );
    return d->query;
}


/*! Gives an SQL string representing this condition.

    The string may include $n placeholders; where() and its helpers
    will bind them as required.
*/

String Selector::where()
{
    switch( d->f ) {
    case InternalDate:
        return whereInternalDate();
        break;
    case Sent:
        return whereSent();
        break;
    case Header:
        if ( d->s8.isEmpty() )
            return whereHeader();
        else
            return whereHeaderField();
        break;
    case Body:
        return whereBody();
        break;
    case Rfc822Size:
        return whereRfc822Size();
        break;
    case Flags:
        return whereFlags();
        break;
    case Uid:
        return whereUid();
        break;
    case Annotation:
        return whereAnnotation();
        break;
    case Modseq:
        return whereModseq();
    case Age:
        return whereAge();
    case NoField:
        return whereNoField();
        break;
    }
    setError( "Internal error for " + debugString() );
    return "";
}

/*! This implements the INTERNALDATE part of where().
*/

String Selector::whereInternalDate()
{
    uint day = d->s8.mid( 0, 2 ).number( 0 );
    String month = d->s8.mid( 3, 3 );
    uint year = d->s8.mid( 7 ).number( 0 );
    // XXX: local time zone is ignored here
    Date d1;
    d1.setDate( year, month, day, 0, 0, 0, 0 );
    Date d2;
    d2.setDate( year, month, day, 23, 59, 59, 0 );

    if ( d->a == OnDate ) {
        uint n1 = placeHolder();
        root()->d->query->bind( n1, d1.unixTime() );
        uint n2 = placeHolder();
        root()->d->query->bind( n2, d2.unixTime() );
        return "(" + mm() + ".idate>=$" + fn( n1 ) +
            " and " + mm() + ".idate<=$" + fn( n2 ) + ")";
    }
    else if ( d->a == SinceDate ) {
        uint n1 = placeHolder();
        root()->d->query->bind( n1, d1.unixTime() );
        return mm() + ".idate>=$" + fn( n1 );
    }
    else if ( d->a == BeforeDate ) {
        uint n2 = placeHolder();
        root()->d->query->bind( n2, d2.unixTime() );
        return mm() + ".idate<=$" + fn( n2 );
    }

    setError( "Cannot search for: " + debugString() );
    return "";
}


/*! This implements the SENTON/SENTBEFORE/SENTSINCE part of where().
*/

String Selector::whereSent()
{
    root()->d->needDateFields = true;

    uint day = d->s8.mid( 0, 2 ).number( 0 );
    String month = d->s8.mid( 3, 3 );
    uint year = d->s8.mid( 7 ).number( 0 );

    Date d1;
    d1.setDate( year, month, day, 0, 0, 0, 0 );
    uint n = placeHolder();

    if ( d->a == OnDate ) {
        d1.setDate( year, month, day, 23, 59, 59, 0 );
        root()->d->query->bind( n, d1.isoDate() + " " + d1.isoTime() );

        uint n2 = placeHolder();
        d1.setDate( year, month, day, 0, 0, 0, 0 );
        root()->d->query->bind( n2, d1.isoDate() );

        return "(df.value<=$" + fn( n ) + " and"
               " df.value>=$" + fn( n2 ) + ")";
    }
    else if ( d->a == SinceDate ) {
        root()->d->query->bind( n, d1.isoDate() );
        return "df.value>=$" + fn( n );
    }
    else if ( d->a == BeforeDate ) {
        root()->d->query->bind( n, d1.isoDate() );
        return "df.value<=$" + fn( n );
    }

    setError( "Cannot search for: " + debugString() );
    return "";
}


static String matchAny( int n )
{
    return "'%'||$" + fn( n ) + "||'%'";
}


static String q( const UString & orig )
{
    Utf8Codec c;
    String r( c.fromUnicode( orig ) );

    String s;
    uint i = 0;
    while ( i < r.length() ) {
        if ( r[i] == '\\' || r[i] == '_' || r[i] == '%' )
            s.append( '\\' );
        s.append( r[i] );
        i++;
    }

    return s;
}


/*! This implements searches on a single header field.
*/

String Selector::whereHeaderField()
{
    d->s8 = d->s8.headerCased();

    uint f = 1;
    while ( f <= HeaderField::LastAddressField &&
            HeaderField::fieldName( (HeaderField::Type)f ) != d->s8 )
        f++;
    if ( f <= HeaderField::LastAddressField )
        return whereAddressField( d->s8 );

    uint t = FieldName::id( d->s8 );
    if ( !t )
        t = HeaderField::fieldType( d->s8 );

    String jn = fn( ++root()->d->join );
    String j = " left join header_fields hf" + jn +
               " on (" + mm() + ".message=hf" + jn + ".message";
    if ( !d->s16.isEmpty() ) {
        uint like = placeHolder();
        root()->d->query->bind( like, q( d->s16 ) );
        j.append( " and hf" + jn + ".value ilike " + matchAny( like ) );
    }
    if ( t ) {
        j.append( " and hf" + jn + ".field=" );
        j.append( fn( t ) );
    }
    else {
        uint f = placeHolder();
        root()->d->query->bind( f, d->s8 );
        j.append( " and hf" + jn + ".field="
                  "(select id from field_names where name=$" + fn(f) + ")" );
    }
    j.append( ")" );
    root()->d->extraJoins.append( j );

    return "hf" + jn + ".field is not null";
}


/*! This implements searches on the single address field \a field, or
    on all address fields if \a field is empty.
*/

String Selector::whereAddressField( const String & field )
{
    StringList l;
    if ( !field.isEmpty() )
        l.append( field );
    return whereAddressFields( l, d->s16 );
}


/*! This implements searching for \a name on the address \a fields, or
  on all address fields if \a fields is the empty list.
*/

String Selector::whereAddressFields( const StringList & fields,
                                     const UString & name )
{
    Query * query = root()->d->query;
    uint join = ++root()->d->join;
    String jn = fn( join );
    String r( " left join address_fields af" + jn +
              " on (af" + jn + ".message=" + mm() + ".message)"
              " left join addresses a" + jn +
              " on (a" + jn + ".id=af" + jn + ".address"
              " and " );

    StringList known, unknown;
    StringList::Iterator it( fields );
    while ( it ) {
        uint fnum = placeHolder();
        uint t = FieldName::id( *it );
        if ( !t ) {
            t = HeaderField::fieldType( *it );
            if ( t == HeaderField::Other )
                t = 0;
        }
        if ( t ) {
            known.append( "af" + jn + ".field=$" + fn( fnum ) );
            query->bind( fnum, t );
        }
        else {
            unknown.append( "fn.name=$" + fn( fnum ) );
            query->bind( fnum, *it );
        }
        ++it;
    }
    if ( !unknown.isEmpty() ) {
        String tmp = "af" + jn + ".field in "
                     "(select id from field_names fn where ";
        if ( unknown.count() == 1 ) {
            tmp.append( unknown.join( "" ) );
        }
        else {
            tmp.append( "(" );
            tmp.append( unknown.join( " or " ) );
            tmp.append( ")" );
        }
        known.append( tmp );
    }
    if ( known.count() == 1 ) {
        r.append( known.join( "" ) );
        r.append( " and " );
    }
    else if ( !known.isEmpty() ) {
        r.append( "(" );
        r.append( known.join( " or " ) );
        r.append( ") and " );
    }

    String raw( q( name ) );
    int at = raw.find( '@' );

    if ( at < 0 ) {
        uint name = placeHolder();
        query->bind( name, raw );
        r.append( "(a" + jn + ".name ilike " + matchAny( name ) + " or"
                  " a" + jn + ".localpart ilike " + matchAny( name ) + " or"
                  " a" + jn + ".domain ilike " + matchAny( name ) + ")" );
    }
    else {
        String lc, dc;
        if ( at > 0 ) {
            uint lp = placeHolder();
            if ( raw.startsWith( "<" ) ) {
                query->bind( lp, raw.mid( 1, at-1 ).lower() );
                lc = "lower(a" + jn + ".localpart)=$" + fn( lp );
            }
            else {
                query->bind( lp, raw.mid( 0, at ) );
                lc = "a" + jn + ".localpart ilike '%'||$" + fn( lp ) + " ";
            }
        }
        if ( at < (int)raw.length() - 1 ) {
            uint dom = placeHolder();
            if ( raw.endsWith( ">" ) ) {
                query->bind( dom, raw.mid( at+1, raw.length()-at-2 ).lower() );
                dc = "lower(a" + jn + ".domain)=$" + fn( dom );
            }
            else {
                query->bind( dom, raw.mid( at+1 ) );
                dc = "a" + jn + ".domain ilike $" + fn( dom ) + "||'%'";
            }
        }
        if ( !lc.isEmpty() && !dc.isEmpty() ) {
            bool paren = true;
            if ( r.isEmpty() )
                paren = false;
            if ( paren )
                r.append( "(" );
            r.append( lc );
            r.append( " and " );
            r.append( dc );
            if ( paren )
                r.append( ")" );
        }
        else if ( !lc.isEmpty() ) {
            r.append( lc );
        }
        else if ( !dc.isEmpty() ) {
            r.append( dc );
        }
        else {
            // imap SEARCH FROM "@" matches messages with a nonempty
            // from field. the sort of thing only a test suite would
            // do.
            r.truncate( r.length() - 5 ); // " and "
        }
    }
    r.append( ")" );
    root()->d->extraJoins.append( r );
    return "a" + jn + ".id is not null";
}


/*! This implements searches on all header fields.
*/

String Selector::whereHeader()
{
    if ( d->s16.isEmpty() )
        return "true"; // there _is_ at least one header field ;)

    uint like = placeHolder();
    root()->d->query->bind( like, q( d->s16 ) );
    String jn = "hf" + fn( ++root()->d->join );
    String j = " left join header_fields " + jn +
               " on (" + mm() + ".message=" + jn + ".message and " +
               jn + ".value ilike " + matchAny( like ) + ")";
    root()->d->extraJoins.append( j );
    return "(" + jn + ".field is not null or " +
        whereAddressField() + ")";
}


static String matchTsvector( const String & col, uint n )
{
    String s( "length(" );
    s.append( col );
    s.append( ")<1024*1024 and to_tsvector(" );
    s.append( *tsconfig );
    s.append( ", " );
    s.append( col );
    s.append( ") @@ plainto_tsquery($" );
    s.append( fn( n ) );
    s.append( ")" );
    return s;
}


/*! This implements searches on (text) bodyparts. We cannot and will
    not do "full-text" search on the contents of e.g. jpeg
    pictures. (For some formats we search on the text part, because
    the injector sets bodyparts.text based on bodyparts.data.)

    This function uses full-text search if available, but filters the
    results with a plain 'ilike' in order to avoid overly liberal
    stemming. (Perhaps we actually want liberal stemming. I don't
    know. IMAP says not to do it, but do we listen?)
*/

String Selector::whereBody()
{
    root()->d->needBodyparts = true;

    String s;

    uint bt = placeHolder();
    root()->d->query->bind( bt, q( d->s16 ) );

    if ( ::tsearchAvailable )
        s.append( "(" + matchTsvector( "bp.text", bt ) + " "
                  "and bp.text ilike " + matchAny( bt ) + ")" );
    else
        s.append( "bp.text ilike " + matchAny( bt ) );

    return s;
}


/*! This implements searches on the rfc822size of messages.
*/

String Selector::whereRfc822Size()
{
    root()->d->needMessages = true;
    uint s = placeHolder();
    root()->d->query->bind( s, d->n );
    if ( d->a == Smaller )
        return "m.rfc822size<$" + fn( s );
    else if ( d->a == Larger )
        return "m.rfc822size>$" + fn( s );
    setError( "Internal error: " + debugString() );
    return "";
}


/*! This implements searches on whether a message has/does not have
    flags.
*/

String Selector::whereFlags()
{
    if ( d->s8 == "\\recent" ) {
        if ( !root()->d->session )
            return "false";
        // the database cannot look at the recent flag, so we turn
        // this query into a test for the relevant UIDs.
        return whereSet( root()->d->session->recent() );
    }

    uint join = ++root()->d->join;
    String n = fn( join );

    String j;
    uint fid = Flag::id( d->s8 );
    if ( fid ) {
        // we know this flag, so look for it reasonably efficiently
        j = " left join flags f" + n +
            " on (" + mm() + ".mailbox=f" + n + ".mailbox and " +
            mm() + ".uid=f" + n + ".uid and "
            "f" + n + ".flag=" + fn( fid ) + ")";
    }
    else {
        // just in case the cache is out of date we look in the db
        uint b = placeHolder();
        root()->d->query->bind( b, d->s8.lower() );
        j = " left join flags f" + n +
            " on (" + mm() + ".mailbox=f" + n + ".mailbox and " +
            mm() + ".uid=f" + n + ".uid and f" + n + ".flag="
            "(select id from flag_names where lower(name)=$" + fn(b) + "))";
    }
    root()->d->extraJoins.append( j );

    // finally use the join in a manner which doesn't accidentally
    // confuse different flags.
    return "f" + n + ".flag is not null";
}


/*! Returns a condition to match the numbers in \a s. Binds 0-2
    variables.
*/

String Selector::whereSet( const MessageSet & s )
{
    if ( s.isEmpty() )
        return "false";

    uint u = placeHolder();
    uint c = s.count();

    if ( c > 2 ) {
        root()->d->query->bind( u, s );
        return mm() + ".uid=any($" + fn( u ) + ")";
    }
    
    if ( c == 2 ) {
        uint u2 = placeHolder();
        root()->d->query->bind( u, s.smallest() );
        root()->d->query->bind( u2, s.largest() );
        return "(" + mm() + ".uid=$" + fn( u ) +
            " or " + mm() + ".uid=$" + fn( u2 ) + ")";
    }

    root()->d->query->bind( u, s.smallest() );
    return mm() + ".uid=$" + fn( u );
}


/*! This implements searches on whether a message has the right UID.
*/

String Selector::whereUid()
{
    return whereSet( d->s );
}


/*! This implements searches on whether a message has/does not have
    the right annotation.
*/

String Selector::whereAnnotation()
{
    root()->d->needAnnotations = true;

    uint id( AnnotationName::id( d->s8 ) );

    String annotations;
    if ( id ) {
        annotations = "a.name=" + fn( id );
    }
    else {
        // XXX: Use ANY($1) here.
        uint n = 0;
        uint u = 1;
        while ( u <= ::AnnotationName::largestId() ) {
            String a( AnnotationName::name( u ) );
            if ( lmatch( d->s8, 0, a, 0 ) == 2 ) {
                n++;
                if ( !annotations.isEmpty() )
                    annotations.append( " or " );
                annotations.append( "a.name=" );
                annotations.append( fn( u ) );
            }
            u++;
        }
        if ( n > 1 )
            annotations = "(" + annotations + ")";
        if ( ( n < 1 || n > 3 ) && d->s8.find( '%' ) < 0 ) {
            // if we don't know the desired annotation or there seems
            // to be many possibles, we're better off using set logic.
            uint pattern = placeHolder();
            annotations = "a.name in ("
                          "select id from annotation_names where name like $" +
                          fn( pattern ) +
                          ")";
            String sql = 0;
            uint i = 0;
            while ( i < d->s8.length() ) {
                if ( d->s8[i] == '*' )
                    sql.append( '%' );
                else
                    sql.append( d->s8[i] );
                i++;
            }
            root()->d->query->bind( pattern, sql );
        }
        // this still leaves a bad case - e.g. if the client searches
        // for '/vendor/microsoft/%' and we don't have any suitable
        // annotation names in RAM.
    }

    String user;
    String attribute;
    if ( d->s8b.endsWith( ".priv" ) ) {
        attribute = d->s8b.mid( 0, d->s8b.length()-5 ).lower();
        uint userId = placeHolder();
        user = "a.owner=$" + fn( userId );
        root()->d->query->bind( userId, root()->d->user->id() );
    }
    else if ( d->s8b.endsWith( ".shared" ) ) {
        attribute = d->s8b.mid( 0, d->s8b.length()-7 ).lower();
        user = "a.owner is null";
    }
    else {
        attribute = d->s8b.lower();
        uint userId = placeHolder();
        user = "(a.owner is null or a.owner=$" + fn( userId ) + ")";
        root()->d->query->bind( userId, root()->d->user->id() );
    }

    String like = "is not null";
    if ( !d->s16.isEmpty() ) {
        uint i = placeHolder();
        root()->d->query->bind( i, q( d->s16 ) );
        like = "ilike " + matchAny( i );
    }

    return "(" + user + " and " + annotations + " and value " + like + ")";
}


/*! This implements the modseq search-key. */

String Selector::whereModseq()
{
    uint i = placeHolder();
    root()->d->query->bind( i, d->n );

    if (action() == Larger )
        return mm() + ".modseq>=$" + fn( i );
    else if ( action() == Smaller )
        return mm() + ".modseq<$" + fn( i );

    log( "Bad selector", Log::Error );
    return "false";
}


/*! This implements the older/younger search-keys. */

String Selector::whereAge()
{
    uint i = placeHolder();
    root()->d->query->bind( i, (uint)::time( 0 ) - d->n );
    if ( d->a == Larger )
        return mm() + ".idate<=$" + fn( i );
    return mm() + ".idate>=$" + fn( i );
}


static bool isAddressField( const String & s )
{
    uint t = HeaderField::fieldType( s );
    if ( t > 0 && t <= HeaderField::LastAddressField )
        return true;
    return false;
}


/*! This implements any search that's not bound to a specific field,
    generally booleans and "all".

    As a hack, oops, as an optimization, this function also looks for
    an OR list of address-field searches, and if any, lifts the shared
    parts of those searches out so the DBMS processes the search
    faster.
*/

String Selector::whereNoField()
{
    if ( d->a == And || d->a == Or ) {
        if ( d->children->isEmpty() ) {
            if ( d->a == And )
                return "true";
            return "false";
        }
        StringList conditions;
        UString address;
        StringList addressFields;
        if ( d->a == Or ) {
            List<Selector>::Iterator i( d->children );
            while ( i && ( i->d->f != Header || !isAddressField( i->d->s8 ) ) )
                i++;
            if ( i )
                address = i->d->s16; // this is the address we optimize for
        }
        bool t = false;
        bool f = false;
        List<Selector>::Iterator i( d->children );
        while ( i ) {
            if ( d->a == Or &&
                 i->d->f == Header &&
                 !address.isEmpty() &&
                 isAddressField( i->d->s8 ) &&
                 address == i->d->s16 ) {
                addressFields.append( i->d->s8.headerCased() );
            }
            else {
                String w = i->where();
                if ( w == "true" )
                    t = true;
                else if ( w == "false" )
                    f = true;
                else
                    conditions.append( w );
            }
            i++;
        }
        if ( !addressFields.isEmpty() ) {
            addressFields.removeDuplicates();
            if ( addressFields.count() == 12 )
                addressFields.clear(); // there are only 12, so look for all
            conditions.append( whereAddressFields( addressFields, address ) );
        }
        String r = "(";
        if ( d->a == And ) {
            if ( f )
                return "false";
            r.append( conditions.join( " and " ) );
        }
        else {
            if ( t )
                return "false";
            r.append( conditions.join( " or " ) );
        }
        r.append( ")" );
        return r;
    }
    else if ( d->a == Not ) {
        String c = d->children->first()->where();
        if ( c == "true" )
            return "false";
        else if ( c == "false" )
            return "true";
        else if ( c.endsWith( " is not null" ) )
            return c.mid( 0, c.length() - 8 ) + "null";
        return "not " + c;
    }
    else if ( d->a == All ) {
        return "true";
    }
    else if ( d->a == None ) {
        return "false";
    }
    setError( "Internal error: " + debugString() );
    return "";
}


/*! Give an ASCII representatation of this object, suitable for debug
    output or for equality testing.
*/

String Selector::debugString() const
{
    String r;

    String o, w;

    switch ( d->a ) {
    case OnDate:
        o = "on";
        break;
    case SinceDate:
        o = "since";
        break;
    case BeforeDate:
        o = "before";
        break;
    case Contains:
        o = "contains";
        break;
    case Larger:
        o = "larger";
        break;
    case Smaller:
        o = "smaller";
        break;
    case And:
    case Or:
        break;
    case Not:
        return "not " + d->children->first()->debugString();
    case All:
        return "all";
        break;
    case None:
        return "none";
        break;
    };

    if ( o.isEmpty() ) {
        r = "(";
        List< Selector >::Iterator i( d->children );
        while ( i ) {
            r += i->debugString();
            ++i;
            if ( i ) {
                if ( d->a == And )
                    r += " and ";
                else
                    r += " or ";
            }
        }
        r += ")";
        return r;
    }

    switch( d->f ) {
    case InternalDate:
        w = "delivery";
        break;
    case Sent:
        w = "sent";
        break;
    case Header:
        if ( d->s8.isEmpty() )
            w = "header";
        else
            w = "header field " + d->s8;
        break;
    case Body:
        w = "body";
        break;
    case Rfc822Size:
        w = "rfc822 size";
        break;
    case Flags:
        w = "set of flags";
        break;
    case NoField:
        w = "none";
        break;
    case Uid:
        return d->s.set();
        break;
    case Annotation:
        w = "annotation " + d->s8b + " of ";
        break;
    case Modseq:
        w = "modseq";
        break;
    case Age:
        w = "age";
    };

    r = w + " " + o + " ";
    if ( d->n )
        r.append( fn( d->n ) );
    else if ( d->s16.isEmpty() )
        r.append( d->s8 );
    else
        r.append( d->s16.ascii() );

    return r;

}


/*! Matches the message with the given \a uid in the session \a s
    against this condition, provided the match is reasonably simple and
    quick, and returns either Yes, No, or (if the match is difficult,
    expensive or depends on data that isn't available) Punt.
*/

Selector::MatchResult Selector::match( Session * s, uint uid )
{
    if ( d->a == And || d->a == Or ) {
        List< Selector >::Iterator i( d->children );
        while ( i ) {
            MatchResult sub = i->match( s, uid );
            if ( sub == Punt )
                return Punt;
            if ( d->a == And && sub == No )
                return No;
            if ( d->a == Or && sub == Yes )
                return Yes;
            ++i;
        }
        if ( d->a == And )
            return Yes;
        else
            return No;
    }
    else if ( d->a == Contains && d->f == Uid ) {
        if ( d->s.contains( uid ) )
            return Yes;
        return No;
    }
    else if ( d->a == Contains && d->f == Flags ) {
        if ( d->s8 == "\\recent" ) {
            if ( s->isRecent( uid ) )
                return Yes;
            return No;
        }
        return Punt;
    }
    else if ( d->a == Not ) {
        MatchResult sub = d->children->first()->match( s, uid );
        if ( sub == Punt )
            return Punt;
        else if ( sub == Yes )
            return No;
        else
            return Yes;
    }
    else if ( d->a == All ) {
        return Yes;
    }

    return Punt;
}


/*! Returns true if this condition needs an updated Session to be
    correctly evaluated, and false if not.
*/

bool Selector::needSession() const
{
    if ( d->a == Contains && d->f == Flags && d->s8 == "\\recent" )
        return true;

    if ( d->a == And || d->a == Or ) {
        List< Selector >::Iterator i( d->children );
        while ( i ) {
            if ( i->needSession() )
                return true;
            ++i;
        }
    }
    return false;
}


static uint lmatch( const String & pattern, uint p,
                    const String & name, uint n )
{
    uint r = 0;
    while ( p <= pattern.length() ) {
        if ( pattern[p] == '*' || pattern[p] == '%' ) {
            bool star = false;
            while ( pattern[p] == '*' || pattern[p] == '%' ) {
                if ( pattern[p] == '*' )
                    star = true;
                p++;
            }
            uint i = n;
            if ( star )
                i = name.length();
            else
                while ( i < name.length() && name[i] != '/' )
                    i++;
            while ( i >= n ) {
                uint s = lmatch( pattern, p, name, i );
                if ( s == 2 )
                    return 2;
                if ( s == 1 )
                    r = 1;
                i--;
            }
        }
        else if ( p == pattern.length() && n == name.length() ) {
            // ran out of pattern and name at the same time. success.
            return 2;
        }
        else if ( pattern[p] == name[n] ) {
            // nothing. proceed.
            p++;
        }
        else if ( pattern[p] == '/' && n == name.length() ) {
            // we ran out of name and the pattern wants a child.
            return 1;
        }
        else {
            // plain old mismatch.
            return r;
        }
        n++;
    }
    return r;
}


/*! Returns a string representing the number (without $) of the
    placeholder that's bound to the mbox id.
*/

String Selector::mboxId()
{
    return fn( root()->d->mboxId );
}


/*! Returns the string representation of this Selector. This is what's
    stored in the views.selector column in the database. */

String Selector::string()
{
    Utf8Codec u;
    String r( "(" );

    switch ( d->a ) {
    case OnDate:
        if ( d->f == InternalDate )
            r.append( "received" );
        else if ( d->f == Sent )
            r.append( "sent" );
        r.append( "on" );
        r.append( " " );
        r.append( d->s8.quoted() );
        break;
    case SinceDate:
        if ( d->f == InternalDate )
            r.append( "received" );
        else if ( d->f == Sent )
            r.append( "sent" );
        r.append( "since" );
        r.append( " " );
        r.append( d->s8.quoted() );
        break;
    case BeforeDate:
        if ( d->f == InternalDate )
            r.append( "received" );
        else if ( d->f == Sent )
            r.append( "sent" );
        r.append( "before" );
        r.append( " " );
        r.append( d->s8.quoted() );
        break;
    case Contains:
        if ( d->f == Header ) {
            r.append( "header" );
            r.append( " " );
            r.append( d->s8.quoted() );
            r.append( " " );
            r.append( u.fromUnicode( d->s16 ).quoted() );
        }
        else if ( d->f == Body ) {
            r.append( "body" );
            r.append( " " );
            r.append( u.fromUnicode( d->s16 ).quoted() );
        }
        else if ( d->f == Flags ) {
            r.append( "flag" );
            r.append( " " );
            r.append( d->s8.quoted() );
        }
        else if ( d->f == Uid ) {
            r.append( "messageset" );
            r.append( " " );
            r.append( d->s.set().quoted() );
        }
        else if ( d->f == Annotation ) {
            r.append( "annotation" );
            r.append( " " );
            r.append( d->s8.quoted() );
            r.append( " " );
            r.append( d->s8b.quoted() );
            r.append( " " );
            r.append( u.fromUnicode( d->s16 ).quoted() );
        }
        break;
    case Larger:
        if ( d->f == Modseq )
            r.append( "modseqlarger" );
        else
            r.append( "messagelarger" );
        r.append( " " );
        r.append( fn( d->n ) );
        break;
    case Smaller:
        r.append( "messagesmaller" );
        r.append( " " );
        r.append( fn( d->n ) );
        break;
    case And:
        r.append( "and" );
        break;
    case Or:
        r.append( "or" );
        break;
    case Not:
        r.append( "not" );
        break;
    case All:
        r.append( "true" );
        break;
    case None:
        r.append( "false" );
        break;
    }

    List< Selector >::Iterator it( d->children );
    while ( it ) {
        r.append( " " );
        r.append( it->string() );
        ++it;
    }

    r.append( ")" );
    return r;
}


/*! Returns the first error recorded with setError, or an empty string
    if none has been recorded yet.
*/

String Selector::error()
{
    return root()->d->error;
}


/*! This static function takes a canonical string representation \a s,
    and returns the Selector corresponding to it, or 0 if there was a
    parsing error.
*/

Selector * Selector::fromString( const String &s )
{
    Selector * r = new Selector;

    uint i = 0;

    if ( s[i++] != '(' )
        return 0;

    String op;
    while ( s[i] <= 'z' && s[i] >= 'a' )
        op.append( s[i++] );

    if ( op == "and" || op == "or" || op == "not" ) {
        if ( op == "and" )
            r->d->a = And;
        else if ( op == "or" )
            r->d->a = Or;
        else if ( op == "not" )
            r->d->a = Not;

        while ( s[i] == ' ' ) {
            i++;

            uint j = i;
            if ( s[i++] != '(' )
                return 0;

            int parenLevel = 1;
            while ( parenLevel > 0 && i < s.length() ) {
                if ( s[i] == '"' ) {
                    i++;
                    while ( s[i] != '"' && i < s.length() ) {
                        if ( s[i] == '\\' )
                            i++;
                        i++;
                    }
                    if ( s[i] != '"' )
                        return 0;
                }
                else if ( s[i] == '(' ) {
                    parenLevel++;
                }
                else if ( s[i] == ')' ) {
                    parenLevel--;
                }
                i++;
            }
            if ( parenLevel != 0 )
                return 0;

            Selector * child = fromString( s.mid( j, i-j ) );
            if ( !child )
                return 0;
            child->d->parent = r;
            r->d->children->append( child );
        }

        if ( r->d->children->isEmpty() ||
             ( op == "not" && r->d->children->count() != 1 ) )
            return 0;
    }
    else if ( op == "receivedon" || op == "senton" ||
              op == "receivedsince" || op == "sentsince" ||
              op == "receivedbefore" || op == "sentbefore" )
    {
        if ( op.endsWith( "on" ) )
            r->d->a = OnDate;
        else if ( op.endsWith( "since" ) )
            r->d->a = SinceDate;
        else
            r->d->a = BeforeDate;

        if ( op.startsWith( "received" ) )
            r->d->f = InternalDate;
        else
            r->d->f = Sent;

        if ( s[i++] != ' ' )
            return 0;

        uint j = i;
        if ( s[i++] != '"' )
            return 0;
        while ( s[i] != '"' && i < s.length() ) {
            if ( s[i] == '\\' )
                i++;
            i++;
        }
        if ( s[i++] != '"' )
            return 0;

        r->d->s8 = s.mid( j, i-j ).unquoted();
    }
    else if ( op == "header" || op == "body" || op == "flag" ||
              op == "messageset" || op == "annotation" )
    {
        r->d->a = Contains;

        if ( op == "header" )
            r->d->f = Header;
        else if ( op == "body" )
            r->d->f = Body;
        else if ( op == "flag" )
            r->d->f = Flags;
        else if ( op == "messageset" )
            r->d->f = Uid;
        else if ( op == "annotation" )
            r->d->f = Annotation;

        if ( r->d->f != Body ) {
            if ( s[i++] != ' ' )
                return 0;

            uint j = i;
            if ( s[i++] != '"' )
                return 0;
            while ( s[i] != '"' && i < s.length() ) {
                if ( s[i] == '\\' )
                    i++;
                i++;
            }
            if ( s[i++] != '"' )
                return 0;

            String t = s.mid( j, i-j ).unquoted();

            if ( r->d->f == Uid ) {
                StringList * l = StringList::split( ',', t );
                StringList::Iterator it( l );
                while ( it ) {
                    StringList * range = StringList::split( ':', *it );
                    r->d->s.add( range->first()->number( 0 ),
                                 range->last()->number( 0 ) );
                    ++it;
                }
            }
            else {
                r->d->s8 = t;
            }
        }

        if ( r->d->f == Annotation ) {
            if ( s[i++] != ' ' )
                return 0;

            uint j = i;
            if ( s[i++] != '"' )
                return 0;
            while ( s[i] != '"' && i < s.length() ) {
                if ( s[i] == '\\' )
                    i++;
                i++;
            }
            if ( s[i++] != '"' )
                return 0;

            r->d->s8b = s.mid( j, i-j ).unquoted();
        }

        if ( r->d->f == Header || r->d->f == Body ||
             r->d->f == Annotation )
        {
            if ( s[i++] != ' ' )
                return 0;

            uint j = i;
            if ( s[i++] != '"' )
                return 0;
            while ( s[i] != '"' && i < s.length() ) {
                if ( s[i] == '\\' )
                    i++;
                i++;
            }
            if ( s[i++] != '"' )
                return 0;

            Utf8Codec u;
            r->d->s16 = u.toUnicode( s.mid( j, i-j ).unquoted() );
            if ( !u.valid() )
                return 0;
        }
    }
    else if ( op == "messagelarger" || op == "messagesmaller" ) {
        r->d->f = Rfc822Size;
        if ( op.endsWith( "larger" ) )
            r->d->a = Larger;
        else
            r->d->a = Smaller;

        if ( s[i++] != ' ' )
            return 0;

        uint j = i;
        if ( s[i] <= '9' && s[i] >= '1' )
            i++;
        else
            return 0;
        while ( s[i] <= '9' && s[i] >= '0' )
            i++;

        bool ok;
        r->d->n = s.mid( j, i-j ).number( &ok );
        if ( !ok )
            return 0;
    }
    else if ( op == "modseqlarger" ) {
        r->d->f = Modseq;
        r->d->a = Larger;

        if ( s[i++] != ' ' )
            return 0;

        uint j = i;
        if ( s[i] <= '9' && s[i] >= '1' )
            i++;
        else
            return 0;
        while ( s[i] <= '9' && s[i] >= '0' )
            i++;

        bool ok;
        r->d->n = s.mid( j, i-j ).number( &ok );
        if ( !ok )
            return 0;
    }
    else if ( op == "true" ) {
        r->d->a = All;
    }
    else if ( op == "false" ) {
        r->d->a = None;
    }
    else {
        return 0;
    }

    if ( s[i++] != ')' || i < s.length() )
        return 0;

    return r;
}


/*! Returns the field specific using the constructor.

*/

Selector::Field Selector::field() const
{
    return d->f;
}


/*! Returns the action specified using the constructor.

*/

Selector::Action Selector::action() const
{
    return d->a;
}


/*! Returns a reference to the set passed to the constructor. If the
    constructor didn't accept a set, messageSet() returns a reference
    to an empty set.
*/

const MessageSet & Selector::messageSet() const
{
    return d->s;
}


/*! Returns true if this Selector includes at least one dynamic
    message attribute (something which can change after message
    arrival). If dynamic(), then repeating the Selector's query() can
    yield different results. (That is of course the very subject of
    RFC 4551.)
*/

bool Selector::dynamic() const
{
    if ( d->f == Flags || d->f == Annotation || d->f == Modseq ||
         d->f == Age )
        return true;
    List< Selector >::Iterator i( d->children );
    while ( i ) {
        Selector * c = i;
        ++i;
        if ( c->dynamic() )
            return true;
    }
    return false;
}


/*! Returns true if this Selector includes at least one time-sensitive
    message attribute (something which can change as time passes). If
    the Selector is timeSensitive(), it is also dynamic().
*/

bool Selector::timeSensitive() const
{
    if ( d->f == Age )
        return true;
    List< Selector >::Iterator i( d->children );
    while ( i ) {
        Selector * c = i;
        ++i;
        if ( c->timeSensitive() )
            return true;
    }
    return false;
}


/*! Returns true if this Selector includes modseq logic, and false if
    not.
*/

bool Selector::usesModseq() const
{
    if ( d->f == Modseq )
        return true;
    List< Selector >::Iterator i( d->children );
    while ( i ) {
        Selector * c = i;
        ++i;
        if ( c->usesModseq() )
            return true;
    }
    return false;
}


/*! Returns the 8-bit string supplied to some constructors, or an
    empty string if none has been specified.
*/

String Selector::stringArgument() const
{
    return d->s8;
}


/*! Returns the unicode string supplied to some constructors, or an
    empty string if none has been specified.
*/

UString Selector::ustringArgument() const
{
    return d->s16; // it's actually 32-bit, isn't it? who cares.
}


/*! Returns the numeric argument supplied to some constructors, or 0
    if none has been specified.
*/

int Selector::integerArgument() const
{
    return d->n;
}


/*! Returns the message set supplied to some constructors, or an empty
    set if none has been specified.
*/

MessageSet Selector::messageSetArgument() const
{
    return d->s;
}


/*! Returns a pointer to this selector's children (only applicable for
    and/or/not selectors). May return a pointer to an empty list, but
    will never return a null pointer.
*/

List<Selector> * Selector::children()
{
    return d->children;
}


/*! Returns a string such as "mm", referring to the mailbox_messages
    table. This may also be "dm", "dm2", "mm42" or worse, if the
    search is really complex.
*/

String Selector::mm()
{
    Selector * t = this;
    while ( t && !t->d->mm && t->d->parent )
        t = t->d->parent;
    if ( t->d->mm )
        return *t->d->mm;
    return "mm";
}


/*! Performs whatever duties the Selector needs to have performed at
    startup. Selector can be used even without calling setup().
*/

void Selector::setup()
{
    if ( !::retunerCreated )
        (void)new RetuningDetector;
}
