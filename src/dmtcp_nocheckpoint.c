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

// compile with: gcc -o dmtcp_nocheckpoint -static dmtcp_nocheckpoint.cpp
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "constants.h"  // for ENV_VAR_ORIG_LD_PRELOAD
#include "protectedfds.h"

static void restoreUserLDPRELOAD();

int
main(int argc, char **argv)
{
  if (getenv("LD_PRELOAD")) {
    restoreUserLDPRELOAD();
  }
  if (argc == 1) {
    fprintf(stderr, "USAGE:  %s cmd ...\n", argv[0]);
    return 1;
  }
  size_t fd;
  for (fd = PROTECTED_FD_START; fd < PROTECTED_FD_END; fd++) {
    close(fd);
  }
  execvp(argv[1], argv + 1);
  perror("execvp:");
  return 2;
}

// This is a copy of the code in dmtcpworker.cpp:restoreUserLDPRELOAD()
// Please keep this function in sync.
// Note that the DMTCP exec wrappers will set ENV_VAR_ORIG_LD_PRELOAD.
static void
restoreUserLDPRELOAD()
{
  char *preload = getenv("LD_PRELOAD");
  char *userPreload = getenv(ENV_VAR_ORIG_LD_PRELOAD);

  // This is a C program.  JASSERT and JTRACE are not available:
  // JASSERT(userPreload == NULL || strlen(userPreload) <= strlen(preload));
  // Destructively modify environment variable "LD_PRELOAD" in place:
  preload[0] = '\0';
  if (userPreload == NULL) {
    // _dmtcp_unsetenv("LD_PRELOAD");
  } else {
    strcat(preload, userPreload);

    // setenv("LD_PRELOAD", userPreload, 1);
  }

  // JTRACE("LD_PRELOAD") (preload) (userPreload) (getenv(ENV_VAR_HIJACK_LIBS))
  // (getenv(ENV_VAR_HIJACK_LIBS_M32)) (getenv("LD_PRELOAD"));
}
