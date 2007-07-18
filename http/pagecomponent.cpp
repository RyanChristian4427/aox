// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

#include "pagecomponent.h"

#include "webpage.h"
#include "address.h"
#include "ustring.h"


class PageComponentData
    : public Garbage
{
public:
    PageComponentData()
        : status( 200 ), page( 0 )
    {}

    uint status;
    String divClass;
    String contents;
    List<FrontMatter> frontMatter;
    WebPage * page;
};


/*! \class PageComponent pagecomponent.h

    A PageComponent has a list of FrontMatter objects that it requires,
    and, once it's done(), contents() returns the text of the component.
*/

/*! Creates a new PageComponent with the div class name \a divClass. */

PageComponent::PageComponent( const String & divClass )
    : d( new PageComponentData )
{
    d->divClass = divClass;
}


/*! Returns true if this component has finished assembling its
    contents(), and false otherwise.
*/

bool PageComponent::done() const
{
    return !d->contents.isEmpty();
}


/*! Returns a numeric HTTP status appropriate to this component. The
    default value is 200.
*/

uint PageComponent::status() const
{
    return d->status;
}


/*! Sets the numeric HTTP status for this component to \a status.
*/

void PageComponent::setStatus( uint status )
{
    d->status = status;
}


/*! Returns a pointer to this component's WebPage, which will be 0 until
    the component has been added to a WebPage.
*/

WebPage * PageComponent::page() const
{
    return d->page;
}


/*! Informs this component that it is being used in \a page. */

void PageComponent::setPage( WebPage * page )
{
    d->page = page;
}


/*! Returns the contents of this component as an HTML-quoted string. The
    return value is meaningful only if done() is true.
*/

String PageComponent::contents() const
{
    String s( "<div class=\"" );
    s.append( d->divClass );
    s.append( "\">\n" );
    s.append( d->contents );
    s.append( "\n</div>\n" );
    return s;
}


/*! Sets the contents of this component to \a s, and signal the WebPage
    that owns this component of its completion. After this call, done()
    will return true, and contents() will return \a s. This function is
    meant for use by subclasses.
*/

void PageComponent::setContents( const String & s )
{
    d->contents = s;
    if ( d->page )
        d->page->execute();
}


/*! Returns the div class name for this component, as set in the call to
    the constructor.
*/

String PageComponent::divClass() const
{
    return d->divClass;
}


/*! Adds \a fm to the list of FrontMatter objects for this component. */

void PageComponent::addFrontMatter( FrontMatter * fm )
{
    d->frontMatter.append( fm );
}


/*! Returns a non-zero pointer to the list of FrontMatter objects that
    this component requires.
*/

List<FrontMatter> * PageComponent::frontMatter() const
{
    return &d->frontMatter;
}


void PageComponent::execute()
{
}


/*! Returns an HTML-quoted version of \a s. */

String PageComponent::quoted( const String & s )
{
    String r;
    r.reserve( s.length() );
    uint i = 0;
    while ( i < s.length() ) {
        if ( s[i] == '<' ) {
            r.append( "&lt;" );
        }
        else if ( s[i] == '>' ) {
            r.append( "&gt;" );
        }
        else if ( s[i] == '&' ) {
            r.append( "&amp;" );
        }
        else {
            r.append( s[i] );
        }
        i++;
    }
    return r;
}


/*! Returns an HTML-quoted version of \a u. */

String PageComponent::quoted( const UString & u )
{
    String r;
    r.reserve( u.length() );
    uint i = 0;
    while ( i < u.length() ) {
        if ( u[i] == '<' ) {
            r.append( "&lt;" );
        }
        else if ( u[i] == '>' ) {
            r.append( "&gt;" );
        }
        else if ( u[i] == '&' ) {
            r.append( "&amp;" );
        }
        else if ( u[i] > 126 || u[i] < 9 ) {
            r.append( "&#" );
            r.append( fn( u[i] ) );
            r.append( ';' );
        }
        else {
            r.append( (char)u[i] );
        }
        i++;
    }
    return r;
}


/*! Returns an HTML representation of \a a. */

String PageComponent::address( Address * a )
{
    String s( "<span class=address>" );
    bool n = true;
    if ( a->uname().isEmpty() )
        n = false;
    if ( n ) {
        s.append( quoted( a->uname().utf8() ) );
        s.append( " &lt;" );
    }
    s.append( quoted( a->localpart() ) );
    s.append( "@" );
    s.append( quoted( a->domain() ) );
    if ( n )
        s.append( "&gt;" );
    s.append( "</span>" );

    return s;
}


/*! Returns an HTML representation of \a a, assuming that \a a is
    really an address or something which reminds the user of an
    address (such as an alias).
*/

String PageComponent::address( const UString & a )
{
    String s( "<span class=address>" );
    s.append( quoted( a ) );
    s.append( "</span>" );

    return s;
}


/*! A wrapper around WebPage::uniqueNumber(), so calling it is less
    work.
*/

uint PageComponent::uniqueNumber()
{
    return page()->uniqueNumber();
}
