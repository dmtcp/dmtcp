/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#ifndef UTIL_H
#define UTIL_H

#include "dmtcpalloc.h"

typedef char * VA;
#define UTIL_MAX_PATH_LEN 256

#define TIMESPEC_CMP(a, b, CMP) 					      \
  (((a)->tv_sec == (b)->tv_sec) ? 					      \
   ((a)->tv_nsec CMP (b)->tv_nsec) : 					      \
   ((a)->tv_sec CMP (b)->tv_sec))

#define TIMESPEC_ADD(a, b, result)					      \
  do {									      \
    (result)->tv_sec = (a)->tv_sec + (b)->tv_sec;			      \
    (result)->tv_nsec = (a)->tv_nsec + (b)->tv_nsec;			      \
    if ((result)->tv_nsec >= 1000 * 1000 * 1000) {			      \
	++(result)->tv_sec;						      \
	(result)->tv_nsec -= 1000 * 1000 * 1000;			      \
    }									      \
  } while (0)

#define TIMESPEC_SUB(a, b, result)					      \
  do {									      \
    (result)->tv_sec = (a)->tv_sec - (b)->tv_sec;			      \
    (result)->tv_nsec = (a)->tv_nsec - (b)->tv_nsec;			      \
    if ((result)->tv_nsec < 0) {					      \
      --(result)->tv_sec;						      \
      (result)->tv_nsec += 1000 * 1000 * 1000;				      \
    }									      \
  } while (0)

#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) < (b) ? (a) : (b))

#define CEIL(a,b) ((a)%(b) ? ((a) + (b) - ((a)%(b))) : (a))

#if defined(__i386__) || defined(__x86_64__)
# if defined(__i386__) && defined(__PIC__)
// FIXME:  After DMTCP-1.2.5, this can be made only case for i386/x86_64
#  define RMB asm volatile ("lfence" \
                           : : : "eax", "ecx", "edx", "memory")
#  define WMB asm volatile ("sfence" \
                           : : : "eax", "ecx", "edx", "memory")
#  define IMB
# else
#  define RMB asm volatile ("xorl %%eax,%%eax ; cpuid" \
                           : : : "eax", "ebx", "ecx", "edx", "memory")
#  define WMB asm volatile ("xorl %%eax,%%eax ; cpuid" \
                           : : : "eax", "ebx", "ecx", "edx", "memory")
#  define IMB
# endif
#elif defined(__arm__)
# define RMB asm volatile ("dsb ; dmb" : : : "memory")
# define WMB asm volatile ("dsb ; dmb" : : : "memory")
# define IMB asm volatile ("isb" : : : "memory")
#else
# error "instruction architecture not implemented"
#endif

extern "C" void initializeJalib();
namespace dmtcp
{
  namespace Util
  {
    typedef struct ProcMapsArea {
      char *addr;   // args required for mmap to restore memory area
      char *endAddr;   // args required for mmap to restore memory area
      size_t size;
      off_t filesize;
      int prot;
      int flags;
      off_t offset;
      dev_t devnum;
      ino_t inodenum;
      char name[UTIL_MAX_PATH_LEN];
    } ProcMapsArea;

    void lockFile(int fd);
    void unlockFile(int fd);
    void dupFds(int oldfd, const dmtcp::vector<int>& newfds);

    bool strStartsWith(const char *str, const char *pattern);
    bool strEndsWith(const char *str, const char *pattern);
    bool strStartsWith(const dmtcp::string& str, const char *pattern);
    bool strEndsWith(const dmtcp::string& str, const char *pattern);

    ssize_t writeAll(int fd, const void *buf, size_t count);
    ssize_t readAll(int fd, void *buf, size_t count);
    ssize_t skipBytes(int fd, size_t count);

    int safeMkdir(const char *pathname, mode_t mode);
    int safeSystem(const char *command);

    int expandPathname(const char *inpath, char * const outpath, size_t size);
    int elfType(const char *pathname, bool *isElf, bool *is32bitElf);

    bool isStaticallyLinked(const char *filename);

    void setVirtualPidEnvVar(pid_t pid, pid_t ppid);
    bool isScreen(const char *filename);
    void setScreenDir();
    dmtcp::string getScreenDir();
    bool isSetuid(const char *filename);
    void freePatchedArgv(char **newArgv);
    void patchArgvIfSetuid(const char* filename, char *const origArgv[],
                           char **newArgv[]);

    int readLine(int fd, char *buf, int count);

    void initializeLogFile(dmtcp::string procname = "",
                           dmtcp::string preLogPath = "");

    void prepareDlsymWrapper();
    void adjustRlimitStack();
    void runMtcpRestore(const char* path, int fd,
                        size_t argvSize, size_t envSize);

    char readDec (int fd, VA *value);
    char readHex (int fd, VA *value);
    char readChar (int fd);
    int readProcMapsLine(int mapsfd, dmtcp::Util::ProcMapsArea *area);
    int memProtToOpenFlags(int prot);
    pid_t getTracerPid(pid_t tid = -1);
    bool isPtraced();
    bool isValidFd(int fd);
    size_t pageSize();
    size_t pageMask();
    bool areZeroPages(void *addr, size_t numPages);

    string ckptCmdPath();
    void getDmtcpArgs(dmtcp::vector<dmtcp::string> &dmtcp_args);
  }
}

#endif
