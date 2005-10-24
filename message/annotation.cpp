// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#include "annotation.h"

#include "allocator.h"
#include "string.h"
#include "query.h"
#include "dict.h"
#include "map.h"
#include "log.h"


static Dict<AnnotationName> * annotationNamesByName;
static Map<AnnotationName> * annotationNamesById;
static uint largestAnnotationNameId;


class AnnotationNameFetcherData
    : public Garbage
{
public:
    AnnotationNameFetcherData(): o( 0 ), q( 0 ) {}

    EventHandler * o;
    Query * q;
};


/*! \class AnnotationNameFetcher flag.h

    The AnnotationNameFetcher class fetches all (or some) annotations from the
    database.
*/


/*! Constructs a AnnotationNameFetcher which will proceed to do whatever
    is right and good. If \a owner is not null, the AnnotationNameFetcher
    will notify its \a owner when done.
*/

AnnotationNameFetcher::AnnotationNameFetcher( EventHandler * owner )
    : d( new AnnotationNameFetcherData )
{
    d->o = owner;
    // XXX: the >= in the next line may be an off-by-one. it's
    // harmless, though, since the reader checks whether such a flag
    // exists.
    d->q = new Query( "select id,name from annotation_names "
                      "where id>=$1 order by id",
                      this );
    d->q->bind( 1, ::largestAnnotationNameId );
    d->q->execute();
    if ( ::annotationNamesByName )
        return;
    ::annotationNamesByName = new Dict<AnnotationName>;
    Allocator::addEternal( ::annotationNamesByName,
                           "list of existing annotations" );
    ::annotationNamesById = new Map<AnnotationName>;
    Allocator::addEternal( ::annotationNamesById,
                           "list of existing annotations" );
}


class AnnotationNameData
    : public Garbage
{
public:
    AnnotationNameData() : id( 0 ) {}
    String name;
    uint id;
};


void AnnotationNameFetcher::execute()
{
    Row * r = d->q->nextRow();
    while ( r ) {
        String n = r->getString( "name" );
        uint i = r->getInt( "id" );
        // is this the only AnnotationNameFetcher working now? best to be careful
        AnnotationName * f = AnnotationName::find( i );
        if ( !f )
            f = new AnnotationName( n, i );
        f->d->name = n;
        if ( i > ::largestAnnotationNameId )
            ::largestAnnotationNameId = i;
        r = d->q->nextRow();
    }
    if ( !d->q->done() )
        return;

    if ( d->o )
        d->o->execute();
}



/*! \class AnnotationName flag.h

    The AnnotationName class represents a single message flag, ie. a named
    binary variable that may be set on any Message.

    A AnnotationName has a name() and an integer id(), both of which are
    unique. The id is used to store annotations. There is a function to
    find() a specific flag either by name or id, and also one to get a
    list of all known annotationNames().
*/


/*! Constructs a flag named \a name and with id \a id. Both \a name
    and \a id must be unique.
*/

AnnotationName::AnnotationName( const String & name, uint id )
    : d( new AnnotationNameData )
{
    d->name = name;
    d->id = id;
    if ( !::annotationNamesByName )
        AnnotationName::setup();
    ::annotationNamesByName->insert( name, this );
    ::annotationNamesById->insert( id, this );
}


/*! Returns the name of this flag, as specified to the constructor. */

String AnnotationName::name() const
{
    return d->name;
}


/*! Returns the id of this flag, as specified to the constructor. */

uint AnnotationName::id() const
{
    return d->id;
}


/*! Returns a pointer to the flag named \a name, or a null pointer of
    there isn't one. The comparison is case insensitive.
*/

AnnotationName * AnnotationName::find( const String & name )
{
    if ( !::annotationNamesByName )
        return 0;
    return ::annotationNamesByName->find( name );
}


/*! Returns a pointer to the flag with id \a id, or a null pointer of
    there isn't one.
*/

AnnotationName * AnnotationName::find( uint id )
{
    if ( !::annotationNamesById )
        return 0;
    return ::annotationNamesById->find( id );
}


/*! Initializes the AnnotationName subsystem, fetching all known
    annotationNames from the database.
*/

void AnnotationName::setup()
{
    (void)new AnnotationNameFetcher( 0 );
}


class AnnotationNameCreatorData
    : public Garbage
{
public:
    AnnotationNameCreatorData(): owner( 0 ) {}
    EventHandler * owner;
    List<Query> queries;
};

/*! \class AnnotationNameCreator flag.h

    The AnnotationNameCreator class creates annotations in the database
    and then updates the Annotation index in RAM.

    When created, a AnnotationNameCreator object immediately sends queries
    to insert the necessary rows, and when that is done, it creates a
    AnnotationNameFetcher. Only when the AnnotationNameFetcher is done is the
    owner notified.
*/

/*! Constructs a AnnotationNameCreator which inserts \a annotations in the
    database and notifies \a owner when the insertion is complete,
    both in RAM and in the database.
*/

AnnotationNameCreator::AnnotationNameCreator( EventHandler * owner, const StringList & annotations )
    : d( new AnnotationNameCreatorData )
{
    d->owner = owner;

    StringList::Iterator it( annotations );
    while ( it ) {
        Query * q
            = new Query( "insert into annotation_names (name) values ($1)",
                         this );
        q->bind( 1, *it );
        q->allowFailure();
        q->execute();
        d->queries.append( q );
        ++it;
    }
}


void AnnotationNameCreator::execute()
{
    bool done = true;
    List<Query>::Iterator it( d->queries );
    while ( it && done ) {
        if ( !it->done() )
            done = false;
        ++it;
    }
    if ( done )
        (void)new AnnotationNameFetcher( d->owner );
}


/*! Returns the largest ID yet seen in for an annotation entry name. */

uint AnnotationName::largestId()
{
    return ::largestAnnotationNameId;
}


class AnnotationData
    : public Garbage
{
public:
    AnnotationData(): entryName( 0 ), ownerId( 0 ) {}
    String value;
    String type;
    String language;
    String displayName;
    AnnotationName * entryName;
    uint ownerId;
};


/*! \class Annotation annotation.h
  
    The Annotation class models a single annotation for a message,
    ie. it has an entr name, a value, an owner and associated data.
    The Annotation object doesn't register itself or maintain pointers
    to other objects - it's a simple value.
    Message::replaceAnnotation() and Message::annotations() are the
    main function that use Annotation.
*/



/*!  Constructs an empty Annotation. */

Annotation::Annotation()
    : d( new AnnotationData )
{
    // nothing more necessary
}


/*! Records that the value of this annotation is \a v. The initial
    value is an empty string.
*/

void Annotation::setValue( const String & v )
{
    d->value = v;
}


/*! Returns the annotation's value, as set by setValue(). */

String Annotation::value() const
{
    return d->value;
}


/*! Records that the type of this annotation is \a t. The initial
    value is an empty string, corresponding to
    text/plain;charset=utf-8.
*/

void Annotation::setType( const String & t )
{
    d->type = t;
}


/*! Returns the annotation's type, as set by setType(). */

String Annotation::type() const
{
    return d->type;
}


/*! Records that the content-language of this annotation is \a l. The
    initial value is an empty string.
*/

void Annotation::setLanguage( const String & l )
{
    d->language = l;
}


/*! Returns the annotation's language, as set by setLanguage(). */

String Annotation::language() const
{
    return d->language;
}


/*! Records that the display-name of this annotation is \a n. The
    initial value is an empty string.
*/

void Annotation::setDisplayName( const String & n )
{
    d->displayName = n;
}


/*! Returns the annotation's display-name, as set by setDisplayName(). */

String Annotation::displayname() const
{
    return d->displayName;
}


/*! Records that the entry name of this annotation is \a n. The
    initial value is a null pointer, which is invalid.

    Annotation does not enforce validity.
*/

void Annotation::setEntryName( AnnotationName * n )
{
    d->entryName = n;
}


/*! Returns the annotation's entry name, as set by setEntryName(). */

AnnotationName * Annotation::entryName() const
{
    return d->entryName;
}


/*! Records that the user id owning this annotation is \a id. The
    initial value is 0, corresponding to a shared annotation.
*/

void Annotation::setOwnerId( uint id )
{
    d->ownerId = id;
}


/*! Returns the annotation's owner ID, as set by setOwnerId(). */

uint Annotation::ownerId() const
{
    return d->ownerId;
}
