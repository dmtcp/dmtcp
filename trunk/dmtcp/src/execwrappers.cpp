/***************************************************************************
 *   Copyright (C) 2006 by Jason Ansel                                     *
 *   jansel@ccs.neu.edu                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include <stdarg.h>
#include "syscallwrappers.h"
#include "jassert.h"
#include "uniquepid.h"
#include "dmtcpworker.h"
#include "sockettable.h"
#include "protectedfds.h"
// extern "C" int fexecve(int fd, char *const argv[], char *const envp[])
// {
//     _real_fexecve(fd,argv,envp);
// }
//
// extern "C" int execve(const char *filename, char *const argv[],
//                 char *const envp[])
// {
//     _real_execve(filename,argv,envp);
// }

#include "protectedfds.h"
#include "sockettable.h"
#include "connectionmanager.h"
#include "connectionidentifier.h"
#include "syslogcheckpointer.h"
#include "jconvert.h"
#include <vector>
#include <list>
#include <string>

extern "C" int close ( int fd )
{
	if ( dmtcp::ProtectedFDs::isProtected ( fd ) )
	{
		JTRACE ( "blocked attempt to close protected fd" ) ( fd );
		errno = EBADF;
		return -1;
	}

	int rv = _real_close ( fd );

// #ifdef DEBUG
//     if(rv==0)
//     {
//         std::string closeDevice = dmtcp::KernelDeviceToConnection::Instance().fdToDevice( fd );
//         if(closeDevice != "") JTRACE("close()")(fd)(closeDevice);
//     }
// #endif

//     else
//     {
// #ifdef DEBUG
//         if(dmtcp::SocketTable::Instance()[fd].state() != dmtcp::SocketEntry::T_INVALID)
//         {
//             dmtcp::SocketEntry& e = dmtcp::SocketTable::Instance()[fd];
//             JTRACE("CLOSE()")(fd)(e.remoteId().id)(e.state());
//         }
// #endif
//         dmtcp::SocketTable::Instance().resetFd(fd);
//     }
	return rv;
}

extern "C" pid_t fork()
{
	pid_t child_pid = _real_fork();
	time_t child_time = time ( NULL );
	long child_host = dmtcp::UniquePid::ThisProcess().hostid();

	dmtcp::UniquePid parent = dmtcp::UniquePid::ThisProcess();


	if ( child_pid == 0 )
	{
		child_pid = getpid();

		JASSERT_SET_LOGFILE ( "/tmp/jassertlog." + jalib::XToString ( child_pid ) );

		dmtcp::UniquePid child = dmtcp::UniquePid ( child_host,child_pid,child_time );

		JTRACE ( "fork()ed [CHILD]" ) ( child );

		//fix the mutex
		_dmtcp_remutex_on_fork();

		//update ThisProcess()
		dmtcp::UniquePid::resetOnFork ( child );

		dmtcp::SyslogCheckpointer::resetOnFork();

		//rewrite socket table
//         dmtcp::SocketTable::Instance().onForkUpdate(parent,child);

		//make new connection to coordinator
		dmtcp::DmtcpWorker::resetOnFork();

		JTRACE ( "fork() done [CHILD]" ) ( child );


		return 0;
	}
	else
	{
		dmtcp::UniquePid child = dmtcp::UniquePid ( child_host,child_pid,child_time );

		JTRACE ( "fork()ed [PARENT] done" ) ( child );

//         _dmtcp_lock();

		//rewrite socket table
//         dmtcp::SocketTable::Instance().onForkUpdate(parent,child);

//         _dmtcp_unlock();

//         JTRACE("fork() done [PARENT]")(child);

		return child_pid;
	}
}

static void handleDup ( int& oldfd, int& newfd, bool isDup2 )
{
//     std::string oldDev = dmtcp::KernelDeviceToConnection::Instance().fdToDevice( oldfd );
//     std::string newDev = dmtcp::KernelDeviceToConnection::Instance().fdToDevice( newfd );
//
//     JTRACE("DUP")(oldfd)(oldDev)(newfd)(newDev)(isDup2);

//
//     _dmtcp_lock();
//
//     //make sure newfd is closed
//     dmtcp::SocketTable::Instance().resetFd( newfd );
//
//     dmtcp::SocketEntry& e = dmtcp::SocketTable::Instance()[oldfd];
//     dmtcp::SocketEntry& dest = dmtcp::SocketTable::Instance()[newfd];
//
//     switch(e.state())
//     {
//         case dmtcp::SocketEntry::T_ACCEPT:
//         case dmtcp::SocketEntry::T_CONNECT:
//             JTRACE("updating connection identifiers after dup")(oldfd)(newfd);
//             dmtcp::ConnectionIdentifiers::Incoming().updateAfterDup(oldfd,newfd);
//             dmtcp::ConnectionIdentifiers::Outgoing().updateAfterDup(oldfd,newfd);
// //             break; fall through to next case
//         case dmtcp::SocketEntry::T_ERROR:
//         case dmtcp::SocketEntry::T_CREATED:
//         case dmtcp::SocketEntry::T_BIND:
//         case dmtcp::SocketEntry::T_LISTEN:
//             JTRACE("dup: [end] copying socket data over")(e.state())(oldfd)(newfd);
//             dest = e;
//             dest.setSockfd( newfd );
//             break;
//         case dmtcp::SocketEntry::T_INVALID:
//             JTRACE("dup: [end] FD not socket")(oldfd);
//             break;
//         default:
//             JASSERT(false)(e.sockfd())(e.state()).Text("Unknown socket state");
//             break;
//     }
//
//     _dmtcp_unlock();
}
//
extern "C" int dup ( int oldfd )
{
	int rv = _real_dup ( oldfd );
	if ( rv >= 0 )
	{
		handleDup ( oldfd,rv,false );
	}
	else
	{
		JTRACE ( "user dup failed" ) ( oldfd );
	}
	return rv;
}
extern "C" int dup2 ( int oldfd, int newfd )
{
	if ( oldfd == newfd )
		return newfd;
	int rv = _real_dup2 ( oldfd,newfd );
	if ( !dmtcp::ProtectedFDs::isProtected ( newfd ) )
	{
		if ( rv == newfd )
		{
			handleDup ( oldfd,rv,true );
		}
		else
		{
			JTRACE ( "dup2 failed [end]" );
		}
	}
	return rv;
}

extern "C" char *ptsname ( int fd )
{
	JNOTE("Calling ptsname");
	static char tmpbuf[80];
	const char *ptr;

        if ( ptsname_r(fd, tmpbuf, 80) != 0 )
	{
		return NULL;
	}

	return tmpbuf;
}

extern "C" int ptsname_r(int fd, char * buf, size_t buflen)
{
	JNOTE("Calling ptsname_r");
	char device[20];
	const char *ptr;

	int rv = _real_ptsname_r ( fd, device,  buflen );
	if ( rv != 0 )
	{	
		JTRACE("ptsname_r failed");
		return rv;
	}

	ptr = dmtcp::UniquePid::ptsSymlinkFilename(device);

	JASSERT(symlink(device, ptr) == 0)(device)(ptr).Text("symlink() failed");

	strcpy(buf, ptr);

	dmtcp::PtsConnection::PtsType type = dmtcp::PtsConnection::Pt_Master;
	dmtcp::PtsConnection *master = new dmtcp::PtsConnection(device, ptr, type);
	dmtcp::KernelDeviceToConnection::Instance().create( fd, master );
	
	dmtcp::PtsToSymlink::Instance().add(device, buf);
	
	return rv;
}

extern "C" int socketpair ( int d, int type, int protocol, int sv[2] )
{
	JASSERT ( sv != NULL );
	int rv = _real_socketpair ( d,type,protocol,sv );
	JTRACE ( "socketpair()" ) ( sv[0] ) ( sv[1] );

	dmtcp::TcpConnection *a, *b;

	a = new dmtcp::TcpConnection ( d, type, protocol );
	a->onConnect();
	b = new dmtcp::TcpConnection ( *a, a->id() );

	dmtcp::KernelDeviceToConnection::Instance().create ( sv[0] , a );
	dmtcp::KernelDeviceToConnection::Instance().create ( sv[1] , b );

	return rv;
}

extern "C" int pipe ( int fds[2] )
{
	JTRACE ( "promoting pipe() to socketpair()" );
	//just promote pipes to socketpairs
	return socketpair ( AF_UNIX, SOCK_STREAM, 0, fds );
}

static void dmtcpPrepareForExec()
{
	static std::string serialFile;
	serialFile = dmtcp::UniquePid::dmtcpTableFilename();
	jalib::JBinarySerializeWriter wr ( serialFile );
	dmtcp::KernelDeviceToConnection::Instance().serialize ( wr );
	setenv ( ENV_VAR_SERIALFILE_INITIAL, serialFile.c_str(), 1 );
}

static const char* ourImportantEnvs[] =
    {
        ENV_VAR_NAME_ADDR,
        ENV_VAR_NAME_PORT,
        ENV_VAR_SERIALFILE_INITIAL,
        "JALIB_STDERR_PATH",
        "LD_PRELOAD",
        "JALIB_UTILITY_DIR",
        "DMTCP_CHECKPOINT_DIR"
    };
#define ourImportantEnvsCnt ((sizeof(ourImportantEnvs))/(sizeof(const char*)))

static bool isImportantEnv ( std::string str )
{
	for ( size_t i=0; i<str.size(); ++i )
		if ( str[i] == '=' )
		{
			str[i] = '\0';
			str = str.c_str();
			break;
		}

	for ( size_t i=0; i<ourImportantEnvsCnt; ++i )
	{
		if ( str == ourImportantEnvs[i] )
			return true;
	}
	return false;
}

static char** patchUserEnv ( char *const envp[] )
{
	static std::vector<char*> envVect;
	static std::list<std::string> strStorage;
	envVect.clear();
	strStorage.clear();

	JTRACE ( "patching user envp..." );

	//pack up our ENV into the new ENV
	for ( size_t i=0; i<ourImportantEnvsCnt; ++i )
	{
		const char* v = getenv ( ourImportantEnvs[i] );
		if ( v != NULL )
		{
			strStorage.push_back ( std::string ( ourImportantEnvs[i] ) + '=' + v );
			envVect.push_back ( &strStorage.back() [0] );
			JASSERT_STDERR << "     addenv[dmtcp]:" << strStorage.back() << '\n';
		}
	}

	for ( ;*envp != NULL; ++envp )
	{
		if ( isImportantEnv ( *envp ) )
		{
			JASSERT_STDERR << "     skipping: " << *envp << '\n';
			continue;
		}
		strStorage.push_back ( *envp );
		envVect.push_back ( &strStorage.back() [0] );
		JASSERT_STDERR << "     addenv[user]:" << strStorage.back() << '\n';
	}

	envVect.push_back ( NULL );

	return &envVect[0];
}

extern "C" int execve ( const char *filename, char *const argv[], char *const envp[] )
{
	//TODO: Right now we assume the user hasn't clobbered our setup of envp
	//(like LD_PRELOAD), we should really go check to make sure it hasn't
	//been destroyed....
	JTRACE ( "exec() wrapper" );
	dmtcpPrepareForExec();
	return _real_execve ( filename, argv, patchUserEnv ( envp ) );
}

extern "C" int fexecve ( int fd, char *const argv[], char *const envp[] )
{
	//TODO: Right now we assume the user hasn't clobbered our setup of envp
	//(like LD_PRELOAD), we should really go check to make sure it hasn't
	//been destroyed....
	JTRACE ( "exec() wrapper" );
	dmtcpPrepareForExec();
	return _real_fexecve ( fd, argv, patchUserEnv ( envp ) );
}

extern "C" int execv ( const char *path, char *const argv[] )
{
	JTRACE ( "exec() wrapper" );
	dmtcpPrepareForExec();
	return _real_execv ( path, argv );
}

extern "C" int execvp ( const char *file, char *const argv[] )
{
	JTRACE ( "exec() wrapper" );
	dmtcpPrepareForExec();
	return _real_execvp ( file, argv );
}

extern "C" int execl ( const char *path, const char *arg, ... )
{
	JASSERT ( false ).Text ( "variable argument version of exec not yet supported by dmtcp" );
	return -1;
}

extern "C" int execlp ( const char *file, const char *arg, ... )
{
	JASSERT ( false ).Text ( "variable argument version of exec not yet supported by dmtcp" );
	return -1;
}

extern "C" int system ( const char *command )
{
	JASSERT ( false ).Text ( "system() is called" );
	return -1;
}

// extern "C" int execle(const char *path, const char *arg, ..., char * const envp[])
// {
//     JASSERT(false).Text("variable argument version of exec not yet supported by dmtcp");
// }
