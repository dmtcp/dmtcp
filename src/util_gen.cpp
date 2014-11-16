/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
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
#include <fcntl.h>
#include  "util.h"
#include  "membarrier.h"
#include  "syscallwrappers.h"
#include  "dmtcp.h"
#include  "../jalib/jassert.h"
#include  "../jalib/jfilesystem.h"

using namespace dmtcp;

void Util::lockFile(int fd)
{
  struct flock fl;

  fl.l_type   = F_WRLCK;  // F_RDLCK, F_WRLCK, F_UNLCK
  fl.l_whence = SEEK_SET; // SEEK_SET, SEEK_CUR, SEEK_END
  fl.l_start  = 0;        // Offset from l_whence
  fl.l_len    = 0;        // length, 0 = to EOF
  //fl.l_pid    = _real_getpid(); // our PID

  int result = -1;
  errno = 0;
  do {
    result = _real_fcntl(fd, F_SETLKW, &fl);  /* F_GETLK, F_SETLK, F_SETLKW */
  } while (result == -1 && errno == EINTR);

  JASSERT (result != -1) (JASSERT_ERRNO)
    .Text("Unable to lock the PID MAP file");
#if (__arm__ || __aarch64__)
  WMB;  // DMB, ensure writes by others to memory have completed before we
        //      we enter protected region.
#endif
}

void Util::unlockFile(int fd)
{
  struct flock fl;
  int result;

#if (__arm__ || __aarch64__)
  RMB; WMB; // DMB, ensure accesses to protected memory have completed
            //      before releasing lock
#endif
  fl.l_type   = F_UNLCK;  // tell it to unlock the region
  fl.l_whence = SEEK_SET; // SEEK_SET, SEEK_CUR, SEEK_END
  fl.l_start  = 0;        // Offset from l_whence
  fl.l_len    = 0;        // length, 0 = to EOF

#if (__arm__ || __aarch64__)
  WMB;  // DSB, ensure update of fl before seen by other CPUs
#endif

  result = _real_fcntl(fd, F_SETLK, &fl); /* set the region to unlocked */

  JASSERT (result != -1 || errno == ENOLCK) (JASSERT_ERRNO)
    .Text("Unlock Failed");
}

bool Util::strStartsWith(const char *str, const char *pattern)
{
  if (str == NULL || pattern == NULL) {
    return false;
  }
  int len1 = strlen(str);
  int len2 = strlen(pattern);
  if (len1 >= len2) {
    return strncmp(str, pattern, len2) == 0;
  }
  return false;
}

bool Util::strEndsWith(const char *str, const char *pattern)
{
  if (str == NULL || pattern == NULL) {
    return false;
  }
  int len1 = strlen(str);
  int len2 = strlen(pattern);
  if (len1 >= len2) {
    size_t idx = len1 - len2;
    return strncmp(str+idx, pattern, len2) == 0;
  }
  return false;
}

bool Util::strStartsWith(const string& str, const char *pattern)
{
  return strStartsWith(str.c_str(), pattern);
}

bool Util::strEndsWith(const string& str, const char *pattern)
{
  return strEndsWith(str.c_str(), pattern);
}

string Util::joinStrings(vector<string> v, const string& delim)
{
  string result;
  if (v.size() > 0) {
    result = v[0];
    for (size_t i = 1; i < v.size(); i++) {
      result += delim + v[i];
    }
  }
  return result;
}

// Tokenizes the string using the delimiters.
// Empty tokens will not be included in the result.
vector<string> Util::tokenizeString(const string& s, const string& delims)
{
  size_t offset = 0;
  vector<string> tokens;

  while (true) {
    size_t i = s.find_first_not_of(delims, offset);
    if (i == string::npos) {
      break;
    }

    size_t j = s.find_first_of(delims, i);
    if (j == string::npos) {
      tokens.push_back(s.substr(i));
      offset = s.length();
      continue;
    }

    tokens.push_back(s.substr(i, j - i));
    offset = j;
  }
  return tokens;
}

// Add it back if needed.
#if 0
// Splits the string using the provided delimiters.
// The string is split each time at the first character
// that matches any of the characters specified in delims.
// Empty tokens are allowed in the result.
// Optionally, maximum number of tokens to be returned
// can be specified.
inline vector<string> split(
    const string& s,
    const string& delims,
    const Option<unsigned int>& n = None())
{
  vector<string> tokens;
  size_t offset = 0;
  size_t next = 0;

  while (n.isNone() || n.get() > 0) {
    next = s.find_first_of(delims, offset);
    if (next == string::npos) {
      tokens.push_back(s.substr(offset));
      break;
    }

    tokens.push_back(s.substr(offset, next - offset));
    offset = next + 1;

    // Finish splitting if we've found enough tokens.
    if (n.isSome() && tokens.size() == n.get() - 1) {
      tokens.push_back(s.substr(offset));
      break;
    }
  }
  return tokens;
}
#endif

// Fails or does entire write (returns count)
ssize_t Util::writeAll(int fd, const void *buf, size_t count)
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
ssize_t Util::readAll(int fd, void *buf, size_t count)
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

ssize_t Util::skipBytes(int fd, size_t count)
{
  char buf[1024];
  ssize_t rc;
  ssize_t totalSkipped = 0;
  while (count > 0) {
    rc = Util::readAll(fd, buf, MIN(count, sizeof(buf)));

    if (rc == -1) {
      break;
    }
    count -= rc;
    totalSkipped += rc;
  }
  return totalSkipped;
}

void Util::changeFd(int oldfd, int newfd)
{
  if (oldfd != newfd) {
    JASSERT(_real_dup2(oldfd, newfd) == newfd);
    _real_close(oldfd);
  }
}

void Util::dupFds(int oldfd, const vector<int>& newfds)
{
  changeFd(oldfd, newfds[0]);
  for (size_t i = 1; i < newfds.size(); i++) {
    JASSERT(_real_dup2(newfds[0], newfds[i]) == newfds[i]);
  }
}


/* Begin miscellaneous/helper functions. */
// Reads from fd until count bytes are read, or newline encountered.
// Returns NULL at EOF.
// FIXME: count is unused. Buffer-overrun possible
int Util::readLine(int fd, char *buf, int count)
{
  int i = 0;
  char c;
  while (i < count) {
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

/* Read decimal number, return value and terminating character */

char Util::readDec (int fd, VA *value)
{
  char c;
  unsigned long int v;

  v = 0;
  while (1) {
    c = readChar (fd);
    if ((c >= '0') && (c <= '9')) c -= '0';
    else break;
    v = v * 10 + c;
  }
  *value = (VA)v;
  return (c);
}

/* Read decimal number, return value and terminating character */

char Util::readHex (int fd, VA *value)
{
  char c;
  unsigned long int v;

  v = 0;
  while (1) {
    c = readChar (fd);
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

char Util::readChar (int fd)
{
  char c;
  int rc;

  do {
    rc = _real_read (fd, &c, 1);
  } while ( rc == -1 && errno == EINTR );
  if (rc <= 0) return (0);
  return (c);
}


int Util::readProcMapsLine(int mapsfd, ProcMapsArea *area)
{
  char c, rflag, sflag, wflag, xflag;
  int i;
  off_t offset;
  unsigned int long devmajor, devminor, inodenum;
  VA startaddr, endaddr;

  c = readHex (mapsfd, &startaddr);
  if ((c == 0) && (startaddr == 0)) return (0);
  if (c != '-') {
    goto skipeol;
  }
  c = readHex (mapsfd, &endaddr);
  if (c != ' ') goto skipeol;
  if (endaddr < startaddr) goto skipeol;

  rflag = c = readChar (mapsfd);
  if ((c != 'r') && (c != '-')) goto skipeol;
  wflag = c = readChar (mapsfd);
  if ((c != 'w') && (c != '-')) goto skipeol;
  xflag = c = readChar (mapsfd);
  if ((c != 'x') && (c != '-')) goto skipeol;
  sflag = c = readChar (mapsfd);
  if ((c != 's') && (c != 'p')) goto skipeol;

  c = readChar (mapsfd);
  if (c != ' ') goto skipeol;

  c = readHex (mapsfd, (VA *)&offset);
  if (c != ' ') goto skipeol;
  area -> offset = offset;

  c = readHex (mapsfd, (VA *)&devmajor);
  if (c != ':') goto skipeol;
  c = readHex (mapsfd, (VA *)&devminor);
  if (c != ' ') goto skipeol;
  c = readDec (mapsfd, (VA *)&inodenum);
  area -> name[0] = '\0';
  while (c == ' ') c = readChar (mapsfd);
  if (c == '/' || c == '[') { /* absolute pathname, or [stack], [vdso], etc. */
    i = 0;
    do {
      area -> name[i++] = c;
      if (i == sizeof area -> name) goto skipeol;
      c = readChar (mapsfd);
    } while (c != '\n');
    area -> name[i] = '\0';
  }

  if (c != '\n') goto skipeol;

  area -> addr = startaddr;
  area -> size = endaddr - startaddr;
  area -> endAddr = endaddr;
  area -> prot = 0;
  if (rflag == 'r') area -> prot |= PROT_READ;
  if (wflag == 'w') area -> prot |= PROT_WRITE;
  if (xflag == 'x') area -> prot |= PROT_EXEC;
  area -> flags = MAP_FIXED;
  if (sflag == 's') area -> flags |= MAP_SHARED;
  if (sflag == 'p') area -> flags |= MAP_PRIVATE;
  if (area -> name[0] == '\0') area -> flags |= MAP_ANONYMOUS;

  area->devmajor = devmajor;
  area->devminor = devminor;
  area->inodenum = inodenum;
  return (1);

skipeol:
  JASSERT(false) .Text("Not Reached");
  return (0);  /* NOTREACHED : stop compiler warning */
}

int Util::memProtToOpenFlags(int prot)
{
  if (prot & (PROT_READ | PROT_WRITE)) return O_RDWR;
  if (prot & PROT_READ) return O_RDONLY;
  if (prot & PROT_WRITE) return O_WRONLY;
  return 0;
}

#define TRACER_PID_STR "TracerPid:"
pid_t Util::getTracerPid(pid_t tid)
{
  if (!dmtcp_real_to_virtual_pid) {
    return 0;
  }

  char buf[512];
  char *str;
  static int tracerStrLen = strlen(TRACER_PID_STR);
  int fd;

  if (tid == -1) {
    tid = gettid();
  }
  sprintf(buf, "/proc/%d/status", tid);
  fd = _real_open(buf, O_RDONLY, 0);
  JASSERT(fd != -1) (buf) (JASSERT_ERRNO);
  readAll(fd, buf, sizeof buf);
  _real_close(fd);
  str = strstr(buf, TRACER_PID_STR);
  JASSERT(str != NULL);
  str += tracerStrLen;

  while (*str == ' ' || *str == '\t') {
    str++;
  }

  pid_t tracerPid = (pid_t) strtol(str, NULL, 10);
  return tracerPid == 0 ? tracerPid : dmtcp_real_to_virtual_pid(tracerPid);
}

bool Util::isPtraced()
{
  return getTracerPid() != 0;
}

bool Util::isValidFd(int fd)
{
  return _real_fcntl(fd, F_GETFL, 0) != -1;
}

size_t Util::pageSize()
{
  static size_t page_size = sysconf(_SC_PAGESIZE);
  return page_size;
}

size_t Util::pageMask()
{
  static size_t page_mask = ~(pageSize() - 1);
  return page_mask;
}

/* This function detects if the given pages are zero pages or not. There is
 * scope of improving this function using some optimizations.
 *
 * TODO: One can use /proc/self/pagemap to detect if the page is backed by a
 * shared zero page.
 */
bool Util::areZeroPages(void *addr, size_t numPages)
{
  static size_t page_size = pageSize();
  long long *buf = (long long*) addr;
  size_t i;
  size_t end = numPages * page_size / sizeof (*buf);
  long long res = 0;
  for (i = 0; i + 7 < end; i += 8) {
    res = buf[i+0] | buf[i+1] | buf[i+2] | buf[i+3] |
          buf[i+4] | buf[i+5] | buf[i+6] | buf[i+7];
    if (res != 0) {
      break;
    }
  }
  return res == 0;
}

/* Caller must allocate exec_path of size at least MTCP_MAX_PATH */
char *Util::findExecutable(char *executable, const char* path_env,
                                  char *exec_path)
{
  char *path;
  const char *tmp_env;
  int len;

  JASSERT(exec_path != NULL);
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
    strncat(exec_path, executable, PATH_MAX - len - 1);
    if (access(exec_path, X_OK) == 0){
      // Artem: Additionally check that this is regular file.
      // From access point of view directories are executables too :)
      // I ran into problem on the system where user home dir was in the PATH
      // and I create a directory named "hbict" in it.
      // Eventually home path was before my sandbox path and DMTCP was
      // trying to call a directory :)
      struct stat buf;
      if( stat(exec_path, &buf) ){
        continue;
      }
      if( S_ISREG(buf.st_mode) )
        return exec_path;
    }
  }

  // In case we're running with PATH environment variable unset:
  const char * stdpath = "/usr/local/bin:/usr/bin:/bin";
  if (strcmp(path_env, stdpath) == 0) {
    return NULL;  // Already tried stdpath
  } else {
    return findExecutable(executable, stdpath, exec_path);
  }
}

