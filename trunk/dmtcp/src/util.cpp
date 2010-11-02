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
#include "constants.h"
#include  "util.h"
#include  "../jalib/jassert.h"

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
  if (str.length() >= strlen(pattern)) {
    return str.compare(0, strlen(pattern), pattern) == 0;
  }
  return false;
}

bool dmtcp::Util::strEndsWith(const dmtcp::string& str, const char *pattern)
{
  if (str.length() >= strlen(pattern)) {
    size_t idx = str.length() - strlen(pattern);
    return str.compare(idx, strlen(pattern), pattern) == 0;
  }
  return false;
}

ssize_t dmtcp::Util::writeAll(int fd, const void *buf, size_t count)
{
  const char *ptr = (const char *) buf;
  ssize_t offs = 0;

  do {
    ssize_t rc = write (fd, ptr + offs, count - offs);
    if (rc == -1 && errno != EINTR && errno != EAGAIN) 
      return rc;
    else if (rc == 0)
      break;
    else if (rc > 0)
      offs += rc;
  } while (offs < count);
  return count;
}

ssize_t dmtcp::Util::readAll(int fd, void *buf, size_t count)
{
  size_t rc;
  char *ptr = (char *) buf;
  int num_read = 0;
  for (num_read = 0; num_read < count;) {
    rc = read (fd, ptr + num_read, count - num_read);
    if (rc == -1 && errno != EINTR && errno != EAGAIN) 
      return rc;
    else if (rc == 0)
      break;
    else
      num_read += rc;
  }
  return num_read;
}
