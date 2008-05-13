// Copyright Oryx Mail Systems GmbH. All enquiries to info@oryx.com, please.

// getpwnam
#include <pwd.h>
// getgrnam
#include <grp.h>
// write, getpid, getdtablesize, close, dup, getuid, geteuid, chroot,
// chdir, setregid, setreuid, fork
#include <unistd.h>
// open, O_RDWR
#include <sys/stat.h>
#include <fcntl.h>
// opendir
#include <dirent.h>
// exit
#include <stdlib.h>
// errno
#include <errno.h>
// fork
#include <sys/types.h>
// fprintf, stderr
#include <stdio.h>
// signal
#include <signal.h>

// our own includes, _after_ the system header files. lots of system
// header files break if we've already defined UINT_MAX, etc.

#include "server.h"

#include "log.h"
#include "file.h"
#include "scope.h"
#include "string.h"
#include "logclient.h"
#include "eventloop.h"
#include "connection.h"
#include "configuration.h"
#include "eventloop.h"
#include "allocator.h"
#include "resolver.h"
#include "entropy.h"
#include "query.h"


class ServerData
    : public Garbage
{
public:
    ServerData( const char * n )
        : name( n ), stage( Server::Configuration ),
          secured( false ), fork( false ),
          chrootMode( Server::JailDir ),
          queries( new List< Query > ),
          children( 0 ),
          mainProcess( false )
    {}

    String name;
    Server::Stage stage;
    String configFile;
    bool secured;
    bool fork;
    Server::ChrootMode chrootMode;
    List< Query > *queries;
    List<pid_t> * children;
    bool mainProcess;
};


ServerData * Server::d;


/*! \class Server server.h

    The Server class performs the server startup functions that are
    common to most/all Archiveopteryx servers. The functions are
    performed in a fixed order - you call setup( x ) to continue up to
    stage x, then return.
*/


/*! Constructs a Server for \a name. \a name will be used for the pid
    file, etc. \a argc and \a argv are parsed to find command-line
    options.
*/

Server::Server( const char * name, int argc, char * argv[] )
{
    d = new ServerData( name );
    Allocator::addEternal( d, "Server data" );

    int c;
    while ( (c=getopt( argc, argv, "fc:" )) != -1 ) {
        switch ( c ) {
        case 'f':
            if ( d->fork ) {
                fprintf( stderr, "%s: -f specified twice\n", name );
                exit( 1 );
            }
            d->fork = true;
            break;
        case 'c':
            if ( !d->configFile.isEmpty() ) {
                fprintf( stderr, "%s: -c specified twice\n", name );
                exit( 1 );
            }
            else {
                d->configFile = optarg;
                File tmp( d->configFile );
                if ( !tmp.valid() ) {
                    fprintf( stderr,
                             "%s: Config file %s not accessible/readable\n",
                             name, tmp.name().cstr() );
                    exit( 1 );
                }
            }
            break;
        default:
            exit( 1 );
            break;
        }
    }
    if ( argc > optind ) {
        fprintf( stderr, "%s: Parse error for argument %d (%s)\n",
                 name, optind, argv[optind] );
        exit( 1 );
    }
}


/*! Notifies the Server that it is to chroot according to \a mode. If
    \a mode is JailDir, secure() will chroot into the jail directory
    and check that '/' is inaccesssible. If \a mode is LogDir,
    secure() will chroot into the logfile directory, where the server
    hopefully can access the logfile.
*/

void Server::setChrootMode( ChrootMode mode )
{
    d->chrootMode = mode;
}


/*! Performs server setup for each stage up to but NOT including \a s. */

void Server::setup( Stage s )
{
    try {
        while ( d->stage < s ) {
            switch ( d->stage ) {
            case Configuration:
                configuration();
                break;
            case NameResolution:
                nameResolution();
                break;
            case Files:
                files();
                break;
            case LogSetup:
                logSetup();
                break;
            case Loop:
                loop();
                break;
            case Report:
                // This just gives us a good place to stop in main.
                break;
            case Fork:
                fork();
                break;
            case PidFile:
                pidFile();
                break;
            case LogStartup:
                logStartup();
                break;
            case Secure:
                secure();
                break;
            case Finish:
                break;
            }
            d->stage = (Stage)(d->stage + 1);
        }
    } catch ( Exception e ) {
        // don't allocate memory or call anything here.
        const char * c = 0;
        switch (e) {
        case Range:
            c = "Out-of-range memory access during server startup.";
            break;
        case Memory:
            c = "Out of memory during server startup.";
            break;
        case FD:
            c = "FD error during server startup.";
            break;
        };
        uint i = 0;
        while( c[i] )
            i++;
        ::write( 2, c, i );
        ::write( 2, "\n", 1 );
        exit( 1 );
    }
}


/*! Reads server configuration, either from the default config file or
    from the one supplied in argc.
*/

void Server::configuration()
{
    if ( d->configFile.isEmpty() )
        Configuration::setup( "archiveopteryx.conf" );
    else
        Configuration::setup( d->configFile );
}


/*! Resolves any domain names used in the configuration file before we
    chroot.
*/

void Server::nameResolution()
{
    List<Configuration::Text>::Iterator i( Configuration::addressVariables() );
    while ( i ) {
        const StringList & r
            = Resolver::resolve( Configuration::text( *i ) );
        if ( r.isEmpty() ) {
            log( String("Unable to resolve ") +
                 Configuration::name( *i ) +
                 " = " + Configuration::text( *i ),
                 Log::Disaster );
        }
        ++i;
    }
    if ( !Log::disastersYet() )
        return;

    StringList::Iterator e( Resolver::errors() );
    while ( e ) {
        log( *e );
        ++e;
    }
}


/*! Closes all files except stdout and stderr. Attaches stdin to
    /dev/null in case something uses it. stderr is kept open so
    that we can tell our daddy about any disasters.
*/

void Server::files()
{
    int s = getdtablesize();
    while ( s > 0 ) {
        s--;
        if ( s != 2 && s != 1 )
            close( s );
    }
    s = open( "/dev/null", O_RDWR );

    Entropy::setup();
}


/*! Creates the global logging context, and sets up a LogClient if no
    Logger has been created already.

    This also creates the Loop object, so that the LogClient doesn't
    feel alone in the world, abandoned by its parents, depressed and
    generally bad.
*/

void Server::logSetup()
{
    EventLoop::setup();
    if ( !Logger::global() )
        LogClient::setup( d->name );
    Scope::current()->setLog( new Log( Log::General ) );
    log( name() + ", Archiveopteryx version " +
         Configuration::compiledIn( Configuration::Version ) );
    Allocator::setReporting( true );
}


static void shutdownLoop( int )
{
    Server::killChildren();
    EventLoop::shutdown();
}


/*! Called by signal handling to kill any children started in fork().

*/

void Server::killChildren()
{
    List<pid_t>::Iterator child( d->children );
    while ( child ) {
        ::kill( *child, SIGTERM );
        ++child;
    }
}


/*! Initializes the global event loop. */

void Server::loop()
{
    // we cannot reread files, so we ignore sighup
    ::signal( SIGHUP, SIG_IGN );
    // sigint and sigterm both should stop the server
    ::signal( SIGINT, shutdownLoop );
    ::signal( SIGTERM, shutdownLoop );
    // sigpipe happens if we're writing to an already-closed fd. we'll
    // discover that it's closed a little later.
    ::signal( SIGPIPE, SIG_IGN );
}


/*! Forks the server as required by -f and the configuration variable
    server-processes.

    If -f is specified, the parent exits in this function and does not
    return from this function.

    As many processes as specified by server-processes return.
*/

void Server::fork()
{
    if ( !d->fork )
        return;

    pid_t p = ::fork();
    if ( p < 0 ) {
        log( "Unable to fork. Error code " + fn( errno ),
             Log::Disaster );
        exit( 1 );
    } else if ( p > 0 ) {
        exit( 0 );
    }
    d->mainProcess = true;
    uint children = 0;
    if ( d->name == "archiveopteryx" ) {
        children = Configuration::scalar( Configuration::ServerProcesses );
        if ( children > 1 )
            d->children = new List<pid_t>;
    }
    uint i = 1;
    while ( d->children && i < children ) {
        pid_t * child = new pid_t;
        *child = ::fork();
        if ( *child < 0 ) {
            log( "Unable to fork server; pressing on. Error code " +
                 fn( errno ), Log::Error );
        }
        else if ( *child > 0 ) {
            log( "Process " + fn( getpid() ) + " forked " + fn( *child ) );
            d->children->append( child );
        }
        else {
            d->mainProcess = false;
            d->children = 0;
            EventLoop::global()->closeAllExceptListeners();
            log( "Process " + fn( getpid() ) + " started" );
            if ( Configuration::toggle( Configuration::UseStatistics ) ) {
                uint port
                    = Configuration::scalar( Configuration::StatisticsPort );
                log( "Using port " + fn( port + i ) +
                     " for statistics queries" );
                Configuration::add( "statistics-port = " + fn( port + i ) );
            }
        }
        i++;
    }
}


/*! Writes the server's pid to an almost hardcoded pidfile. We don't
    lock the file, since most of these servers don't have a problem
    with multiple instances of themselves. The pidfile is just a
    convenience for tools like start-stop-daemon.
*/

void Server::pidFile()
{
    if ( !d->mainProcess )
        return;

    String dir( Configuration::compiledIn( Configuration::PidFileDir ) );

    String n = dir + "/" + d->name + ".pid";
    File f( n, File::Write );
    if ( f.valid() )
        f.write( fn( getpid() ) + "\n" );
    else
        log( "Unable to write to PID file " + n );
}


/*! Logs the startup details. By this time, the logger must be in
    working order.
*/

void Server::logStartup()
{
    log( "Starting server " + d->name +
         " (host " + Configuration::hostname() + ")" +
         " (pid " + fn( getpid() ) + ") " +
         String( d->secured ? "securely" : "insecurely" ) );
}


/*! Loses all rights. Dies with an error if that isn't possible, or if
    anything fails.
*/

void Server::secure()
{
    if ( Configuration::present( Configuration::DbOwnerPassword ) ) {
        log( "db-owner-password specified in archiveopteryx.conf "
             "(should be in aoxsuper.conf)",
             Log::Disaster );
        exit( 1 );
    }
    bool security = Configuration::toggle( Configuration::Security );
    if ( !security ) {
        if ( getuid() == 0 || geteuid() == 0 )
            log( "Warning: Starting " + d->name + " insecurely as root" );
        d->secured = false;
        return;
    }

    String user( Configuration::text( Configuration::JailUser ) );
    struct passwd * pw = getpwnam( user.cstr() );
    if ( !pw ) {
        log( "Cannot secure server " + d->name +
             " since " + user + " is not a valid login (says getpwnam())",
             Log::Disaster );
        exit( 1 );
    }
    if ( pw->pw_uid == 0 ) {
        log( "Cannot secure server " + d->name + " since " + user +
             " has UID 0",
             Log::Disaster );
        exit( 1 );
    }

    String group( Configuration::text( Configuration::JailGroup ) );
    struct group * gr = getgrnam( group.cstr() );
    if ( !gr ) {
        log( "Cannot secure server " + d->name +
             " since " + group + " is not a valid group (says getgrnam())",
             Log::Disaster );
        exit( 1 );
    }

    String cfn( d->configFile );
    if ( cfn.isEmpty() )
        cfn = Configuration::configFile();

    struct stat st;
    if ( stat( cfn.cstr(), &st ) < 0 ) {
        log( "Cannot stat configuration file " + cfn,
             Log::Disaster );
        exit( 1 );
    }
    if ( st.st_uid != pw->pw_uid ) {
        log( "Configuration file " + cfn +
             " must be owned by " + user +
             " (uid " + fn( pw->pw_uid ) + ")" +
             " (is owned by uid " +
             fn( st.st_uid ) + ")",
             Log::Disaster );
        exit( 1 );
    }
    if ( (gid_t)st.st_gid != (gid_t)gr->gr_gid ) {
        log( "Configuration file " + cfn +
             " must be in group " + user +
             " (gid " + fn( gr->gr_gid ) + ")" +
             " (is in gid " +
             fn( st.st_gid ) + ")",
             Log::Disaster );
        exit( 1 );
    }
    if ( (st.st_mode & 027) != 0 ) {
        log( "Configuration file " + cfn +
             " must be readable for user " + user + "/group " + group +
             " only (mode is " +
             fn( st.st_mode & 0777, 8 ) + ", should be " +
             fn( st.st_mode & 0740, 8 ) + ")",
             Log::Disaster );
        exit( 1 );
    }

    String root;
    switch ( d->chrootMode ) {
    case MessageCopyDir:
        root = Configuration::text( Configuration::MessageCopyDir );
        break;
    case JailDir:
        root = Configuration::text( Configuration::JailDir );
        break;
    case TlsProxyDir:
        root = Configuration::compiledIn( Configuration::LibDir );
        if ( !root.endsWith( "/" ) )
            root.append( "/" );
        root.append( "tlsproxy" );
        break;
    case LogDir:
        root = Configuration::text( Configuration::LogFile );
        if ( root == "-" ) {
            root = Configuration::text( Configuration::JailDir );
        }
        else if ( root.startsWith( "syslog/" ) ) {
            root = "/";
        }
        else {
            uint i = root.length();
            while ( i > 0 && root[i] != '/' )
                i--;
            if ( i == 0 ) {
                log( "Cannot secure server " + d->name +
                     " since logfile does not contain '/'",
                     Log::Disaster );
                log( "Value of logfile: " + root, Log::Info );
                exit( 1 );
            }
            root.truncate( i );
        }
        break;
    }
    if ( chroot( root.cstr() ) ) {
        log( "Cannot secure server " + d->name + " since chroot( \"" +
             root + "\" ) failed with error " + fn( errno ),
             Log::Disaster );
        exit( 1 );
    }
    if ( chdir( "/" ) ) {
        log( "Cannot secure server " + d->name + " since chdir( \"/\" ) "
             "failed in jail directory (\"" + root + "\") with error " +
             fn( errno ),
             Log::Disaster );
        exit( 1 );
    }
    File::setRoot( root );

    if ( setregid( gr->gr_gid, gr->gr_gid ) ) {
        log( "Cannot secure server " + d->name + " since setregid( " +
             fn( gr->gr_gid ) + ", " + fn( gr->gr_gid ) + " ) "
             "failed with error " + fn( errno ),
             Log::Disaster );
        exit( 1 );
    }

    if ( setgroups( 1, (gid_t*)&(gr->gr_gid) ) ) {
        log( "Cannot secure server " + d->name + " since setgroups( 1, [" +
             fn( gr->gr_gid ) + "] ) failed with error " + fn( errno ),
             Log::Disaster );
        exit( 1 );
    }

    if ( setreuid( pw->pw_uid, pw->pw_uid ) ) {
        log( "Cannot secure server " + d->name + " since setreuid( " +
             fn( pw->pw_uid ) + ", " + fn( pw->pw_uid ) + " ) "
             "failed with error " + fn( errno ),
             Log::Disaster );
        exit( 1 );
    }

    if ( d->chrootMode == JailDir ) {
        // check that the jail directory really is a jail
        DIR * slash = opendir( "/" ); // checks 'x' access
        int fd = open( "/does/not/exist", O_RDONLY ); // checks 'r'
        if ( slash || fd >= 0 || errno != EACCES ) {
            log( "Cannot secure server " + d->name +
                 " since jail directory " + root +
                 " is accessible to user " + user,
                 Log::Disaster );
            exit( 1 );
        }
        if ( fd >= 0 ) {
        }
    }

    // one final check...
    if ( geteuid() != pw->pw_uid || getuid() != pw->pw_uid ) {
        log( "Cannot secure server " + d->name +
             " since setreuid() failed. Desired uid " +
             fn( pw->pw_uid ) + ", got uid " + fn( getuid() ) +
             " and euid " + fn( geteuid() ),
             Log::Disaster );
        exit( 1 );
    }

    // success
    log( "Secured server " + d->name + " using jail directory " + root +
         ", uid " + fn( pw->pw_uid ) + ", gid " + fn( gr->gr_gid ) );
    d->secured = true;
}


/*! Finishes setup and runs the main loop of the server. */

void Server::run()
{
    setup( Finish );
    Configuration::report();

    uint listeners = 0;
    List< Connection >::Iterator it( EventLoop::global()->connections() );
    while ( it ) {
        if ( it->type() == Connection::Listener )
            listeners++;
        ++it;
    }

    if ( listeners == 0 ) {
        log( "No active listeners. " + d->name + " exiting.", Log::Disaster );
        exit( 1 );
    }

    if ( Scope::current()->log()->disastersYet() ) {
        log( "Aborting server " + d->name + " due to earlier problems." );
        exit( 1 );
    }

    dup2( 0, 1 );
    dup2( 0, 2 );
    EventLoop::global()->start();

    if ( Scope::current()->log()->disastersYet() )
        exit( 1 );
    exit( 0 );
}


/*! This static function returns the name of the application.
    Is server the right way to publicise this name?
*/

String Server::name()
{
    if ( d )
        return d->name;
    return "";
}
