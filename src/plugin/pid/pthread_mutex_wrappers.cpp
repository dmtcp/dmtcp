/* FILE: pthread_mutex_wrappers.cpp
 * AUTHOR: Rohan Garg
 * EMAIL: rohgarg@ccs.neu.edu
 * Copyright (C) 2015 Rohan Garg
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <syscall.h>
#include <sys/types.h>
#include <vector>

#include "jassert.h"
#include "virtualpidtable.h"
#include "pidwrappers.h"

#define __real_pthread_mutex_lock    NEXT_FNC(pthread_mutex_lock)
#define __real_pthread_mutex_unlock  NEXT_FNC(pthread_mutex_unlock)
#define __real_syscall               NEXT_FNC(syscall)

/* Globals structs */
/* This maps mutex addresses to the virtual tid of the owner thread. */
static dmtcp::map<pthread_mutex_t*, int> mutexMap;

/* This maps addresses of mutexes that were locked at checkpoint time
 * to the virtual tid of the owner thread.
 */
static dmtcp::map<pthread_mutex_t*, int> prunedMutexMap;

/* File local functions  */
static void
patch_mutex_owner(pthread_mutex_t *mutex, pid_t newOwner)
{
  mutex->__data.__owner = newOwner;
}

/* Wrapper functions */
extern "C" int
pthread_mutex_lock(pthread_mutex_t *mutex)
{
  mutexMap[mutex] = dmtcp_gettid();
  return __real_pthread_mutex_lock(mutex);
}

extern "C" int
pthread_mutex_unlock(pthread_mutex_t *mutex)
{
  mutexMap[mutex] = 0;
  return __real_pthread_mutex_unlock(mutex);
}

/* Global functions */
/*
 * This function is called on WRITE_CKPT event to prepare a list
 * of mutexes that are still locked (at checkpoint time).
 */
void
pruneUnlockedMutexesAtCheckpoint()
{
  dmtcp::map<pthread_mutex_t*, int>::iterator it;
  for (it = mutexMap.begin(); it != mutexMap.end(); it++) {
    pthread_mutex_t* mutex = it->first;
    if (it->second > 0) {
      prunedMutexMap[mutex] = it->second;
    }
  }
}

/*
 * This function is called on RESTART event to patch the owners
 * of all mutexes that were not unlocked at checkpoint time.
 */
void
patchMutexesPostRestart()
{
  /* It is safe to directly access the PID virtual table here without
   * any locks because we are called on the RESTART event where
   * the only active thread is the checkpoint thread.
   */
  dmtcp::map<pid_t, pid_t> tidMap = dmtcp::VirtualPidTable::instance().getMap();
  dmtcp::map<pthread_mutex_t*, int>::iterator it;
  for (it = prunedMutexMap.begin(); it != prunedMutexMap.end(); it++) {
    pthread_mutex_t* mutex = it->first;
    JASSERT (mutex->__data.__lock) ((void*)mutex);
    if (it->second > 0) {
      patch_mutex_owner(mutex, tidMap[it->second]);
    }
  }
  prunedMutexMap.clear();
}
