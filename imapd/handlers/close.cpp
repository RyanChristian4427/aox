/*! \class Close close.h
    \brief The Close class implements the IMAP CLOSE command.

    Four lines of code and seemingly correct.

    Since Close is a variant of Expunge, this class inherits Expunge
    and switches to authenticated state after a silent expunge.

    The Unselect command is similar to this, but does not
    expunge. Perhaps Close should inherit Unselect rather than
    Expunge. It doesn't really matter - at best we might save one line
    of code.
*/

#include "close.h"

#include "imap.h"


void Close::execute()
{
    expunge( false );
    imap()->setMailbox( 0 );
    imap()->setState( IMAP::Authenticated );
    setState( Finished );
}
