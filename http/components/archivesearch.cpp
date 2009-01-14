// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#include "archivesearch.h"

#include "link.h"
#include "field.h"
#include "mailbox.h"
#include "ustring.h"
#include "frontmatter.h"
#include "addressfield.h"
#include "integerset.h"
#include "threader.h"
#include "webpage.h"
#include "message.h"
#include "header.h"
#include "query.h"

#include "archivemessage.h"


class ArchiveSearchData
    : public Garbage
{
public:
    ArchiveSearchData()
        : link( 0 ), done( false )
    {
        matchesAll.add( 1, UINT_MAX );
    }

    Link * link;
    bool done;

    struct SearchTerm
        : public Garbage
    {
        SearchTerm(): address( false ), domain( false ), query( 0 ) {}
        UString term;
        bool address;
        bool domain;
        Query * query;
        IntegerSet result;
    };

    List<SearchTerm> terms;
    IntegerSet matchesAll;
    IntegerSet matchesSome;
    List<SubjectThread> all;
    List<SubjectThread> some;
};


/*! \class ArchiveSearch archivesearch.h
    A page component representing the results of a search.
*/


/*! Create a new ArchiveSearch for \a link. */

ArchiveSearch::ArchiveSearch( Link * link )
    : PageComponent( "archivesearch" ),
      d( new ArchiveSearchData )
{
    d->link = link;
}


void ArchiveSearch::execute()
{
    if ( d->done )
        return;

    Mailbox * m = d->link->mailbox();
    Threader * t = m->threader();

    page()->requireRight( m, Permissions::Read );

    if ( !t->updated() )
        t->refresh( this );

    if ( !page()->permitted() )
        return;

    if ( d->terms.isEmpty() ) {
        parseTerms();
        if ( d->terms.isEmpty() ) {
            setContents( "<p>No results." ); // XXX _highly_ suboptimal
            return;
        }
        sendQueries();
    }

    if ( !queriesDone() )
        return;

    setTitle();
    computeResultSets();

    Dict<Address> addresses;

    List<SubjectThread>::Iterator i( t->subjectThreads() );
    while ( i ) {
        if ( !i->members().intersection( d->matchesAll ).isEmpty() )
            d->all.append( i );
        else if ( !i->members().intersection( d->matchesSome ).isEmpty() )
            d->some.append( i );
        ++i;
    }

    String s;

    s.append( "<p>" );
    s.appendNumber( d->matchesSome.count() );
    s.append( " results found in " );
    s.appendNumber( d->some.count() + d->all.count() );
    s.append( " threads\n" );
    s.append( "<p>Search terms:\n" );
    s.append( searchTerms() );

    if ( d->all.count() + d->some.count() > 20 )
        s.append( looongResultList() );
    else if ( d->matchesSome.count() > 10 )
        s.append( middlingResultList() );
    else
        s.append( shortishResultList() );

    setContents( s );
    d->done = true;
}


/*! Parses the query term and stores the resulting terms. Quietly
    ignores errors.

    There aren't really very many errors. Empty search terms, search
    terms containing nulls, BOMs, private-use codepoints, and that's
    about it.
*/

void ArchiveSearch::parseTerms()
{
    UString terms = page()->link()->argument( "query" );
    if ( terms.isEmpty() )
        return;

    uint i = 0;
    while ( i < terms.length() ) {
        UString term;
        if ( terms[i] == '"' ) {
            i++;
            while ( i < terms.length() && terms[i] != '"' ) {
                term.append( terms[i] );
                i++;
            }
        }
        else {
            while ( i < terms.length() && terms[i] != ' ' ) {
                term.append( terms[i] );
                i++;
            }
        }
        uint j = 0;
        while ( j < term.length() &&
                term[j] != 0 && ( term[j] < 0xE000 || term[j] > 0xF8FF ) &&
                term[j] != 0xFFFD && term[j] != 0xFFFE && term[j] != 0xFEFF )
            j++;
        if ( j == term.length() && j > 0 ) {
           ArchiveSearchData::SearchTerm * st
               = new ArchiveSearchData::SearchTerm;
           st->term = term;
           d->terms.append( st );
        }
        while ( terms[i] == ' ' )
            ++i;
    }
}


/*! Sends off each query to the database. Does not generate any output. */

void ArchiveSearch::sendQueries()
{
    List<ArchiveSearchData::SearchTerm>::Iterator term( d->terms );
    while ( term ) {
        String localpart;
        String domain;
        String t8 = term->term.utf8();
        if ( t8.contains( '@' ) ) {
            StringList * l = StringList::split( '@', t8 );
            if ( l->count() == 2 ) {
                localpart = l->first()->lower();
                domain = l->last()->lower();
                uint i = 0;
                bool alpha = false;
                bool ok = true;
                while ( ok && i < domain.length() ) {
                    char c = domain[i];
                    if ( c == '.' ) {
                        if ( i == 0 || i == domain.length() - 1 ||
                             domain[i+1] == '.' )
                            ok = false;
                    }
                    else if ( c >= 'a' && c <= 'z' ) {
                        alpha = true;
                    }
                    else if ( c >= '0' && c <= '9' ) {
                        // nothing needed
                    }
                    else if ( c == '-' ) {
                        // nothing needed
                    }
                    else {
                        ok = false;
                    }
                    i++;
                }
                if ( ok ) {
                    if ( localpart.isEmpty() )
                        term->domain = true;
                    else
                        term->address = true;
                }
            }
        }
        if ( term->domain ) {
            term->query
                = new Query( "select mm.uid from mailbox_messages mm "
                             "join address_fields af using message "
                             "join addresses a on (af.address=a.id) "
                             "where mm.mailbox=$1 and lower(a.domain)=$2",
                             this );
            term->query->bind( 2, domain );
        }
        else if ( term->address ) {
            term->query
                = new Query( "select mm.uid from mailbox_messages mm "
                             "join address_fields af using message "
                             "join addresses a on (af.address=a.id) "
                             "where mm.mailbox=$1 and "
                             "lower(a.localpart)=$2 and lower(a.domain)=$3",
                             this );
            term->query->bind( 2, localpart );
            term->query->bind( 3, domain );
        }
        else {
            String s;
            s = "select mm.uid from mailbox_messages mm "
                "left join header_fields hf on "
                " (mm.message=hf.message and hf.field=20 and "
                "  hf.value ilike '%'||$2||'%') "
                "left join part_numbers pn on mm.message=pn.message "
                "left join bodyparts b on "
                " (pn.bodypart=b.id and b.text ilike '%'||$2||'%') "
                "where mm.mailbox=$1 and "
                "(hf.message is not null or b.id is not null)";
            term->query = new Query( s, this );
            term->query->bind( 2, term->term );
        }
        term->query->bind( 1, d->link->mailbox()->id() );
        term->query->execute();
        ++term;
    }
}


/*! Computes and sets a suitable title. The title should be the
    shortest possible rendering of the search results.
*/

void ArchiveSearch::setTitle()
{
    FrontMatter * title = new FrontMatter( "title" );
    List<ArchiveSearchData::SearchTerm>::Iterator i( d->terms );
    while ( i ) {
        title->append( quoted( i->term ) );
        title->append( ' ' );
        ++i;
    }
    title->append( "- search results" );
    addFrontMatter( title );
}


/*! Returns HTML to describe the search terms as parsed and used. */

String ArchiveSearch::searchTerms() const
{
    String s( "<ul class=searchtermlist>\n" );
    List<ArchiveSearchData::SearchTerm>::Iterator i( d->terms );
    while ( i ) {
        s.append( "<li>" );
        if ( i->address )
            s.append( "Message uses address: " );
        else if ( i->domain )
            s.append( "Message uses domain: " );
        else
            s.append( "Message contains text: " );
        s.append( quoted( i->term ) );
        s.append( " (" );
        uint c = i->result.count();
        if ( c > 1 ) {
            s.append( "<a href=" );
            Link l;
            l.setType( page()->link()->type() );
            l.setMailbox( page()->link()->mailbox() );
            l.addArgument( "query", i->term );
            s.append( l.canonical().quoted() );
            s.append( ">" );
            s.appendNumber( c );
            s.append( " results</a>" );
        }
        else if ( c == 1 ) {
            s.append( "one result" );
        }
        else {
            s.append( "no results" );
        }
        s.append( ")\n" );
        ++i;
    }
    s.append( "</ul>\n" ); // searchtermlist
    return s;
}


/*! Retrieves the result rows of the individual queries and computes
    the partial and aggregate result sets.
*/

void ArchiveSearch::computeResultSets()
{
    List<ArchiveSearchData::SearchTerm>::Iterator i( d->terms );
    while ( i ) {
        Row * r;
        while ( (r=i->query->nextRow()) )
            i->result.add( r->getInt( "uid" ) );
        d->matchesSome.add( i->result );
        d->matchesAll = d->matchesAll.intersection( i->result );
        ++i;
    }
    if ( d->matchesSome.isEmpty() )
        d->matchesAll.clear();
}


/*! Returns true if all queries are done, false if at least one is
    outstanding.
*/

bool ArchiveSearch::queriesDone() const
{
    List<ArchiveSearchData::SearchTerm>::Iterator i( d->terms );
    while ( i ) {
        if ( !i->query->done() )
            return false;
        ++i;
    }
    return true;
}


/*! Returns a list of search results, optimised for small result
    set. */

String ArchiveSearch::shortishResultList() const
{
    const PageComponent * p = this;
    String s( "<div class=searchresults>\n" );

    IntegerSet m = d->matchesAll;
    bool stillAll = true;
    if ( m.isEmpty() ) {
        m = d->matchesSome;
        stillAll = false;
    }
    while ( !m.isEmpty() ) {
        uint uid = m.smallest();
        m.remove( uid );
        Link * l = new Link;
        l->setType( d->link->type() );
        l->setMailbox( d->link->mailbox() );
        l->setUid( uid );
        ArchiveMessage * am = new ArchiveMessage( l );
        page()->addComponent( am, p );
        p = am;
        if ( m.isEmpty() && !stillAll ) {
            m = d->matchesSome;
            stillAll = false;
        }
    }
    return "";
}


/*! Returns a list of search results, optimised for large result
    sets.
*/

String ArchiveSearch::middlingResultList() const
{
    // I'm tired, tired, tired, and don't have good ideas now.
    return looongResultList();
}


/*! Returns a list of search results, optimised for large result
    sets.
*/

String ArchiveSearch::looongResultList() const
{
    String s( "<div class=searchresults>\n" );

    List<SubjectThread>::Iterator i( d->all );
    bool stillAll = true;
    if ( !i ) {
        i = d->some.first();
        stillAll = false;
    }
    while ( i ) {
        s.append( "<div class=matchingthread>\n" );
        Link l;
        l.setType( d->link->type() );
        l.setMailbox( d->link->mailbox() );
        l.setUid( i->members().smallest() );
        l.setSuffix( Link::Thread );
        s.append( "<a href=" );
        s.append( l.canonical().quoted() );
        s.append( ">" );
        s.append( quoted( i->subject() ) );
        s.append( "</a><br>\n" );
        IntegerSet matching( i->members() );
        s.append( "Contains " );
        s.append( fn ( matching.count() ) );
        s.append( " messages, " );
        s.append( fn ( matching.intersection( d->matchesSome ).count() ) );
        s.append( " matching.\n" );
        s.append( "</div>\n" ); //matchingthread
        ++i;
        if ( !i && stillAll ) {
            i = d->some.first();
            stillAll = false;
        }
    }

    s.append( "</div>\n" ); //searchresults
    return s;
}
