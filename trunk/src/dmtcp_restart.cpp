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

#include <stdio.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <limits.h>
#include <elf.h>

#include "constants.h"
#include "coordinatorapi.h"
#include "util.h"
#include "uniquepid.h"
#include "processinfo.h"
#include "shareddata.h"
#include "ckptserializer.h"
#include  "../jalib/jassert.h"
#include  "../jalib/jfilesystem.h"

#define BINARY_NAME "dmtcp_restart"
#define MTCP_RESTART_BINARY "mtcp_restart"

using namespace dmtcp;

static void setEnvironFd();

dmtcp::string dmtcpTmpDir = "/DMTCP/Uninitialized/Tmp/Dir";

// gcc-4.3.4 -Wformat=2 issues false positives for warnings unless the format
// string has at least one format specifier with corresponding format argument.
// Ubuntu 9.01 uses -Wformat=2 by default.
static const char* theUsage =
  "Usage: dmtcp_restart [OPTIONS] <ckpt1.dmtcp> [ckpt2.dmtcp...]\n\n"
  "Restart processes from a checkpoint image.\n\n"
  "Connecting to the DMTCP Coordinator:\n"
  "  -h, --host HOSTNAME (environment variable DMTCP_HOST)\n"
  "              Hostname where dmtcp_coordinator is run (default: localhost)\n"
  "  -p, --port PORT_NUM (environment variable DMTCP_PORT)\n"
  "              Port where dmtcp_coordinator is run (default: 7779)\n"
  "  --port-file FILENAME\n"
  "              File to write listener port number.\n"
  "              (Useful with '--port 0', which is used to assign a random port)\n"
  "  -j, --join\n"
  "              Join an existing coordinator, raise error if one doesn't\n"
  "              already exist\n"
  "  --new-coordinator\n"
  "              Create a new coordinator at the given port. Fail if one \n"
  "              already exists on the given port. The port can be specified\n"
  "              with --port, or with environment variable DMTCP_PORT.  If no\n"
  "              port is specified, start coordinator at a random port (same\n"
  "              as specifying port '0').\n"
  "  -i, -interval SECONDS (environment variable DMTCP_CHECKPOINT_INTERVAL)\n"
  "              Time in seconds between automatic checkpoints.\n"
  "              0 implies never (manual ckpt only); if not set and no env var,\n"
  "              use default value set in dmtcp_coordinator or dmtcp_command.\n"
  "              Not allowed if --join is specified\n"
  "\n"
  "Other options:\n"
  "  --run-as-root\n"
  "              Allow root to run dmtcp_restart and disable uid checking.\n"
  "              (default: disabled)\n"
  "  --no-strict-uid-checking\n"
  "              Disable uid checking for the checkpoint image. This allows\n"
  "              the checkpoint image to be restarted by a different user\n"
  "              than the one that created it.\n"
  "              (environment variable DMTCP_DISABLE_UID_CHECKING)\n"
  "  --ckptdir (environment variable DMTCP_CHECKPOINT_DIR):\n"
  "              Directory to store checkpoint images\n"
  "              (default: use the same directory used in previous checkpoint)\n"
  "  --tmpdir PATH (environment variable DMTCP_TMPDIR)\n"
  "              Directory to store temporary files \n"
  "              (default: $TMDPIR/dmtcp-$USER@$HOST or /tmp/dmtcp-$USER@$HOST)\n"
  "  -q, --quiet (or set environment variable DMTCP_QUIET = 0, 1, or 2)\n"
  "              Skip NOTE messages; if given twice, also skip WARNINGs\n"
  "  --help\n"
  "              Print this message and exit.\n"
  "  --version\n"
  "              Print version information and exit.\n"
  "\n"
  HELP_AND_CONTACT_INFO
  "\n"
;

class RestoreTarget;

typedef dmtcp::map<dmtcp::UniquePid, RestoreTarget*> RestoreTargetMap;
RestoreTargetMap targets;
RestoreTargetMap independentProcessTreeRoots;
bool noStrictUIDChecking = false;
bool runAsRoot = false;
static dmtcp::string thePortFile;
CoordinatorAPI::CoordinatorMode allowedModes = CoordinatorAPI::COORD_ANY;

static void setEnvironFd();
static void runMtcpRestart(int is32bitElf, int fd, dmtcp::ProcessInfo *pInfo);

class RestoreTarget
{
  public:
    RestoreTarget(const dmtcp::string& path)
      : _path(path)
    {
      JASSERT(jalib::Filesystem::FileExists(_path)) (_path)
        .Text ( "checkpoint file missing" );

      _fd = dmtcp::CkptSerializer::readCkptHeader(_path, &_pInfo);
      JTRACE("restore target") (_path) (_pInfo.numPeers()) (_pInfo.compGroup());
    }

    const int fd() const { return _fd; }
    const UniquePid& upid() const { return _pInfo.upid(); }
    const pid_t pid() const { return _pInfo.pid(); }
    const pid_t sid() const { return _pInfo.sid(); }
    const bool isRootOfProcessTree() const {
      return _pInfo.isRootOfProcessTree();
    }
    string procname() { return _pInfo.procname(); }
    UniquePid compGroup() { return _pInfo.compGroup(); }
    int numPeers() { return _pInfo.numPeers(); }

    void restoreGroup()
    {
      if (_pInfo.isGroupLeader()) {
        // create new Group where this process becomes a leader
        JTRACE("Create new Group.");
        setpgid(0, 0);
      }
    }

    void createDependentChildProcess()
    {
      pid_t pid = fork();
      JASSERT(pid != -1);
      if (pid != 0) {
        return;
      }
      createProcess();
    }

    void createDependentNonChildProcess()
    {
      pid_t pid = fork();
      JASSERT(pid != -1);
      if (pid == 0) {
        pid_t gchild = fork();
        JASSERT(gchild != -1);
        if (gchild != 0) {
          exit(0);
        }
        createProcess();
      } else {
        JASSERT(waitpid(pid, NULL, 0) == pid);
      }
    }

    void createProcess(bool createIndependentRootProcesses = false)
    {
      UniquePid::ThisProcess() = _pInfo.upid();
      UniquePid::ParentProcess() = _pInfo.uppid();
      dmtcp::Util::initializeLogFile(_pInfo.procname());

      if (createIndependentRootProcesses) {
        DmtcpUniqueProcessId compId = _pInfo.compGroup().upid();
        CoordinatorInfo coordInfo;
        struct in_addr localIPAddr;
        CoordinatorAPI::CoordinatorMode mode = CoordinatorAPI::COORD_ANY;
        if (_pInfo.noCoordinator()) {
          mode = CoordinatorAPI::COORD_NONE;
        }

        CoordinatorAPI::instance().connectToCoordOnRestart(mode,
                                                           _pInfo.procname(),
                                                           _pInfo.compGroup(),
                                                           _pInfo.numPeers(),
                                                           &coordInfo,
                                                           &localIPAddr);
        Util::writeCoordPortToFile(getenv(ENV_VAR_NAME_PORT),
                                   thePortFile.c_str());

        /* We need to initialize SharedData here to make sure that it is
         * initialized with the correct coordinator timestamp.  The coordinator
         * timestamp is updated only during postCkpt callback. However, the
         * SharedData area may be initialized earlier (for example, while
         * recreating threads), causing it to use *older* timestamp.
         */
        SharedData::initialize(Util::getTmpDir().c_str(), &compId, &coordInfo,
                               &localIPAddr);
      }

      JTRACE("Creating process during restart") (upid()) (_pInfo.procname());

      RestoreTargetMap::iterator it;
      for (it = targets.begin(); it != targets.end(); it++) {
        RestoreTarget *t = it->second;
        if (_pInfo.upid() == t->_pInfo.upid()) {
          continue;
        } else if (_pInfo.isChild(t->upid()) &&
                   t->_pInfo.sid() != _pInfo.pid()) {
          t->createDependentChildProcess();
        }
      }

      if (createIndependentRootProcesses) {
        RestoreTargetMap::iterator it;
        for (it = independentProcessTreeRoots.begin();
             it != independentProcessTreeRoots.end();
             it++) {
          RestoreTarget *t = it->second;
          if (t != this) {
            t->createDependentNonChildProcess();
          }
        }
      }

      // If we were the session leader, become one now.
      if (_pInfo.sid() == _pInfo.pid()) {
        if (getsid(0) != _pInfo.pid()) {
          JWARNING(setsid() != -1) (getsid(0)) (JASSERT_ERRNO)
            .Text("Failed to restore this process as session leader.");
        }
      }

      // Now recreate processes with sid == _pid
      for (it = targets.begin(); it != targets.end(); it++) {
        RestoreTarget *t = it->second;
        if (_pInfo.upid() == t->_pInfo.upid()) {
          continue;
        } else if (t->_pInfo.sid() == _pInfo.pid()) {
          if (_pInfo.isChild(t->upid())) {
            t->createDependentChildProcess();
          } else if (t->isRootOfProcessTree()) {
            t->createDependentNonChildProcess();
          }
        }
      }

      // Now close all open fds except _fd;
      for (it = targets.begin(); it != targets.end(); it++) {
        RestoreTarget *t = it->second;
        if (t != this) {
          close(t->fd());
        }
      }

      string ckptDir = jalib::Filesystem::GetDeviceName(PROTECTED_CKPT_DIR_FD);
      if (ckptDir.length() == 0) {
        // Create the ckpt-dir fd so that the restarted process can know about
        // the abs-path of ckpt-image.
        dmtcp::string dirName = jalib::Filesystem::DirName(_path);
        int dirfd = open(dirName.c_str(), O_RDONLY);
        JASSERT(dirfd != -1) (JASSERT_ERRNO);
        if (dirfd != PROTECTED_CKPT_DIR_FD) {
          JASSERT(dup2(dirfd, PROTECTED_CKPT_DIR_FD) == PROTECTED_CKPT_DIR_FD);
          close(dirfd);
        }
      }

      if (!createIndependentRootProcesses) {
        CoordinatorAPI::instance().connectToCoordOnRestart(CoordinatorAPI::COORD_ANY,
                                                           _pInfo.procname(),
                                                           _pInfo.compGroup(),
                                                           _pInfo.numPeers(),
                                                           NULL, NULL);
      }

      setEnvironFd();
      int is32bitElf = 0;

#if defined(__x86_64__)
      is32bitElf = (_pInfo.elfType() == ProcessInfo::Elf_32);
#endif
      runMtcpRestart(is32bitElf, _fd, &_pInfo);

      JASSERT ( false ).Text ( "unreachable" );
    }

  private:
    dmtcp::string _path;
    dmtcp::ProcessInfo _pInfo;
    int _fd;
};

static void runMtcpRestart(int is32bitElf, int fd, dmtcp::ProcessInfo *pInfo)
{
  char fdBuf[8];
  char stderrFdBuf[8];
  sprintf(fdBuf, "%d", fd);
  sprintf(stderrFdBuf, "%d", PROTECTED_STDERR_FD);

  static dmtcp::string mtcprestart =
    jalib::Filesystem::FindHelperUtility ("mtcp_restart");

  if (is32bitElf) {
    mtcprestart = jalib::Filesystem::FindHelperUtility("mtcp_restart-32",
                                                       is32bitElf);
  }

  char* const newArgs[] = {
    (char*) mtcprestart.c_str(),
    const_cast<char*> ("--fd"), fdBuf,
    const_cast<char*> ("--stderr-fd"), stderrFdBuf,
    NULL
  };

  execve (newArgs[0], newArgs, environ);
  JASSERT (false) (newArgs[0]) (newArgs[1]) (JASSERT_ERRNO)
    .Text ("exec() failed");
}

static void setEnvironFd()
{
  char envFile[PATH_MAX];
  sprintf(envFile, "%s/envFile.XXXXXX", dmtcpTmpDir.c_str());
  int fd = mkstemp(envFile);
  JASSERT(fd != -1) (envFile) (JASSERT_ERRNO);
  JASSERT(unlink(envFile) == 0) (envFile) (JASSERT_ERRNO);
  JASSERT(dup2(fd, PROTECTED_ENVIRON_FD) == PROTECTED_ENVIRON_FD)
    (JASSERT_ERRNO);
  JASSERT(close(fd) == 0);
  fd = PROTECTED_ENVIRON_FD;

  char **env = environ;
  while (*env != NULL) {
    Util::writeAll(fd, *env, strlen(*env) + 1); // Also write null character
    env++;
  }
  Util::writeAll(fd, *env, 1); // Write final null character
}

static void setNewCkptDir(char *path)
{
  struct stat st;
  if (stat(path, &st) == -1) {
    JASSERT(mkdir(path, S_IRWXU) == 0 || errno == EEXIST)
      (JASSERT_ERRNO) (path)
      .Text("Error creating checkpoint directory");
    JASSERT(0 == access(path, X_OK|W_OK)) (path)
      .Text("ERROR: Missing execute- or write-access to checkpoint dir");
  } else {
    JASSERT(S_ISDIR(st.st_mode)) (path) .Text("ckptdir not a directory");
  }

  int fd = open(path, O_RDONLY);
  JASSERT(fd != -1) (path);
  JASSERT(dup2(fd, PROTECTED_CKPT_DIR_FD) == PROTECTED_CKPT_DIR_FD)
    (fd) (path);
  if (fd != PROTECTED_CKPT_DIR_FD) {
    close(fd);
  }
}

//shift args
#define shift argc--,argv++

int main(int argc, char** argv)
{
  initializeJalib();

  if (!getenv(ENV_VAR_QUIET)) {
    setenv(ENV_VAR_QUIET, "0", 0);
  }

  if (getenv(ENV_VAR_DISABLE_UID_CHECKING)) {
    noStrictUIDChecking = true;
  }

  if (argc == 1) {
    printf("%s", DMTCP_VERSION_AND_COPYRIGHT_INFO);
    printf("(For help: %s --help)\n\n", argv[0]);
    return DMTCP_FAIL_RC;
  }

  //process args
  shift;
  while (true) {
    dmtcp::string s = argc>0 ? argv[0] : "--help";
    if (s == "--help" && argc == 1) {
      printf("%s", theUsage);
      return DMTCP_FAIL_RC;
    } else if ((s == "--version") && argc == 1) {
      printf("%s", DMTCP_VERSION_AND_COPYRIGHT_INFO);
      return DMTCP_FAIL_RC;
    } else if (s == "-j" || s == "--join") {
      allowedModes = dmtcp::CoordinatorAPI::COORD_JOIN;
      shift;
    } else if (s == "--new-coordinator") {
      allowedModes = dmtcp::CoordinatorAPI::COORD_NEW;
      shift;
    } else if (s == "--run-as-root") {
      runAsRoot = true;
      shift;
    } else if (s == "--no-strict-uid-checking") {
      noStrictUIDChecking = true;
      shift;
    } else if (s == "-i" || s == "--interval" ||
               (s.c_str()[0] == '-' && s.c_str()[1] == 'i' &&
                isdigit(s.c_str()[2]))) {
      if (isdigit(s.c_str()[2])) { // if -i5, for example
        setenv(ENV_VAR_CKPT_INTR, s.c_str()+2, 1);
        shift;
      } else { // else -i 5
        setenv(ENV_VAR_CKPT_INTR, argv[1], 1);
        shift; shift;
      }
    } else if (argc > 1 && (s == "-h" || s == "--host")) {
      setenv(ENV_VAR_NAME_HOST, argv[1], 1);
      shift; shift;
    } else if (argc > 1 && (s == "-p" || s == "--port")) {
      setenv(ENV_VAR_NAME_PORT, argv[1], 1);
      shift; shift;
    } else if (argc>1 && s == "--port-file"){
      thePortFile = argv[1];
      shift; shift;
    } else if (argc > 1 && (s == "-c" || s == "--ckptdir")) {
      setNewCkptDir(argv[1]);
      shift; shift;
    } else if (argc > 1 && (s == "-t" || s == "--tmpdir")) {
      setenv(ENV_VAR_TMPDIR, argv[1], 1);
      shift; shift;
    } else if (s == "-q" || s == "--quiet") {
      *getenv(ENV_VAR_QUIET) = *getenv(ENV_VAR_QUIET) + 1;
      // Just in case a non-standard version of setenv is being used:
      setenv(ENV_VAR_QUIET, getenv(ENV_VAR_QUIET), 1);
      shift;
    } else if ((s.length() > 2 && s.substr(0, 2) == "--") ||
               (s.length() > 1 && s.substr(0, 1) == "-")) {
      printf("Invalid Argument\n%s", theUsage);
      return DMTCP_FAIL_RC;
    } else if (argc > 1 && s == "--") {
      shift;
      break;
    } else {
      break;
    }
  }

  dmtcp::Util::setTmpDir(getenv(ENV_VAR_TMPDIR));
  dmtcpTmpDir = dmtcp::Util::getTmpDir();

  jassert_quiet = *getenv(ENV_VAR_QUIET) - '0';

  //make sure JASSERT initializes now, rather than during restart
  Util::initializeLogFile();

  if (!runAsRoot && (getuid() == 0 || geteuid() == 0)) {
    JASSERT_STDERR <<
      "Running dmtcp_restart as root is dangerous.  Aborting.\n"
      "If you still want to do this (at your own risk), then use\n" \
      "    dmtcp_restart --run-as-root\n";
    exit(0);
  }

  JTRACE("New dmtcp_restart process; _argc_ ckpt images") (argc);

  bool doAbort = false;
  for (; argc > 0; shift) {
    dmtcp::string restorename(argv[0]);
    struct stat buf;
    int rc = stat(restorename.c_str(), &buf);
    if (Util::strEndsWith(restorename, "_files")) {
      continue;
    } else if (!Util::strEndsWith(restorename, ".dmtcp")) {
      JNOTE("File doesn't have .dmtcp extension. Check Usage.")
        (restorename);
      JASSERT_STDERR << theUsage;
      doAbort = true;
    } else if (rc == -1) {
      char error_msg[1024];
      sprintf(error_msg, "\ndmtcp_restart: ckpt image %s", restorename.c_str());
      perror(error_msg);
      doAbort = true;
    } else if (buf.st_uid != getuid() && !noStrictUIDChecking && !runAsRoot) {
      /*Could also run if geteuid() matches*/
      printf("\nProcess uid (%d) doesn't match uid (%d) of\n" \
             "checkpoint image (%s).\n" \
	     "This is dangerous.  Aborting for security reasons.\n" \
             "If you still want to do this (at your own risk),\n" \
             "  then modify dmtcp/src/%s:%d and re-compile.\n",
             getuid(), buf.st_uid, restorename.c_str(), __FILE__, __LINE__ - 6);
      doAbort = true;
    }
    if (doAbort) {
      exit(DMTCP_FAIL_RC);
    }

    JTRACE("Will restart ckpt image") (argv[0]);
    RestoreTarget *t = new RestoreTarget(argv[0]);
    targets[t->upid()] = t;
  }

  // Prepare list of independent process tree roots
  RestoreTargetMap::iterator i;
  for (i = targets.begin(); i != targets.end(); i++) {
    RestoreTarget *t1 = i->second;
    if (t1->isRootOfProcessTree()) {
      RestoreTargetMap::iterator j;
      for (j = targets.begin(); j != targets.end(); j++) {
        RestoreTarget *t2 = j->second;
        if (t1 == t2) continue;
        if (t1->sid() == t2->pid()) {
          break;
        }
      }
      if (j == targets.end()) {
        independentProcessTreeRoots[t1->upid()] = t1;
      }
    }
  }
  JASSERT(independentProcessTreeRoots.size() > 0)
    .Text("There must atleast one process tree which doesn't have a different "
          "process as session leader.");

  WorkerState::setCurrentState(WorkerState::RESTARTING);

  dmtcp::Util::prepareDlsymWrapper();

  RestoreTarget *t = independentProcessTreeRoots.begin()->second;
  JASSERT(t->pid() != 0);
  t->createProcess(true);
  JASSERT(false).Text("unreachable");
  return -1;
}
