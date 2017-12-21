/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel                                 *
 *   jansel@csail.mit.edu                                                   *
 *                                                                          *
 *   This file is part of the JALIB module of DMTCP (DMTCP:dmtcp/jalib).    *
 *                                                                          *
 *  DMTCP:dmtcp/jalib is free software: you can redistribute it and/or      *
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

#ifndef JALIBJFILESYSTEM_H
#define JALIBJFILESYSTEM_H

#include "dmtcpalloc.h"
#include <string>
#include <vector>

namespace jalib
{
struct linux_dirent {
  unsigned long d_ino;        /* Inode number */
  unsigned long d_off;        /* Offset to next linux_dirent */
  unsigned short d_reclen;    /* Length of this linux_dirent */
  char d_name[];              /* Filename (null-terminated) */

  /* length is actually (d_reclen - 2 -
     offsetof(struct linux_dirent, d_name) */

  /*
     char           pad;       // Zero padding byte
     char           d_type;    // File type (only since Linux 2.6.4;
                               // offset is (d_reclen - 1))
  */
};

namespace Filesystem
{
// true if a given file exists
bool FileExists(const dmtcp::string &str);

dmtcp::string GetCWD();
dmtcp::string GetProgramDir();
dmtcp::string GetProgramName();
dmtcp::string GetProgramPath();

dmtcp::string GetDeviceName(int fd);
dmtcp::string ResolveSymlink(const dmtcp::string &file);
dmtcp::string DirName(const dmtcp::string &str);
dmtcp::string BaseName(const dmtcp::string &str);
int mkdir_r(const dmtcp::string &dir, mode_t mode);

dmtcp::vector<dmtcp::string> GetProgramArgs();

dmtcp::vector<int> ListOpenFds();

dmtcp::string GetControllingTerm(pid_t pid = -1);

dmtcp::string GetCurrentHostname();
}
}
#endif // ifndef JALIBJFILESYSTEM_H
