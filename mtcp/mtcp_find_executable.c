/*****************************************************************************
 *   Copyright (C) 2006-2009 by Michael Rieker, Jason Ansel, Kapil Arya, and *
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

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define MTCP_MAX_PATH 256

int mtcp_is_executable(const char *exec_path) {
  struct stat stat_buf;
  /* Bash says "have to use access(2) to determine access because AFS
    does not [find] answers for non-AFS files when ruid != euid." ??  */
  return 0 == stat(exec_path, &stat_buf)
    && S_ISREG(stat_buf.st_mode) && stat_buf.st_mode & S_IXOTH;
}

/* Caller must allocate exec_path of size at least MTCP_MAX_PATH */
char * mtcp_find_executable(char *executable, char exec_path[MTCP_MAX_PATH]){
  char *path;
  char *path_env = getenv("PATH");
  int len;

  if (path_env == NULL) {
    *exec_path = 0;
    return NULL;
  }

  while (*path_env != '\0') {
    path = exec_path;
    len = 0;
    while (*path_env != ':' && *path_env != '\0' && ++len < MTCP_MAX_PATH - 1)
      *path++ = *path_env++;
    if (*path_env == ':') /* but if *path_env == '\0', will exit while loop */
      path_env++;
    *path++ = '/'; /* '...//... is same as .../... in POSIX */
    len++;
    *path++ = '\0';
    strncat(exec_path, executable, MTCP_MAX_PATH - len - 1);
    if (mtcp_is_executable(exec_path))
      return exec_path;
  }
  return NULL;
}
