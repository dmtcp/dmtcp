/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include <sys/syscall.h>
#include "constants.h"
#include "uniquepid.h"
#include "dmtcpworker.h"
#include "processinfo.h"
#include "syscallwrappers.h"
#include "syslogwrappers.h"
#include "util.h"
#include "coordinatorapi.h"
#include "mtcpinterface.h"
#include "shareddata.h"
#include "threadsync.h"
#include  "../jalib/jconvert.h"
#include  "../jalib/jassert.h"
#include  "../jalib/jfilesystem.h"

#define INITIAL_ARGV_MAX 32

using namespace dmtcp;

#ifdef DEBUG
  const static bool dbg = true;
#else
  const static bool dbg = false;
#endif

static bool pthread_atfork_enabled = false;
static uint64_t child_time;
static dmtcp::CoordinatorAPI coordinatorAPI;

// Allow plugins to call fork/exec/system to perform specific tasks during
// preCKpt/postCkpt/PostRestart etc. event.
static bool isPerformingCkptRestart()
{
  if (dmtcp::WorkerState::currentState() != dmtcp::WorkerState::UNKNOWN &&
      dmtcp::WorkerState::currentState() != dmtcp::WorkerState::RUNNING) {
    return true;
  }
  return false;
}

static bool isBlacklistedProgram(const char *path)
{
  dmtcp::string programName = jalib::Filesystem::BaseName(path);

  JASSERT(programName != "dmtcp_coordinator" &&
          programName != "dmtcp_launch"  &&
          programName != "dmtcp_restart"     &&
          programName != "mtcp_restart")
    (programName) .Text("This program should not be run under ckpt control");

  /*
   * When running gdb or any shell which does a waitpid() on the child
   * processes, executing dmtcp_command from within gdb session / shell results
   * in process getting hung up because:
   *   gdb shell dmtcp_command -c => hangs because gdb forks off a new process
   *   and it does a waitpid  (in which we block signals) ...
   */
  if (programName == "dmtcp_command") {
    //make sure coordinator connection is closed
    _real_close (PROTECTED_COORD_FD);

    pid_t cpid = _real_fork();
    JASSERT(cpid != -1);
    if (cpid != 0) {
      _real_exit(0);
    }
  }

  if (programName == "dmtcp_nocheckpoint" || programName == "dmtcp_command" ||
      programName == "ssh") {
    return true;
  }
  return false;
}

LIB_PRIVATE void pthread_atfork_prepare()
{
  /* FIXME: The user process might register a fork prepare handler with
   * pthread_atfork. That handler will be called _after_ we have acquired the
   * wrapper-exec lock in exclusive mode. This can lead to a deadlock situation
   * if the user process decides to do some operations that require calling a
   * dmtcp wrapper that require the wrapper-exec lock.
   *
   * Also, the preparation that dmtcp needs to do for fork() should be done
   * right before the child process is created, i.e. after all the user
   * handlers have been invoked. Fortunately, pthread_atfork prepare handlers
   * are called in reverse order or registration (as opposed to parent and
   * child handlers which are called in the order of registration), thus our
   * prepare handle will be called at the very last.
   *
   * FIXME: PID-conflict detection poses yet another serious problem. On a
   * pid-conflict, _real_fork() will be called more than once, resulting in
   * multiple calls of user-defined prepare handlers. This is undesired and can
   * cause several issues. One solution to this problem is to call the fork
   * system call directly whenever a tid-conflict is detected, however, it
   * might have some other side-effects.  Another possible solution would be to
   * have pid-virtualization plugin, which always assigns virtual pids, to the
   * newly created processes, and thus avoiding the pid-conflict totally.
   */
  return;
}

LIB_PRIVATE void pthread_atfork_parent()
{
  return;
}

LIB_PRIVATE void pthread_atfork_child()
{
  if (!pthread_atfork_enabled) {
    return;
  }
  pthread_atfork_enabled = false;

  uint64_t host = dmtcp::UniquePid::ThisProcess().hostid();
  dmtcp::UniquePid parent = dmtcp::UniquePid::ThisProcess();
  dmtcp::UniquePid child = dmtcp::UniquePid(host, getpid(), child_time);
  dmtcp::string child_name = jalib::Filesystem::GetProgramName() + "_(forked)";
  JALIB_RESET_ON_FORK();
  _dmtcp_remutex_on_fork();
  dmtcp::SyslogCheckpointer::resetOnFork();
  dmtcp::ThreadSync::resetLocks();

  dmtcp::UniquePid::resetOnFork(child);
  dmtcp::Util::initializeLogFile(child_name);

  dmtcp::ProcessInfo::instance().resetOnFork();

  JTRACE("fork()ed [CHILD]") (child) (parent);
  dmtcp::CoordinatorAPI::resetOnFork(coordinatorAPI);
  dmtcp::DmtcpWorker::resetOnFork();
}

extern "C" pid_t fork()
{
  if (isPerformingCkptRestart()) {
    return _real_syscall(SYS_fork);
  }

  /* Acquire the wrapperExeution lock to prevent checkpoint to happen while
   * processing this system call.
   */
  WRAPPER_EXECUTION_GET_EXCL_LOCK();
  dmtcp::DmtcpWorker::eventHook(DMTCP_EVENT_ATFORK_PREPARE, NULL);

  /* Little bit cheating here: child_time should be same for both parent and
   * child, thus we compute it before forking the child. */
  child_time = time(NULL);
  uint64_t host = dmtcp::UniquePid::ThisProcess().hostid();
  dmtcp::UniquePid parent = dmtcp::UniquePid::ThisProcess();
  dmtcp::string child_name = jalib::Filesystem::GetProgramName() + "_(forked)";

  coordinatorAPI.createNewConnectionBeforeFork(child_name);

  //Enable the pthread_atfork child call
  pthread_atfork_enabled = true;
  pid_t childPid = _real_fork();

  if (childPid == -1) {
  } else if (childPid == 0) { /* child process */
    /* NOTE: Any work that needs to be done for the newly created child
     * should be put into pthread_atfork_child() function. That function is
     * hooked to the libc:fork() and will be called right after the new
     * process is created and before the fork() returns.
     *
     * pthread_atfork_child is registered by calling pthread_atfork() from
     * within the DmtcpWorker constructor to make sure that this is the first
     * registered handle.
     */
    dmtcp::UniquePid child = dmtcp::UniquePid(host, getpid(), child_time);
    JTRACE("fork() done [CHILD]") (child) (parent);

    dmtcp::initializeMtcpEngine();
  } else if (childPid > 0) { /* Parent Process */
    dmtcp::UniquePid child = dmtcp::UniquePid(host, childPid, child_time);
    dmtcp::ProcessInfo::instance().insertChild(childPid, child);
    JTRACE("fork()ed [PARENT] done") (child);;
  }

  pthread_atfork_enabled = false;

  if (childPid != 0) {
    coordinatorAPI.closeConnection();
    dmtcp::DmtcpWorker::eventHook(DMTCP_EVENT_ATFORK_PARENT, NULL);
    WRAPPER_EXECUTION_RELEASE_EXCL_LOCK();
  }
  return childPid;
}

extern "C" pid_t vfork()
{
  JTRACE("vfork wrapper calling fork");
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
    output = _real_popen(path, "r");
  } else {
    dmtcp::string command = path;
    for (int i = 1; argv[i] != NULL; i++)
      command = command + " " + argv[i];
    output = _real_popen(command.c_str(), "r");
  }
  int numRead = fread(buf, 1, bufSize - 1, output);
  numRead++, numRead--; // suppress unused-var warning

  pclose(output); // /lib/libXXX process is now done; can checkpoint now
  // FIXME:  code currently allows wrapper to proceed without lock if
  //   it was busy because of a writer.  The unlock will then fail below.
  bool __wrapperExecutionLockAcquired = true; // needed for LOCK_UNLOCK macro
  WRAPPER_EXECUTION_RELEASE_EXCL_LOCK();
  // We  are now the new /lib/libXXX process, and it's safe for DMTCP to ckpt us.
  printf("%s", buf); // print buf, which is what /lib/libXXX would print
  JALLOC_HELPER_FREE(buf);
  // Avoid running exit handlers of the parent process by calling _exit.
  _exit(0);
}

// FIXME:  Unify this code with code prior to execvp in dmtcp_launch.cpp
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
  if (path != NULL && dmtcp::Util::strEndsWith(path, "/utempter")) {
    JTRACE("Trying to exec: utempter")(path)(argv[0])(argv[1]);
    int oldIdx = -1;
    char *oldStr = NULL;
    dmtcp::string realPtsNameStr;
    // utempter takes a pts slave name as an argument. Since we virtualize
    // ptys, the slave name points to a virtual slave name, thus we need to
    // replace it with the real one.
    for (size_t i = 0; argv[i] != NULL; i++) {
      if (dmtcp::Util::strStartsWith(argv[i], VIRT_PTS_PREFIX_STR)) {
        // FIXME: Potential memory leak if exec() fails.
        char *realPtsNameStr = (char*)JALLOC_HELPER_MALLOC(PTS_PATH_MAX);
        oldStr = argv[i];
        oldIdx = i;
        dmtcp::SharedData::getRealPtyName(argv[i], realPtsNameStr,
                                          PTS_PATH_MAX);
        // Override const restriction
        *(const char**)&argv[i] = realPtsNameStr;
      }
    }
    execShortLivedProcessAndExit(path, argv);
    if (oldIdx != -1) {
      // Restore original argv[] if exec failed.
      *(const char**)&argv[oldIdx] = oldStr;
    }
  }

  // FIXME:  SEE COMMENTS IN dmtcp_launch.cpp, rev. 1087; AND CHANGE THIS.
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

  ostringstream os;
  os << dmtcp_get_tmpdir() << "/dmtcpLifeBoat." << UniquePid::ThisProcess()
     << "-XXXXXX";
  char *buf = (char*) JALLOC_HELPER_MALLOC(os.str().length()+1);
  strcpy(buf, os.str().c_str());
  int fd = _real_mkstemp(buf);
  JASSERT(fd != -1) (JASSERT_ERRNO);
  JASSERT(unlink(buf) == 0) (JASSERT_ERRNO);
  Util::changeFd(fd, PROTECTED_LIFEBOAT_FD);
  jalib::JBinarySerializeWriterRaw wr ("", PROTECTED_LIFEBOAT_FD);
  dmtcp::UniquePid::serialize (wr);
  DmtcpEventData_t edata;
  edata.serializerInfo.fd = PROTECTED_LIFEBOAT_FD;
  dmtcp::DmtcpWorker::eventHook(DMTCP_EVENT_PRE_EXEC, &edata);

  JTRACE("Will exec filename instead of path") (path) (*filename);

  dmtcp::Util::adjustRlimitStack();

  char str[21] = {0};
  sprintf(str, "%d", SharedData::getDlsymOffset());
  setenv(ENV_VAR_DLSYM_OFFSET, str, 1);
  sprintf(str, "%d", SharedData::getDlsymOffset_m32());
  setenv(ENV_VAR_DLSYM_OFFSET_M32, str, 1);

  // Remove FD_CLOEXEC flag from protected file descriptors.
  for (size_t i  = PROTECTED_FD_START; i < PROTECTED_FD_END; i++) {
    int flags = fcntl(i, F_GETFD, NULL);
    if (flags != -1) {
      fcntl(i, F_SETFD, flags & ~FD_CLOEXEC);
    }
  }
  JTRACE("Prepared for Exec") (getenv("LD_PRELOAD"));
}

static void dmtcpProcessFailedExec(const char *path, char *newArgv[])
{
  int saved_errno = errno;

  if (dmtcp::Util::isSetuid(path)) {
    dmtcp::Util::freePatchedArgv(newArgv);
  }

  restoreUserLDPRELOAD();

  unsetenv(ENV_VAR_DLSYM_OFFSET);
  unsetenv(ENV_VAR_DLSYM_OFFSET_M32);

  JTRACE("Processed failed Exec Attempt") (path) (getenv("LD_PRELOAD"));
  errno = saved_errno;
  JASSERT(_real_close(PROTECTED_LIFEBOAT_FD) == 0) (JASSERT_ERRNO);
}

static dmtcp::string getUpdatedLdPreload(const char* filename,
                                         const char* currLdPreload = NULL)
{
  dmtcp::string preload = getenv(ENV_VAR_HIJACK_LIBS);
  bool isElf, is32bitElf;
  if  (dmtcp::Util::elfType(filename, &isElf, &is32bitElf) != -1) {
    if (isElf && is32bitElf && getenv(ENV_VAR_HIJACK_LIBS_M32) != NULL) {
      preload = getenv(ENV_VAR_HIJACK_LIBS_M32);
    }
  }

  const char *preloadEnv = getenv("LD_PRELOAD");
  if (currLdPreload != NULL && strlen(currLdPreload) > 0) {
    setenv(ENV_VAR_ORIG_LD_PRELOAD, currLdPreload, 1);
    preload = preload + ":" + currLdPreload;
  } else if (preloadEnv != NULL && strlen(preloadEnv) > 0) {
    setenv(ENV_VAR_ORIG_LD_PRELOAD, getenv("LD_PRELOAD"), 1);
    preload = preload + ":" + getenv("LD_PRELOAD");
  }
  return preload;
}

static const char* ourImportantEnvs[] =
{
  ENV_VARS_ALL //expands to a long list
};
#define ourImportantEnvsCnt ((sizeof(ourImportantEnvs))/(sizeof(const char*)))

static bool isImportantEnv (dmtcp::string str)
{
  str = str.substr(0, str.find("="));

  for (size_t i=0; i<ourImportantEnvsCnt; ++i) {
    if (str == ourImportantEnvs[i])
      return true;
  }
  return false;
}

static dmtcp::vector<dmtcp::string> copyUserEnv (char *const envp[])
{
  dmtcp::vector<dmtcp::string> strStorage;

  dmtcp::ostringstream out;
  out << "non-DMTCP env vars:\n";
  for (; *envp != NULL; ++envp) {
    if (isImportantEnv (*envp)) {
      if (dbg) {
        out << "     skipping: " << *envp << '\n';
      }
      continue;
    }
    dmtcp::string e(*envp);
    strStorage.push_back (e);
    if (dbg) {
      out << "     addenv[user]:" << strStorage.back() << '\n';
    }
  }
  JTRACE("Creating a copy of (non-DMTCP) user env vars...") (out.str());

  return strStorage;
}

static
dmtcp::vector<const char*> patchUserEnv (dmtcp::vector<dmtcp::string> &envp,
                                         const char* filename)
{
  dmtcp::vector<const char*> envVect;
  const char *userPreloadStr = NULL;
  envVect.clear();
  JASSERT(envVect.size() == 0);

  dmtcp::ostringstream out;
  out << "non-DMTCP env vars:\n";

  for (size_t i = 0; i < envp.size(); i++) {
    if (isImportantEnv (envp[i].c_str())) {
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
    if (dbg) {
      out << "     addenv[user]:" << envVect.back() << '\n';
    }
  }
  JTRACE("Creating a copy of (non-DMTCP) user env vars...") (out.str());

  //pack up our ENV into the new ENV
  out.str("DMTCP env vars:\n");
  for (size_t i=0; i<ourImportantEnvsCnt; ++i) {
    const char* v = getenv (ourImportantEnvs[i]);
    dmtcp::string e = ourImportantEnvs[i];
    if (strcmp(ourImportantEnvs[i], ENV_VAR_ORIG_LD_PRELOAD) == 0 &&
        userPreloadStr != NULL && strlen(userPreloadStr) > 0) {
      envp.push_back(e + "=" + userPreloadStr);
    } else if (v != NULL) {
      envp.push_back (e + '=' + v);
      const char *ptr = envp.back().c_str();
      JASSERT(ptr != NULL);
      envVect.push_back(ptr);
      if (dbg) {
        out << "     addenv[dmtcp]:" << envVect.back() << '\n';
      }
    }
  }

  dmtcp::string ldPreloadStr = "LD_PRELOAD=";
  ldPreloadStr += getUpdatedLdPreload(filename, userPreloadStr);

  envp.push_back(ldPreloadStr);
  envVect.push_back(envp.back().c_str());
  if (dbg) {
    out << "     addenv[dmtcp]:" << envVect.back() << '\n';
  }

  JTRACE("patching user envp...")  (out.str());

  envVect.push_back (NULL);

  JTRACE("Done patching environ");
  return envVect;
}

extern "C" int execve (const char *filename, char *const argv[],
                        char *const envp[])
{

  if( strstr(filename,"srun") != NULL ){
    JTRACE("execvp() wrapper (srun) - will call ecexvp directly") (filename);
  }

  if (isPerformingCkptRestart() || isBlacklistedProgram(filename) || (strstr(filename,"srun") != NULL) ) {
    return _real_execve(filename, argv, envp);
  }
  JTRACE("execve() wrapper") (filename);

  /* Acquire the wrapperExeution lock to prevent checkpoint to happen while
   * processing this system call.
   */
  WRAPPER_EXECUTION_GET_EXCL_LOCK();

  dmtcp::vector<dmtcp::string> origUserEnv = copyUserEnv(envp);

  char *newFilename;
  char **newArgv;
  dmtcpPrepareForExec(filename, argv, &newFilename, &newArgv);

  dmtcp::vector<const char*> envVect = patchUserEnv(origUserEnv, filename);

  int retVal = _real_execve (newFilename, newArgv, (char* const*)&envVect[0]);

  dmtcpProcessFailedExec(filename, newArgv);

  WRAPPER_EXECUTION_RELEASE_EXCL_LOCK();

  return retVal;
}

extern "C" int execv (const char *path, char *const argv[])
{
  JTRACE("execv() wrapper, calling execve with environ") (path);
  return execve(path, argv, environ);
}

extern "C" int execvp (const char *filename, char *const argv[])
{
  if( strstr(filename,"srun") != NULL ){
    JTRACE("execvp() wrapper (srun) - will call ecexvp directly") (filename);
  }

  if (isPerformingCkptRestart() || isBlacklistedProgram(filename) || (strstr(filename,"srun") != NULL) ) {
    return _real_execvp(filename, argv);
  }
  JTRACE("execvp() wrapper") (filename);
  /* Acquire the wrapperExeution lock to prevent checkpoint to happen while
   * processing this system call.
   */
  WRAPPER_EXECUTION_GET_EXCL_LOCK();

  char *newFilename;
  char **newArgv;
  dmtcpPrepareForExec(filename, argv, &newFilename, &newArgv);
  setenv("LD_PRELOAD", getUpdatedLdPreload(filename).c_str(), 1);

  int retVal = _real_execvp (newFilename, newArgv);

  dmtcpProcessFailedExec(filename, newArgv);

  WRAPPER_EXECUTION_RELEASE_EXCL_LOCK();

  return retVal;
}

// This function first appeared in glibc 2.11
extern "C" int execvpe (const char *filename, char *const argv[],
                         char *const envp[])
{
  if (isPerformingCkptRestart() || isBlacklistedProgram(filename)) {
    return _real_execvpe(filename, argv, envp);
  }
  JTRACE("execvpe() wrapper") (filename);
  /* Acquire the wrapperExeution lock to prevent checkpoint to happen while
   * processing this system call.
   */
  WRAPPER_EXECUTION_GET_EXCL_LOCK();

  dmtcp::vector<dmtcp::string> origUserEnv = copyUserEnv(envp);

  char *newFilename;
  char **newArgv;
  dmtcpPrepareForExec(filename, argv, &newFilename, &newArgv);

  dmtcp::vector<const char*> envVect = patchUserEnv(origUserEnv, filename);

  int retVal = _real_execvpe(newFilename, newArgv, (char* const*)&envVect[0]);

  dmtcpProcessFailedExec(filename, newArgv);

  WRAPPER_EXECUTION_RELEASE_EXCL_LOCK();

  return retVal;
}

extern "C" int fexecve (int fd, char *const argv[], char *const envp[])
{
  char buf[sizeof "/proc/self/fd/" + sizeof (int) * 3];
  snprintf (buf, sizeof (buf), "/proc/self/fd/%d", fd);

  JTRACE("fexecve() wrapper calling execve()") (fd) (buf);
  return execve(buf, argv, envp);
}


extern "C" int execl (const char *path, const char *arg, ...)
{
  JTRACE("execl() wrapper") (path);

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


extern "C" int execlp (const char *file, const char *arg, ...)
{
  JTRACE("execlp() wrapper") (file);

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
  JTRACE("execle() wrapper") (path);

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
  JTRACE("before system(), checkpointing may not work")
    (line) (getenv (ENV_VAR_HIJACK_LIBS)) (getenv ("LD_PRELOAD"));

  if (line == NULL)
    /* Check that we have a command processor available.  It might
       not be available after a chroot(), for example.  */
    return do_system ("exit 0") == 0;

  int result = do_system (line);

  JTRACE("after system()");

  return result;
}
