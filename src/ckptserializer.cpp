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
 *  License along with DMTCP.  If not, see <http://www.gnu.org/licenses/>.  *
 ****************************************************************************/

#include <limits.h> /* for LONG_MIN and LONG_MAX */
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#if defined(__aarch64__) || defined(__riscv)

/* On aarch64 and riscv, fork() is not implemented, in favor of clone().
 *   A true fork call would include CLONE_CHILD_SETTID and set the thread id
 * in the thread area of the child (using set_thread_area).  We don't do that.
 */
# define _real_sys_fork()                                            \
  _real_syscall(SYS_clone,                                           \
                CLONE_CHILD_CLEARTID | CLONE_CHILD_SETTID | SIGCHLD, \
                NULL,                                                \
                NULL,                                                \
                NULL)
#else  // ifdef __aarch64__
# define _real_sys_fork() _real_syscall(SYS_fork)
#endif  // ifdef __aarch64__
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include "ckptserializer.h"
#include "constants.h"
#include "dmtcp.h"
#include "protectedfds.h"
#include "syscallwrappers.h"
#include "util.h"

// aarch64 and riscv don't define SYS_pipe kernel call by default.
#if defined(__aarch64__) || defined(__riscv)
# define _real_pipe(a)         _real_syscall(SYS_pipe2, a, 0)
#else  // if defined(__aarch64__)
# define _real_pipe(a)         _real_syscall(SYS_pipe, a)
#endif  // if defined(__aarch64__)
#define _real_waitpid(a, b, c) _real_syscall(SYS_wait4, a, b, c, NULL)

using namespace dmtcp;

// FIXME: Can remove if this logic is in writeckpt.cpp
#define FORKED_CKPT_FAILED 0
#define FORKED_CKPT_PARENT 1
#define FORKED_CKPT_CHILD  2

static int forked_ckpt_status = -1;
static pid_t ckpt_extcomp_child_pid = -1;
static struct sigaction saved_sigchld_action;
static int open_ckpt_to_write(int fd, int pipe_fds[2], char **extcomp_args);
// FIXME:  Add these next two to "procmapsarea.h", instead.
enum memory_area_type {AREA_SHARED, AREA_NOTSHARED, AREA_END, AREA_ALL};
void mtcp_writememoryareas(int fd, memory_area_type type)
  __attribute__((weak));

/* We handle SIGCHLD while checkpointing. */
static void
default_sigchld_handler(int sig)
{
  JASSERT(sig == SIGCHLD);
}

static void
prepare_sigchld_handler()
{
  /* 3a. Set SIGCHLD to our own handler;
   *     User handling is restored after gzip finishes.
   * SEE: sigaction(2), NOTES: only portable method of avoiding zombies
   *      is to catch SIGCHLD signal and perform a wait(2) or similar.
   *
   * NOTE: Although the default action for SIGCHLD is supposedly SIG_IGN,
   * for historical reasons there are differences between SIG_DFL and SIG_IGN
   * for SIGCHLD.  See sigaction(2), NOTES section for more details. */
  struct sigaction default_sigchld_action;

  memset(&default_sigchld_action, 0, sizeof(default_sigchld_action));
  default_sigchld_action.sa_handler = default_sigchld_handler;
  sigaction(SIGCHLD, &default_sigchld_action, &saved_sigchld_action);
}

void
restore_sigchld_handler_and_wait_for_zombie(pid_t pid)
{
  /* This is done to avoid calling the user SIGCHLD handler when gzip
   * or another compression utility exits.
   * NOTE: We must wait in case user did rapid ckpt-kill in succession.
   *  Otherwise, kernel could have optimization allowing us to close fd and
   *  rename tmp ckpt file to permanent even while gzip is still writing.
   */

  /* All signals, incl. SIGCHLD, were blocked in
   * threadlist.cpp:checkpointhread().  A SIGCHLD handler was declared before
   * the gzip child process was forked.  The child may have already
   * terminated and returned, creating a blocked SIGCHLD.
   *   So, we use sigsuspend so that we can clean up the child zombie
   * with wait4 whether it already terminated or not yet.
   */
  sigset_t suspend_sigset;

  sigfillset(&suspend_sigset);
  sigdelset(&suspend_sigset, SIGCHLD);
  _real_sigsuspend(&suspend_sigset);
  JWARNING(_real_waitpid(pid, NULL, 0) != -1) (pid) (JASSERT_ERRNO);
  pid = -1;
  sigaction(SIGCHLD, &saved_sigchld_action, NULL);
}

/*
 * This function returns the fd to which the checkpoint file should be written.
 * The purpose of using this function over open() is that this
 * function will handle compression and gzipping.
 */
static int
test_use_compression(char *compressor, char *command, char *path, int def)
{
  char *default_val;
  char evar[256] = "DMTCP_";
  char *do_we_compress;

  if (def) {
    default_val = const_cast<char *>("1");
  } else {
    default_val = const_cast<char *>("0");
  }

  JASSERT(strlen(strncat(evar, compressor, sizeof(evar) - strlen(evar) - 1))
          < sizeof(evar) - 1)
    (compressor).Text("compressor is too long.");
  do_we_compress = getenv(evar);

  // env var is unset, let's default to enabled
  // to disable compression, run with MTCP_GZIP=0
  if (do_we_compress == NULL) {
    do_we_compress = default_val;
  }

  char *endptr;
  errno = 0;
  long int rc = strtol(do_we_compress, &endptr, 0);
  JASSERT(rc != LONG_MIN && rc != LONG_MAX) (do_we_compress) (JASSERT_ERRNO);
  if (*do_we_compress == '\0' || *endptr != '\0') {
    JWARNING(false) (evar) (do_we_compress)
    .Text("Compression env var not defined as a number."
          "  Checkpoint image will not be compressed.");
    do_we_compress = const_cast<char *>("0");
  }

  if (0 == strcmp(do_we_compress, "0")) {
    return 0;
  }

  /* Check if the executable exists. */
  if (Util::findExecutable(command, getenv("PATH"), path) == NULL) {
    JWARNING(false) (command)
    .Text("Command cannot be executed. Compression will not be used.");
    return 0;
  }

  /* If we arrive down here, it's safe to compress. */
  return 1;
}

static int
open_ckpt_to_write_gz(int fd, int pipe_fds[2], char *gzip_path)
{
  char *gzip_args[] = {
    const_cast<char *>("gzip"),
    const_cast<char *>("-1"),
    const_cast<char *>("-"),
    NULL
  };

  gzip_args[0] = gzip_path;
  JTRACE("open_ckpt_to_write_gz\n");

  return open_ckpt_to_write(fd, pipe_fds, gzip_args);
}

static int
perform_open_ckpt_image_fd(const char *tempCkptFilename,
                           bool *use_compression,
                           int *fdCkptFileOnDisk)
{
  *use_compression = false;  /* default value */

  /* 1. Open fd to checkpoint image on disk */
  /* Create temp checkpoint file and write magic number to it */
  int flags = O_CREAT | O_TRUNC | O_WRONLY;
  int fd = _real_open(tempCkptFilename, flags, 0600);
  *fdCkptFileOnDisk = fd; /* if use_compression, fd will be reset to pipe */
  JASSERT(fd != -1) (tempCkptFilename) (JASSERT_ERRNO)
  .Text("Error creating file.");

#ifdef FAST_RST_VIA_MMAP
  return fd;
#endif  // ifdef FAST_RST_VIA_MMAP

  /* 2. Test if using GZIP compression */
  int use_gzip_compression = 0;
  int use_deltacompression = 0;
  char *gzip_cmd = const_cast<char *>("gzip");
  char gzip_path[PATH_MAX];
  use_gzip_compression = test_use_compression(const_cast<char *>("GZIP"),
                                              gzip_cmd, gzip_path, 1);

  /* 3. We now have the information to pipe to gzip, or directly to fd.
  *     We do it this way, so that gzip will be direct child of forked process
  *       when using forked checkpointing.
  */

  if (use_deltacompression || use_gzip_compression) { /* fork compr. process */
    /* 3a. Set SIGCHLD to our own handler;
     *     User handling is restored after gzip finishes.
     */
    prepare_sigchld_handler();

    /* 3b. Open pipe */
    int pipe_fds[2];
    if (_real_pipe(pipe_fds) == -1) {
      JWARNING(false).Text("Error creating pipe. Compression won't be used.");
      use_gzip_compression = use_deltacompression = 0;
    }

    /* 3c. Fork compressor child */
    if (use_gzip_compression) {/* fork a gzip process */
      *use_compression = true;
      fd = open_ckpt_to_write_gz(fd, pipe_fds, gzip_path);
      if (pipe_fds[0] == -1) {
        /* If open_ckpt_to_write_gz() failed to fork the gzip process */
        *use_compression = false;
      }
    } else {
      JASSERT(false).Text("Not Reached!\n");
    }
  }

  return fd;
}

// FIXME: DELETE THIS: Moved to writeckpt.cpp
int
test_and_fork_if_forked_ckpt()
{
#ifdef TEST_FORKED_CHECKPOINTING
  return 1;
#endif  // ifdef TEST_FORKED_CHECKPOINTING

  if (getenv(ENV_VAR_FORKED_CKPT) == NULL) {
    return 0;
  }

  /* Set SIGCHLD to our own handler;
   *     User handling is restored after forking child process.
   */
  prepare_sigchld_handler();

  pid_t forked_cpid = _real_sys_fork();
  if (forked_cpid == -1) {
    JWARNING(false)
    .Text("Failed to do forked checkpointing, trying normal checkpoint");
    return FORKED_CKPT_FAILED;
  } else if (forked_cpid > 0) {
    restore_sigchld_handler_and_wait_for_zombie(forked_cpid);
    JTRACE("checkpoint complete\n");
    return FORKED_CKPT_PARENT;
  } else {
    pid_t grandchild_pid = _real_sys_fork();
    JWARNING(grandchild_pid != -1)
      .Text("WARNING: Forked checkpoint failed, no checkpoint available");
    if (grandchild_pid > 0) {
      // Uses rename() syscall, which doesn't change i-nodes.
      // So, gzip process can continue to write to file even after renaming.
      JASSERT(rename(ProcessInfo::instance().getTempCkptFilename().c_str(),
            ProcessInfo::instance().getCkptFilename().c_str()) == 0);
      // Use _exit() instead of exit() to avoid popping atexit() handlers
      // registered by the parent process.
      _exit(0); /* child exits */
    }

    /* grandchild continues; no need now to waitpid() on grandchild */
    JTRACE("inside grandchild process");
  }
  return FORKED_CKPT_CHILD;
}

int
open_ckpt_to_write(int fd, int pipe_fds[2], char **extcomp_args)
{
  pid_t cpid;

// FIXME:  We should create a grandchild and let the parent exit.
//         Then we don't have to worry about Zombie processes
//           SIGCHILD_HANDLER, and waiting for the child.
//         When the grandchild reads the end of the pipe to write areas,
//           it sees EOF and exits.
  cpid = _real_sys_fork();
  if (cpid == -1) {
    JWARNING(false) (extcomp_args[0]) (JASSERT_ERRNO)
    .Text("WARNING: error forking child process. Compression won't be used");
    _real_close(pipe_fds[0]);
    _real_close(pipe_fds[1]);
    pipe_fds[0] = pipe_fds[1] = -1;

    // fall through to return fd
  } else if (cpid > 0) { /* parent process */
    // Before running gzip in child process, we must not use LD_PRELOAD.
    // See revision log 342 for details concerning bash.
    ckpt_extcomp_child_pid = cpid;
    JWARNING(_real_close(pipe_fds[0]) == 0) (JASSERT_ERRNO)
    .Text("WARNING: close failed");
    fd = pipe_fds[1];  // change return value
  } else { /* child process */
    // static int (*libc_unsetenv) (const char *name);
    // static int (*libc_execvp) (const char *path, char *const argv[]);

    _real_close(pipe_fds[1]);
    int infd = _real_dup(pipe_fds[0]);
    int outfd = _real_dup(fd);
    _real_dup2(infd, STDIN_FILENO);
    _real_dup2(outfd, STDOUT_FILENO);

    // No need to close the other FDS
    if (pipe_fds[0] > STDERR_FILENO) {
      _real_close(pipe_fds[0]);
    }
    if (infd > STDERR_FILENO) {
      _real_close(infd);
    }
    if (outfd > STDERR_FILENO) {
      _real_close(outfd);
    }
    if (fd > STDERR_FILENO) {
      _real_close(fd);
    }

    // Don't load libdmtcp.so, etc. in exec.
    unsetenv("LD_PRELOAD");  // If in bash, this is bash env. var. version
    char *ld_preload_str = (char *)getenv("LD_PRELOAD");
    if (ld_preload_str != NULL) {
      ld_preload_str[0] = '\0';
    }

    _real_execvp(extcomp_args[0], extcomp_args);

    /* should not arrive here */
    JASSERT(false)
    .Text("Compression failed! No checkpointing will be performed!"
          " Cancel now!");
  }

  return fd;
}

void
CkptSerializer::createCkptDir()
{
  string ckptDir = ProcessInfo::instance().getCkptDir();

  JASSERT(!ckptDir.empty());
  JASSERT(mkdir(ckptDir.c_str(), S_IRWXU) == 0 || errno == EEXIST)
    (JASSERT_ERRNO) (ckptDir)
  .Text("Error creating checkpoint directory");

  JASSERT(0 == access(ckptDir.c_str(), X_OK | W_OK)) (ckptDir)
  .Text("ERROR: Missing execute- or write-access to checkpoint dir");
}

// Ckpt file may be gzipped, and re read it from a pipe.  We can't lseek on a
// pipe.  So, we write the DmtcpCkptHdr twice.  The first header is read by
// 'dmtcp_restart'.  And then 'dmtcp_restart' execs into 'mtcp_restart'.
// 'mtcp_restart inherits the fd, and then reads the second copy of
// DmtcpCkptHdr.
void
CkptSerializer::writeCkptImage(DmtcpCkptHeader ckptHdr,
                               const string& ckptFilename)
{
  JTRACE("Thread performing checkpoint.");
  createCkptDir();

  /* fd will either point to the ckpt file to write, or else the write end
   * of a pipe leading to a compression child process.
   */
  bool use_compression = false;
  int fdCkptFileOnDisk = -1;
  int fd = -1;

  fd = perform_open_ckpt_image_fd(ckptFilename.c_str(), &use_compression,
                                  &fdCkptFileOnDisk);
  JASSERT(fdCkptFileOnDisk >= 0);
  JASSERT(use_compression || fd == fdCkptFileOnDisk);

  // Write ckpt header twice. It's read once by dmtcp_restart and again by
  // mtcp_restart.
  JASSERT(Util::writeAll(fd, &ckptHdr, sizeof(ckptHdr)) == sizeof(ckptHdr));
  JASSERT(Util::writeAll(fd, &ckptHdr, sizeof(ckptHdr)) == sizeof(ckptHdr));

// FIXME:  Write any shared memory areas first, and then fork grandchild.
//         To do this, add a second argument to  mtcp_writememoryareas()
//           to write shared memory areas (and then fork child)
//           and then again, to write non-shared memory areas.
//           Maybe to write all memory areas if no forked ckpt.
// TODO:  When is fd closed?  It's not same as fdCkptFileOnDisk if compression.
//        Where do we close fd?  Or add a comment about this.
  JTRACE("MTCP is about to write checkpoint image.")(ckptFilename);
  mtcp_writememoryareas(fd, AREA_SHARED);
  // forked_ckpt_status = test_and_fork_if_forked_ckpt();
  // if (forked_ckpt_status == FORKED_CKPT_PARENT) {
  //   close(fd);  // grandchild process will finish writing ckpt.
  //   // Delete the previous checkpoint filename now.  It may take a while
  //   // time before the gradnchild process create ckpt file and users
  //   // should not be fooled by seeing a previous checkpoint image.
  //   JASSERT(unlink(ckptFilename.c_str()) == 0 || errno == ENOENT);
  //   JTRACE("*** Using forked checkpointing.\n");
  //   return;
  // }
  // // NOTE:  if (forked_ckpt_status == FORKED_CKPT_CHILD), then
  // //        it continues writing the ckpt image file, below.
  // mtcp_writememoryareas(fd, AREA_NOTSHARED);
  // mtcp_writememoryareas(fd, AREA_END);
// mtcp_writememoryareas(fd, AREA_ALL);
// JASSERT(rename(ProcessInfo::instance().getTempCkptFilename().c_str(),
//         ProcessInfo::instance().getCkptFilename().c_str()) == 0);

  if (forked_ckpt_status == FORKED_CKPT_CHILD) {
    // Use _exit() instead of exit() to avoid popping atexit() handlers
    // registered by the parent process.
    _exit(0); /* grandchild exits */
  }

  if (use_compression) {
    /* In perform_open_ckpt_image_fd(), we set SIGCHLD to our own handler.
     * Restore it now, and wait on compression process (e.g., gzip).
     */
    restore_sigchld_handler_and_wait_for_zombie(ckpt_extcomp_child_pid);

    /* IF OUT OF DISK SPACE, REPORT IT HERE. */
    JASSERT(fsync(fdCkptFileOnDisk) != -1) (JASSERT_ERRNO)
    .Text("(compression): fsync error on checkpoint file");
    JASSERT(_real_close(fdCkptFileOnDisk) == 0) (JASSERT_ERRNO)
    .Text("(compression): error closing checkpoint file.");
  }

  JTRACE("checkpoint complete");
}
