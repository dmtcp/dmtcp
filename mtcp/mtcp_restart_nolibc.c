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

/***************************************************************************** 
 *
 *  Static part of restore - This gets linked in the libmtcp.so shareable image
 *  that gets loaded as part of the user's original application. The makefile
 *  appends all the needed system call routines onto the end of this module so
 *  it will link with no undefined symbols.  This allows the restore procedure
 *  to simply read this object image from the restore file, load it to the same
 *  address it was in the user's original application, and jump to it.
 *
 *  If we didn't assemble it all as one module, the idiot loader would make
 *  references to glibc routines go to libc.so even though there are object
 *  modules linked in with those routines defined.
 *
 *****************************************************************************/

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
  int mtcp_restore_cpfd = -1; // '-1' puts it in regular data instead of common
__attribute__ ((visibility ("hidden")))
  int mtcp_restore_verify = 0; // 0: normal restore; 1: verification restore
__attribute__ ((visibility ("hidden")))
  pid_t mtcp_restore_gzip_child_pid = -1; // '-1' puts it in regular data
                                          // instead of common
__attribute__ ((visibility ("hidden"))) char mtcp_ckpt_newname[PATH_MAX+1];
#define MAX_ARGS 50
__attribute__ ((visibility ("hidden"))) char *mtcp_restore_cmd_file;

__attribute__ ((visibility ("hidden"))) char *mtcp_restore_argv[MAX_ARGS+1];
__attribute__ ((visibility ("hidden"))) char *mtcp_restore_envp[MAX_ARGS+1];
__attribute__ ((visibility ("hidden")))
  VA mtcp_saved_break = NULL;  // saved brk (0) value
__attribute__ ((visibility ("hidden")))
  char mtcp_saved_working_directory[PATH_MAX+1];

  /* These two are used by the linker script to define the beginning and end of
   * the image.
   * The '.long 0' is needed so shareable_begin>0 as the linker is too st00pid
   * to relocate a zero. */

asm (".section __shareable_begin ; .globl mtcp_shareable_begin \
                                 ; .long 0 ; mtcp_shareable_begin:");
asm (".section __shareable_end   ; .globl mtcp_shareable_end \
                                 ; .long 0  ; mtcp_shareable_end:");
asm (".text");

	/* Internal routines */

static void readfiledescrs (void);
static void readmemoryareas (void);
static void mmapfile(void *buf, size_t size, int prot, int flags);
static void skipfile(size_t size);
static void read_shared_memory_area_from_file(Area* area, int flags);
static VA highest_userspace_address (VA *vdso_addr, VA *vsyscall_addr,
                                     VA * stack_end_addr);
static char* fix_filename_if_new_cwd(char* filename);
static int open_shared_file(char* filename);
static void lock_file(int fd, char* name, short l_type);
// These will all go away when we use a linker to reserve space.
static VA global_vdso_addr = 0;

/***************************************************************************** 
 *
 *  This routine is called executing on the temporary stack
 *  It performs the actual restore of everything (except the libmtcp.so area)
 *
 *****************************************************************************/

__attribute__ ((visibility ("hidden"))) void mtcp_restoreverything (void)

{
  int rc;
  VA holebase, highest_va;
  VA vdso_addr = NULL, vsyscall_addr = NULL, stack_end_addr = NULL;
  VA current_brk;
  VA new_brk;
  void (*finishrestore) (void);

  DPRINTF("Entering mtcp_restart_nolibc.c:mtcp_restoreverything\n");

  /* The kernel (2.6.9 anyway) has a variable mm->brk that we should restore.
   * The only access we have is brk() which basically sets mm->brk to the new
   * value, but also has a nasty side-effect (as far as we're concerned) of
   * mmapping an anonymous section between the old value of mm->brk and the
   * value being passed to brk().  It will munmap the bracketed memory if the
   * value being passed is lower than the old value.  But if zero, it will
   * return the current mm->brk value.
   *
   * So we're going to restore the brk here.  As long as the current mm->brk
   * value is below the static restore region, we're ok because we 'know' the
   * restored brk can't be in the static restore region, and we don't care if
   * the kernel mmaps something or munmaps something because we're going to wipe
   * it all out anyway.
   */

  current_brk = mtcp_sys_brk (NULL);
  if ((current_brk > mtcp_shareable_begin) &&
      (mtcp_saved_break < mtcp_shareable_end)) {
    MTCP_PRINTF("current_brk %p, mtcp_saved_break %p, mtcp_shareable_begin %p,"
                " mtcp_shareable_end %p\n", 
                 current_brk, mtcp_saved_break, mtcp_shareable_begin,
                 mtcp_shareable_end);
    mtcp_abort ();
  }

  new_brk = mtcp_sys_brk (mtcp_saved_break);
  if (new_brk == (VA)-1) {
    MTCP_PRINTF("sbrk(%p): errno: %d (bad heap)\n",
		 mtcp_saved_break, mtcp_sys_errno );
    mtcp_abort();
  }
  if (new_brk != mtcp_saved_break) {
    if (new_brk == current_brk && new_brk > mtcp_saved_break)
      DPRINTF("new_brk == current_brk == %p\n  saved_break, %p,"
              " is strictly smaller; data segment not extended.\n",
              new_brk, mtcp_saved_break);
    else {
      MTCP_PRINTF("error: new break (%p) != saved break  (%p)\n",
                  current_brk, mtcp_saved_break);
      mtcp_abort ();
    }
  }
  DPRINTF("current_brk: %p; mtcp_saved_break: %p; new_brk: %p\n",
          current_brk, mtcp_saved_break, new_brk);

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

  holebase  = mtcp_shareable_begin;
  holebase = (VA)((unsigned long int)holebase & -MTCP_PAGE_SIZE);
  // the unmaps will wipe what it points to anyway
  asm volatile (CLEAN_FOR_64_BIT(xor %%eax,%%eax ; movw %%ax,%%fs)
				: : : CLEAN_FOR_64_BIT(eax));

  // so make sure we get a hard failure just in case
  // ... it's left dangling on something I want
  // asm volatile (CLEAN_FOR_64_BIT(xor %%eax,%%eax ; movw %%ax,%%gs)
  //                                : : : CLEAN_FOR_64_BIT(eax));

  /* Unmap from address 0 to holebase, except for [vdso] section */
  vdso_addr = vsyscall_addr = stack_end_addr = 0;
  highest_va = highest_userspace_address(&vdso_addr, &vsyscall_addr,
					 &stack_end_addr);
  if (stack_end_addr == 0) /* 0 means /proc/self/maps doesn't mark "[stack]" */
    highest_va = HIGHEST_VA;
  else
    highest_va = stack_end_addr;
  DPRINTF("new_brk (end of heap): %p, holebase (libmtcp.so): %p,\n"
          " stack_end_addr: %p, vdso_addr: %p, highest_va: %p,\n"
          " vsyscall_addr: %p\n",
	  new_brk, holebase, stack_end_addr,
	  vdso_addr, highest_va, vsyscall_addr);

  if (vdso_addr != NULL && vdso_addr < holebase) {
    DPRINTF("unmapping %p..%p, %p..%p\n",
            NULL, vdso_addr-1, vdso_addr+MTCP_PAGE_SIZE, holebase - 1);
    rc = mtcp_sys_munmap (NULL, (size_t)vdso_addr);
    rc |= mtcp_sys_munmap (vdso_addr + MTCP_PAGE_SIZE,
			   holebase - vdso_addr - MTCP_PAGE_SIZE);
  } else {
    DPRINTF("unmapping 0..%p\n", holebase - 1);
    rc = mtcp_sys_munmap (NULL, holebase);
  }
  if (rc == -1) {
      MTCP_PRINTF("error %d unmapping from 0 to %p\n",
                  mtcp_sys_errno, holebase);
      mtcp_abort ();
  }

  /* Unmap from address holebase to highest_va, except for [vdso] section */
  /* Value of mtcp_shareable_end (end of data segment) can change from before */
  holebase  = mtcp_shareable_end;
  holebase  = (VA)((unsigned long int)(holebase + MTCP_PAGE_SIZE - 1)
		   & -MTCP_PAGE_SIZE);
  if (vdso_addr != NULL && vdso_addr + MTCP_PAGE_SIZE <= highest_va) {
    if (vdso_addr > holebase) {
      DPRINTF("unmapping %p..%p, %p..%p\n",
              holebase, vdso_addr-1, vdso_addr + MTCP_PAGE_SIZE,
              highest_va - 1);
      rc = mtcp_sys_munmap (holebase, vdso_addr - holebase);
      rc |= mtcp_sys_munmap (vdso_addr + MTCP_PAGE_SIZE,
                             highest_va - vdso_addr - MTCP_PAGE_SIZE);
    } else {
      DPRINTF("unmapping %p..%p\n", holebase, highest_va - 1);
      if (highest_va < holebase) {
        MTCP_PRINTF("error unmapping: highest_va(%p) < holebase(%p)\n",
                    highest_va, holebase);
        mtcp_abort ();
      }
      rc = mtcp_sys_munmap (holebase, highest_va - holebase);
    }
  }
  if (rc == -1) {
      MTCP_PRINTF("error %d unmapping from %p by %p bytes\n",
                  mtcp_sys_errno, holebase, highest_va - holebase);
      mtcp_abort ();
  }
  DPRINTF("\n"); /* end of munmap */

#ifdef FAST_CKPT_RST_VIA_MMAP
  fastckpt_prepare_for_restore(mtcp_restore_cpfd);
  finishrestore = (void*) fastckpt_get_finishrestore();
#else
  /* Read address of mtcp.c's finishrestore routine */

  mtcp_readcs (mtcp_restore_cpfd, CS_FINISHRESTORE);
  mtcp_readfile(mtcp_restore_cpfd, &finishrestore, sizeof finishrestore);

  /* Restore file descriptors */

  DPRINTF("restoring file descriptors\n");
  readfiledescrs ();                              // restore files
#endif

  /* Restore memory areas */

  global_vdso_addr = vdso_addr;/* This global var goes away when linker used. */
  DPRINTF("restoring memory areas\n");
  readmemoryareas ();

  /* Everything restored, close file and finish up */

  DPRINTF("close cpfd %d\n", mtcp_restore_cpfd);
  mtcp_sys_close (mtcp_restore_cpfd);
  mtcp_restore_cpfd = -1;
  DPRINTF("waiting on gzip_child_pid: %d\n", mtcp_restore_gzip_child_pid );
  // Calling waitpid here, but on 32-bit Linux, libc:waitpid() calls wait4()
  if( mtcp_restore_gzip_child_pid != -1 ) {
    if( mtcp_sys_wait4(mtcp_restore_gzip_child_pid , NULL, 0, NULL ) == -1 )
        DPRINTF("error wait4: errno: %d", mtcp_sys_errno);
    mtcp_restore_gzip_child_pid = -1;
  }

  DPRINTF("restore complete, resuming...\n");

  /* Jump to finishrestore in original program's libmtcp.so image */

  (*finishrestore) ();
}

/***************************************************************************** 
 *
 *  Read file descriptor info from checkpoint file and re-open and re-position
 *  files on the same descriptors
 * 
 *  Move the checkpoint file to a different fd if needed
 *
 *****************************************************************************/

static void readfiledescrs (void)

{
  char linkbuf[FILENAMESIZE];
  int fdnum, flags, linklen, tempfd;
  off_t offset;
  struct stat statbuf;
#ifdef FAST_CKPT_RST_VIA_MMAP
  MTCP_PRINTF("Unimplemented.\n");
  mtcp_abort();
#endif

  mtcp_readcs (mtcp_restore_cpfd, CS_FILEDESCRS);

  while (1) {

    /* Read parameters of next file to restore */

    mtcp_readfile(mtcp_restore_cpfd, &fdnum, sizeof fdnum);
    if (fdnum < 0) break;
    mtcp_readfile(mtcp_restore_cpfd, &statbuf, sizeof statbuf);
    mtcp_readfile(mtcp_restore_cpfd, &offset, sizeof offset);
    mtcp_readfile(mtcp_restore_cpfd, &linklen, sizeof linklen);
    if (linklen >= sizeof linkbuf) {
      MTCP_PRINTF("filename too long %d\n", linklen);
      mtcp_abort ();
    }
    mtcp_readfile(mtcp_restore_cpfd, linkbuf, linklen);
    linkbuf[linklen] = 0;

    DPRINTF("restoring %d -> %s\n", fdnum, linkbuf);

    /* Maybe it restores to same fd as we're using for checkpoint file. */
    /* If so, move the checkpoint file somewhere else.                  */

    if (fdnum == mtcp_restore_cpfd) {
      flags = mtcp_sys_dup (mtcp_restore_cpfd);
      if (flags < 0) {
        MTCP_PRINTF("error %d duping checkpoint file fd %d\n",
                    mtcp_sys_errno, mtcp_restore_cpfd);
        mtcp_abort ();
      }
      mtcp_restore_cpfd = flags;
      DPRINTF("cpfd changed to %d\n", mtcp_restore_cpfd);
    }

    /* Open the file on a temp fd */

    flags = O_RDWR;
    if (!(statbuf.st_mode & S_IWUSR)) flags = O_RDONLY;
    else if (!(statbuf.st_mode & S_IRUSR)) flags = O_WRONLY;
    tempfd = mtcp_sys_open (linkbuf, flags, 0);
    if (tempfd < 0) {
      MTCP_PRINTF("error %d re-opening %s flags %o\n",
                  mtcp_sys_errno, linkbuf, flags);
      continue;
    }

    /* Move it to the original fd if it didn't coincidentally open there */

    if (tempfd != fdnum) {
      if (mtcp_sys_dup2 (tempfd, fdnum) < 0) {
        MTCP_PRINTF("error %d duping %s from %d to %d\n",
                    mtcp_sys_errno, linkbuf, tempfd, fdnum);
        mtcp_abort ();
      }
      mtcp_sys_close (tempfd);
    }

    /* Position the file to its same spot it was at when checkpointed */

    if (S_ISREG (statbuf.st_mode) &&
        (mtcp_sys_lseek (fdnum, offset, SEEK_SET) != offset)) {
      MTCP_PRINTF("error %d positioning %s to %ld\n",
                  mtcp_sys_errno, linkbuf, (long)offset);
      mtcp_abort ();
    }
  }
}

/************************************************************************** 
 *
 *  Read memory area descriptors from checkpoint file
 *  Read memory area contents and/or mmap original file
 *  Four cases: MAP_ANONYMOUS (if file /proc/.../maps reports file,
 *		   handle it as if MAP_PRIVATE and not MAP_ANONYMOUS,
 *		   but restore from ckpt image: no copy-on-write);
 *		 private, currently assumes backing file exists
 *               shared, but need to recreate file;
 *		 shared and file currently exists
 *		   (if writeable by us and memory map has write
 *		    protection, then write to it from checkpoint file;
 *		    else skip ckpt image and map current data of file)
 *		 NOTE:  Linux option MAP_SHARED|MAP_ANONYMOUS
 *		    currently not supported; result is undefined.
 *		    If there is an important use case, we will fix this.
 *		 (NOTE:  mmap requires that if MAP_ANONYMOUS
 *		   was not set, then mmap must specify a backing store.
 *		   Further, a reference by mmap constitutes a reference
 *		   to the file, and so the file cannot truly be deleted
 *		   until the process no longer maps it.  So, if we don't
 *		   see the file on restart and there is no MAP_ANONYMOUS,
 *		   then we have a responsibility to recreate the file.
 *		   MAP_ANONYMOUS is not currently POSIX.)
 *
 **************************************************************************/

static void readmemoryareas (void)

{
  Area area;
  char cstype;
  int flags, imagefd;
  void *mmappedat;
  // make check:  stale-fd and forkexec fail (and others?) with this turned on.
#if 0
  /* If not using gzip decompression, then use mmapfile instead of readfile. */
  int do_mmap_ckpt_image = (mtcp_restore_gzip_child_pid == -1);
#else
  int do_mmap_ckpt_image = 0;
#endif

  while (1) {
#ifdef FAST_CKPT_RST_VIA_MMAP
    if (fastckpt_get_next_area_dscr(&area) == 0) break;
#else
    int try_skipping_existing_segment = 0;

    mtcp_readfile(mtcp_restore_cpfd, &cstype, sizeof cstype);
    if (cstype == CS_THEEND) break;
    if (cstype != CS_AREADESCRIP) {
      MTCP_PRINTF("expected CS_AREADESCRIP but had %d\n", cstype);
      mtcp_abort ();
    }
    mtcp_readfile(mtcp_restore_cpfd, &area, sizeof area);
#endif

    if ((area.flags & MAP_ANONYMOUS) && (area.flags & MAP_SHARED))
      MTCP_PRINTF("\n\n*** WARNING:  Next area specifies MAP_ANONYMOUS"
		  "  and MAP_SHARED.\n"
		  "*** Turning off MAP_ANONYMOUS and hoping for best.\n\n");

    /* CASE MAP_ANONYMOUS (usually implies MAP_PRIVATE):
     * For anonymous areas, the checkpoint file contains the memory contents
     * directly.  So mmap an anonymous area and read the file into it.
     * If file exists, turn off MAP_ANONYMOUS: standard private map
     */

    if (area.flags & MAP_ANONYMOUS) {

      /* If there is a filename there, though, pretend like we're mapping
       * to it so a new /proc/self/maps will show a filename there like with
       * original process.  We only need read-only access because we don't
       * want to ever write the file.
       */

      if (area.flags & MAP_ANONYMOUS) {
        DPRINTF("restoring anonymous area %p at %p\n", area.size, area.addr);
      } else {
        DPRINTF("restoring to non-anonymous area from anonymous area %p at %p "
                " from %s + 0x%X\n",
                area.size, area.addr, area.name, area.offset);
      }
#ifdef FAST_CKPT_RST_VIA_MMAP
      fastckpt_restore_mem_region(mtcp_restore_cpfd, &area);
#else
      imagefd = 0;
      if (area.name[0] == '/') { /* If not null string, not [stack] or [vdso] */
        imagefd = mtcp_sys_open (area.name, O_RDONLY, 0);
        if (imagefd < 0) imagefd = 0;
        else area.flags ^= MAP_ANONYMOUS;
      }

      /* Create the memory area */

      /* POSIX says mmap would unmap old memory.  Munmap never fails if args
       * are valid.  Can we unmap vdso and vsyscall in Linux?  Used to use
       * mtcp_safemmap here to check for address conflicts.
       */
      mmappedat = mtcp_sys_mmap (area.addr, area.size, area.prot | PROT_WRITE,
				 area.flags, imagefd, area.offset);

      /* Close image file (fd only gets in the way) */
      if (!(area.flags & MAP_ANONYMOUS)) mtcp_sys_close (imagefd);
      if (mmappedat == MAP_FAILED) {
        DPRINTF("error %d mapping %p bytes at %p\n",
                mtcp_sys_errno, area.size, area.addr);
	try_skipping_existing_segment = 1;
      }
      if (mmappedat != area.addr && !try_skipping_existing_segment) {
        MTCP_PRINTF("area at %p got mmapped to %p\n", area.addr, mmappedat);
        mtcp_abort ();
      }

      /* Read saved area contents */
      mtcp_readcs (mtcp_restore_cpfd, CS_AREACONTENTS);

      if (try_skipping_existing_segment) {
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
        mtcp_readfile(mtcp_restore_cpfd, area.addr, area.size);
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
      if (area.name[0] == '/' /* If not null string, not [stack] or [vdso] */
          && global_vdso_addr >= area.addr
          && global_vdso_addr < area.addr + area.size) {
        DPRINTF("randomized vdso conflict; retrying\n");
        mtcp_sys_close (mtcp_restore_cpfd);
        mtcp_restore_cpfd = -1;
        if (-1 == mtcp_sys_execve(mtcp_restore_cmd_file,
                                  mtcp_restore_argv, mtcp_restore_envp))
          DPRINTF("execve failed.  Restart may fail.\n");
      } else {
        skipfile (area.size);
      }
# endif
#endif
      } else {
        /* This mmapfile after prev. mmap is okay; use same args again.
         *  Posix says prev. map will be munmapped.
         */
        /* ANALYZE THE CONDITION FOR DOING mmapfile MORE CAREFULLY. */
        if (do_mmap_ckpt_image
            && mtcp_strstr(area.name, "[vdso]")
            && mtcp_strstr(area.name, "[vsyscall]"))
          mmapfile (area.addr, area.size, area.prot | PROT_WRITE, area.flags);
        else
          mtcp_readfile(mtcp_restore_cpfd, area.addr, area.size);
        if (!(area.prot & PROT_WRITE))
          if (mtcp_sys_mprotect (area.addr, area.size, area.prot) < 0) {
            MTCP_PRINTF("error %d write-protecting %p bytes at %p\n",
                        mtcp_sys_errno, area.size, area.addr);
            mtcp_abort ();
          }
      }
#endif // FAST_CKPT_RST_VIA_MMAP
    }

    /* CASE NOT MAP_ANONYMOUS:
     * Otherwise, we mmap the original file contents to the area
     */

    else {
      DPRINTF("restoring mapped area %p at %p to %s + 0x%X\n",
              area.size, area.addr, area.name, area.offset);
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
#if 1
        /* During checkpoint, MAP_ANONYMOUS flag is forced whenever MAP_PRIVATE
         * is set. There is no reason for any mapping to have MAP_PRIVATE and
         * not have MAP_ANONYMOUS at this point. (MAP_ANONYMOUS is handled
         * earlier in this function.
         */
        MTCP_PRINTF("Unreachable. MAP_PRIVATE implies MAP_ANONYMOUS\n");
        mtcp_abort();
#else
        imagefd = mtcp_sys_open (area.name, flags, 0);  // open it

        /* CASE NOT MAP_ANONYMOUS, MAP_PRIVATE, backing file doesn't exist: */
        if (imagefd < 0) {
          MTCP_PRINTF("error %d opening mmap file %s\n",
                      mtcp_sys_errno, area.name);
          mtcp_abort ();
        }

        /* CASE NOT MAP_ANONYMOUS, and MAP_PRIVATE, */
        mmappedat = mtcp_sys_mmap (area.addr, area.size, area.prot, 
            area.flags, imagefd, area.offset);
        if (mmappedat == MAP_FAILED) {
          MTCP_PRINTF("error %d mapping %s offset %d at %p\n",
                      mtcp_sys_errno, area.name, area.offset, area.addr);
          mtcp_abort ();
        }
        if (mmappedat != area.addr) {
          MTCP_PRINTF("area at %p got mmapped to %p\n", area.addr, mmappedat);
          mtcp_abort ();
        }
        mtcp_sys_close (imagefd); // don't leave dangling fd

        mtcp_readcs (mtcp_restore_cpfd, CS_AREACONTENTS);

        // If we have write permission on file and memory area (data segment),
	// then we use data in checkpoint image.
        // In the case of DMTCP, multiple processes may duplicate this work.
        if ( (imagefd = mtcp_sys_open(area.name, O_WRONLY, 0)) >= 0
              && ( (flags == O_WRONLY || flags == O_RDWR) ) ) {
          MTCP_PRINTF("mapping %s with data from ckpt image\n", area.name);
          mtcp_readfile(mtcp_restore_cpfd, area.addr, area.size);
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
            MTCP_PRINTF("mapping current version of %s into memory;\n"
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
#endif
      }
    }

    if (area.name && mtcp_strstr(area.name, "[heap]")
        && mtcp_sys_brk(NULL) != area.addr + area.size) {
      DPRINTF("WARNING: break (%p) not equal to end of heap (%p)\n",
              mtcp_sys_brk(NULL), area.addr + area.size);
    }
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
 * If the file already exists on the disk, there are two possible scenarios as
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
  char *area_name = area->name; /* Modified in fix_filename_if_new_cwd below. */

  if (!(area->prot & MAP_SHARED)) {
    MTCP_PRINTF("Illegal function call\n");
    mtcp_abort();
  }

  /* Check to see if the filename ends with " (deleted)" */
  if (mtcp_strendswith(area_name, DELETED_FILE_SUFFIX)) {
    area_name[ mtcp_strlen(area_name) - mtcp_strlen(DELETED_FILE_SUFFIX) ] =
      '\0';
  }

  imagefd = mtcp_sys_open (area_name, flags, 0);  // open it

  if (imagefd < 0 && mtcp_sys_errno != ENOENT) {
    MTCP_PRINTF("error %d opening mmap file %s with flags:%d\n",
                mtcp_sys_errno, area_name, flags);
    mtcp_abort();
  } 

  if (imagefd < 0) {
    // If the shared file doesn't exist on the disk, we try to create it
    DPRINTF("Shared file %s not found. Creating new\n", area_name);

    /* Dangerous for DMTCP:  Since file is created with O_CREAT,
     * hopefully, a second process should ignore O_CREAT and just
     *  duplicate the work of the first process, with no ill effect.
     */
    area_name = fix_filename_if_new_cwd(area_name);
    imagefd = open_shared_file(area_name);

    /* Acquire write lock on the file before writing anything to it 
     * If we don't, then there is a weird RACE going on between the
     * restarting processes which causes problems with mmap()ed area for
     * this file and hence the restart fails. We still don't know the
     * reason for it.                                       --KAPIL 
     * NOTE that we don't need to unlock the file as it will be
     * automatically done when we close it.
     */
    lock_file(imagefd, area_name, F_WRLCK);

#ifdef FAST_CKPT_RST_VIA_MMAP
    fastckpt_populate_shared_file_from_ckpt_image(mtcp_restore_cpfd, imagefd,
                                                  area);
#else
    // Create a temp area in the memory exactly of the size of the
    // shared file.  We read the contents of the shared file from
    // checkpoint file(.mtcp) into system memory. From system memory,
    // the contents are written back to newly created replica of the shared
    // file (at the same path where it used to exist before checkpoint).
    mmappedat = mtcp_sys_mmap (area->addr, area->size, PROT_READ | PROT_WRITE, 
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mmappedat == MAP_FAILED) {
      MTCP_PRINTF("error %d mapping temp memory at %p\n",
                  mtcp_sys_errno, area->addr);
      mtcp_abort ();
    }

    mtcp_readcs (mtcp_restore_cpfd, CS_AREACONTENTS);
    mtcp_readfile(mtcp_restore_cpfd, area->addr, area->size);

    areaContentsAlreadyRead = 1;

    if ( mtcp_sys_write(imagefd, area->addr,area->size) < 0 ){
      MTCP_PRINTF("error %d creating mmap file %s\n",
                  mtcp_sys_errno, area_name);
      mtcp_abort();
    }

    // unmap the temp memory allocated earlier
    rc = mtcp_sys_munmap (area->addr, area->size);
    if (rc == -1) {
      MTCP_PRINTF("error %d unmapping temp memory at %p\n",
                  mtcp_sys_errno, area->addr);
      mtcp_abort ();
    }
#endif

    // set file permissions as per memory area protection.
    int fileprot = 0;
    if (area->prot & PROT_READ)  fileprot |= S_IRUSR;
    if (area->prot & PROT_WRITE) fileprot |= S_IWUSR;
    if (area->prot & PROT_EXEC)  fileprot |= S_IXUSR;
    mtcp_sys_fchmod(imagefd, fileprot);

    //close the file
    mtcp_sys_close(imagefd);

    // now open the file again, this time with appropriate flags
    imagefd = mtcp_sys_open (area_name, flags, 0);
    if (imagefd < 0){
      MTCP_PRINTF("error %d opening mmap file %s\n", mtcp_sys_errno, area_name);
      mtcp_abort ();
    }
  } else { /* else file exists */
    /* This prevents us writing to an mmap()ed file whose length is smaller
     * than the region in memory.  This occurred when checkpointing from within
     * OpenMPI.
     */
    int file_size = mtcp_sys_lseek(imagefd, 0, SEEK_END);
    if (area->size > file_size) {
      DPRINTF("File size (%d) on disk smaller than mmap() size (%d).\n"
              "  Extending it to mmap() size.\n", file_size, area->size);
      mtcp_sys_ftruncate(imagefd, area->size);
    }

    /* Acquire read lock on the shared file before doing an mmap. See
     * detailed comments above.
     */
    DPRINTF("Acquiring lock on shared file :%s\n", area_name);
    lock_file(imagefd, area_name, F_RDLCK); 
    DPRINTF("After Acquiring lock on shared file :%s\n", area_name);
  }

  mmappedat = mtcp_sys_mmap (area->addr, area->size, area->prot,
                             area->flags, imagefd, area->offset);
  if (mmappedat == MAP_FAILED) {
    MTCP_PRINTF("error %d mapping %s offset %d at %p\n",
                 mtcp_sys_errno, area_name, area->offset, area->addr);
    mtcp_abort ();
  }
  if (mmappedat != area->addr) {
    MTCP_PRINTF("area at %p got mmapped to %p\n", area->addr, mmappedat);
    mtcp_abort ();
  }

  // TODO: Simplify this entire logic.
  if ( areaContentsAlreadyRead == 0 ){
#ifndef FAST_CKPT_RST_VIA_MMAP
    mtcp_readcs (mtcp_restore_cpfd, CS_AREACONTENTS);
#endif

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

      MTCP_PRINTF("mapping %s with data from ckpt image\n", area->name);
      mtcp_readfile(mtcp_restore_cpfd, area->addr, area->size);
      mtcp_sys_close (imagefd); // don't leave dangling fd
    }
#else
    if (area->prot & PROT_WRITE) {
      MTCP_PRINTF("mapping %s with data from ckpt image\n", area->name);
# ifdef FAST_CKPT_RST_VIA_MMAP
      fastckpt_populate_shared_file_from_ckpt_image(mtcp_restore_cpfd, imagefd,
                                                    area);
# else
      mtcp_readfile(mtcp_restore_cpfd, area->addr, area->size);
# endif
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
      if (imagefd >= 0 && -1 == mtcp_sys_access(area->name, X_OK)) {
        if (mtcp_strstartswith(area->name, "/usr/") ||
            mtcp_strstartswith(area->name, "/var/")) {
          DPRINTF("mapping current version of %s into memory;\n"
                  "  _not_ file as it existed at time of checkpoint.\n"
                  "  Change %s:%d and re-compile, if you want different "
                  "behavior.\n",
                  area->name, __FILE__, __LINE__);
        } else {
          MTCP_PRINTF("mapping current version of %s into memory;\n"
                      "  _not_ file as it existed at time of checkpoint.\n"
                      "  Change %s:%d and re-compile, if you want different "
                      "behavior. %d: %d\n",
                      area->name, __FILE__, __LINE__);
        }
      }
#ifndef FAST_CKPT_RST_VIA_MMAP
      skipfile (area->size);
#endif
    }
  }
  if (imagefd >= 0)
    mtcp_sys_close (imagefd); // don't leave dangling fd in way of other stuff
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
    if (addr == MAP_FAILED) {
      MTCP_PRINTF("error %d reading checkpoint file\n", mtcp_sys_errno);
    } else {
      MTCP_PRINTF("Requested address %p, but got address %p\n", buf, addr);
    }
    mtcp_abort();
  }
  /* Now update mtcp_restore_cpfd so as to work the same way as readfile() */
  rc = mtcp_sys_lseek(mtcp_restore_cpfd, size, SEEK_CUR);
}

static void skipfile(size_t size)
{
#if 1
  VA tmp_addr = mtcp_sys_mmap(0, size, PROT_WRITE | PROT_READ,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (tmp_addr == MAP_FAILED) {
    MTCP_PRINTF("mtcp_sys_mmap() failed with error: %s", MTCP_STR_ERRNO);
    mtcp_abort();
  }
  mtcp_readfile(mtcp_restore_cpfd, tmp_addr, size);
  if (mtcp_sys_munmap(tmp_addr, size) == -1) {
    MTCP_PRINTF("mtcp_sys_munmap() failed with error: %s", MTCP_STR_ERRNO);
    mtcp_abort();
  }
#else
  size_t ar;
  ssize_t rc;
  ar = 0;
  char array[512];

  while(ar != size) {
    rc = mtcp_sys_read(mtcp_restore_cpfd, array,
                       (size-ar < 512 ? size - ar : 512));
    if(rc < 0) {
      MTCP_PRINTF("error %d skipping checkpoint\n", mtcp_sys_errno);
      mtcp_abort();
    } else if(rc == 0) {
      MTCP_PRINTF("only skipped %zu bytes instead of %zu from ckpt file\n",
                  ar, size);
      mtcp_abort();
    }

    ar += rc;
  }
#endif
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
  const int bufsize = 1 + sizeof "[vsyscall]"; /* largest of last 3 strings */
  char buf[bufsize];

  buf[0] = '\0';
  buf[bufsize - 1] = '\0';

  /* Scan through the mappings of this process */

  mapsfd = mtcp_sys_open ("/proc/self/maps", O_RDONLY, 0);
  if (mapsfd < 0) {
    MTCP_PRINTF("couldn't open /proc/self/maps\n");
    mtcp_abort();
  }

  *vdso_addr = NULL;
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

    if (0 == mtcp_strncmp(buf, stackstring, mtcp_strlen(stackstring))) {
      *stack_end_addr = endaddr;
      highaddr = endaddr;  /* We found "[stack]" in /proc/self/maps */
    }

    if (0 == mtcp_strncmp(buf, vdsostring, mtcp_strlen(vdsostring))) {
      *vdso_addr = startaddr;
      highaddr = endaddr;  /* We found "[vdso]" in /proc/self/maps */
    }

    if (0 == mtcp_strncmp(buf, vsyscallstring, mtcp_strlen(vsyscallstring))) {
      *vsyscall_addr = startaddr;
      highaddr = endaddr;  /* We found "[vsyscall]" in /proc/self/maps */
    }
  }

  mtcp_sys_close (mapsfd);

  return highaddr;
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
    MTCP_PRINTF("error opening /proc/self/maps: errno: %d\n", errno);
    mtcp_abort ();
  }

  *vdso_addr = NULL; /* Default to NULL if not found. */
  while (readmapsline (mapsfd, &area)) {
    /* Gcc expands strstr() inline, but it's safer to use our own function. */
    p = mtcp_strstr (area.name, "[stack]");
    if (p != NULL)
      area_end = area.addr + area.size;
    p = mtcp_strstr (area.name, "[vdso]");
    if (p != NULL)
      *vdso_addr = area.addr;
    p = mtcp_strstr (area.name, "[vsyscall]");
    if (p != NULL)
      *vsyscall_addr = area.addr;
    p = mtcp_strstr (area.name, "[stack]");
    if (p != NULL)
      *stack_end_addr = area.addr + addr.size;
    p = mtcp_strstr (area.name, "[vsyscall]");
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
  while (result == -1 || mtcp_sys_errno == EINTR ) {
    /* F_GETLK, F_SETLK, F_SETLKW */
    result = mtcp_sys_fcntl3(fd, F_SETLKW, &fl);
  }

  /* Coverity static analyser stated the following code as DEAD. It is not
   * DEADCODE because it is possible that mtcp_sys_fcntl3() fails with some
   * error other than EINTR
   */
  if ( result == -1 ) {
    MTCP_PRINTF("error %d locking shared file: %s\n", mtcp_sys_errno, name);
    mtcp_abort();
  }
}

/* Adjust for new pwd, and recreate any missing subdirectories. */
static char* fix_filename_if_new_cwd(char* filename)
{
  int i;
  int fIndex;
  int errorFilenameFromPreviousCwd = 0;
  char currentFolder[FILENAMESIZE];
  
  /* Find the starting index where the actual filename begins */
  for ( i=0; filename[i] != '\0'; i++ ){
    if ( filename[i] == '/' )
      fIndex = i+1;
  }
  
  /* We now try to create the directories structure from the given path */
  for ( i=0 ; i<fIndex ; i++ ){
    if (filename[i] == '/' && i > 0){
      int res;
      currentFolder[i] = '\0';
      res = mtcp_sys_mkdir(currentFolder, S_IRWXU);
      if (res<0 && mtcp_sys_errno != EEXIST ){
        if (mtcp_strstartswith(filename, mtcp_saved_working_directory)) {
          errorFilenameFromPreviousCwd = 1;
          break;
        }
        MTCP_PRINTF("error %d creating directory %s in path of %s\n",
		    mtcp_sys_errno, currentFolder, filename);
	mtcp_abort();
      }
    }
    currentFolder[i] = filename[i];
  }

  /* If filename began with previous cwd and wasn't found there,
   * then let's try creating in current cwd (current working directory).
   */
  if (errorFilenameFromPreviousCwd) {
    int prevCwdLen;
    i=mtcp_strlen(mtcp_saved_working_directory);
    while (filename[i] == '/')
      i++;
    prevCwdLen = i;
    for ( i=prevCwdLen ; i<fIndex ; i++ ){
      if (filename[i] == '/'){
        int res;
        currentFolder[i-prevCwdLen] = '\0';
        res = mtcp_sys_mkdir(currentFolder, S_IRWXU);
        if (res<0 && mtcp_sys_errno != EEXIST ){
          MTCP_PRINTF("error %d creating directory %s in path of %s in cwd\n",
		      mtcp_sys_errno, currentFolder, filename);
	  mtcp_abort();
        }
      }
      currentFolder[i-prevCwdLen] = filename[i];
    }
    filename = filename + prevCwdLen;  /* Now filename is relative filename. */
  }
  return filename;
}

static int open_shared_file(char* filename)
{
  int fd;
  /* Create the file */
  fd = mtcp_sys_open(filename, O_CREAT|O_RDWR, S_IRUSR|S_IWUSR);
  if (fd<0){
    MTCP_PRINTF("unable to create file %s\n", filename);
    mtcp_abort();
  }
  return fd;
}
#endif
