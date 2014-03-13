/****************************************************************************
 *   Copyright (C) 2006-2010 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

//compile with: gcc -o dmtcp_nocheckpoint -static dmtcp_nocheckpoint.cpp
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include "protectedfds.h"

int main(int argc, char** argv) {
  unsetenv("LD_PRELOAD");
  if(argc==1){
    fprintf(stderr, "USAGE %s cmd...\n", argv[0]);
    return 1;
  }
  size_t fd;
  for (fd = PROTECTED_FD_START; fd < PROTECTED_FD_END; fd++) {
    close(fd);
  }
  execvp(argv[1], argv+1);
  perror("execvp:");
  return 2;
}
