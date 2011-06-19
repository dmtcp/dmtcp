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

int Util::safeMkdir(const char *pathname, mode_t mode)
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

int Util::safeSystem(const char *command)
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

int Util::expandPathname(const char *inpath, char * const outpath, size_t size)
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

int Util::elfType(const char *pathname, bool *isElf, bool *is32bitElf) {
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

bool Util::isStaticallyLinked(const char *filename)
{
  bool isElf, is32bitElf;
  char pathname[PATH_MAX];
  Util::expandPathname(filename, pathname, sizeof(pathname));
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

bool Util::isScreen(const char *filename)
{
  return jalib::Filesystem::BaseName(filename) == "screen" &&
         isSetuid(filename);
}

dmtcp::string Util::getScreenDir()
{
  dmtcp::string tmpdir = dmtcp::UniquePid::getTmpDir() + "/" + "uscreens";
  safeMkdir(tmpdir.c_str(), 0700);
  return tmpdir;
}

bool Util::isSetuid(const char *filename)
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

void Util::patchArgvIfSetuid(const char* filename, char *const origArgv[],
                             char ***newArgv)
{
  if (isSetuid(filename) == false) return;

  //sleep(20);

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
  (*newArgv)[i] = NULL;

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

  newArgv[0] = ldStrPtr;
  newArgv[1] = newArgv0;
  for (int i = 1; origArgv[i] != NULL; i++)
    newArgv[i+1] = origArgv[i];
#endif
  return;
}

void Util::freePatchedArgv(char **newArgv)
{
  JALLOC_HELPER_FREE(*newArgv);
}

void Util::initializeLogFile(dmtcp::string procname, dmtcp::string prevLogPath)
{
  dmtcp::UniquePid::ThisProcess(true);
#ifdef DEBUG
  // Initialize JASSERT library here
  dmtcp::ostringstream o;
  o << dmtcp::UniquePid::getTmpDir() << "/jassertlog."
    << dmtcp::UniquePid::ThisProcess()
    << "_";
  if (procname.empty()) {
    o << jalib::Filesystem::GetProgramName();
  } else {
    o << procname;
  }

  JASSERT_INIT(o.str());

  dmtcp::ostringstream a;
  a << "\n========================================";
  a << "\nThis Process: " << dmtcp::UniquePid::ThisProcess()
    << "\nParent Process: " << dmtcp::UniquePid::ParentProcess();

  if (!prevLogPath.empty()) {
    a << "\nPrev JAssertLog path: " << prevLogPath;
  }

  a << "\nArgv: ";
  dmtcp::vector<dmtcp::string> args = jalib::Filesystem::GetProgramArgs();
  int i;
  for (i = 0; i < args.size(); i++) {
    a << " " << args[i];
  }

  a << "\nEnvirnoment: ";
  for (i = 0; environ[i] != NULL; i++) {
    a << " " << environ[i] << ";";
  }
  a << "\n========================================\n";

  JASSERT_SET_CONSOLE_FD(-1);
  JTRACE("Process Information") (a.str());
  JASSERT_SET_CONSOLE_FD(PROTECTED_STDERR_FD);
#endif
  if (getenv(ENV_VAR_QUIET)) {
    jassert_quiet = *getenv(ENV_VAR_QUIET) - '0';
  } else {
    jassert_quiet = 0;
  }
}
