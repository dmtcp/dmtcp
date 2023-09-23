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
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "config.h"
#ifdef HAS_PR_SET_PTRACER
#include <sys/prctl.h>
#endif  // ifdef HAS_PR_SET_PTRACER

#include "../jalib/jassert.h"
#include "../jalib/jconvert.h"
#include "../jalib/jfilesystem.h"
#include "constants.h"
#include "coordinatorapi.h"
#include "dmtcprestartinternal.h"
#include "processinfo.h"
#include "shareddata.h"
#include "uniquepid.h"
#include "util.h"

using namespace dmtcp;

// Copied from mtcp/mtcp_restart.c.
#define DMTCP_MAGIC_FIRST 'D'
#define GZIP_FIRST        037
#ifdef HBICT_DELTACOMP
# define HBICT_FIRST      'H'
#endif // ifdef HBICT_DELTACOMP

// gcc-4.3.4 -Wformat=2 issues false positives for warnings unless the format
// string has at least one format specifier with corresponding format argument.
// Ubuntu 9.01 uses -Wformat=2 by default.
static const char *theUsage =
  "Usage:       dmtcp_restart [OPTIONS] <ckpt1.dmtcp> [ckpt2.dmtcp...]\n"
  "Usage (MPI): dmtcp_restart [OPTIONS] --restartdir [DIR w/ ckpt_rank_*/ ]\n\n"
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
  "  --restartdir Directory that contains checkpoint image directories\n"
  "  --mpi       Use as MPI proxy (default: no MPI proxy)\n"
  "  --tmpdir PATH (environment variable DMTCP_TMPDIR)\n"
  "              Directory to store temp files (default: $TMDPIR or /tmp)\n"
  "  -q, --quiet (or set environment variable DMTCP_QUIET = 0, 1, or 2)\n"
  "              Skip NOTE messages; if given twice, also skip WARNINGs\n"
  "  --coord-logfile PATH (environment variable DMTCP_COORD_LOG_FILENAME\n"
  "              Coordinator will dump its logs to the given file\n"
  "  --debug-restart-pause (or set env var DMTCP_RESTART_PAUSE=1,2,... or 7)\n"
  "              dmtcp_restart will pause early to debug with:  GDB attach\n"
  "              Levels 6 and 7 valid only with DMTCP_EVENT_RESTART in plugin\n"
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

string tmpDir = "/DMTCP/Uninitialized/Tmp/Dir";
string ckptdir_arg;
CoordinatorMode allowedModes = COORD_ANY;
string coord_host;
int coord_port = UNINITIALIZED_PORT;
string thePortFile;

string mtcp_restart;
string mtcp_restart_32;
string fdBuf;
string stderrFd;
string restoreBufAddrStr;
string restoreBufLenStr;
char *pause_param;


static void setEnvironFd();
static void runMtcpRestart(int fd, RestoreTarget *target);
static int readCkptHeader(const string &path, ProcessInfo *pInfo);
static int openCkptFileToRead(const string &path);
static int processCkptImages();

RestoreTarget::RestoreTarget(const string &path)
  : _path(path)
{
  JASSERT(jalib::Filesystem::FileExists(_path))
  (_path).Text("checkpoint file missing");

  _fd = readCkptHeader(_path, &_pInfo);
  uint64_t clock_gettime_offset =
    dmtcp_dlsym_lib_fnc_offset("linux-vdso", "__vdso_clock_gettime");
  uint64_t getcpu_offset =
    dmtcp_dlsym_lib_fnc_offset("linux-vdso", "__vdso_getcpu");
  uint64_t gettimeofday_offset =
    dmtcp_dlsym_lib_fnc_offset("linux-vdso", "__vdso_gettimeofday");
  uint64_t time_offset =
    dmtcp_dlsym_lib_fnc_offset("linux-vdso", "__vdso_time");
  JWARNING(!_pInfo.vdsoOffsetMismatch(clock_gettime_offset, getcpu_offset,
                                      gettimeofday_offset, time_offset))
    .Text("The vDSO section on the current system is different than"
          " the host where the checkpoint image was generated. "
          "Restart may fail if the program calls a function in"
          " vDSO, like gettimeofday(), clock_gettime(), etc.");
  JTRACE("restore target")(_path)(_pInfo.numPeers())(_pInfo.compGroup());
}

void
RestoreTarget::initialize()
{
  UniquePid::ThisProcess() = _pInfo.upid();
  UniquePid::ParentProcess() = _pInfo.uppid();

  DmtcpUniqueProcessId compId = _pInfo.compGroup().upid();
  CoordinatorInfo coordInfo;
  struct in_addr localIPAddr;

  // FIXME:  We will use the new HOST and PORT here, but after restart,
  // we will use the old HOST and PORT from the ckpt image.
  CoordinatorAPI::connectToCoordOnRestart(allowedModes, _pInfo.procname(),
                                          _pInfo.compGroup(), _pInfo.numPeers(),
                                          &coordInfo, &localIPAddr);

  // If port was 0, we'll get new random port when coordinator starts up.
  CoordinatorAPI::getCoordHostAndPort(allowedModes, &coord_host, &coord_port);
  Util::writeCoordPortToFile(coord_port, thePortFile.c_str());

  /* We need to initialize SharedData here to make sure that it is
   * initialized with the correct coordinator timestamp.  The coordinator
   * timestamp is updated only during postCkpt callback. However, the
   * SharedData area may be initialized earlier (for example, while
   * recreating threads), causing it to use *older* timestamp.
   */
  SharedData::initialize(tmpDir.c_str(), &compId, &coordInfo, &localIPAddr);

  Util::initializeLogFile(SharedData::getTmpDir());

  if (ckptdir_arg.empty()) {
    // Create the ckpt-dir fd so that the restarted process can know about
    // the abs-path of ckpt-image.
    string dirName = jalib::Filesystem::DirName(_path);
    int dirfd = open(dirName.c_str(), O_RDONLY);
    JASSERT(dirfd != -1)(JASSERT_ERRNO);
    if (dirfd != PROTECTED_CKPT_DIR_FD) {
      JASSERT(dup2(dirfd, PROTECTED_CKPT_DIR_FD) == PROTECTED_CKPT_DIR_FD);
      close(dirfd);
    }
  }

  setEnvironFd();
}

void
RestoreTarget::restoreGroup()
{
  if (_pInfo.isGroupLeader()) {
    // create new Group where this process becomes a leader
    JTRACE("Create new Group.");
    setpgid(0, 0);
  }
}

void
RestoreTarget::createDependentChildProcess()
{
  pid_t pid = fork();

  JASSERT(pid != -1);
  if (pid != 0) {
    return;
  }
  createProcess();
}

void
RestoreTarget::createDependentNonChildProcess()
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

void
RestoreTarget::createOrphanedProcess(bool createIndependentRootProcesses)
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

void
RestoreTarget::createProcess(bool createIndependentRootProcesses)
{
  initialize();

  JTRACE("Creating process during restart")(upid())(_pInfo.procname());

  RestoreTargetMap::iterator it;
  for (it = targets.begin(); it != targets.end(); it++) {
    RestoreTarget *t = it->second;
    if (_pInfo.upid() == t->_pInfo.upid()) {
      continue;
    } else if (t->uppid() == _pInfo.upid() && t->_pInfo.sid() != _pInfo.pid()) {
      t->createDependentChildProcess();
    }
  }

  if (createIndependentRootProcesses) {
    RestoreTargetMap::iterator it;
    for (it = independentProcessTreeRoots.begin();
         it != independentProcessTreeRoots.end(); it++) {
      RestoreTarget *t = it->second;
      if (t != this) {
        t->createDependentNonChildProcess();
      }
    }
  }

  // If we were the session leader, become one now.
  if (_pInfo.sid() == _pInfo.pid()) {
    if (getsid(0) != _pInfo.pid()) {
      JWARNING(setsid() != -1)
      (getsid(0))(JASSERT_ERRNO)
        .Text("Failed to restore this process as session leader.");
    }
  }

  // Now recreate processes with sid == _pid
  for (it = targets.begin(); it != targets.end(); it++) {
    RestoreTarget *t = it->second;
    if (_pInfo.upid() == t->_pInfo.upid()) {
      continue;
    } else if (t->_pInfo.sid() == _pInfo.pid()) {
      if (t->uppid() == _pInfo.upid()) {
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

  runMtcpRestart(_fd, this);

  JASSERT(false).Text("unreachable");
}

char *get_pause_param()
{
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

  char * pause_param = getenv("DMTCP_RESTART_PAUSE");
  if (pause_param == NULL) {
    pause_param = getenv("MTCP_RESTART_PAUSE");
  }

  if (pause_param == NULL) {
    return NULL;
  }

  if (pause_param[0] < '1' || pause_param[0] > '7' || pause_param[1] != '\0') {
    return NULL;
  }

#ifdef HAS_PR_SET_PTRACER
  prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0); // For: gdb attach
#endif // ifdef HAS_PR_SET_PTRACER

  // If pause_param is not NULL, mtcp_restart will invoke
  //     postRestartDebug() in the checkpoint image instead of postRestart().
  return pause_param;
}

vector<char *>
getMtcpArgs(uint64_t restoreBufAddr, uint64_t restoreBufLen)
{
  vector<char *> mtcpArgs;
  mtcp_restart = Util::getPath(mtcp_restart.c_str());
  pause_param = get_pause_param();
  stderrFd = jalib::XToString(PROTECTED_STDERR_FD);

  mtcpArgs.push_back((char *) mtcp_restart.c_str());
  mtcpArgs.push_back((char *) "--stderr-fd");
  mtcpArgs.push_back((char *) stderrFd.c_str());

  mtcpArgs.push_back((char *) "--restore-buffer-addr");
  restoreBufAddrStr = jalib::XToHexString((void*)restoreBufAddr);
  mtcpArgs.push_back((char *) restoreBufAddrStr.c_str());

  mtcpArgs.push_back((char *) "--restore-buffer-len");
  restoreBufLenStr = jalib::XToHexString((void*)restoreBufLen);
  mtcpArgs.push_back((char *) restoreBufLenStr.c_str());

  if (pause_param) {
    mtcpArgs.push_back((char *) "--mtcp-restart-pause");
    mtcpArgs.push_back(pause_param);
  }

  return mtcpArgs;
}

void
publishKeyValueMapToMtcpEnvironment(RestoreTarget *restoreTarget)
{
  const map<string, string> &kvmap = restoreTarget->getKeyValueMap();
  for (auto kv : kvmap) {
    setenv(kv.first.c_str(), kv.second.c_str(), 1);
  }

  return;
}

vector<char*> StringVectorToCharPtrVector(vector<string> const& strings)
{
  vector<char*> ptrs;
  for (size_t i = 0; i < strings.size(); i++) {
    ptrs.push_back((char*) strings[i].c_str());
  }

  return ptrs;
}

static void
runMtcpRestart(int fd, RestoreTarget *restoreTarget)
{
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
      char cpid[11]; // XXX: Is 10 digits for long PID plus a terminating null
      snprintf(cpid, 11, "%ld", (long unsigned)pid);
      char* const command[] = {const_cast<char*>("gdb"),
                               const_cast<char*>(restoreTarget->
                                                 procSelfExe().c_str()),
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

  publishKeyValueMapToMtcpEnvironment(restoreTarget);
  vector<char *> mtcpArgs = getMtcpArgs(restoreTarget->restoreBufAddr(), restoreTarget->restoreBufLen());

#if defined(__x86_64__) || defined(__aarch64__)
  // FIXME: This is needed for CONFIG_M32 only because getPath("mtcp_restart")
  // fails to return the absolute path for mtcp_restart.  We should fix
  // the bug in Util::getPath() and remove CONFIG_M32 condition in #if.
  if (restoreTarget->getElfType() == ProcessInfo::Elf_32) {
    mtcp_restart_32 = Util::getPath(mtcp_restart_32.c_str(), true);
    mtcpArgs[0] = (char *) mtcp_restart_32.c_str();
  }
#endif // if defined(__x86_64__) || defined(__aarch64__) || defined(CONFIG_M32)

  fdBuf = jalib::XToString(fd);
  mtcpArgs.push_back((char *) "--fd");
  mtcpArgs.push_back((char *) fdBuf.c_str());

  mtcpArgs.push_back(NULL);
  execvp(mtcpArgs[0], &mtcpArgs[0]);

  JASSERT(false) (mtcpArgs[0]) (mtcpArgs[1]) (JASSERT_ERRNO)
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
setNewCkptDir(const string& path)
{
  struct stat st;

  if (stat(path.c_str(), &st) == -1) {
    JASSERT(mkdir(path.c_str(), S_IRWXU) == 0 || errno == EEXIST)
      (JASSERT_ERRNO) (path)
    .Text("Error creating checkpoint directory");
    JASSERT(0 == access(path.c_str(), X_OK | W_OK)) (path)
    .Text("ERROR: Missing execute- or write-access to checkpoint dir");
  } else {
    JASSERT(S_ISDIR(st.st_mode)) (path).Text("ckptdir not a directory");
  }

  int fd = open(path.c_str(), O_RDONLY);
  JASSERT(fd != -1) (path);
  JASSERT(dup2(fd, PROTECTED_CKPT_DIR_FD) == PROTECTED_CKPT_DIR_FD)
    (fd) (path);
  if (fd != PROTECTED_CKPT_DIR_FD) {
    close(fd);
  }
}

// shift args
#define shift argc--, argv++

DmtcpRestart::DmtcpRestart(int argc, char **argv, const string& binaryName, const string& mtcpRestartBinaryName)
: argc(argc), argv(argv), binaryName(binaryName), mtcpRestartBinaryName(mtcpRestartBinaryName)
{
  char *tmpdir_arg = NULL;

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
    exit(DMTCP_FAIL_RC);
  }

  // process args
  shift;
  while (argc > 0) { // ... --restartdir ./ OR ... ckptImg
    string s = argc > 0 ? argv[0] : "--help";
    if (s == "--help" && argc == 1) {
      printf("%s", theUsage);
      exit(0);
    } else if ((s == "--version") && argc == 1) {
      printf("%s", DMTCP_VERSION_AND_COPYRIGHT_INFO);
      exit(0);
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
      JASSERT(argv[1] && argv[1][0] >= '1' && argv[1][0] <= '7'
                      && argv[1][1] == '\0')
        .Text("--debug-restart-pause requires arg. of '1' or '2' or ...` '7'");
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
    } else if (argc > 1 && (s == "-r" || s == "--restartdir")) {
      restartDir = string(argv[1]);
      shift; shift;
    } else if (argc > 1 && (s == "--gdb")) {
      requestedDebugLevel = atoi(argv[1]);
      shift; shift;
    } else if (s == "--mpi") {
      runMpiProxy = true;
      shift;
    } else if (s == "-q" || s == "--quiet") {
      *getenv(ENV_VAR_QUIET) = *getenv(ENV_VAR_QUIET) + 1;

      // Just in case a non-standard version of setenv is being used:
      setenv(ENV_VAR_QUIET, getenv(ENV_VAR_QUIET), 1);
      shift;
    } else if ((s.length() > 2 && s.substr(0, 2) == "--") ||
               (s.length() > 1 && s.substr(0, 1) == "-")) {
      printf("Invalid Argument\n%s", theUsage);
      exit(DMTCP_FAIL_RC);
    } else if (argc > 1 && s == "--") {
      shift;
      break;
    } else {
      break; // argv[0] is first ckpt file to be restored; finish parsing later
    }
  }

  tmpDir = Util::calcTmpDir(tmpdir_arg);

  // make sure JASSERT initializes now, rather than during restart
  Util::initializeLogFile(tmpDir.c_str(), binaryName.c_str());

  if ((getenv(ENV_VAR_NAME_PORT) == NULL ||
       getenv(ENV_VAR_NAME_PORT)[0]== '\0') &&
      allowedModes != COORD_NEW) {
    allowedModes = (allowedModes == COORD_ANY) ? COORD_NEW : allowedModes;
    setenv(ENV_VAR_NAME_PORT, STRINGIFY(DEFAULT_PORT), 1);
    JTRACE("No port specified\n"
           "Setting mode to --new-coordinator --coord-port "
                                                  STRINGIFY(DEFAULT_PORT));
  }

  if (!ckptdir_arg.empty()) {
    setNewCkptDir(ckptdir_arg);
  }

  jassert_quiet = *getenv(ENV_VAR_QUIET) - '0';
  if (!noStrictChecking && jassert_quiet < 2 &&
      (getuid() == 0 || geteuid() == 0)) {
    JASSERT_STDERR <<
      "WARNING:  Running dmtcp_restart as root can be dangerous.\n"
      "  An unknown checkpoint image or bugs in DMTCP may lead to unforeseen\n"
      "  consequences.  Continuing as root ....\n";
  }

  JTRACE("New dmtcp_restart process; _argc_ ckpt images") (argc);

  for (; argc > 0; shift) {
    string restorename(argv[0]);
    struct stat buf;
    JASSERT(stat(restorename.c_str(), &buf) != -1);

    if (Util::strEndsWith(restorename.c_str(), "_files")) {
      continue;
    } else if (!Util::strEndsWith(restorename.c_str(), ".dmtcp")) {
      JNOTE("File doesn't have .dmtcp extension. Check Usage.") (restorename);
      // Don't test for --quiet here.  We're aborting.  We need to say why.
      JASSERT_STDERR << theUsage;
      exit(DMTCP_FAIL_RC);
    } else if (buf.st_uid != getuid() && !noStrictChecking) {
      /*Could also run if geteuid() matches*/
      JASSERT(false) (getuid()) (buf.st_uid) (restorename)
        .Text("Process uid doesn't match uid of checkpoint image.\n"      \
              "This is dangerous.  Aborting for security reasons.\n"      \
              "If you still want to do this, then re-run dmtcp_restart\n" \
              "  with the --no-strict-checking flag.\n");
    }

    JTRACE("Will restart ckpt image") (argv[0]);
    ckptImages.push_back(argv[0]);
  }

  // Can't specify ckpt images with --restartdir flag.
  if (restartDir.empty() ^ (ckptImages.size() > 0)) {
    JASSERT_STDERR << theUsage;
    exit(DMTCP_FAIL_RC);
  }

  CoordinatorAPI::getCoordHostAndPort(allowedModes, &coord_host, &coord_port);

  mtcp_restart = mtcpRestartBinaryName;
  mtcp_restart_32 = mtcpRestartBinaryName + "-32";
}

void
DmtcpRestart::processCkptImages()
{
  for (const string& ckptImage : ckptImages) {
      RestoreTarget *t = new RestoreTarget(ckptImage);
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

  if (foundNonOrphan) {
    t->createProcess(true);
  } else {
    /* we were unable to find any non-orphaned procs.
     * pick the first one and orphan it */
    t = independentProcessTreeRoots.begin()->second;
    t->createOrphanedProcess(true);
  }

  JASSERT(false).Text("unreachable");
}
