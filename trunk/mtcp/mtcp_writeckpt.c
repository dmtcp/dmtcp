/*****************************************************************************
 *   Copyright (C) 2006-2010 by Michael Rieker, Jason Ansel, Kapil Arya, and *
 *                                                            Gene Cooperman *
 *   mrieker@nii.net, jansel@csail.mit.edu, kapil@ccs.neu.edu, and           *
 *                                                          gene@ccs.neu.edu *
 *                                                                           *
 *   This file is part of the MTCP module of DMTCP (DMTCP:mtcp).             *
 *                                                                           *
 *  DMTCP:mtcp is free software: you can redistribute it and/or              *
 *  modify it under the terms of the GNU Lesser General Public License as    *
 *  published by the Free Software Foundation, either version 3 of the       *
 *  License, or (at your option) any later version.                          *
 *                                                                           *
 *  DMTCP:dmtcp/src is distributed in the hope that it will be useful,       *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *  GNU Lesser General Public License for more details.                      *
 *                                                                           *
 *  You should have received a copy of the GNU Lesser General Public         *
 *  License along with DMTCP:dmtcp/src.  If not, see                         *
 *  <http://www.gnu.org/licenses/>.                                          *
 *****************************************************************************/


#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <termios.h>       // for tcdrain, tcsetattr, etc.
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/types.h>     // for gettid, tgkill, waitpid
#include <sys/wait.h>	   // for waitpid
#include <asm/unistd.h>  // for gettid, tgkill

#include "mtcp_internal.h"
#include "mtcp_util.h"

static int test_use_compression(char *compressor, char *command, char *path,
                                int def);
static int open_ckpt_to_write(int fd, int pipe_fds[2], char **args);
static size_t writefiledescrs (int fd, int fdCkptFileOnDisk);
static void writememoryarea (int fd, Area *area,
			     int stack_was_seen, int vsyscall_exists);
//static void writefile (int fd, void const *buff, size_t size);
static void preprocess_special_segments(int *vsyscall_exists);

#define FORKED_CKPT_FAILED 0
#define FORKED_CKPT_PARENT 1
#define FORKED_CKPT_CHILD 2

#ifdef HBICT_DELTACOMP
static int open_ckpt_to_write_hbict(int fd, int pipe_fds[2], char *hbict_path,
                                    char *gzip_path);
#endif
static int open_ckpt_to_write_gz(int fd, int pipe_fds[2], char *gzip_path);

static int test_and_prepare_for_forked_ckpt();
static int perform_open_ckpt_image_fd(const char *temp_ckpt_filename,
                                      int *use_compression,
                                      int *fdCkptFileOnDisk);
static void write_ckpt_to_file(int fd, int fdCkptFileOnDisk);


extern int mtcp_verify_count;  // number of checkpoints to go
extern int mtcp_verify_total;  // value given by envar
extern VA mtcp_saved_heap_start;
extern int  (*mtcp_callback_ckpt_fd)(int fd);
extern void (*mtcp_callback_write_ckpt_header)(int fd);


static pid_t mtcp_ckpt_extcomp_child_pid = -1;
static struct sigaction saved_sigchld_action;
static void (*restore_start_fptr)(); /* will be bound to fnc, mtcp_restore_start */
static void (*restore_finish_fptr)(); /* will be bound to fnc, mtcp_restore_finish */

void mtcp_writeckpt_init(VA restore_start_fn, VA restore_finish_fn)
{
  /* Need to get the addresses for the library only once */
  static int initialized = 0;
  if (initialized) {
    return;
  }

  /* Get size and address of the shareable - used to separate it from the rest
   * of the stuff. All routines needed to perform restore must be within this
   * address range
   */
#ifndef USE_PROC_MAPS
  mtcp_shareable_begin = (VA)((unsigned long int)mtcp_shareable_begin)
                         & MTCP_PAGE_MASK;
  mtcp_shareable_end = (mtcp_shareable_end + MTCP_PAGE_SIZE - 1)
                       & MTCP_PAGE_MASK;
#else
  mtcp_get_memory_region_of_this_library(&mtcp_shareable_begin,
                                         &mtcp_shareable_end);
#endif
  restore_start_fptr = (void*)restore_start_fn;
  restore_finish_fptr = (void*)restore_finish_fn;
  initialized = 1;
}

/*
 *
 * This function returns the fd to which the checkpoint file should be written.
 * The purpose of using this function over mtcp_sys_open() is that this
 * function will handle compression and gzipping.
 */
static int test_use_compression(char *compressor, char *command, char *path,
                                int def)
{
  char *default_val;
  char env_var1[256] = "MTCP_";
  char env_var2[256] = "DMTCP_";
  char *do_we_compress;

  int env_var1_len = sizeof(env_var1);
  int env_var2_len = sizeof(env_var2);

  if (def)
    default_val = "1";
  else
    default_val = "0";

  mtcp_strncat(env_var1,compressor,env_var1_len);
  mtcp_strncat(env_var2,compressor,env_var2_len);
  do_we_compress = getenv(env_var1);
  // allow alternate name for env var
  if (do_we_compress == NULL){
    do_we_compress = getenv(env_var2);
  }
  // env var is unset, let's default to enabled
  // to disable compression, run with MTCP_GZIP=0
  if (do_we_compress == NULL)
    do_we_compress = default_val;

  char *endptr;
  strtol(do_we_compress, &endptr, 0);
  if ( *do_we_compress == '\0' || *endptr != '\0' ) {
    mtcp_printf("WARNING: %s/%s defined as %s (not a number)\n"
	        "  Checkpoint image will not be compressed.\n",
	        env_var1, env_var2, do_we_compress);
    do_we_compress = "0";
  }

  if ( 0 == mtcp_strcmp(do_we_compress, "0") )
    return 0;

  /* Check if the executable exists. */
  if (mtcp_find_executable(command, getenv("PATH"), path) == NULL) {
    MTCP_PRINTF("WARNING: %s cannot be executed. Compression will "
                "not be used.\n", command);
    return 0;
  }

  /* If we arrive down here, it's safe to compress. */
  return 1;
}

#ifdef HBICT_DELTACOMP
static int open_ckpt_to_write_hbict(int fd, int pipe_fds[2], char *hbict_path,
                                    char *gzip_path)
{
  char *hbict_args[] = { "hbict", "-a", NULL, NULL };
  hbict_args[0] = hbict_path;
  DPRINTF("open_ckpt_to_write_hbict\n");

  if (gzip_path != NULL){
    hbict_args[2] = "-z100";
  }
  return open_ckpt_to_write(fd,pipe_fds,hbict_args);
}
#endif

static int open_ckpt_to_write_gz(int fd, int pipe_fds[2], char *gzip_path)
{
  char *gzip_args[] = { "gzip", "-1", "-", NULL };
  gzip_args[0] = gzip_path;
  DPRINTF("open_ckpt_to_write_gz\n");

  return open_ckpt_to_write(fd,pipe_fds,gzip_args);
}

int
open_ckpt_to_write(int fd, int pipe_fds[2], char **extcomp_args)
{
  pid_t cpid;

  cpid = mtcp_sys_fork();
  if (cpid == -1) {
    MTCP_PRINTF("WARNING: error forking child process `%s`.  Compression will "
                "not be used [%s].\n", extcomp_args[0],
                strerror(mtcp_sys_errno));
    mtcp_sys_close(pipe_fds[0]);
    mtcp_sys_close(pipe_fds[1]);
    pipe_fds[0] = pipe_fds[1] = -1;
    //fall through to return fd
  } else if (cpid > 0) { /* parent process */
    //Before running gzip in child process, we must not use LD_PRELOAD.
    // See revision log 342 for details concerning bash.
    mtcp_ckpt_extcomp_child_pid = cpid;
    if (mtcp_sys_close(pipe_fds[0]) == -1)
      MTCP_PRINTF("WARNING: close failed: %s\n", strerror(mtcp_sys_errno));
    fd = pipe_fds[1]; //change return value
  } else { /* child process */
    //static int (*libc_unsetenv) (const char *name);
    //static int (*libc_execvp) (const char *path, char *const argv[]);

    mtcp_sys_close(pipe_fds[1]);
    int infd = mtcp_sys_dup(pipe_fds[0]);
    int outfd = mtcp_sys_dup(fd);
    mtcp_sys_dup2(infd, STDIN_FILENO);
    mtcp_sys_dup2(outfd, STDOUT_FILENO);

    // No need to close the other FDS
    if (pipe_fds[0] > STDERR_FILENO) {
      mtcp_sys_close(pipe_fds[0]);
    }
    if (infd > STDERR_FILENO) {
      mtcp_sys_close(infd);
    }
    if (outfd > STDERR_FILENO) {
      mtcp_sys_close(outfd);
    }
    if (fd > STDERR_FILENO) {
      mtcp_sys_close(fd);
    }

    // Don't load libdmtcp.so, etc. in exec.
    unsetenv("LD_PRELOAD"); // If in bash, this is bash env. var. version
    char *ld_preload_str = (char*) getenv("LD_PRELOAD");
    if (ld_preload_str != NULL) {
      ld_preload_str[0] = '\0';
    }
    //libc_unsetenv = mtcp_get_libc_symbol("unsetenv");
    //(*libc_unsetenv)("LD_PRELOAD");

    //libc_execvp = mtcp_get_libc_symbol("execvp");
    //(*libc_execvp)(extcomp_args[0], extcomp_args);
    mtcp_sys_execve(extcomp_args[0], extcomp_args, NULL);

    /* should not arrive here */
    MTCP_PRINTF("ERROR: compression failed!  No checkpointing will be "
                "performed!  Cancel now!\n");
    mtcp_sys_exit(1);
  }

  return fd;
}


/*****************************************************************************
 *
 *  This routine is called from time-to-time to write a new checkpoint file.
 *  It assumes all the threads are suspended.
 *
 *****************************************************************************/
void mtcpHookWriteCkptData(const void *buf, size_t size) __attribute__ ((weak));
void mtcp_checkpointeverything(const char *temp_ckpt_filename,
                               const char *perm_ckpt_filename)
{
  DPRINTF("thread:%d performing checkpoint.\n", mtcp_sys_kernel_gettid ());

  int forked_ckpt_status = test_and_prepare_for_forked_ckpt();
  if (forked_ckpt_status == FORKED_CKPT_PARENT) {
    DPRINTF("*** Using forked checkpointing.\n");
    return;
  }

  /* fd will either point to the ckpt file to write, or else the write end
   * of a pipe leading to a compression child process.
   */
  int use_compression = 0;
  int fdCkptFileOnDisk = -1;
  int fd = -1;

  /* Allow target application to write ckpt-image according to their own
   * preference. If the symbol is defined, MTCP will not create the checkpoint
   * image.
   */
  if (mtcpHookWriteCkptData == NULL) {
    fd = perform_open_ckpt_image_fd(temp_ckpt_filename, &use_compression,
                                    &fdCkptFileOnDisk);
    MTCP_ASSERT( fdCkptFileOnDisk >= 0 );
    MTCP_ASSERT( use_compression || fd == fdCkptFileOnDisk );
  }

  // Let DMTCP write the header.
  if (mtcp_callback_write_ckpt_header != NULL) {
    (*mtcp_callback_write_ckpt_header)(fd);
  }

  write_ckpt_to_file(fd, fdCkptFileOnDisk);

  if (mtcpHookWriteCkptData == NULL) {
    if (use_compression) {
      /* IF OUT OF DISK SPACE, REPORT IT HERE. */
      /* In perform_open_ckpt_image_fd(), we set SIGCHLD to SIG_DFL.
       * This is done to avoid calling the user SIGCHLD handler (if the user
       * SIG_DFL is needed for mtcp_sys_wait4() to work cleanly.
       * NOTE: We must wait in case user did rapid ckpt-kill in succession.
       *  Otherwise, kernel could have optimization allowing us to close fd
       *  and rename tmp ckpt file to permanent even while gzip is still writing.
       */
      if ( mtcp_sys_wait4(mtcp_ckpt_extcomp_child_pid, NULL, 0, NULL ) == -1 ) {
        DPRINTF("(compression): waitpid: %s\n", strerror(mtcp_sys_errno));
      }
      mtcp_ckpt_extcomp_child_pid = -1;
      sigaction(SIGCHLD, &saved_sigchld_action, NULL);
      if (fsync(fdCkptFileOnDisk) < 0) {
        MTCP_PRINTF("(compression): fsync error on checkpoint file: %s\n",
                    strerror(errno));
        mtcp_abort ();
      }
      if (close(fdCkptFileOnDisk) < 0) {
        MTCP_PRINTF("(compression): error closing checkpoint file: %s\n",
                    strerror(errno));
        mtcp_abort ();
      }
    }

    /* Maybe it's time to verify the checkpoint.
     * If so, exec an mtcp_restore with the temp file (in case temp file is bad,
     *   we'll still have the last one).
     * If the new file is good, mtcp_restore will rename it over the last one.
     */

    if (mtcp_verify_total != 0) --mtcp_verify_count;

    /* Now that temp checkpoint file is complete, rename it over old permanent
     * checkpoint file.  Uses rename() syscall, which doesn't change i-nodes.
     * So, gzip process can continue to write to file even after renaming.
     */

    else mtcp_rename_ckptfile(temp_ckpt_filename, perm_ckpt_filename);

  }
  if (forked_ckpt_status == FORKED_CKPT_CHILD)
    mtcp_sys_exit (0); /* grandchild exits */

  DPRINTF("checkpoint complete\n");
}

static int perform_open_ckpt_image_fd(const char *temp_ckpt_filename,
                                      int *use_compression,
                                      int *fdCkptFileOnDisk)
{
  *use_compression = 0;  /* default value */

  /* 1. Open fd to checkpoint image on disk */
  /* Create temp checkpoint file and write magic number to it */
  int flags = O_CREAT | O_TRUNC | O_WRONLY;
  int fd = mtcp_safe_open(temp_ckpt_filename, flags, 0600);
  *fdCkptFileOnDisk = fd; /* if use_compression, fd will be reset to pipe */
  if (fd < 0) {
    MTCP_PRINTF("error creating %s: %s\n",
                temp_ckpt_filename, strerror(mtcp_sys_errno));
    mtcp_abort();
  }

#ifdef FAST_RST_VIA_MMAP
  return fd;
#endif

  /* 2. Test if using GZIP/HBICT compression */
  /* 2a. Test if using GZIP compression */
  int use_gzip_compression = 0;
  int use_deltacompression = 0;
  char *gzip_cmd = "gzip";
  char gzip_path[PATH_MAX];
  use_gzip_compression = test_use_compression("GZIP", gzip_cmd, gzip_path, 1);

  /* 2b. Test if using HBICT compression */
# ifdef HBICT_DELTACOMP
  char *hbict_cmd = "hbict";
  char hbict_path[PATH_MAX];
  DPRINTF("NOTICE: hbict compression is enabled\n");

  use_deltacompression = test_use_compression("HBICT", hbict_cmd, hbict_path, 1);
# endif

  /* 3. We now have the information to pipe to gzip, or directly to fd.
  *     We do it this way, so that gzip will be direct child of forked process
  *       when using forked checkpointing.
  */

  if (use_deltacompression || use_gzip_compression) { /* fork a hbict process */
    /* 3a. Set SIGCHLD to ignore; user handling is restored after gzip finishes.
     *
     * NOTE: Although the default action for SIGCHLD is supposedly SIG_IGN,
     * for historical reasons there are differences between SIG_DFL and SIG_IGN
     * for SIGCHLD.  See sigaction(2), NOTES section for more details. */
    struct sigaction default_sigchld_action;
    memset(&default_sigchld_action, 0, sizeof (default_sigchld_action));
    default_sigchld_action.sa_handler = SIG_IGN;
    sigaction(SIGCHLD, &default_sigchld_action, &saved_sigchld_action);

    /* 3b. Open pipe */
    /* Note:  Must use mtcp_sys_pipe(), to go to kernel, since
     *   DMTCP has a wrapper around glibc promoting pipes to socketpairs,
     *   DMTCP doesn't directly checkpoint/restart pipes.
     */
    int pipe_fds[2];
    if (mtcp_sys_pipe(pipe_fds) == -1) {
      MTCP_PRINTF("WARNING: error creating pipe. Compression will "
          "not be used.\n");
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
      MTCP_PRINTF("Not Reached!\n");
      mtcp_abort();
    }
  }

  return fd;
}

static int test_and_prepare_for_forked_ckpt()
{
#ifdef TEST_FORKED_CHECKPOINTING
  return 1;
#endif

  if (getenv("MTCP_FORKED_CHECKPOINT") == NULL) {
    return 0;
  }

  pid_t forked_cpid = mtcp_sys_fork();
  if (forked_cpid == -1) {
    MTCP_PRINTF("WARNING: Failed to do forked checkpointing,"
                " trying normal checkpoint\n");
    return FORKED_CKPT_FAILED;
  } else if (forked_cpid > 0) {
    // Calling mtcp_sys_waitpid here, but on 32-bit Linux, libc:waitpid()
    // calls wait4()
    if ( mtcp_sys_wait4(forked_cpid, NULL, 0, NULL) == -1 ) {
      DPRINTF("error mtcp_sys_wait4: errno: %d", mtcp_sys_errno);
    }
    DPRINTF("checkpoint complete\n");
    return FORKED_CKPT_PARENT;
  } else {
    pid_t grandchild_pid = mtcp_sys_fork();
    if (grandchild_pid == -1) {
      MTCP_PRINTF("WARNING: Forked checkpoint failed,"
                  " no checkpoint available\n");
    } else if (grandchild_pid > 0) {
      mtcp_sys_exit(0); /* child exits */
    }
    /* grandchild continues; no need now to mtcp_sys_wait4() on grandchild */
    DPRINTF("inside grandchild process\n");
  }
  return FORKED_CKPT_CHILD;
}

/* FIXME:
 * We should read /proc/self/maps into temporary array and mtcp_readmapsline
 * should then read from it.  This is cleaner than this hack here.
 * Then this body can go back to replacing:
 *    remap_nscd_areas_array[num_remap_nscd_areas++] = area;
 * - Gene
 */
static const int END_OF_NSCD_AREAS = -1;
static void remap_nscd_areas(Area remap_nscd_areas_array[],
			     int  num_remap_nscd_areas) {
  Area *area;
  for (area = remap_nscd_areas_array; num_remap_nscd_areas-- > 0; area++) {
    if (area->flags == END_OF_NSCD_AREAS) {
      MTCP_PRINTF("Too many NSCD areas to remap.\n");
      mtcp_abort();
    }
    if ( munmap(area->addr, area->size) == -1) {
      MTCP_PRINTF("error unmapping NSCD shared area: %s\n",
                  strerror(mtcp_sys_errno));
      mtcp_abort();
    }
    if (mmap(area->addr, area->size, area->prot, area->flags, 0, 0)
        == MAP_FAILED) {
      MTCP_PRINTF("error remapping NSCD shared area: %s\n", strerror(errno));
      mtcp_abort();
    }
    memset(area->addr, 0, area->size);
  }
}

static void write_header_and_restore_image(int fd, int fdCkptFileOnDisk)
{
  size_t num_written = 0;
  char tmpBuf[MTCP_PAGE_SIZE];
  mtcp_ckpt_image_hdr_t *ckpt_hdr;

  memset(tmpBuf, 0, sizeof(tmpBuf));

  memcpy(tmpBuf, MAGIC, MAGIC_LEN);
  ckpt_hdr = (mtcp_ckpt_image_hdr_t*) &tmpBuf[MAGIC_LEN];

  getrlimit(RLIMIT_STACK, &ckpt_hdr->stack_rlimit);
  ckpt_hdr->libmtcp_begin = mtcp_shareable_begin;
  ckpt_hdr->libmtcp_size = mtcp_shareable_end - mtcp_shareable_begin;
  ckpt_hdr->restore_start_fptr = (VA) restore_start_fptr;
  ckpt_hdr->restore_finish_fptr = (VA) restore_finish_fptr;

  DPRINTF("saved stack resource limit: soft_lim:%p, hard_lim:%p\n",
          ckpt_hdr->stack_rlimit.rlim_cur, ckpt_hdr->stack_rlimit.rlim_max);
  DPRINTF("restore_begin %X at %p from [libmtcp.so]\n",
          ckpt_hdr->libmtcp_size, ckpt_hdr->libmtcp_begin);

  num_written = mtcp_writefile(fd, tmpBuf, sizeof(tmpBuf));
  MTCP_ASSERT((num_written & MTCP_PAGE_OFFSET_MASK) == 0);

  num_written += mtcp_writefile(fd, ckpt_hdr->libmtcp_begin,
                                ckpt_hdr->libmtcp_size);
  MTCP_ASSERT((num_written & MTCP_PAGE_OFFSET_MASK) == 0);

  /* Write out file descriptors */
  num_written += writefiledescrs (fd, fdCkptFileOnDisk);
  if ((num_written & MTCP_PAGE_OFFSET_MASK) != 0) {
    num_written += mtcp_writefile(fd, tmpBuf, MTCP_PAGE_SIZE -
                                  (num_written & MTCP_PAGE_OFFSET_MASK));
  }
}

/* fd is file descriptor for gzip or other compression process */
static void write_ckpt_to_file(int fd, int fdCkptFileOnDisk)
{
  Area area;
  DeviceInfo dev_info;
  int stack_was_seen = 0;
  VA restore_begin = mtcp_shareable_begin;
  VA restore_end = mtcp_shareable_end;

  /* Drain stdin and stdout before checkpoint */
  tcdrain(STDOUT_FILENO);
  tcdrain(STDERR_FILENO);

  int vsyscall_exists = 0;
  // Preprocess special segments like vsyscall, stack, heap etc.
  preprocess_special_segments(&vsyscall_exists);

  write_header_and_restore_image(fd, fdCkptFileOnDisk);

  /* Finally comes the memory contents */

  /**************************************************************************/
  /* We can't do any more mallocing at this point because malloc stuff is   */
  /* outside the limits of the libmtcp.so image, so it won't get            */
  /* checkpointed, and it's possible that we would checkpoint an            */
  /* inconsistent state.  See note in restoreverything routine.             */
  /**************************************************************************/

  int num_remap_nscd_areas = 0;
  Area remap_nscd_areas_array[10];
  remap_nscd_areas_array[9].flags = END_OF_NSCD_AREAS;

  int mapsfd = mtcp_sys_open2 ("/proc/self/maps", O_RDONLY);

  while (mtcp_readmapsline (mapsfd, &area, &dev_info)) {
    VA area_begin = area.addr;
    VA area_end   = area_begin + area.size;

    /* Original comment:  Skip anything in kernel address space ---
     *   beats me what's at FFFFE000..FFFFFFFF - we can't even read it;
     * Added: That's the vdso section for earlier Linux 2.6 kernels.  For later
     *  2.6 kernels, vdso occurs at an earlier address.  If it's unreadable,
     *  then we simply won't copy it.  But let's try to read all areas, anyway.
     * **COMMENTED OUT:** if (area_begin >= HIGHEST_VA) continue;
     */
    /* If it's readable, but it's VDSO, it will be dangerous to restore it.
     * In 32-bit mode later Red Hat RHEL Linux 2.6.9 releases use 0xffffe000,
     * the last page of virtual memory.  Note 0xffffe000 >= HIGHEST_VA
     * implies we're in 32-bit mode.
     */
    if (area_begin >= HIGHEST_VA && area_begin == (VA)0xffffe000)
      continue;
#ifdef __x86_64__
    /* And in 64-bit mode later Red Hat RHEL Linux 2.6.9 releases
     * use 0xffffffffff600000 for VDSO.
     */
    if (area_begin >= HIGHEST_VA && area_begin == (VA)0xffffffffff600000)
      continue;
#endif

    /* Skip anything that has no read or execute permission.  This occurs
     * on one page in a Linux 2.6.9 installation.  No idea why.  This code
     * would also take care of kernel sections since we don't have read/execute
     * permission there.
     *
     * EDIT: We should only skip the "---p" section for the shared libraries.
     * Anonymous memory areas with no rwx permission should be saved regardless
     * as the process might have removed the permissions temporarily and might
     * want to use it later.
     *
     * This happens, for example, with libpthread where the pthread library
     * tries to recycle thread stacks. When a thread exits, libpthread will
     * remove the access permissions from the thread stack and later, when a
     * new thread is created, it will provide the proper permission to this
     * area and use it as the thread stack.
     *
     * If we do not restore this area on restart, the area might be returned by
     * some mmap() call. Later on, when pthread wants to use this area, it will
     * just try to use this area which now belongs to some other object. Even
     * worse, the other object can then call munmap() on that area after
     * libpthread started using it as thread stack causing the parts of thread
     * stack getting munmap()'d from the memory resulting in a SIGSEGV.
     *
     * We suspect that libpthread is using mmap() instead of mprotect to change
     * the permission from "---p" to "rw-p".
     */

    if (!((area.prot & PROT_READ) || (area.prot & PROT_WRITE)) &&
        area.name[0] != '\0') {
      continue;
    }

    if (mtcp_strstartswith(area.name, DEV_ZERO_DELETED_STR) ||
        mtcp_strstartswith(area.name, DEV_NULL_DELETED_STR)) {
      /* If the process has an area labelled as "/dev/zero (deleted)", we mark
       *   the area as Anonymous and save the contents to the ckpt image file.
       * If this area has a MAP_SHARED attribute, it should be replaced with
       *   MAP_PRIVATE and we won't do any harm because, the /dev/zero file is
       *   an absolute source and sink. Anything written to it will be
       *   discarded and anything read from it will be all zeros.
       * The following call to mmap will create "/dev/zero (deleted)" area
       *         mmap(addr, size, protection, MAP_SHARED | MAP_ANONYMOUS, 0, 0)
       *
       * The above explanation also applies to "/dev/null (deleted)"
       */
      DPRINTF("saving area \"%s\" as Anonymous\n", area.name);
      area.flags = MAP_PRIVATE | MAP_ANONYMOUS;
      area.name[0] = '\0';
    } else if (mtcp_strstartswith(area.name, SYS_V_SHMEM_FILE)) {
      DPRINTF("saving area \"%s\" as Anonymous\n", area.name);
      area.flags = MAP_PRIVATE | MAP_ANONYMOUS;
      area.name[0] = '\0';
    } else if (mtcp_strstartswith(area.name, NSCD_MMAP_STR1) ||
               mtcp_strstartswith(area.name, NSCD_MMAP_STR2) ||
               mtcp_strstartswith(area.name, NSCD_MMAP_STR3)) {
      /* Special Case Handling: nscd is enabled*/
      DPRINTF("NSCD daemon shared memory area present:  %s\n"
              "  MTCP will now try to remap this area in read/write mode as\n"
              "  private (zero pages), so that glibc will automatically\n"
              "  stop using NSCD or ask NSCD daemon for new shared area\n\n",
              area.name);
      area.prot = PROT_READ | PROT_WRITE;
      area.flags = MAP_PRIVATE | MAP_ANONYMOUS;

      /* We're still using proc-maps in mtcp_readmapsline();
       * So, remap NSCD later.
       */
      remap_nscd_areas_array[num_remap_nscd_areas++] = area;
    }

#ifdef IBV
    else if (mtcp_strstartswith(area.name, INFINIBAND_SHMEM_FILE)) {
      // TODO: Don't checkpoint infiniband shared area for now.
      continue;
    }
#endif
    else if (mtcp_strendswith(area.name, DELETED_FILE_SUFFIX)) {
      /* Deleted File */
    } else if (area.name[0] == '/' && mtcp_strstr(&area.name[1], "/") != NULL) {
      /* If an absolute pathname
       * Posix and SysV shared memory segments can be mapped as /XYZ
       */
      struct stat statbuf;
      unsigned int long devnum;
      if (stat(area.name, &statbuf) < 0) {
        MTCP_PRINTF("ERROR: error statting %s : %s\n",
                    area.name, strerror(errno));
      } else {
        devnum = makedev (dev_info.devmajor, dev_info.devminor);
        if (devnum != statbuf.st_dev || dev_info.inodenum != statbuf.st_ino) {
          MTCP_PRINTF("ERROR: image %s dev:inode %X:%u not eq maps %X:%u\n",
                      area.name, statbuf.st_dev, statbuf.st_ino,
                      devnum, dev_info.inodenum);
        }
      }
    }

    area.filesize = 0;
    if (area.name[0] != '\0') {
      int ffd = mtcp_sys_open(area.name, O_RDONLY, 0);
      if (ffd != -1) {
        area.filesize = mtcp_sys_lseek(ffd, 0, SEEK_END);
        if (area.filesize == -1)
          area.filesize = 0;
      }
      mtcp_sys_close(ffd);
    }

    /* Force the anonymous flag if it's a private writeable section, as the
     * data has probably changed from the contents of the original images.
     */

    /* We also do this for read-only private sections as it's possible
     * to modify a page there, too (via mprotect).
     */

    if ((area.flags & MAP_PRIVATE) /*&& (area.prot & PROT_WRITE)*/) {
      area.flags |= MAP_ANONYMOUS;
    }

    if ( area.flags & MAP_SHARED ) {
      /* invalidate shared memory pages so that the next read to it (when we are
       * writing them to ckpt file) will cause them to be reloaded from the
       * disk.
       */
      if ( msync(area.addr, area.size, MS_INVALIDATE) < 0 ){
        MTCP_PRINTF("error %d Invalidating %X at %p from %s + %X\n",
                     mtcp_sys_errno, area.size,
                     area.addr, area.name, area.offset);
        mtcp_abort();
      }
    }


    /* Only write this image if it is not CS_RESTOREIMAGE.
     * Skip any mapping for this image - it got saved as CS_RESTOREIMAGE
     * at the beginning.
     */

    if (area_begin < restore_begin) {
      if (area_end <= restore_begin) {
        // the whole thing is before the restore image
        writememoryarea (fd, &area, 0, vsyscall_exists);
      } else if (area_end <= restore_end) {
        // we just have to chop the end part off
        area.size = restore_begin - area_begin;
        writememoryarea (fd, &area, 0, vsyscall_exists);
      } else {
        // we have to write stuff that comes before restore image
        area.size = restore_begin - area_begin;
        writememoryarea (fd, &area, 0, vsyscall_exists);
        // ... and we have to write stuff that comes after restore image
        area.offset += restore_end - area_begin;
        area.size = area_end - restore_end;
        area.addr = restore_end;
        writememoryarea (fd, &area, 0, vsyscall_exists);
      }
    } else if (area_begin < restore_end) {
      if (area_end > restore_end) {
        // we have to write stuff that comes after restore image
        area.offset += restore_end - area_begin;
        area.size = area_end - restore_end;
        area.addr = restore_end;
        writememoryarea (fd, &area, 0, vsyscall_exists);
      }
    } else {
      if ( mtcp_strstr (area.name, "[stack]") )
        stack_was_seen = 1;
      // the whole thing comes after the restore image
      writememoryarea (fd, &area, stack_was_seen, vsyscall_exists);
    }
  }

  /* It's now safe to do this, since we're done using mtcp_readmapsline() */
  remap_nscd_areas(remap_nscd_areas_array, num_remap_nscd_areas);

  close (mapsfd);

  area.size = -1; // End of data
  mtcp_writefile(fd, &area, sizeof(area));

  /* That's all folks */
  if (mtcp_sys_close (fd) < 0) {
    MTCP_PRINTF("error closing checkpoint file: %s\n",
                strerror(errno));
    mtcp_abort ();
  }
}

/* True if the given FD should be checkpointed */
static int should_ckpt_fd (int fd)
{
   /* DMTCP policy is to never let MTCP checkpoint fd; DMTCP will do it. */
   if (mtcp_callback_ckpt_fd != NULL) {
     return (*mtcp_callback_ckpt_fd)(fd); //delegate to callback
   } else if (fd > 2) {
     return 1;
   } else {
     /* stdin/stdout/stderr */
     /* we only want to checkpoint these if they are from a file */
     struct stat statbuf;
     fstat(fd, &statbuf);
     return S_ISREG(statbuf.st_mode);
   }
}

/* Write list of open files to the ckpt file.  fd is file descriptor to gzip
 * or other compression process.  May or may not be same as fdCkptFileOnDisk */
static size_t writefiledescrs (int fd, int fdCkptFileOnDisk)
{
  Area area;
  size_t num_written = 0;
  char dbuf[BUFSIZ], linkbuf[FILENAMESIZE], *p, procfdname[64];
  int doff, dsiz, fddir, fdnum, linklen, rc;
  off_t offset;
  struct linux_dirent *dent;
  struct stat lstatbuf, statbuf;

  /* Open /proc/self/fd directory - it contains a list of files I have open */

  fddir = mtcp_sys_open ("/proc/self/fd", O_RDONLY, 0);
  if (fddir < 0) {
    MTCP_PRINTF("error opening directory /proc/self/fd: %s\n",
                strerror(mtcp_sys_errno));
    mtcp_abort ();
  }

  /* Check each entry */

  while (1) {
    dsiz = -1;
    if (sizeof dent -> d_ino == 4)
      dsiz = mtcp_sys_getdents (fddir, dbuf, sizeof dbuf);
    if (sizeof dent -> d_ino == 8)
      dsiz = mtcp_sys_getdents64 (fddir, dbuf, sizeof dbuf);
    if (dsiz <= 0) break;

    for (doff = 0; doff < dsiz; doff += dent -> d_reclen) {
      dent = (struct linux_dirent *) (dbuf + doff);

      /* The filename should just be a decimal number = the fd it represents.
       * Also, skip the entry for the checkpoint and directory files
       * as we don't want the restore to know about them.
       */

      fdnum = strtol (dent -> d_name, &p, 10);
      if ((*p == '\0') && (fdnum >= 0)
          && (fdnum != fd) && (fdnum != fdCkptFileOnDisk) && (fdnum != fddir)
          && (should_ckpt_fd (fdnum) > 0)) {

        // Read the symbolic link so we get the filename that's open on the fd
        sprintf (procfdname, "/proc/self/fd/%d", fdnum);
        linklen = readlink (procfdname, linkbuf, sizeof linkbuf - 1);
        if ((linklen >= 0) || (errno != ENOENT)) {
          // probably was the proc/self/fd directory itself
          if (linklen < 0) {
            MTCP_PRINTF("error reading %s: %s\n", procfdname, strerror(errno));
            mtcp_abort ();
          }
          linkbuf[linklen] = '\0';

          DPRINTF("checkpointing fd %d -> %s\n", fdnum, linkbuf);

          /* Read about the link itself so we know read/write open flags */

          rc = lstat (procfdname, &lstatbuf);
          if (rc < 0) {
            MTCP_PRINTF("error statting %s -> %s: %s\n",
                        procfdname, linkbuf, strerror(-rc));
            mtcp_abort ();
          }

          /* Read about the actual file open on the fd */

          rc = stat (linkbuf, &statbuf);
          if (rc < 0) {
            DPRINTF("error statting %s -> %s: %s\n",
                    procfdname, linkbuf, strerror(-rc));
          }

          /* Write state information to checkpoint file.
           * Replace file's permissions with current access flags
           * so restore will know how to open it.
           */

          else {
            offset = 0;
            if (S_ISREG (statbuf.st_mode))
              offset = mtcp_sys_lseek (fdnum, 0, SEEK_CUR);
            statbuf.st_mode = (statbuf.st_mode & ~0777)
              | (lstatbuf.st_mode & 0777);
            area.fdinfo.fdnum = fdnum;
            area.fdinfo.statbuf = statbuf;
            area.fdinfo.offset = offset;
            strcpy(area.name, linkbuf);
            num_written += mtcp_writefile(fd, &area, sizeof area);
          }
        }
      }
    }
  }
  if (dsiz < 0) {
    MTCP_PRINTF("error reading /proc/self/fd: %s\n", strerror(mtcp_sys_errno));
    mtcp_abort ();
  }

  mtcp_sys_close (fddir);

  /* Write end-of-fd-list marker to checkpoint file */

  area.fdinfo.fdnum = -1;
  linkbuf[0] = '\0';
  num_written += mtcp_writefile(fd, &area, sizeof area);
  return num_written;
}

/* This function detects if the given pages are zero pages or not. There is
 * scope of improving this function using some optimizations.
 *
 * TODO: One can use /proc/self/pagemap to detect if the page is backed by a
 * shared zero page.
 */
static int mtcp_are_zero_pages(void *addr, size_t num_pages)
{
  long long *buf = (long long*) addr;
  size_t i;
  size_t end = num_pages * MTCP_PAGE_SIZE / sizeof (*buf);
  long long res = 0;
  for (i = 0; i + 7 < end; i += 8) {
    res = buf[i+0] | buf[i+1] | buf[i+2] | buf[i+3] |
          buf[i+4] | buf[i+5] | buf[i+6] | buf[i+7];
    if (res != 0) {
      break;
    }
  }
  return res == 0;
}


/* This function returns a range of zero or non-zero pages. If the first page
 * is non-zero, it searches for all contiguous non-zero pages and returns them.
 * If the first page is all-zero, it searches for contiguous zero pages and
 * returns them.
 */
#if 1
// Remove the else part once satisfied with the this code
static void mtcp_get_next_page_range(Area *area, size_t *size, int *is_zero)
{
  char *pg;
  char *prevAddr;
  size_t count = 0;
  const size_t one_MB = (1024 * 1024);
  if (area->size < one_MB) {
    *size = area->size;
    *is_zero = 0;
    return;
  }
  *size = one_MB;
  *is_zero = mtcp_are_zero_pages(area->addr, one_MB / MTCP_PAGE_SIZE);
  prevAddr = area->addr;
  for (pg = area->addr + one_MB;
       pg < area->addr + area->size;
       pg += one_MB) {
    size_t minsize = MIN(one_MB, area->addr + area->size - pg);
    if (*is_zero != mtcp_are_zero_pages(pg, minsize / MTCP_PAGE_SIZE)) {
      break;
    }
    *size += minsize;
    if (*is_zero && ++count % 10 == 0) { // madvise every 10MB
      if (madvise(prevAddr, area->addr + *size - prevAddr,
                  MADV_DONTNEED) == -1) {
        MTCP_PRINTF("error %d doing madvise(%p, %d, MADV_DONTNEED)\n",
                    errno, area->addr, (int)*size);
        prevAddr = pg;
      }
    }
  }
}
#else
static void mtcp_get_next_page_range(Area *area, size_t *size, int *is_zero)
{
  char *pg;
  size_t count = 0;
  *size = MTCP_PAGE_SIZE;
  *is_zero = mtcp_are_zero_pages(area->addr);
  for (pg = area->addr + MTCP_PAGE_SIZE;
       pg < area->addr + area->size;
       pg += MTCP_PAGE_SIZE) {
    if (*is_zero != mtcp_are_zero_pages(pg)) {
      break;
    }
    *size += MTCP_PAGE_SIZE;
    if (*is_zero && ++count % 1024 == 0) {
      if (madvise(a.addr, a.size, MADV_DONTNEED) == -1) {
        MTCP_PRINTF("error %d doing madvise(%p, %d, MADV_DONTNEED)\n",
                    errno, a.addr, (int)a.size);
      }
    }
  }
}
#endif

static void mtcp_write_non_rwx_and_anonymous_pages(int fd, Area *orig_area)
{
  Area area = *orig_area;
  /* Now give read permission to the anonymous pages that do not have read
   * permission. We should remove the permission as soon as we are done
   * writing the area to the checkpoint image
   *
   * NOTE: Changing the permission here can results in two adjacent memory
   * areas to become one (merged), if they have similar permissions. This can
   * results in a modified /proc/self/maps file. We shouldn't get affected by
   * the changes because we are going to remove the PROT_READ later in the
   * code and that should reset the /proc/self/maps files to its original
   * condition.
   */
  if (orig_area->name[0] != '\0') {
    MTCP_PRINTF("NOTREACHED\n");
    mtcp_abort();
  }

  if ((orig_area->prot & PROT_READ) == 0) {
    if (mtcp_sys_mprotect(orig_area->addr, orig_area->size,
                          orig_area->prot | PROT_READ) < 0) {
      MTCP_PRINTF("error %d adding PROT_READ to %p bytes at %p\n",
                  mtcp_sys_errno, orig_area->size, orig_area->addr);
      mtcp_abort();
    }
  }

  while (area.size > 0) {
    size_t size;
    int is_zero;
    Area a = area;

    mtcp_get_next_page_range(&a, &size, &is_zero);

    a.prot |= is_zero ? MTCP_PROT_ZERO_PAGE : 0;
    a.size = size;

    mtcp_writefile(fd, &a, sizeof(a));
    if (!is_zero) {
      mtcp_writefile(fd, a.addr, a.size);
    } else {
      if (madvise(a.addr, a.size, MADV_DONTNEED) == -1) {
        MTCP_PRINTF("error %d doing madvise(%p, %d, MADV_DONTNEED)\n",
                    errno, a.addr, (int)a.size);
      }
    }
    area.addr += size;
    area.size -= size;
  }

  /* Now remove the PROT_READ from the area if it didn't have it originally
  */
  if ((orig_area->prot & PROT_READ) == 0) {
    if (mtcp_sys_mprotect(orig_area->addr, orig_area->size,
                          orig_area->prot) < 0) {
      MTCP_PRINTF("error %d removing PROT_READ from %p bytes at %p\n",
                  mtcp_sys_errno, orig_area->size, orig_area->addr);
      mtcp_abort ();
    }
  }
}

static void writememoryarea (int fd, Area *area, int stack_was_seen,
			     int vsyscall_exists)
{
  static void * orig_stack = NULL;

  /* Write corresponding descriptor to the file */

  if (orig_stack == NULL && 0 == mtcp_strcmp(area -> name, "[stack]"))
    orig_stack = area -> addr + area -> size;

  if (0 == mtcp_strcmp(area -> name, "[vdso]") && !stack_was_seen)
    DPRINTF("skipping over [vdso] section"
            " %p at %p\n", area -> size, area -> addr);
  else if (0 == mtcp_strcmp(area -> name, "[vsyscall]") && !stack_was_seen)
    DPRINTF("skipping over [vsyscall] section"
    	    " %p at %p\n", area -> size, area -> addr);
  else if (0 == mtcp_strcmp(area -> name, "[vectors]") && !stack_was_seen)
    DPRINTF("skipping over [vectors] section"
    	    " %p at %p\n", area -> size, area -> addr);
  else if (0 == mtcp_strcmp(area -> name, "[stack]") &&
	   orig_stack != area -> addr + area -> size)
    /* Kernel won't let us munmap this.  But we don't need to restore it. */
    DPRINTF("skipping over [stack] segment"
            " %X at %pi (not the orig stack)\n", area -> size, area -> addr);
  else if (!(area -> flags & MAP_ANONYMOUS))
    DPRINTF("save %p at %p from %s + %X\n",
            area -> size, area -> addr, area -> name, area -> offset);
  else if (area -> name[0] == '\0')
    DPRINTF("save anonymous %p at %p\n", area -> size, area -> addr);
  else DPRINTF("save anonymous %p at %p from %s + %X\n",
               area -> size, area -> addr, area -> name, area -> offset);

  if ((area -> name[0]) == '\0') {
    char *brk = mtcp_sys_brk(NULL);
    if (brk > area -> addr && brk <= area -> addr + area -> size)
      mtcp_strcpy(area -> name, "[heap]");
  }

  if (area->prot == 0 ||
      (area->name[0] == '\0' &&
       ((area->flags & MAP_ANONYMOUS) != 0) &&
       ((area->flags & MAP_PRIVATE) != 0))) {
    /* Detect zero pages and do not write them to ckpt image.
     * Currently, we detect zero pages in non-rwx mapping and anonymous
     * mappings only
     */
    mtcp_write_non_rwx_and_anonymous_pages(fd, area);
  } else if (0 != mtcp_strcmp(area -> name, "[vsyscall]")
             && 0 != mtcp_strcmp(area -> name, "[vectors]")
             && ((0 != mtcp_strcmp(area -> name, "[vdso]")
                  || vsyscall_exists /* which implies vdso can be overwritten */
                  || !stack_was_seen))) /* If vdso appeared before stack, it can be
                                         replaced */
  {
    /* Anonymous sections need to have their data copied to the file,
     *   as there is no file that contains their data
     * We also save shared files to checkpoint file to handle shared memory
     *   implemented with backing files
     */
    if (area -> flags & MAP_ANONYMOUS || area -> flags & MAP_SHARED) {
      mtcp_writefile(fd, area, sizeof(*area));
      mtcp_writefile(fd, area->addr, area->size);
    } else {
      MTCP_PRINTF("UnImplemented");
      mtcp_abort();
    }
  }
}

static void preprocess_special_segments(int *vsyscall_exists)
{
  Area area;
  int mapsfd = mtcp_sys_open2 ("/proc/self/maps", O_RDONLY);
  if (mapsfd < 0) {
    MTCP_PRINTF("error opening /proc/self/maps: %s\n",
                strerror(mtcp_sys_errno));
    mtcp_abort ();
  }

  while (mtcp_readmapsline (mapsfd, &area, NULL)) {
    if (0 == mtcp_strcmp(area.name, "[vsyscall]")) {
      /* Determine if [vsyscall] exists.  If [vdso] and [vsyscall] exist,
       * [vdso] will be saved and restored.
       * NOTE:  [vdso] is relocated if /proc/sys/kernel/randomize_va_space == 2.
       * We must restore old [vdso] and also keep [vdso] in that case.
       * On Linux 2.6.25:
       *   32-bit Linux has:  [heap], /lib/ld-2.7.so, [vdso], libs, [stack].
       *   64-bit Linux has:  [stack], [vdso], [vsyscall].
       * and at least for gcl, [stack], libmtcp.so, [vsyscall] seen.
       * If 32-bit process in 64-bit Linux:
       *     [stack] (0xffffd000), [vdso] (0xffffe0000)
       * On 32-bit Linux, mtcp_restart has [vdso], /lib/ld-2.7.so, [stack]
       * Need to restore old [vdso] into mtcp_restart, to restart.
       * With randomize_va_space turned off, libraries start at high address
       *     0xb8000000 and are loaded progressively at lower addresses.
       * mtcp_restart loads vdso (which looks like a shared library) first.
       * But libpthread/libdl/libc libraries are loaded above vdso in user
       * image.
       * So, we must use the opposite of the user's setting (no randomization if
       *     user turned it on, and vice versa).  We must also keep the
       *     new vdso segment, provided by mtcp_restart.
       */
      *vsyscall_exists = 1;
    } else if (!mtcp_saved_heap_start && mtcp_strcmp(area.name, "[heap]") == 0) {
      // Record start of heap which will later be used in mtcp_restore_finish()
      mtcp_saved_heap_start = area.addr;
    } else if (mtcp_strcmp(area.name, "[stack]") == 0) {
      /*
       * When using Matlab with dmtcp_checkpoint, sometimes the bottom most
       * page of stack (the page with highest address) which contains the
       * environment strings and the argv[] was not shown in /proc/self/maps.
       * This is arguably a bug in the Linux kernel as of version 2.6.32, etc.
       * This happens on some odd combination of environment passed on to
       * Matlab process. As a result, the page was not checkpointed and hence
       * the process segfaulted on restart. The fix is to try to mprotect this
       * page with RWX permission to make the page visible again. This call
       * will fail if no stack page was invisible to begin with.
       */
      // FIXME : If the area following the stack is not empty, dont exercise this path
      int ret = mprotect(area.addr + area.size, 0x1000,
                         PROT_READ | PROT_WRITE | PROT_EXEC);
      if (ret == 0) {
        MTCP_PRINTF("bottom-most page of stack (page with highest address)\n"
                    "  was invisible in /proc/self/maps.\n"
                    "  It is made visible again now.\n");
      }
    }
  }
  close(mapsfd);
}

