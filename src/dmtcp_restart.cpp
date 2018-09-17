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

#include <elf.h>
#include <limits.h>
#include <stdio.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "config.h"
#ifdef HAS_PR_SET_PTRACER
#include <sys/prctl.h>
#endif  // ifdef HAS_PR_SET_PTRACER

#include "../jalib/jassert.h"
#include "../jalib/jfilesystem.h"
#include "constants.h"
#include "coordinatorapi.h"
#include "dmtcp_dlsym.h"
#include "processinfo.h"
#include "shareddata.h"
#include "uniquepid.h"
#include "util.h"

#define BINARY_NAME         "dmtcp_restart"
#define MTCP_RESTART_BINARY "mtcp_restart"

using namespace dmtcp;

// Copied from mtcp/mtcp_restart.c.
#define DMTCP_MAGIC_FIRST 'D'
#define GZIP_FIRST        037
#ifdef HBICT_DELTACOMP
# define HBICT_FIRST      'H'
#endif // ifdef HBICT_DELTACOMP

static void setEnvironFd();

string tmpDir = "/DMTCP/Uninitialized/Tmp/Dir";

// gcc-4.3.4 -Wformat=2 issues false positives for warnings unless the format
// string has at least one format specifier with corresponding format argument.
// Ubuntu 9.01 uses -Wformat=2 by default.
static const char *theUsage =
  "Usage: dmtcp_restart [OPTIONS] <ckpt1.dmtcp> [ckpt2.dmtcp...]\n\n"
  "Restart processes from a checkpoint image.\n\n"
  "Connecting to the DMTCP Coordinator:\n"
  "  -h, --coord-host HOSTNAME (environment variable DMTCP_COORD_HOST)\n"
  "              Hostname where dmtcp_coordinator is run (default: localhost)\n"
  "  -p, --coord-port PORT_NUM (environment variable DMTCP_COORD_PORT)\n"
  "              Port where dmtcp_coordinator is run (default: "
                                                  STRINGIFY(DEFAULT_PORT) ")\n"
  "  --port-file FILENAME\n"
  "              File to write listener port number.\n"
  "              (Useful with '--port 0', in order to assign a random port)\n"
  "  -j, --join-coordinator\n"
  "              Join an existing coordinator, raise error if one doesn't\n"
  "              already exist\n"
  "  --new-coordinator\n"
  "              Create a new coordinator at the given port. Fail if one\n"
  "              already exists on the given port. The port can be specified\n"
  "              with --coord-port, or with environment variable\n"
  "              DMTCP_COORD_PORT.\n"
  "              If no port is specified, start coordinator at a random port\n"
  "              (same as specifying port '0').\n"
  "  --any-coordinator\n"
  "              Use --join-coordinator if possible, but only if port"
                                                            " was specified.\n"
  "              Else use --new-coordinator with specified port (if avail.),\n"
  "                and otherwise with the default port: --port "
                                                  STRINGIFY(DEFAULT_PORT) ")\n"
  "              (This is the default.)\n"
  "  -i, --interval SECONDS (environment variable DMTCP_CHECKPOINT_INTERVAL)\n"
  "              Time in seconds between automatic checkpoints.\n"
  "              0 implies never (manual ckpt only); if not set and no env\n"
  "              var, use default value set in dmtcp_coordinator or \n"
  "              dmtcp_command.\n"
  "              Not allowed if --join-coordinator is specified\n"
  "\n"
  "Other options:\n"
  "  --no-strict-checking\n"
  "              Disable uid checking for checkpoint image. Allow checkpoint\n"
  "              image to be restarted by a different user than the one\n"
  "              that created it. And suppress warning about running as root.\n"
  "              (environment variable DMTCP_DISABLE_STRICT_CHECKING)\n"
  "  --ckptdir (environment variable DMTCP_CHECKPOINT_DIR):\n"
  "              Directory to store checkpoint images\n"
  "              (default: use the same dir used in previous checkpoint)\n"
  "  --tmpdir PATH (environment variable DMTCP_TMPDIR)\n"
  "              Directory to store temp files (default: $TMDPIR or /tmp)\n"
  "  -q, --quiet (or set environment variable DMTCP_QUIET = 0, 1, or 2)\n"
  "              Skip NOTE messages; if given twice, also skip WARNINGs\n"
  "  --coord-logfile PATH (environment variable DMTCP_COORD_LOG_FILENAME\n"
  "              Coordinator will dump its logs to the given file\n"
  "  --debug-restart-pause (or set env. var. DMTCP_RESTART_PAUSE =1,2,3 or 4)\n"
  "              dmtcp_restart will pause early to debug with:  GDB attach\n"
  "  --help\n"
  "              Print this message and exit.\n"
  "  --version\n"
  "              Print version information and exit.\n"
  "\n"
  HELP_AND_CONTACT_INFO
  "\n";

static int requestedDebugLevel = 0;

class RestoreTarget;

typedef map<UniquePid, RestoreTarget *>RestoreTargetMap;
RestoreTargetMap targets;
RestoreTargetMap independentProcessTreeRoots;
bool noStrictChecking = false;
static string thePortFile;
CoordinatorMode allowedModes = COORD_ANY;

static void setEnvironFd();
static void runMtcpRestart(int is32bitElf, int fd, ProcessInfo *pInfo);
static int readCkptHeader(const string &path, ProcessInfo *pInfo);
static int openCkptFileToRead(const string &path);

class RestoreTarget
{
  public:
    RestoreTarget(const string &path)
      : _path(path)
    {
      JASSERT(jalib::Filesystem::FileExists(_path)) (_path)
      .Text("checkpoint file missing");

      _fd = readCkptHeader(_path, &_pInfo);
      ptrdiff_t clock_gettime_offset =
                            dmtcp_dlsym_lib_fnc_offset("linux-vdso",
                                                       "__vdso_clock_gettime");
      ptrdiff_t getcpu_offset = dmtcp_dlsym_lib_fnc_offset("linux-vdso",
                                                           "__vdso_getcpu");
      ptrdiff_t gettimeofday_offset =
                              dmtcp_dlsym_lib_fnc_offset("linux-vdso",
                                                         "__vdso_gettimeofday");
      ptrdiff_t time_offset = dmtcp_dlsym_lib_fnc_offset("linux-vdso",
                                                         "__vdso_time");
      JWARNING(!_pInfo.vdsoOffsetMismatch(clock_gettime_offset, getcpu_offset,
                                          gettimeofday_offset, time_offset))
              .Text("The vDSO section on the current system is different than"
                    " the host where the checkpoint image was generated. "
                    "Restart may fail if the program calls a function in to"
                    " vDSO, like, gettimeofday(), clock_gettime(), etc.");
      JTRACE("restore target") (_path) (_pInfo.numPeers()) (_pInfo.compGroup());
    }

    int fd() const { return _fd; }

    const UniquePid &upid() const { return _pInfo.upid(); }

    pid_t pid() const { return _pInfo.pid(); }

    pid_t sid() const { return _pInfo.sid(); }

    bool isRootOfProcessTree() const
    {
      return _pInfo.isRootOfProcessTree();
    }

    const string& procSelfExe() const { return _pInfo.procSelfExe(); }

    bool isOrphan()
    {
      return _pInfo.isOrphan();
    }

    string procname() { return _pInfo.procname(); }

    UniquePid compGroup() { return _pInfo.compGroup(); }

    int numPeers() { return _pInfo.numPeers(); }

    bool noCoordinator() { return _pInfo.noCoordinator(); }

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

    void createOrphanedProcess(bool createIndependentRootProcesses = false)
    {
      pid_t pid = fork();

      JASSERT(pid != -1);
      if (pid == 0) {
        pid_t gchild = fork();
        JASSERT(gchild != -1);
        if (gchild != 0) {
          exit(0);
        }
        createProcess(createIndependentRootProcesses);
      } else {
        JASSERT(waitpid(pid, NULL, 0) == pid);
        exit(0);
      }
    }

    void createProcess(bool createIndependentRootProcesses = false)
    {
      UniquePid::ThisProcess() = _pInfo.upid();
      UniquePid::ParentProcess() = _pInfo.uppid();

      if (createIndependentRootProcesses) {
        DmtcpUniqueProcessId compId = _pInfo.compGroup().upid();
        CoordinatorInfo coordInfo;
        struct in_addr localIPAddr;
        if (_pInfo.noCoordinator()) {
          allowedModes = COORD_NONE;
        }

        // dmtcp_restart sets ENV_VAR_NAME_HOST/PORT, even if cmd line flag used
        string host = "";
        int port = UNINITIALIZED_PORT;
        CoordinatorAPI::getCoordHostAndPort(allowedModes, host, &port);

        // FIXME:  We will use the new HOST and PORT here, but after restart,,
        // we will use the old HOST and PORT from the ckpt image.
        CoordinatorAPI::connectToCoordOnRestart(allowedModes,
                                                _pInfo.procname(),
                                                _pInfo.compGroup(),
                                                _pInfo.numPeers(),
                                                &coordInfo,
                                                host.c_str(),
                                                port,
                                                &localIPAddr);

        // If port was 0, we'll get new random port when coordinator starts up.
        CoordinatorAPI::getCoordHostAndPort(allowedModes, host, &port);
        Util::writeCoordPortToFile(port, thePortFile.c_str());

        string installDir =
          jalib::Filesystem::DirName(jalib::Filesystem::GetProgramDir());

#if defined(__i386__) || defined(__arm__)
        if (Util::strEndsWith(installDir, "/lib/dmtcp/32")) {
          // If dmtcp_launch was compiled for 32 bits in 64-bit O/S, then note:
          // DMTCP_ROOT/bin/dmtcp_launch is a symbolic link to:
          // DMTCP_ROOT/bin/dmtcp_launch/lib/dmtcp/32/bin
          // GetProgramDir() followed the link.  So, need to remove the suffix.
          char *str = const_cast<char *>(installDir.c_str());
          str[strlen(str) - strlen("/lib/dmtcp/32")] = '\0';
          installDir = str;
        }
#endif // if defined(__i386__) || defined(__arm__)

        /* We need to initialize SharedData here to make sure that it is
         * initialized with the correct coordinator timestamp.  The coordinator
         * timestamp is updated only during postCkpt callback. However, the
         * SharedData area may be initialized earlier (for example, while
         * recreating threads), causing it to use *older* timestamp.
         */
        SharedData::initialize(tmpDir.c_str(),
                               installDir.c_str(),
                               &compId,
                               &coordInfo,
                               &localIPAddr);
        Util::initializeLogFile(SharedData::getTmpDir(), _pInfo.procname());

        Util::prepareDlsymWrapper();
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
        string dirName = jalib::Filesystem::DirName(_path);
        int dirfd = open(dirName.c_str(), O_RDONLY);
        JASSERT(dirfd != -1) (JASSERT_ERRNO);
        if (dirfd != PROTECTED_CKPT_DIR_FD) {
          JASSERT(dup2(dirfd, PROTECTED_CKPT_DIR_FD) == PROTECTED_CKPT_DIR_FD);
          close(dirfd);
        }
      }

      if (!createIndependentRootProcesses) {
        // dmtcp_restart sets ENV_VAR_NAME_HOST/PORT, even if cmd line flag used
        string host = "";
        int port = UNINITIALIZED_PORT;
        int *port_p = &port;
        CoordinatorAPI::getCoordHostAndPort(allowedModes, host, port_p);
        CoordinatorAPI::connectToCoordOnRestart(allowedModes,
                                                _pInfo.procname(),
                                                _pInfo.compGroup(),
                                                _pInfo.numPeers(),
                                                NULL,
                                                host.c_str(),
                                                port,
                                                NULL);
      }

      setEnvironFd();
      int is32bitElf = 0;

#if defined(__x86_64__) || defined(__aarch64__)
      is32bitElf = (_pInfo.elfType() == ProcessInfo::Elf_32);
#elif defined(__i386__) || defined(__arm__)
      is32bitElf = true;
#endif // if defined(__x86_64__) || defined(__aarch64__)


      runMtcpRestart(is32bitElf, _fd, &_pInfo);

      JASSERT(false).Text("unreachable");
    }

  private:
    string _path;
    ProcessInfo _pInfo;
    int _fd;
};

static void
runMtcpRestart(int is32bitElf, int fd, ProcessInfo *pInfo)
{
  char fdBuf[8];
  char stderrFdBuf[8];

  sprintf(fdBuf, "%d", fd);
  sprintf(stderrFdBuf, "%d", PROTECTED_STDERR_FD);

#ifdef HAS_PR_SET_PTRACER
  if (getenv("DMTCP_GDB_ATTACH_ON_RESTART")) {
    JNOTE("\n     *******************************************************\n"
          "     *** Environment variable, DMTCP_GDB_ATTACH_ON_RESTART is set\n"
          "     *** You can attach to the running process as follows:\n"
          "     ***     gdb _PROGRAM_NAME_ PID  [See below for PID.]\n"
          "     *** NOTE:  This mode can be a security risk.\n"
          "     ***        Do not set the env. variable normally.\n"
          "     *******************************************************")
      (getpid());
    prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0); // Allow 'gdb attach'
  }
#endif // ifdef HAS_PR_SET_PTRACER

  static string mtcprestart = Util::getPath("mtcp_restart");

#if defined(__x86_64__) || defined(__aarch64__) || defined(CONFIG_M32)

  // FIXME: This is needed for CONFIG_M32 only because getPath("mtcp_restart")
  // fails to return the absolute path for mtcprestart.  We should fix
  // the bug in Util::getPath() and remove CONFIG_M32 condition in #if.
  if (is32bitElf) {
    mtcprestart = Util::getPath("mtcp_restart-32", is32bitElf);
  }
#endif // if defined(__x86_64__) || defined(__aarch64__) || defined(CONFIG_M32)

  if (requestedDebugLevel > 0) {
    int debugPipe[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, debugPipe);
    pid_t pid = fork();
    if (pid > 0) {
      int currentDebugLevel = 0;
      int rc = -1;
      close(debugPipe[1]);
      do {
        rc = read(debugPipe[0], &currentDebugLevel, sizeof(currentDebugLevel));
        if (rc < 0) break;
        rc = write(debugPipe[0], &requestedDebugLevel,
                   sizeof(currentDebugLevel));
        if (rc < 0) break;
      } while (currentDebugLevel != requestedDebugLevel);
      if (rc < 0) {
        JASSERT(false)
               .Text("Unable to set up debug connection "
                     "with the restarted process");
      }
      char cpid[10]; // XXX: Is 10 digits enough for a PID?
      snprintf(cpid, 10, "%d", pid);
      char* const command[] = {const_cast<char*>("gdb"),
                               const_cast<char*>(pInfo->procSelfExe().c_str()),
                               cpid,
                               NULL};
      execvp(command[0], command);
    } else if (pid == 0) {
      close(debugPipe[0]); // child doesn't need the read end
      JASSERT(dup2(debugPipe[1], PROTECTED_DEBUG_SOCKET_FD)
              == PROTECTED_DEBUG_SOCKET_FD)(JASSERT_ERRNO);
      close(debugPipe[1]);
    } else {
     JASSERT(false)(JASSERT_ERRNO).Text("Fork failed");
    }
  }

  /* If DMTCP_RESTART_PAUSE>1, mtcp_restart will loop until gdb attach.*/
  int mtcp_restart_pause = 0;
  char * pause_param = getenv("DMTCP_RESTART_PAUSE");
  if (pause_param == NULL) {
    pause_param = getenv("MTCP_RESTART_PAUSE");
  }
  if (pause_param != NULL && pause_param[0] >= '1' && pause_param[0] <= '4'
                          && pause_param[1] == '\0') {
#ifdef HAS_PR_SET_PTRACER
    prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0); // For: gdb attach
#endif // ifdef HAS_PR_SET_PTRACER
    mtcp_restart_pause = pause_param[0] - '0';
    // If mtcp_restart_pause == true, mtcp_restart will invoke
    //     postRestartDebug() in the checkpoint image instead of postRestart().
  }

  char *const newArgs[] = {
    (char *)mtcprestart.c_str(),
    const_cast<char *>("--fd"), fdBuf,
    const_cast<char *>("--stderr-fd"), stderrFdBuf,
    // These two flag must be last, since they may become NULL
    ( mtcp_restart_pause ? const_cast<char *>("--mtcp-restart-pause") : NULL ),
    ( mtcp_restart_pause ? pause_param : NULL ),
    NULL
  };

  execve(newArgs[0], newArgs, environ);
  JASSERT(false) (newArgs[0]) (newArgs[1]) (JASSERT_ERRNO)
  .Text("exec() failed");
}

// ************************ For reading checkpoint files *****************

int
readCkptHeader(const string &path, ProcessInfo *pInfo)
{
  int fd = openCkptFileToRead(path);
  const size_t len = strlen(DMTCP_FILE_HEADER);

  jalib::JBinarySerializeReaderRaw rdr("", fd);

  pInfo->serialize(rdr);
  size_t numRead = len + rdr.bytes();

  // We must read in multiple of PAGE_SIZE
  const ssize_t pagesize = Util::pageSize();
  ssize_t remaining = pagesize - (numRead % pagesize);
  char buf[remaining];
  JASSERT(Util::readAll(fd, buf, remaining) == remaining);
  return fd;
}

static char
first_char(const char *filename)
{
  int fd, rc;
  char c;

  fd = open(filename, O_RDONLY);
  JASSERT(fd >= 0) (filename).Text("ERROR: Cannot open filename");

  rc = read(fd, &c, 1);
  JASSERT(rc == 1) (filename).Text("ERROR: Error reading from filename");

  close(fd);
  return c;
}

// Copied from mtcp/mtcp_restart.c.
// Let's keep this code close to MTCP code to avoid maintenance problems.
// MTCP code in:  mtcp/mtcp_restart.c:open_ckpt_to_read()
// A previous version tried to replace this with popen, causing a regression:
// (no call to pclose, and possibility of using a wrong fd).
// Returns fd;
static int
open_ckpt_to_read(const char *filename)
{
  int fd;
  int fds[2];
  char fc;
  const char *decomp_path;
  const char **decomp_args;
  const char *gzip_path = "gzip";
  static const char *gzip_args[] = {
    const_cast<char *>("gzip"),
    const_cast<char *>("-d"),
    const_cast<char *>("-"),
    NULL
  };

#ifdef HBICT_DELTACOMP
  const char *hbict_path = const_cast<char *>("hbict");
  static const char *hbict_args[] = {
    const_cast<char *>("hbict"),
    const_cast<char *>("-r"),
    NULL
  };
#endif // ifdef HBICT_DELTACOMP
  pid_t cpid;

  fc = first_char(filename);
  fd = open(filename, O_RDONLY);
  JASSERT(fd >= 0)(filename).Text("Failed to open file.");

  if (fc == DMTCP_MAGIC_FIRST) { /* no compression */
    return fd;
  } else if (fc == GZIP_FIRST
#ifdef HBICT_DELTACOMP
             || fc == HBICT_FIRST
#endif // ifdef HBICT_DELTACOMP
             ) {
    if (fc == GZIP_FIRST) {
      decomp_path = gzip_path;
      decomp_args = gzip_args;
    }
#ifdef HBICT_DELTACOMP
    else {
      decomp_path = hbict_path;
      decomp_args = hbict_args;
    }
#endif // ifdef HBICT_DELTACOMP

    JASSERT(pipe(fds) != -1) (filename)
    .Text("Cannot create pipe to execute gunzip to decompress ckpt file!");

    cpid = fork();

    JASSERT(cpid != -1)
    .Text("ERROR: Cannot fork to execute gunzip to decompress ckpt file!");
    if (cpid > 0) { /* parent process */
      JTRACE("created child process to uncompress checkpoint file") (cpid);
      close(fd);
      close(fds[1]);

      // Wait for child process
      JASSERT(waitpid(cpid, NULL, 0) == cpid);
      return fds[0];
    } else { /* child process */
      /* Fork a grandchild process and kill the parent. This way the grandchild
       * process never becomes a zombie.
       *
       * Sometimes dmtcp_restart is called with multiple ckpt images. In that
       * situation, the dmtcp_restart process creates gzip processes and only
       * later forks mtcp_restart processes. The gzip processes can not be
       * wait()'d upon by the corresponding mtcp_restart processes because
       * their parent is the original dmtcp_restart process and thus they
       * become zombie.
       */
      cpid = fork();
      JASSERT(cpid != -1);
      if (cpid > 0) {
        // Use _exit() instead of exit() to avoid popping atexit() handlers
        // registered by the parent process.
        _exit(0);
      }

      // Grandchild process
      JTRACE("child process, will exec into external de-compressor");
      fd = dup(dup(dup(fd)));
      fds[1] = dup(fds[1]);
      close(fds[0]);
      JASSERT(fd != -1);
      JASSERT(dup2(fd, STDIN_FILENO) == STDIN_FILENO);
      close(fd);
      JASSERT(dup2(fds[1], STDOUT_FILENO) == STDOUT_FILENO);
      close(fds[1]);
      execvp(decomp_path, (char **)decomp_args);
      JASSERT(decomp_path != NULL) (decomp_path)
      .Text("Failed to launch gzip.");

      /* should not get here */
      JASSERT(false)
      .Text("Decompression failed!  No restoration will be performed!");
    }
  } else { /* invalid magic number */
    JASSERT(false)
    .Text("ERROR: Invalid magic number in this checkpoint file!");
  }
  return -1;
}

// See comments above for open_ckpt_to_read()
int
openCkptFileToRead(const string &path)
{
  char buf[1024];
  int fd = open_ckpt_to_read(path.c_str());

  // The rest of this function is for compatibility with original definition.
  JASSERT(fd >= 0) (path).Text("Failed to open file.");
  const int len = strlen(DMTCP_FILE_HEADER);
  JASSERT(read(fd, buf, len) == len)(path).Text("read() failed");
  if (strncmp(buf, DMTCP_FILE_HEADER, len) == 0) {
    JTRACE("opened checkpoint file [uncompressed]")(path);
  } else {
    close(fd);
    fd = open_ckpt_to_read(path.c_str()); /* Re-open from beginning */
    JASSERT(fd >= 0) (path).Text("Failed to open file.");
  }
  return fd;
}

// ************************ End of for reading checkpoint files *************


static void
setEnvironFd()
{
  char envFile[PATH_MAX];

  sprintf(envFile, "%s/envFile.XXXXXX", tmpDir.c_str());
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

static void
setNewCkptDir(char *path)
{
  struct stat st;

  if (stat(path, &st) == -1) {
    JASSERT(mkdir(path, S_IRWXU) == 0 || errno == EEXIST)
      (JASSERT_ERRNO) (path)
    .Text("Error creating checkpoint directory");
    JASSERT(0 == access(path, X_OK | W_OK)) (path)
    .Text("ERROR: Missing execute- or write-access to checkpoint dir");
  } else {
    JASSERT(S_ISDIR(st.st_mode)) (path).Text("ckptdir not a directory");
  }

  int fd = open(path, O_RDONLY);
  JASSERT(fd != -1) (path);
  JASSERT(dup2(fd, PROTECTED_CKPT_DIR_FD) == PROTECTED_CKPT_DIR_FD)
    (fd) (path);
  if (fd != PROTECTED_CKPT_DIR_FD) {
    close(fd);
  }
}

// shift args
#define shift argc--, argv++

int
main(int argc, char **argv)
{
  char *tmpdir_arg = NULL;
  char *ckptdir_arg = NULL;

  initializeJalib();

  if (!getenv(ENV_VAR_QUIET)) {
    setenv(ENV_VAR_QUIET, "0", 0);
  }

  if (getenv(ENV_VAR_DISABLE_STRICT_CHECKING)) {
    noStrictChecking = true;
  }

  if (getenv(ENV_VAR_CHECKPOINT_DIR)) {
    ckptdir_arg = getenv(ENV_VAR_CHECKPOINT_DIR);
  }

  if (argc == 1) {
    printf("%s", DMTCP_VERSION_AND_COPYRIGHT_INFO);
    printf("(For help: %s --help)\n\n", argv[0]);
    return DMTCP_FAIL_RC;
  }

  // process args
  shift;
  while (true) {
    string s = argc > 0 ? argv[0] : "--help";
    if (s == "--help" && argc == 1) {
      printf("%s", theUsage);
      return DMTCP_FAIL_RC;
    } else if ((s == "--version") && argc == 1) {
      printf("%s", DMTCP_VERSION_AND_COPYRIGHT_INFO);
      return DMTCP_FAIL_RC;
    } else if (s == "-j" || s == "--join-coordinator" || s == "--join") {
      allowedModes = COORD_JOIN;
      shift;
    } else if (s == "--new-coordinator") {
      allowedModes = COORD_NEW;
      shift;
    } else if (s == "--any-coordinator") {
      allowedModes = COORD_ANY;
      shift;
    } else if (s == "--no-strict-checking") {
      noStrictChecking = true;
      shift;
    } else if (s == "-i" || s == "--interval") {
      setenv(ENV_VAR_CKPT_INTR, argv[1], 1);
      shift; shift;
    } else if (s == "--coord-logfile") {
      setenv(ENV_VAR_COORD_LOGFILE, argv[1], 1);
      shift; shift;
    } else if (s == "--debug-restart-pause") {
      JASSERT(argv[1] && argv[1][0] >= '1' && argv[1][0] <= '4'
                      && argv[1][1] == '\0')
        .Text("--debug-restart-pause requires arg. of '1', '2', '3' or '4'");
      setenv("DMTCP_RESTART_PAUSE", argv[1], 1);
      shift; shift;
    } else if (argv[0][0] == '-' && argv[0][1] == 'i' &&
               isdigit(argv[0][2])) { // else if -i5, for example
      setenv(ENV_VAR_CKPT_INTR, argv[0] + 2, 1);
      shift;
    } else if (argc > 1 &&
               (s == "-h" || s == "--coord-host" || s == "--host")) {
      setenv(ENV_VAR_NAME_HOST, argv[1], 1);
      shift; shift;
    } else if (argc > 1 &&
               (s == "-p" || s == "--coord-port" || s == "--port")) {
      setenv(ENV_VAR_NAME_PORT, argv[1], 1);
      shift; shift;
    } else if (argv[0][0] == '-' && argv[0][1] == 'p' &&
               isdigit(argv[0][2])) { // else if -p0, for example
      setenv(ENV_VAR_NAME_PORT, argv[0] + 2, 1);
      shift;
    } else if (argc > 1 && s == "--port-file") {
      thePortFile = argv[1];
      shift; shift;
    } else if (argc > 1 && (s == "-c" || s == "--ckptdir")) {
      ckptdir_arg = argv[1];
      shift; shift;
    } else if (argc > 1 && (s == "-t" || s == "--tmpdir")) {
      tmpdir_arg = argv[1];
      shift; shift;
    } else if (argc > 1 && (s == "--gdb")) {
      requestedDebugLevel = atoi(argv[1]);
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

  if ((getenv(ENV_VAR_NAME_PORT) == NULL ||
       getenv(ENV_VAR_NAME_PORT)[0]== '\0') &&
      allowedModes != COORD_NEW) {
    allowedModes = (allowedModes == COORD_ANY) ? COORD_NEW : allowedModes;
    setenv(ENV_VAR_NAME_PORT, STRINGIFY(DEFAULT_PORT), 1);
    JTRACE("No port specified\n"
           "Setting mode to --new-coordinator --coord-port "
                                                  STRINGIFY(DEFAULT_PORT));
  }

  tmpDir = Util::calcTmpDir(tmpdir_arg);
  if (ckptdir_arg) {
    setNewCkptDir(ckptdir_arg);
  }

  jassert_quiet = *getenv(ENV_VAR_QUIET) - '0';

  // make sure JASSERT initializes now, rather than during restart
  Util::initializeLogFile(tmpDir);

  if (!noStrictChecking && jassert_quiet < 2 &&
      (getuid() == 0 || geteuid() == 0)) {
    JASSERT_STDERR <<
      "WARNING:  Running dmtcp_restart as root can be dangerous.\n"
      "  An unknown checkpoint image or bugs in DMTCP may lead to unforeseen\n"
      "  consequences.  Continuing as root ....\n";
  }

  JTRACE("New dmtcp_restart process; _argc_ ckpt images") (argc);

  bool doAbort = false;
  for (; argc > 0; shift) {
    string restorename(argv[0]);
    struct stat buf;
    int rc = stat(restorename.c_str(), &buf);
    if (Util::strEndsWith(restorename, "_files")) {
      continue;
    } else if (!Util::strEndsWith(restorename, ".dmtcp")) {
      JNOTE("File doesn't have .dmtcp extension. Check Usage.") (restorename);

      // Don't test for --quiet here.  We're aborting.  We need to say why.
      JASSERT_STDERR << theUsage;
      doAbort = true;
    } else if (rc == -1) {
      char error_msg[1024];
      sprintf(error_msg, "\ndmtcp_restart: ckpt image %s", restorename.c_str());
      perror(error_msg);
      doAbort = true;
    } else if (buf.st_uid != getuid() && !noStrictChecking) {
      /*Could also run if geteuid() matches*/
      printf("\nProcess uid (%d) doesn't match uid (%d) of\n"            \
             "checkpoint image (%s).\n"                                  \
             "This is dangerous.  Aborting for security reasons.\n"      \
             "If you still want to do this, then re-run dmtcp_restart\n" \
             "  with the --no-strict-checking flag.\n",
             getuid(), buf.st_uid, restorename.c_str());
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
        if (t1 == t2) {
          continue;
        }
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
  .Text("There must be at least one process tree that doesn't have\n"
        "  a different process as session leader.");

  WorkerState::setCurrentState(WorkerState::RESTARTING);

  /* Try to find non-orphaned process in independent procs list */
  RestoreTarget *t = NULL;
  bool foundNonOrphan = false;
  RestoreTargetMap::iterator it;
  for (it = independentProcessTreeRoots.begin();
       it != independentProcessTreeRoots.end();
       it++) {
    t = it->second;
    if (!t->isOrphan()) {
      foundNonOrphan = true;
      break;
    }
  }

  JASSERT(t != NULL);
  JASSERT(t->pid() != 0);
  JASSERT(!t->noCoordinator() || allowedModes == COORD_ANY)
  .Text("Process had no coordinator prior to checkpoint;\n"
        "  but either --join-coordinator or --new-coordinator was specified.");

  if (foundNonOrphan) {
    t->createProcess(true);
  } else {
    /* we were unable to find any non-orphaned procs.
     * pick the first one and orphan it */
    t = independentProcessTreeRoots.begin()->second;
    t->createOrphanedProcess(true);
  }

  JASSERT(false).Text("unreachable");
  return -1;
}
