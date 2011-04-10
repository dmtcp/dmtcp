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

/********************************************************************************************************************************/
/*																*/
/*  This command-line utility is what a user uses to perform a restore								*/
/*  It reads the given checkpoint file into memory then jumps to it, thus being just like the original program was restarted 	*/
/*  from the last checkpoint.													*/
/*																*/
/*  It is also used by the checkpoint verification to perform a restore while the original application program is running, to 	*/
/*  make sure the restore works.  The --verify option tells it to rename the checkpoint file, removing the .temp from the end.	*/
/*																*/
/********************************************************************************************************************************/

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
static int open_ckpt_to_read(char *filename);
static void readcs (int fd, char cs);
static void readfile (int fd, void *buf, size_t size);

static pid_t decomp_child_pid = -1;
pid_t saved_pid = 0;

extern int dmtcp_info_stderr_fd;

//shift args
#define shift argc--,argv++

static const char* theUsage =
  "USAGE:\n"
  "mtcp_restart [--verify] <ckeckpointfile>\n\n"
  "mtcp_restart [--offset <offset-in-bytes>] [--stderr-fd <fd>] [--] <ckeckpointfile>\n\n"
  "mtcp_restart [--fd <ckpt-fd>] [--gzip-child-pid <pid>] [--rename-ckpt <newname>] [--stderr-fd <fd>]\n\n"
;

int main (int argc, char *argv[], char *envp[])
{
  char magicbuf[MAGIC_LEN], *restorename;
  int fd, verify;
  size_t restore_size, offset=0;
  void *restore_begin, *restore_mmap;
  void (*restore_start) (int fd, int verify, pid_t decomp_child_pid,
                         char *ckpt_newname, char *cmd_file,
                         char *argv[], char *envp[]);
  char cmd_file[MAXPATHLEN+1];
  char ckpt_newname[MAXPATHLEN+1] = "";
  char **orig_argv = argv;
  int orig_argc = argc;

  if (getuid() == 0 || geteuid() == 0) {
    mtcp_printf("Running mtcp_restart as root is dangerous.  Aborting.\n" \
	   "If you still want to do this (at your own risk)," \
	   "  then modify mtcp/%s:%d and re-compile.\n",
	   __FILE__, __LINE__ - 4);
    abort();
  }

  /* Turn off randomize_va (by re-exec'ing) or warn user if vdso_enabled is on. */
  mtcp_check_vdso_enabled();

  fd = decomp_child_pid = -1;
  verify = 0;

  shift;
  while (1) {
    if (argc == 0 || (strcasecmp(argv[0], "--help") == 0 && argc == 1)) {
      mtcp_printf("%s", theUsage);
      return (-1);
    } else if (strcasecmp (argv[0], "--verify") == 0 && argc == 2) {
      verify = 1;
      restorename = argv[1];
      break;
    } else if (strcasecmp (argv[0], "--offset") == 0 && argc >= 3) {
      offset = atoi(argv[1]);
      shift; shift;
    } else if (strcasecmp (argv[0], "--fd") == 0 && argc >= 2) {
      fd = atoi(argv[1]);
      shift; shift;
    } else if (strcasecmp (argv[0], "--gzip-child-pid") == 0 && argc >= 2) {
      decomp_child_pid = atoi(argv[1]);
      shift; shift;
    } else if (strcasecmp (argv[0], "--rename-ckpt") == 0 && argc >= 2) {
      strncpy(ckpt_newname, argv[1], MAXPATHLEN);
      shift; shift;
    } else if (strcasecmp (argv[0], "--stderr-fd") == 0 && argc >= 2) {
      dmtcp_info_stderr_fd = atoi(argv[1]);
      shift; shift;
    } else if (strcasecmp (argv[0], "--") == 0 && argc == 2) {
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
    struct stat buf;
    int rc = stat(restorename, &buf);
    if (rc == -1) {
      char error_msg[MAXPATHLEN+35];
      sprintf(error_msg, "\nmtcp_restart: ckpt image %s", restorename);
      perror(error_msg);
      abort();
    } else if (buf.st_uid != getuid()) { /*Could also run if geteuid() matches*/
      MTCP_PRINTF("\nProcess uid (%d) doesn't match uid (%d) of\n" \
	          "  checkpoint image (%s).\n" \
		  "This is dangerous.  Aborting for security reasons.\n" \
                  "If you still want to do this, modify mtcp/%s:%d and"
                  "  re-compile.\n",
                  getuid(), buf.st_uid, restorename, __FILE__, __LINE__ - 5);
      abort();
    }
  }

  if (strlen(ckpt_newname) == 0 && restorename != NULL && offset != 0) {
    strncpy(ckpt_newname, restorename, MAXPATHLEN);
  }

  if(restorename!=NULL) fd = open_ckpt_to_read(restorename);
  if(offset>0){
    //skip into the file a bit
    char* tmp = malloc(offset);
    readfile(fd, tmp, offset);
    free(tmp);
  }
  memset(magicbuf, 0, sizeof magicbuf);
  readfile (fd, magicbuf, MAGIC_LEN);
  if (memcmp (magicbuf, MAGIC, MAGIC_LEN) != 0) {
    MTCP_PRINTF("'%s' is '%s', but this restore is '%s' (fd=%d)\n",
                restorename, magicbuf, MAGIC, fd);
    return (-1);
  }

  /* Set the resource limits for stack from saved values */
  struct rlimit stack_rlimit;
  readcs (fd, CS_STACKRLIMIT); /* resource limit for stack */
  readfile (fd, &stack_rlimit, sizeof stack_rlimit);
#ifdef DEBUG
  MTCP_PRINTF("saved stack resource limit: soft_lim:%p, hard_lim:%p\n",
              stack_rlimit.rlim_cur, stack_rlimit.rlim_max);
#endif
  setrlimit(RLIMIT_STACK, &stack_rlimit);

  /* Find where the restore image goes */
  readcs (fd, CS_RESTOREBEGIN); /* beginning of checkpointed libmtcp.so image */
  readfile (fd, &restore_begin, sizeof restore_begin);
  readcs (fd, CS_RESTORESIZE); /* size of checkpointed libmtcp.so image */
  readfile (fd, &restore_size, sizeof restore_size);
  readcs (fd, CS_RESTORESTART);
  readfile (fd, &restore_start, sizeof restore_start);

  /* Read in the restore image to same address where it was loaded at time
   *  of checkpoint.  This is libmtcp.so, including both text and data sections
   *  as a single section.  Hence, we need both write and exec permission,
   *  and MAP_ANONYMOUS, since the data could have changed.
   */

#ifdef DEBUG
  MTCP_PRINTF("restoring anonymous area %p at %p\n",
              restore_size, restore_begin);
#endif
  if (munmap(restore_begin, restore_size) < 0) {
    MTCP_PRINTF("failed to unmap region at %p\n", restore_begin);
    abort ();
  }
  restore_mmap = mtcp_safemmap (restore_begin, restore_size,
                                PROT_READ | PROT_WRITE | PROT_EXEC,
                                MAP_ANONYMOUS | MAP_FIXED | MAP_PRIVATE, -1, 0);
  if (restore_mmap == MAP_FAILED) {
#ifndef _XOPEN_UNIX
    MTCP_PRINTF("Does mmap here support MAP_FIXED?\n");
#endif
    if (mtcp_sys_errno != EBUSY) {
      MTCP_PRINTF("error creating %p byte restore region at %p: %s\n",
                  restore_size, restore_begin, strerror(mtcp_sys_errno));
      abort ();
    } else {
      MTCP_PRINTF("restarting due to address conflict...\n");
      close (fd);
      execvp (argv[0], argv);
    }
  }
  if (restore_mmap != restore_begin) {
    MTCP_PRINTF("%p byte restore region at %p got mapped at %p\n",
                restore_size, restore_begin, restore_mmap);
    abort ();
  }
  readcs (fd, CS_RESTOREIMAGE);
  readfile (fd, restore_begin, restore_size);

#ifndef __x86_64__
  // Copy command line to libmtcp.so, so that we can re-exec if randomized vdso
  //   steps on us.  This won't be needed when we use the linker to map areas.
  cmd_file[0] = '\0';
  { int cmd_len = readlink("/proc/self/exe", cmd_file, MAXPATHLEN);
    if (cmd_len == -1)
      MTCP_PRINTF("WARNING:  Couldn't find /proc/self/exe."
		  "  Trying to continue anyway.\n");
    else
      cmd_file[cmd_len] = '\0';
  }
#endif

#if defined(DEBUG) &&  !defined(DMTCP_DEBUG)
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

  /* Now call it - it shouldn't return */
  (*restore_start) (fd, verify, decomp_child_pid, ckpt_newname, cmd_file, argv, envp);
  mtcp_printf("mtcp_restart: restore routine returned (it should never do this!)\n");
  abort ();
  return (0);
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

    fd = open(filename, O_RDONLY);
    if(fd < 0)
    {
        MTCP_PRINTF("ERROR: Cannot open file %s\n", filename);
        abort();
    }

    rc = read(fd, &c, 1);
    if(rc != 1)
    {
        MTCP_PRINTF("ERROR: Error reading from file %s\n", filename);
        abort();
    }

    close(fd);
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
static int open_ckpt_to_read(char *filename) {
    int fd;
    int fds[2];
    char fc;
    char *gzip_cmd = "gzip";
    char *hbict_cmd = "hbict";
    char decomp_path[MTCP_MAX_PATH];
    static char *gzip_args[] = { "gzip", "-d", "-", NULL };
    static char *hbict_args[] = { "hbict", "-r", NULL };
    static char **decomp_args;
    pid_t cpid;

    fc = first_char(filename);
    fd = open(filename, O_RDONLY);
    if(fd < 0) {
        MTCP_PRINTF("ERROR: Cannot open checkpoint file %s\n", filename);
        abort();
    }

    if (fc == MAGIC_FIRST || fc == 'D') /* no compression ('D' from DMTCP) */
        return fd;
    else if (fc == GZIP_FIRST || fc == HBICT_FIRST){ /* Set prog_path */
        if( fc == GZIP_FIRST ){
            decomp_args = gzip_args;
            if( mtcp_find_executable(gzip_cmd, decomp_path) == NULL ) {
                fputs("ERROR: Cannot find gunzip to decompress checkpoint file!\n", stderr);
                abort();
            }
        }

       	if( fc == HBICT_FIRST ){
            decomp_args = hbict_args;
            if( mtcp_find_executable(hbict_cmd, decomp_path) == NULL ) {
                fputs("ERROR: Cannot find hbict to decompress checkpoint file!\n", stderr);
                abort();
            }
        }

        if (pipe(fds) == -1) {
            fputs("ERROR: Cannot create pipe to execute gunzip to decompress"
                  " checkpoint file!\n", stderr);
            abort();
        }

        cpid = fork();

        if(cpid == -1) {
            fputs("ERROR: Cannot fork to execute gunzip to decompress"
                  " checkpoint file!\n", stderr);
            abort();
        }
        else if(cpid > 0) /* parent process */ {
            decomp_child_pid = cpid;
            close(fd);
            close(fds[1]);
            return fds[0];
        }
        else /* child process */ {
            fd = dup(dup(dup(fd)));
            if (fd == -1) {
            fputs("ERROR: dup() failed!  No restoration will be performed!"
                  " Cancel now!\n", stderr);
              mtcp_abort();
            }
            fds[1] = dup(fds[1]);
            close(fds[0]);
            if (dup2(fd, STDIN_FILENO) != STDIN_FILENO) {
            fputs("ERROR: dup2() failed!  No restoration will be performed!"
                  " Cancel now!\n", stderr);
              mtcp_abort();
            }
            close(fd);
            dup2(fds[1], STDOUT_FILENO);
            close(fds[1]);
            execvp(decomp_path, decomp_args);
            /* should not get here */
            fputs("ERROR: Decompression failed!  No restoration will be"
                  " performed!  Cancel now!\n", stderr);
            abort();
        }
    }
    else /* invalid magic number */ {
        fputs("ERROR: Invalid magic number in this checkpoint file!\n", stderr);
        abort();
    }
}

static void readcs (int fd, char cs)

{
  char xcs;

  readfile (fd, &xcs, sizeof xcs);
  if (xcs != cs) {
    MTCP_PRINTF("checkpoint section %d next, expected %d\n", xcs, cs);
    abort ();
  }
}

static void readfile(int fd, void *buf, size_t size)
{
    int rc;
    unsigned int ar = 0;

    while(ar != size)
    {
        rc = read(fd, (char *)buf + ar, size - ar);
        if(rc < 0)
        {
            MTCP_PRINTF("error reading checkpoint file: %s\n", strerror(errno));
            abort();
        }
        else if(rc == 0)
        {
            MTCP_PRINTF("only read %d bytes instead of %d from checkpoint"
                        " file\n", ar, size);
            abort();
        }

        ar += rc;
    }
}
