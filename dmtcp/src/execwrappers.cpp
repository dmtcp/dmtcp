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
#include  "../jalib/jassert.h"
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
#include  "../jalib/jconvert.h"
#include "constants.h"
#include <vector>
#include <list>
#include <string>

#define INITIAL_ARGV_MAX 32

static void protectLD_PRELOAD();

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
  protectLD_PRELOAD();
  pid_t child_pid = _real_fork();
  time_t child_time = time ( NULL );
  long child_host = dmtcp::UniquePid::ThisProcess().hostid();

  dmtcp::UniquePid parent = dmtcp::UniquePid::ThisProcess();

  if ( child_pid == 0 )
  {
    child_pid = getpid();
#ifdef DEBUG
    //child should get new logfile
    JASSERT_SET_LOGFILE ( "/tmp/jassertlog." + jalib::XToString ( child_pid ) );
#endif

    dmtcp::UniquePid child = dmtcp::UniquePid ( child_host,child_pid,child_time );

    JTRACE ( "fork()ed [CHILD]" ) ( child ) ( getenv ( "LD_PRELOAD" ) );

    //fix the mutex
    _dmtcp_remutex_on_fork();

    //update ThisProcess()
    dmtcp::UniquePid::resetOnFork ( child );

    dmtcp::SyslogCheckpointer::resetOnFork();

    //rewrite socket table
    //         dmtcp::SocketTable::Instance().onForkUpdate(parent,child);

    //make new connection to coordinator
    dmtcp::DmtcpWorker::resetOnFork();

    JTRACE ( "fork() done [CHILD]" ) ( child ) ( getenv ( "LD_PRELOAD" ) );

    return 0;
  }
  else
  {
    dmtcp::UniquePid child = dmtcp::UniquePid ( child_host,child_pid,child_time );

    JTRACE ( "fork()ed [PARENT] done" ) ( child ) ( getenv ( "LD_PRELOAD" ) );;

    //         _dmtcp_lock();

    //rewrite socket table
    //         dmtcp::SocketTable::Instance().onForkUpdate(parent,child);

    //         _dmtcp_unlock();

    //         JTRACE("fork() done [PARENT]")(child);

    return child_pid;
  }
}

extern "C" char *ptsname ( int fd )
{
  JTRACE ( "ptsname() promoted to ptsname_r()" );
  static char tmpbuf[1024];

  if ( ptsname_r ( fd, tmpbuf, sizeof ( tmpbuf ) ) != 0 )
  {
    return NULL;
  }

  return tmpbuf;
}

extern "C" int ptsname_r ( int fd, char * buf, size_t buflen )
{
  JTRACE ( "Calling ptsname_r" );
  char device[1024];
  const char *ptr;

  int rv = _real_ptsname_r ( fd, device, sizeof ( device ) );
  if ( rv != 0 )
  {
    JTRACE ( "ptsname_r failed" );
    return rv;
  }

  ptr = dmtcp::UniquePid::ptsSymlinkFilename ( device );

  if ( strlen ( ptr ) >=buflen )
  {
    JWARNING ( false ) ( ptr ) ( strlen ( ptr ) ) ( buflen )
      .Text ( "fake ptsname() too long for user buffer" );
    errno = ERANGE;
    return -1;
  }

  if ( dmtcp::PtsToSymlink::Instance().isDuplicate(device) == true )
  {
    std::string name = dmtcp::PtsToSymlink::Instance().getFilename(device);
    strcpy ( buf, name.c_str() );
    return rv;
  }

  JASSERT ( symlink ( device, ptr ) == 0 ) ( device ) ( ptr ) ( JASSERT_ERRNO )
    .Text ( "symlink() failed" );

  strcpy ( buf, ptr );

  //  dmtcp::PtsConnection::PtsType type = dmtcp::PtsConnection::Pt_Master;
  //  dmtcp::PtsConnection *master = new dmtcp::PtsConnection ( device, ptr, type );
  //  dmtcp::KernelDeviceToConnection::Instance().create ( fd, master );

  dmtcp::PtsToSymlink::Instance().add ( device, buf );

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
  protectLD_PRELOAD();
  std::string serialFile = dmtcp::UniquePid::dmtcpTableFilename();
  jalib::JBinarySerializeWriter wr ( serialFile );
  dmtcp::KernelDeviceToConnection::Instance().serialize ( wr );
  setenv ( ENV_VAR_SERIALFILE_INITIAL, serialFile.c_str(), 1 );
  JTRACE ( "Prepared for Exec" );
}

static void protectLD_PRELOAD()
{
  const char* actual = getenv ( "LD_PRELOAD" );
  const char* expctd = getenv ( ENV_VAR_HIJACK_LIB );

  if ( actual!=0 && expctd!=0 ){
    JASSERT ( strcmp ( actual,expctd ) ==0 )( actual ) ( expctd )
      .Text ( "eeek! Someone stomped on LD_PRELOAD" );
  }
}

static const char* ourImportantEnvs[] =
{
  "LD_PRELOAD",
  ENV_VARS_ALL //expands to a long list
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

#ifdef DEBUG
  const static bool dbg = true;
#else
  const static bool dbg = false;
#endif

  JTRACE ( "patching user envp..." ) ( getenv ( "LD_PRELOAD" ) );

  //pack up our ENV into the new ENV
  for ( size_t i=0; i<ourImportantEnvsCnt; ++i )
  {
    const char* v = getenv ( ourImportantEnvs[i] );
    if ( v != NULL )
    {
      strStorage.push_back ( std::string ( ourImportantEnvs[i] ) + '=' + v );
      envVect.push_back ( &strStorage.back() [0] );
      if(dbg) JASSERT_STDERR << "     addenv[dmtcp]:" << strStorage.back() << '\n';
    }
  }

  for ( ;*envp != NULL; ++envp )
  {
    if ( isImportantEnv ( *envp ) )
    {
      if(dbg) JASSERT_STDERR << "     skipping: " << *envp << '\n';
      continue;
    }
    strStorage.push_back ( *envp );
    envVect.push_back ( &strStorage.back() [0] );
    if(dbg) JASSERT_STDERR << "     addenv[user]:" << strStorage.back() << '\n';
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
  size_t argv_max = INITIAL_ARGV_MAX;
  const char *initial_argv[INITIAL_ARGV_MAX];
  const char **argv = initial_argv;
  va_list args;

  argv[0] = arg;

  va_start (args, arg);
  unsigned int i = 0;
  while (argv[i++] != NULL)
  {
    if (i == argv_max)
    {
      argv_max *= 2;
      const char **nptr = (const char**) realloc (argv == initial_argv ? NULL : argv,
          argv_max * sizeof (const char *));
      if (nptr == NULL)
      {
        if (argv != initial_argv)
          free (argv);
        return -1;
      }
      if (argv == initial_argv)
        /* We have to copy the already filled-in data ourselves.  */
        memcpy (nptr, argv, i * sizeof (const char *));

      argv = nptr;
    }

    argv[i] = va_arg (args, const char *);
  }
  va_end (args);

  int ret = execv (path, (char *const *) argv);
  if (argv != initial_argv)
    free (argv);

  return ret;
}


extern "C" int execlp ( const char *file, const char *arg, ... )
{
  size_t argv_max = INITIAL_ARGV_MAX;
  const char *initial_argv[INITIAL_ARGV_MAX];
  const char **argv = initial_argv;
  va_list args;

  argv[0] = arg;

  va_start (args, arg);
  unsigned int i = 0;
  while (argv[i++] != NULL)
  {
    if (i == argv_max)
    {
      argv_max *= 2;
      const char **nptr = (const char**) realloc (argv == initial_argv ? NULL : argv,
          argv_max * sizeof (const char *));
      if (nptr == NULL)
      {
        if (argv != initial_argv)
          free (argv);
        return -1;
      }
      if (argv == initial_argv)
        /* We have to copy the already filled-in data ourselves.  */
        memcpy (nptr, argv, i * sizeof (const char *));

      argv = nptr;
    }

    argv[i] = va_arg (args, const char *);
  }
  va_end (args);

  int ret = execvp (file, (char *const *) argv);
  if (argv != initial_argv)
    free (argv);

  return ret;
}


extern "C" int execle(const char *path, const char *arg, ...)
{
  size_t argv_max = INITIAL_ARGV_MAX;
  const char *initial_argv[INITIAL_ARGV_MAX];
  const char **argv = initial_argv;
  va_list args;
  argv[0] = arg;

  va_start (args, arg);
  unsigned int i = 0;
  while (argv[i++] != NULL)
  {
    if (i == argv_max)
    {
      argv_max *= 2;
      const char **nptr = (const char**) realloc (argv == initial_argv ? NULL : argv,
          argv_max * sizeof (const char *));
      if (nptr == NULL)
      {
        if (argv != initial_argv)
          free (argv);
        return -1;
      }
      if (argv == initial_argv)
        /* We have to copy the already filled-in data ourselves.  */
        memcpy (nptr, argv, i * sizeof (const char *));

      argv = nptr;
    }

    argv[i] = va_arg (args, const char *);
  }

  const char *const *envp = va_arg (args, const char *const *);
  va_end (args);

  int ret = execve (path, (char *const *) argv, (char *const *) envp);
  if (argv != initial_argv)
    free (argv);

  return ret;
}

extern "C" int system ( const char *cmd )
{
  JTRACE ( "before system(), checkpointing may not work" )
    ( cmd ) ( getenv ( ENV_VAR_HIJACK_LIB ) ) ( getenv ( "LD_PRELOAD" ) );
  protectLD_PRELOAD();
  int rv = _real_system ( cmd );
  JTRACE ( "after system()" );
  //JASSERT ( false )( cmd ).Text ( "system() is called" );
  return rv;
}
