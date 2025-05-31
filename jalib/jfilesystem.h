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

#include <sys/stat.h> // for mode_t
#include "dmtcpalloc.h"
#include <string>
#include <vector>

namespace jalib
{
namespace Filesystem
{
// true if a given file exists
bool FileExists(const dmtcp::string &str);

dmtcp::vector<dmtcp::string> ListDirEntries(const dmtcp::string& dir);
dmtcp::string GetCWD();
dmtcp::string GetProgramDir();
dmtcp::string GetProgramName();
dmtcp::string GetProgramPath();

dmtcp::string GetDeviceName(int fd);
dmtcp::string ResolveSymlink(const dmtcp::string &file);
dmtcp::string DirName(const dmtcp::string &str);
void DirName(char *dirname, const char *fname);
dmtcp::string BaseName(const dmtcp::string &str);
int mkdir_r(const dmtcp::string &dir, mode_t mode);

dmtcp::vector<int> ListOpenFds();

dmtcp::string GetControllingTerm(pid_t pid = -1);

dmtcp::string GetCurrentHostname();
}
}
#endif // ifndef JALIBJFILESYSTEM_H
