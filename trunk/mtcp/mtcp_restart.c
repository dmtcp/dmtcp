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
/*  make sure the restore works.  The -verify option tells it to rename the checkpoint file, removing the .temp from the end.	*/
/*																*/
/********************************************************************************************************************************/

#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>

#include "mtcp_internal.h"

static char first_char(char *filename);
static int open_ckpt_to_read(char *filename);
static void readcs (int fd, char cs);
static void readfile (int fd, void *buf, int size);

static pid_t gzip_child_pid = -1;

int main (int argc, char *argv[], char *envp[])

{
  char magicbuf[MAGIC_LEN], *restorename;
  int fd, restore_size, verify, offset=0;
  void *restore_begin, *restore_mmap;
  void (*restore_start) (int fd, int verify, pid_t gzip_child_pid,char *ckpt_newname,
			 char *cmd_file, char *argv[], char *envp[]);
  char cmd_file[MAXPATHLEN+1];
  char ckpt_newname[MAXPATHLEN+1] = "";
  int cmd_len;

  if (getuid() == 0 || geteuid() == 0) {
    mtcp_printf("Running mtcp_restart as root is dangerous.  Aborting.\n"
	   "If you still want to do this, modify %s:%d and re-compile.\n",
	   __FILE__, __LINE__);
    abort();
  }

  mtcp_check_vdso_enabled();

  if (argc == 2) {
    verify = 0;
    restorename = argv[1];
  } else if ((argc == 3) && (strcasecmp (argv[1], "-verify") == 0)) {
    verify = 1;
    restorename = argv[2];
  } else if ((argc == 4) && (strcasecmp (argv[1], "-offset") == 0)) {
    verify = 0;
    offset = atoi(argv[2]);
    restorename = argv[3];
	strncpy(ckpt_newname,restorename,MAXPATHLEN);
  } else if ((argc == 3) && (strcasecmp (argv[1], "-fd") == 0)) {
    /* This case used only when dmtcp_restart exec's to mtcp_restart. */
    verify = 0;
    restorename = NULL;
    fd = atoi(argv[2]);
  } else if ((argc == 5) && (strcasecmp (argv[1], "-fd") == 0)
	     && (strcasecmp (argv[3], "-gzip_child_pid") == 0)) {
    /* This case used only when dmtcp_restart exec's to mtcp_restart. */
    verify = 0;
    restorename = NULL;
    fd = atoi(argv[2]);
    gzip_child_pid = atoi(argv[4]);
  } else if ((argc == 7) && (strcasecmp (argv[1], "-fd") == 0)
	     && (strcasecmp (argv[3], "-gzip_child_pid") == 0)
		 && (strcasecmp (argv[5], "-rename-ckpt") == 0)) {
    /* This case used only when dmtcp_restart exec's to mtcp_restart. & wants to rename checkpoint filename */
    verify = 0;
    restorename = NULL;
    fd = atoi(argv[2]);
    gzip_child_pid = atoi(argv[4]);
	strncpy(ckpt_newname,argv[6],MAXPATHLEN);
  } else {
    mtcp_printf("usage: mtcp_restart [-verify] <checkpointfile>\n");
    return (-1);
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
    mtcp_printf("mtcp_restart: '%s' is '%s', but this restore is '%s' (fd=%d)\n", restorename, magicbuf, MAGIC, fd);
    return (-1);
  }

  /* Find where the restore image goes */

  readcs (fd, CS_RESTOREBEGIN); /* beginning of checkpointed mtcp.so image */
  readfile (fd, &restore_begin, sizeof restore_begin);
  readcs (fd, CS_RESTORESIZE); /* size of checkpointed mtcp.so image */
  readfile (fd, &restore_size, sizeof restore_size);
  readcs (fd, CS_RESTORESTART);
  readfile (fd, &restore_start, sizeof restore_start);

  /* Read in the restore image to same address where it was loaded at time
   *  of checkpoint.  This is mtcp.so, including both text and data sections
   *  as a single section.  Hence, we need both write and exec permission,
   *  and MAP_ANONYMOUS, since the data could have changed.
   */

#ifdef DEBUG
  mtcp_printf("mtcp_restart.c: main*: restoring anonymous area 0x%X at %p\n",
              restore_size, restore_begin);
#endif
  restore_mmap = mtcp_safemmap (restore_begin, restore_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_FIXED | MAP_PRIVATE, -1, 0);
  if (restore_mmap == MAP_FAILED) {
#ifndef _XOPEN_UNIX
    mtcp_printf("mtcp_restart: Does mmap here support MAP_FIXED?\n");
#endif
    if (mtcp_sys_errno != EBUSY) {
      mtcp_printf("mtcp_restart: error creating %d byte restore region at %p: %s\n", restore_size, restore_begin, strerror(mtcp_sys_errno));
      abort ();
    } else {
      mtcp_printf("mtcp_restart: info: restarting due to address conflict...\n");
      close (fd);
      execvp (argv[0], argv);
    }
  }
  if (restore_mmap != restore_begin) {
    mtcp_printf("mtcp_restart: %d byte restore region at %p got mapped at %p\n", restore_size, restore_begin, restore_mmap);
    abort ();
  }
  readcs (fd, CS_RESTOREIMAGE);
  readfile (fd, restore_begin, restore_size);

#ifndef __x86_64__
  // Copy command line to mtcp.so, so that we can re-exec if randomized vdso
  //   steps on us.  This won't be needed when we use the linker to map areas.
  cmd_file[0] = '\0';
  cmd_len = readlink("/proc/self/exe", cmd_file, MAXPATHLEN);
  if (cmd_len == -1)
    mtcp_printf("WARNING:  Couldn't find /proc/self/exe."
		"  Trying to continue anyway.\n");
  else
    cmd_file[cmd_len] = '\0';
#endif

#if defined(DEBUG) && ! DMTCP
    char *p, symbolbuff[256];
    FILE *symbolfile;
    VA textbase;

    mtcp_printf("mtcp_restart*: restore_begin=%p, restore_start=%p\n", restore_begin, restore_start);
    textbase = 0;

    symbolfile = popen ("readelf -S mtcp.so", "r");
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
        mtcp_printf("     add-symbol-file mtcp.so %p\n",
                 restore_begin + textbase);
        mtcp_printf("Then type \"continue\" to continue debugging.\n");
	mtcp_printf("**********\n");
      }
    }
    mtcp_maybebpt ();
#endif

  /* Now call it - it shouldn't return */
  (*restore_start) (fd, verify, gzip_child_pid, ckpt_newname, cmd_file, argv, envp);
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
        mtcp_printf("ERROR: Cannot open file %s\n", filename);
        abort();
    }

    rc = read(fd, &c, 1);
    if(rc != 1)
    {
        mtcp_printf("ERROR: Error reading from file %s\n", filename);
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
 *
 * @param filename the name of the checkpoint file
 * @return the fd to use
 */
static int open_ckpt_to_read(char *filename)
{
    int fd;
    int fds[2];
    char fc;
    char *gzip_path = "gzip";
    static char *gzip_args[] = { "gzip", "-d", "-", NULL };
    pid_t cpid;

    fc = first_char(filename);
    fd = open(filename, O_RDONLY);
    if(fd < 0)
    {
        mtcp_printf("ERROR: Cannot open checkpoint file %s\n", filename);
        abort();
    }

    if(fc == MAGIC_FIRST || fc=='D') /* no compression */
        return fd;
    else if(fc == GZIP_FIRST) /* gzip */
    {
        if((gzip_path = mtcp_executable_path("gzip")) == NULL)
        {
            fputs("ERROR: Cannot find gunzip to decompress checkpoint file!\n", stderr);
            abort();
        }

        if(pipe(fds) == -1)
        {
            fputs("ERROR: Cannot create pipe to execute gunzip to decompress checkpoint file!\n", stderr);
            abort();
        }

        cpid = fork();

        if(cpid == -1)
        {
            fputs("ERROR: Cannot fork to execute gunzip to decompress checkpoint file!\n", stderr);
            abort();
        }
        else if(cpid > 0) /* parent process */
        {
            gzip_child_pid = cpid;
            close(fd);
            close(fds[1]);
            return fds[0];
        }
        else /* child process */
        {
            fd = dup(dup(dup(fd)));
            fds[1] = dup(fds[1]);
            close(fds[0]);
            dup2(fd, STDIN_FILENO);
            close(fd);
            dup2(fds[1], STDOUT_FILENO);
            close(fds[1]);
	    unsetenv("LD_PRELOAD");
            execvp(gzip_path, gzip_args);
            /* should not get here */
            fputs("ERROR: Decompression failed!  No restoration will be performed!  Cancel now!\n", stderr);
            abort();
        }
    }
    else /* invalid magic number */
    {
        fputs("ERROR: Invalid magic number in this checkpoint file!\n", stderr);
        abort();
    }
}

static void readcs (int fd, char cs)

{
  char xcs;

  readfile (fd, &xcs, sizeof xcs);
  if (xcs != cs) {
    mtcp_printf("mtcp_restart readcs: checkpoint section %d next, expected %d\n", xcs, cs);
    abort ();
  }
}

static void readfile(int fd, void *buf, int size)
{
    int rc, ar;

    ar = 0;

    while(ar != size)
    {
        rc = read(fd, buf + ar, size - ar);
        if(rc < 0)
        {
            mtcp_printf("mtcp_restart readfile: error reading checkpoint file: %s\n", strerror(errno));
            abort();
        }
        else if(rc == 0)
        {
            mtcp_printf("mtcp_restart readfile: only read %d bytes instead of %d from checkpoint file\n", ar, size);
            abort();
        }

        ar += rc;
    }
}
