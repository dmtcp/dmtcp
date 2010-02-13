/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *   This file is part of the dmtcp/src module of DMTCP (DMTCP:dmtcp/src).  *
 *                                                                          *
 *  DMTCP:dmtcp/src is free software: you can redistribute it and/or        *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP:dmtcp/src is distributed in the hope that it will be useful,      *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include <stdarg.h>
#include <stdlib.h>
#include <vector>
#include <list>
#include <string>
#include "constants.h"
#include "connectionmanager.h"
#include "uniquepid.h"
#include "dmtcpworker.h"
#include "virtualpidtable.h"
#include "syscallwrappers.h"
#include "syslogcheckpointer.h"
#include  "../jalib/jconvert.h"
#include  "../jalib/jassert.h"

#define INITIAL_ARGV_MAX 32

static void protectLD_PRELOAD()
{
  const char* actual = getenv ( "LD_PRELOAD" );
  const char* expctd = getenv ( ENV_VAR_HIJACK_LIB );

  JTRACE ("LD_PRELOAD") (actual) (expctd);

  if ( actual!=0 && expctd!=0 ){
    JASSERT ( strcmp ( actual,expctd ) == 0 )( actual ) ( expctd )
      .Text ( "eeek! Someone stomped on LD_PRELOAD" );
  }
}

static pid_t forkChild()
{
  while ( 1 ) {

    pid_t childPid = _real_fork();

    if ( childPid == -1 ) {
      // fork() failed
      return childPid;
    } else if ( childPid == 0 ) { 
      /* child process */
      if ( dmtcp::VirtualPidTable::isConflictingPid ( getpid() ) ) {
        _exit(1);
      } else {
        return childPid;
      }
    } else { 
      /* Parent Process */
      if ( dmtcp::VirtualPidTable::isConflictingPid ( childPid ) ) {
        JTRACE( "PID Conflict, creating new child" ) (childPid);
        _real_waitpid ( childPid, NULL, 0 );
      } else {
        return childPid;
      }
    }
  }
  return -1;
}

static pid_t fork_work()
{
  protectLD_PRELOAD();

  /* Little bit cheating here: child_time should be same for both parent and
   * child, thus we compute it before forking the child. */
  time_t child_time = time ( NULL );
  //pid_t child_pid = _real_fork();
  pid_t child_pid = forkChild();
  if (child_pid < 0) {
    return child_pid;
  }

  long child_host = dmtcp::UniquePid::ThisProcess().hostid();

  dmtcp::UniquePid parent = dmtcp::UniquePid::ThisProcess();

  if ( child_pid == 0 )
  {
    child_pid = _real_getpid();
    dmtcp::UniquePid child = dmtcp::UniquePid ( child_host, child_pid, child_time );
#ifdef DEBUG
    //child should get new logfile
    dmtcp::ostringstream o;
    o << dmtcp::UniquePid::getTmpDir(getenv(ENV_VAR_TMPDIR)) 
      << "/jassertlog." << child.toString();
    JASSERT_SET_LOGFILE (o.str());
#endif


    JTRACE ( "fork()ed [CHILD]" ) ( child ) ( parent ) ( getenv ( "LD_PRELOAD" ) );

    //fix the mutex
    _dmtcp_remutex_on_fork();

    //update ThisProcess()
    dmtcp::UniquePid::resetOnFork ( child );

#ifdef PID_VIRTUALIZATION
    dmtcp::VirtualPidTable::Instance().resetOnFork( );
#endif

    dmtcp::SyslogCheckpointer::resetOnFork();

    //rewrite socket table
    //         dmtcp::SocketTable::Instance().onForkUpdate(parent,child);

    //make new connection to coordinator
    dmtcp::DmtcpWorker::resetOnFork();

    JTRACE ( "fork() done [CHILD]" ) ( child ) ( parent ) ( getenv ( "LD_PRELOAD" ) );

    return 0;
  }
  else
  {
    dmtcp::UniquePid child = dmtcp::UniquePid ( child_host, child_pid, child_time );

#ifdef PID_VIRTUALIZATION
    dmtcp::VirtualPidTable::Instance().insert ( child_pid, child );
#endif

    JTRACE ( "fork()ed [PARENT] done" ) ( child ) ( getenv ( "LD_PRELOAD" ) );;

    //         _dmtcp_lock();

    //rewrite socket table
    //         dmtcp::SocketTable::Instance().onForkUpdate(parent,child);

    //         _dmtcp_unlock();

    //         JTRACE("fork() done [PARENT]")(child);

    return child_pid;
  }
}

extern "C" pid_t fork()
{
  /* Acquire the wrapperExeution lock to prevent checkpoint to happen while
   * processing this system call.
   */
  WRAPPER_EXECUTION_LOCK_LOCK();

  int retVal = fork_work();

  if (retVal != 0) {
    WRAPPER_EXECUTION_LOCK_UNLOCK();
  }

  return retVal;
}


extern "C" pid_t vfork()
{
  JTRACE ( "vfork wrapper calling fork" );
  // This might not preserve the full semantics of vfork. 
  // Used for checkpointing gdb.
  return fork();
}

static void dmtcpPrepareForExec()
{
  protectLD_PRELOAD();
  dmtcp::string serialFile = dmtcp::UniquePid::dmtcpTableFilename();
  jalib::JBinarySerializeWriter wr ( serialFile );
  dmtcp::UniquePid::serialize ( wr );
  dmtcp::KernelDeviceToConnection::Instance().serialize ( wr );
#ifdef PID_VIRTUALIZATION
  dmtcp::VirtualPidTable::Instance().prepareForExec();
  dmtcp::VirtualPidTable::Instance().serialize ( wr );
#endif
  setenv ( ENV_VAR_SERIALFILE_INITIAL, serialFile.c_str(), 1 );
  JTRACE ( "Prepared for Exec" );
}

static const char* ourImportantEnvs[] =
{
  "LD_PRELOAD",
  ENV_VARS_ALL //expands to a long list
};
#define ourImportantEnvsCnt ((sizeof(ourImportantEnvs))/(sizeof(const char*)))

static bool isImportantEnv ( dmtcp::string str )
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
  static dmtcp::vector<char*> envVect;
  static dmtcp::list<dmtcp::string> strStorage;
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
      strStorage.push_back ( dmtcp::string ( ourImportantEnvs[i] ) + '=' + v );
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
  JTRACE ( "execve() wrapper" ) ( filename );
  /* Acquire the wrapperExeution lock to prevent checkpoint to happen while
   * processing this system call.
   */
  WRAPPER_EXECUTION_LOCK_LOCK();

  dmtcpPrepareForExec();
  int retVal = _real_execve ( filename, argv, patchUserEnv ( envp ) );

  WRAPPER_EXECUTION_LOCK_UNLOCK();

  return retVal;
}

extern "C" int fexecve ( int fd, char *const argv[], char *const envp[] )
{
  //TODO: Right now we assume the user hasn't clobbered our setup of envp
  //(like LD_PRELOAD), we should really go check to make sure it hasn't
  //been destroyed....
  JTRACE ( "fexecve() wrapper" ) ( fd );
  /* Acquire the wrapperExeution lock to prevent checkpoint to happen while
   * processing this system call.
   */
  WRAPPER_EXECUTION_LOCK_LOCK();

  dmtcpPrepareForExec();
  int retVal = _real_fexecve ( fd, argv, patchUserEnv ( envp ) );

  WRAPPER_EXECUTION_LOCK_UNLOCK();

  return retVal;
}

extern "C" int execv ( const char *path, char *const argv[] )
{
  JTRACE ( "execv() wrapper" ) ( path );
  /* Acquire the wrapperExeution lock to prevent checkpoint to happen while
   * processing this system call.
   */
  WRAPPER_EXECUTION_LOCK_LOCK();

  dmtcpPrepareForExec();
  int retVal = _real_execv ( path, argv );

  WRAPPER_EXECUTION_LOCK_UNLOCK();

  return retVal;
}

extern "C" int execvp ( const char *file, char *const argv[] )
{
  JTRACE ( "execvp() wrapper" ) ( file );
  /* Acquire the wrapperExeution lock to prevent checkpoint to happen while
   * processing this system call.
   */
  WRAPPER_EXECUTION_LOCK_LOCK();

  dmtcpPrepareForExec();
  int retVal = _real_execvp ( file, argv );

  WRAPPER_EXECUTION_LOCK_UNLOCK();

  return retVal;
}

extern "C" int execl ( const char *path, const char *arg, ... )
{
  JTRACE ( "execl() wrapper" ) ( path );

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
  JTRACE ( "execlp() wrapper" ) ( file );

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
  JTRACE ( "execle() wrapper" ) ( path );

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

extern int do_system (const char *line);

extern "C" int system (const char *line)
{
  JTRACE ( "before system(), checkpointing may not work" )
    ( line ) ( getenv ( ENV_VAR_HIJACK_LIB ) ) ( getenv ( "LD_PRELOAD" ) );
  protectLD_PRELOAD();

  if (line == NULL)
    /* Check that we have a command processor available.  It might
       not be available after a chroot(), for example.  */
    return do_system ("exit 0") == 0;

  int result = do_system (line);

  JTRACE ( "after system()" );

  return result;
}
