/****************************************************************************
 *   Copyright (C) 2006-2012 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include "constants.h"
#include "util.h"
#include "syscallwrappers.h"
#include "ckptserializer.h"

using namespace dmtcp;

// Copied from mtcp/mtcp_restart.c.
#define DMTCP_MAGIC_FIRST 'D'
#define GZIP_FIRST 037
#ifdef HBICT_DELTACOMP
#define HBICT_FIRST 'H'
#endif

char *mtcp_executable_path(char *filename);
static char first_char(const char *filename)
{
  int fd, rc;
  char c;

  fd = open(filename, O_RDONLY);
  JASSERT(fd >= 0) (filename) .Text("ERROR: Cannot open filename");

  rc = _real_read(fd, &c, 1);
  JASSERT(rc == 1) (filename) .Text("ERROR: Error reading from filename");

  close(fd);
  return c;
}

// Copied from mtcp/mtcp_restart.c.
// Let's keep this code close to MTCP code to avoid maintenance problems.
// MTCP code in:  mtcp/mtcp_restart.c:open_ckpt_to_read()
// A previous version tried to replace this with popen, causing a regression:
//   (no call to pclose, and possibility of using a wrong fd).
// Returns fd;
static int open_ckpt_to_read(const char *filename)
{
  int fd;
  int fds[2];
  char fc;
  const char *decomp_path;
  const char **decomp_args;
  const char *gzip_path = "gzip";
  static const char * gzip_args[] = { "gzip", "-d", "-", NULL };
#ifdef HBICT_DELTACOMP
  const char *hbict_path = "hbict";
  static const char *hbict_args[] = { "hbict", "-r", NULL };
#endif
  pid_t cpid;

  fc = first_char(filename);
  fd = open(filename, O_RDONLY);
  JASSERT(fd>=0)(filename).Text("Failed to open file.");

  if (fc == DMTCP_MAGIC_FIRST) { /* no compression */
    return fd;
  }
  else if (fc == GZIP_FIRST
#ifdef HBICT_DELTACOMP
           || fc == HBICT_FIRST
#endif
          ) {
    if (fc == GZIP_FIRST) {
      decomp_path = gzip_path;
      decomp_args = gzip_args;
    }
#ifdef HBICT_DELTACOMP
    else {
      decomp_path = hbict_path;
      decomp_args = hbict_args;
    }
#endif

    JASSERT(pipe(fds) != -1) (filename)
      .Text("Cannot create pipe to execute gunzip to decompress ckpt file!");

    cpid = _real_fork();

    JASSERT(cpid != -1)
      .Text("ERROR: Cannot fork to execute gunzip to decompress ckpt file!");
    if (cpid > 0) { /* parent process */
      JTRACE("created child process to uncompress checkpoint file") (cpid);
      close(fd);
      close(fds[1]);
      // Wait for child process
      JASSERT(waitpid(cpid, NULL, 0) == cpid);
      return fds[0];
    } else { /* child process */
      /* Fork a grandchild process and kill the parent. This way the grandchild
       * process never becomes a zombie.
       *
       * Sometimes dmtcp_restart is called with multiple ckpt images. In that
       * situation, the dmtcp_restart process creates gzip processes and only
       * later forks mtcp_restart processes. The gzip processes can not be
       * wait()'d upon by the corresponding mtcp_restart processes because
       * their parent is the original dmtcp_restart process and thus they
       * become zombie.
       */
      cpid = _real_fork();
      JASSERT(cpid != -1);
      if (cpid > 0) {
        _real_exit(0);
      }

      // Grandchild process
      JTRACE ( "child process, will exec into external de-compressor");
      fd = _real_dup(_real_dup(_real_dup(fd)));
      fds[1] = _real_dup(fds[1]);
      close(fds[0]);
      JASSERT(fd != -1);
      JASSERT(_real_dup2(fd, STDIN_FILENO) == STDIN_FILENO);
      close(fd);
      JASSERT(_real_dup2(fds[1], STDOUT_FILENO) == STDOUT_FILENO);
      close(fds[1]);
      _real_execvp(decomp_path, (char **)decomp_args);
      JASSERT(decomp_path!=NULL) (decomp_path)
        .Text("Failed to launch gzip.");
      /* should not get here */
      JASSERT(false)
        .Text("Decompression failed!  No restoration will be performed!");
    }
  } else { /* invalid magic number */
    JASSERT(false)
      .Text("ERROR: Invalid magic number in this checkpoint file!");
  }
  return -1;
}

// See comments above for open_ckpt_to_read()
int dmtcp::CkptSerializer::openDmtcpCheckpointFile(const dmtcp::string& path)
{
  char buf[1024];
  int fd = open_ckpt_to_read(path.c_str());
  // The rest of this function is for compatibility with original definition.
  JASSERT(fd >= 0) (path) .Text("Failed to open file.");
  const int len = strlen(DMTCP_FILE_HEADER);
  JASSERT(_real_read(fd, buf, len) == len)(path) .Text("_real_read() failed");
  if (strncmp(buf, DMTCP_FILE_HEADER, len) == 0) {
    JTRACE("opened checkpoint file [uncompressed]")(path);
  } else {
    _real_close(fd);
    fd = open_ckpt_to_read(path.c_str()); /* Re-open from beginning */
    JASSERT(fd >= 0) (path) .Text("Failed to open file.");
  }
  return fd;
}

void dmtcp::CkptSerializer::writeCkptHeader(int fd)
{
  const ssize_t len = strlen(DMTCP_FILE_HEADER);
  JASSERT(write(fd, DMTCP_FILE_HEADER, len) == len);

  jalib::JBinarySerializeWriterRaw wr("", fd);
  dmtcp::ProcessInfo::instance().serialize(wr);
  ssize_t written = len + wr.bytes();

  // We must write in multiple of PAGE_SIZE
  const ssize_t pagesize = Util::pageSize();
  ssize_t remaining = pagesize - (written % pagesize);
  char buf[remaining];
  JASSERT(Util::writeAll(fd, buf, remaining) == remaining);
}

int dmtcp::CkptSerializer::readCkptHeader(const dmtcp::string& path,
                                          dmtcp::ProcessInfo *pInfo)
{
  int fd = openDmtcpCheckpointFile(path);
  const size_t len = strlen(DMTCP_FILE_HEADER);

  jalib::JBinarySerializeReaderRaw rdr("", fd);
  pInfo->serialize(rdr);
  size_t numRead = len + rdr.bytes();

  // We must read in multiple of PAGE_SIZE
  const ssize_t pagesize = Util::pageSize();
  ssize_t remaining = pagesize - (numRead % pagesize);
  char buf[remaining];
  JASSERT(Util::readAll(fd, buf, remaining) == remaining);
  return fd;
}
