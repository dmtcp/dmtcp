//+++2007-10-06
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
//---2007-10-06

/********************************************************************************************************************************/
/*																*/
/*  Another simple multi-threaded test program											*/
/*  Checkpoint is written to testmtcp4.mtcp every 10 seconds									*/
/*  It uses pthread_create routine to create the threads									*/
/*																*/
/*  Typical command line:  testmtcp4 5 6 100											*/
/*																*/
/*    5 = means create 5 threads												*/
/*    6 = means each thread has 6MB of random data to compute									*/
/*    100 = means each thread runs 100 iterations										*/
/*																*/
/*    You can have from 1 to 9 threads												*/
/*    You can have up to 2GB total data												*/
/*																*/
/*  Each thread generates number in sequence n 1n 2n 3n 4n 5n 6n ...  where n is digit 1 through 9				*/
/*  The main thread consumes the produced numbers and prints them out								*/
/*																*/
/********************************************************************************************************************************/

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mtcp.h"

#define QUEUESIZE 4
#define COUNTINC 10

static pthread_mutex_t indexmutex  = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t producawait = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t consumawait = PTHREAD_MUTEX_INITIALIZER;

static int babblesize;
static int l2babblesize;
static int nproducers;
static int niterations;
static int queuevalues[QUEUESIZE];
static int volatile consumaindex = 0;
static int volatile producaindex = 0;
static long long malloctotal = 0;
static long long freetotal = 0;

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

static void *produca_func (void *dummy);
static void *consuma_func (void *dummy);

int main (int argc, char *argv[])

{
  char *p;
  int i;
  pthread_t thread_tid;

  if (argc != 4) goto usage;

  nproducers = strtol (argv[1], &p, 0);
  if ((*p != 0) || (nproducers <= 0) || (nproducers > 9)) {
    fprintf (stderr, "testmtcp4: invalid number_of_producers %s\n", argv[1]);
    goto usage;
  }

  babblesize = strtol (argv[2], &p, 0);
  if ((*p != 0) || (babblesize <= 0) || (babblesize * nproducers > 2*1024)) {
    fprintf (stderr, "testmtcp4: invalid number_of_megabytes %s\n", argv[2]);
    goto usage;
  }
  babblesize <<= 20;
  babblesize /= sizeof (int);
  for (l2babblesize = 0; (1 << l2babblesize) < babblesize; l2babblesize ++) { }

  niterations = strtol (argv[3], &p, 0);
  if ((*p != 0) || (niterations <= 0)) {
    fprintf (stderr, "testmtcp4: invalid number_of_iterations %s\n", argv[3]);
    goto usage;
  }

  mtcp_init ("testmtcp4.mtcp", 10, 1);
  mtcp_ok ();

  for (i = 0; i < nproducers; i ++) {
    if (pthread_create (&thread_tid, NULL, produca_func, NULL) < 0) {
      fprintf (stderr, "error creating produca: %s\n", strerror (errno));
      return (-1);
    }
  }

  consuma_func (NULL);
  return (0);

usage:
  fprintf (stderr, "usage: testmtcp4 <number_of_producers> <number_of_megabytes> <iterations>\n");
  fprintf (stderr, "   number_of_producers in range 1..9\n");
  fprintf (stderr, "   number_of_megabytes * number_of_producers <= 2G\n");
  fprintf (stderr, "   number_of_iterations to run producers\n");
  fprintf (stderr, "   an example is: testmtcp4 5 6 100\n");
  return (-1);
}

static int threadno = 0;

static void *produca_func (void *dummy)

{
  int *babblebuff, babblelen, count, i, iter, j, queuewasempty;
  struct timespec sleeptime;

  do count = threadno;                                       // get an unique number for low digit
  while (!atomic_setif_int (&threadno, count + 1, count));

  count ++;

  for (iter = niterations; -- iter >= 0;) {
    babblelen  = random () % l2babblesize;                   // malloc a random-sized buffer
    babblelen  = random () % (1 << babblelen);
    babblebuff = malloc (babblelen * sizeof *babblebuff);
    if (babblebuff == NULL) {
      fprintf (stderr, "Out of memory!\n");
      abort ();
    }
    malloctotal += babblelen * sizeof *babblebuff;
    count += COUNTINC;                                       // this is next value to store in queue
    memset (&sleeptime, 0, sizeof sleeptime);                // wait up to a tenth of a second
    sleeptime.tv_nsec = random () % 100000000;
    nanosleep (&sleeptime, NULL);
    for (i = babblelen; -- i >= 0;) {                        // perform some useless computation
      babblebuff[i] = random ();
    }
    while (1) {
      pthread_mutex_lock (&indexmutex);                      // lock the indices
      i = producaindex;                                      // see where to put next value
      j = (i + 1) % QUEUESIZE;
      if (j != consumaindex) break;                          // but make sure there's room
      pthread_mutex_unlock (&indexmutex);                    // queue full, unlock indicies
      pthread_mutex_lock (&producawait);                     // ... then wait for room
    }
    queuevalues[i] = count;                                  // there's room, put value in queue
    producaindex = j;                                        // increment producer index
    queuewasempty = (consumaindex == i);                     // remember if queue was empty or not
    pthread_mutex_unlock (&indexmutex);                      // unlock indices
    if (queuewasempty) pthread_mutex_unlock (&consumawait);  // wake consumer if queue was empty
    if ((random () % QUEUESIZE) == 0) {
      free (babblebuff);     // on rare occaision, do a free
      freetotal += babblelen * sizeof *babblebuff;
    }
  }
  return babblebuff;  // not used, but POSIX requires a return value
}

static void *consuma_func (void *dummy)

{
  int i, j, *prodlastcount, queuewasfull, value;

  prodlastcount = alloca (nproducers * sizeof *prodlastcount);
  for (i = 0; i < nproducers; i ++) {
    prodlastcount[i] = i + 1;
  }

  while (1) {
    pthread_mutex_lock (&indexmutex);                         // lock the indices
    i = consumaindex;
    if (i == producaindex) {                                  // see if there is anything in the queue
      pthread_mutex_unlock (&indexmutex);                     // if not, unlock the indices
      pthread_mutex_lock (&consumawait);                      // ... and wait for something in queue
    } else {
      j = (producaindex + 1) % QUEUESIZE;                     // something there, see if queue is full
      queuewasfull = (j == i);
      value = queuevalues[i++];                               // get value
      i %= QUEUESIZE;                                         // increment past it to remove it
      consumaindex = i;
      pthread_mutex_unlock (&indexmutex);                     // unlock the indices
      if (queuewasfull) pthread_mutex_unlock (&producawait);  // if queue was full, wake any waiting producers
      printf (" %d", value);                                  // print the value out
      fflush (stdout);
      i = (value - 1) % COUNTINC;                             // see which producer wrote the entry
      prodlastcount[i] += COUNTINC;                           // see what value we should get from it
      if (prodlastcount[i] != value) {                        // make sure that's what we got
        pthread_mutex_lock (&indexmutex);                     // shut other threads up
        fprintf (stderr, "Value should be %d\n", prodlastcount[i]);
        abort ();                                             // barf!
      }
      for (i = nproducers; -- i >= 0;) {
        if (prodlastcount[i] / COUNTINC != niterations) break;
      }
      if (i < 0) {
        printf ("\nAll iterations complete!\n");
        printf ("  malloctotal=%lld\n", malloctotal);
        printf ("    freetotal=%lld\n", freetotal);
        break;
      }
    }
  }
  return prodlastcount; // Not used, but POSIX requires a return value
}
