/****************************************************************************
 *   Copyright (C) 2006-2010 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <list>
#include <string>
#include "constants.h"
#include "connectionmanager.h"
#include "uniquepid.h"
#include "dmtcpworker.h"
#include "virtualpidtable.h"
#include "sysvipc.h"
#include "syscallwrappers.h"
#include "syslogwrappers.h"
#include "util.h"
#include  "../jalib/jconvert.h"
#include  "../jalib/jassert.h"
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/personality.h>

#define INITIAL_ARGV_MAX 32

#ifdef DEBUG
  const static bool dbg = true;
#else
  const static bool dbg = false;
#endif

static pid_t forkChild ( long child_host, time_t child_time )
{
  while ( 1 ) {

    pid_t child_pid = _real_fork();

    if ( child_pid == -1 ) {
      // fork() failed
      return child_pid;
    } else if ( child_pid == 0 ) {
      /* child process */

      JALIB_RESET_ON_FORK ();

      dmtcp::UniquePid child = dmtcp::UniquePid ( child_host, _real_getpid(),
                                                  child_time );
      //update ThisProcess()
      dmtcp::UniquePid::resetOnFork ( child );

      dmtcp::Util::initializeLogFile(jalib::Filesystem::GetProgramName()
                              + " (forked)");

#ifdef PID_VIRTUALIZATION
      if ( dmtcp::VirtualPidTable::isConflictingPid ( _real_getpid() ) ) {
        _exit(DMTCP_FAIL_RC);
      } else {
        return child_pid;
      }
#else
      return child_pid;
#endif
    } else {
      /* Parent Process */
#ifdef PID_VIRTUALIZATION
      if ( dmtcp::VirtualPidTable::isConflictingPid ( child_pid ) ) {
        JTRACE( "PID Conflict, creating new child" ) (child_pid);
        _real_waitpid ( child_pid, NULL, 0 );
      } else {
        return child_pid;
      }
#else
      return child_pid;
#endif
    }
  }
  return -1;
}

static pid_t fork_work()
{
  /* Little bit cheating here: child_time should be same for both parent and
   * child, thus we compute it before forking the child. */
  time_t child_time = time ( NULL );
  long child_host = dmtcp::UniquePid::ThisProcess().hostid();
  dmtcp::UniquePid parent = dmtcp::UniquePid::ThisProcess();

  //pid_t child_pid = _real_fork();
  pid_t child_pid = forkChild ( child_host, child_time );
  if (child_pid < 0) {
    return child_pid;
  }

  if ( child_pid == 0 ) {
    dmtcp::UniquePid child = dmtcp::UniquePid::ThisProcess();
    JTRACE ( "fork()ed [CHILD]" ) ( child ) ( parent );

    //fix the mutex
    _dmtcp_remutex_on_fork();

#ifdef PID_VIRTUALIZATION
    dmtcp::VirtualPidTable::instance().resetOnFork( );
#endif

    dmtcp::SyslogCheckpointer::resetOnFork();

    //rewrite socket table
    //         dmtcp::SocketTable::instance().onForkUpdate(parent,child);

    //make new connection to coordinator
    dmtcp::DmtcpWorker::resetOnFork();

    JTRACE ( "fork() done [CHILD]" ) ( child ) ( parent );

    return 0;
  } else {
    dmtcp::UniquePid child = dmtcp::UniquePid ( child_host, child_pid, child_time );

#ifdef PID_VIRTUALIZATION
    dmtcp::VirtualPidTable::instance().insert ( child_pid, child );
#endif

    JTRACE ( "fork()ed [PARENT] done" ) ( child );;

    //         _dmtcp_lock();

    //rewrite socket table
    //         dmtcp::SocketTable::instance().onForkUpdate(parent,child);

    //         _dmtcp_unlock();

    //         JTRACE("fork() done [PARENT]")(child);

    return child_pid;
  }
}

static void prepareForFork()
{
  dmtcp::KernelDeviceToConnection::instance().prepareForFork();
}

extern "C" pid_t fork()
{
  /* Acquire the wrapperExeution lock to prevent checkpoint to happen while
   * processing this system call.
   */
  WRAPPER_EXECUTION_DISABLE_CKPT();

  prepareForFork();
  int retVal = fork_work();

  if (retVal != 0) {
    WRAPPER_EXECUTION_ENABLE_CKPT();
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

// Special short-lived processes from executables like /lib/libc.so.6
//   and man setuid/setgid executables cannot be loaded with LD_PRELOAD.
// Since they're short-lived, we execute them while holding a lock
//   delaying checkpointing.
static void execShortLivedProcessAndExit(const char *path, char *const argv[])
{
  unsetenv("LD_PRELOAD"); // /lib/ld.so won't let us preload if exec'ing lib
  const unsigned int bufSize = 100000;
  char *buf = (char*)JALLOC_HELPER_MALLOC(bufSize);
  memset(buf, 0, bufSize);
  FILE *output;
  if (argv[0] == NULL) {
    output = popen(path, "r");
  } else {
    dmtcp::string command = path;
    for (int i = 1; argv[i] != NULL; i++)
      command = command + " " + argv[i];
    output = popen(command.c_str(), "r");
  }
  int numRead = fread(buf, 1, bufSize, output);
  numRead++, numRead--; // suppress unused-var warning

  pclose(output); // /lib/libXXX process is now done; can checkpoint now
  // FIXME:  code currently allows wrapper to proceed without lock if
  //   it was busy because of a writer.  The unlock will then fail below.
  bool __wrapperExecutionLockAcquired = true; // needed for LOCK_UNLOCK macro
  WRAPPER_EXECUTION_ENABLE_CKPT();
  // We  are now the new /lib/libXXX process, and it's safe for DMTCP to ckpt us.
  printf("%s", buf); // print buf, which is what /lib/libXXX would print
  JALLOC_HELPER_FREE(buf);
  exit(0);
}

// FIXME:  Unify this code with code prior to execvp in dmtcp_checkpoint.cpp
//   Can use argument to dmtcpPrepareForExec() or getenv("DMTCP_...")
//   from DmtcpWorker constructor, to distinguish the two cases.
static void dmtcpPrepareForExec(const char *path, char *const argv[],
                                char **filename, char ***newArgv)
{
  JTRACE("Preparing for Exec") (path);

  const char * libPrefix = "/lib/lib";
  const char * lib64Prefix = "/lib64/lib";
  if (path != NULL && dmtcp::Util::strStartsWith(path, libPrefix))
    execShortLivedProcessAndExit(path, argv);
  if (path != NULL && dmtcp::Util::strStartsWith(path, lib64Prefix))
    execShortLivedProcessAndExit(path, argv);
  // Needed for /usr/libexec/utempter/utempter and other short-lived
  //  setuid/setgid processes.
  // FIXME:  USE THIS FOR ALL setuid/setgid PROCESSES EXCEPT ONES THAT
  //         WE DIRECTLY HANDLE, LIKE 'screen'.  (Need to name special routine,
  //         execScreenProcess() ??)
  if (path != NULL &&
      dmtcp::Util::strEndsWith(path, "/utempter")) {
    JTRACE("Trying to exec: utempter")(path)(argv[0])(argv[1]);
    execShortLivedProcessAndExit(path, argv);
  }

  // FIXME:  SEE COMMENTS IN dmtcp_checkpoint.cpp, rev. 1087; AND CHANGE THIS.
  if (dmtcp::Util::isSetuid(path)) {
    if (dmtcp::Util::isScreen(path)) {
      dmtcp::Util::setScreenDir();
    }
    // THIS NEXT LINE IS DANGEROUS.  MOST setuid PROGRAMS CAN'T RUN UNPRIVILEGED
    dmtcp::Util::patchArgvIfSetuid(path, argv, newArgv);
    // BUG:  dmtcp::Util::patchArgvIfSetuid() DOES NOT SET newArgv WHEN COPYING
    //   BINARY IN CODE RE-FACTORING FROM REVISION 911.
    *filename = (*newArgv)[0];
  } else {
    *filename = (char*)path;
    *newArgv = (char**)argv;
  }

  dmtcp::string serialFile = dmtcp::UniquePid::dmtcpTableFilename();
  jalib::JBinarySerializeWriter wr ( serialFile );
  dmtcp::UniquePid::serialize ( wr );
  dmtcp::KernelDeviceToConnection::instance().serialize ( wr );
#ifdef PID_VIRTUALIZATION
  dmtcp::VirtualPidTable::instance().serialize ( wr );
  dmtcp::SysVIPC::instance().serialize ( wr );
#endif

  setenv ( ENV_VAR_SERIALFILE_INITIAL, serialFile.c_str(), 1 );
  JTRACE ( "Will exec filename instead of path" ) ( path ) (*filename);

  dmtcp::Util::adjustRlimitStack();

  dmtcp::Util::prepareDlsymWrapper();

  JTRACE ( "Prepared for Exec" ) ( getenv( "LD_PRELOAD" ) );
}

static void dmtcpProcessFailedExec(const char *path, char *newArgv[])
{
  int saved_errno = errno;

  if (dmtcp::Util::isSetuid(path)) {
    dmtcp::Util::freePatchedArgv(newArgv);
  }

  const char *preload = getenv("LD_PRELOAD");
  if (preload != NULL &&
      dmtcp::Util::strStartsWith(preload, dmtcp::DmtcpWorker::ld_preload_c)) {
    setenv("LD_PRELOAD",
           preload + strlen(dmtcp::DmtcpWorker::ld_preload_c) + 1,
           1);
  }

  unsetenv(ENV_VAR_DLSYM_OFFSET);

  JTRACE ( "Processed failed Exec Attempt" ) (path) ( getenv( "LD_PRELOAD" ) );
  errno = saved_errno;
}

static dmtcp::string getUpdatedLdPreload(const char* currLdPreload = NULL)
{
  dmtcp::string preload (dmtcp::DmtcpWorker::ld_preload_c);
  if (currLdPreload != NULL) {
    preload = preload + ":" + currLdPreload;
  } else if (getenv("LD_PRELOAD") != NULL) {
    preload = preload + ":" + getenv("LD_PRELOAD");
  }
  return preload;
}

static const char* ourImportantEnvs[] =
{
  ENV_VARS_ALL //expands to a long list
};
#define ourImportantEnvsCnt ((sizeof(ourImportantEnvs))/(sizeof(const char*)))

static bool isImportantEnv ( dmtcp::string str )
{
  str = str.substr(0, str.find("="));

  for ( size_t i=0; i<ourImportantEnvsCnt; ++i ) {
    if ( str == ourImportantEnvs[i] )
      return true;
  }
  return false;
}

static dmtcp::vector<dmtcp::string> copyUserEnv ( char *const envp[] )
{
  dmtcp::vector<dmtcp::string> strStorage;

  dmtcp::ostringstream out;
  out << "non-DMTCP env vars:\n";
  for ( ; *envp != NULL; ++envp ) {
    if ( isImportantEnv ( *envp ) ) {
      if (dbg) {
        out << "     skipping: " << *envp << '\n';
      }
      continue;
    }
    dmtcp::string e(*envp);
    strStorage.push_back (e);
    if(dbg) {
      out << "     addenv[user]:" << strStorage.back() << '\n';
    }
  }
  JTRACE ( "Creating a copy of (non-DMTCP) user env vars..." ) (out.str());

  return strStorage;
}

static dmtcp::vector<const char*> patchUserEnv (dmtcp::vector<dmtcp::string>
                                                  &envp)
{
  dmtcp::vector<const char*> envVect;
  const char *userPreloadStr = NULL;
  envVect.clear();
  JASSERT(envVect.size() == 0);

  dmtcp::ostringstream out;
  out << "non-DMTCP env vars:\n";

  for ( size_t i = 0; i < envp.size(); i++) {
    if ( isImportantEnv ( envp[i].c_str() ) ) {
      if (dbg) {
        out << "     skipping: " << envp[i] << '\n';
      }
      continue;
    }
    if (dmtcp::Util::strStartsWith(envp[i], "LD_PRELOAD=")) {
      userPreloadStr = envp[i].c_str() + strlen("LD_PRELOAD=");
      continue;
    }

    envVect.push_back (envp[i].c_str());
    if(dbg) {
      out << "     addenv[user]:" << envVect.back() << '\n';
    }
  }
  JTRACE ( "Creating a copy of (non-DMTCP) user env vars..." ) (out.str());

  //pack up our ENV into the new ENV
  out.str("DMTCP env vars:\n");
  for ( size_t i=0; i<ourImportantEnvsCnt; ++i ) {
    const char* v = getenv ( ourImportantEnvs[i] );
    if ( v != NULL ) {
      envp.push_back ( dmtcp::string ( ourImportantEnvs[i] ) + '=' + v );
      const char *ptr = envp.back().c_str();
      JASSERT(ptr != NULL);
      envVect.push_back(ptr);
      if(dbg) {
        out << "     addenv[dmtcp]:" << envVect.back() << '\n';
      }
    }
  }

  dmtcp::string ldPreloadStr = "LD_PRELOAD=";
  ldPreloadStr += getUpdatedLdPreload(userPreloadStr);

  envp.push_back(ldPreloadStr);
  envVect.push_back(envp.back().c_str());
  if(dbg) {
    out << "     addenv[dmtcp]:" << envVect.back() << '\n';
  }

  JTRACE ( "patching user envp..." )  (out.str());

  envVect.push_back ( NULL );

  JTRACE ( "Done patching environ" );
  return envVect;
}

extern "C" int execve ( const char *filename, char *const argv[],
                        char *const envp[] )
{
  JTRACE ( "execve() wrapper" ) ( filename );

  /* Acquire the wrapperExeution lock to prevent checkpoint to happen while
   * processing this system call.
   */
  WRAPPER_EXECUTION_DISABLE_CKPT();

  dmtcp::vector<dmtcp::string> origUserEnv = copyUserEnv( envp );

  char *newFilename;
  char **newArgv;
  dmtcpPrepareForExec(filename, argv, &newFilename, &newArgv);

  dmtcp::vector<const char*> envVect = patchUserEnv(origUserEnv);

  int retVal = _real_execve ( newFilename, newArgv, (char* const*)&envVect[0]);

  dmtcpProcessFailedExec(filename, newArgv);

  WRAPPER_EXECUTION_ENABLE_CKPT();

  return retVal;
}

extern "C" int execv ( const char *path, char *const argv[] )
{
  JTRACE ( "execv() wrapper, calling execve with environ" ) ( path );
  return execve(path, argv, environ);
}

extern "C" int execvp ( const char *filename, char *const argv[] )
{
  JTRACE ( "execvp() wrapper" ) ( filename );
  /* Acquire the wrapperExeution lock to prevent checkpoint to happen while
   * processing this system call.
   */
  WRAPPER_EXECUTION_DISABLE_CKPT();

  char *newFilename;
  char **newArgv;
  dmtcpPrepareForExec(filename, argv, &newFilename, &newArgv);
  setenv("LD_PRELOAD", getUpdatedLdPreload().c_str(), 1);

  int retVal = _real_execvp ( newFilename, newArgv );

  dmtcpProcessFailedExec(filename, newArgv);

  WRAPPER_EXECUTION_ENABLE_CKPT();

  return retVal;
}

// This function first appeared in glibc 2.11
extern "C" int execvpe ( const char *filename, char *const argv[],
                         char *const envp[] )
{
  JTRACE ( "execvpe() wrapper" ) ( filename );
  /* Acquire the wrapperExeution lock to prevent checkpoint to happen while
   * processing this system call.
   */
  WRAPPER_EXECUTION_DISABLE_CKPT();

  dmtcp::vector<dmtcp::string> origUserEnv = copyUserEnv( envp );

  char *newFilename;
  char **newArgv;
  dmtcpPrepareForExec(filename, argv, &newFilename, &newArgv);

  dmtcp::vector<const char*> envVect = patchUserEnv(origUserEnv);

  int retVal = _real_execvpe(newFilename, newArgv, (char* const*)&envVect[0]);

  dmtcpProcessFailedExec(filename, newArgv);

  WRAPPER_EXECUTION_ENABLE_CKPT();

  return retVal;
}

extern "C" int fexecve ( int fd, char *const argv[], char *const envp[] )
{
  char buf[sizeof "/proc/self/fd/" + sizeof (int) * 3];
  snprintf (buf, sizeof (buf), "/proc/self/fd/%d", fd);

  JTRACE ("fexecve() wrapper calling execve()") (fd) (buf);
  return execve(buf, argv, envp);
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

// See comment in glibcsystem.cpp for why this exists and how it works.
extern int do_system (const char *line);

extern "C" int system (const char *line)
{
  JTRACE ( "before system(), checkpointing may not work" )
    ( line ) ( getenv ( ENV_VAR_HIJACK_LIB ) ) ( getenv ( "LD_PRELOAD" ) );

  if (line == NULL)
    /* Check that we have a command processor available.  It might
       not be available after a chroot(), for example.  */
    return do_system ("exit 0") == 0;

  int result = do_system (line);

  JTRACE ( "after system()" );

  return result;
}
