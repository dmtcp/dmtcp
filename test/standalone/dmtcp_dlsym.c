/****************************************************************************
 *   Copyright (C) 2014 by Gene Cooperman                                   *
 *   gene@ccs.neu.edu                                                       *
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

#include <pthread.h>
#include <stddef.h>
#include <stdio.h>

#include "dmtcp.h"

// Compile using:
//   g++ -I../../include ../../src/dmtcp_dlsym.cpp dmtcp_dlsym.c -ldl
int
main()
{
  void *fnc;

  printf("pthread_cond_broadcast (via normal linker): %p\n",
         pthread_cond_broadcast);

  printf("================ dlsym ================\n");
  fnc = dlsym(RTLD_DEFAULT, "pthread_cond_broadcast");
  printf("pthread_cond_broadcast (via RTLD_DEFAULT): %p\n", fnc);
  fnc = dlsym(RTLD_NEXT, "pthread_cond_broadcast");
  printf("pthread_cond_broadcast (via RTLD_NEXT): %p\n", fnc);

  printf("================ dmtcp_dlsym ================\n");

  // NOTE: RTLD_DEFAULT would try to use this a.out, and fail to find a library
  // fnc = dmtcp_dlsym(RTLD_DEFAULT, "pthread_cond_broadcast");
  // printf("pthread_cond_broadcast (via RTLD_DEFAULT): %p\n", fnc);
  fnc = dmtcp_dlsym(RTLD_NEXT, "pthread_cond_broadcast");
  printf("pthread_cond_broadcast (via RTLD_NEXT): %p\n", fnc);

  return 0;
}
