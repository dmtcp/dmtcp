//+++2006-05-03
//    Copyright (C) 2006  Mike Rieker, Beverly, MA USA
//    EXPECT it to FAIL when someone's HeALTh or PROpeRTy is at RISk
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; version 2 of the License.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program; if not, write to the Free Software
//    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//---2006-05-03

/********************************************************************************************************************************/
/*																*/
/*  This command-line utility is what a user uses to perform a restore								*/
/*  It reads the given checkpoint file into memory then jumps to it, thus being just like the original program was restarted 	*/
/*  from the last checkpoint.													*/
/*																*/
/*  It is also used by the checkpoint verification to perform a restore while the original application program is running, to 	*/
/*  make sure the restore works.  THe -verify option tells it to rename the checkpoint file, removing the .temp from the end.	*/
/*																*/
/********************************************************************************************************************************/

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "mtcp_internal.h"

static void readcs (int fd, char cs);
static void readfile (int fd, void *buff, int size);

int main (int argc, char *argv[])

{
  char magicbuf[sizeof MAGIC], *restorename;
  int fd, restore_size, verify;
  void *restore_begin, *restore_mmap;
  void (*restore_start) (int fd, int verify);

  if (argc == 2) {
    verify = 0;
    restorename = argv[1];
  } else if ((argc == 3) && (strcasecmp (argv[1], "-verify") == 0)) {
    verify = 1;
    restorename = argv[2];
  } else {
    fprintf (stderr, "usage: mtcp_restore [-verify] <checkpointfile>\n");
    return (-1);
  }

  fd = open (restorename, O_RDONLY);
  if (fd < 0) {
    fprintf (stderr, "mtcp_restore: error opening %s; Reason: %s\n", restorename, strerror (errno));
    return (-1);
  }

  readfile (fd, magicbuf, sizeof magicbuf);
  if (memcmp (magicbuf, MAGIC, sizeof magicbuf) != 0) {
    fprintf (stderr, "mtcp_restore: %s is %s, but this restore is %s\n", restorename, magicbuf, MAGIC);
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
   *  of checkpoint.  This is libc.so, including both text and data sections
   *  as a single section.  Hence, we need both write and exec permission,
   *  and MAP_ANONYMOUS, since the data could have changed.
   */

  restore_mmap = mtcp_safemmap (restore_begin, restore_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_FIXED | MAP_PRIVATE, 0, 0);
  if (restore_mmap == MAP_FAILED) {
#ifndef _XOPEN_UNIX
    printf(stderr, "mtcp_restore: Does mmap here support MAP_FIXED?\n");
#endif
printf("restore_mmap: %x\n", restore_mmap);
    if (mtcp_sys_errno != EBUSY) {
      fprintf (stderr, "mtcp_restore: error creating %d byte restore region at %p: %s\n", restore_size, restore_begin, strerror(mtcp_sys_errno));
      abort ();
    } else {
      fprintf (stderr, "mtcp_restore: restarting due to address conflict...\n");
      close (fd);
      execvp (argv[0], argv);
    }
  }
  if (restore_mmap != restore_begin) {
    fprintf (stderr, "mtcp_restore: %d byte restore region at %p got mapped at %p\n", restore_size, restore_begin, restore_mmap);
    abort ();
  }
  readcs (fd, CS_RESTOREIMAGE);
  readfile (fd, restore_begin, restore_size);
  /* Now call it - it shouldn't return */

#ifdef DEBUG
    char *p, symbolbuff[256];
    FILE *symbolfile;
    VA textbase;

    fprintf (stderr, "mtcp_restore*: restore_begin=%p, restore_start=%p\n", restore_begin, restore_start);
    textbase = 0;

    symbolfile = popen ("readelf -S mtcp.so", "r");
    if (symbolfile != NULL) {
      while (fgets (symbolbuff, sizeof symbolbuff, symbolfile) != NULL) {
        if (memcmp (symbolbuff + 5, "] .text ", 8) == 0) {
          textbase = strtoul (symbolbuff + 41, &p, 16);
        }
      }
      fclose (symbolfile);
      if (textbase != 0) {
	fprintf (stderr, "\n**********\nmtcp_restore*: The symbol table of the"
		 " checkpointed file can be\nmade available to gdb."
		 "  Just type the command below in gdb:\n");
        fprintf (stderr, "     add-symbol-file mtcp.so %p\n",
                 restore_begin + textbase);
        fprintf (stderr, "Then type \"continue\" to continue debugging.\n");
	fprintf (stderr, "**********\n");
      }
    }
    mtcp_maybebpt ();
#endif

  (*restore_start) (fd, verify);
  fprintf (stderr, "mtcp_restore: restore routine returned (it should never do this!)\n");
  abort ();
  return (0);
}

static void readcs (int fd, char cs)

{
  char xcs;

  readfile (fd, &xcs, sizeof xcs);
  if (xcs != cs) {
    fprintf (stderr, "mtcp_restore readcs: checkpoint section %d next, expected %d\n", xcs, cs);
    abort ();
  }
}

static void readfile (int fd, void *buff, int size)

{
  int rc;

  rc = read (fd, buff, size);
  if (rc < 0) {
    fprintf (stderr, "mtcp_restore readfile: error reading checkpoint file: %s\n", strerror (errno));
    abort ();
  }
  if (rc != size) {
    fprintf (stderr, "mtcp_restore readfile: only read %d bytes instead of %d from checkpoint file\n", rc, size);
    abort ();
  }
}
