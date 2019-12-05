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

#include "util.h"
#include <fcntl.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include "../jalib/jassert.h"
#include "../jalib/jconvert.h"
#include "../jalib/jfilesystem.h"
#include "protectedfds.h"
#include "shareddata.h"
#include "syscallwrappers.h"
#include "uniquepid.h"

using namespace dmtcp;

void
Util::setVirtualPidEnvVar(pid_t pid, pid_t virtPpid, pid_t realPpid)
{
  // We want to use setenv() only once. For all later changes, we manipulate
  // the buffer in place. This was done to avoid a bug when using Perl. Perl
  // implements its own setenv by keeping a private copy libc:environ and never
  // refers to libc:private, thus libc:setenv is outdated and calling setenv()
  // can cause segfault.
  char buf1[80];
  char buf2[80];

  memset(buf2, '#', sizeof(buf2));
  buf2[sizeof(buf2) - 1] = '\0';

  sprintf(buf1, "%d:%d:%d:", pid, virtPpid, realPpid);

  if (getenv(ENV_VAR_VIRTUAL_PID) == NULL) {
    memcpy(buf2, buf1, strlen(buf1));
    setenv(ENV_VAR_VIRTUAL_PID, buf2, 1);
  } else {
    char *envStr = (char *)getenv(ENV_VAR_VIRTUAL_PID);
    memcpy(envStr, buf1, strlen(buf1));
  }
}

// 'screen' requires directory with permissions 0700
static int
isdir0700(const char *pathname)
{
  struct stat st;

  stat(pathname, &st);
  return S_ISDIR(st.st_mode) == 1
         && (st.st_mode & 0777) == 0700
         && st.st_uid == getuid()
         && access(pathname, R_OK | W_OK | X_OK) == 0;
}

int
Util::safeMkdir(const char *pathname, mode_t mode)
{
  // If it exists and we can give it the right permissions, do it.
  chmod(pathname, 0700);
  if (isdir0700(pathname)) {
    return 0;
  }

  // else start over
  unlink(pathname);
  rmdir(pathname); // Maybe it was an empty directory
  mkdir(pathname, 0700);
  return isdir0700(pathname);
}

int
Util::safeSystem(const char *command)
{
  char *str = getenv("LD_PRELOAD");
  string dmtcphjk;

  if (str != NULL) {
    dmtcphjk = str;
  }
  unsetenv("LD_PRELOAD");
  int rc = _real_system(command);
  if (str != NULL) {
    setenv("LD_PRELOAD", dmtcphjk.c_str(), 1);
  }
  return rc;
}

int
Util::expandPathname(const char *inpath, char *const outpath, size_t size)
{
  bool success = false;

  if (*inpath == '/' || strstr(inpath, "/") != NULL) {
    strncpy(outpath, inpath, size);
    success = true;
  } else if (strStartsWith(inpath, "~/")) {
    snprintf(outpath, size, "%s%s", getenv("HOME"), &inpath[1]);
    success = true;
  } else if (strStartsWith(inpath, "~")) {
    snprintf(outpath, size, "/home/%s", &inpath[1]);
    success = true;
  } else if (strStartsWith(inpath, ".")) {
    snprintf(outpath, size, "%s/%s", jalib::Filesystem::GetCWD().c_str(),
             inpath);
    success = true;
  } else {
    char *pathVar = getenv("PATH");
    outpath[0] = '\0';
    if (pathVar == NULL) {
      pathVar = (char *)":/bin:/usr/bin";
    }
    while (*pathVar != '\0') {
      char *nextPtr;
      nextPtr = strchrnul(pathVar, ':');
      if (nextPtr == pathVar) {
        /* Two adjacent colons, or a colon at the beginning or the end
           of `PATH' means to search the current directory.  */
        strcpy(outpath, jalib::Filesystem::GetCWD().c_str());
      } else {
        strncpy(outpath, pathVar, nextPtr - pathVar);
        outpath[nextPtr - pathVar] = '\0';
      }

      JASSERT(size > strlen(outpath) + strlen(inpath) + 1)
        (size) (outpath) (strlen(outpath)) (inpath) (strlen(inpath))
      .Text("Pathname too long; Use larger buffer.");

      strcat(outpath, "/");
      strcat(outpath, inpath);

      if (*nextPtr == '\0') {
        pathVar = nextPtr;
      } else { // else *nextPtr == ':'
        pathVar = nextPtr + 1; // prepare for next iteration
      }
      if (access(outpath, X_OK) == 0) {
        success = true;
        break;
      }
    }
  }
  return success ? 0 : -1;
}

int
Util::elfType(const char *pathname, bool *isElf, bool *is32bitElf)
{
  const char *magic_elf = "\177ELF"; // Magic number for ELF
  const char *magic_elf32 = "\177ELF\001"; // Magic number for ELF 32-bit
  // Magic number for ELF 64-bit is "\177ELF\002"
  const int len = strlen(magic_elf32);
  char argv_buf[len + 1];
  char full_path[PATH_MAX];

  expandPathname(pathname, full_path, sizeof(full_path));
  int fd = _real_open(full_path, O_RDONLY, 0);
  if (fd == -1) {
    return -1;
  }
  ssize_t ret = readAll(fd, argv_buf, len);
  close(fd);
  if (ret != len) {
    return -1;
  }
  *isElf = (memcmp(magic_elf, argv_buf, strlen(magic_elf)) == 0);
  *is32bitElf = (memcmp(magic_elf32, argv_buf, strlen(magic_elf32)) == 0);
  return 0;
}

static string
ld_linux_so_path(int version, bool is32bitElf = false)
{
  char buf[80];

#if (defined(__x86_64__) || defined(__aarch64__)) && !defined(CONFIG_M32)
  if (is32bitElf) {
    sprintf(buf, "/lib/ld-linux.so.%d", version);
  } else {
    sprintf(buf, ELF_INTERPRETER);
  }
#else // if (defined(__x86_64__) || defined(__aarch64__)) &&
      // !defined(CONFIG_M32)
  sprintf(buf, ELF_INTERPRETER);
#endif // if (defined(__x86_64__) || defined(__aarch64__)) &&
       // !defined(CONFIG_M32)

  string cmd = buf;
  return cmd;
}

bool
Util::isStaticallyLinked(const char *filename)
{
  bool isElf, is32bitElf;
  char pathname[PATH_MAX];

  expandPathname(filename, pathname, sizeof(pathname));
  elfType(pathname, &isElf, &is32bitElf);

  int version = 2;
  string cmd;
  do {
    cmd = ld_linux_so_path(version, is32bitElf);
    version++;
  } while (!jalib::Filesystem::FileExists(cmd) && version < 10);

  cmd = cmd + " --verify " + pathname + " > /dev/null";

  // FIXME:  When tested on dmtcp/test/pty.c, 'ld.so -verify' returns
  // nonzero status.  Why is this?  It's dynamically linked.
  if (isElf && safeSystem(cmd.c_str())) {
    return true;
  }
  return false;
}

static string
getScreenDir()
{
  string tmpdir = string(dmtcp_get_tmpdir()) + "/" + "uscreens";

  Util::safeMkdir(tmpdir.c_str(), 0700);
  return tmpdir;
}

bool
Util::isScreen(const char *filename)
{
  return jalib::Filesystem::BaseName(filename) == "screen" &&
         isSetuid(filename);
}

// NOTE:  This routine is called only if 'screen' is setuid.
// In Ubuntu 9.10, an unprivileged 'screen' (no setuid) will ckpt and restart
// fine if SCREENDIR is set to the file $USER/tmp4 when $USER/tmp4 doesn't exist
// Arguably this is a bug in screen-4.0.  Should we take advantage of it?
void
Util::setScreenDir()
{
  if (getenv("SCREENDIR") == NULL) {
    // This will flash by, but the user will see it again on exiting screen.
    JASSERT_STDERR <<
      "*** WARNING: Environment variable SCREENDIR is not set!\n"
                   << "***  Set this to a safe location, and if restarting on\n"
                   << "***  a new host, copy your SCREENDIR directory there.\n"
                   << "***  DMTCP will use"
       " $DMTCP_TMPDIR/dmtcp-USER@HOST/uscreens for now,\n"
                   << "***  but this directory may not survive a re-boot!\n"
                   << "***      As of DMTCP-1.2.3, emacs23 not yet supported\n"
                   << "***  inside screen.  Please use emacs22 for now.  This\n"
                   << "***  will be fixed in a future version of DMTCP.\n\n";
    setenv("SCREENDIR", getScreenDir().c_str(), 1);
  } else {
    if (access(getenv("SCREENDIR"), R_OK | W_OK | X_OK) != 0) {
      JASSERT_STDERR << "*** WARNING: Environment variable SCREENDIR is set\n"
                     << "***  to directory with improper permissions.\n"
                     << "***  Please use a SCREENDIR with permission 700."
                     << "  [ SCREENDIR = " << getenv("SCREENDIR") << " ]\n"
                     << "***  Continuing anyway, and hoping for the best.\n";
    }
  }
}

bool
Util::isSetuid(const char *filename)
{
  char pathname[PATH_MAX];

  if (expandPathname(filename, pathname, sizeof(pathname)) == 0) {
    struct stat buf;
    if (stat(pathname, &buf) == 0 && (buf.st_mode & S_ISUID ||
                                      buf.st_mode & S_ISGID)) {
      return true;
    }
  }
  return false;
}

void
Util::patchArgvIfSetuid(const char *filename,
                        char *const origArgv[],
                        char ***newArgv)
{
  if (isSetuid(filename) == false) {
    return;
  }

  char realFilename[PATH_MAX];
  memset(realFilename, 0, sizeof(realFilename));
  expandPathname(filename, realFilename, sizeof(realFilename));

  // char expandedFilename[PATH_MAX];
  // expandPathname(filename, expandedFilename, sizeof (expandedFilename));
  // JASSERT(readlink(expandedFilename, realFilename, PATH_MAX - 1) != -1)
  // (filename) (expandedFilename) (realFilename) (JASSERT_ERRNO);

  size_t newArgc = 0;
  while (origArgv[newArgc] != NULL) {
    newArgc++;
  }
  newArgc += 2;
  size_t newArgvSize = newArgc * sizeof(char *);

  // IS THIS A MEMORY LEAK?  WHEN DO WE FREE buf?  - Gene
  void *buf = JALLOC_HELPER_MALLOC(newArgvSize + 2 + PATH_MAX);
  memset(buf, 0, newArgvSize + 2 + PATH_MAX);

  *newArgv = (char **)buf;
  char *newFilename = (char *)buf + newArgvSize + 1;

#define COPY_BINARY
#ifdef COPY_BINARY

  // cp /usr/bin/screen /tmp/dmtcp-USER@HOST/screen
  char cpCmdBuf[PATH_MAX * 2 + 8];

  snprintf(newFilename, PATH_MAX, "%s/%s",
           dmtcp_get_tmpdir(),
           jalib::Filesystem::BaseName(realFilename).c_str());

  snprintf(cpCmdBuf, sizeof(cpCmdBuf),
           "/bin/cp %s %s", realFilename, newFilename);

  // Remove any stale copy, just in case it's not right.
  JASSERT(unlink(newFilename) == 0 || errno == ENOENT) (newFilename);

  JASSERT(safeSystem(cpCmdBuf) == 0)(cpCmdBuf)
  .Text("call to system(cpCmdBuf) failed");

  JASSERT(access(newFilename, X_OK) == 0) (newFilename) (JASSERT_ERRNO);

  (*newArgv)[0] = newFilename;
  int i;
  for (i = 1; origArgv[i] != NULL; i++) {
    (*newArgv)[i] = (char *)origArgv[i];
  }
  (*newArgv)[i] = origArgv[i];  // copy final NULL pointer.

  return;

#else // ifdef COPY_BINARY

  // FIXME : This might fail to compile. Will fix later. -- Kapil
  // Translate: screen   to: /lib/ld-linux.so /usr/bin/screen
  // This version is more general, but has a bug on restart:
  // memory layout is altered on restart, and so brk() doesn't match.
  // Switch argvPtr from ptr to input to ptr to output now.
  *argvPtr = (char **)(cmdBuf + strlen(cmdBuf) + 1); // ... + 1 for '\0'
  // Use /lib64 if 64-bit O/S and not 32-bit app:

  char *ldStrPtr = NULL;
# if (defined(__x86_64__) || defined(__aarch64__)) && !defined(CONFIG_M32)
  bool isElf, is32bitElf;
  elfType(cmdBuf, &isElf, &is32bitElf);
  if (is32bitElf) {
    ldStrPtr = (char *)"/lib/ld-linux.so.2";
  } else {
    ldStrPtr = (char *)ELF_INTERPRETER;
  }
# else // if (defined(__x86_64__) || defined(__aarch64__)) &&
       // !defined(CONFIG_M32)
  ldStrPtr = (char *)ELF_INTERPRETER;
# endif // if (defined(__x86_64__) || defined(__aarch64__)) &&
        // !defined(CONFIG_M32)

  JASSERT(newArgv0Len > strlen(origPath) + 1)
    (newArgv0Len) (origPath) (strlen(origPath)).Text("Buffer not large enough");

  strncpy(newArgv0, origPath, newArgv0Len);

  size_t origArgvLen = 0;
  while (origArgv[origArgvLen] != NULL) {
    origArgvLen++;
  }

  JASSERT(newArgvLen >= origArgvLen + 1) (origArgvLen) (newArgvLen)
  .Text("newArgv not large enough to hold the expanded argv");

  // ISN'T THIS A BUG?  newArgv WAS DECLARED 'char ***'.
  newArgv[0] = ldStrPtr;
  newArgv[1] = newArgv0;
  for (int i = 1; origArgv[i] != NULL; i++) {
    newArgv[i + 1] = origArgv[i];
  }
  newArgv[i + 1] = origArgv[i]; // Copy final NULL pointer.
#endif // ifdef COPY_BINARY
}

void
Util::freePatchedArgv(char **newArgv)
{
  JALLOC_HELPER_FREE(*newArgv);
}

void
Util::runMtcpRestore(int is32bitElf,
                     const char *path,
                     int fd,
                     size_t argvSize,
                     size_t envSize)
{
  static string mtcprestart = Util::getPath("mtcp_restart");

  if (is32bitElf) {
    mtcprestart = Util::getPath("mtcp_restart-32", is32bitElf);
  }

  // Tell mtcp_restart process to write its debugging information to
  // PROTECTED_STDERR_FD. This way we prevent it from spitting out garbage onto
  // FD_STDERR if it is being used by the user process in a special way.
  char protected_stderr_fd_str[16];
  sprintf(protected_stderr_fd_str, "%d", PROTECTED_STDERR_FD);

  char buf[64];
  sprintf(buf, "%d", fd);

  char *newArgs[] = {
    (char *)mtcprestart.c_str(),
    (char *)"--stderr-fd",
    protected_stderr_fd_str,
    (char *)"--fd",
    buf,
    NULL
  };
  JTRACE("launching mtcp_restart --fd")(fd)(path);

  // Create the placeholder for "MTCP_OLDPERS" environment.
  // setenv("MTCP_OLDPERS_DUMMY", "XXXXXXXXXXXXXXXX", 1);
  // FIXME: Put an explanation of the logic below.   -- Kapil
#define ENV_PTR(x) ((char *)(getenv(x) - strlen(x) - 1))
  char *dummyEnviron = NULL;
  const int pathIndex = 0; // index in newEnv[]
  const int dummyEnvironIndex = 1; // index in newEnv[]
  // Eventually, newEnv = {ENV_PTR("MTCP_OLDPERS"), ENV_PTR("PATH"), NULL}
  // newEnv[2] = NULL; newEnv[3] and newEnv[4] are available so that
  // they can easily be used to modify envp inside mtcp_restart.c:main().
  // for debugging in GDB.  These appear _after_ final NULL  of newEnv[].
  char *newEnv[] = { NULL, NULL, NULL,
                      const_cast<char *>("MTCP_RESTART_PAUSE=0"),
                      const_cast<char *>("DMTCP_RESTART_PAUSE=0") };

  // Will put ENV_PTR("MTCP_OLDPERS") here.
  newEnv[dummyEnvironIndex] = (char *)dummyEnviron;
  newEnv[pathIndex] = (getenv("PATH") ? ENV_PTR("PATH") : NULL);

  size_t newArgsSize = 0;
  for (int i = 0; newArgs[i] != NULL; i++) {
    newArgsSize += strlen(newArgs[i]) + 1;
  }
  size_t newEnvSize = 0;
  for (int i = 0; newEnv[i] != NULL; i++) {
    newEnvSize += strlen(newEnv[i]) + 1;
  }
  size_t originalArgvEnvSize = argvSize + envSize;
  size_t newArgvEnvSize = newArgsSize + newEnvSize + strlen(newArgs[0]);
  size_t argvSizeDiff = originalArgvEnvSize - newArgvEnvSize;
  if (argvSizeDiff > 0) {
    dummyEnviron = (char *)malloc(argvSizeDiff);
    memset(dummyEnviron, '0', argvSizeDiff - 1);
    memcpy(dummyEnviron,
           ENV_VAR_DMTCP_DUMMY "=",
           strlen(ENV_VAR_DMTCP_DUMMY "="));
    dummyEnviron[argvSizeDiff - 1] = '\0';
    newEnv[dummyEnvironIndex] = dummyEnviron;
  }

  JTRACE("Args/Env Sizes")
    (newArgsSize) (newEnvSize) (argvSize) (envSize) (argvSizeDiff);

  execve(newArgs[0], newArgs, newEnv);
  JASSERT(false) (newArgs[0]) (newArgs[1]) (JASSERT_ERRNO)
  .Text("exec() failed");
}

void
Util::adjustRlimitStack()
{
#ifdef __i386__

  // This is needed in 32-bit Ubuntu 9.10, to fix bug with test/dmtcp5.c
  // NOTE:  Setting personality() is cleanest way to force legacy_va_layout,
  // but there's currently a bug on restart in the sequence:
  // checkpoint -> restart -> checkpoint -> restart
# if 0
  { unsigned long oldPersonality = personality(0xffffffffL);
    if (!(oldPersonality & ADDR_COMPAT_LAYOUT)) {
      // Force ADDR_COMPAT_LAYOUT for libs in high mem, to avoid vdso conflict
      personality(oldPersonality & ADDR_COMPAT_LAYOUT);
      JTRACE("setting ADDR_COMPAT_LAYOUT");
      setenv("DMTCP_ADDR_COMPAT_LAYOUT", "temporarily is set", 1);
    }
  }
# else // if 0
  { struct rlimit rlim;
    getrlimit(RLIMIT_STACK, &rlim);
    if (rlim.rlim_cur != RLIM_INFINITY) {
      char buf[100];
      sprintf(buf, "%lu", rlim.rlim_cur); // "%llu" for BSD/Mac OS
      JTRACE("setting rlim_cur for RLIMIT_STACK") (rlim.rlim_cur);
      setenv("DMTCP_RLIMIT_STACK", buf, 1);

      // Force kernel's internal compat_va_layout to 0; Force libs to high mem.
      rlim.rlim_cur = rlim.rlim_max;

      // FIXME: if rlim.rlim_cur != RLIM_INFINITY, then we should warn the user.
      setrlimit(RLIMIT_STACK, &rlim);

      // After exec, process will restore DMTCP_RLIMIT_STACK in DmtcpWorker()
    }
  }
# endif // if 0
#endif // ifdef __i386__
}

// TODO(kapil): rewrite getPath to remove dependency on jalib.
char *
Util::getPath(const char *cmd, bool is32bit)
{
  // search relative to base dir of dmtcp installation.
  const char *p1[] = {
    "/bin/",
    "/lib64/dmtcp/",
    "/lib/dmtcp/",
  };

  string suffixFor32Bits;

#if defined(__x86_64__) || defined(__aarch64__)
  if (is32bit) {  // if this is a multi-architecture build
    string basename = jalib::Filesystem::BaseName(cmd);
    if (strcmp(cmd, "mtcp_restart-32") == 0) {
      suffixFor32Bits = "32/bin/";
    } else {
      suffixFor32Bits = "32/lib/dmtcp/";
    }
  }
#endif // if defined(__x86_64__) || defined(__aarch64__)

  // Search relative to dir of this command (bin/dmtcp_launch), (using p1).
  string udir = SharedData::getInstallDir();
  string result = cmd;
  for (size_t i = 0; i < sizeof(p1) / sizeof(char *); i++) {
    string p = udir + p1[i] + suffixFor32Bits + cmd;
    if (jalib::Filesystem::FileExists(p)) {
      result = p;
      break;
    }
  }

  char *buf = (char*) JALLOC_MALLOC(result.length() + 1);
  strncpy(buf, result.c_str(), result.length() + 1);
  return buf;
}

char **
Util::getDmtcpArgs(void)
{
  const char *sigckpt = getenv(ENV_VAR_SIGCKPT);
  const char *compression = getenv(ENV_VAR_COMPRESSION);
  const char *allocPlugin = getenv(ENV_VAR_ALLOC_PLUGIN);
  const char *dlPlugin = getenv(ENV_VAR_DL_PLUGIN);

#ifdef HBICT_DELTACOMP
  const char *deltacompression = getenv(ENV_VAR_DELTACOMPRESSION);
#endif // ifdef HBICT_DELTACOMP
  const char *ckptOpenFiles = getenv(ENV_VAR_CKPT_OPEN_FILES);
  const char *ckptDir = getenv(ENV_VAR_CHECKPOINT_DIR);
  const char *tmpDir = getenv(ENV_VAR_TMPDIR);
  const char *plugins = getenv(ENV_VAR_PLUGIN);

  vector<string> argVector;
  // modify the command
  argVector.push_back("--coord-host");
  argVector.push_back(SharedData::coordHost());
  argVector.push_back("--coord-port");
  argVector.push_back(jalib::XToString(SharedData::coordPort()));

  if (jassert_quiet == 1) {
    argVector.push_back("-q");
  } else if (jassert_quiet == 2) {
    argVector.push_back("-q -q");
  }

  if (sigckpt != NULL) {
    argVector.push_back("--ckpt-signal");
    argVector.push_back(sigckpt);
  }

  if (ckptDir != NULL) {
    argVector.push_back("--ckptdir");
    argVector.push_back(ckptDir);
  }

  if (tmpDir != NULL) {
    argVector.push_back("--tmpdir");
    argVector.push_back(tmpDir);
  }

  if (ckptOpenFiles != NULL) {
    argVector.push_back("--checkpoint-open-files");
  }

  if (plugins != NULL) {
    argVector.push_back("--with-plugin");
    argVector.push_back(plugins);
  }

  if (compression != NULL) {
    if (strcmp(compression, "1") == 0) {
      argVector.push_back("--no-gzip");
    } else {
      argVector.push_back("--gzip");
    }
  }

  if (allocPlugin != NULL && strcmp(allocPlugin, "0") == 0) {
    argVector.push_back("--disable-alloc-plugin");
  }

  if (dlPlugin != NULL && strcmp(dlPlugin, "0") == 0) {
    argVector.push_back("--disable-dl-plugin");
  }

  if (dmtcp_ptrace_enabled != NULL && dmtcp_ptrace_enabled()) {
    argVector.push_back("--ptrace");
  }

  if (dmtcp_modify_env_enabled != NULL && dmtcp_modify_env_enabled()) {
    argVector.push_back("--modify-env");
  }

  if (dmtcp_infiniband_enabled != NULL && dmtcp_infiniband_enabled()) {
    argVector.push_back("--infiniband");
  }

  if (dmtcp_batch_queue_enabled != NULL && dmtcp_batch_queue_enabled()) {
    argVector.push_back("--batch-queue");
  }

#ifdef HBICT_DELTACOMP
  if (deltacompression != NULL) {
    if (strcmp(deltacompression, "0") == 0) {
      argVector.push_back("--no-hbict");
    } else {
      argVector.push_back("--hbict");
    }
  }
#endif // ifdef HBICT_DELTACOMP
  int totalBytes = 0;
  int num_args = argVector.size();
  for (size_t i = 0; i < argVector.size(); i++) {
    totalBytes += argVector[i].length() + 1;
  }

  char **args = (char**) JALLOC_MALLOC((num_args + 1) * sizeof(char*));
  JASSERT(args != NULL);

  char *buffer = (char*) JALLOC_MALLOC(totalBytes);
  JASSERT(buffer != NULL);
  memset(buffer, 0, totalBytes);

  char *ptr = buffer;
  for (size_t i = 0; i < argVector.size(); i++) {
    strcpy(ptr, argVector[i].c_str());
    args[i] = ptr;
    ptr += argVector[i].length() + 1;
  }
  args[argVector.size()] = NULL;
  return args;
}
