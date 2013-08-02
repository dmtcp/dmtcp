/*****************************************************************************
 *   Copyright (C) 2006-2008 by Michael Rieker, Jason Ansel, Kapil Arya, and *
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

#define _GNU_SOURCE
#include <errno.h>
#include <sched.h> // for clone, _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define NTHREADS 3
#define STACKSIZE 1024*1024

static int clonefunc (void *threadindexv);

int main ()

{
  char *esps[NTHREADS];
  int i, pids[NTHREADS], rc;

  for (i = 0; i < NTHREADS; i ++) {
    esps[i] = malloc (STACKSIZE);
    esps[i] += STACKSIZE;
    fprintf (stderr, "esp[%d]=%p\n", i, esps[i]);
    rc = clone (clonefunc, esps[i], CLONE_FS | CLONE_FILES | CLONE_VM, (void *)(long)i);
    if (rc < 0) {
      fprintf (stderr, "error creating thread %d: %s\n", i, strerror (errno));
      return (-1);
    }
    pids[i] = rc;
  }

  sleep (10000);
  return (0);
}

static int clonefunc (void *threadindexv)

{
  long threadindex = (long)threadindexv;

  printf ("Thread index %ld\n", threadindex);
  sleep (3);
  return (0);
}
