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

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/version.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <list>
#include <string>
#include <vector>

#include "dmtcp.h"

#include "fileconnection.h"
#include "fileconnlist.h"
#include "filewrappers.h"

using namespace dmtcp;

extern "C" FILE * tmpfile()
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  FILE *fp = _real_tmpfile();
  if (fp != NULL && dmtcp_is_running_state()) {
    FileConnList::instance().processFileConnection(fileno(
                                                     fp), NULL, O_RDWR, 0600);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return fp;
}

extern "C" int
mkstemp(char *ttemplate)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int fd = _real_mkstemp(ttemplate);
  if (fd >= 0 && dmtcp_is_running_state()) {
    FileConnList::instance().processFileConnection(fd, NULL, O_RDWR, 0600);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return fd;
}

extern "C" int
mkostemp(char *ttemplate, int flags)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int fd = _real_mkostemp(ttemplate, flags);
  if (fd >= 0 && dmtcp_is_running_state()) {
    FileConnList::instance().processFileConnection(fd, NULL, flags, 0600);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return fd;
}

extern "C" int
mkstemps(char *ttemplate, int suffixlen)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int fd = _real_mkstemps(ttemplate, suffixlen);
  if (fd >= 0 && dmtcp_is_running_state()) {
    FileConnList::instance().processFileConnection(fd, NULL, O_RDWR, 0600);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return fd;
}

extern "C" int
mkostemps(char *ttemplate, int suffixlen, int flags)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int fd = _real_mkostemps(ttemplate, suffixlen, flags);
  if (fd >= 0 && dmtcp_is_running_state()) {
    FileConnList::instance().processFileConnection(fd, NULL, flags, 0600);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return fd;
}

extern "C" DIR * opendir(const char *name)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  DIR *dir = _real_opendir(name);
  if (dir != NULL && dmtcp_is_running_state()) {
    FileConnList::instance().processFileConnection(dirfd(dir), name, -1, -1);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return dir;
}
