/*****************************************************************************
 *   Copyright (C) 2006-2008 by Michael Rieker, Jason Ansel, Kapil Arya, and *
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

/*****************************************************************************
 *
 *  This command-line utility is what a user uses to perform a restore
 *  It reads the given checkpoint file into memory then jumps to it, thus being
 *  just like the original program was restarted from the last
 *  checkpoint.
 *
 *  It is also used by the checkpoint verification to perform a restore while
 *  the original application program is running, to make sure the restore works.
 *  The --verify option tells it to rename the checkpoint file, removing the
 *  .temp from the end.
 *
 *****************************************************************************/

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>

#include "mtcp_internal.h"

#include <sys/personality.h>

static char first_char(char *filename);
static int open_ckpt_to_read(char *filename, char *envp[]);

static pid_t decomp_child_pid = -1;

extern int dmtcp_info_stderr_fd;

//shift args
#define shift argc--,argv++

static const char* theUsage =
  "USAGE:\n"
  "mtcp_restart [--verify] <ckeckpointfile>\n\n"
  "mtcp_restart [--offset <offset-in-bytes>] [--stderr-fd <fd>] [--]"
      " <ckeckpointfile>\n\n"
  "mtcp_restart [--fd <ckpt-fd>] [--gzip-child-pid <pid>]"
      " [--rename-ckpt <newname>] [--stderr-fd <fd>]\n\n"
;

char **environ = NULL;
int main (int argc, char *argv[], char *envp[])
{
  char magicbuf[MAGIC_LEN], *restorename;
  int fd, verify;
  size_t restore_size, offset=0;
  void *restore_begin, *restore_mmap;
  void (*restore_start) (int fd, int verify, pid_t decomp_child_pid,
                         char *ckpt_newname, char *cmd_file,
                         char *argv[], char *envp[]);
  char cmd_file[PATH_MAX+1];
  char ckpt_newname[PATH_MAX+1] = "";
  char **orig_argv = argv;
  int orig_argc = argc;
  environ = envp;

  if (mtcp_sys_getuid() == 0 || mtcp_sys_geteuid() == 0) {
    mtcp_printf("Running mtcp_restart as root is dangerous.  Aborting.\n" \
	   "If you still want to do this (at your own risk)," \
	   "  then modify mtcp/%s:%d and re-compile.\n",
	   __FILE__, __LINE__ - 4);
    mtcp_abort();
  }

  // Turn off randomize_va (by re-exec'ing) or warn user if vdso_enabled is on.
  mtcp_check_vdso_enabled();

  fd = decomp_child_pid = -1;
  verify = 0;

  shift;
  while (1) {
    if (argc == 0 || (mtcp_strcmp(argv[0], "--help") == 0 && argc == 1)) {
      mtcp_printf("%s", theUsage);
      return (-1);
    } else if (mtcp_strcmp (argv[0], "--verify") == 0 && argc == 2) {
      verify = 1;
      restorename = argv[1];
      break;
    } else if (mtcp_strcmp (argv[0], "--offset") == 0 && argc >= 3) {
      offset = mtcp_atoi(argv[1]);
      shift; shift;
    } else if (mtcp_strcmp (argv[0], "--fd") == 0 && argc >= 2) {
      fd = mtcp_atoi(argv[1]);
      shift; shift;
    } else if (mtcp_strcmp (argv[0], "--gzip-child-pid") == 0 && argc >= 2) {
      decomp_child_pid = mtcp_atoi(argv[1]);
      shift; shift;
    } else if (mtcp_strcmp (argv[0], "--rename-ckpt") == 0 && argc >= 2) {
      mtcp_strncpy(ckpt_newname, argv[1], PATH_MAX);
      shift; shift;
    } else if (mtcp_strcmp (argv[0], "--stderr-fd") == 0 && argc >= 2) {
      // If using with DMTCP/jassert, Pass in a non-standard stderr
      dmtcp_info_stderr_fd = mtcp_atoi(argv[1]);
      shift; shift;
    } else if (mtcp_strcmp (argv[0], "--") == 0 && argc == 2) {
      restorename = argv[1];
      break;
    } else if (argc == 1) {
      restorename = argv[0];
      break;
    } else {
      mtcp_printf("%s", theUsage);
      return (-1);
    }
  }
  // Restore argc and argv pointer
  argv = orig_argv;
  argc = orig_argc;

  /* XXX XXX XXX:
   *    DO NOT USE mtcp_printf OR DPRINTF BEFORE THIS BLOCK, IT'S DANGEROUS AND
   *    CAN MESS UP YOUR PROCESSES BY WRITING GARBAGE TO THEIR STDERR FD,
   *    IF THEY ARE NOT USING IT AS STDERR.
   *                                                                   --Kapil
   */

  if (fd != -1 && decomp_child_pid != -1) {
    restorename = NULL;
  } else if ((fd == -1 && decomp_child_pid != -1) ||
             (offset != 0 && fd != -1)) {
    mtcp_printf("%s", theUsage);
    return (-1);
  }

  if (restorename) {
#if 1
    if (mtcp_sys_access(restorename, R_OK) != 0 && mtcp_sys_errno == EACCES) {
      MTCP_PRINTF("\nProcess does not have read permission for\n" \
	          "  checkpoint image (%s).\n" \
                  "  (Check file permissions, UIDs etc.)\n",
                  restorename);
      mtcp_abort();
    }
#else
    struct stat buf;
    int rc = mtcp_sys_stat(restorename, &buf);
    if (rc == -1) {
      MTCP_PRINTF("Error %d stat()'ing ckpt image %s.",
                  mtcp_sys_errno, restorename);
      mtcp_abort();
    } else if (buf.st_uid != mtcp_sys_getuid()) { /*Could also run if geteuid()
                                                    matches*/
      MTCP_PRINTF("\nProcess uid (%d) doesn't match uid (%d) of\n" \
	          "  checkpoint image (%s).\n" \
		  "This is dangerous.  Aborting for security reasons.\n" \
                  "If you still want to do this, modify mtcp/%s:%d and"
                  "  re-compile.\n",
                  mtcp_sys_getuid(), buf.st_uid, restorename,
                  __FILE__, __LINE__ - 5);
      mtcp_abort();
    }
#endif
  }

  if (mtcp_strlen(ckpt_newname) == 0 && restorename != NULL && offset != 0) {
    mtcp_strncpy(ckpt_newname, restorename, PATH_MAX);
  }

  if (restorename!=NULL) fd = open_ckpt_to_read(restorename, envp);
  if (offset>0) {
    //skip into the file a bit
    VA addr = (VA) mtcp_sys_mmap(0, offset, PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) {
      MTCP_PRINTF("mmap failed with error %d\n", mtcp_sys_errno);
      mtcp_abort();
    }
    mtcp_readfile(fd, addr, offset);
    if (mtcp_sys_munmap(addr, offset) == -1) {
      MTCP_PRINTF("munmap failed with error %d\n", mtcp_sys_errno);
      mtcp_abort();
    }
  }
  mtcp_memset(magicbuf, 0, sizeof magicbuf);
  mtcp_readfile (fd, magicbuf, MAGIC_LEN);
  if (mtcp_memcmp (magicbuf, MAGIC, MAGIC_LEN) != 0) {
    MTCP_PRINTF("'%s' is '%s', but this restore is '%s' (fd=%d)\n",
                restorename, magicbuf, MAGIC, fd);
    return (-1);
  }

  /* Set the resource limits for stack from saved values */
  struct rlimit stack_rlimit;

#ifdef FAST_CKPT_RST_VIA_MMAP
  Area area;
  fastckpt_read_header(fd, &stack_rlimit, &area, (VA*) &restore_start);
  restore_begin = area.addr;
  restore_size = area.size;
#else
  mtcp_readcs (fd, CS_STACKRLIMIT); /* resource limit for stack */
  mtcp_readfile (fd, &stack_rlimit, sizeof stack_rlimit);
  /* Find where the restore image goes */
  mtcp_readcs (fd, CS_RESTOREBEGIN); /* beginning of checkpointed libmtcp.so image */
  mtcp_readfile (fd, &restore_begin, sizeof restore_begin);
  mtcp_readcs (fd, CS_RESTORESIZE); /* size of checkpointed libmtcp.so image */
  mtcp_readfile (fd, &restore_size, sizeof restore_size);
  mtcp_readcs (fd, CS_RESTORESTART);
  mtcp_readfile (fd, &restore_start, sizeof restore_start);

  DPRINTF("saved stack resource limit: soft_lim:%p, hard_lim:%p\n",
          stack_rlimit.rlim_cur, stack_rlimit.rlim_max);

#endif // FAST_CKPT_RST_VIA_MMAP
  mtcp_sys_setrlimit(RLIMIT_STACK, &stack_rlimit);

  /* Read in the restore image to same address where it was loaded at time
   *  of checkpoint.  This is libmtcp.so, including both text and data sections
   *  as a single section.  Hence, we need both write and exec permission,
   *  and MAP_ANONYMOUS, since the data could have changed.
   */

  DPRINTF("restoring anonymous area %p at %p\n", restore_size, restore_begin);

  if (mtcp_sys_munmap(restore_begin, restore_size) < 0) {
    MTCP_PRINTF("failed to unmap region at %p\n", restore_begin);
    mtcp_abort ();
  }

#ifdef FAST_CKPT_RST_VIA_MMAP
  fastckpt_load_restore_image(fd, &area);
#else
  restore_mmap = mtcp_safemmap (restore_begin, restore_size,
                                PROT_READ | PROT_WRITE | PROT_EXEC,
                                MAP_ANONYMOUS | MAP_FIXED | MAP_PRIVATE, -1, 0);
  if (restore_mmap == MAP_FAILED) {
#ifndef _XOPEN_UNIX
    MTCP_PRINTF("Does mmap here support MAP_FIXED?\n");
#endif
    if (mtcp_sys_errno != EBUSY) {
      MTCP_PRINTF("Error %d creating %p byte restore region at %p.\n",
                  mtcp_sys_errno, restore_size, restore_begin);
      mtcp_abort ();
    } else {
      MTCP_PRINTF("restarting due to address conflict...\n");
      mtcp_sys_close (fd);
      mtcp_sys_execve (argv[0], argv, envp);
    }
  }
  if (restore_mmap != restore_begin) {
    MTCP_PRINTF("%p byte restore region at %p got mapped at %p\n",
                restore_size, restore_begin, restore_mmap);
    mtcp_abort ();
  }
  mtcp_readcs (fd, CS_RESTOREIMAGE);
  mtcp_readfile (fd, restore_begin, restore_size);
#endif

#ifndef __x86_64__
  // Copy command line to libmtcp.so, so that we can re-exec if randomized vdso
  //   steps on us.  This won't be needed when we use the linker to map areas.
  cmd_file[0] = '\0';
  { int cmd_len = mtcp_sys_readlink("/proc/self/exe", cmd_file, PATH_MAX);
    if (cmd_len == -1)
      MTCP_PRINTF("WARNING:  Couldn't find /proc/self/exe."
		  "  Trying to continue anyway.\n");
    else
      cmd_file[cmd_len] = '\0';
  }
#endif

#ifdef LIBC_STATIC_AVAILABLE
/********************************************************************
 * Apparently, there is no consistent way to define LIBC_STATIC_AVAILABLE.
 * The purpose of the code below is to be able to use a symbolic debugger
 * like gdb when dmtcp_restart calls MTCP.  It would print a command that
 * you can paste into gdb to allow debugging inside the function restore_start.
 * When "ifdef LIBC_STATIC_AVAILABLE" was added, mtcp/Makefile was modified
 * to forbid using functions from libc.a like popen.  If you want to debug
 * restore_start(), you should read the code below and manually calculate
 * by hand what this used to automatically calculate.  For a semi-automated
 * substitute, when you reach restore_start(), call it with (gdb) si
 * Then try:  (gdb) shell ../utils/gdb-add-libmtcp-symbol-file.py
 * where ADDR will be restore_start or an arb. address in restore_start()
 ********************************************************************/
#if defined(DEBUG) || defined(DMTCP_DEBUG)
  char *p, symbolbuff[256];
  FILE *symbolfile;
  long textbase; /* offset */

  MTCP_PRINTF("restore_begin=%p, restore_start=%p\n",
      	restore_begin, restore_start);
  textbase = 0;

  symbolfile = popen ("readelf -S libmtcp.so", "r");
  if (symbolfile != NULL) {
    while (fgets (symbolbuff, sizeof symbolbuff, symbolfile) != NULL) {
      if (memcmp (symbolbuff + 5, "] .text ", 8) == 0) {
        textbase = strtoul (symbolbuff + 41, &p, 16);
      }
    }
    pclose (symbolfile);
    if (textbase != 0) {
      mtcp_printf("\n**********\nmtcp_restart*: The symbol table of the"
      	 " checkpointed file can be\nmade available to gdb."
      	 "  Just type the command below in gdb:\n");
      mtcp_printf("     add-symbol-file libmtcp.so %p\n",
               restore_begin + textbase);
      mtcp_printf("Then type \"continue\" to continue debugging.\n");
      mtcp_printf("**********\n");
    }
  }
  mtcp_maybebpt ();
#endif
#endif

  /* Now call it - it shouldn't return */
  (*restore_start) (fd, verify, decomp_child_pid, ckpt_newname, cmd_file, argv, envp);
  MTCP_PRINTF("restore routine returned (it should never do this!)\n");
  mtcp_abort ();
  return (0);
}

int __libc_start_main (int (*main) (int, char **, char **),
                       int argc, char **argv,
                       void (*init) (void), void (*fini) (void),
                       void (*rtld_fini) (void), void *stack_end)
{
  char **envp = argv + argc + 1;
  int result = main (argc, argv, envp);
  mtcp_sys_exit(result);
  while(1);
}


void
__libc_csu_init (int argc, char **argv, char **envp)
{
  while(1);
}

/* This function should not be used anymore.  We run the executable's
   destructor now just like any other.  We cannot remove the function,
   though.  */
void __libc_csu_fini (void)
{
}

/**
 * This function will return the first character of the given file.  If the
 * file is not readable, we will abort.
 *
 * @param filename the name of the file to read
 * @return the first character of the given file
 */
static char first_char(char *filename)
{
    int fd, rc;
    char c;

    fd = mtcp_sys_open(filename, O_RDONLY, 0);
    if(fd < 0)
    {
        MTCP_PRINTF("ERROR: Cannot open file %s\n", filename);
        mtcp_abort();
    }

    rc = mtcp_sys_read(fd, &c, 1);
    if(rc != 1)
    {
        MTCP_PRINTF("ERROR: Error reading from file %s\n", filename);
        mtcp_abort();
    }

    mtcp_sys_close(fd);
    return c;
}

/**
 * This function will open the checkpoint file stored at the given filename.
 * It will check the magic number and take the appropriate action.  If the
 * magic number is unknown, we will abort.  The fd returned points to the
 * beginning of the uncompressed data.
 * NOTE: related code in ../dmtcp/src/connectionmanager.cpp:open_ckpt_to_read()
 *
 * @param filename the name of the checkpoint file
 * @return the fd to use
 */
static int open_ckpt_to_read(char *filename, char *envp[])
{
  int fd;
  int fds[2];
  char fc;
  char *gzip_cmd = "gzip";
  static char *gzip_args[] = { "gzip", "-d", "-", NULL };
#ifdef HBICT_DELTACOMP
  char *hbict_cmd = "hbict";
  static char *hbict_args[] = { "hbict", "-r", NULL };
#endif
  char decomp_path[PATH_MAX];
  static char **decomp_args;
  pid_t cpid;

  fc = first_char(filename);
  fd = mtcp_sys_open(filename, O_RDONLY, 0);
  if(fd < 0) {
    MTCP_PRINTF("ERROR: Cannot open checkpoint file %s\n", filename);
    mtcp_abort();
  }

  if (fc == MAGIC_FIRST || fc == 'D') /* no compression ('D' from DMTCP) */
    return fd;
  else if (fc == GZIP_FIRST
#ifdef HBICT_DELTACOMP
           || fc == HBICT_FIRST
#endif
          ) { /* Set prog_path */
    if( fc == GZIP_FIRST ){
      decomp_args = gzip_args;
      if( mtcp_find_executable(gzip_cmd, getenv("PATH"),
                               decomp_path) == NULL ) {
        MTCP_PRINTF("ERROR: Cannot find gunzip to decompress ckpt file!\n");
        mtcp_abort();
      }
    }
#ifdef HBICT_DELTACOMP
    if( fc == HBICT_FIRST ){
      decomp_args = hbict_args;
      if( mtcp_find_executable(hbict_cmd, getenv("PATH"), 
                              decomp_path) == NULL ) {
        MTCP_PRINTF("ERROR: Cannot find hbict to decompress ckpt file!\n");
        mtcp_abort();
      }
    }
#endif
    if (mtcp_sys_pipe(fds) == -1) {
      MTCP_PRINTF("ERROR: Cannot create pipe to execute gunzip to decompress"
                  " checkpoint file!\n");
      mtcp_abort();
    }

    cpid = mtcp_sys_fork();

    if(cpid == -1) {
      MTCP_PRINTF("ERROR: Cannot fork to execute gunzip to decompress"
                  " checkpoint file!\n");
      mtcp_abort();
    }
    else if(cpid > 0) /* parent process */ {
      decomp_child_pid = cpid;
      mtcp_sys_close(fd);
      mtcp_sys_close(fds[1]);
      return fds[0];
    }
    else /* child process */ {
      fd = mtcp_sys_dup(mtcp_sys_dup(mtcp_sys_dup(fd)));
      if (fd == -1) {
        MTCP_PRINTF("ERROR: dup() failed!  No restoration will be performed!"
                    " Cancel now!\n");
        mtcp_abort();
      }
      fds[1] = mtcp_sys_dup(fds[1]);
      mtcp_sys_close(fds[0]);
      if (mtcp_sys_dup2(fd, STDIN_FILENO) != STDIN_FILENO) {
        MTCP_PRINTF("ERROR: dup2() failed!  No restoration will be performed!"
                    " Cancel now!\n");
        mtcp_abort();
      }
      mtcp_sys_close(fd);
      mtcp_sys_dup2(fds[1], STDOUT_FILENO);
      mtcp_sys_close(fds[1]);
      mtcp_sys_execve(decomp_path, decomp_args, envp);
      /* should not get here */
      MTCP_PRINTF("ERROR: Decompression failed!  No restoration will be"
                  " performed!  Cancel now!\n");
      mtcp_abort();
    }
  }
  else /* invalid magic number */ {
    MTCP_PRINTF("ERROR: Invalid magic number in this checkpoint file!\n");
    mtcp_abort();
  }
}

char* getenv(const char* name)
{
  int i;
  extern char **environ;
  ssize_t len = mtcp_strlen(name);

  if (environ == NULL)
    return NULL;

  for (i = 0; environ[i] != NULL; i++) {
    if (mtcp_strstartswith(environ[i], name)) {
      if (mtcp_strlen(environ[i]) > len && environ[i][len] == '=') {
        if (environ[i][len+1] == '\0') return NULL;
        return &(environ[i][len+1]);
      }
    }
  }
  return NULL;
}

void *memcpy(void *dest, const void *src, size_t n)
{
  mtcp_strncpy(dest, src, n);
  return dest;
}

/* Implement memcpy() and memset() inside mtcp_restart. Although we are not
 * calling memset, the compiler may generate a call to memset() when trying to
 * initialize a large array etc.
 */
void *memset(void *s, int c, size_t n)
{
  while (n-- > 0) {
    *(char*)s = (char)c;
  }
  return s;
}

void __stack_chk_fail(void)
{
  MTCP_PRINTF("ERROR: Stack Overflow detected.\n");
  mtcp_abort();
}
