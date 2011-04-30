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

size_t mtcp_strlen(const char *s)
{
  size_t len = 0;
  while (*s++ != '\0') {
    len++;
  }
  return len;
}

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

void *mtcp_strstr(char *string, char *substring)
{
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

int mtcp_strstartswith (const char *s1, const char *s2)
{
  if (mtcp_strlen(s1) >= mtcp_strlen(s2)) {
    return mtcp_strncmp(s1, s2, mtcp_strlen(s2)) == 0;
  }
  return 0;
}

int mtcp_strendswith (const char *s1, const char *s2)
{
  size_t len1 = mtcp_strlen(s1);
  size_t len2 = mtcp_strlen(s2);

  if (len1 < len2)
    return 0;

  s1 += (len1 - len2);

  return mtcp_strncmp(s1, s2, len2) == 0;
}

ssize_t mtcp_write_all(int fd, const void *buf, size_t count)
{
  const char *ptr = (const char *) buf;
  size_t num_written = 0;

  do {
    ssize_t rc = mtcp_sys_write (fd, ptr + num_written, count - num_written);
    if (rc == -1) {
      if (errno == EINTR || errno == EAGAIN) 
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
ssize_t mtcp_read_all(int fd, void *buf, size_t count)
{
  int rc;
  char *ptr = (char *) buf;
  size_t num_read = 0;
  for (num_read = 0; num_read < count;) {
    rc = mtcp_sys_read (fd, ptr + num_read, count - num_read);
    if (rc == -1) {
      if (errno == EINTR || errno == EAGAIN)
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

// Return Value
// 0 : Succeeded
// -1: Failed
int mtcp_get_controlling_term(char* ttyName, size_t len)
{
  char sbuf[1024];
  char *tmp;
  char *S;
  char state;
  int ppid, pgrp, session, tty, tpgid;

  int fd, num_read;

  if (len < strlen("/dev/pts/123456780"))
    return -1;
  ttyName[0] = '\0';

  fd = open("/proc/self/stat", O_RDONLY, 0);
  if (fd == -1) return -1;

  num_read = read(fd, sbuf, sizeof sbuf - 1);
  close(fd);
  if(num_read<=0) return -1;
  sbuf[num_read] = '\0';

  S = strchr(sbuf, '(') + 1;
  tmp = strrchr(S, ')');
  S = tmp + 2;                 // skip ") "

  sscanf(S,
      "%c "
      "%d %d %d %d %d ",
      &state,
      &ppid, &pgrp, &session, &tty, &tpgid
      );

  int maj =  ((unsigned)(tty)>>8u) & 0xfffu;
  int min =  ((unsigned)(tty)&0xffu) | (((unsigned)(tty)&0xfff00000u)>>12u);

  /* /dev/pts/ * has major numbers in the range 136 - 143 */
  if ( maj >= 136 && maj <= 143) 
    sprintf(ttyName, "/dev/pts/%d", min+(maj-136)*256);

  return 0;
}
