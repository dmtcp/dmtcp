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

#include <fenv.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "util.h"
#include "dmtcp_dlsym.h"
#include "syscallwrappers.h"
#include "uniquepid.h"
#include "processinfo.h"
#include "procselfmaps.h"
#include "coordinatorapi.h"
#include  "../jalib/jconvert.h"
#include  "../jalib/jfilesystem.h"

using namespace dmtcp;

EXTERNC int dmtcp_get_max_user_fd() __attribute__((weak));

static pthread_mutex_t tblLock = PTHREAD_MUTEX_INITIALIZER;

static int roundingMode = -1;
static fenv_t envp;
static rlim_t rlim_cur_as = 0;
static rlim_t rlim_cur_core = 0;
static rlim_t rlim_cur_cpu = 0;
static rlim_t rlim_cur_data = 0;
static rlim_t rlim_cur_fsize = 0;
static rlim_t rlim_cur_nice = 0;
static rlim_t rlim_cur_nofile = 0;
static rlim_t rlim_cur_nproc = 0;
static rlim_t rlim_cur_stack = 0;

static void _do_lock_tbl()
{
  JASSERT(_real_pthread_mutex_lock(&tblLock) == 0) (JASSERT_ERRNO);
}

static void _do_unlock_tbl()
{
  JASSERT(_real_pthread_mutex_unlock(&tblLock) == 0) (JASSERT_ERRNO);
}

void dmtcp_ProcessInfo_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
    case DMTCP_EVENT_INIT:
      ProcessInfo::instance().init();
      break;

    case DMTCP_EVENT_PRE_EXEC:
      {
        jalib::JBinarySerializeWriterRaw wr("", data->serializerInfo.fd);
        ProcessInfo::instance().refresh();
        ProcessInfo::instance().serialize(wr);
      }
      break;

    case DMTCP_EVENT_POST_EXEC:
      {
        jalib::JBinarySerializeReaderRaw rd("", data->serializerInfo.fd);
        ProcessInfo::instance().serialize(rd);
        ProcessInfo::instance().postExec();
      }
      break;

    case DMTCP_EVENT_DRAIN:
      ProcessInfo::instance().refresh();
      break;

    case DMTCP_EVENT_RESTART:
      fesetenv(&envp);
      fesetround(roundingMode);

      { struct rlimit rlim = {0, 0};

#define RESTORE_RLIMIT(_RLIMIT,_rlim_cur) \
        getrlimit(_RLIMIT, &rlim); \
        JWARNING(_rlim_cur <= rlim.rlim_max) (_rlim_cur) (rlim.rlim_max) \
          .Text("Prev. soft limit of " #_RLIMIT " lowered to new hard limit"); \
        rlim.rlim_cur = _rlim_cur; \
        JASSERT(setrlimit(_RLIMIT, &rlim) == 0);

        RESTORE_RLIMIT(RLIMIT_AS, rlim_cur_as);
        RESTORE_RLIMIT(RLIMIT_CORE, rlim_cur_core);
        RESTORE_RLIMIT(RLIMIT_CPU, rlim_cur_cpu);
        RESTORE_RLIMIT(RLIMIT_DATA, rlim_cur_data);
        RESTORE_RLIMIT(RLIMIT_FSIZE, rlim_cur_fsize);
        RESTORE_RLIMIT(RLIMIT_NICE, rlim_cur_nice);
        RESTORE_RLIMIT(RLIMIT_NOFILE, rlim_cur_nofile);
        RESTORE_RLIMIT(RLIMIT_NPROC, rlim_cur_nproc);
        RESTORE_RLIMIT(RLIMIT_STACK, rlim_cur_stack);
      }
      ProcessInfo::instance().restart();
      break;

    case DMTCP_EVENT_REFILL:
      if (data->refillInfo.isRestart) {
        ProcessInfo::instance().restoreProcessGroupInfo();
      }
      break;

    case DMTCP_EVENT_THREADS_SUSPEND:
      roundingMode = fegetround();
      fegetenv(&envp);

      { struct rlimit rlim = {0, 0};
#define SAVE_RLIMIT(_RLIMIT,_rlim_cur) \
        getrlimit(_RLIMIT, &rlim); \
        _rlim_cur = rlim.rlim_cur;

        SAVE_RLIMIT(RLIMIT_AS, rlim_cur_as);
        SAVE_RLIMIT(RLIMIT_CORE, rlim_cur_core);
        SAVE_RLIMIT(RLIMIT_CPU, rlim_cur_cpu);
        SAVE_RLIMIT(RLIMIT_DATA, rlim_cur_data);
        SAVE_RLIMIT(RLIMIT_FSIZE, rlim_cur_fsize);
        SAVE_RLIMIT(RLIMIT_NICE, rlim_cur_nice);
        SAVE_RLIMIT(RLIMIT_NOFILE, rlim_cur_nofile);
        SAVE_RLIMIT(RLIMIT_NPROC, rlim_cur_nproc);
        SAVE_RLIMIT(RLIMIT_STACK, rlim_cur_stack);
      }
      break;

    case DMTCP_EVENT_THREADS_RESUME:
      if (data->refillInfo.isRestart) {
        _real_close(PROTECTED_ENVIRON_FD);
      }
      break;

    default:
      break;
  }
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
  _noCoordinator = false;
  _generation = 0;
    // _generation, above, is per-process.
    // This contrasts with DmtcpUniqueProcessId:_computation_generation, which is
    //   shared among all process on a node; used in variable sharedDataHeader.
    // _generation is updated when _this_ process begins its checkpoint.
  _childTable.clear();
  _pthreadJoinId.clear();
  _procSelfExe = jalib::Filesystem::ResolveSymlink("/proc/self/exe");
  _maxUserFd = -1;
  _uppid = UniquePid();
  JASSERT(getcwd(buf, sizeof buf) != NULL);
  _launchCWD = buf;
#ifdef CONFIG_M32
  _elfType = Elf_32;
#else
  _elfType = Elf_64;
#endif
  _restoreBufLen = RESTORE_TOTAL_SIZE;
  _restoreBufAddr = 0;
  _do_unlock_tbl();
}

static ProcessInfo *pInfo = NULL;
ProcessInfo& ProcessInfo::instance()
{
  if (pInfo == NULL) {
    pInfo = new ProcessInfo();
  }
  return *pInfo;
}

void ProcessInfo::findMiscAreas()
{
  // Find the current stack area, heap, stack, vDSO and vvar areas.
  ProcMapsArea area;
  ProcSelfMaps procSelfMaps;
  while (procSelfMaps.getNextArea(&area)) {
    if (strcmp(area.name, "[heap]") == 0) {
      // Record start of heap which will later be used to restore heap
      _savedHeapStart = (unsigned long) area.addr;
    } else if (strcmp(area.name, "[vdso]") == 0) {
      _vdsoStart = (unsigned long) area.addr;
      _vdsoEnd = (unsigned long) area.endAddr;
    } else if (strcmp(area.name, "[vvar]") == 0) {
      _vvarStart = (unsigned long) area.addr;
      _vvarEnd = (unsigned long) area.endAddr;
    } else if ((VA) &area >= area.addr && (VA) &area < area.endAddr) {
      /* Modern kernels label this '[stack]', but testing '&area' is more
       * reliable.
       */
      JLOG(DMTCP)("Original stack area") ((void*)area.addr) (area.size);
       /*
       * Record only end address (higher) of stack as it can grow downwards and
       * start address will then changed.
       */
      _stackEnd = (uintptr_t) area.endAddr;
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
      //         exercise this path.
      int ret = mprotect(area.addr + area.size, 0x1000,
                         PROT_READ | PROT_WRITE | PROT_EXEC);
      if (ret == 0) {
        JNOTE("bottom-most page of stack (page with highest address) was\n"
              "  invisible in /proc/self/maps. It is made visible again now.");
      }
    }
  }
}

void ProcessInfo::init()
{
  if (_pid == -1) {
    // This is a brand new process.
    _pid = getpid();
    _ppid = getppid();
    _isRootOfProcessTree = true;
    _uppid = UniquePid();
    _procSelfExe = jalib::Filesystem::ResolveSymlink("/proc/self/exe");
    _maxUserFd = -1;
  }

#ifdef CONFIG_M32
  _elfType = Elf_32;
#else
  _elfType = Elf_64;
#endif

  _vdsoStart = _vdsoEnd = _vvarStart = _vvarEnd = _stackEnd = 0;

  processRlimit();

  findMiscAreas();

  // Reserve space for restoreBuf
  _restoreBufLen = RESTORE_TOTAL_SIZE;

  _restoreBufAddr = (uint64_t) mmap(NULL, _restoreBufLen, PROT_NONE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  JASSERT(_restoreBufLen != (uint64_t) MAP_FAILED) (JASSERT_ERRNO);

  if (_ckptDir.empty()) {
    updateCkptDirFileSubdir();
  }
}

void ProcessInfo::processRlimit()
{
#ifdef __i386__
  // Match work begun in dmtcpPrepareForExec()
# if 0
  if (getenv("DMTCP_ADDR_COMPAT_LAYOUT")) {
    _dmtcp_unsetenv("DMTCP_ADDR_COMPAT_LAYOUT");
    // DMTCP had set ADDR_COMPAT_LAYOUT.  Now unset it.
    personality((unsigned long)personality(0xffffffff) ^ ADDR_COMPAT_LAYOUT);
    JLOG(DMTCP)("unsetting ADDR_COMPAT_LAYOUT");
  }
# else
  { char * rlim_cur_char = getenv("DMTCP_RLIMIT_STACK");
    if (rlim_cur_char != NULL) {
      struct rlimit rlim;
      getrlimit(RLIMIT_STACK, &rlim);
      rlim.rlim_cur = atol(rlim_cur_char);
      JLOG(DMTCP)("rlim_cur for RLIMIT_STACK being restored.") (rlim.rlim_cur);
      setrlimit(RLIMIT_STACK, &rlim);
      _dmtcp_unsetenv("DMTCP_RLIMIT_STACK");
    }
  }
# endif
#endif
}

void ProcessInfo::calculateArgvAndEnvSize()
{
  vector<string> args = jalib::Filesystem::GetProgramArgs();
  _argvSize = 0;
  for (size_t i = 0; i < args.size(); i++) {
    _argvSize += args[i].length() + 1;
  }

  _envSize = 0;
  if (environ != NULL) {
    char *ptr = environ[0];
    while (*ptr != '\0' && args[0].compare(ptr) != 0) {
      _envSize += strlen(ptr) + 1;
      ptr += strlen(ptr) + 1;
    }
  }
  _envSize += args[0].length();
}

void ProcessInfo::updateCkptDirFileSubdir(string newCkptDir)
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

void ProcessInfo::postExec()
{
  _procname   = jalib::Filesystem::GetProgramName();
  _procSelfExe = jalib::Filesystem::ResolveSymlink("/proc/self/exe");
  _maxUserFd = -1;
  _upid       = UniquePid::ThisProcess();
  _uppid      = UniquePid::ParentProcess();
  updateCkptDirFileSubdir();
}

void ProcessInfo::resetOnFork()
{
  pthread_mutex_t newlock = PTHREAD_MUTEX_INITIALIZER;
  tblLock = newlock;
  _ppid = _pid;
  _pid = getpid();
  _isRootOfProcessTree = false;
  _childTable.clear();
  _pthreadJoinId.clear();
  _ckptFileName.clear();
  _ckptFilesSubDir.clear();
  updateCkptDirFileSubdir();
}

void ProcessInfo::restoreHeap()
{
  /* If the original start of heap is lower than the current end of heap, we
   * want to mmap the area between _savedBrk and current break. This
   * happens when the size of checkpointed program is smaller then the size of
   * mtcp_restart program.
   */
  uint64_t curBrk = (uint64_t) sbrk(0);
  if (curBrk > _savedBrk) {
    JNOTE("Area between saved_break and curr_break not mapped, mapping it now")
      (_savedBrk) (curBrk);
    size_t oldsize = _savedBrk - _savedHeapStart;
    size_t newsize = curBrk - _savedHeapStart;

    JASSERT(mremap((void*) _savedHeapStart, oldsize, newsize, 0) != NULL)
      (_savedBrk) (curBrk)
      .Text("mremap failed to map area between saved break and current break");
  } else if (curBrk < _savedBrk) {
    if (brk((void*)_savedBrk) != 0) {
      JNOTE("Failed to restore area between saved_break and curr_break.")
        (_savedBrk) (curBrk) (JASSERT_ERRNO);
    }
  }
}

void ProcessInfo::restart()
{
  // Unmap the restore buffer and remap it with PROT_NONE. We do munmap followed
  // mmap to ensure that the kernel releases the backing physical pages.
  JASSERT(munmap((void *)_restoreBufAddr, _restoreBufLen) == 0)
    ((void *)_restoreBufAddr) (_restoreBufLen) (JASSERT_ERRNO);

  JASSERT(mmap((void*) _restoreBufAddr , _restoreBufLen, PROT_NONE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) != MAP_FAILED)
    ((void *)_restoreBufAddr) (_restoreBufLen) (JASSERT_ERRNO);

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
        JLOG(DMTCP)("Changed cwd") (_launchCWD) (_ckptCWD) (_launchCWD + rpath);
      } else {
        JWARNING(chdir(_ckptCWD.c_str()) == 0) (_ckptCWD) (_launchCWD)
          (JASSERT_ERRNO) .Text("Failed to change directory to _ckptCWD");
      }
    }
  }
}

void ProcessInfo::restoreProcessGroupInfo()
{
  // Restore group assignment
  if (dmtcp_virtual_to_real_pid && dmtcp_virtual_to_real_pid(_gid) != _gid) {
    pid_t cgid = getpgid(0);
    // Group ID is known inside checkpointed processes
    if (_gid != cgid) {
      JLOG(DMTCP)("Restore Group Assignment")
        (_gid) (_fgid) (cgid) (_pid) (_ppid) (getppid());
      JWARNING(setpgid(0, _gid) == 0) (_gid) (JASSERT_ERRNO)
        .Text("Cannot change group information");
    } else {
      JLOG(DMTCP)("Group is already assigned") (_gid) (cgid);
    }
  } else {
    JLOG(DMTCP)("SKIP Group information, GID unknown");
  }
}

void ProcessInfo::insertChild(pid_t pid, UniquePid uniquePid)
{
  _do_lock_tbl();
  iterator i = _childTable.find(pid);
  JWARNING(i == _childTable.end()) (pid) (uniquePid) (i->second)
    .Text("child pid already exists!");

  _childTable[pid] = uniquePid;
  _do_unlock_tbl();

  JLOG(DMTCP)("Creating new virtualPid -> realPid mapping.") (pid) (uniquePid);
}

void ProcessInfo::eraseChild(pid_t virtualPid)
{
  _do_lock_tbl();
  iterator i = _childTable.find(virtualPid);
  if (i != _childTable.end())
    _childTable.erase(virtualPid);
  _do_unlock_tbl();
}

bool ProcessInfo::isChild(const UniquePid& upid)
{
  bool res = false;
  _do_lock_tbl();
  for (iterator i = _childTable.begin(); i != _childTable.end(); i++) {
    if (i->second == upid) {
      res = true;
      break;
    }
  }
  _do_unlock_tbl();
  return res;
}

bool ProcessInfo::beginPthreadJoin(pthread_t thread)
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

void ProcessInfo::clearPthreadJoinState(pthread_t thread)
{
  _do_lock_tbl();
  if (_pthreadJoinId.find(thread) != _pthreadJoinId.end()) {
    _pthreadJoinId.erase(thread);
  }
  _do_unlock_tbl();
}

void ProcessInfo::endPthreadJoin(pthread_t thread)
{
  _do_lock_tbl();
  if (_pthreadJoinId.find(thread) != _pthreadJoinId.end() &&
      pthread_equal(_pthreadJoinId[thread], pthread_self())) {
    _pthreadJoinId.erase(thread);
  }
  _do_unlock_tbl();
}

void ProcessInfo::setCkptFilename(const char *filename)
{
  JASSERT(filename != NULL);
  if (filename[0] == '/') {
    _ckptDir = jalib::Filesystem::DirName(filename);
    _ckptFileName = filename;
  } else {
    _ckptFileName = _ckptDir + "/" + filename;
  }

  if (Util::strEndsWith(_ckptFileName, CKPT_FILE_SUFFIX)) {
    string ckptFileBaseName =
      _ckptFileName.substr(0, _ckptFileName.length() - CKPT_FILE_SUFFIX_LEN);
    _ckptFilesSubDir = ckptFileBaseName +CKPT_FILES_SUBDIR_SUFFIX;
  } else {
    _ckptFilesSubDir = _ckptFileName + CKPT_FILES_SUBDIR_SUFFIX;
  }
}


void ProcessInfo::setCkptDir(const char *dir)
{
  JASSERT(dir != NULL);
  _ckptDir = dir;
  _ckptFileName = _ckptDir + "/" + jalib::Filesystem::BaseName(_ckptFileName);
  _ckptFilesSubDir = _ckptDir + "/" + jalib::Filesystem::BaseName(_ckptFilesSubDir);

  JLOG(DMTCP)("setting ckptdir") (_ckptDir) (_ckptFilesSubDir);
  //JASSERT(access(_ckptDir.c_str(), X_OK|W_OK) == 0) (_ckptDir)
    //.Text("Missing execute- or write-access to checkpoint dir.");
}

void ProcessInfo::refresh()
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
  _maxUserFd = -1;
  _hostname = jalib::Filesystem::GetCurrentHostname();
  _upid = UniquePid::ThisProcess();
  _noCoordinator = dmtcp_no_coordinator();

  char buf[PATH_MAX];
  JASSERT(getcwd(buf, sizeof buf) != NULL);
  _ckptCWD = buf;

  _sessionIds.clear();
  refreshChildTable();

  JLOG(DMTCP)("CHECK GROUP PID")(_gid)(_fgid)(_ppid)(_pid);
}

void ProcessInfo::refreshChildTable()
{
  iterator i = _childTable.begin();
  while (i != _childTable.end()) {
    pid_t pid = i->first;
    iterator j = i++;
    /* Check to see if the child process is alive*/
    if (kill(pid, 0) == -1 && errno == ESRCH) {
      _childTable.erase(j);
    } else {
      _sessionIds[pid] = getsid(pid);
    }
  }
}

void ProcessInfo::updateMaxUserFd(int fd)
{
  if (fd > _maxUserFd) {
    _maxUserFd = fd;
  }
}

bool ProcessInfo::vdsoOffsetMismatch(ptrdiff_t f1, ptrdiff_t f2,
                                     ptrdiff_t f3, ptrdiff_t f4)
{
  return (f1 != _clock_gettime_offset) || (f2 != _getcpu_offset) ||
         (f3 != _gettimeofday_offset) || (f4 != _time_offset);
}

void ProcessInfo::serialize(jalib::JBinarySerializer& o)
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
  o & _procname & _procSelfExe & _hostname & _launchCWD & _ckptCWD & _upid & _uppid;
  o & _clock_gettime_offset & _getcpu_offset
    & _gettimeofday_offset & _time_offset;
  o & _compGroup & _numPeers & _noCoordinator & _argvSize & _envSize;
  o & _restoreBufAddr & _maxUserFd & _savedHeapStart & _savedBrk;
  o & _vdsoStart & _vdsoEnd & _vvarStart & _vvarEnd & _stackEnd;
  o & _ckptDir & _ckptFileName & _ckptFilesSubDir;

  JLOG(DMTCP)("Serialized process information")
    (_sid) (_ppid) (_gid) (_fgid) (_isRootOfProcessTree)
    (_procname) (_hostname) (_launchCWD) (_ckptCWD) (_upid) (_uppid)
    (_compGroup) (_numPeers) (_noCoordinator) (_argvSize) (_envSize) (_elfType);

  JASSERT(!_noCoordinator || _numPeers == 1) (_noCoordinator) (_numPeers);

  if (_isRootOfProcessTree) {
    JLOG(DMTCP)("This process is Root of Process Tree");
  }

  JLOG(DMTCP)("Serializing ChildPid Table") (_childTable.size()) (o.filename());
  o.serializeMap(_childTable);

  JSERIALIZE_ASSERT_POINT("EOF");
}
