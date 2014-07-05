/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *   This file is part of the dmtcp/src module of DMTCP (DMTCP:dmtcp/src).  *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or        *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,      *
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

#include "procmapsarea.h"

#ifndef EXTERNC
# ifdef __cplusplus
#  define EXTERNC extern "C"
# else
#  define EXTERNC
# endif
#endif

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

EXTERNC void initializeJalib();

EXTERNC int dmtcp_infiniband_enabled(void) __attribute__((weak));
EXTERNC int dmtcp_alloc_enabled(void) __attribute__((weak));
EXTERNC int dmtcp_dl_enabled(void) __attribute__((weak));
EXTERNC int dmtcp_batch_queue_enabled(void) __attribute__((weak));
EXTERNC int dmtcp_modify_env_enabled(void) __attribute__((weak));
EXTERNC int dmtcp_ptrace_enabled(void) __attribute__((weak));
EXTERNC int dmtcp_unique_ckpt_enabled(void) __attribute__((weak));


/*
 * struct MtcpRestartThreadArg
 *
 * DMTCP requires the virtualTids of the threads being created during
 *  the RESTARTING phase.  We use an MtcpRestartThreadArg structure to pass
 *  the virtualTid of the thread being created from MTCP to DMTCP.
 *
 * actual clone call: clone (fn, child_stack, flags, void *, ...)
 * new clone call   : clone (fn, child_stack, flags,
 *                           (struct MtcpRestartThreadArg *), ...)
 *
 * DMTCP automatically extracts arg from this structure and passes that
 * to the _real_clone call.
 *
 * NOTE: This structure will be moved to a more appropriate place once we have
 * finalized the code in threadlist.cpp.
 */
struct MtcpRestartThreadArg {
  void * arg;
  pid_t virtualTid;
};

#ifdef __cplusplus
#include "dmtcpalloc.h"
namespace dmtcp
{
  namespace Util
  {
    void lockFile(int fd);
    void unlockFile(int fd);
    void changeFd(int oldfd, int newfd);
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


    void writeCoordPortToFile(const char *port, const char *portFile);
    void setTmpDir(const char *tmpDir);
    string &getTmpDir();
    void initializeLogFile(dmtcp::string procname = "",
                           dmtcp::string preLogPath = "");

    void prepareDlsymWrapper();
    void adjustRlimitStack();
    void runMtcpRestore(int is32bitElf, const char* path, int fd,
                        size_t argvSize, size_t envSize);

    char readDec (int fd, VA *value);
    char readHex (int fd, VA *value);
    char readChar (int fd);
    int readProcMapsLine(int mapsfd, ProcMapsArea *area);
    int memProtToOpenFlags(int prot);
    pid_t getTracerPid(pid_t tid = -1);
    bool isPtraced();
    bool isValidFd(int fd);
    size_t pageSize();
    size_t pageMask();
    bool areZeroPages(void *addr, size_t numPages);

    char *findExecutable(char *executable, const char* path_env,
                         char *exec_path);
    string getPath(dmtcp::string cmd);
    void getDmtcpArgs(dmtcp::vector<dmtcp::string> &dmtcp_args);
  }
}
#endif

#endif
