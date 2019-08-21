/* Copyright (C) 2003-2013 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Ulrich Drepper <drepper@redhat.com>, 2003.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public License as
   published by the Free Software Foundation; either version 2.1 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If
   not, see <http://www.gnu.org/licenses/>.  */

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "timerwrappers.h"

#include "jalloc.h"
#include "jassert.h"

// As defined in libc.
#define SIGCANCEL SIGRTMIN
#define SIGTIMER  SIGCANCEL

/* Internal representation of timer.  */
struct timer {
  /* Notification mechanism.  */
  int sigev_notify;

  /* Parameters for the thread to be started for SIGEV_THREAD.  */
  void (*thrfunc) (sigval_t);
  sigval_t sival;
  pthread_attr_t attr;

  /* Next element in list of active SIGEV_THREAD timers.  */
  struct timer *next;
};

struct thread_start_data {
  void (*thrfunc) (sigval_t);
  sigval_t sival;
};

/* List of active SIGEV_THREAD timers.  */
struct timer *active_timer_sigev_thread;

/* Lock for the active_timer_sigev_thread.  */
DmtcpMutex active_timer_sigev_thread_lock = DMTCP_MUTEX_INITIALIZER;

/* Control variable for helper thread creation.  */
static pthread_once_t helper_once;

/* TID of the helper thread.  */
static pid_t helper_tid = 0;
static sem_t helper_notification;

static void *timer_helper_thread(void *arg);
static void start_helper_thread(void);

/* Reset variables so that after a fork a new helper thread gets started.  */
static void
timer_create_reset_on_fork(void)
{
  helper_once = PTHREAD_ONCE_INIT;
  helper_tid = 0;
}

LIB_PRIVATE
int
timer_create_sigev_thread(clockid_t clock_id,
                          struct sigevent *evp,
                          timer_t *timerid,
                          struct sigevent *sevOut)
{
  /* If the user wants notification via a thread we need to handle
     this special.  */
  JASSERT(evp == NULL || evp->sigev_notify == SIGEV_THREAD);

  /* Create the helper thread.  */
  pthread_once(&helper_once, start_helper_thread);
  sem_wait(&helper_notification);
  if (helper_tid == 0) {
    /* No resources to start the helper thread.  */
    errno = EAGAIN;
    return -1;
  }

  struct timer *newp;
  newp = (struct timer *)JALLOC_MALLOC(sizeof(struct timer));
  if (newp == NULL) {
    return -1;
  }

  /* Copy the thread parameters the user provided.  */
  newp->sival = evp->sigev_value;
  newp->thrfunc = evp->sigev_notify_function;
  newp->sigev_notify = SIGEV_THREAD;

  /* We cannot simply copy the thread attributes since the
     implementation might keep internal information for
     each instance.  */
  (void)pthread_attr_init(&newp->attr);

  // TODO: Copy attributes from evp->sigev_notify_attributes to newp->attr.

  /* In any case set the detach flag.  */
  (void)pthread_attr_setdetachstate(&newp->attr, PTHREAD_CREATE_DETACHED);

  /* Create the event structure for the kernel timer.  */
  sevOut->sigev_value.sival_ptr = newp;
  sevOut->sigev_signo = SIGTIMER;
  sevOut->sigev_notify = SIGEV_SIGNAL | SIGEV_THREAD_ID;
  sevOut->_sigev_un._tid = helper_tid;

  /* Create the timer.  */
  int res = _real_timer_create(clock_id, sevOut, timerid);
  if (res == 0) {
    /* Add to the queue of active timers with thread delivery.  */
    DmtcpMutexLock(&active_timer_sigev_thread_lock);
    newp->next = active_timer_sigev_thread;
    active_timer_sigev_thread = newp;
    DmtcpMutexUnlock(&active_timer_sigev_thread_lock);
    return 0;
  }

  /* Free the resources.  */
  JALLOC_FREE(newp);
  return -1;
}

/* Helper thread to call the user-provided function.  */
static void *
timer_sigev_thread(void *arg)
{
  /* The parent thread has all signals blocked.  This is a bit
     surprising for user code, although valid.  We unblock all
     signals.  */
  sigset_t ss;

  sigemptyset(&ss);
  pthread_sigmask(SIG_SETMASK, &ss, NULL);

  struct thread_start_data *td = (struct thread_start_data *)arg;

  void (*thrfunc) (sigval_t) = td->thrfunc;
  sigval_t sival = td->sival;

  /* The TD object was allocated in timer_helper_thread.  */
  JALLOC_FREE(td);

  /* Call the user-provided function.  */
  thrfunc(sival);

  return NULL;
}

/* Helper function to support starting threads for SIGEV_THREAD.  */
static void *
timer_helper_thread(void *arg)
{
  helper_tid = syscall(SYS_gettid);
  sem_post(&helper_notification);

  /* Wait for the SIGTIMER signal, allowing the setXid signal, and
     none else.  */
  sigset_t ss;
  sigemptyset(&ss);
  sigaddset(&ss, SIGTIMER);

  /* Endless loop of waiting for signals.  The loop is only ended when
     the thread is canceled.  */
  while (1) {
    siginfo_t si;

    /* sigwaitinfo cannot be used here, since it deletes
       SIGCANCEL == SIGTIMER from the set.  */

    pthread_testcancel();

    // int oldtype = LIBC_CANCEL_ASYNC ();

    /* XXX The size argument hopefully will have to be changed to the
       real size of the user-level sigset_t.  */
    int result = sigtimedwait(&ss, &si, NULL);

    // LIBC_CANCEL_RESET (oldtype);

    if (result > 0) {
      if (si.si_code == SI_TIMER) {
        struct timer *tk = (struct timer *)si.si_ptr;

        /* Check the timer is still used and will not go away
           while we are reading the values here.  */
        DmtcpMutexLock(&active_timer_sigev_thread_lock);

        struct timer *runp = active_timer_sigev_thread;
        while (runp != NULL) {
          if (runp == tk) {
            break;
          } else {
            runp = runp->next;
          }
        }

        if (runp != NULL) {
          struct thread_start_data *td =
            (struct thread_start_data *)JALLOC_MALLOC(sizeof(*td));

          /* There is not much we can do if the allocation fails.  */
          if (td != NULL) {
            /* This is the signal we are waiting for.  */
            td->thrfunc = tk->thrfunc;
            td->sival = tk->sival;

            pthread_t th;
            (void)pthread_create(&th, &tk->attr, timer_sigev_thread, td);
          }
        }

        DmtcpMutexUnlock(&active_timer_sigev_thread_lock);
      } else if (si.si_code == SI_TKILL) {
        /* The thread is canceled.  */
        pthread_exit(NULL);
      }
    }
  }
}

static void
start_helper_thread(void)
{
  sem_init(&helper_notification, 0, 0);

  /* The helper thread needs only very little resources
     and should go away automatically when canceled.  */
  pthread_attr_t attr;
  (void)pthread_attr_init(&attr);
  (void)pthread_attr_setstacksize(&attr, 2 * 1024 * 1024);

  /* Block all signals in the helper thread but SIGSETXID.  To do this
     thoroughly we temporarily have to block all signals here.  The
     helper can lose wakeups if SIGCANCEL is not blocked throughout,
     but sigfillset omits it SIGSETXID.  So, we add SIGCANCEL back
     explicitly here.  */
  sigset_t ss;
  sigset_t oss;
  sigfillset(&ss);
  sigaddset(&ss, SIGCANCEL);
  sigprocmask(SIG_SETMASK, &ss, &oss);

  /* Create the helper thread for this timer.  */
  pthread_t th;
  int res = pthread_create(&th, &attr, timer_helper_thread, NULL);
  JASSERT(res == 0);
  if (res != 0) {
    sem_post(&helper_notification);
  }

  /* Restore the signal mask.  */
  sigprocmask(SIG_SETMASK, &oss, NULL);

  /* No need for the attribute anymore.  */
  (void)pthread_attr_destroy(&attr);

  /* We have to make sure that after fork()ing a new helper thread can
     be created.  */
  pthread_atfork(NULL, NULL, timer_create_reset_on_fork);
}
