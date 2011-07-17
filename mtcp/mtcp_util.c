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
    if (rc < 0) {
      MTCP_PRINTF("error %d reading checkpoint\n", mtcp_sys_errno);
      mtcp_abort();
    }
    else if (rc == 0) {
      MTCP_PRINTF("only read %zu bytes instead of %zu from checkpoint file\n",
                  ar, size);
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
char *mtcp_find_executable(char *executable, const char* path_env, char exec_path[PATH_MAX])
{
  char *path;
  int len;

  if (path_env == NULL) {
    path_env = ""; // Will try stdpath later in this function
  }

  while (*path_env != '\0') {
    path = exec_path;
    len = 0;
    while (*path_env != ':' && *path_env != '\0' && ++len < PATH_MAX - 1)
      *path++ = *path_env++;
    if (*path_env == ':') /* but if *path_env == '\0', will exit while loop */
      path_env++;
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

