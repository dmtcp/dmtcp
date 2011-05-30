/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#ifndef UTIL_H
#define UTIL_H

#include <stdlib.h>
#include <string.h>
#include <string>
#include <sstream>
#include <fcntl.h>
#include <sys/syscall.h>
#include "constants.h"
#include "dmtcpalloc.h"

namespace Util
{
  void lockFile(int fd);
  void unlockFile(int fd);

  bool strStartsWith(const char *str, const char *pattern);
  bool strEndsWith(const char *str, const char *pattern);
  bool strStartsWith(const dmtcp::string& str, const char *pattern);
  bool strEndsWith(const dmtcp::string& str, const char *pattern);

  ssize_t writeAll(int fd, const void *buf, size_t count);
  ssize_t readAll(int fd, void *buf, size_t count);

  //char *dmtcpBasename(char *pathname);
  int isdir0700(const char *pathname);
  int safeMkdir(const char *pathname, mode_t mode);
  int safeSystem(const char *command);

  int expandPathname(const char *inpath, char * const outpath, size_t size);
  int elfType(const char *pathname, bool *isElf, bool *is32bitElf);

  bool isStaticallyLinked(const char *filename);


  bool isScreen(const char *filename);
  dmtcp::string getScreenDir();
  bool isSetuid(const char *filename);
  void freePatchedArgv(char **newArgv);
  void patchArgvIfSetuid(const char* filename, char *const origArgv[],
                         char **newArgv[]);
}

#endif
