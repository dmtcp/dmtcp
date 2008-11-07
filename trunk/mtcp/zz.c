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

/********************************************************************************************************************************/
/*																*/
/*  Simple multi-threaded test program												*/
/*  Checkpoint is written to testmtcp2.mtcp every 10 seconds									*/
/*  It uses the mtcp_wrapper_clone routine to create the threads								*/
/*																*/
/*  Four threads are created													*/
/*  First thread prints 1 11 21 31 41 51 61 ...											*/
/*  Second thread prints 2 12 22 32 42 52 62 ...										*/
/*  Third thread prints 3 13 23 33 43 53 63 ...											*/
/*  Fourth thread prints 4 14 24 34 44 54 64 ...										*/
/*  When checkpoint is restored, the counts should resume where they left off							*/
/*																*/
/********************************************************************************************************************************/

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define STACKSIZE 1024*1024
#define THREADFLAGS (CLONE_FS | CLONE_FILES | CLONE_VM)

/* Set *loc to newval iff *loc equal to oldval */
/* Return 0 if failed, 1 if succeeded          */

static inline int atomic_setif_int (volatile int *loc, int newval, int oldval)

{
  char rc;

  asm volatile ("lock\n\t"
                "cmpxchgl %2,%3\n\t"
                "sete     %%al"
                : "=a" (rc)
                :  "a" (oldval), "r" (newval), "m" (*loc)
                : "cc", "memory");

  return (rc);
}

static void *thread1_func (void *dummy);

int main ()

{
  char *thread1_stack;
  int i, thread1_tid;
  pthread_t threadid;

  for (i = 0; i < 1; i ++) {
    thread1_stack = malloc (STACKSIZE);
    thread1_tid = pthread_create (&threadid, NULL, thread1_func, NULL);
    if (thread1_tid < 0) {
      fprintf (stderr, "error creating thread1: %s\n", strerror (-thread1_tid));
      return (-1);
    }
  }

  thread1_func (NULL);
  return (0);
}

static int volatile threadno = 0;

static void *thread1_func (void *dummy)

{
  char ok;
  int count, delay;

  do count = threadno;
  while (!atomic_setif_int (&threadno, count + 1, count));

  count ++;

  while (1) {
    for (delay = 100; -- delay >= 0;) usleep (10);
    printf (" %d", count);
    fflush (stdout);
    count += 10;
  }
}
