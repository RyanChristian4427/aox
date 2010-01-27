// Copyright 2009 The Archiveopteryx Developers <info@aox.org>

#include "capability.h"

#include "scope.h"
#include "configuration.h"
#include "imap.h"
#include "estringlist.h"
#include "log.h"
#include "mechanism.h"
#if defined(USE_CRYPTLIB)
#include "tls.h"
#endif


/*! \class Capability capability.h
    Announces supported features (RFC 3501 section 6.1.1).

    We announce the following standard capabilities:

    RFC 3501: IMAP4rev1, STARTTLS, LOGINDISABLED,
    RFC 3502: MULTIAPPEND,
    RFC 2086: ACL,
    RFC 2088: LITERAL+,
    RFC 2177: IDLE,
    RFC 2971: ID,
    RFC 2342: NAMESPACE,
    RFC 2359: UIDPLUS,
    RFC 3691: UNSELECT,
    RFC 2245: AUTH=ANONYMOUS,
    RFC 2595: AUTH=PLAIN,
    RFC 2195: AUTH=CRAM-MD5,
    RFC 2831: AUTH=DIGEST-MD5,
    RFC 3348: CHILDREN,
    RFC 3516: BINARY,
    RFC 4469: CATENATE,
    RFC 4551: CONDSTORE,
    RFC 4467: URLAUTH,
    RFC 4731: ESEARCH (also from RFC 4466),
    RFC 4959: SASL-IR,
    RFC 4978: COMPRESS=DEFLATE,
    RFC 5032: WITHIN,
    RFC 5255: I18NLEVEL=1,
    RFC 5256: SORT,
    RFC 5257: ANNOTATE-EXPERIMENT-1,
    RFC 5258: LISTEXT,
    RFC 5465: NOTIFY.
*/

void Capability::execute()
{
    respond( "CAPABILITY " + capabilities( imap(), true ) );
    finish();
}


/*! Returns all capabilities that are applicable to \a i. If \a all is
    specified and true, the list includes capabilities that are not
    applicable to the current IMAP::state().
*/

EString Capability::capabilities( IMAP * i, bool all )
{
    EStringList c;

    c.append( "IMAP4rev1" );

    bool login = true;
    if ( i->state() == IMAP::NotAuthenticated )
        login = false;

    // the remainder of the capabilities are kept sorted by name

    // ugly X-... prefixes are disregarded when sorting by name

    if ( all || ( !login && i->accessPermitted() ) )
        c.append( SaslMechanism::allowedMechanisms( "AUTH=", i->hasTls() ) );

    if ( all || login ) {
        c.append( "ACL" );
        c.append( "ANNOTATE-EXPERIMENT-1" );
        c.append( "BINARY" );
        c.append( "CATENATE" );
        c.append( "CHILDREN" );
    }
    // should we advertise COMPRESS only if not compressed?
    // c.append( "COMPRESS=DEFLATE" );
    if ( all || login )
        c.append( "CONDSTORE" );
    c.append( "ENABLE" );
    if ( all || login ) {
        c.append( "ESEARCH" );
        c.append( "I18NLEVEL=1" );
    }
    c.append( "ID" );
    if ( all || login )
        c.append( "IDLE" );
    if ( all || login )
        c.append( "LIST-EXTENDED" );
    c.append( "LITERAL+" );
    if ( ( all || !login ) &&
         !SaslMechanism::allowed( SaslMechanism::Plain, i->hasTls() ) )
        c.append( "LOGINDISABLED" );
    if ( all || login ) {
        c.append( "MULTIAPPEND" );
        c.append( "NAMESPACE" );
        //c.append( "NOTIFY" );
    }
    if ( all || login )
        c.append( "RIGHTS=ekntx" );
    if ( all || !login )
        c.append( "SASL-IR" );
    if ( all || login ) {
        c.append( "SORT" );
        c.append( "SORT=DISPLAY" ); // draft-ietf-morg-sortdisplay
    }
    if ( Configuration::toggle( Configuration::UseTls ) && !i->hasTls() )
        c.append( "STARTTLS" );
    if ( all || login ) {
        // also THREAD=...
        c.append( "UIDPLUS" );
        c.append( "UNSELECT" );
        c.append( "URLAUTH" );
    }
    if ( all || login )
        c.append( "WITHIN" );

    return c.join( " " );
}
