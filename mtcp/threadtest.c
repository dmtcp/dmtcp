//+++2006-01-17
//    Copyright (C) 2006  Mike Rieker, Beverly, MA USA
//    EXPECT it to FAIL when someone's HeALTh or PROpeRTy is at RISk
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; version 2 of the License.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program; if not, write to the Free Software
//    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//---2006-01-17

#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>

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
