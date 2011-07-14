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

#include <stdlib.h>
#include <string.h>
#include <string>
#include <sstream>
#include <fcntl.h>
#include <errno.h>
#include <sys/syscall.h>
#include <linux/limits.h>
#include <dlfcn.h>
#include "constants.h"
#include "protectedfds.h"
#include  "util.h"
#include  "syscallwrappers.h"
#include  "uniquepid.h"
#include  "../jalib/jassert.h"
#include  "../jalib/jfilesystem.h"

// 'screen' requires directory with permissions 0700
static int isdir0700(const char *pathname)
{
  struct stat st;
  stat(pathname, &st);
  return (S_ISDIR(st.st_mode) == 1
          && (st.st_mode & 0777) == 0700
          && st.st_uid == getuid()
          && access(pathname, R_OK | W_OK | X_OK) == 0
         );
}

int dmtcp::Util::safeMkdir(const char *pathname, mode_t mode)
{
  // If it exists and we can give it the right permissions, do it.
  chmod(pathname, 0700);
  if (isdir0700(pathname))
    return 0;
  // else start over
  unlink(pathname);
  rmdir(pathname); // Maybe it was an empty directory
  mkdir(pathname, 0700);
  return isdir0700(pathname);
}

int dmtcp::Util::safeSystem(const char *command)
{
  char *str = getenv("LD_PRELOAD");
  dmtcp::string dmtcphjk;
  if (str != NULL)
    dmtcphjk = str;
  unsetenv("LD_PRELOAD");
  int rc = _real_system(command);
  if (str != NULL)
    setenv( "LD_PRELOAD", dmtcphjk.c_str(), 1 );
  return rc;
}

int dmtcp::Util::expandPathname(const char *inpath, char * const outpath, size_t size)
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
      pathVar = (char*) ":/bin:/usr/bin";
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
        outpath[nextPtr-pathVar] = '\0';
      }

      JASSERT(size > strlen(outpath) + strlen(inpath) + 1)
        (size) (outpath) (strlen(outpath)) (inpath) (strlen(inpath))
         .Text("Pathname too long; Use larger buffer.");

      strcat(outpath, "/");
      strcat(outpath, inpath);

      if (*nextPtr  == '\0')
        pathVar = nextPtr;
      else // else *nextPtr == ':'
        pathVar = nextPtr + 1; // prepare for next iteration
      if (access(outpath, X_OK) == 0) {
	success = true;
	break;
      }
    }
  }
  return (success ? 0 : -1);
}

int dmtcp::Util::elfType(const char *pathname, bool *isElf, bool *is32bitElf) {
  const char *magic_elf = "\177ELF"; // Magic number for ELF
  const char *magic_elf32 = "\177ELF\001"; // Magic number for ELF 32-bit
  // Magic number for ELF 64-bit is "\177ELF\002"
  const int len = strlen(magic_elf32);
  char argv_buf[len];
  char full_path[PATH_MAX];
  expandPathname(pathname, full_path, sizeof(full_path));
  int fd = _real_open(full_path, O_RDONLY, 0);
  if (fd == -1 || 5 != readAll(fd, argv_buf, 5))
    return -1;
  else
    close (fd);
  *isElf = (memcmp(magic_elf, argv_buf, strlen(magic_elf)) == 0);
  *is32bitElf = (memcmp(magic_elf32, argv_buf, strlen(magic_elf32)) == 0);
  return 0;
}

bool dmtcp::Util::isStaticallyLinked(const char *filename)
{
  bool isElf, is32bitElf;
  char pathname[PATH_MAX];
  expandPathname(filename, pathname, sizeof(pathname));
  elfType(pathname, &isElf, &is32bitElf);
#if defined(__x86_64__) && !defined(CONFIG_M32)
  dmtcp::string cmd = is32bitElf ? "/lib/ld-linux.so.2 --verify "
			         : "/lib64/ld-linux-x86-64.so.2 --verify " ;
#else
  dmtcp::string cmd = "/lib/ld-linux.so.2 --verify " ;
#endif
  cmd = cmd + pathname + " > /dev/null";
  // FIXME:  When tested on dmtcp/test/pty.c, 'ld.so -verify' returns
  // nonzero status.  Why is this?  It's dynamically linked.
  if ( isElf && safeSystem(cmd.c_str()) ) {
    return true;
  }
  return false;
}

bool dmtcp::Util::isScreen(const char *filename)
{
  return jalib::Filesystem::BaseName(filename) == "screen" &&
         isSetuid(filename);
}

dmtcp::string dmtcp::Util::getScreenDir()
{
  dmtcp::string tmpdir = dmtcp::UniquePid::getTmpDir() + "/" + "uscreens";
  safeMkdir(tmpdir.c_str(), 0700);
  return tmpdir;
}

bool dmtcp::Util::isSetuid(const char *filename)
{
  char pathname[PATH_MAX];
  if (expandPathname(filename, pathname, sizeof(pathname)) ==  0) {
    struct stat buf;
    if (stat(pathname, &buf) == 0 && (buf.st_mode & S_ISUID ||
                                      buf.st_mode & S_ISGID)) {
      return true;
    }
  }
  return false;
}

void dmtcp::Util::patchArgvIfSetuid(const char* filename, char *const origArgv[],
                             char ***newArgv)
{
  if (isSetuid(filename) == false) return;

  char realFilename[PATH_MAX];
  memset(realFilename, 0, sizeof(realFilename));
  expandPathname(filename, realFilename, sizeof (realFilename));
  //char expandedFilename[PATH_MAX];
//  expandPathname(filename, expandedFilename, sizeof (expandedFilename));
//  JASSERT (readlink(expandedFilename, realFilename, PATH_MAX - 1) != -1)
//    (filename) (expandedFilename) (realFilename) (JASSERT_ERRNO);

  size_t newArgc = 0;
  while (origArgv[newArgc] != NULL) newArgc++;
  newArgc += 2;
  size_t newArgvSize = newArgc * sizeof(char*);

  // IS THIS A MEMORY LEAK?  WHEN DO WE FREE buf?  - Gene
  void *buf = JALLOC_HELPER_MALLOC(newArgvSize + 2 + PATH_MAX);
  memset(buf, 0, newArgvSize + 2 + PATH_MAX);

  *newArgv = (char**) buf;
  char *newFilename = (char*)buf + newArgvSize + 1;

#define COPY_BINARY
#ifdef COPY_BINARY
  // cp /usr/bin/screen /tmp/dmtcp-USER@HOST/screen
  char cpCmdBuf[PATH_MAX * 2 + 8];

  snprintf(newFilename, PATH_MAX, "%s/%s",
                                  dmtcp::UniquePid::getTmpDir().c_str(),
                                  jalib::Filesystem::BaseName(realFilename).c_str());

  snprintf(cpCmdBuf, sizeof(cpCmdBuf),
           "/bin/cp %s %s", realFilename, newFilename);

  // Remove any stale copy, just in case it's not right.
  JASSERT(unlink(newFilename) == 0 || errno == ENOENT) (newFilename);

  JASSERT (safeSystem(cpCmdBuf) == 0)(cpCmdBuf)
    .Text("call to system(cpCmdBuf) failed");

  JASSERT (access(newFilename, X_OK) == 0) (newFilename) (JASSERT_ERRNO);

  (*newArgv)[0] = newFilename;
  int i;
  for (i = 1; origArgv[i] != NULL; i++)
    (*newArgv)[i] = (char*)origArgv[i];
  (*newArgv)[i] = origArgv[i];  // copy final NULL pointer.

  return;
#else
  // FIXME : This might fail to compile. Will fix later. -- Kapil
  // Translate: screen   to: /lib/ld-linux.so /usr/bin/screen
  // This version is more general, but has a bug on restart:
  //    memory layout is altered on restart, and so brk() doesn't match.
  // Switch argvPtr from ptr to input to ptr to output now.
  *argvPtr = (char **)(cmdBuf + strlen(cmdBuf) + 1); // ... + 1 for '\0'
  // Use /lib64 if 64-bit O/S and not 32-bit app:

  char *ldStrPtr = NULL;
# if defined(__x86_64__) && !defined(CONFIG_M32)
  bool isElf, is32bitElf;
  elfType(cmdBuf, &isElf, &is32bitElf);
  if (is32bitElf)
    ldStrPtr = (char *)"/lib/ld-linux.so.2";
  else
    ldStrPtr = (char *)"/lib64/ld-linux-x86-64.so.2";
# else
  ldStrPtr = (char *)"/lib/ld-linux.so.2";
# endif

  JASSERT (newArgv0Len > strlen(origPath) + 1)
    (newArgv0Len) (origPath) (strlen(origPath)) .Text ("Buffer not large enough");

  strncpy(newArgv0, origPath, newArgv0Len);

  size_t origArgvLen = 0;
  while (origArgv[origArgvLen] != NULL)
    origArgvLen++;

  JASSERT(newArgvLen >= origArgvLen + 1) (origArgvLen) (newArgvLen)
    .Text ("newArgv not large enough to hold the expanded argv");

  // ISN'T THIS A BUG?  newArgv WAS DECLARED 'char ***'.
  newArgv[0] = ldStrPtr;
  newArgv[1] = newArgv0;
  for (int i = 1; origArgv[i] != NULL; i++)
    newArgv[i+1] = origArgv[i];
  newArgv[i+1] = origArgv[i]; // Copy final NULL pointer.
#endif
  return;
}

void dmtcp::Util::freePatchedArgv(char **newArgv)
{
  JALLOC_HELPER_FREE(*newArgv);
}

// FIXME: ENV_VAR_DLSYM_OFFSET should be reset in _real_dlsym and it should be
// recalculated/reset right before returning from prepareForExec to support
// process migration (the offset might have changed after the process had
// migrated to a new machine with different ld.so.
void dmtcp::Util::prepareDlsymWrapper()
{
  /* For the sake of dlsym wrapper. We compute the address of _real_dlsym by
   * adding dlsym_offset to the address of dlopen after the exec into the user
   * application. */
  void* base_addr = NULL;
  void* dlsym_addr = NULL;
  int diff;
  void* handle = NULL;
  handle = dlopen("libdl.so", RTLD_NOW);
  if (handle == NULL) {
    fprintf(stderr, "dmtcp: get_libc_symbol: ERROR in dlopen: %s \n",
            dlerror());
    abort();
  }

  /* Earlier, we used to compute the offset of "dlsym" from "dlerror" by
   * computing the address of the two symbols using '&' operator. However, in
   * some distros (for ex. SLES 9), '&dlsym' might give the address of the
   * symbol defined in binary's PLT. Thus, to compute the correct offset, we
   * use dlopen/dlsym.
   */
  base_addr = dlsym(handle, LIBDL_BASE_FUNC_STR);
  dlsym_addr = dlsym(handle, "dlsym");
  diff = (char *)dlsym_addr - (char *)base_addr;
  char str[21] = {0};
  sprintf(str, "%d", diff);
  setenv(ENV_VAR_DLSYM_OFFSET, str, 0);
  dlclose(handle);
}

void dmtcp::Util::adjustRlimitStack()
{
#ifdef __i386__
  // This is needed in 32-bit Ubuntu 9.10, to fix bug with test/dmtcp5.c
  // NOTE:  Setting personality() is cleanest way to force legacy_va_layout,
  //   but there's currently a bug on restart in the sequence:
  //   checkpoint -> restart -> checkpoint -> restart
# if 0
  { unsigned long oldPersonality = personality(0xffffffffL);
    if ( ! (oldPersonality & ADDR_COMPAT_LAYOUT) ) {
      // Force ADDR_COMPAT_LAYOUT for libs in high mem, to avoid vdso conflict
      personality(oldPersonality & ADDR_COMPAT_LAYOUT);
      JTRACE( "setting ADDR_COMPAT_LAYOUT" );
      setenv("DMTCP_ADDR_COMPAT_LAYOUT", "temporarily is set", 1);
    }
  }
# else
  { struct rlimit rlim;
    getrlimit(RLIMIT_STACK, &rlim);
    if (rlim.rlim_cur != RLIM_INFINITY) {
      char buf[100];
      sprintf(buf, "%lu", rlim.rlim_cur); // "%llu" for BSD/Mac OS
      JTRACE( "setting rlim_cur for RLIMIT_STACK" ) ( rlim.rlim_cur );
      setenv("DMTCP_RLIMIT_STACK", buf, 1);
      // Force kernel's internal compat_va_layout to 0; Force libs to high mem.
      rlim.rlim_cur = rlim.rlim_max;
      // FIXME: if rlim.rlim_cur != RLIM_INFINITY, then we should warn the user.
      setrlimit(RLIMIT_STACK, &rlim);
      // After exec, process will restore DMTCP_RLIMIT_STACK in DmtcpWorker()
    }
  }
# endif
#endif
}
