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
 *  Read from file without using any external memory routines (like malloc,
 *  fget, etc)
 *
 *
 *****************************************************************************/

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/sysmacros.h>

#include "mtcp_internal.h"
#include "mtcp_util.h"

/* Read decimal number, return value and terminating character */

char mtcp_readdec (int fd, VA *value)
{
  char c;
  unsigned long int v;

  v = 0;
  while (1) {
    c = mtcp_readchar (fd);
    if ((c >= '0') && (c <= '9')) c -= '0';
    else break;
    v = v * 10 + c;
  }
  *value = (VA)v;
  return (c);
}

/* Read decimal number, return value and terminating character */

char mtcp_readhex (int fd, VA *value)
{
  char c;
  unsigned long int v;

  v = 0;
  while (1) {
    c = mtcp_readchar (fd);
      if ((c >= '0') && (c <= '9')) c -= '0';
    else if ((c >= 'a') && (c <= 'f')) c -= 'a' - 10;
    else if ((c >= 'A') && (c <= 'F')) c -= 'A' - 10;
    else break;
    v = v * 16 + c;
  }
  *value = (VA)v;
  return (c);
}

/* Read non-null character, return null if EOF */

char mtcp_readchar (int fd)
{
  char c;
  int rc;

  do {
    rc = mtcp_sys_read (fd, &c, 1);
  } while ( rc == -1 && mtcp_sys_errno == EINTR );
  if (rc <= 0) return (0);
  return (c);
}

__attribute__ ((visibility ("hidden")))
size_t mtcp_strlen(const char *s)
{
  size_t len = 0;
  while (*s++ != '\0') {
    len++;
  }
  return len;
}

__attribute__ ((visibility ("hidden")))
void mtcp_strncpy(char *dest, const char *src, size_t n)
{
  size_t i;

  for (i = 0; i < n && src[i] != '\0'; i++)
    dest[i] = src[i];
  if (i < n) {
    dest[i] = '\0';
  }

  //return dest;
}

__attribute__ ((visibility ("hidden")))
void mtcp_strcpy(char *dest, const char *src)
{
  while (*src != '\0') {
    *dest++ = *src++;
  }
}

__attribute__ ((visibility ("hidden")))
void mtcp_strncat(char *dest, const char *src, size_t n)
{
  mtcp_strncpy(dest + mtcp_strlen(dest), src, n);
  //return dest;
}

__attribute__ ((visibility ("hidden")))
int mtcp_strncmp (const char *s1, const char *s2, size_t n)
{
  unsigned char c1 = '\0';
  unsigned char c2 = '\0';

  while (n > 0) {
    c1 = (unsigned char) *s1++;
    c2 = (unsigned char) *s2++;
    if (c1 == '\0' || c1 != c2)
      return c1 - c2;
    n--;
  }
  return c1 - c2;
}

__attribute__ ((visibility ("hidden")))
int mtcp_strcmp (const char *s1, const char *s2)
{
  size_t n = mtcp_strlen(s2);
  unsigned char c1 = '\0';
  unsigned char c2 = '\0';

  while (n > 0) {
    c1 = (unsigned char) *s1++;
    c2 = (unsigned char) *s2++;
    if (c1 == '\0' || c1 != c2)
      return c1 - c2;
    n--;
  }
  return c1 - c2;
}

__attribute__ ((visibility ("hidden")))
const void *mtcp_strstr(const char *string, const char *substring)
{
  for ( ; *string != '\0' ; string++) {
    const char *ptr1, *ptr2;
    for (ptr1 = string, ptr2 = substring;
         *ptr1 == *ptr2 && *ptr2 != '\0';
         ptr1++, ptr2++) ;
    if (*ptr2 == '\0')
      return string;
  }
  return NULL;
}

__attribute__ ((visibility ("hidden")))
int mtcp_strstartswith (const char *s1, const char *s2)
{
  if (mtcp_strlen(s1) >= mtcp_strlen(s2)) {
    return mtcp_strncmp(s1, s2, mtcp_strlen(s2)) == 0;
  }
  return 0;
}

__attribute__ ((visibility ("hidden")))
int mtcp_strendswith (const char *s1, const char *s2)
{
  size_t len1 = mtcp_strlen(s1);
  size_t len2 = mtcp_strlen(s2);

  if (len1 < len2)
    return 0;

  s1 += (len1 - len2);

  return mtcp_strncmp(s1, s2, len2) == 0;
}

__attribute__ ((visibility ("hidden")))
int mtcp_memcmp(char *dest, const char *src, size_t n)
{
  return mtcp_strncmp(dest, src, n);
}

__attribute__ ((visibility ("hidden")))
void mtcp_memset(char *dest, int c, size_t n)
{
  size_t i;
  for (i = 0; i < n; i++)
    dest[i] = (char) c;
}

//void mtcp_check_vdso_enabled() {
//}

__attribute__ ((visibility ("hidden")))
int mtcp_atoi(const char *nptr)
{
  int v = 0;

  while (*nptr >= '0' && *nptr <= '9') {
    v = v * 10 + (*nptr - '0');
    nptr++;
  }
  return v;
}

void mtcpHookWriteCkptData(const void *buf, size_t size) __attribute__ ((weak));

/* Write something to checkpoint file */
__attribute__ ((visibility ("hidden")))
size_t mtcp_writefile (int fd, void const *buff, size_t size)
{
  if (mtcpHookWriteCkptData == NULL) {
    MTCP_ASSERT(mtcp_write_all(fd, buff, size) == size);
  } else {
    mtcpHookWriteCkptData(buff, size);
  }
  return size;
}

__attribute__ ((visibility ("hidden")))
void mtcp_readfile(int fd, void *buf, size_t size)
{
  ssize_t rc;
  size_t ar = 0;
  int tries = 0;

  while(ar != size) {
    rc = mtcp_sys_read(fd, buf + ar, size - ar);
    if (rc < 0 && rc > -4096) { /* kernel could return large unsigned int */
      MTCP_PRINTF("error %d reading checkpoint\n", mtcp_sys_errno);
      mtcp_abort();
    }
    else if (rc == 0) {
      MTCP_PRINTF("only read %u bytes instead of %u from checkpoint file\n",
                  (unsigned)ar, (unsigned)size);
      if (tries++ >= 10) {
        MTCP_PRINTF(" failed to read after 10 tries in a row.\n");
        mtcp_abort();
      }
    }
    ar += rc;
  }
}

__attribute__ ((visibility ("hidden")))
void mtcp_skipfile(int fd, size_t size)
{
  VA tmp_addr = mtcp_sys_mmap(0, size, PROT_WRITE | PROT_READ,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (tmp_addr == MAP_FAILED) {
    MTCP_PRINTF("mtcp_sys_mmap() failed with error: %d", mtcp_sys_errno);
    mtcp_abort();
  }
  mtcp_readfile(fd, tmp_addr, size);
  if (mtcp_sys_munmap(tmp_addr, size) == -1) {
    MTCP_PRINTF("mtcp_sys_munmap() failed with error: %d", mtcp_sys_errno);
    mtcp_abort();
  }
}


// NOTE: This functions is called by mtcp_printf() so do not invoke
// mtcp_printf() from within this function.
__attribute__ ((visibility ("hidden")))
ssize_t mtcp_write_all(int fd, const void *buf, size_t count)
{
  const char *ptr = (const char *) buf;
  size_t num_written = 0;

  do {
    ssize_t rc = mtcp_sys_write (fd, ptr + num_written, count - num_written);
    if (rc == -1) {
      if (mtcp_sys_errno == EINTR || mtcp_sys_errno == EAGAIN)
	continue;
      else
        return rc;
    }
    else if (rc == 0)
      break;
    else // else rc > 0
      num_written += rc;
  } while (num_written < count);
  return num_written;
}

// Fails, succeeds, or partial read due to EOF (returns num read)
__attribute__ ((visibility ("hidden")))
ssize_t mtcp_read_all(int fd, void *buf, size_t count)
{
  int rc;
  char *ptr = (char *) buf;
  size_t num_read = 0;
  for (num_read = 0; num_read < count;) {
    rc = mtcp_sys_read (fd, ptr + num_read, count - num_read);
    if (rc == -1) {
      if (mtcp_sys_errno == EINTR || mtcp_sys_errno == EAGAIN)
        continue;
      else
        return -1;
    }
    else if (rc == 0)
      break;
    else // else rc > 0
      num_read += rc;
  }
  return num_read;
}

int mtcp_is_executable(const char *exec_path)
{
#if 1
  return 0 == mtcp_sys_access(exec_path, X_OK);
#else
  struct stat stat_buf;
  /* Bash says "have to use access(2) to determine access because AFS
    does not [find] answers for non-AFS files when ruid != euid." ??  */
  return 0 == mtcp_sys_stat(exec_path, &stat_buf)
    && S_ISREG(stat_buf.st_mode) && stat_buf.st_mode & S_IXOTH;
#endif
}

/* Caller must allocate exec_path of size at least MTCP_MAX_PATH */
char *mtcp_find_executable(char *executable, const char* path_env,
                           char exec_path[PATH_MAX])
{
  char *path;
  const char *tmp_env;
  int len;

  if (path_env == NULL) {
    path_env = ""; // Will try stdpath later in this function
  }
  tmp_env = path_env;

  while (*tmp_env != '\0') {
    path = exec_path;
    len = 0;
    while (*tmp_env != ':' && *tmp_env != '\0' && ++len < PATH_MAX - 1)
      *path++ = *tmp_env++;
    if (*tmp_env == ':') /* but if *tmp_env == '\0', will exit while loop */
      tmp_env++;
    *path++ = '/'; /* '...//... is same as .../... in POSIX */
    len++;
    *path++ = '\0';
    mtcp_strncat(exec_path, executable, PATH_MAX - len - 1);
    if (mtcp_is_executable(exec_path))
      return exec_path;
  }

  // In case we're running with PATH environment variable unset:
  const char * stdpath = "/usr/local/bin:/usr/bin:/bin";
  if (mtcp_strcmp(path_env, stdpath) == 0) {
    return NULL;  // Already tried stdpath
  } else {
    return mtcp_find_executable(executable, stdpath, exec_path);
  }
}

void mtcp_rename_ckptfile(const char *tempckpt, const char *permckpt)
{
  if (mtcp_sys_rename(tempckpt, permckpt) < 0) {
    MTCP_PRINTF("error %d renaming %s to %s\n",
                mtcp_sys_errno, tempckpt, permckpt);
    mtcp_abort ();
  }
}

/*****************************************************************************
 *
 *  Read /proc/self/maps line, converting it to an Area descriptor struct
 *    Input:
 *	mapsfd = /proc/self/maps file, positioned to beginning of a line
 *    Output:
 *	mtcp_readmapsline = 0 : was at end-of-file, nothing read
 *	*area = filled in
 *    Note:
 *	Line from /procs/self/maps is in form:
 *	<startaddr>-<endaddrexclusive> rwxs <fileoffset> <devmaj>:<devmin>
 *	    <inode>    <filename>\n
 *	all numbers in hexadecimal except inode is in decimal
 *	anonymous will be shown with offset=devmaj=devmin=inode=0 and
 *	    no '     filename'
 *
 *****************************************************************************/

int mtcp_readmapsline (int mapsfd, Area *area, DeviceInfo *dev_info)
{
  char c, rflag, sflag, wflag, xflag;
  int i;
  unsigned int long devmajor, devminor, inodenum;
  VA startaddr, endaddr;

  c = mtcp_readhex (mapsfd, &startaddr);
  if (c != '-') {
    if ((c == 0) && (startaddr == 0)) return (0);
    goto skipeol;
  }
  c = mtcp_readhex (mapsfd, &endaddr);
  if (c != ' ') goto skipeol;
  if (endaddr < startaddr) goto skipeol;

  rflag = c = mtcp_readchar (mapsfd);
  if ((c != 'r') && (c != '-')) goto skipeol;
  wflag = c = mtcp_readchar (mapsfd);
  if ((c != 'w') && (c != '-')) goto skipeol;
  xflag = c = mtcp_readchar (mapsfd);
  if ((c != 'x') && (c != '-')) goto skipeol;
  sflag = c = mtcp_readchar (mapsfd);
  if ((c != 's') && (c != 'p')) goto skipeol;

  c = mtcp_readchar (mapsfd);
  if (c != ' ') goto skipeol;

  c = mtcp_readhex (mapsfd, (VA *)&devmajor);
  if (c != ' ') goto skipeol;
  area -> offset = (off_t)devmajor;

  c = mtcp_readhex (mapsfd, (VA *)&devmajor);
  if (c != ':') goto skipeol;
  c = mtcp_readhex (mapsfd, (VA *)&devminor);
  if (c != ' ') goto skipeol;
  c = mtcp_readdec (mapsfd, (VA *)&inodenum);
  area -> name[0] = '\0';
  while (c == ' ') c = mtcp_readchar (mapsfd);
  if (c == '/' || c == '[') { /* absolute pathname, or [stack], [vdso], etc. */
    i = 0;
    do {
      area -> name[i++] = c;
      if (i == sizeof area -> name) goto skipeol;
      c = mtcp_readchar (mapsfd);
    } while (c != '\n');
    area -> name[i] = '\0';
  }

  if (c != '\n') goto skipeol;

  area -> addr = startaddr;
  area -> size = endaddr - startaddr;
  area -> prot = 0;
  if (rflag == 'r') area -> prot |= PROT_READ;
  if (wflag == 'w') area -> prot |= PROT_WRITE;
  if (xflag == 'x') area -> prot |= PROT_EXEC;
  area -> flags = MAP_FIXED;
  if (sflag == 's') area -> flags |= MAP_SHARED;
  if (sflag == 'p') area -> flags |= MAP_PRIVATE;
  if (area -> name[0] == '\0') area -> flags |= MAP_ANONYMOUS;

  if (dev_info != NULL) {
    dev_info->devmajor = devmajor;
    dev_info->devminor = devminor;
    dev_info->inodenum = inodenum;
  }
  return (1);

skipeol:
  DPRINTF("ERROR:  mtcp readmapsline*: bad maps line <%c", c);
  while ((c != '\n') && (c != '\0')) {
    c = mtcp_readchar (mapsfd);
    mtcp_printf ("%c", c);
  }
  mtcp_printf (">\n");
  mtcp_abort ();
  return (0);  /* NOTREACHED : stop compiler warning */
}

/*****************************************************************************
 *  Discover the memory occupied by this library (libmtcp.so)
 *
 * This is used to find:  mtcp_shareable_begin mtcp_shareable_end
 * The standard way is to modifiy the linker script (mtcp.t in Makefile).
 * The method here works by looking at /proc/PID/maps
 * However, this is error-prone.  It assumes that the kernel labels
 *   all memory regions of this library with the library filename,
 *   except for a single memory region for static vars in lib.  The
 *   latter case is handled by assuming a single region adjacent to
 *   to the labelled regions, and occuring after the labelled regions.
 *   This assumes that all of these memory regions form a contiguous region.
 * We optionally call this only because Fedora uses eu-strip in rpmlint,
 *   and eu-strip modifies libmtcp.so in a way that libmtcp.so no longer works.
 * This is arguably a bug in eu-strip.
 *****************************************************************************/
static int dummy_uninitialized_static_var;
void mtcp_get_memory_region_of_this_library(VA *startaddr, VA *endaddr)
{
  struct {
    VA start_addr;
    VA end_addr;
  } text, guard, rodata, rwdata, bssdata;

  Area area;
  VA thislib_fnc = (void*) &mtcp_get_memory_region_of_this_library;
  VA thislib_static_var = (VA) &dummy_uninitialized_static_var;
  char filename[PATH_MAX] = {0};
  text.start_addr = guard.start_addr = rodata.start_addr = NULL;
  rwdata.start_addr = bssdata.start_addr = bssdata.end_addr = NULL;
  int mapsfd = mtcp_sys_open("/proc/self/maps", O_RDONLY, 0);
  MTCP_ASSERT(mapsfd != -1);

  while (mtcp_readmapsline (mapsfd, &area, NULL)) {
    VA start_addr = area.addr;
    VA end_addr = area.addr + area.size;

    if (thislib_fnc >= start_addr && thislib_fnc < end_addr) {
      MTCP_ASSERT(text.start_addr == NULL);
      text.start_addr = start_addr; text.end_addr = end_addr;
      mtcp_strcpy(filename, area.name);
      continue;
    }

    if (text.start_addr != NULL && guard.start_addr == NULL &&
        mtcp_strcmp(filename, area.name) == 0) {
      MTCP_ASSERT(area.addr == text.end_addr);
      if (area.prot == 0) {
        /* The guard pages are unreadable due to the "---p" protection. Even if
         * the protection is changed to "r--p", a read will result in a SIGSEGV
         * as the pages are not backed by the kernel. A better way to handle this
         * is to remap these pages with anonymous memory.
         */
        MTCP_ASSERT(mtcp_sys_mmap(start_addr, area.size, PROT_READ,
                                  MAP_ANONYMOUS|MAP_PRIVATE|MAP_FIXED,
                                  -1, 0) == start_addr);
        guard.start_addr = start_addr; guard.end_addr = end_addr;
        continue;
      } else {
        // No guard pages found. This is probably the ROData section.
        guard.start_addr = start_addr; guard.end_addr = start_addr;
      }
    }

    if (guard.start_addr != NULL && rodata.start_addr == NULL &&
        mtcp_strcmp(filename, area.name) == 0) {
      MTCP_ASSERT(area.addr == guard.end_addr);
      if (area.prot == PROT_READ) {
        rodata.start_addr = start_addr; rodata.end_addr = end_addr;
        continue;
      } else {
        // No ROData section. This is probably the RWData section.
        rodata.start_addr = start_addr; rodata.end_addr = start_addr;
      }
    }

    if (rodata.start_addr != NULL && rwdata.start_addr == NULL &&
        mtcp_strcmp(filename, area.name) == 0) {
      MTCP_ASSERT(area.addr == rodata.end_addr);
      MTCP_ASSERT(area.prot == (PROT_READ|PROT_WRITE));
      rwdata.start_addr = start_addr; rwdata.end_addr = end_addr;
      continue;
    }

    if (rwdata.start_addr != NULL && bssdata.start_addr == NULL &&
        area.name[0] == '\0') {
      /* /proc/PID/maps does not label the filename for memory region holding
       * static variables in a library.  But that is also part of this
       * library (libmtcp.so).
       * So, find the meory region for static memory variables and add it.
       */
      MTCP_ASSERT(area.addr == rwdata.end_addr);
      MTCP_ASSERT(area.prot == (PROT_READ|PROT_WRITE));
      MTCP_ASSERT(thislib_static_var >= start_addr && thislib_static_var < end_addr);
      bssdata.start_addr = start_addr; bssdata.end_addr = end_addr;
      break;
    }
  }
  mtcp_sys_close(mapsfd);
  MTCP_ASSERT(text.start_addr != NULL);
  MTCP_ASSERT(bssdata.end_addr != NULL);
  *startaddr = text.start_addr;
  *endaddr   = bssdata.end_addr;
}
