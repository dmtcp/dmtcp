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
#include "coordinatorapi.h"

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

typedef enum {
  FORKED_CKPT_FAILED,
  FORKED_CKPT_PARENT,
  FORKED_CKPT_GRANDCHILD
} forked_rc_t;

static forked_rc_t forked_ckpt_status = FORKED_CKPT_FAILED;
static int open_ckpt_to_write(int fd, int pipe_fds[2], char **extcomp_args);
void mtcp_writememoryareas(int fd) __attribute__((weak));

static void
trivial_sigchld_handler(int sig)
{
  JASSERT(sig == SIGCHLD);
}
static void
set_trivial_sigchld_action(struct sigaction *trivial_sigchld_action_ptr)
{
  memset(trivial_sigchld_action_ptr, 0, sizeof(*trivial_sigchld_action_ptr));
  trivial_sigchld_action_ptr->sa_handler = trivial_sigchld_handler;
}
static void
allow_delivery_of_one_sigchld()
{
  /* This is done to avoid calling the user SIGCHLD handler when gzip
   * or another compression utility exits.
   * NOTE: rename tmp ckpt file is done in dmtcpworker.cpp.  If user did rapid
   *       ckpt-kill in succession, then rename doesn't happen. So, no problem.
   */

  /* All signals, incl. SIGCHLD, were blocked in
   * threadlist.cpp:checkpointhread().  A SIGCHLD handler was declared before
   * the gzip child process was forked.  The child may have already
   * terminated and returned, creating a blocked SIGCHLD.
   *   So, we use sigsuspend so that we can clean up the child zombie
   * with waitpid/wait4 whether it already terminated or not yet.
   */
  sigset_t mask_all_but_sigchld;
  sigfillset(&mask_all_but_sigchld);
  sigdelset(&mask_all_but_sigchld, SIGCHLD);
  _real_sigsuspend(&mask_all_but_sigchld); // block everything except SIGCHLD
  // This assumes SIGCHLD handler is trivial_sigchld_action, not SIG_IGN.
  // sigsuspend was interrupted when child process called _exit: SIGCHLD
  // delivered, and mask_all_but_sigchld is replaced by the old mask, which
  //   then bloks SIGCHLD: So, any second SIGSHLD will be blocked.
}

// returns FORKED_PARENT, FORKED_GRANDCHILD, FORKED_CKPT_FAILED
static forked_rc_t
double_fork()
{
  pid_t pid;
  // 0. This assumes SIGCHLD is already blocked.  This works during checkpoint.
  struct sigaction saved_sigchld_action;
  // 1. Set SIGCHLD handler to trivial handler; save original SIGCHLD handler
  // NOTE: allow_delivery_of_one_sigchld calls sigsuspend(). This is unblocked
  //       when SIGCHLD arrives, only for trivial_sigchld_action, not SIG_IGN.
  struct sigaction trivial_sigchld_action;
  set_trivial_sigchld_action(&trivial_sigchld_action);
  sigaction(SIGCHLD, &trivial_sigchld_action, &saved_sigchld_action);
  pid = _real_sys_fork();
  JASSERT(pid >= 0)(pid); // Or could do:  return FORKED_CKPT_FAILED;

  if (pid > 0) {
    // 2. Allow delivery of just one SIGCHLD (due to the child that did _exit())
    allow_delivery_of_one_sigchld();

    // 3. Next, we reap that child process (which is now a zombie).
    JWARNING(_real_waitpid(pid, NULL, 0) != -1) (pid) (JASSERT_ERRNO);

    // 4. ... and we then restore the original SIGCHLD handler
    sigaction(SIGCHLD, &saved_sigchld_action, NULL);
    return FORKED_CKPT_PARENT;
  } else {
    pid = _real_sys_fork();
    JASSERT(pid >= 0)(pid);
    if (pid > 0) {
      _exit(EXIT_SUCCESS); // Child process exits, delivers SIGCHLD
    }
    return FORKED_CKPT_GRANDCHILD;
  }
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
    /* 3a. Open pipe */
    int pipe_fds[2];
    if (_real_pipe(pipe_fds) == -1) {
      JWARNING(false).Text("Error creating pipe. Compression won't be used.");
      use_gzip_compression = use_deltacompression = 0;
    }

    /* 3b. Fork compressor child */
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

static forked_rc_t
test_and_prepare_for_forked_ckpt()
{
#ifdef TEST_FORKED_CHECKPOINTING
  return FORKED_CKPT_PARENT;
#endif  // ifdef TEST_FORKED_CHECKPOINTING

  if (getenv(ENV_VAR_FORKED_CKPT) == NULL) {
    return FORKED_CKPT_FAILED;
  }

  return double_fork();
}

int
open_ckpt_to_write(int fd, int pipe_fds[2], char **extcomp_args)
{
  forked_rc_t fork_rc = double_fork();
  if (fork_rc == FORKED_CKPT_FAILED) {
    JWARNING(false) (extcomp_args[0]) (JASSERT_ERRNO)
    .Text("WARNING: error forking child process. Compression won't be used");
    _real_close(pipe_fds[0]);
    _real_close(pipe_fds[1]);
    pipe_fds[0] = pipe_fds[1] = -1;

  // fall through to return fd
  } else if (fork_rc == FORKED_CKPT_PARENT) { /* parent process */
    // Before running gzip in child process, we must not use LD_PRELOAD.
    // See revision log 342 for details concerning bash.
    JWARNING(_real_close(pipe_fds[0]) == 0) (JASSERT_ERRNO)
    .Text("WARNING: close failed");
    fd = pipe_fds[1];  // change return value
  } else { /* child process */
    JASSERT(fork_rc == FORKED_CKPT_GRANDCHILD)(fork_rc);
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
  forked_ckpt_status = test_and_prepare_for_forked_ckpt();
  if (forked_ckpt_status == FORKED_CKPT_PARENT) {
    JTRACE("*** Using forked checkpointing.\n");
    return;
  }

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

  JTRACE("MTCP is about to write checkpoint image.")(ckptFilename);
  mtcp_writememoryareas(fd);

  if (use_compression) {
    /* IF OUT OF DISK SPACE, REPORT IT HERE. */
    JASSERT(fsync(fdCkptFileOnDisk) != -1) (JASSERT_ERRNO)
    .Text("(compression): fsync error on checkpoint file");
    JASSERT(_real_close(fdCkptFileOnDisk) == 0) (JASSERT_ERRNO)
    .Text("(compression): error closing checkpoint file.");
  }

  if (forked_ckpt_status == FORKED_CKPT_GRANDCHILD) {
    /* Now that temp checkpoint file is complete, rename it over old permanent
     * checkpoint file.  Uses rename() syscall, which doesn't change i-nodes.
     * So, gzip process can continue to write to file even after renaming.
     */
    JASSERT(rename(ProcessInfo::instance().getTempCkptFilename().c_str(),
                   ProcessInfo::instance().getCkptFilename().c_str()) == 0);

    // Use _exit() instead of exit() to avoid popping atexit() handlers
    // registered by the parent process.
    _exit(0); /* grandchild exits */
  }

  JTRACE("checkpoint complete");
}
