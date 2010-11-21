/*****************************************************************************
 *   Copyright (C) 2006-2009 by Michael Rieker, Jason Ansel, Kapil Arya, and *
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
/*  Static part of restore - This gets linked in the libmtcp.so shareable image that gets loaded as part of the user's original 	*/
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
__attribute__ ((visibility ("hidden"))) char mtcp_ckpt_newname[MAXPATHLEN+1];
#define MAX_ARGS 50
__attribute__ ((visibility ("hidden"))) char *mtcp_restore_cmd_file;

__attribute__ ((visibility ("hidden"))) char *mtcp_restore_argv[MAX_ARGS+1];
__attribute__ ((visibility ("hidden"))) char *mtcp_restore_envp[MAX_ARGS+1];
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
static void readfile (void *buf, size_t size);
static void mmapfile(void *buf, size_t size, int prot, int flags);
static void skipfile(size_t size);
static void read_shared_memory_area_from_file(Area* area, int flags);
static VA highest_userspace_address (VA *vdso_addr, VA *vsyscall_addr,
                                     VA * stack_end_addr);
static int open_shared_file(char* fileName);
static void lock_file(int fd, char* name, short l_type);
// These will all go away when we use a linker to reserve space.
static VA global_vdso_addr = 0;

static void *mystrstr(char *string, char *substring) {
  for ( ; *string != '\0' ; string++) {
    char *ptr1, *ptr2;
    for (ptr1 = string, ptr2 = substring;
         *ptr1 == *ptr2 && *ptr2 != '\0';
         ptr1++, ptr2++) ;
    if (*ptr2 == '\0')
      return string;
  }
  return NULL;
}

/********************************************************************************************************************************/
/*																*/
/*  This routine is called executing on the temporary stack									*/
/*  It performs the actual restore of everything (except the libmtcp.so area)							*/
/*																*/
/********************************************************************************************************************************/

__attribute__ ((visibility ("hidden"))) void mtcp_restoreverything (void)

{
  int rc;
  VA holebase, highest_va;
  VA vdso_addr = (VA)NULL, vsyscall_addr = (VA)NULL,
	stack_end_addr = (VA)NULL; /* VA = virtual address */
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
  if (new_brk == (void *)-1) {
    mtcp_printf( "mtcp_restoreverything: sbrk(%p): errno:  %d (bad heap)\n",
		 mtcp_saved_break, mtcp_sys_errno );
    mtcp_abort();
  }
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
  DPRINTF(("current_brk: %p; mtcp_saved_break: %p; new_brk: %p\n",
	   current_brk, mtcp_saved_break, new_brk));

  /* Unmap everything except for this image as everything we need
   *   is contained in the libmtcp.so image.
   * Unfortunately, in later Linuxes, it's important also not to wipe
   *   out [vsyscall] if it exists (we may not have permission to remove it).
   *   In any case, [vsyscall] is the highest section if it exists.
   * Further, if the [vdso] when we restart is different from the old
   *   [vdso] that was saved at checkpoint time, then we need to keep
   *   both of them.  The old one may be needed if we're returning from
   *   a system call at checkpoint time.  The new one is needed for future
   *   system calls.
   * Highest_userspace_address is determined heuristically.  Primarily, it
   *   was intended to make sure we don't overwrite [vdso] or [vsyscall].
   *   But it was heuristically chosen as a constant (works for earlier
   *   Linuxes), or as the end of stack.  Probably, we should review that,
   *   and just make it beginning of [vsyscall] where that exists.
   */

  holebase  = (VA)mtcp_shareable_begin;
  holebase &= -PAGE_SIZE;
  asm volatile (CLEAN_FOR_64_BIT(xor %%eax,%%eax ; movw %%ax,%%fs)
				: : : CLEAN_FOR_64_BIT(eax)); // the unmaps will wipe what it points to anyway
  // asm volatile (CLEAN_FOR_64_BIT(xor %%eax,%%eax ; movw %%ax,%%gs) : : : CLEAN_FOR_64_BIT(eax)); // so make sure we get a hard failure just in case
                                                                  // ... it's left dangling on something I want

  /* Unmap from address 0 to holebase, except for [vdso] section */
  vdso_addr = vsyscall_addr = stack_end_addr = 0;
  highest_va = highest_userspace_address(&vdso_addr, &vsyscall_addr,
					 &stack_end_addr);
  if (stack_end_addr == 0) /* 0 means /proc/self/maps doesn't mark "[stack]" */
    highest_va = HIGHEST_VA;
  else
    highest_va = stack_end_addr;
  DPRINTF(("new_brk (end of heap): %p, holebase (libmtcp.so): %p, stack_end_addr: %p\n"
	   "    vdso_addr: %p, highest_va: %p, vsyscall_addr: %p\n",
	   new_brk, holebase, stack_end_addr,
	   vdso_addr, highest_va, vsyscall_addr));

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

  /* Unmap from address holebase to highest_va, except for [vdso] section */
  /* Value of mtcp_shareable_end (end of data segment) can change from before */
  holebase  = (VA)mtcp_shareable_end;
  holebase  = (holebase + PAGE_SIZE - 1) & -PAGE_SIZE;
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
  DPRINTF(("\n")); /* end of munmap */

  /* Read address of mtcp.c's finishrestore routine */

  readcs (CS_FINISHRESTORE);
  readfile (&finishrestore, sizeof finishrestore);

  /* Restore file descriptors */

  DPRINTF (("mtcp restoreverything*: restoring file descriptors\n"));
  readfiledescrs ();                              // restore files

  /* Restore memory areas */

  global_vdso_addr = vdso_addr;/* This global var goes away when linker used. */
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

  /* Jump to finishrestore in original program's libmtcp.so image */

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
  struct stat statbuf;

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
        mtcp_printf ("mtcp readfiledescrs: error %d duping checkpoint file fd %d\n",
		     mtcp_sys_errno, mtcp_restore_cpfd);
        mtcp_abort ();
      }
      mtcp_restore_cpfd = flags;
      DPRINTF (("mtcp readfiledescrs*: cpfd changed to %d\n",
	        mtcp_restore_cpfd));
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
  int flags, imagefd;
  void *mmappedat;
/* make check:  stale-fd and forkexec fail (and others?) with this turned on. */
#if 0
  /* If not using gzip decompression, then use mmapfile instead of readfile. */
  int do_mmap_ckpt_image = (mtcp_restore_gzip_child_pid == -1);
#else
  int do_mmap_ckpt_image = 0;
#endif

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
        DPRINTF (("mtcp restoreverything*: restoring anonymous area %p at %p\n", area.size, area.addr));
      } else {
        DPRINTF (("mtcp restoreverything*: restoring to non-anonymous area from anonymous area %p at %p from %s + 0x%X\n", area.size, area.addr, area.name, area.offset));
      }
      /* POSIX says mmap would unmap old memory.  Munmap never fails if args
       * are valid.  Can we unmap vdso and vsyscall in Linux?  Used to use
       * mtcp_safemmap here to check for address conflicts.
       */
      mmappedat = mtcp_sys_mmap (area.addr, area.size, area.prot | PROT_WRITE,
				 area.flags, imagefd, area.offset);
      if (mmappedat == MAP_FAILED) {
        DPRINTF(("mtcp_restart_nolibc: error %d mapping %p bytes at %p\n",
		 mtcp_sys_errno, area.size, area.addr));

	try_skipping_existing_segment = 1;
      }
      if (mmappedat != area.addr && !try_skipping_existing_segment) {
        mtcp_printf ("mtcp_restart_nolibc: area at %p got mmapped to %p\n",
		     area.addr, mmappedat);
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
# ifdef __x86_64__
        // This fails on teracluster.  Presumably extra symbols cause overflow.
        skipfile (area.size);
# else
      // With Red Hat Release 5.2, Red Hat allows vdso to go almost anywhere.
      // If we were unlucky and it was randomized onto our memory area, re-exec.
      // In the future, a cleaner fix will be a linker script to reserve
      //   or even load our own memory section at fixed addresses, so that
      //   vdso will be placed elsewhere.
      // This patch is not safe, because there are unnamed sections that
      //   might be required.  But early 32-bit Linux kernels also don't name
      //   [vdso] in the /proc filesystem, and it's safe to skipfile() there.
      // This code is based on what's in mtcp_check_vdso.c .
      { if (area.name[0] == '/' /* If not null string, not [stack] or [vdso] */
            && global_vdso_addr >= (VA)area.addr
            && global_vdso_addr < (VA)area.addr + area.size
           ) {
          DPRINTF(("randomized vdso conflict; retrying\n"));
          mtcp_sys_close (mtcp_restore_cpfd);
          mtcp_restore_cpfd = -1;
          if (-1 == mtcp_sys_execve(mtcp_restore_cmd_file,
                                    mtcp_restore_argv, mtcp_restore_envp))
            DPRINTF(("execve failed.  Restart may fail.\n"));
        } else
          skipfile (area.size);
      }
# endif
#endif
      else {
        /* This mmapfile after prev. mmap is okay; use same args again.
         *  Posix says prev. map will be munmapped.
         */
/* ANALYZE THE CONDITION FOR DOING mmapfile MORE CAREFULLY. */
        if (do_mmap_ckpt_image
            && mystrstr(area.name, "[vdso]")
            && mystrstr(area.name, "[vsyscall]"))
          mmapfile (area.addr, area.size, area.prot | PROT_WRITE, area.flags);
        else
          readfile (area.addr, area.size);
        if (!(area.prot & PROT_WRITE))
          if (mtcp_sys_mprotect (area.addr, area.size, area.prot) < 0) {
            mtcp_printf ("mtcp_restart_nolibc: error %d write-protecting %p bytes at %p\n",
			 mtcp_sys_errno, area.size, area.addr);
            mtcp_abort ();
          }
      }

      /* Close image file (fd only gets in the way) */
      if (!(area.flags & MAP_ANONYMOUS)) mtcp_sys_close (imagefd);
    }

    /* CASE NOT MAP_ANONYMOUS:						     */
    /* Otherwise, we mmap the original file contents to the area */

    else {
      DPRINTF (("mtcp restoreverything*: restoring mapped area %p at %p to %s + 0x%X\n", area.size, area.addr, area.name, area.offset));
      flags = 0;            // see how to open it based on the access required
      // O_RDONLY = 00
      // O_WRONLY = 01
      // O_RDWR   = 02
      if (area.prot & PROT_WRITE) flags = O_WRONLY;
      if (area.prot & (PROT_EXEC | PROT_READ)){
        flags = O_RDONLY;
        if (area.prot & PROT_WRITE) flags = O_RDWR;
      }

      if (area.prot & MAP_SHARED) {
        read_shared_memory_area_from_file(&area, flags);

      } else { /* not MAP_ANONYMOUS, not MAP_SHARED */
        imagefd = mtcp_sys_open (area.name, flags, 0);  // open it

        /* CASE NOT MAP_ANONYMOUS, MAP_PRIVATE, backing file doesn't exist:	     */
        if (imagefd < 0) {
          mtcp_printf ("mtcp_restart_nolibc: error %d opening mmap file %s\n",
              mtcp_sys_errno, area.name);
          mtcp_abort ();
        }

        /* CASE NOT MAP_ANONYMOUS, and MAP_PRIVATE,		     */
        mmappedat = mtcp_sys_mmap (area.addr, area.size, area.prot, 
            area.flags, imagefd, area.offset);
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
        mtcp_sys_close (imagefd); // don't leave dangling fd

        readcs (CS_AREACONTENTS);

        // If we have write permission on file and memory area (data segment),
	// then we use data in checkpoint image.
        // In the case of DMTCP, multiple processes may duplicate this work.
        if ( (imagefd = mtcp_sys_open(area.name, O_WRONLY, 0)) >= 0
              && ( (flags == O_WRONLY || flags == O_RDWR) ) ) {
          mtcp_printf ("mtcp_restart_nolibc: mapping %s with data from ckpt image\n",
              area.name);
          readfile (area.addr, area.size);
        }
        // If we have no write permission on file, then we should use data
        //   from version of file at restart-time (not from checkpoint-time).
        // Because Linux library files have execute permission,
        //   the dynamic libraries from time of checkpoint will be used.
        // NOTE: man 2 access: access  may  not  work  correctly on NFS file
        //   systems with UID mapping enabled, because UID mapping is done
        //   on the server and hidden from the client, which checks permissions.
        else {
	  /* read-exec permission ==> executable library, unlikely to change;
	   * For example, gconv-modules.cache is shared with read-exec perm.
	   * If read-only permission, warn user that we're using curr. file.
	   */
	  if (-1 == mtcp_sys_access(area.name, X_OK))
            mtcp_printf ("MTCP: mtcp_restart_nolibc: mapping current version "
              "of %s into memory;\n"
              "  _not_ file as it existed at time of checkpoint.\n"
              "  Change %s:%d and re-compile, if you want different "
              "behavior.\n",
              area.name, __FILE__, __LINE__);

          // We want to skip the checkpoint file pointer
          // and move to the end of the shared file data.  We can't
          // use lseek() function as it can fail if we are using a pipe to read
          // the contents of checkpoint file (we might be using gzip to
          // uncompress checkpoint file on the fly).  Thus we have to read or
          // skip contents using skipfile().
          skipfile (area.size);
        }
        if (imagefd >= 0)
	  mtcp_sys_close (imagefd); // don't leave dangling fd
      }
    }

    if (area.name && mystrstr(area.name, "[heap]")
        && mtcp_sys_brk(NULL) != area.addr + area.size)
      DPRINTF(("WARNING: break (%p) not equal to end of heap (%p)\n",
               mtcp_sys_brk(NULL), area.addr + area.size));
  }
}

/*
 * CASE NOT MAP_ANONYMOUS, MAP_SHARED :
 *
 * If the shared file does NOT exist on the system, the restart process creates
 * the file on the disk and writes the contents from the ckpt image into this
 * recreated file. The file is later mapped into memory with MAP_SHARED and
 * correct protection flags.
 *
 * If the file already exists on the disk, there are two possible scenerios as
 * follows:
 * 1. The shared memory has WRITE access: In this case it is possible that the
 *    file was modified by the checkpoint process and so we restore the file
 *    contents from the checkpoint image. In doing so, we can fail however if
 *    we do not have sufficient access permissions.
 * 2. The shared memory has NO WRITE access: In this case, we use the current
 *    version of the file rather than the one that existed at checkpoint time.
 *    We map the file with correct flags and discard the checkpointed copy of
 *    the file contents.
 *
 * Other than these, if we can't access the file, we print an error message
 * and quit.
 */
static void read_shared_memory_area_from_file(Area* area, int flags)
{
  void *mmappedat;
  int areaContentsAlreadyRead = 0;
  int imagefd, rc;

  if (!(area->prot & MAP_SHARED)) {
    mtcp_printf("read_shared_memory_area_from_file: Illegal function call\n");
    mtcp_abort();
  }

  imagefd = mtcp_sys_open (area->name, flags, 0);  // open it

  if (imagefd < 0 && mtcp_sys_errno != ENOENT) {
    mtcp_printf ("mtcp_restart_nolibc: error %d opening mmap file %s"
                 "with flags:%d\n", mtcp_sys_errno, area->name, flags);
    mtcp_abort();
  } 

  if (imagefd < 0) {

    // If the shared file doesn't exist on the disk, we try to create it
    DPRINTF(("mtcp restoreverything*: Shared file %s not found, Creating new\n",area->name));

    /* Dangerous for DMTCP:  Since file is created with O_CREAT,    */
    /* hopefully, a second process should ignore O_CREAT and just     */
    /*  duplicate the work of the first process, with no ill effect.*/
    imagefd = open_shared_file(area->name);

    /* Acquire write lock on the file before writing anything to it 
     * If we don't, then there is a weird RACE going on between the
     * restarting processes which causes problems with mmap()ed area for
     * this file and hence the restart fails. We still don't know the
     * reason for it.                                       --KAPIL 
     * NOTE that we don't need to unlock the file as it will be
     * automatically done when we close it.
     */
    lock_file(imagefd, area->name, F_WRLCK);

    // create a temp area in the memory exactly of the size of the
    // shared file.  We read the contents of the shared file from
    // checkpoint file(.mtcp) into system memory. From system memory,
    // the contents are written back to newly created replica of the shared
    // file (at the same path where it used to exist before checkpoint).
    mmappedat = mtcp_sys_mmap (area->addr, area->size, PROT_READ | PROT_WRITE, 
                               MAP_PRIVATE | MAP_ANONYMOUS, imagefd, 
                               area->offset);
    if (mmappedat == MAP_FAILED) {
      mtcp_printf ("mtcp_restart_nolibc: error %d mapping temp memory at %p\n",
          mtcp_sys_errno, area->addr);
      mtcp_abort ();
    }

    readcs (CS_AREACONTENTS);
    readfile (area->addr, area->size);
    areaContentsAlreadyRead = 1;

    if ( mtcp_sys_write(imagefd, area->addr,area->size) < 0 ){
      mtcp_printf ("mtcp_restart_nolibc: error %d creating mmap file %s\n",
          mtcp_sys_errno, area->name);
      mtcp_abort();
    }

    // unmap the temp memory allocated earlier
    rc = mtcp_sys_munmap (area->addr, area->size);
    if (rc == -1) {
      mtcp_printf ("mtcp_restart_nolibc: error %d unmapping temp memory at %p\n",
          mtcp_sys_errno, area->addr);
      mtcp_abort ();
    }

    // set file permissions as per memory area protection.
    int fileprot = 0;
    if (area->prot & PROT_READ)  fileprot |= S_IRUSR;
    if (area->prot & PROT_WRITE) fileprot |= S_IWUSR;
    if (area->prot & PROT_EXEC)  fileprot |= S_IXUSR;
    mtcp_sys_fchmod(imagefd, fileprot);

    //close the file
    mtcp_sys_close(imagefd);

    // now open the file again, this time with appropriate flags
    imagefd = mtcp_sys_open (area->name, flags, 0);
    if (imagefd < 0){
      mtcp_printf ("mtcp_restart_nolibc: error %d opening mmap file %s\n",
          mtcp_sys_errno, area->name);
      mtcp_abort ();
    }
  } else { /* else file exists */
    /* Acquire read lock on the shared file before doing an mmap. See
     * detailed comments above.
     */
    DPRINTF(("Acquiring lock on shared file :%s\n", area->name));
    lock_file(imagefd, area->name, F_RDLCK); 
    DPRINTF(("After Acquiring lock on shared file :%s\n", area->name));
  }

  mmappedat = mtcp_sys_mmap (area->addr, area->size, area->prot, 
                             area->flags, imagefd, area->offset);
  if (mmappedat == MAP_FAILED) {
    mtcp_printf ("mtcp_restart_nolibc: error %d mapping %s offset %d at %p\n",
                 mtcp_sys_errno, area->name, area->offset, area->addr);
    mtcp_abort ();
  }
  if (mmappedat != area->addr) {
    mtcp_printf ("mtcp_restart_nolibc: area at %p got mmapped to %p\n",
                 area->addr, mmappedat);
    mtcp_abort ();
  }

  if ( areaContentsAlreadyRead == 0 ){
    readcs (CS_AREACONTENTS);

#if 0
    // If we have write permission or execute permission on file,
    //   then we use data in checkpoint image,
    // If MMAP_SHARED, this reverts the file to data at time of checkpoint.
    // In the case of DMTCP, multiple processes may duplicate this work.
    // NOTE: man 2 access: access  may  not  work  correctly on NFS file
    //   systems with UID mapping enabled, because UID mapping is done
    //   on the server and hidden from the client, which checks permissions.
    /* if (flags == O_WRONLY || flags == O_RDWR) */
    if ( ( (imagefd = mtcp_sys_open(area->name, O_WRONLY, 0)) >= 0
          && ( (flags == O_WRONLY || flags == O_RDWR) ) )
        || (0 == mtcp_sys_access(area->name, X_OK)) ) {

      mtcp_printf ("mtcp_restart_nolibc: mapping %s with data from ckpt image\n",
          area->name);
      readfile (area->addr, area->size);
      mtcp_sys_close (imagefd); // don't leave dangling fd
    }
#else
    if (area->prot & PROT_WRITE) {
      mtcp_printf ("mtcp_restart_nolibc: mapping %s with data from ckpt image\n",
                   area->name);
      readfile (area->addr, area->size);
    } 
#endif 
    // If we have no write permission on file, then we should use data
    //   from version of file at restart-time (not from checkpoint-time).
    // Because Linux library files have execute permission,
    //   the dynamic libraries from time of checkpoint will be used.

    // If we haven't created the file (i.e. the shared file _does_ exist
    // when this process wants to map it) and the memory area does not have
    // WRITE access, we want to skip the checkpoint
    // file pointer and move to the end of the shared file. We can not
    // use lseek() function as it can fail if we are using a pipe to read
    // the contents of checkpoint file (we might be using gzip to
    // uncompress checkpoint file on the fly). Thus we have to read or
    // skip contents using the following code.

    // NOTE: man 2 access: access  may  not  work  correctly on NFS file
    //   systems with UID mapping enabled, because UID mapping is done
    //   on the server and hidden from the client, which checks permissions.
    else {
      /* read-exec permission ==> executable library, unlikely to change;
       * For example, gconv-modules.cache is shared with read-exec perm.
       * If read-only permission, warn user that we're using curr. file.
       */
      if (imagefd >= 0 && -1 == mtcp_sys_access(area->name, X_OK))
        mtcp_printf ("MTCP: mtcp_restart_nolibc: mapping current version "
          "of %s into memory;\n"
          "  _not_ file as it existed at time of checkpoint.\n"
          "  Change %s:%d and re-compile, if you want different "
          "behavior.\n",
          area->name, __FILE__, __LINE__);
      skipfile (area->size);
    }
  }
  if (imagefd >= 0)
    mtcp_sys_close (imagefd); // don't leave dangling fd in way of other stuff
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

static void readfile(void *buf, size_t size)
{
    ssize_t rc;
    size_t ar = 0;
    int tries = 0;

    while(ar != size)
    {
        rc = mtcp_sys_read(mtcp_restore_cpfd, buf + ar, size - ar);
        if (rc < 0)
        {
            mtcp_printf("mtcp_restart_nolibc readfile: error %d reading checkpoint\n", mtcp_sys_errno);
            mtcp_abort();
        }
        else if (rc == 0)
        {
            mtcp_printf("mtcp_restart_nolibc readfile: only read %zu bytes instead of %zu from checkpoint file\n", ar, size);
	    if (tries++ >= 10) {
	        mtcp_printf("mtcp_restart_nolibc readfile:" \
			    " failed to read after 10 tries in a row.\n");
                mtcp_abort();
	    }
        }

        ar += rc;
    }
}

static void mmapfile(void *buf, size_t size, int prot, int flags)
{
    void *addr;
    off_t rc;
    size_t ar;
    ar = 0;

    /* Use mmap for this portion of checkpoint image. */
    addr = mtcp_sys_mmap(buf, size, prot, flags, mtcp_restore_cpfd, 0);
    if (addr != buf) {
        if (addr == MAP_FAILED)
            mtcp_printf("mtcp_restart_nolibc mmapfile:"
                        " error %d reading checkpoint file\n", mtcp_sys_errno);
        else
            mtcp_printf("mmapfile: Requested address %p, but got address %p\n",
                        buf, addr);
        mtcp_abort();
    }
    /* Now update mtcp_restore_cpfd so as to work the same way as readfile() */
    rc = mtcp_sys_lseek(mtcp_restore_cpfd, size, SEEK_CUR);
}

static void skipfile(size_t size)
{
    size_t ar;
    ssize_t rc;
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
            mtcp_printf("mtcp_restart_nolibc skipfile: only skipped %zu bytes instead of %zu from checkpoint file\n", ar, size);
            mtcp_abort();
        }

        ar += rc;
    }
}

#if 1
/* Modelled after mtcp_safemmap.  - Gene */
static VA highest_userspace_address (VA *vdso_addr, VA *vsyscall_addr,
				     VA *stack_end_addr)
{
  char c;
  int mapsfd, i;
  VA endaddr, startaddr;
  VA highaddr = 0; /* high stack address should be highest userspace addr */
  const char *stackstring = "[stack]";
  const char *vdsostring = "[vdso]";
  const char *vsyscallstring = "[vsyscall]";
  const int bufsize = sizeof "[vsyscall]"; /* largest of last 3 strings */
  char buf[bufsize];

  buf[0] = '\0';

  /* Scan through the mappings of this process */

  mapsfd = mtcp_sys_open ("/proc/self/maps", O_RDONLY, 0);
  if (mapsfd < 0) {
    mtcp_printf("couldn't open /proc/self/maps\n");
    mtcp_abort();
  }

  *vdso_addr = (VA)NULL;
  while (1) {

    /* Read a line from /proc/self/maps */

    c = mtcp_readhex (mapsfd, &startaddr);
    if (c == '\0') break;
    if (c != '-') continue; /* skip to next line */
    c = mtcp_readhex (mapsfd, &endaddr);
    if (c == '\0') break;
    if (c != ' ') continue; /* skip to next line */

    while ((c != '\0') && (c != '\n')) {
      if (c != ' ') {
        for (i = 0; (i < bufsize) && (c != ' ')
		    && (c != 0) && (c != '\n'); i++) {
          buf[i] = c;
          c = mtcp_readchar (mapsfd);
        }
      } else {
        c = mtcp_readchar (mapsfd);
      }
    }
    /* i < bufsize - 1 */
    buf[i] = '\0';

    /* emulate strcmp */
    for (i = 0; i < sizeof "[stack]"; i++)
      if (buf[i] != stackstring[i])
        break;
    if (i == sizeof "[stack]") {
      *stack_end_addr = endaddr;
      highaddr = endaddr;  /* We found "[stack]" in /proc/self/maps */
    }

    for (i = 0; i < sizeof "[vdso]"; i++)
      if (buf[i] != vdsostring[i])
        break;
    if (i == sizeof "[vdso]") {
      *vdso_addr = startaddr;
      highaddr = endaddr;  /* We found "[vdso]" in /proc/self/maps */
    }

    for (i = 0; i < sizeof "[vsyscall]"; i++)
      if (buf[i] != vsyscallstring[i])
        break;
    if (i == sizeof "[vsyscall]") {
      *vsyscall_addr = startaddr;
      highaddr = endaddr;  /* We found "[vsyscall]" in /proc/self/maps */
    }
  }

  mtcp_sys_close (mapsfd);

  return (VA)highaddr;
}

#else
/* Added by Gene Cooperman */
static VA highest_userspace_address (VA *vdso_addr, VA *vsyscall_address,
				     VA *stack_end_addr)
{
  Area area;
  VA area_end = 0;
  int mapsfd;
  char *p;

  mapsfd = open ("/proc/self/maps", O_RDONLY);
  if (mapsfd < 0) {
    mtcp_printf ("mtcp highest_userspace_address:"
            " error opening /proc/self/maps: errno: %d\n", errno);
    mtcp_abort ();
  }

  *vdso_addr = NULL; /* Default to NULL if not found. */
  while (readmapsline (mapsfd, &area)) {
    /* Gcc expands strstr() inline, but it's safer to use our own function. */
    p = mystrstr (area.name, "[stack]");
    if (p != NULL)
      area_end = (VA)area.addr + area.size;
    p = mystrstr (area.name, "[vdso]");
    if (p != NULL)
      *vdso_addr = area.addr;
    p = mystrstr (area.name, "[vsyscall]");
    if (p != NULL)
      *vsyscall_addr = area.addr;
    p = mystrstr (area.name, "[stack]");
    if (p != NULL)
      *stack_end_addr = area.addr + addr.size;
    p = mystrstr (area.name, "[vsyscall]");
    if (p != NULL) /* vsyscall is highest section, when it exists */
      return area.addr;
  }

  close (mapsfd);
  return area_end;
}
#endif

#if 1
static void lock_file(int fd, char* name, short l_type)
{
  struct flock fl;

  fl.l_type   = l_type;   /* F_RDLCK, F_WRLCK, F_UNLCK    */
  fl.l_whence = SEEK_SET; /* SEEK_SET, SEEK_CUR, SEEK_END */
  fl.l_start  = 0;        /* Offset from l_whence         */
  fl.l_len    = 0;        /* length, 0 = to EOF           */

  int result = -1;
  mtcp_sys_errno = 0;
  while (result == -1 || mtcp_sys_errno == EINTR )
    result = mtcp_sys_fcntl3(fd, F_SETLKW, &fl);  /* F_GETLK, F_SETLK, F_SETLKW */

  if ( result == -1 ) {
    mtcp_printf("mtcp_restart_nolibc lock_file: error %d locking shared file: %s\n",
        mtcp_sys_errno, name);
    mtcp_abort();
  }
}

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
      res = mtcp_sys_mkdir(currentFolder, S_IRWXU);
      if (res<0 && mtcp_sys_errno != EEXIST ){
        mtcp_printf("mtcp_restart_nolibc open_shared_file: error %d creating directory %s in path of %s\n", mtcp_sys_errno, currentFolder, fileName);
	mtcp_abort();
      }
    }
    currentFolder[i] = fileName[i];
  }

  /* Create the file */
  fd = mtcp_sys_open(fileName, O_CREAT|O_RDWR, S_IRUSR|S_IWUSR);
  if (fd<0){
    mtcp_printf("mtcp_restart_nolibc open_shared_file: unable to create file %s\n", fileName);
    mtcp_abort();
  }
  return fd;
}
#endif
