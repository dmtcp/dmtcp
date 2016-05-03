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
#include "syscallwrappers.h"
#include "uniquepid.h"
#include "processinfo.h"
#include "procselfmaps.h"
#include "coordinatorapi.h"
#include  "../jalib/jconvert.h"
#include  "../jalib/jfilesystem.h"

namespace dmtcp {

static pthread_mutex_t tblLock = PTHREAD_MUTEX_INITIALIZER;

static int roundingMode;

static void _do_lock_tbl()
{
  JASSERT(_real_pthread_mutex_lock(&tblLock) == 0) (JASSERT_ERRNO);
}

static void _do_unlock_tbl()
{
  JASSERT(_real_pthread_mutex_unlock(&tblLock) == 0) (JASSERT_ERRNO);
}

static void checkpoint()
{
  ProcessInfo::instance().refresh();
}

static void resume()
{
  ProcessInfo::instance().incrementNumCheckpoints();
}

static void restart()
{
  ProcessInfo::instance().incrementNumRestarts();
  ProcessInfo::instance().restart();
}

static void processInfo_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
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

    default:
      break;
  }
}

static DmtcpBarrier processInfoBarriers[] = {
  {DMTCP_PRIVATE_BARRIER_PRE_CKPT, checkpoint, "checkpoint"},
  {DMTCP_PRIVATE_BARRIER_RESUME, resume, "resume"},
  {DMTCP_PRIVATE_BARRIER_RESTART, restart, "restart"}
};

static DmtcpPluginDescriptor_t processInfoPlugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "processInfo",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "processInfo plugin",
  DMTCP_DECL_BARRIERS(processInfoBarriers),
  processInfo_EventHook
};


DmtcpPluginDescriptor_t dmtcp_ProcessInfo_PluginDescr()
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
  _noCoordinator = false;
  _generation = 0;
    // _generation, above, is per-process.
    // This constrasts with DmtcpUniqueProcessId:_computation_generation, which is
    //   shared among all process on a node; used in variable sharedDataHeader.
    // _generation is updated when _this_ process begins its checkpoint.
  _childTable.clear();
  _pthreadJoinId.clear();
  _procSelfExe = jalib::Filesystem::ResolveSymlink("/proc/self/exe");
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

void ProcessInfo::growStack()
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
  ProcMapsArea stackArea = {0};
  size_t allocSize;
  void *tmpbuf;
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
      JTRACE("Original stack area") ((void*)area.addr) (area.size);
      stackArea = area;
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
  JASSERT(stackArea.addr != NULL);

  // Grow the stack
  {
    allocSize = stackSize - stackArea.size - 4095;
    tmpbuf = alloca(allocSize);
    JASSERT(tmpbuf != NULL) (JASSERT_ERRNO);
    memset(tmpbuf, 0, allocSize);
  }

#ifdef DEBUG
  {
    ProcSelfMaps maps;
    while (maps.getNextArea(&area)) {
      if ((VA)&area >= area.addr && (VA)&area < area.endAddr) { // Stack found
        JTRACE("New stack size") ((void*)area.addr) (area.size);
        break;
      }
    }
  }
#endif
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
  }

#ifdef CONFIG_M32
  _elfType = Elf_32;
#else
  _elfType = Elf_64;
#endif

  _vdsoStart = _vdsoEnd = _vvarStart = _vvarEnd = 0;

  processRlimit();

  growStack();

  // Reserve space for restoreBuf
  _restoreBufLen = RESTORE_TOTAL_SIZE;
  // Allocate two extra pages -- one at the start, one at the end -- to work as
  // guard pages for the restore area.
  void *addr =  mmap(NULL, _restoreBufLen + (2 * 4096), PROT_READ,
                     MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  JASSERT(addr != MAP_FAILED) (JASSERT_ERRNO);
  _restoreBufAddr = (uint64_t) addr + 4096;
  JASSERT(mprotect((void*)_restoreBufAddr, _restoreBufLen, PROT_NONE) == 0)
    ((void*)_restoreBufAddr) (_restoreBufLen) (JASSERT_ERRNO);

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
    JTRACE("unsetting ADDR_COMPAT_LAYOUT");
  }
# else
  { char * rlim_cur_char = getenv("DMTCP_RLIMIT_STACK");
    if (rlim_cur_char != NULL) {
      struct rlimit rlim;
      getrlimit(RLIMIT_STACK, &rlim);
      rlim.rlim_cur = atol(rlim_cur_char);
      JTRACE("rlim_cur for RLIMIT_STACK being restored.") (rlim.rlim_cur);
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

#ifdef RESTORE_ARGV_AFTER_RESTART
void Process:Info::restoreArgvAfterRestart(char* mtcpRestoreArgvStartAddr)
{
  /*
   * The addresses where argv of mtcp_restart process starts. /proc/PID/cmdline
   * information is looked up from these addresses.  We observed that the
   * stack base for mtcp_restart is always 0x7ffffffff000 in 64-bit system and
   * 0xc0000000 in case of 32-bit systems.  Once we restore the checkpointed
   * process's memory, we will map the pages ending in these address into the
   * process's memory if they are unused i.e. not mapped by the process (which
   * is true for most processes running with ASLR).  Once we map them, we can
   * put the argv of the checkpointed process in there so that
   * /proc/self/cmdline shows the correct values.
   * Note that if compiled in 32-bit mode '-m32', the stack base address
   * is in still a different location, and so this logic is not valid.
   */
  JASSERT(mtcpRestoreArgvStartAddr != NULL);

  long page_size = sysconf(_SC_PAGESIZE);
  long page_mask = ~(page_size - 1);
  char *startAddr = (char*) ((unsigned long) mtcpRestoreArgvStartAddr & page_mask);

  size_t len;
  len = (ProcessInfo::instance().argvSize() + page_size) & page_mask;

  // Check to verify if any page in the given range is already mmap()'d.
  // It assumes that the given addresses may belong to stack only, and if
  // mapped, will have read+write permissions.
  for (size_t i = 0; i < len; i += page_size) {
    int ret = mprotect ((char*) startAddr + i, page_size,
                        PROT_READ | PROT_WRITE);
    if (ret != -1 || errno != ENOMEM) {
      _mtcpRestoreArgvStartAddr = NULL;
      return;
    }
  }

  //None of the pages are mapped -- it is safe to mmap() them
  void *retAddr = mmap((void*) startAddr, len, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
  if (retAddr != MAP_FAILED) {
    JTRACE("Restoring /proc/self/cmdline")
      (mtcpRestoreArgvStartAddr) (startAddr) (len) (JASSERT_ERRNO) ;
    vector<string> args = jalib::Filesystem::GetProgramArgs();
    char *addr = mtcpRestoreArgvStartAddr;
    // Do NOT change restarted process's /proc/self/cmdline.
    //args[0] = DMTCP_PRGNAME_PREFIX + args[0];
    for ( size_t i=0; i< args.size(); ++i ) {
      if (addr + args[i].length() >= startAddr + len)
        break;
      strcpy(addr, args[i].c_str());
      addr += args[i].length() + 1;
    }
    _mtcpRestoreArgvStartAddr = startAddr;
  } else {
    JTRACE("Unable to restore /proc/self/cmdline") (startAddr) (len) (JASSERT_ERRNO) ;
    _mtcpRestoreArgvStartAddr = NULL;
  }
  return;
}

static char *_mtcpRestoreArgvStartAddr = NULL;
// FIXME(kapil): Call this after restart has finished.
static void unmapRestoreArgv()
{
  long page_size = sysconf(_SC_PAGESIZE);
  long page_mask = ~(page_size - 1);
  if (_mtcpRestoreArgvStartAddr != NULL) {
    JTRACE("Unmapping previously mmap()'d pages (that were mmap()'d for restoring argv");
    size_t len;
    len = (ProcessInfo::instance().argvSize() + page_size) & page_mask;
    JASSERT(_real_munmap(_mtcpRestoreArgvStartAddr, len) == 0)
      (_mtcpRestoreArgvStartAddr) (len)
      .Text ("Failed to munmap extra pages that were mapped during restart");
  }
}
#endif

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
  fesetround(roundingMode);
  JASSERT(mprotect((void*)_restoreBufAddr, _restoreBufLen, PROT_NONE) == 0)
    ((void*)_restoreBufAddr) (_restoreBufLen) (JASSERT_ERRNO);

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
          (JASSERT_ERRNO) .Text("Failed to change directory to _ckptCWD");
      }
    }
  }

#ifdef RESTORE_ARGV_AFTER_RESTART
  restoreArgvAfterRestart(char* mtcpRestoreArgvStartAddr);
#endif

  restoreProcessGroupInfo();
  _real_close(PROTECTED_ENVIRON_FD);
}

void ProcessInfo::restoreProcessGroupInfo()
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

void ProcessInfo::insertChild(pid_t pid, UniquePid uniquePid)
{
  _do_lock_tbl();
  iterator i = _childTable.find(pid);
  JWARNING(i == _childTable.end()) (pid) (uniquePid) (i->second)
    .Text("child pid already exists!");

  _childTable[pid] = uniquePid;
  _do_unlock_tbl();

  JTRACE("Creating new virtualPid -> realPid mapping.") (pid) (uniquePid);
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

  JTRACE("setting ckptdir") (_ckptDir) (_ckptFilesSubDir);
  //JASSERT(access(_ckptDir.c_str(), X_OK|W_OK) == 0) (_ckptDir)
    //.Text("Missing execute- or write-access to checkpoint dir.");
}

void ProcessInfo::refresh()
{
  JASSERT(_pid == getpid()) (_pid) (getpid());

  roundingMode = fegetround();

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
  _hostname = jalib::Filesystem::GetCurrentHostname();
  _upid = UniquePid::ThisProcess();
  _noCoordinator = dmtcp_no_coordinator();

  char buf[PATH_MAX];
  JASSERT(getcwd(buf, sizeof buf) != NULL);
  _ckptCWD = buf;

  _sessionIds.clear();
  refreshChildTable();

  JTRACE("CHECK GROUP PID")(_gid)(_fgid)(_ppid)(_pid);
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

void ProcessInfo::serialize(jalib::JBinarySerializer& o)
{
  JSERIALIZE_ASSERT_POINT("ProcessInfo:");
  _savedBrk = (uint64_t) sbrk(0);

  o & _elfType;
  o & _isRootOfProcessTree & _pid & _sid & _ppid & _gid & _fgid & _generation;
  o & _procname & _hostname & _launchCWD & _ckptCWD & _upid & _uppid;
  o & _compGroup & _numPeers & _noCoordinator & _argvSize & _envSize;
  o & _numChildCoordinators;
  o & _restoreBufAddr & _savedHeapStart & _savedBrk;
  o & _vdsoStart & _vdsoEnd & _vvarStart & _vvarEnd;
  o & _ckptDir & _ckptFileName & _ckptFilesSubDir;

  JTRACE("Serialized process information")
    (_sid) (_ppid) (_gid) (_fgid) (_isRootOfProcessTree)
    (_procname) (_hostname) (_launchCWD) (_ckptCWD) (_upid) (_uppid)
    (_compGroup) (_numPeers) (_noCoordinator) (_argvSize) (_envSize) (_elfType)
    (_numChildCoordinators);

  JASSERT(!_noCoordinator || _numPeers == 1) (_noCoordinator) (_numPeers);

  if (_isRootOfProcessTree) {
    JTRACE("This process is Root of Process Tree");
  }

  JTRACE("Serializing ChildPid Table") (_childTable.size()) (o.filename());
  o.serializeMap(_childTable);

  JSERIALIZE_ASSERT_POINT("EOF");
}
}
