#include "addresscache.h"

#include "arena.h"
#include "scope.h"
#include "event.h"
#include "query.h"
#include "address.h"
#include "dict.h"
#include "list.h"
#include "map.h"


static Arena acArena;
static Map< Address > *idCache;
static Dict< Address > *nameCache;
static PreparedStatement *addressLookup;
static PreparedStatement *addressInsert;
static PreparedStatement *addressInsertId;


/*! \class AddressCache addresscache.h
    This class maintains a cache of the addresses in the database.

    This class is responsible for finding the numeric id of an Address
    object. It may find the id in its in-memory cache, or by issuing a
    SELECT against the addresses table, or, failing that, by inserting
    a new row into the table and retrieving its id.

    (...We want to describe the id-lookups here...)

    (...We need a note about serialised insertions here...)

    (...We need to talk about synchronisation through ocd here...)

    This class is used only by the Injector at present.
*/

/*! This function initialises the cache of Address objects at startup.
    It expects to be called from ::main().
*/

void AddressCache::setup()
{
    Scope x( &acArena );

    // The idCache maps numeric ids to their corresponding Addresses,
    // and the nameCache maps a string representation of each address
    // to the same set of objects.

    idCache = new Map< Address >;
    nameCache = new Dict< Address >;

    // The first query is used to resolve cache misses. If the address
    // doesn't exist in the table, the other two queries insert it and
    // find its id.

    addressLookup =
        new PreparedStatement( "select id from addresses where "
                               "name=$1 and localpart=$2 and domain=$3" );
    addressInsert =
        new PreparedStatement( "insert into addresses(name,localpart,domain) "
                               "values ($1,$2,$3)" );
    addressInsertId =
        new PreparedStatement( "select currval('address_ids')::integer as id" );
}


class AddressLookup
    : public EventHandler
{
protected:
    Query *q;
    Address *address;
    CacheLookup *status;
    EventHandler *owner;
    List< Query > *queries;

public:
    AddressLookup() {}

    AddressLookup( Address *a, List< Query > *l, CacheLookup *st,
                  EventHandler *ev )
        : address( a ), status( st ), owner( ev ), queries( l )
    {
        q = new Query( *addressLookup, this );
        q->bind( 1, a->name() );
        q->bind( 2, a->localpart() );
        q->bind( 3, a->domain() );
        l->append( q );
    }

    void execute();
};


class AddressInsert
    : public AddressLookup
{
public:
    AddressInsert( Address *a, List< Query > *l, CacheLookup *st,
                  EventHandler *ev )
    {
        address = a;
        status = st;
        owner = ev;
        queries = l;

        Query *i = new Query( *addressInsert, this );
        i->bind( 1, a->name() );
        i->bind( 2, a->localpart() );
        i->bind( 3, a->domain() );

        q = new Query( *addressInsertId, this );
        l->append( q );

        Database *db = Database::handle();
        db->enqueue( i );
        db->enqueue( q );
        db->execute();
    }
};


void AddressLookup::execute() {
    if ( !q->done() )
        return;

    Row *r = q->nextRow();
    delete queries->take( queries->find( q ) );

    if ( !r ) {
        // It may be better to collect INSERTs and execute them together
        // on one database handle after processing the SELECTs. We don't
        // bother yet.
        (void)new AddressInsert( address, queries, status, owner );
        return;
    }

    uint id = *r->getInt( "id" );
    address->setId( id );
    {
        Scope x( &acArena );
        Address *a = new Address( address->name(), address->localpart(),
                                  address->domain() );
        a->setId( id );

        idCache->insert( a->id(), a );
        nameCache->insert( a->toString(), a );
    }

    if ( queries->isEmpty() ) {
        status->setState( CacheLookup::Completed );
        owner->notify();
    }
}


/*! This function accepts the List \a l of Address objects, and notifies
    \a ev after it has called Address::setId() for each Address in \a l.
    Cached addresses will be resolved immediately. Uncached ones incur a
    database lookup, and possibly an insert followed by a select, before
    being added to the cache.

    (We assume, for the moment, that none of the queries will fail.)
*/

CacheLookup *AddressCache::lookup( List< Address > *l, EventHandler *ev )
{
    // We step through l, resolving cached addresses, and adding queries
    // for the others to this List:
    List< Query > * lookups = new List< Query >;
    CacheLookup * status = new CacheLookup;

    List< Address >::Iterator it( l->first() );
    while ( it ) {
        Address *a = nameCache->find( it->toString() );

        if ( !a )
            (void)new AddressLookup( it, lookups, status, ev );
        else
            it->setId( a->id() );

        it++;
    }

    if ( lookups->isEmpty() )
        status->setState( CacheLookup::Completed );
    else
        Database::query( lookups );

    return status;
}
