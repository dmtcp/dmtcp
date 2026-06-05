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

#include "processinfo.h"
#include <fcntl.h>
#include <fenv.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>
#include "../jalib/jconvert.h"
#include "../jalib/jfilesystem.h"
#include "coordinatorapi.h"
#include "plugin/pid/pidhelpers.h"
#include "procselfmaps.h"
#include "syscallwrappers.h"
#include "uniquepid.h"
#include "util.h"
#include "util_assert.h"

namespace dmtcp
{
static ProcessInfo *pInfo = NULL;
static ProcessInfo *vforkBackup = NULL;
constexpr uint64_t EndOfBrkMapSize = 0x1000000;

static DmtcpMutex tblLock = DMTCP_MUTEX_INITIALIZER;

static void
_do_lock_tbl()
{
  ASSERT_LOCK_SUCCESS(DmtcpMutexLock(&tblLock));
}

static void
_do_unlock_tbl()
{
  ASSERT_LOCK_SUCCESS(DmtcpMutexUnlock(&tblLock));
}

static void
checkpoint()
{
  ProcessInfo::instance().getState();
}

static void
resume()
{
  ProcessInfo::instance().incrementNumCheckpoints();
}

static void
restart()
{
  ProcessInfo::instance().incrementNumRestarts();
  ProcessInfo::instance().restart();
}

static void
processInfo_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
  case DMTCP_EVENT_INIT:
    ProcessInfo::instance().init();
    break;

  case DMTCP_EVENT_PRE_EXEC:
  {
    jalib::JBinarySerializeWriterRaw wr("", data->preExec.serializationFd);
    ProcessInfo::instance().getState();
    ProcessInfo::instance().serialize(wr);
    break;
  }

  case DMTCP_EVENT_POST_EXEC:
  {
    jalib::JBinarySerializeReaderRaw rd("", data->postExec.serializationFd);
    ProcessInfo::instance().serialize(rd);
    ProcessInfo::instance().postExec();
    break;
  }

  case DMTCP_EVENT_ATFORK_CHILD:
  case DMTCP_EVENT_VFORK_CHILD:
    ProcessInfo::instance().resetOnFork();
    break;

  case DMTCP_EVENT_VFORK_PREPARE:
    vforkBackup = pInfo;
    pInfo = NULL;
    break;

  case DMTCP_EVENT_VFORK_PARENT:
  case DMTCP_EVENT_VFORK_FAILED:
    delete pInfo;
    pInfo = vforkBackup;
    break;

  case DMTCP_EVENT_PRESUSPEND:
    break;

  case DMTCP_EVENT_PRECHECKPOINT:
    checkpoint();
    break;

  case DMTCP_EVENT_RESUME:
    resume();
    break;

  case DMTCP_EVENT_RESTART:
    restart();
    break;

  default:
    break;
  }
}

LIB_PRIVATE DmtcpPluginDescriptor_t processInfoPlugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "PROCESS_INFO",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "processInfo plugin",
  processInfo_EventHook
};

ProcessInfo::ProcessInfo()
  : _upid(),
    _uppid(),
    _compGroup(),
    _pid(-1),
    _ppid(-1),
    _sid(-1),
    _gid(-1),
    _fgid(-1),
    _isRootOfProcessTree(false),
    _numPeers(1),
    _elfType(0),
    _clock_gettime_offset(0),
    _getcpu_offset(0),
    _gettimeofday_offset(0),
    _time_offset(0),
    _restoreBuf{0, 0},
    _vdso{0, 0},
    _vvar{0, 0},
    _vvarVClock{0, 0},
    _savedBrkForCkpt(0),
    _endOfStack(0),
    _procname(),
    _procSelfExe(),
    upid(_upid),
    uppid(_uppid),
    compGroup(_compGroup),
    pid(_pid),
    ppid(_ppid),
    sid(_sid),
    gid(_gid),
    fgid(_fgid),
    isRootOfProcessTree(_isRootOfProcessTree),
    numPeers(_numPeers),
    elfType(_elfType),
    clock_gettime_offset(_clock_gettime_offset),
    getcpu_offset(_getcpu_offset),
    gettimeofday_offset(_gettimeofday_offset),
    time_offset(_time_offset),
    restoreBuf(_restoreBuf),
    vdso(_vdso),
    vvar(_vvar),
    vvarVClock(_vvarVClock),
    savedBrk(_savedBrkForCkpt),
    endOfStack(_endOfStack),
    procname(_procname),
    procSelfExe(_procSelfExe)
{
  char buf[PATH_MAX];

  _do_lock_tbl();

  upid = UniquePid::ThisProcess();
  uppid = UniquePid::ParentProcess();

  restoreBuf.startAddr = 0;
  restoreBuf.endAddr = 0;

  pid = -1;
  ppid = -1;
  sid = -1;
  gid = -1;
  fgid = -1;
  isRootOfProcessTree = false;
  numPeers = 1;

#ifdef CONFIG_M32
  elfType = Elf_32;
#else // ifdef CONFIG_M32
  elfType = Elf_64;
#endif // ifdef CONFIG_M32

  clock_gettime_offset = dmtcp_dlsym_lib_fnc_offset("linux-vdso",
                                                     "__vdso_clock_gettime");
  getcpu_offset = dmtcp_dlsym_lib_fnc_offset("linux-vdso",
                                              "__vdso_getcpu");
  gettimeofday_offset = dmtcp_dlsym_lib_fnc_offset("linux-vdso",
                                                    "__vdso_gettimeofday");
  time_offset = dmtcp_dlsym_lib_fnc_offset("linux-vdso", "__vdso_time");

  vdso = {0, 0};
  vvar = {0, 0};
  vvarVClock = {0, 0};
  endOfStack = 0;
  savedBrk = (uint64_t) sbrk(0);
  endOfStack = 0;

  string procSelfExeStr = jalib::Filesystem::ResolveSymlink("/proc/self/exe");
  strncpy(procSelfExe, procSelfExeStr.c_str(), sizeof(procSelfExe) - 1);

  _generation = 0;

  // _generation, above, is per-process.
  // This contrasts with DmtcpUniqueProcessId:_computation_generation, which is
  // shared among all process on a node; used in variable sharedDataHeader.
  // _generation is updated when _this_ process begins its checkpoint.
  _pthreadJoinId.clear();
  ASSERT_ERRNO(getcwd(buf, sizeof buf) != NULL,
               "failed to capture launch cwd");
  _launchCWD = buf;
  _do_unlock_tbl();
}

ProcessInfo&
ProcessInfo::instance()
{
  if (pInfo == NULL) {
    pInfo = new ProcessInfo();
  }
  return *pInfo;
}

void
ProcessInfo::growStack()
{
  /* Grow the stack to the stack limit */
  struct rlimit rlim;
  size_t stackSize;
  const rlim_t eightMB = 8 * 1024 * 1024;

  ASSERT_ERRNO(getrlimit(RLIMIT_STACK, &rlim) == 0,
               "failed to read RLIMIT_STACK");
  if (rlim.rlim_cur == RLIM_INFINITY) {
    if (rlim.rlim_max == RLIM_INFINITY) {
      stackSize = 8 * 1024 * 1024;
    } else {
      stackSize = MIN(rlim.rlim_max, eightMB);
    }
  } else {
    stackSize = rlim.rlim_cur;
  }

  // Find the current stack area, heap, stack, vDSO and vvar areas.
  ProcMapsArea area;
  ProcMapsArea stackArea;
  memset(&stackArea, 0, sizeof(stackArea));
  size_t allocSize;
  void *tmpbuf;
  ProcSelfMaps procSelfMaps;
  while (procSelfMaps.getNextArea(&area)) {
    if (strcmp(area.name, "[heap]") == 0) {
      // Record start of heap which will later be used to restore heap
      _savedHeapStart = (unsigned long)area.addr;
    } else if (strcmp(area.name, "[vdso]") == 0) {
      vdso.startAddr = (unsigned long)area.addr;
      vdso.endAddr = (unsigned long)area.endAddr;
    } else if (strcmp(area.name, "[vvar]") == 0) {
      vvar.startAddr = (unsigned long)area.addr;
      vvar.endAddr = (unsigned long)area.endAddr;
    } else if (strcmp(area.name, "[vvar_vclock]") == 0) {
      vvarVClock.startAddr = (unsigned long)area.addr;
      vvarVClock.endAddr = (unsigned long)area.endAddr;
    } else if ((VA)&area >= area.addr && (VA)&area < area.endAddr) {
      JTRACE("Original stack area") ((void *)area.addr) (area.size);
      stackArea = area;
      endOfStack = (uintptr_t) area.endAddr;
      /*
       * When using Matlab with dmtcp_launch, sometimes the bottommost
       * page of stack (the page with highest address) which contains the
       * environment strings and the argv[] was not shown in /proc/self/maps.
       * This is arguably a bug in the Linux kernel as of version 2.6.32, etc.
       * This happens on some odd combination of environment passed on to
       * Matlab process. As a result, the page was not checkpointed and hence
       * the process segfaulted on restart. The fix is to try to mprotect this
       * page with RWX permission to make the page visible again. This call
       * will fail if no stack page was invisible to begin with.
       */

      // FIXME : If the area following the stack is not empty, don't
      // exercise this path.
      int ret = mprotect(area.addr + area.size, 0x1000,
                         PROT_READ | PROT_WRITE | PROT_EXEC);
      if (ret == 0) {
        JTRACE("bottommost page of stack (page with highest address) was"
               " invisible in /proc/self/maps. It is made visible again now.");
      }
    }
  }
  ASSERT(stackArea.addr != NULL,
         "failed to find current stack mapping in /proc/self/maps");

  if (stackSize > stackArea.size + 4095) {
    // Grow the stack, if possible
    allocSize = stackSize - stackArea.size - 4095;
    tmpbuf = alloca(allocSize);
    ASSERT(tmpbuf != NULL, "failed to grow stack: allocSize={}", allocSize);
    memset(tmpbuf, 0, allocSize);
  }

#ifdef LOGGING
  {
    ProcSelfMaps maps;
    while (maps.getNextArea(&area)) {
      if ((VA)&area >= area.addr && (VA)&area < area.endAddr) { // Stack found
        JTRACE("New stack size") ((void *)area.addr) (area.size);
        break;
      }
    }
  }
#endif // ifdef LOGGING
}

void
ProcessInfo::init()
{
  if (pid == -1) {
    // This is a brand new process.
    pid = getpid();
    ppid = getppid();
    isRootOfProcessTree = true;
    uppid = UniquePid();

    string procSelfExeStr = jalib::Filesystem::ResolveSymlink("/proc/self/exe");
    strncpy(procSelfExe, procSelfExeStr.c_str(), sizeof(procSelfExe) - 1);
  }

#ifdef CONFIG_M32
  elfType = Elf_32;
#else // ifdef CONFIG_M32
  elfType = Elf_64;
#endif // ifdef CONFIG_M32

  vdso = {0, 0};
  vvar = {0, 0};
  vvarVClock = {0, 0};
  endOfStack = 0;

  processRlimit();

  growStack();

  _initialSavedBrk = (uint64_t)sbrk(0);
  uint64_t brkMmap = (uint64_t)mmap(
    (void *)_initialSavedBrk, EndOfBrkMapSize, PROT_NONE,
    MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0);
  ASSERT_EQ(brkMmap, _initialSavedBrk);

  // Reserve space for restoreBuf
  updateRestoreBufAddr();

  if (_ckptDir.empty()) {
    updateCkptDirFileSubdir();
  }
}

void
ProcessInfo::updateRestoreBufAddr()
{
  // This method could be called by ProcessInfo::init() or ProcessInfo::restart().
  // If it was called by ProcessInfo::restart(), then mtcp_restart() will
  // have mmap'ed this "restoreBuf".
  // NOTE:  We are now doing 'munmap' on it, only to do ''mmap' on it once again
  //        at the end of this method.  We need to munmap/mmap because we want
  //        to free the backing physical pages created by mtcp_restart.

  if (restoreBuf.startAddr != 0) {
    ASSERT_ERRNO(munmap((void*) restoreBuf.startAddr,
                        RESTORE_BUF_TOTAL_SIZE) == 0,
                 "failed to unmap restore buffer: start={} size={}",
                 restoreBuf.startAddr, RESTORE_BUF_TOTAL_SIZE);
  }

  int flags = MAP_SHARED | MAP_ANONYMOUS;

  if (restoreBuf.startAddr != 0) {
    flags += MAP_FIXED;
  }

  // FIXME:  ProcessInfo::init() sets  len to RESTORE_TOTAL_SIZE.  This
  //         then sets it to _restoreBufLen.  But ProcessInfo::init() had
  //         previously set _restoreBufLen to RESTORE_TOTAL_SIZE.  So, we
  //         have now made the round trip.  This is spaghetti code. :-(
  uint64_t requestedAddr = restoreBuf.startAddr;
  restoreBuf.startAddr = (uint64_t) mmap((void*)requestedAddr,
                                         RESTORE_BUF_TOTAL_SIZE,
                                         PROT_NONE, flags, -1, 0);
  ASSERT_ERRNO(restoreBuf.startAddr != (uint64_t) MAP_FAILED,
               "failed to map restore buffer: requested={} size={} flags={}",
               requestedAddr, RESTORE_BUF_TOTAL_SIZE, flags);
  restoreBuf.endAddr = restoreBuf.startAddr + RESTORE_BUF_TOTAL_SIZE;
}

void
ProcessInfo::processRlimit()
{
#ifdef __i386__

  // Match work begun in dmtcpPrepareForExec()
# if 0
  if (getenv("DMTCP_ADDR_COMPAT_LAYOUT")) {
    _dmtcp_unsetenv("DMTCP_ADDR_COMPAT_LAYOUT");

    // DMTCP had set ADDR_COMPAT_LAYOUT.  Now unset it.
    personality((unsigned long)personality(0xffffffff) ^ ADDR_COMPAT_LAYOUT);
    JTRACE("unsetting ADDR_COMPAT_LAYOUT");
  }
# else // if 0
  { char *rlim_cur_char = getenv("DMTCP_RLIMIT_STACK");
    if (rlim_cur_char != NULL) {
      struct rlimit rlim;
      getrlimit(RLIMIT_STACK, &rlim);
      rlim.rlim_cur = atol(rlim_cur_char);
      JTRACE("rlim_cur for RLIMIT_STACK being restored.") (rlim.rlim_cur);
      setrlimit(RLIMIT_STACK, &rlim);
      _dmtcp_unsetenv("DMTCP_RLIMIT_STACK");
    }
  }
# endif // if 0
#endif // ifdef __i386__
}

void
ProcessInfo::updateCkptDirFileSubdir(string newCkptDir)
{
  if (newCkptDir != "") {
    _ckptDir = newCkptDir;
  }

  if (_ckptDir.empty()) {
    const char *dir = getenv(ENV_VAR_CHECKPOINT_DIR);
    if (dir == NULL) {
      dir = ".";
    }
    _ckptDir = dir;
  }

  ostringstream o;
  o << _ckptDir << "/"
    << CKPT_FILE_PREFIX
    << jalib::Filesystem::GetProgramName()
    << '_' << UniquePid::ThisProcess();

  _ckptFileName = o.str() + CKPT_FILE_SUFFIX;
  _ckptFilesSubDir = o.str() + CKPT_FILES_SUBDIR_SUFFIX;
}

void
ProcessInfo::postExec()
{
  string procSelfExeStr = jalib::Filesystem::ResolveSymlink("/proc/self/exe");
  strncpy(procSelfExe, procSelfExeStr.c_str(), sizeof(procSelfExe) - 1);

  strncpy(procname, jalib::Filesystem::GetProgramName().c_str(), sizeof(procname) -1);

  upid = UniquePid::ThisProcess();
  uppid = UniquePid::ParentProcess();
  updateCkptDirFileSubdir();
}

void
ProcessInfo::resetOnFork()
{
  // Initialize the log file
  Util::initializeLogFile(SharedData::getTmpDir());

  DmtcpMutexInit(&tblLock, DMTCP_MUTEX_NORMAL);
  ppid = pid;
  pid = getpid();

  upid = UniquePid();
  uppid = UniquePid();
  _upidStr.clear();

  isRootOfProcessTree = false;
  _pthreadJoinId.clear();
  _ckptFileName.clear();
  _ckptFilesSubDir.clear();
  updateCkptDirFileSubdir();
}

void
ProcessInfo::restoreHeap()
{
  // Release backing memory for EndOfBrkMap memory region.
  ASSERT_EQ(0,
            madvise((void *)_initialSavedBrk, EndOfBrkMapSize, MADV_DONTNEED));

  /* If the original start of heap is lower than the current end of heap, we
   * want to mmap the area between _savedBrk and current break. This
   * happens when the size of checkpointed program is smaller then the size of
   * mtcp_restart program.
   */
  uint64_t curBrk = (uint64_t)sbrk(0);

  if (curBrk > savedBrk) {
    JNOTE("Area between saved_break and curr_break not mapped, mapping it now")
      (savedBrk) (curBrk);
    size_t oldsize = savedBrk - _savedHeapStart;
    size_t newsize = curBrk - _savedHeapStart;

    void *remappedHeap = mremap((void *)_savedHeapStart, oldsize, newsize, 0);
    ASSERT_ERRNO(remappedHeap != MAP_FAILED,
                 "mremap failed to map area between saved break and current "
                 "break: heapStart={} oldSize={} newSize={} savedBrk={} "
                 "curBrk={}",
                 _savedHeapStart, oldsize, newsize, savedBrk, curBrk);
  } else if (curBrk < savedBrk) {
    if (brk((void *)savedBrk) != 0) {
      JNOTE("Failed to restore area between saved_break and curr_break.")
        (savedBrk) (curBrk) (JASSERT_ERRNO);
    }
  }
}

void
ProcessInfo::restart()
{
  updateRestoreBufAddr();

  restoreHeap();

  // Update the ckptDir
  string ckptDir = jalib::Filesystem::GetDeviceName(PROTECTED_CKPT_DIR_FD);
  ASSERT(ckptDir.length() > 0, "restart checkpoint directory is empty");
  _real_close(PROTECTED_CKPT_DIR_FD);
  updateCkptDirFileSubdir(ckptDir);

  if (_launchCWD != _ckptCWD) {
    string rpath = "";
    size_t llen = _launchCWD.length();
    if (Util::strStartsWith(_ckptCWD.c_str(), _launchCWD.c_str()) &&
        _ckptCWD[llen] == '/') {
      // _launchCWD = "/A/B"; _ckptCWD = "/A/B/C" -> rpath = "./c"
      rpath = "./" + _ckptCWD.substr(llen + 1);
      if (chdir(rpath.c_str()) == 0) {
        JTRACE("Changed cwd") (_launchCWD) (_ckptCWD) (_launchCWD + rpath);
      } else {
        WARNING_ERRNO(chdir(_ckptCWD.c_str()) == 0,
                      "failed to change directory to checkpoint cwd: "
                      "ckptCWD={} launchCWD={}",
                      _ckptCWD, _launchCWD);
      }
    }
  }

  restoreProcessGroupInfo();
  // Closing PROTECTED_ENVIRON_FD here breaks dmtcp_get_restart_env()
  // _real_close(PROTECTED_ENVIRON_FD);
}

void
ProcessInfo::restoreProcessGroupInfo()
{
  pid_t curPid = getpid();
  pid_t curSid = getsid(0);

  if (sid == pid && curSid != curPid) {
    JTRACE("Restore Session Leadership") (sid) (pid) (curSid) (curPid);
    WARNING_ERRNO(setsid() != -1,
                  "cannot restore session leadership: savedSid={} "
                  "savedPid={} currentSid={} currentPid={}",
                  sid, pid, curSid, curPid);
  }

  // Restore group assignment
  pid_t cgid = getpgid(0);
  if (gid != cgid) {
    JTRACE("Restore Group Assignment")
      (gid) (fgid) (cgid) (pid) (ppid) (getppid());
    WARNING_ERRNO(setpgid(0, gid) == 0,
                  "cannot change process group: savedGid={} currentGid={} "
                  "savedPid={} savedPpid={}",
                  gid, cgid, pid, ppid);
  } else {
    JTRACE("Group is already assigned") (gid) (cgid);
  }
}

bool
ProcessInfo::beginPthreadJoin(pthread_t thread)
{
  bool res = false;

  _do_lock_tbl();
  map<pthread_t, pthread_t>::iterator i = _pthreadJoinId.find(thread);
  if (i == _pthreadJoinId.end()) {
    _pthreadJoinId[thread] = pthread_self();
    res = true;
  }
  _do_unlock_tbl();
  return res;
}

void
ProcessInfo::clearPthreadJoinState(pthread_t thread)
{
  _do_lock_tbl();
  if (_pthreadJoinId.find(thread) != _pthreadJoinId.end()) {
    _pthreadJoinId.erase(thread);
  }
  _do_unlock_tbl();
}

void
ProcessInfo::endPthreadJoin(pthread_t thread)
{
  _do_lock_tbl();
  if (_pthreadJoinId.find(thread) != _pthreadJoinId.end() &&
      pthread_equal(_pthreadJoinId[thread], pthread_self())) {
    _pthreadJoinId.erase(thread);
  }
  _do_unlock_tbl();
}

void
ProcessInfo::setCkptFilename(const char *filename)
{
  ASSERT(filename != NULL, "checkpoint filename must not be null");
  if (filename[0] == '/') {
    _ckptDir = jalib::Filesystem::DirName(filename);
    _ckptFileName = filename;
  } else {
    _ckptFileName = _ckptDir + "/" + filename;
  }

  if (Util::strEndsWith(_ckptFileName.c_str(), CKPT_FILE_SUFFIX)) {
    string ckptFileBaseName =
      _ckptFileName.substr(0, _ckptFileName.length() - CKPT_FILE_SUFFIX_LEN);
    _ckptFilesSubDir = ckptFileBaseName + CKPT_FILES_SUBDIR_SUFFIX;
  } else {
    _ckptFilesSubDir = _ckptFileName + CKPT_FILES_SUBDIR_SUFFIX;
  }
}

void
ProcessInfo::setCkptDir(const char *dir)
{
  ASSERT(dir != NULL, "checkpoint directory must not be null");
  _ckptDir = dir;
  _ckptFileName = _ckptDir + "/" + jalib::Filesystem::BaseName(_ckptFileName);
  _ckptFilesSubDir = _ckptDir + "/" + jalib::Filesystem::BaseName(
      _ckptFilesSubDir);

  JTRACE("setting ckptdir") (_ckptDir) (_ckptFilesSubDir);

  // JASSERT(access(_ckptDir.c_str(), X_OK|W_OK) == 0) (_ckptDir)
  // .Text("Missing execute- or write-access to checkpoint dir.");
}

void
ProcessInfo::getState()
{
  pid_t currentPid = getpid();
  ASSERT(pid == currentPid,
         "ProcessInfo pid invariant failed: stored={} current={}", pid,
         currentPid);

  gid = getpgid(0);
  sid = getsid(0);

  fgid = -1;

  // Try to open the controlling terminal
  int tfd = _real_open("/dev/tty", O_RDWR);
  if (tfd != -1) {
    fgid = tcgetpgrp(tfd);
    _real_close(tfd);
  }

  if (ppid != getppid()) {
    // Our original parent died; we are the root of the process tree now.
    //
    // On older systems, a process is inherited by init (pid = 1) after its
    // parent dies. However, with the new per-user init process, the parent
    // pid is no longer "1"; it's the pid of the user-specific init process.
    ppid = getppid();
    isRootOfProcessTree = true;
    uppid = UniquePid();
  } else {
    uppid = UniquePid::ParentProcess();
  }

  string procSelfExeStr = jalib::Filesystem::ResolveSymlink("/proc/self/exe");
  strncpy(procSelfExe, procSelfExeStr.c_str(), sizeof(procSelfExe) - 1);

  strncpy(procname, jalib::Filesystem::GetProgramName().c_str(), sizeof(procname) -1);
  _hostname = jalib::Filesystem::GetCurrentHostname();
  upid = UniquePid::ThisProcess();

  char buf[PATH_MAX];
  ASSERT_ERRNO(getcwd(buf, sizeof buf) != NULL,
               "failed to capture checkpoint cwd");
  _ckptCWD = buf;

  savedBrk = (uint64_t) sbrk(0);

  JTRACE("CHECK GROUP PID")(gid)(fgid)(ppid)(pid);
}

void
ProcessInfo::fillCheckpointHeader(DmtcpCkptHeader *header,
                                  uint64_t checkpointSavedBrk,
                                  PostRestartFnPtr_t postRestart) const
{
  memset(header, 0, sizeof(*header));
  strcpy(header->ckptSignature, DMTCP_CKPT_SIGNATURE);
  dmtcp_init_ckpt_header_bootstrap(header);

  header->upid = upid;
  header->uppid = uppid;
  header->compGroup = compGroup;

  header->pid = pid;
  header->ppid = ppid;
  header->sid = sid;
  header->gid = gid;
  header->fgid = fgid;
  header->isRootOfProcessTree = isRootOfProcessTree;

  header->numPeers = numPeers;
  header->elfType = elfType;

  header->clock_gettime_offset = clock_gettime_offset;
  header->getcpu_offset = getcpu_offset;
  header->gettimeofday_offset = gettimeofday_offset;
  header->time_offset = time_offset;

  header->restoreBuf = restoreBuf;
  header->vdso = vdso;
  header->vvar = vvar;
  header->vvarVClock = vvarVClock;

  header->savedBrk = checkpointSavedBrk;
  header->endOfStack = endOfStack;
  header->postRestartAddr = reinterpret_cast<uintptr_t>(postRestart);

  memcpy(header->procname, procname, sizeof(header->procname));
  memcpy(header->procSelfExe, procSelfExe, sizeof(header->procSelfExe));
}

void
ProcessInfo::addKeyValuePairToCkptHeader(const string &key, const string &value)
{
  kvmap[key] = value;
}

const string&
ProcessInfo::getValue(const string &key)
{
  static string *empty = new string();
  if (kvmap.find(key) != kvmap.end()) {
    return kvmap[key];
  }

  return *empty;
}

void
ProcessInfo::serialize(jalib::JBinarySerializer &o)
{
  JSERIALIZE_ASSERT_POINT("ProcessInfo:");
  savedBrk = (uint64_t) sbrk(0);

  o & elfType;
  o & isRootOfProcessTree & pid & sid & ppid & gid & fgid & _generation;
  o & procname & procSelfExe & _hostname & _launchCWD & _ckptCWD;
  o & upid & uppid;
  o & clock_gettime_offset & getcpu_offset
    & gettimeofday_offset & time_offset;
  o & compGroup & numPeers;
  o & restoreBuf.startAddr & _savedHeapStart & savedBrk;
  o & vdso & vvar & vvarVClock;
  o & endOfStack;
  o & _ckptDir & _ckptFileName & _ckptFilesSubDir;
  o & kvmap;

  JTRACE("Serialized process information")
    (sid) (ppid) (gid) (fgid) (isRootOfProcessTree)
    (procname) (_hostname) (_launchCWD) (_ckptCWD) (upid) (uppid)
    (compGroup) (numPeers) (elfType);

  if (isRootOfProcessTree) {
    JTRACE("This process is Root of Process Tree");
  }

  JSERIALIZE_ASSERT_POINT("EOF");
}
}
