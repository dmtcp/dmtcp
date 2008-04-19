//+++2006-01-17
//    Copyright (C) 2006  Mike Rieker, Beverly, MA USA
//    Modifications to make it 64-bit clean by Gene Cooperman
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
//---2006-01-17

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
static void skipfile (int size);
static VA highest_userspace_address (void);
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
  VA holebase, highest_va; /* VA = virtual address */
  void *current_brk;
  void *new_brk;
  void (*finishrestore) (void);

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
      mtcp_printf ("mtcp_restoreverything: new_brk == current_brk == %p\n"
        "  saved_break, %p, is strictly smaller; data segment not extended.\n",
        new_brk, mtcp_saved_break);
    else {
      mtcp_printf ("mtcp_restoreverything: error: new break (%p) != saved break"
                   "  (%p)\n", (VA)current_brk, mtcp_saved_break);
      mtcp_abort ();
    }
  }

  /* Unmap everything except for this image as everything we need is contained in the mtcp.so image */

  holebase  = (VA)mtcp_shareable_begin;
  holebase &= -PAGE_SIZE;
  asm volatile (CLEAN_FOR_64_BIT(xor %%eax,%%eax ; movw %%ax,%%fs)
				: : : CLEAN_FOR_64_BIT(eax)); // the unmaps will wipe what it points to anyway
  // asm volatile (CLEAN_FOR_64_BIT(xor %%eax,%%eax ; movw %%ax,%%gs) : : : CLEAN_FOR_64_BIT(eax)); // so make sure we get a hard failure just in case
                                                                  // ... it's left dangling on something I want
  DPRINTF (("mtcp restoreverything*: unmapping 0..%p\n", holebase - 1));
  rc = mtcp_sys_munmap (NULL, holebase);
  if (rc == -1) {
      mtcp_printf ("mtcp_sys_munmap: error %d unmapping from 0 to %p\n", mtcp_sys_errno, holebase);
      mtcp_abort ();
  }

  holebase  = (VA)mtcp_shareable_end;
  holebase  = (holebase + PAGE_SIZE - 1) & -PAGE_SIZE;
  highest_va = highest_userspace_address();
  if (highest_va == 0) { /* 0 means /proc/self/maps doesn't mark "[stack]" */
    highest_va = HIGHEST_VA;
  }
  DPRINTF (("mtcp restoreverything*: unmapping %p..%p\n", holebase, highest_va - 1));
  rc = mtcp_sys_munmap ((void *)holebase, highest_va - holebase);
  if (rc == -1) {
      mtcp_printf ("mtcp_sys_munmap: error %d unmapping from %p by %p bytes\n", mtcp_sys_errno, holebase, highest_va - holebase);
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

    /* Move it to the original fd if it didn't coincientally open there */

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

/********************************************************************************************************************************/
/*																*/
/*  Read memory area descriptors from checkpoint file										*/
/*  Read memory area contents and/or mmap original file										*/
/*																*/
/********************************************************************************************************************************/

static void readmemoryareas (void)

{
  Area area;
  char cstype;
  int flags, imagefd;
  void *mmappedat;
  int areaContentsAlreadyRead = 0;

  while (1) {
    int try_overwriting_existing_segment = 0;

    readfile (&cstype, sizeof cstype);
    if (cstype == CS_THEEND) break;
    if (cstype != CS_AREADESCRIP) {
      mtcp_printf ("mtcp_restart: expected CS_AREADESCRIP but had %d\n", cstype);
      mtcp_abort ();
    }
    readfile (&area, sizeof area);

    /* For anonymous areas, the checkpoint file contains the memory contents directly */
    /* So mmap an anonymous area and read the file into it                            */

    if (area.flags & MAP_ANONYMOUS) {

      /* If there is a filename there, though, pretend like we're mapping to it so a */
      /* new /proc/self/maps will show a filename there like with original process.  */
      /* We only need read-only access because we don't want to ever write the file. */

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
        DPRINTF (("mtcp restoreverything*: restoring anonymous area 0x%X at %p from %s + %X\n", area.size, area.addr, area.name, area.offset));
      }
      mmappedat = mtcp_safemmap (area.addr, area.size, area.prot | PROT_WRITE, area.flags, imagefd, area.offset);
      if (mmappedat == MAP_FAILED) {
        DPRINTF(("mtcp_restart: error %d mapping %X bytes at %p\n", mtcp_sys_errno, area.size, area.addr));
	try_overwriting_existing_segment = 1;
      }
      if (mmappedat != area.addr && !try_overwriting_existing_segment) {
        mtcp_printf ("mtcp_restart: area at %p got mmapped to %p\n", area.addr, mmappedat);
        mtcp_abort ();
      }

      /* Read saved area contents */
      readcs (CS_AREACONTENTS);
      if (!try_overwriting_existing_segment) {
        readfile (area.addr, area.size);
        if (!(area.prot & PROT_WRITE) && (mtcp_sys_mprotect (area.addr, area.size, area.prot) < 0)) {
          mtcp_printf ("mtcp_restart: error %d write-protecting %X bytes at %p\n", mtcp_sys_errno, area.size, area.addr);
          mtcp_abort ();
        }
      }
      else
        skipfile (area.size);

      /* Close image file (fd only gets in the way) */

      if (!(area.flags & MAP_ANONYMOUS)) mtcp_sys_close (imagefd);
    }

    /* Otherwise, we mmap the original file contents to the area */

    else {
      DPRINTF (("mtcp restoreverything*: restoring mapped area 0x%X at %p to %s + %X\n", area.size, area.addr, area.name, area.offset));
      flags = 0;                                      // see how to open it based on the access required
      // O_RDONLY = 00
      // O_WRONLY = 01
      // O_RDWR   = 02
      if (area.prot & PROT_WRITE) flags = O_WRONLY;
      if (area.prot & (PROT_EXEC | PROT_READ)){
        flags = O_RDONLY;
        if (area.prot & PROT_WRITE) flags = O_RDWR;
      }
 
      imagefd = mtcp_sys_open (area.name, flags, 0);  // open it

      // If the shared file doesn't exist on the disk, we try to create it
      if (imagefd < 0 && (area.prot & MAP_SHARED) && mtcp_sys_errno == ENOENT ){

        DPRINTF(("mtcp restoreverything*: Shared file %s not found, Creating new\n",area.name));

        imagefd = open_shared_file(area.name);

        // create a temp area in the memory exactly of the size of the shared file.
        // we read the contents of the shared file from checkpoint file(.mtcp) into
        // system memory. From system memory, the contents are written back to newly
        // created replica of the shared file (at the same path where it used to exist
        // before checkpoint.
        mmappedat = mtcp_safemmap (area.addr, area.size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, imagefd, area.offset);
        if (mmappedat == MAP_FAILED) {
          mtcp_printf ("mtcp_restart: error %d mapping temp memory at %p\n", mtcp_sys_errno, area.addr);
          mtcp_abort ();
        }

        readcs (CS_AREACONTENTS);
        readfile (area.addr, area.size);
        areaContentsAlreadyRead = 1;

        if ( mtcp_sys_write(imagefd, area.addr,area.size) < 0 ){
          mtcp_printf ("mtcp_restart: error %d creating mmap file %s\n", mtcp_sys_errno, area.name);
          mtcp_abort();
        }

        // unmap the temp memory allocated earlier
        mmappedat = mtcp_sys_munmap (area.addr, area.size);
        if (mmappedat == -1) {
          mtcp_printf ("mtcp_restart: error %d unmapping temp memory at %p\n", mtcp_sys_errno, area.addr);
          mtcp_abort ();
        }
          
        //close the file
        mtcp_sys_close(imagefd);

        // now open the file again, this time with appropriate flags
        imagefd = mtcp_sys_open (area.name, flags, 0);
        if (imagefd < 0){
          mtcp_printf ("mtcp_restart: error %d opening mmap file %s\n", mtcp_sys_errno, area.name);
          mtcp_abort ();
        }
      }

      if (imagefd < 0) {
        mtcp_printf ("mtcp_restart: error %d opening mmap file %s\n", mtcp_sys_errno, area.name);
        mtcp_abort ();
      }
      
      // Map the shared file into memory
      mmappedat = mtcp_safemmap (area.addr, area.size, area.prot, area.flags, imagefd, area.offset);
      if (mmappedat == MAP_FAILED) {
        mtcp_printf ("mtcp_restart: error %d mapping %s offset %d at %p\n", mtcp_sys_errno, area.name, area.offset, area.addr);
        mtcp_abort ();
      }
      if (mmappedat != area.addr) {
        mtcp_printf ("mtcp_restart: area at %p got mmapped to %p\n", area.addr, mmappedat);
        mtcp_abort ();
      }
      mtcp_sys_close (imagefd);                       // don't leave a dangling fd in the way of other stuff

      if ( areaContentsAlreadyRead == 0 ){
        // If we haven't created the file (i.e. the shared file does exists when this process wants
        // map it) we want to skip the the checkpoint file pointer to move to the end of the shared
        // file data. We can not use lseek() function as it can fail if we are using a pipe to read
        // the contents of checkpoint file (we might be using gzip to uncompress checkpoint file on
        // the fly). Thus we have to read the contents using the following code
        readcs (CS_AREACONTENTS);
        readfile (area.addr, area.size);
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
            mtcp_printf("mtcp_restart readfile: error %d reading checkpoint\n", mtcp_sys_errno);
            mtcp_abort();
        }
        else if(rc == 0)
        {
            mtcp_printf("mtcp_restart readfile: only read %d bytes instead of %d from checkpoint file\n", ar, size);
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
            mtcp_printf("mtcp_restart skipfile: error %d skipping checkpoint\n", mtcp_sys_errno);
            mtcp_abort();
        }
        else if(rc == 0)
        {
            mtcp_printf("mtcp_restart skipfile: only skipped %d bytes instead of %d from checkpoint file\n", ar, size);
            mtcp_abort();
        }

        ar += rc;
    }
}

#if 1
/* Modelled after mtcp_safemmap.  - Gene */
static VA highest_userspace_address (void)
{
  char c;
  int mapsfd, i;
  VA endaddr, startaddr;
  VA histackaddr = 0; /* high stack address should be highest userspace addr */
  const char *stackstring = "[stack]";
  const int bufsize = sizeof "[stack]";
  char buf[bufsize];

  buf[0] = '\0';

  /* Scan through the mappings of this process */

  mapsfd = mtcp_sys_open ("/proc/self/maps", O_RDONLY, 0);
  if (mapsfd < 0) {
    mtcp_printf("couldn't open /proc/self/maps\n");
    mtcp_abort();
  }

  while (1) {

    /* Read a line from /proc/self/maps */

    c = mtcp_readhex (mapsfd, &startaddr);
    if (c != '-') goto skipeol;
    c = mtcp_readhex (mapsfd, &endaddr);
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
    for (i = 0; i < bufsize; i++)
      if (buf[i] != stackstring[i])
        break;
    if (i == bufsize)
      histackaddr = endaddr;  /* We found "[stack]" in /proc/self/maps */
  }

  mtcp_sys_close (mapsfd);

  return (VA)histackaddr;

}

#else
/* Added by Gene Cooperman */
static VA highest_userspace_address (void)
{
  Area area;
  VA area_end = 0;
  int mapsfd;
  char *p;

  mapsfd = open ("/proc/self/maps", O_RDONLY);
  if (mapsfd < 0) {
    printf ("mtcp highest_userspace_address:"
            " error opening /proc/self/maps: %s\n", strerror (errno));
    mtcp_abort ();
  }

  while (readmapsline (mapsfd, &area)) {
    p = strstr (area.name, "[stack]");
    if (p != NULL)
      area_end = (VA)area.addr + area.size;
  }

  close (mapsfd);
  return (VA)area_end;
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
        mtcp_printf("mtcp_restart open_shared_file: error %d creating directory %s in path of %s\n", mtcp_sys_errno, currentFolder, fileName);
	mtcp_abort();
      }
    }
    currentFolder[i] = fileName[i];
  }

  /* Create the file*/
  fd = mtcp_sys_open(fileName,O_CREAT|O_RDWR,S_IRWXU);
  if (fd<0){
    mtcp_printf("mtcp_restart open_shared_file: unable to create file %s\n", fileName);
    mtcp_abort();
  }
  return fd;
}

#endif
