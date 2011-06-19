/****************************************************************************
 *   Copyright (C) 2006-2010 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *   This file is part of the dmtcp/src module of DMTCP (DMTCP:dmtcp/src).  *
 *                                                                          *
 *  DMTCP:dmtcp/src is free software: you can redistribute it and/or        *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP:dmtcp/src is distributed in the hope that it will be useful,      *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <string>
#include <sstream>
#include <fcntl.h>
#include <errno.h>
#include <sys/syscall.h>
#include <linux/limits.h>
#include "constants.h"
#include  "util.h"
#include  "syscallwrappers.h"
#include  "../jalib/jassert.h"
#include  "../jalib/jfilesystem.h"

void dmtcp::Util::lockFile(int fd)
{
  struct flock fl;

  fl.l_type   = F_WRLCK;  // F_RDLCK, F_WRLCK, F_UNLCK
  fl.l_whence = SEEK_SET; // SEEK_SET, SEEK_CUR, SEEK_END
  fl.l_start  = 0;        // Offset from l_whence
  fl.l_len    = 0;        // length, 0 = to EOF
  //fl.l_pid    = _real_getpid(); // our PID

  int result = -1;
  errno = 0;
  while (result == -1 || errno == EINTR)
    result = fcntl(fd, F_SETLKW, &fl);  /* F_GETLK, F_SETLK, F_SETLKW */

  JASSERT (result != -1) (JASSERT_ERRNO)
    .Text("Unable to lock the PID MAP file");
}

void dmtcp::Util::unlockFile(int fd)
{
  struct flock fl;
  int result;
  fl.l_type   = F_UNLCK;  // tell it to unlock the region
  fl.l_whence = SEEK_SET; // SEEK_SET, SEEK_CUR, SEEK_END
  fl.l_start  = 0;        // Offset from l_whence
  fl.l_len    = 0;        // length, 0 = to EOF

  result = fcntl(fd, F_SETLK, &fl); /* set the region to unlocked */

  JASSERT (result != -1 || errno == ENOLCK) (JASSERT_ERRNO)
    .Text("Unlock Failed");
}

bool dmtcp::Util::strStartsWith(const char *str, const char *pattern)
{
  int len1 = strlen(str);
  int len2 = strlen(pattern);
  if (len1 >= len2) {
    return strncmp(str, pattern, len2) == 0;
  }
  return false;
}

bool dmtcp::Util::strEndsWith(const char *str, const char *pattern)
{
  int len1 = strlen(str);
  int len2 = strlen(pattern);
  if (len1 >= len2) {
    size_t idx = len1 - len2;
    return strncmp(str+idx, pattern, len2) == 0;
  }
  return false;
}

bool dmtcp::Util::strStartsWith(const dmtcp::string& str, const char *pattern)
{
  return strStartsWith(str.c_str(), pattern);
}

bool dmtcp::Util::strEndsWith(const dmtcp::string& str, const char *pattern)
{
  return strEndsWith(str.c_str(), pattern);
}

// Fails or does entire write (returns count)
ssize_t dmtcp::Util::writeAll(int fd, const void *buf, size_t count)
{
  const char *ptr = (const char *) buf;
  size_t num_written = 0;

  do {
    ssize_t rc = _real_write (fd, ptr + num_written, count - num_written);
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
  JASSERT (num_written == count) (num_written) (count);
  return num_written;
}

// Fails, succeeds, or partial read due to EOF (returns num read)
// return value:
//    -1: unrecoverable error
//   <n>: number of bytes read
ssize_t dmtcp::Util::readAll(int fd, void *buf, size_t count)
{
  ssize_t rc;
  char *ptr = (char *) buf;
  size_t num_read = 0;
  for (num_read = 0; num_read < count;) {
    rc = _real_read (fd, ptr + num_read, count - num_read);
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

/* Begin miscellaneous/helper functions. */
// Reads from fd until count bytes are read, or newline encountered.
// Returns NULL at EOF.
int dmtcp::Util::readLine(int fd, char *buf, int count)
{
  int i = 0;
  char c;
  while (1) {
    if (_real_read(fd, &c, 1) == 0) {
      buf[i] = '\0';
      return '\0';
    }
    buf[i++] = c;
    if (c == '\n') break;
  }
  buf[i++] = '\0';
  return i;
}
