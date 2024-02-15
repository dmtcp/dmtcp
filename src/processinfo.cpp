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
#include "procselfmaps.h"
#include "syscallwrappers.h"
#include "uniquepid.h"
#include "util.h"

namespace dmtcp
{
static ProcessInfo *pInfo = NULL;
static ProcessInfo *vforkBackup = NULL;

static DmtcpMutex tblLock = DMTCP_MUTEX_INITIALIZER;

static void
_do_lock_tbl()
{
  JASSERT(DmtcpMutexLock(&tblLock) == 0);
}

static void
_do_unlock_tbl()
{
  JASSERT(DmtcpMutexUnlock(&tblLock) == 0);
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

static DmtcpPluginDescriptor_t processInfoPlugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "processInfo",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "processInfo plugin",
  processInfo_EventHook
};


DmtcpPluginDescriptor_t
dmtcp_ProcessInfo_PluginDescr()
{
  return processInfoPlugin;
}

ProcessInfo::ProcessInfo()
{
  char buf[PATH_MAX];

  _do_lock_tbl();
  _pid = -1;
  _ppid = -1;
  _gid = -1;
  _sid = -1;
  _isRootOfProcessTree = false;
  _generation = 0;

  // _generation, above, is per-process.
  // This contrasts with DmtcpUniqueProcessId:_computation_generation, which is
  // shared among all process on a node; used in variable sharedDataHeader.
  // _generation is updated when _this_ process begins its checkpoint.
  _pthreadJoinId.clear();
  _procSelfExe = jalib::Filesystem::ResolveSymlink("/proc/self/exe");
  _uppid = UniquePid();
  JASSERT(getcwd(buf, sizeof buf) != NULL);
  _launchCWD = buf;
#ifdef CONFIG_M32
  _elfType = Elf_32;
#else // ifdef CONFIG_M32
  _elfType = Elf_64;
#endif // ifdef CONFIG_M32
  _restoreBufLen = RESTORE_TOTAL_SIZE;
  _restoreBufAddr = 0;
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
  const rlim_t eightMB = 8 * MB;

  JASSERT(getrlimit(RLIMIT_STACK, &rlim) == 0) (JASSERT_ERRNO);
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
      _vdsoStart = (unsigned long)area.addr;
      _vdsoEnd = (unsigned long)area.endAddr;
    } else if (strcmp(area.name, "[vvar]") == 0) {
      _vvarStart = (unsigned long)area.addr;
      _vvarEnd = (unsigned long)area.endAddr;
    } else if ((VA)&area >= area.addr && (VA)&area < area.endAddr) {
      JTRACE("Original stack area") ((void *)area.addr) (area.size);
      stackArea = area;
      _endOfStack = (uintptr_t) area.endAddr;
      /*
       * When using Matlab with dmtcp_launch, sometimes the bottom most
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
        JNOTE("bottom-most page of stack (page with highest address) was\n"
              "  invisible in /proc/self/maps. It is made visible again now.");
      }
    }
  }
  JASSERT(stackArea.addr != NULL);

  if (stackSize > stackArea.size + 4095) {
    // Grow the stack, if possible
    allocSize = stackSize - stackArea.size - 4095;
    tmpbuf = alloca(allocSize);
    JASSERT(tmpbuf != NULL) (JASSERT_ERRNO);
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
  if (_pid == -1) {
    // This is a brand new process.
    _pid = getpid();
    _ppid = getppid();
    _isRootOfProcessTree = true;
    _uppid = UniquePid();
    _procSelfExe = jalib::Filesystem::ResolveSymlink("/proc/self/exe");
  }

#ifdef CONFIG_M32
  _elfType = Elf_32;
#else // ifdef CONFIG_M32
  _elfType = Elf_64;
#endif // ifdef CONFIG_M32

  _vdsoStart = _vdsoEnd = _vvarStart = _vvarEnd = _endOfStack = 0;

  processRlimit();

  growStack();

  // Reserve space for restoreBuf
  updateRestoreBufAddr(nullptr, RESTORE_TOTAL_SIZE);

  if (_ckptDir.empty()) {
    updateCkptDirFileSubdir();
  }
}

void
ProcessInfo::updateRestoreBufAddr(void* addr, uint64_t len)
{
  if (_restoreBufAddr != 0) {
    JASSERT(munmap((void*) _restoreBufAddr, _restoreBufLen) == 0) (JASSERT_ERRNO);
  }

  int flags = MAP_SHARED | MAP_ANONYMOUS;

  if (addr != nullptr) {
    flags += MAP_FIXED;
  }

  _restoreBufLen = len;
  _restoreBufAddr = (uint64_t) mmap(addr, _restoreBufLen,
                    PROT_NONE, flags, -1, 0);
  JASSERT(_restoreBufAddr != (uint64_t) MAP_FAILED) (JASSERT_ERRNO);
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
  _procname = jalib::Filesystem::GetProgramName();
  _procSelfExe = jalib::Filesystem::ResolveSymlink("/proc/self/exe");
  _upid = UniquePid::ThisProcess();
  _uppid = UniquePid::ParentProcess();
  updateCkptDirFileSubdir();
}

void
ProcessInfo::resetOnFork()
{
  // Initialize the log file
  Util::initializeLogFile(SharedData::getTmpDir());

  DmtcpMutexInit(&tblLock, DMTCP_MUTEX_NORMAL);
  _ppid = _pid;
  _pid = getpid();

  _upid = UniquePid();
  _uppid = UniquePid();
  _upidStr.clear();

  _isRootOfProcessTree = false;
  _pthreadJoinId.clear();
  _ckptFileName.clear();
  _ckptFilesSubDir.clear();
  updateCkptDirFileSubdir();
}

void
ProcessInfo::restoreHeap()
{
  /* If the original start of heap is lower than the current end of heap, we
   * want to mmap the area between _savedBrk and current break. This
   * happens when the size of checkpointed program is smaller then the size of
   * mtcp_restart program.
   */
  uint64_t curBrk = (uint64_t)sbrk(0);

  if (curBrk > _savedBrk) {
    JNOTE("Area between saved_break and curr_break not mapped, mapping it now")
      (_savedBrk) (curBrk);
    size_t oldsize = _savedBrk - _savedHeapStart;
    size_t newsize = curBrk - _savedHeapStart;

    JASSERT(mremap((void *)_savedHeapStart, oldsize, newsize, 0) != NULL)
      (_savedBrk) (curBrk)
    .Text("mremap failed to map area between saved break and current break");
  } else if (curBrk < _savedBrk) {
    if (brk((void *)_savedBrk) != 0) {
      JNOTE("Failed to restore area between saved_break and curr_break.")
        (_savedBrk) (curBrk) (JASSERT_ERRNO);
    }
  }
}

void
ProcessInfo::restart()
{
  updateRestoreBufAddr((void *)_restoreBufAddr, _restoreBufLen);

  restoreHeap();

  // Update the ckptDir
  string ckptDir = jalib::Filesystem::GetDeviceName(PROTECTED_CKPT_DIR_FD);
  JASSERT(ckptDir.length() > 0);
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
        JWARNING(chdir(_ckptCWD.c_str()) == 0) (_ckptCWD) (_launchCWD)
          (JASSERT_ERRNO).Text("Failed to change directory to _ckptCWD");
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
  // Restore group assignment
  if (dmtcp_virtual_to_real_pid && dmtcp_virtual_to_real_pid(_gid) != _gid) {
    pid_t cgid = getpgid(0);

    // Group ID is known inside checkpointed processes
    if (_gid != cgid) {
      JTRACE("Restore Group Assignment")
        (_gid) (_fgid) (cgid) (_pid) (_ppid) (getppid());
      JWARNING(setpgid(0, _gid) == 0) (_gid) (JASSERT_ERRNO)
      .Text("Cannot change group information");
    } else {
      JTRACE("Group is already assigned") (_gid) (cgid);
    }
  } else {
    JTRACE("SKIP Group information, GID unknown");
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
  JASSERT(filename != NULL);
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
  JASSERT(dir != NULL);
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
  JASSERT(_pid == getpid()) (_pid) (getpid());

  _gid = getpgid(0);
  _sid = getsid(0);

  _fgid = -1;

  // Try to open the controlling terminal
  int tfd = _real_open("/dev/tty", O_RDWR);
  if (tfd != -1) {
    _fgid = tcgetpgrp(tfd);
    _real_close(tfd);
  }

  if (_ppid != getppid()) {
    // Our original parent died; we are the root of the process tree now.
    //
    // On older systems, a process is inherited by init (pid = 1) after its
    // parent dies. However, with the new per-user init process, the parent
    // pid is no longer "1"; it's the pid of the user-specific init process.
    _ppid = getppid();
    _isRootOfProcessTree = true;
    _uppid = UniquePid();
  } else {
    _uppid = UniquePid::ParentProcess();
  }

  _procname = jalib::Filesystem::GetProgramName();
  _procSelfExe = jalib::Filesystem::ResolveSymlink("/proc/self/exe");
  _hostname = jalib::Filesystem::GetCurrentHostname();
  _upid = UniquePid::ThisProcess();

  char buf[PATH_MAX];
  JASSERT(getcwd(buf, sizeof buf) != NULL);
  _ckptCWD = buf;

  JTRACE("CHECK GROUP PID")(_gid)(_fgid)(_ppid)(_pid);
}

bool
ProcessInfo::vdsoOffsetMismatch(uint64_t f1, uint64_t f2,
                                uint64_t f3, uint64_t f4)
{
  return (f1 != _clock_gettime_offset) || (f2 != _getcpu_offset) ||
         (f3 != _gettimeofday_offset) || (f4 != _time_offset);
}

// NOTE: ProcessInfo object acts as the checkpoint header for DMTCP.
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
  _savedBrk = (uint64_t) sbrk(0);
  _clock_gettime_offset = dmtcp_dlsym_lib_fnc_offset("linux-vdso",
                                                     "__vdso_clock_gettime");
  _getcpu_offset = dmtcp_dlsym_lib_fnc_offset("linux-vdso",
                                              "__vdso_getcpu");
  _gettimeofday_offset = dmtcp_dlsym_lib_fnc_offset("linux-vdso",
                                                    "__vdso_gettimeofday");
  _time_offset = dmtcp_dlsym_lib_fnc_offset("linux-vdso", "__vdso_time");

  o & _elfType;
  o & _isRootOfProcessTree & _pid & _sid & _ppid & _gid & _fgid & _generation;
  o & _procname & _procSelfExe & _hostname & _launchCWD & _ckptCWD;
  o & _upid & _uppid;
  o & _clock_gettime_offset & _getcpu_offset
    & _gettimeofday_offset & _time_offset;
  o & _compGroup & _numPeers;
  o & _restoreBufAddr & _savedHeapStart & _savedBrk;
  o & _vdsoStart & _vdsoEnd & _vvarStart & _vvarEnd & _endOfStack;
  o & _ckptDir & _ckptFileName & _ckptFilesSubDir;
  o & kvmap;

  JTRACE("Serialized process information")
    (_sid) (_ppid) (_gid) (_fgid) (_isRootOfProcessTree)
    (_procname) (_hostname) (_launchCWD) (_ckptCWD) (_upid) (_uppid)
    (_compGroup) (_numPeers) (_elfType);

  if (_isRootOfProcessTree) {
    JTRACE("This process is Root of Process Tree");
  }

  JSERIALIZE_ASSERT_POINT("EOF");
}
}
