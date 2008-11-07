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
/*  Static part of restore - This gets linked in the mtcp.so shareable image that gets loaded as part of the user's original 	*/
/*  application.  The makefile appends all the needed system call routines onto the end of this module so it will link with no 	*/
/*  undefined symbols.  This allows the restore procedure to simply read this object image from the restore file, load it to 	*/
/*  the same address it was in the user's original application, and jump to it.							*/
/*																*/
/*  If we didn't assemble it all as one module, the idiot loader would make references to glibc routines go to libc.so even 	*/
/*  though there are object modules linked in with those routines defined.							*/
/*																*/
/********************************************************************************************************************************/

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mtcp_internal.h"

__attribute__ ((visibility ("hidden")))
  int mtcp_restore_cpfd = -1; // '= -1' puts it in regular data instead of common
__attribute__ ((visibility ("hidden")))
  int mtcp_restore_verify = 0;// 0: normal restore; 1: verification restore
__attribute__ ((visibility ("hidden")))
  pid_t mtcp_restore_gzip_child_pid = -1; // '= -1' puts it in regular data instead of common
__attribute__ ((visibility ("hidden")))
  void *mtcp_saved_break = NULL;  // saved brk (0) value

	/* These two are used by the linker script to define the beginning and end of the image.         */
	/* The '.long 0' is needed so shareable_begin>0 as the linker is too st00pid to relocate a zero. */

asm (".section __shareable_begin ; .globl mtcp_shareable_begin ; .long 0 ; mtcp_shareable_begin:");
asm (".section __shareable_end   ; .globl mtcp_shareable_end; .long 0  ; mtcp_shareable_end:");
asm (".text");

	/* Internal routines */

static void readfiledescrs (void);
static void readmemoryareas (void);
static void readcs (char cs);
static void readfile (void *buf, int size);
static void skipfile(int size);
static VA highest_userspace_address (VA *vdso_addr);
static int open_shared_file(char* fileName);

/********************************************************************************************************************************/
/*																*/
/*  This routine is called executing on the temporary stack									*/
/*  It performs the actual restore of everything (except the mtcp.so area)							*/
/*																*/
/********************************************************************************************************************************/

__attribute__ ((visibility ("hidden"))) void mtcp_restoreverything (void)

{
  int rc;
  VA holebase, highest_va, vdso_addr = (VA)NULL; /* VA = virtual address */
  void *current_brk;
  void *new_brk;
  void (*finishrestore) (void);

  DPRINTF(("Entering mtcp_restart_nolibc.c:mtcp_restoreverything\n"));

  /* The kernel (2.6.9 anyway) has a variable mm->brk that we should restore.  The only access we have is brk() which basically */
  /* sets mm->brk to the new value, but also has a nasty side-effect (as far as we're concerned) of mmapping an anonymous       */
  /* section between the old value of mm->brk and the value being passed to brk().  It will munmap the bracketed memory if the  */
  /* value being passed is lower than the old value.  But if zero, it will return the current mm->brk value.                    */

  /* So we're going to restore the brk here.  As long as the current mm->brk value is below the static restore region, we're ok */
  /* because we 'know' the restored brk can't be in the static restore region, and we don't care if the kernel mmaps something  */
  /* or munmaps something because we're going to wipe it all out anyway.                                                        */

  current_brk = mtcp_sys_brk (NULL);
  if (((VA)current_brk > (VA)mtcp_shareable_begin) && ((VA)mtcp_saved_break < (VA)mtcp_shareable_end)) {
    mtcp_printf ("mtcp_restoreverything: current_brk %p, mtcp_saved_break %p, mtcp_shareable_begin %p, mtcp_shareable_end %p\n", 
                  current_brk, mtcp_saved_break, mtcp_shareable_begin, mtcp_shareable_end);
    mtcp_abort ();
  }

  new_brk = mtcp_sys_brk (mtcp_saved_break);
  if (new_brk != mtcp_saved_break) {
    if (new_brk == current_brk && new_brk > mtcp_saved_break)
      DPRINTF(("mtcp_restoreverything: new_brk == current_brk == %p\n"
        "  saved_break, %p, is strictly smaller; data segment not extended.\n",
        new_brk, mtcp_saved_break));
    else {
      mtcp_printf ("mtcp_restoreverything: error: new break (%p) != saved break"
                   "  (%p)\n", (VA)current_brk, mtcp_saved_break);
      mtcp_abort ();
    }
  }

  /* Unmap everything except for this image as everything we need
   *   is contained in the mtcp.so image.
   * Unfortunately, in later Linuxes, it's important also not to wipe
   *   out [vsyscall] if it exists (we may not have permission to remove it).
   *   In any case, [vsyscall] is the highest segment if it exists.
   * Further, if the [vdso] when we restart is different from the old
   *   [vdso] that was saved at checkpoint time, then we need to keep
   *   both of them.  The old one may be needed if we're returning from
   *   a system call at checkpoint time.  The new one is needed for future
   *   system calls.
   * Highest_userspace_address is determined heuristically.  Primarily, it
   *   was intended to make sure we don't overwrite [vdso] or [vsyscall].
   *   But it was heuristically chosen as a constant (works for earlier
   *   Linuxes), or as the end of stack.  Probably, we should review that,
   *   and just make it beginning of [vsyscall]] where that exists.
   */

  holebase  = (VA)mtcp_shareable_begin;
  holebase &= -PAGE_SIZE;
  asm volatile (CLEAN_FOR_64_BIT(xor %%eax,%%eax ; movw %%ax,%%fs)
				: : : CLEAN_FOR_64_BIT(eax)); // the unmaps will wipe what it points to anyway
  // asm volatile (CLEAN_FOR_64_BIT(xor %%eax,%%eax ; movw %%ax,%%gs) : : : CLEAN_FOR_64_BIT(eax)); // so make sure we get a hard failure just in case
                                                                  // ... it's left dangling on something I want

  /* Unmap from address 0 to holebase, except for [vdso] segment */
  highest_va = highest_userspace_address(&vdso_addr);
  if (highest_va == 0) /* 0 means /proc/self/maps doesn't mark "[stack]" */
    highest_va = HIGHEST_VA;

  if (vdso_addr != (VA)NULL && vdso_addr < holebase) {
    DPRINTF (("mtcp restoreverything*: unmapping %p..%p, %p..%p\n",
	      NULL, vdso_addr-1, vdso_addr+PAGE_SIZE, holebase - 1));
    rc = mtcp_sys_munmap ((void *)NULL, (size_t)vdso_addr);
    rc |= mtcp_sys_munmap ((void *)vdso_addr + PAGE_SIZE,
			   (size_t)holebase - vdso_addr - PAGE_SIZE);
  } else {
    DPRINTF (("mtcp restoreverything*: unmapping 0..%p\n", holebase - 1));
    rc = mtcp_sys_munmap (NULL, holebase);
  }
  if (rc == -1) {
      mtcp_printf ("mtcp_sys_munmap: error %d unmapping from 0 to %p\n",
		   mtcp_sys_errno, holebase);
      mtcp_abort ();
  }

  /* Unmap from address holebase to highest_va, except for [vdso] segment */
  holebase  = (VA)mtcp_shareable_end;
  holebase  = (holebase + PAGE_SIZE - 1) & -PAGE_SIZE;
  if (highest_va == 0) { /* 0 means /proc/self/maps doesn't mark "[stack]" */
    highest_va = HIGHEST_VA;
  }
  if (vdso_addr != (VA)NULL && vdso_addr + PAGE_SIZE <= (VA)highest_va) {
    if (vdso_addr > holebase) {
      DPRINTF (("mtcp restoreverything*: unmapping %p..%p, %p..%p\n",
	        holebase, vdso_addr-1, vdso_addr+PAGE_SIZE, highest_va - 1));
      rc = mtcp_sys_munmap ((void *)holebase, vdso_addr - holebase);
      rc |= mtcp_sys_munmap ((void *)vdso_addr + PAGE_SIZE,
			   highest_va - vdso_addr - PAGE_SIZE);
    } else {
      DPRINTF (("mtcp restoreverything*: unmapping %p..%p\n",
	        holebase, highest_va - 1));
      if (highest_va < holebase) {
        mtcp_printf ("mtcp_sys_munmap: error unmapping:"
		     " highest_va(%p) < holebase(%p)\n",
		     highest_va, holebase);
        mtcp_abort ();
      }
      rc = mtcp_sys_munmap ((void *)holebase, highest_va - holebase);
    }
  }
  if (rc == -1) {
      mtcp_printf ("mtcp_sys_munmap: error %d unmapping from %p by %p bytes\n",
		   mtcp_sys_errno, holebase, highest_va - holebase);
      mtcp_abort ();
  }

  /* Read address of mtcp.c's finishrestore routine */

  readcs (CS_FINISHRESTORE);
  readfile (&finishrestore, sizeof finishrestore);

  /* Restore file descriptors */

  DPRINTF (("mtcp restoreverything*: restoring file descriptors\n"));
  readfiledescrs ();                              // restore files

  /* Restore memory areas */

  DPRINTF (("mtcp restoreverything*: restoring memory areas\n"));
  readmemoryareas ();

  /* Everything restored, close file and finish up */

  DPRINTF (("mtcp restoreverything*: close cpfd %d\n", mtcp_restore_cpfd));
  mtcp_sys_close (mtcp_restore_cpfd);
  mtcp_restore_cpfd = -1;
  DPRINTF (("mtcp restoreverything*: waiting on gzip_child_pid: %d\n", mtcp_restore_gzip_child_pid ));
  // Calling waitpid here, but on 32-bit Linux, libc:waitpid() calls wait4()
  if( mtcp_restore_gzip_child_pid != -1 ) {
    if( mtcp_sys_wait4(mtcp_restore_gzip_child_pid , NULL, 0, NULL ) == -1 )
        DPRINTF (("mtcp restoreverything*: error wait4: errno: %d", mtcp_sys_errno));
    mtcp_restore_gzip_child_pid = -1;
  }

  DPRINTF (("mtcp restoreverything*: restore complete, resuming...\n"));

  /* Jump to finishrestore in original program's mtcp.so image */

  (*finishrestore) ();
}

/********************************************************************************************************************************/
/*																*/
/*  Read file descriptor info from checkpoint file and re-open and re-position files on the same descriptors			*/
/*  Move the checkpoint file to a different fd if needed									*/
/*																*/
/********************************************************************************************************************************/

static void readfiledescrs (void)

{
  char linkbuf[FILENAMESIZE];
  int fdnum, flags, linklen, tempfd;
  off_t offset;
  struct Stat statbuf;

  readcs (CS_FILEDESCRS);

  while (1) {

    /* Read parameters of next file to restore */

    readfile (&fdnum, sizeof fdnum);
    if (fdnum < 0) break;
    readfile (&statbuf, sizeof statbuf);
    readfile (&offset, sizeof offset);
    readfile (&linklen, sizeof linklen);
    if (linklen >= sizeof linkbuf) {
      mtcp_printf ("filename too long %d\n", linklen);
      mtcp_abort ();
    }
    readfile (linkbuf, linklen);
    linkbuf[linklen] = 0;

    DPRINTF (("mtcp readfiledescrs*: restoring %d -> %s\n", fdnum, linkbuf));

    /* Maybe it restores to same fd as we're using for checkpoint file. */
    /* If so, move the checkpoint file somewhere else.                  */

    if (fdnum == mtcp_restore_cpfd) {
      flags = mtcp_sys_dup (mtcp_restore_cpfd);
      if (flags < 0) {
        mtcp_printf ("mtcp readfiledescrs: error %d duping checkpoint file fd %d\n", mtcp_sys_errno, mtcp_restore_cpfd);
        mtcp_abort ();
      }
      mtcp_restore_cpfd = flags;
      DPRINTF (("mtcp readfiledescrs*: cpfd changed to %d\n", mtcp_restore_cpfd));
    }

    /* Open the file on a temp fd */

    flags = O_RDWR;
    if (!(statbuf.st_mode & S_IWUSR)) flags = O_RDONLY;
    else if (!(statbuf.st_mode & S_IRUSR)) flags = O_WRONLY;
    tempfd = mtcp_sys_open (linkbuf, flags, 0);
    if (tempfd < 0) {
      mtcp_printf ("mtcp readfiledescrs: error %d re-opening %s flags %o\n", mtcp_sys_errno, linkbuf, flags);
      if (mtcp_sys_errno == EACCES)
        mtcp_printf("  Permission denied.\n");
      //mtcp_abort ();
      continue;
    }

    /* Move it to the original fd if it didn't coincidentally open there */

    if (tempfd != fdnum) {
      if (mtcp_sys_dup2 (tempfd, fdnum) < 0) {
        mtcp_printf ("mtcp readfiledescrs: error %d duping %s from %d to %d\n", mtcp_sys_errno, linkbuf, tempfd, fdnum);
        mtcp_abort ();
      }
      mtcp_sys_close (tempfd);
    }

    /* Position the file to its same spot it was at when checkpointed */

    if (S_ISREG (statbuf.st_mode) && (mtcp_sys_lseek (fdnum, offset, SEEK_SET) != offset)) {
      mtcp_printf ("mtcp readfiledescrs: error %d positioning %s to %ld\n", mtcp_sys_errno, linkbuf, (long)offset);
      mtcp_abort ();
    }
  }
}

/**************************************************************************/
/*									  */
/*  Read memory area descriptors from checkpoint file			  */
/*  Read memory area contents and/or mmap original file			  */
/*  Four cases: MAP_ANONYMOUS (if file /proc/.../maps reports file,	  */
/*		   handle it as if MAP_PRIVATE and not MAP_ANONYMOUS,	  */
/*		   but restore from ckpt image: no copy-on-write);	  */
/*		 private, currently assumes backing file exists		  */
/*               shared, but need to recreate file;			  */
/*		 shared and file currently exists			  */
/*		   (if writeable by us and memory map has write		  */
/*		    protection, then write to it from checkpoint file;	  */
/*		    else skip ckpt image and map current data of file)	  */ 
/*		 NOTE:  Linux option MAP_SHARED|MAP_ANONYMOUS		  */
/*		    currently not supported; result is undefined.	  */
/*		    If there is an important use case, we will fix this.  */
/*		 (NOTE:  mmap requires that if MAP_ANONYMOUS		  */
/*		   was not set, then mmap must specify a backing store.	  */
/*		   Further, a reference by mmap constitutes a reference	  */
/*		   to the file, and so the file cannot truly be deleted	  */
/*		   until the process no longer maps it.  So, if we don't  */
/*		   see the file on restart and there is no MAP_ANONYMOUS, */
/*		   then we have a responsibility to recreate the file.	  */
/*		   MAP_ANONYMOUS is not currently POSIX.)		  */
/*									  */
/**************************************************************************/

static void readmemoryareas (void)

{
  Area area;
  char cstype;
  int flags, imagefd, rc;
  void *mmappedat;
  int areaContentsAlreadyRead = 0;

  while (1) {
    int try_skipping_existing_segment = 0;

    readfile (&cstype, sizeof cstype);
    if (cstype == CS_THEEND) break;
    if (cstype != CS_AREADESCRIP) {
      mtcp_printf ("mtcp_restart_nolibc: expected CS_AREADESCRIP but had %d\n", cstype);
      mtcp_abort ();
    }
    readfile (&area, sizeof area);

    if ((area.flags & MAP_ANONYMOUS) && (area.flags & MAP_SHARED))
      mtcp_printf("\n\n*** WARNING:  Next area specifies MAP_ANONYMOUS"
		  "  and MAP_SHARED.\n"
		  "*** Turning off MAP_ANONYMOUS and hoping for best.\n\n");

    /* CASE MAP_ANONYMOUS (usually implies MAP_PRIVATE):		     */
    /* For anonymous areas, the checkpoint file contains the memory contents */
    /* directly.  So mmap an anonymous area and read the file into it.       */
    /* If file exists, turn off MAP_ANONYMOUS: standard private map	     */

    if (area.flags & MAP_ANONYMOUS) {

      /* If there is a filename there, though, pretend like we're mapping    */
      /* to it so a new /proc/self/maps will show a filename there like with */
      /* original process.  We only need read-only access because we don't   */
      /* want to ever write the file.					     */

      imagefd = 0;
      if (area.name[0] == '/') { /* If not null string, not [stack] or [vdso] */
        imagefd = mtcp_sys_open (area.name, O_RDONLY, 0);
        if (imagefd < 0) imagefd = 0;
        else area.flags ^= MAP_ANONYMOUS;
      }

      /* Create the memory area */

      if (area.flags & MAP_ANONYMOUS) {
        DPRINTF (("mtcp restoreverything*: restoring anonymous area 0x%X at %p\n", area.size, area.addr));
      } else {
        DPRINTF (("mtcp restoreverything*: restoring to non-anonymous area from anonymous area 0x%X at %p from %s + %X\n", area.size, area.addr, area.name, area.offset));
      }
      mmappedat = mtcp_safemmap (area.addr, area.size, area.prot | PROT_WRITE, area.flags, imagefd, area.offset);
      if (mmappedat == MAP_FAILED) {
        DPRINTF(("mtcp_restart_nolibc: error %d mapping %X bytes at %p\n", mtcp_sys_errno, area.size, area.addr));

	try_skipping_existing_segment = 1;
      }
      if (mmappedat != area.addr && !try_skipping_existing_segment) {
        mtcp_printf ("mtcp_restart_nolibc: area at %p got mmapped to %p\n", area.addr, mmappedat);
        mtcp_abort ();
      }

      /* Read saved area contents */
      readcs (CS_AREACONTENTS);
      if (try_skipping_existing_segment)
#ifdef BUG_64BIT_2_6_9
# if 0
        // This fails on teracluster.  Presumably extra symbols cause overflow.
        {
          char tmpbuf[4];
          int i;
          /* slow, but rare case; and only for old Linux 2.6.9 */
          for ( i = 0; i < area.size / 4; i++ )
            readfile (tmpbuf, 4);
        }
# else
	// This fails in CERN Linux 2.6.9; can't readfile on top of vsyscall
        readfile (area.addr, area.size);
# endif
#else
        // This fails on teracluster.  Presumably extra symbols cause overflow.
        skipfile (area.size);
#endif
      else {
        readfile (area.addr, area.size);
        if (!(area.prot & PROT_WRITE))
          if (mtcp_sys_mprotect (area.addr, area.size, area.prot) < 0) {
            mtcp_printf ("mtcp_restart_nolibc: error %d write-protecting %X bytes at %p\n", mtcp_sys_errno, area.size, area.addr);
            mtcp_abort ();
          }
      }

      /* Close image file (fd only gets in the way) */

      if (!(area.flags & MAP_ANONYMOUS)) mtcp_sys_close (imagefd);
    }

    /* CASE NOT MAP_ANONYMOUS:						     */
    /* Otherwise, we mmap the original file contents to the area */

    else {
      DPRINTF (("mtcp restoreverything*: restoring mapped area 0x%X at %p to %s + %X\n", area.size, area.addr, area.name, area.offset));
      flags = 0;            // see how to open it based on the access required
      // O_RDONLY = 00
      // O_WRONLY = 01
      // O_RDWR   = 02
      if (area.prot & PROT_WRITE) flags = O_WRONLY;
      if (area.prot & (PROT_EXEC | PROT_READ)){
        flags = O_RDONLY;
        if (area.prot & PROT_WRITE) flags = O_RDWR;
      }
 
      imagefd = mtcp_sys_open (area.name, flags, 0);  // open it

    /* CASE NOT MAP_ANONYMOUS, MAP_SHARED, backing file doesn't exist:	     */
      // If the shared file doesn't exist on the disk, we try to create it
      if (imagefd < 0 && (area.prot & MAP_SHARED) && mtcp_sys_errno == ENOENT ){

        DPRINTF(("mtcp restoreverything*: Shared file %s not found, Creating new\n",area.name));

	/* Dangerous for DMTCP:  Since file is created with O_CREAT,    */
	/* hopefully, a second process should ignore O_CREAT and just     */
	/*  duplicate the work of the first process, with no ill effect.*/
        imagefd = open_shared_file(area.name);

        // create a temp area in the memory exactly of the size of the
        // shared file.  We read the contents of the shared file from
	// checkpoint file(.mtcp) into system memory. From system memory,
	// the contents are written back to newly created replica of the shared
	// file (at the same path where it used to exist before checkpoint).
        mmappedat = mtcp_safemmap (area.addr, area.size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, imagefd, area.offset);
        if (mmappedat == MAP_FAILED) {
          mtcp_printf ("mtcp_restart_nolibc: error %d mapping temp memory at %p\n",
		       mtcp_sys_errno, area.addr);
          mtcp_abort ();
        }

        readcs (CS_AREACONTENTS);
        readfile (area.addr, area.size);
        areaContentsAlreadyRead = 1;

        if ( mtcp_sys_write(imagefd, area.addr,area.size) < 0 ){
          mtcp_printf ("mtcp_restart_nolibc: error %d creating mmap file %s\n",
		       mtcp_sys_errno, area.name);
          mtcp_abort();
        }

        // unmap the temp memory allocated earlier
        rc = mtcp_sys_munmap (area.addr, area.size);
        if (rc == -1) {
          mtcp_printf ("mtcp_restart_nolibc: error %d unmapping temp memory at %p\n",
		       mtcp_sys_errno, area.addr);
          mtcp_abort ();
        }
          
        //close the file
        mtcp_sys_close(imagefd);

        // now open the file again, this time with appropriate flags
        imagefd = mtcp_sys_open (area.name, flags, 0);
        if (imagefd < 0){
          mtcp_printf ("mtcp_restart_nolibc: error %d opening mmap file %s\n",
		       mtcp_sys_errno, area.name);
          mtcp_abort ();
        }
      }

    /* CASE NOT MAP_ANONYMOUS, MAP_PRIVATE, backing file doesn't exist:	     */
      if (imagefd < 0) {
        mtcp_printf ("mtcp_restart_nolibc: error %d opening mmap file %s\n",
		     mtcp_sys_errno, area.name);
        mtcp_abort ();
      }
      
    /* CASE NOT MAP_ANONYMOUS, MAP_SHARED or MAP_PRIVATE,		     */
    /*      backing file now exists:					     */
      // Map the shared file into memory
      mmappedat = mtcp_safemmap (area.addr, area.size, area.prot, area.flags, imagefd, area.offset);
      if (mmappedat == MAP_FAILED) {
        mtcp_printf ("mtcp_restart_nolibc: error %d mapping %s offset %d at %p\n",
		     mtcp_sys_errno, area.name, area.offset, area.addr);
        mtcp_abort ();
      }
      if (mmappedat != area.addr) {
        mtcp_printf ("mtcp_restart_nolibc: area at %p got mmapped to %p\n",
		     area.addr, mmappedat);
        mtcp_abort ();
      }
      mtcp_sys_close (imagefd); // don't leave dangling fd in way of other stuff

      if ( areaContentsAlreadyRead == 0 ){
        // If we haven't created the file (i.e. the shared file does exist
        // when this process wants to map it) we want to skip the checkpoint
        // file pointer and move to the end of the shared file data. We can not
        // use lseek() function as it can fail if we are using a pipe to read
        // the contents of checkpoint file (we might be using gzip to
        // uncompress checkpoint file on the fly). Thus we have to read or
        // skip contents using the following code.

        readcs (CS_AREACONTENTS);
	// If we have write permission or execute permission on file,
	//   then we use data in checkpoint image,
	// If MMAP_SHARED, this reverts the file to data at time of checkpoint.
	// In the case of DMTCP, multiple processes may duplicate this work.
	// NOTE: man 2 access: access  may  not  work  correctly on NFS file
	//   systems with UID mapping enabled, because UID mapping is done
	//   on the server and hidden from the client, which checks permissions.
	/* if (flags == O_WRONLY || flags == O_RDWR) */
	if ( ( (imagefd = mtcp_sys_open(area.name, O_WRONLY, 0)) >= 0
	       && ( (flags == O_WRONLY || flags == O_RDWR) ) )
	     || (0 == mtcp_sys_access(area.name, X_OK)) ) {
           mtcp_printf ("mtcp_restart_nolibc: mapping %s with data from ckpt image\n",
			area.name);
          readfile (area.addr, area.size);
	}
	// If we have no write permission on file, then we should use data
	//   from version of file at restart-time (not from checkpoint-time).
	// Because Linux library files have execute permission,
	//   the dynamic libraries from time of checkpoint will be used.
	else {
           mtcp_printf ("MTCP: mtcp_restart_nolibc: mapping current version"
	   		"of %s into memory;\n"
			"  _not_ file as it existed at time of checkpoint.\n"
			"  Change %s:%d and re-compile, if you want different"
			  "behavior.\n",
			area.name, __FILE__, __LINE__);
          skipfile (area.size);
	}
	if (imagefd >= 0)
          mtcp_sys_close (imagefd); // don't leave dangling fd
      }
    }
  }
}

static void readcs (char cs)

{
  char xcs;

  readfile (&xcs, sizeof xcs);
  if (xcs != cs) {
    mtcp_printf ("mtcp readcs: checkpoint section %d next, expected %d\n", xcs, cs);
    mtcp_abort ();
  }
}

static void readfile(void *buf, int size)
{
    int rc, ar;
    ar = 0;

    while(ar != size)
    {
        rc = mtcp_sys_read(mtcp_restore_cpfd, buf + ar, size - ar);
        if(rc < 0)
        {
            mtcp_printf("mtcp_restart_nolibc readfile: error %d reading checkpoint\n", mtcp_sys_errno);
            mtcp_abort();
        }
        else if(rc == 0)
        {
            mtcp_printf("mtcp_restart_nolibc readfile: only read %d bytes instead of %d from checkpoint file\n", ar, size);
            mtcp_abort();
        }

        ar += rc;
    }
}

static void skipfile(int size)
{
    int rc, ar;
    ar = 0;
    char array[512];

    while(ar != size)
    {
        rc = mtcp_sys_read(mtcp_restore_cpfd, array, (size-ar < 512 ? size - ar : 512));
        if(rc < 0)
        {
            mtcp_printf("mtcp_restart_nolibc skipfile: error %d skipping checkpoint\n", mtcp_sys_errno);
            mtcp_abort();
        }
        else if(rc == 0)
        {
            mtcp_printf("mtcp_restart_nolibc skipfile: only skipped %d bytes instead of %d from checkpoint file\n", ar, size);
            mtcp_abort();
        }

        ar += rc;
    }
}

#if 1
/* Modelled after mtcp_safemmap.  - Gene */
static VA highest_userspace_address (VA *vdso_addr)
{
  char c;
  int mapsfd, i;
  VA endaddr, startaddr;
  VA histackaddr = 0; /* high stack address should be highest userspace addr */
  const char *stackstring = "[stack]";
  const char *vdsostring = "[vdso]";
  const int bufsize = sizeof "[stack]"; /* larger of "[vdso]" and "[stack]" */
  char buf[bufsize];

  buf[0] = '\0';

  /* Scan through the mappings of this process */

  mapsfd = mtcp_sys_open ("/proc/self/maps", O_RDONLY, 0);
  if (mapsfd < 0) {
    mtcp_printf("couldn't open /proc/self/maps\n");
    mtcp_abort();
  }

  *vdso_addr = NULL;
  while (1) {

    /* Read a line from /proc/self/maps */

    c = mtcp_readhex (mapsfd, &startaddr);
#ifndef __x86_64__
DPRINTF(("startaddr: %x\n", startaddr));
#endif
    if (c != '-') goto skipeol;
    c = mtcp_readhex (mapsfd, &endaddr);
#ifndef __x86_64__
DPRINTF(("endaddr: %x\n", endaddr));
#endif
    if (c != ' ') goto skipeol;

    /* skip to next line */

skipeol:
    buf[bufsize-1] = '\0';
    while ((c != 0) && (c != '\n')) {
      for (i = 0; i < bufsize-2; i++)
        buf[i] = buf[i+1];
      buf[bufsize-2] = c;
      c = mtcp_readchar (mapsfd);
    }
    if (c == 0) break;  /* Must be end of file */
    /* emulate strcmp */
    for (i = 0; i < sizeof "[stack]"; i++)
      if (buf[i] != stackstring[i])
        break;
    if (i == sizeof "[stack]")
      histackaddr = endaddr;  /* We found "[stack]" in /proc/self/maps */
    for (i = 0; i < sizeof "[vdso]"; i++)
      if (buf[i+1] != vdsostring[i])
        break;
    if (i == sizeof "[vdso]")
      *vdso_addr = startaddr;
#ifndef __x86_64__
DPRINTF(("startaddr: %p\n", startaddr));
DPRINTF(("vdso_addr: %p\n", *vdso_addr));
#endif
  }

  mtcp_sys_close (mapsfd);

  return (VA)histackaddr;

}

#else
/* Added by Gene Cooperman */
static VA highest_userspace_address (VA *vdso_addr)
{
  Area area;
  VA area_end = 0;
  int mapsfd;
  char *p;

  mapsfd = open ("/proc/self/maps", O_RDONLY);
  if (mapsfd < 0) {
    mtcp_printf ("mtcp highest_userspace_address:"
            " error opening /proc/self/maps: %s\n", strerror (errno));
    mtcp_abort ();
  }

  *vdso_addr = NULL; /* Default to NULL if not found. */
  while (readmapsline (mapsfd, &area)) {
    p = strstr (area.name, "[stack]");
    if (p != NULL)
      area_end = (VA)area.addr + area.size;
    p = strstr (area.name, "[vdso]");
    if (p != NULL)
      *vdso_addr = area.addr;
    p = strstr (area.name, "[vsyscall]");
    if (p != NULL) /* vsyscall is highest segment, when it exists */
      return area.addr;
  }

  close (mapsfd);
  return area_end;
}
#endif

#if 1
static int open_shared_file(char* fileName)
{
  int i;
  int fd;
  int fIndex;
  char currentFolder[FILENAMESIZE];
  
  /* Find the starting index where the actual filename begins */
  for ( i=0; fileName[i] != '\0'; i++ ){
    if ( fileName[i] == '/' )
      fIndex = i+1;
  }
  
  /* We now try to create the directories structure from the give path */
  for ( i=0 ; i< fIndex; i++ ){
    if (fileName[i] == '/' && i > 0){
      int res;
      currentFolder[i] = '\0';
      res = mtcp_sys_mkdir(currentFolder,S_IRWXU);
      if (res<0 && mtcp_sys_errno != EEXIST ){
        mtcp_printf("mtcp_restart_nolibc open_shared_file: error %d creating directory %s in path of %s\n", mtcp_sys_errno, currentFolder, fileName);
	mtcp_abort();
      }
    }
    currentFolder[i] = fileName[i];
  }

  /* Create the file */
  fd = mtcp_sys_open(fileName,O_CREAT|O_RDWR,S_IRWXU);
  if (fd<0){
    mtcp_printf("mtcp_restart_nolibc open_shared_file: unable to create file %s\n", fileName);
    mtcp_abort();
  }
  return fd;
}
#endif
