// Copyright 2009 The Archiveopteryx Developers <info@aox.org>

#include "headerfile.h"

#include "list.h"
#include "enum.h"
#include "class.h"
#include "error.h"
#include "parser.h"
#include "function.h"


static List<HeaderFile> * headers = 0;


/*! \class HeaderFile headerfile.h
    The HeaderFile class models a header file.

    The HeaderFile file is viewed as a collection of class { ... }
    statements, each of which is scanned for member functions and
    superclass names. Other content is ignored (for now - enums may
    one day be handled).
*/



/*! Constructs a HeaderFile for \a file, which is presumed to be in the
    current directory.

    The file is parsed immediately.
*/

HeaderFile::HeaderFile( const EString & file )
    : File( file, Read )
{
    if ( valid() ) {
        if ( !headers )
            headers = new List<HeaderFile>;
        headers->append( this );
        parse();
    }
}


/*! Parses this header file and creates Class and Function objects as
    appropriate.

    The parsing is minimalistic: All it does is look for a useful
    subset of class declarations, and process those.
*/

void HeaderFile::parse()
{
    Parser p( contents() );
    p.scan( "\nclass " );
    while ( !p.atEnd() ) {
        EString className = p.identifier();
        EString superclass = 0;
        p.whitespace();
        if ( p.lookingAt( ":" ) ) {
            p.step();
            EString inheritance = p.word();
            if ( inheritance != "public" ) {
                (void)new Error( this, p.line(),
                                 "Non-public inheritance for class " +
                                 className );
                return;
            }
            EString parent = p.identifier();
            if ( parent.isEmpty() ) {
                (void)new Error( this, p.line(),
                                 "Cannot parse superclass name for class " +
                                 className );
                return;
            }
            superclass = parent;
        }
        p.whitespace();
        if ( p.lookingAt( "{" ) ) {
            Class * c = Class::find( className );
            if ( !c )
                c = new Class( className, 0, 0 );
            c->setParent( superclass );
            if ( c && c->file() ) {
                (void) new Error( this, p.line(),
                                  "Class " + className +
                                  " conflicts with " + className + " at " +
                                  c->file()->name() + ":" +
                                  fn( c->line() ) );
                (void) new Error( c->file(), c->line(),
                                  "Class " + className +
                                  " conflicts with " + className + " at " +
                                  name() + ":" +
                                  fn( p.line() ) );
            }
            else {
                c->setSource( this, p.line() );
            }
            p.step();
            bool ok = false;
            do {
                ok = false;
                p.whitespace();
                while ( p.lookingAt( "public:" ) ||
                        p.lookingAt( "private:" ) ||
                        p.lookingAt( "protected:" ) ) {
                    p.scan( ":" );
                    p.step();
                    p.whitespace();
                }
                if ( p.lookingAt( "virtual " ) )
                    p.scan( " " );
                p.whitespace();
                EString t;
                EString n;
                uint l = p.line();
                if ( p.lookingAt( "operator " ) ) {
                    n = p.identifier();
                }
                else if ( p.lookingAt( "enum " ) ) {
                    p.scan( " " );
                    Enum * e = new Enum( c, p.word(), this, l );
                    p.whitespace();
                    if ( p.lookingAt( "{" ) ) {
                        bool again = true;
                        while ( again ) {
                            p.step();
                            p.whitespace();
                            EString v = p.word();
                            if ( v.isEmpty() )
                                (void)new Error( this, p.line(),
                                                 "Could not parse "
                                                 "enum value" );
                            else
                                e->addValue( v );
                            p.whitespace();
                            if ( p.lookingAt( "=" ) ) {
                                p.step();
                                p.whitespace();
                                (void)p.value();
                                p.whitespace();
                            }
                            again = p.lookingAt( "," );
                        }
                        if ( p.lookingAt( "}" ) ) {
                            p.step();
                            ok = true;
                        }
                        else {
                            (void)new Error( this, p.line(),
                                             "Enum definition for " +
                                             className + "::" + n +
                                             " does not end with '}'" );
                        }
                    }
                    else if ( p.lookingAt( ";" ) ) {
                        // senseless crap
                        ok = true;
                    }
                    else {
                        (void)new Error( this, l,
                                         "Cannot parse enum " +
                                         className + "::" + n );
                    }
                }
                else if ( p.lookingAt( "typedef " ) ) {
                    ok = true;
                }
                else {
                    t = p.type();
                    n = p.identifier();
                    if ( n.isEmpty() ) {
                        // constructor/destructor?
                        if ( t == className || t == "~" + className ) {
                            n = t;
                            t = "";
                        }
                        else if ( t.isEmpty() && p.lookingAt( "~" ) ) {
                            p.step();
                            n = "~" + p.identifier();
                        }
                    }
                }
                if ( !n.isEmpty() ) {
                    p.whitespace();
                    if ( p.lookingAt( ";" ) )
                        ok = true;
                    EString a = p.argumentList();
                    p.whitespace();
                    bool fc = false;
                    if ( p.lookingAt( "const" ) ) {
                        fc = true;
                        p.word();
                    }
                    if ( !n.isEmpty() && n.find( ':' ) < 0 &&
                         !a.isEmpty() ) {
                        n = className + "::" + n;
                        Function * f = Function::find( n, a, fc );
                        if ( !f )
                            f = new Function( t, n, a, fc, this, l );
                        ok = true;
                    }
                }
                if ( ok ) {
                    p.whitespace();
                    if ( p.lookingAt( "{" ) ) {
                        uint level = 0;
                        while ( level > 0 || p.lookingAt( "{" ) ) {
                            if ( p.lookingAt( "{" ) ) {
                                level++;
                            }
                            else if ( p.lookingAt( "}" ) ) {
                                level--;
                            }
                            p.step();
                            p.whitespace();
                        }
                    }
                    else {
                        p.scan( ";" );
                    }
                }
            } while ( ok );
        }
        p.scan( "\nclass " );
    }
}


/*! Returns a pointer to the HeaderFile whose unqualified file name is \a
    s, or a null pointer if there is no such HeaderFile.
*/

HeaderFile * HeaderFile::find( const EString & s )
{
    if ( !headers )
        return 0;

    List<HeaderFile>::Iterator it( headers );
    HeaderFile * h = 0;
    EString hack = "/" + s;
    while ( (h=it) != 0 &&
            h->name() != s &&
            !h->name().endsWith( hack ) )
        ++it;
    return h;
}
