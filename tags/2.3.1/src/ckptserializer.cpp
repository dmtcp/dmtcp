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

#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include "constants.h"
#include "util.h"
#include "syscallwrappers.h"
#include "dmtcp.h"
#include "protectedfds.h"
#include "ckptserializer.h"

#define _real_pipe(a) _real_syscall(SYS_pipe, a)
#define _real_waitpid(a,b,c) _real_syscall(SYS_wait4,a,b,c,NULL)

using namespace dmtcp;

// Copied from mtcp/mtcp_restart.c.
#define DMTCP_MAGIC_FIRST 'D'
#define GZIP_FIRST 037
#ifdef HBICT_DELTACOMP
#define HBICT_FIRST 'H'
#endif

#define FORKED_CKPT_FAILED 0
#define FORKED_CKPT_PARENT 1
#define FORKED_CKPT_CHILD 2

static int forked_ckpt_status = -1;
static pid_t ckpt_extcomp_child_pid = -1;
static struct sigaction saved_sigchld_action;
static int open_ckpt_to_write(int fd, int pipe_fds[2], char **extcomp_args);
void mtcp_writememoryareas(int fd) __attribute__((weak));

static char first_char(const char *filename)
{
  int fd, rc;
  char c;

  fd = open(filename, O_RDONLY);
  JASSERT(fd >= 0) (filename) .Text("ERROR: Cannot open filename");

  rc = _real_read(fd, &c, 1);
  JASSERT(rc == 1) (filename) .Text("ERROR: Error reading from filename");

  close(fd);
  return c;
}

/* We handle SIGCHLD while checkpointing. */
static void default_sigchld_handler(int sig) {
  JASSERT(sig == SIGCHLD);
}

static void prepare_sigchld_handler()
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
  memset(&default_sigchld_action, 0, sizeof (default_sigchld_action));
  default_sigchld_action.sa_handler = default_sigchld_handler;
  sigaction(SIGCHLD, &default_sigchld_action, &saved_sigchld_action);
}

static void restore_sigchld_handler_and_wait_for_zombie(pid_t pid)
{
    /* This is done to avoid calling the user SIGCHLD handler when gzip
     * or another compression utility exits.
     * NOTE: We must wait in case user did rapid ckpt-kill in succession.
     *  Otherwise, kernel could have optimization allowing us to close fd and
     *  rename tmp ckpt file to permanent even while gzip is still writing.
     */
    /* All signals, incl. SIGCHLD, were blocked in mtcp.c:save_sig_state()
     * when beginning the ckpt.  A SIGCHLD handler was declared before
     * the gzip child process was forked.  The child may have already
     * terminated and returned, creating a blocked SIGCHLD.
     *   So, we use sigsuspend so that we can clean up the child zombie
     * with wait4 whether it already terminated or not yet.
     */
    sigset_t suspend_sigset;
    sigfillset(&suspend_sigset);
    sigdelset(&suspend_sigset, SIGCHLD);
    sigsuspend(&suspend_sigset);
    JWARNING(_real_waitpid(pid, NULL, 0) != -1) (pid) (JASSERT_ERRNO);
    pid = -1;
    sigaction(SIGCHLD, &saved_sigchld_action, NULL);
}

/*
 * This function returns the fd to which the checkpoint file should be written.
 * The purpose of using this function over open() is that this
 * function will handle compression and gzipping.
 */
static int test_use_compression(char *compressor, char *command, char *path,
                                int def)
{
  char *default_val;
  char evar[256] = "DMTCP_";
  char *do_we_compress;

  if (def)
    default_val = const_cast<char*> ("1");
  else
    default_val = const_cast<char*> ("0");

  JASSERT( strlen(strncat(evar, compressor, sizeof(evar) - strlen(evar) - 1))
           < sizeof(evar) - 1 )
         (compressor) .Text("compressor is too long.");
  do_we_compress = getenv(evar);
  // env var is unset, let's default to enabled
  // to disable compression, run with MTCP_GZIP=0
  if (do_we_compress == NULL)
    do_we_compress = default_val;

  char *endptr;
  strtol(do_we_compress, &endptr, 0);
  if (*do_we_compress == '\0' || *endptr != '\0') {
    JWARNING(false) (evar) (do_we_compress)
      .Text("Compression env var not defined as a number."
            "  Checkpoint image will not be compressed.");
    do_we_compress = const_cast<char*> ("0");
  }

  if ( 0 == strcmp(do_we_compress, "0") )
    return 0;

  /* Check if the executable exists. */
  if (Util::findExecutable(command, getenv("PATH"), path) == NULL) {
    JWARNING(false) (command)
      .Text("Command cannot be executed. Compression will not be used.");
    return 0;
  }

  /* If we arrive down here, it's safe to compress. */
  return 1;
}

#ifdef HBICT_DELTACOMP
static int open_ckpt_to_write_hbict(int fd, int pipe_fds[2], char *hbict_path,
                                    char *gzip_path)
{
  char *hbict_args[] = {
    const_cast<char*> ("hbict"),
    const_cast<char*> ("-a"),
    NULL,
    NULL
  };
  hbict_args[0] = hbict_path;
  JTRACE("open_ckpt_to_write_hbict\n");

  if (gzip_path != NULL){
    hbict_args[2] = const_cast<char*> ("-z100");
  }
  return open_ckpt_to_write(fd,pipe_fds,hbict_args);
}
#endif

static int open_ckpt_to_write_gz(int fd, int pipe_fds[2], char *gzip_path)
{
  char *gzip_args[] = {
    const_cast<char*> ("gzip"),
    const_cast<char*> ("-1"),
    const_cast<char*> ("-"),
    NULL
  };
  gzip_args[0] = gzip_path;
  JTRACE("open_ckpt_to_write_gz\n");

  return open_ckpt_to_write(fd,pipe_fds,gzip_args);
}

static int perform_open_ckpt_image_fd(const char *tempCkptFilename,
                                      int *use_compression,
                                      int *fdCkptFileOnDisk)
{
  *use_compression = 0;  /* default value */

  /* 1. Open fd to checkpoint image on disk */
  /* Create temp checkpoint file and write magic number to it */
  int flags = O_CREAT | O_TRUNC | O_WRONLY;
  int fd = _real_open(tempCkptFilename, flags, 0600);
  *fdCkptFileOnDisk = fd; /* if use_compression, fd will be reset to pipe */
  JASSERT(fd != -1) (tempCkptFilename) (JASSERT_ERRNO)
    .Text ("Error creating file.");

#ifdef FAST_RST_VIA_MMAP
  return fd;
#endif

  /* 2. Test if using GZIP/HBICT compression */
  /* 2a. Test if using GZIP compression */
  int use_gzip_compression = 0;
  int use_deltacompression = 0;
  char *gzip_cmd = const_cast<char*> ("gzip");
  char gzip_path[PATH_MAX];
  use_gzip_compression = test_use_compression(const_cast<char*> ("GZIP"),
                                              gzip_cmd, gzip_path, 1);

  /* 2b. Test if using HBICT compression */
# ifdef HBICT_DELTACOMP
  char *hbict_cmd = const_cast<char*> ("hbict");
  char hbict_path[PATH_MAX];
  JTRACE("NOTICE: hbict compression is enabled\n");

  use_deltacompression = test_use_compression(const_cast<char*> ("HBICT"),
                                              hbict_cmd, hbict_path, 1);
# endif

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
      JWARNING(false) .Text("Error creating pipe. Compression won't be used.");
      use_gzip_compression = use_deltacompression = 0;
    }

    /* 3c. Fork compressor child */
    if (use_deltacompression) { /* fork a hbict process */
# ifdef HBICT_DELTACOMP
      *use_compression = 1;
      if ( use_gzip_compression ) // We may want hbict compression only
        fd = open_ckpt_to_write_hbict(fd, pipe_fds, hbict_path, gzip_path);
      else
        fd = open_ckpt_to_write_hbict(fd, pipe_fds, hbict_path, NULL);
# endif
    } else if (use_gzip_compression) {/* fork a gzip process */
      *use_compression = 1;
      fd = open_ckpt_to_write_gz(fd, pipe_fds, gzip_path);
      if (pipe_fds[0] == -1) {
        /* If open_ckpt_to_write_gz() failed to fork the gzip process */
        *use_compression = 0;
      }
    } else {
      JASSERT(false) .Text("Not Reached!\n");
    }
  }

  return fd;
}

static int test_and_prepare_for_forked_ckpt()
{
#ifdef TEST_FORKED_CHECKPOINTING
  return 1;
#endif

  if (getenv(ENV_VAR_FORKED_CKPT) == NULL) {
    return 0;
  }

  /* Set SIGCHLD to our own handler;
   *     User handling is restored after forking child process.
   */
  prepare_sigchld_handler();

  pid_t forked_cpid = _real_syscall(SYS_fork);
  if (forked_cpid == -1) {
    JWARNING(false)
      .Text("Failed to do forked checkpointing, trying normal checkpoint");
    return FORKED_CKPT_FAILED;
  } else if (forked_cpid > 0) {
    restore_sigchld_handler_and_wait_for_zombie(forked_cpid);
    JTRACE("checkpoint complete\n");
    return FORKED_CKPT_PARENT;
  } else {
    pid_t grandchild_pid = _real_syscall(SYS_fork);
    JWARNING(grandchild_pid != -1)
      .Text("WARNING: Forked checkpoint failed, no checkpoint available");
    if (grandchild_pid > 0) {
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

  cpid = _real_syscall(SYS_fork);
  if (cpid == -1) {
    JWARNING(false) (extcomp_args[0]) (JASSERT_ERRNO)
      .Text("WARNING: error forking child process. Compression won't be used");
    _real_close(pipe_fds[0]);
    _real_close(pipe_fds[1]);
    pipe_fds[0] = pipe_fds[1] = -1;
    //fall through to return fd
  } else if (cpid > 0) { /* parent process */
    //Before running gzip in child process, we must not use LD_PRELOAD.
    // See revision log 342 for details concerning bash.
    ckpt_extcomp_child_pid = cpid;
    JWARNING(_real_close(pipe_fds[0]) == 0) (JASSERT_ERRNO)
      .Text("WARNING: close failed");
    fd = pipe_fds[1]; //change return value
  } else { /* child process */
    //static int (*libc_unsetenv) (const char *name);
    //static int (*libc_execvp) (const char *path, char *const argv[]);

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
    unsetenv("LD_PRELOAD"); // If in bash, this is bash env. var. version
    char *ld_preload_str = (char*) getenv("LD_PRELOAD");
    if (ld_preload_str != NULL) {
      ld_preload_str[0] = '\0';
    }

    _real_execve(extcomp_args[0], extcomp_args, NULL);

    /* should not arrive here */
    JASSERT(false)
      .Text("Compression failed! No checkpointing will be performed!"
            " Cancel now!");
  }

  return fd;
}


// Copied from mtcp/mtcp_restart.c.
// Let's keep this code close to MTCP code to avoid maintenance problems.
// MTCP code in:  mtcp/mtcp_restart.c:open_ckpt_to_read()
// A previous version tried to replace this with popen, causing a regression:
//   (no call to pclose, and possibility of using a wrong fd).
// Returns fd;
static int open_ckpt_to_read(const char *filename)
{
  int fd;
  int fds[2];
  char fc;
  const char *decomp_path;
  const char **decomp_args;
  const char *gzip_path = "gzip";
  static const char * gzip_args[] = {
    const_cast<char*> ("gzip"),
    const_cast<char*> ("-d"),
    const_cast<char*> ("-"),
    NULL
  };
#ifdef HBICT_DELTACOMP
  const char *hbict_path = const_cast<char*> ("hbict");
  static const char *hbict_args[] = {
    const_cast<char*> ("hbict"),
    const_cast<char*> ("-r"),
    NULL
  };
#endif
  pid_t cpid;

  fc = first_char(filename);
  fd = open(filename, O_RDONLY);
  JASSERT(fd>=0)(filename).Text("Failed to open file.");

  if (fc == DMTCP_MAGIC_FIRST) { /* no compression */
    return fd;
  }
  else if (fc == GZIP_FIRST
#ifdef HBICT_DELTACOMP
           || fc == HBICT_FIRST
#endif
          ) {
    if (fc == GZIP_FIRST) {
      decomp_path = gzip_path;
      decomp_args = gzip_args;
    }
#ifdef HBICT_DELTACOMP
    else {
      decomp_path = hbict_path;
      decomp_args = hbict_args;
    }
#endif

    JASSERT(pipe(fds) != -1) (filename)
      .Text("Cannot create pipe to execute gunzip to decompress ckpt file!");

    cpid = _real_fork();

    JASSERT(cpid != -1)
      .Text("ERROR: Cannot fork to execute gunzip to decompress ckpt file!");
    if (cpid > 0) { /* parent process */
      JTRACE("created child process to uncompress checkpoint file") (cpid);
      close(fd);
      close(fds[1]);
      // Wait for child process
      JASSERT(waitpid(cpid, NULL, 0) == cpid);
      return fds[0];
    } else { /* child process */
      /* Fork a grandchild process and kill the parent. This way the grandchild
       * process never becomes a zombie.
       *
       * Sometimes dmtcp_restart is called with multiple ckpt images. In that
       * situation, the dmtcp_restart process creates gzip processes and only
       * later forks mtcp_restart processes. The gzip processes can not be
       * wait()'d upon by the corresponding mtcp_restart processes because
       * their parent is the original dmtcp_restart process and thus they
       * become zombie.
       */
      cpid = _real_fork();
      JASSERT(cpid != -1);
      if (cpid > 0) {
        // Use _exit() instead of exit() to avoid popping atexit() handlers
        // registered by the parent process.
        _exit(0);
      }

      // Grandchild process
      JTRACE ( "child process, will exec into external de-compressor");
      fd = _real_dup(_real_dup(_real_dup(fd)));
      fds[1] = _real_dup(fds[1]);
      close(fds[0]);
      JASSERT(fd != -1);
      JASSERT(_real_dup2(fd, STDIN_FILENO) == STDIN_FILENO);
      close(fd);
      JASSERT(_real_dup2(fds[1], STDOUT_FILENO) == STDOUT_FILENO);
      close(fds[1]);
      _real_execvp(decomp_path, (char **)decomp_args);
      JASSERT(decomp_path!=NULL) (decomp_path)
        .Text("Failed to launch gzip.");
      /* should not get here */
      JASSERT(false)
        .Text("Decompression failed!  No restoration will be performed!");
    }
  } else { /* invalid magic number */
    JASSERT(false)
      .Text("ERROR: Invalid magic number in this checkpoint file!");
  }
  return -1;
}

// See comments above for open_ckpt_to_read()
int dmtcp::CkptSerializer::openCkptFileToRead(const dmtcp::string& path)
{
  char buf[1024];
  int fd = open_ckpt_to_read(path.c_str());
  // The rest of this function is for compatibility with original definition.
  JASSERT(fd >= 0) (path) .Text("Failed to open file.");
  const int len = strlen(DMTCP_FILE_HEADER);
  JASSERT(_real_read(fd, buf, len) == len)(path) .Text("_real_read() failed");
  if (strncmp(buf, DMTCP_FILE_HEADER, len) == 0) {
    JTRACE("opened checkpoint file [uncompressed]")(path);
  } else {
    _real_close(fd);
    fd = open_ckpt_to_read(path.c_str()); /* Re-open from beginning */
    JASSERT(fd >= 0) (path) .Text("Failed to open file.");
  }
  return fd;
}

void dmtcp::CkptSerializer::createCkptDir()
{
  string ckptDir = ProcessInfo::instance().getCkptDir();
  JASSERT(!ckptDir.empty());
  JASSERT(mkdir(ckptDir.c_str(), S_IRWXU) == 0 || errno == EEXIST)
    (JASSERT_ERRNO) (ckptDir)
    .Text("Error creating checkpoint directory");

  JASSERT(0 == access(ckptDir.c_str(), X_OK|W_OK)) (ckptDir)
    .Text("ERROR: Missing execute- or write-access to checkpoint dir");
}

// See comments above for open_ckpt_to_read()
void dmtcp::CkptSerializer::writeCkptImage(void *mtcpHdr, size_t mtcpHdrLen)

{
  string ckptFilename = ProcessInfo::instance().getCkptFilename();
  string tempCkptFilename = ckptFilename;
  tempCkptFilename += ".temp";

  JTRACE("Thread performing checkpoint.") (gettid());
  createCkptDir();
  forked_ckpt_status = test_and_prepare_for_forked_ckpt();
  if (forked_ckpt_status == FORKED_CKPT_PARENT) {
    JTRACE("*** Using forked checkpointing.\n");
    return;
  }

  /* fd will either point to the ckpt file to write, or else the write end
   * of a pipe leading to a compression child process.
   */
  int use_compression = 0;
  int fdCkptFileOnDisk = -1;
  int fd = -1;
#ifdef DEBUG
  // For easy inspection inside gdb:
  int jassertlog_fd = PROTECTED_JASSERTLOG_FD;
#endif

  fd = perform_open_ckpt_image_fd(tempCkptFilename.c_str(), &use_compression,
                                  &fdCkptFileOnDisk);
  JASSERT(fdCkptFileOnDisk >= 0 );
  JASSERT(use_compression || fd == fdCkptFileOnDisk);

  // The rest of this function is for compatibility with original definition.
  writeDmtcpHeader(fd);

  // Write MTCP header
  JASSERT(Util::writeAll(fd, mtcpHdr, mtcpHdrLen) == (ssize_t) mtcpHdrLen);

  JTRACE ( "MTCP is about to write checkpoint image." )(ckptFilename);
  mtcp_writememoryareas(fd);

  if (use_compression) {
    /* In perform_open_ckpt_image_fd(), we set SIGCHLD to our own handler.
     * Restore it now.
     */
    restore_sigchld_handler_and_wait_for_zombie(ckpt_extcomp_child_pid);

    /* IF OUT OF DISK SPACE, REPORT IT HERE. */
    JASSERT(fsync(fdCkptFileOnDisk) != -1) (JASSERT_ERRNO)
      .Text("(compression): fsync error on checkpoint file");
    JASSERT(_real_close(fdCkptFileOnDisk) == 0) (JASSERT_ERRNO)
      .Text("(compression): error closing checkpoint file.");
  }

  /* Now that temp checkpoint file is complete, rename it over old permanent
   * checkpoint file.  Uses rename() syscall, which doesn't change i-nodes.
   * So, gzip process can continue to write to file even after renaming.
   */
  JASSERT(rename(tempCkptFilename.c_str(), ckptFilename.c_str()) == 0);

  if (forked_ckpt_status == FORKED_CKPT_CHILD) {
    // Use _exit() instead of exit() to avoid popping atexit() handlers
    // registered by the parent process.
    _exit(0); /* grandchild exits */
  }

  JTRACE("checkpoint complete");
}

void dmtcp::CkptSerializer::writeDmtcpHeader(int fd)
{
  const ssize_t len = strlen(DMTCP_FILE_HEADER);
  JASSERT(write(fd, DMTCP_FILE_HEADER, len) == len);

  jalib::JBinarySerializeWriterRaw wr("", fd);
  dmtcp::ProcessInfo::instance().serialize(wr);
  ssize_t written = len + wr.bytes();

  // We must write in multiple of PAGE_SIZE
  const ssize_t pagesize = Util::pageSize();
  ssize_t remaining = pagesize - (written % pagesize);
  char buf[remaining];
  JASSERT(Util::writeAll(fd, buf, remaining) == remaining);
}

int dmtcp::CkptSerializer::readCkptHeader(const dmtcp::string& path,
                                          dmtcp::ProcessInfo *pInfo)
{
  int fd = openCkptFileToRead(path);
  const size_t len = strlen(DMTCP_FILE_HEADER);

  jalib::JBinarySerializeReaderRaw rdr("", fd);
  pInfo->serialize(rdr);
  size_t numRead = len + rdr.bytes();

  // We must read in multiple of PAGE_SIZE
  const ssize_t pagesize = Util::pageSize();
  ssize_t remaining = pagesize - (numRead % pagesize);
  char buf[remaining];
  JASSERT(Util::readAll(fd, buf, remaining) == remaining);
  return fd;
}
