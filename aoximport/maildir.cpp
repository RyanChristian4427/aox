// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#include "maildir.h"

#include "map.h"
#include "file.h"
#include "integerset.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>


/*! \class MaildirDirectory maildir.h

    Picks out maildir mailboxes (directories containing cur and tmp
    subdirectories) from a DirectoryTree, and hands them out one by
    one to the Migrator.
*/


/*! Constructs an MaildirDirectory for \a path. */

MaildirDirectory::MaildirDirectory( const String &path )
    : DirectoryTree( path )
{
}


bool MaildirDirectory::isMailbox( const String &path, struct stat * st )
{
    if ( !S_ISDIR( st->st_mode ) )
        return false;

    struct stat s;
    String n( path + "/cur" );
    if ( stat( n.cstr(), &s ) < 0 )
        return false;
    if ( !S_ISDIR( s.st_mode ) )
        return false;
    n = path + "/new";
    if ( stat( n.cstr(), &s ) < 0 )
        return false;
    if ( !S_ISDIR( s.st_mode ) )
        return false;

    return true;
}


MigratorMailbox * MaildirDirectory::newMailbox( const String &path, uint n )
{
    return new MaildirMailbox( path, n );
}


class MaildirMailboxData
    : public Garbage
{
public:
    MaildirMailboxData()
        : opened( false )
    {}

    bool opened;
    String path;
    StringList messages;
};


/*! \class MaildirMailbox maildir.h

    This class models a maildir mailbox: a directory containing the
    two subdirectories cur and new, each containing message files.

    All files in new are considered newer than those in cur. Within
    each directory, files are sorted numerically based on the portion
    of the name before the first dot.
*/


/*! Creates a new MaildirMailbox for \a path. The first \a n characters of
    the path are disregarded when creating target mailboxes.
*/

MaildirMailbox::MaildirMailbox( const String &path, uint n )
    : MigratorMailbox( path.mid( n ) ),
      d( new MaildirMailboxData )
{
    d->path = path;
}


/*! Returns a pointer to the next message in this MaildirMailbox, or 0 if
    there are no more messages (or if this object doesn't represent
    a valid maildir).
*/

MigratorMessage * MaildirMailbox::nextMessage()
{
    if ( !d->opened ) {
        d->opened = true;
        readSubDir( "cur" );
        readSubDir( "new" );
    }

    String * n = d->messages.first();
    if ( !n )
        return 0;
    d->messages.shift();

    String f( d->path + "/" + *n );
    File m( f );
    String c( m.contents() );
    if ( c[0] == ' ' && c[4] == ' ' ) {
        // Some messages copied from Courier start with a line like
        // " Feb 12 12:12:12 2012". Must drop that.
        uint i = 0;
        while ( i < c.length() && c[i] != '\r' && c[i] != '\n' )
            i++;
        while ( c[i] == '\r' )
            i++;
        if ( c[i] == '\n' )
            i++;
        c = c.mid( i );
    }
    MigratorMessage * mm = new MigratorMessage( c, f );

    int i = n->find( ',' );
    if ( i >= 0 ) {
        while ( i < (int)n->length() ) {
            switch ( (*n)[i] ) {
                // Comments from the maildir documentation:
            case 'D':
                // "D" - this is a 'draft' message
                mm->addFlag( "\\draft" );
                break;
            case 'R':
                // "R" - this message has been replied to
                mm->addFlag( "\\answered" );
                break;

            case 'S':
                // "S" - this message has been viewed (seen)
                mm->addFlag( "\\seen" );
                break;

            case 'T':
                // "T" - this message has been marked to be deleted
                // (trashed), but is not yet removed (messages are
                // removed from maildirs simply by deleting their
                // file)
                mm->addFlag( "\\deleted" );
                break;

            case 'F':
                // "F" - this message has been marked by the user, for
                // some purpose.
                mm->addFlag( "\\flagged" );
                break;
            }
            i++;
        }
    }
    return mm;
}


/*! Reads a cur/new/tmp subdirectory and sorts the messages into
    order.
*/

void MaildirMailbox::readSubDir( const String & sub )
{
    DIR * dir = opendir( ( d->path + "/" + sub ).cstr() );
    if ( !dir )
        return;

    Map<StringList> files;
    IntegerSet times;

    struct dirent * de = readdir( dir );
    while ( de ) {
        if ( de->d_name[0] >= '0' && de->d_name[0] <= '9' ) {
            String n( de->d_name );
            int i = n.find( '.' );
            bool ok = false;
            if ( i > 0 )
                i = n.mid( 0, i ).number( &ok );
            if ( ok ) {
                String * f = new String ( sub );
                f->append( "/" );
                f->append( n );
                StringList * l = 0;
                l = files.find( i );
                if ( !l ) {
                    l = new StringList;
                    files.insert( i, l );
                    times.add( i ); // XXX: how very evil
                }
                l->append( f );
            }
            else {
                // no dot in the name... what is it?
            }
        }
        de = readdir( dir );
    }
    closedir( dir );

    while ( !times.isEmpty() ) {
        uint n = times.smallest();
        times.remove( n );
        StringList * l = files.find( n );
        StringList::Iterator i( *l );
        while ( i ) {
            d->messages.append( *i );
            ++i;
        }
    }
}
