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

#include "mtcp_internal.h"

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

/* Write something to checkpoint file */
__attribute__ ((visibility ("hidden")))
void mtcp_writefile (int fd, void const *buff, size_t size)
{
  char const *bf;
  ssize_t rc;
  size_t sz, wt;
  static char zeroes[MTCP_PAGE_SIZE] = { 0 };

  //checkpointsize += size;

  bf = buff;
  sz = size;
  while (sz > 0) {
    for (wt = sz; wt > 0; wt /= 2) {
      rc = mtcp_sys_write (fd, bf, wt);
      if ((rc >= 0) || (mtcp_sys_errno != EFAULT)) break;
    }

    /* Sometimes image page alignment will leave a hole in the middle of an
     * image ... but the idiot proc/self/maps will include it anyway
     */

    if (wt == 0) {
      rc = (sz > sizeof zeroes ? sizeof zeroes : sz);
      //checkpointsize -= rc; /* Correct now, since mtcp_writefile will add rc
      //                           back */
      mtcp_writefile (fd, zeroes, rc);
    }

    /* Otherwise, check for real error */

    else {
      if (rc == 0) mtcp_sys_errno = EPIPE;
      if (rc <= 0) {
        MTCP_PRINTF("Error %d writing from %p to checkpoint file.\n",
	             mtcp_sys_errno, bf);
        mtcp_abort ();
      }
    }

    /* It's ok, we're on to next part */

    sz -= rc;
    bf += rc;
  }
}

/* Write checkpoint section number to checkpoint file */
__attribute__ ((visibility ("hidden")))
void mtcp_writecs (int fd, char cs)
{
  mtcp_writefile (fd, &cs, sizeof cs);
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
void mtcp_readcs(int fd, char cs)
{
  char xcs;

  mtcp_readfile (fd, &xcs, sizeof xcs);
  if (xcs != cs) {
    MTCP_PRINTF("checkpoint section %d next, expected %d\n", xcs, cs);
    mtcp_abort ();
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

/*****************************************************************************
 *
 *  Discover the memory occupied by this library (libmtcp.so)
 *  PROVIDES:  mtcp_selfmap_open / mtcp_selfmap_readline / mtcp_selfmap_close
 *
 *****************************************************************************/

/*
 * USAGE:
 * VA startaddr, endaddr;
 * int selfmapfd = mtcp_selfmap_open();
 * while (mtcp_selfmap_readline(selfmapfd, &startaddr, &endaddr, NULL)) {
 *  ... startaddr ... endaddr
 * }
 * mtcp_selfmap_close(selfmapfd);
 *
 * SPECIAL CASE:
 * off_t offset;
 * while (mtcp_selfmap_readline(selfmapfd, &startaddr, &endaddr, &offset)) {
 *  if (startaddr <= interestingAddr && interestingAddr <= endaddr)
 *    interestingFileOffset = offset;
 * }
 * mtcp_sys_lseek(selfmapfd, interestingFileOffset, SEEK_SET);
 */

/* Return 1 if both offsets point to the same
 *   filename (delimited by whitespace)
 * Else return 0
 */
static int compareoffset(int selfmapfd1, off_t offset1,
                         int selfmapfd2, off_t offset2) {
  off_t old_offset1 = mtcp_sys_lseek(selfmapfd1, 0,  SEEK_CUR);
  off_t old_offset2 = mtcp_sys_lseek(selfmapfd2, 0,  SEEK_CUR);
  char c1, c2;
  mtcp_sys_lseek(selfmapfd1, offset1, SEEK_SET);
  mtcp_sys_lseek(selfmapfd2, offset2, SEEK_SET);
  do {
    c1 = mtcp_readchar(selfmapfd1);
    c2 = mtcp_readchar(selfmapfd2);
  } while ((c1 == c2) && (c1 != '\n') && (c1 != '\t') && (c1 != '\0'));
  mtcp_sys_lseek(selfmapfd1, old_offset1, SEEK_SET);
  mtcp_sys_lseek(selfmapfd2, old_offset2, SEEK_SET);
  return (c1 == c2) && ((c1 == '\n') || (c1 == '\t') || (c1 == '\0'));
}

/*
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
 */
void mtcp_get_memory_region_of_this_library(VA *startaddr, VA *endaddr);
static int dummy;
static void * dummy_fnc = &mtcp_get_memory_region_of_this_library;
__attribute__ ((visibility ("hidden")))
void mtcp_get_memory_region_of_this_library(VA *startaddr, VA *endaddr) {
  *startaddr = NULL;
  *endaddr = NULL;
  VA thislib_fnc = dummy_fnc;
  VA tmpstartaddr, tmpendaddr;
  off_t offset1, offset2;
  int selfmapfd, selfmapfd2;
  selfmapfd = mtcp_selfmap_open();
  while (mtcp_selfmap_readline(selfmapfd, &tmpstartaddr, &tmpendaddr,
                               &offset1)) {
    if (tmpstartaddr <= thislib_fnc && thislib_fnc <= tmpendaddr) {
      break;
    }
  }
  /* Compute entire memory region corresponding to this file. */
  selfmapfd2 = mtcp_selfmap_open();
  while (mtcp_selfmap_readline(selfmapfd2, &tmpstartaddr, &tmpendaddr,
                               &offset2)) {
    if (offset2 != -1 &&
        compareoffset(selfmapfd, offset1, selfmapfd2, offset2)) {
      if (*startaddr == NULL)
        *startaddr = tmpstartaddr;
      *endaddr = tmpendaddr;
    }
  }
  mtcp_selfmap_close(selfmapfd2);
  mtcp_selfmap_close(selfmapfd);

  /* /proc/PID/maps does not label the filename for memory region holding
   * static variables in a library.  But that is also part of this
   * library (libmtcp.so).
   * So, find the meory region for static memory variables and add it.
   */
  VA thislib_staticvar = &dummy;
  selfmapfd = mtcp_selfmap_open();
  while (mtcp_selfmap_readline(selfmapfd, &tmpstartaddr, &tmpendaddr,
                               &offset1)) {
    if (tmpstartaddr <= thislib_staticvar && thislib_staticvar <= tmpendaddr) {
      break;
    }
  }
  mtcp_selfmap_close(selfmapfd);
  if (*endaddr == tmpstartaddr)
    *endaddr = tmpendaddr;
  DPRINTF("libmtcp.so: startaddr: %x; endaddr: %x\n", *startaddr, *endaddr);
}

__attribute__ ((visibility ("hidden")))
int mtcp_selfmap_open() {
  return mtcp_sys_open ("/proc/self/maps", O_RDONLY, 0);
}
/* Return 1 if a line was successfully read (was not at EOF) */
__attribute__ ((visibility ("hidden")))
int mtcp_selfmap_readline(int selfmapfd, VA *startaddr, VA *endaddr,
	                  off_t *file_offset) {
  int rc = 0;
  char c;
  if (file_offset != NULL)
    *file_offset = (off_t)-1;
  c = mtcp_readhex (selfmapfd, startaddr);
  if (c == '\0') return 0; /* c == '\0' implies that reached end-of-file */
  if (c != '-') goto skipeol;
  c = mtcp_readhex (selfmapfd, endaddr);
  if (c != ' ') goto skipeol;
skipeol:
  while ((c != '\0') && (c != '\n')) {
    if ( (file_offset != NULL) && (*file_offset == (off_t)-1) &&
         ((c == '/') || (c == '[')) )
      *file_offset = mtcp_sys_lseek(selfmapfd, 0,  SEEK_CUR) - 1;
    c = mtcp_readchar (selfmapfd);
  }
  return 1; /* 1 means successfully read a line */
}
__attribute__ ((visibility ("hidden")))
int mtcp_selfmap_close(int selfmapfd) {
  return mtcp_sys_close (selfmapfd);
}
