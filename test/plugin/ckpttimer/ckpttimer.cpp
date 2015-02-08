/* FILE: ckpttimer.cpp
 * AUTHOR: Rohan Garg
 * EMAIL: rohgarg@ccs.neu.edu
 * Copyright (C) 2015 Rohan Garg
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>

#include "dmtcp.h"
#include "jassert.h"

#define DEBUG_SIGNATURE "[Ckpttimer Plugin]"
#ifdef CKPTTIMER_PLUGIN_DEBUG
# define DPRINTF(fmt, ...) \
  do { fprintf(stderr, DEBUG_SIGNATURE fmt, ## __VA_ARGS__); } while (0)
#else
# define DPRINTF(fmt, ...) \
  do { } while (0)
#endif

#define PRINTF(fmt, ...) \
  do { fprintf(stdout, DEBUG_SIGNATURE fmt, ## __VA_ARGS__); } while (0)

#define handleError(msg) \
    do { perror(msg); exit(EXIT_FAILURE); } while (0)


#define CLOCKID         CLOCK_REALTIME
#define DEFAULT_SIGN    SIGRTMIN+2
#define SAMPLE_INTERVAL 1000000000 // 1 second
#define START_TIMER     true
#define STOP_TIMER      false

#define PRINT_WARNING           0
#define PRINT_WARNING_AND_EXIT  1

/* Globals */
static long long g_interval = SAMPLE_INTERVAL;
static int g_action  = PRINT_WARNING_AND_EXIT;
static int g_sig_num = DEFAULT_SIGN;

/* File local functions  */
static void
get_and_save_envvars()
{
  const char *signal = getenv("DMTCP_CKPTTIMER_SIGNAL");
  const char *action = getenv("DMTCP_CKPTTIMER_ACTION");
  const char *interval = getenv("DMTCP_CKPTTIMER_INTERVAL");

#ifdef CKPTTIMER_PLUGIN_TEST
  static int dummy = 0;
  while (!dummy);
#endif

  if (signal) {
    g_sig_num = atoi(signal);
    DPRINTF("Using signal (%d) for ckpt timer\n", g_sig_num);
  } else {
    g_sig_num = DEFAULT_SIGN;
  }

  if (action) {
    g_action = atoi(action);
  }

  if (interval) {
    g_interval = atoll(interval);
  }
}

static void
timeout_handler(int sig, siginfo_t *si, void *uc)
{
  JWARNING("Checkpoint took longer than expected.");
  fflush(stdout);
  if (g_action == PRINT_WARNING_AND_EXIT) {
    JASSERT(false)("Killing the application.");
  }
  signal(sig, SIG_IGN);
}

static void
setup_handler()
{
  struct sigaction sa;
  /* Set up a signal handler for timeouts */
  sa.sa_flags = SA_SIGINFO;
  sa.sa_sigaction = timeout_handler;
  sigemptyset(&sa.sa_mask);
  if (sigaction(g_sig_num, &sa, NULL) == -1) {
      handleError("sigaction");
  }
}

static timer_t
make_timer()
{
  timer_t timerid = 0;
  struct sigevent sev;

  setup_handler();

  sev.sigev_notify = SIGEV_SIGNAL;
  sev.sigev_signo = g_sig_num;
  sev.sigev_value.sival_ptr = &timerid;
  if (timer_create(CLOCKID, &sev, &timerid) == -1) {
      handleError("timer_create");
  }

  return timerid;
}

static void
start_stop_timer(timer_t timerid, long long interval, bool start)
{
  struct itimerspec its;

  if (start) {
    its.it_value.tv_sec = interval / 1000000000;
    its.it_value.tv_nsec = interval % 1000000000;
  } else {
#ifdef CKPTTIMER_PLUGIN_DEBUG
    if (timer_gettime(timerid, &its) == 0) {
      DPRINTF("it_value(%lld, %lld), it_interval(%lld, %lld)\n", its.it_value.tv_sec, its.it_value.tv_nsec, its.it_interval.tv_sec, its.it_interval.tv_nsec);
    } else {
      handleError("timer_gettime");
    }
#endif
    its.it_value.tv_sec = 0;
    its.it_value.tv_nsec = 0;
  }

  its.it_interval.tv_sec = its.it_value.tv_sec;
  its.it_interval.tv_nsec = its.it_value.tv_nsec;

  if (timer_settime(timerid, 0, &its, NULL) == -1) {
    handleError("timer_settime");
  }
#ifdef CKPTTIMER_PLUGIN_DEBUG
  if (timer_gettime(timerid, &its) == 0) {
    DPRINTF("After disabling: it_value(%lld, %lld), it_interval(%lld, %lld)\n", its.it_value.tv_sec, its.it_value.tv_nsec, its.it_interval.tv_sec, its.it_interval.tv_nsec);
  } else {
    handleError("timer_gettime");
  }
#endif
}

extern "C" void
dmtcp_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  static timer_t timerid = 0;
  static int doneInitialization = 0;
  sigset_t mask;

  switch (event) {
    case DMTCP_EVENT_INIT:
      {
        if (!doneInitialization) {
          get_and_save_envvars();

          /* Create a timer */
          timerid = make_timer();
          sigemptyset(&mask);
          sigaddset(&mask, g_sig_num);
          doneInitialization = 1;
        }

        DPRINTF("The plugin containing %s has been initialized.\n", __FILE__);
        break;
      }
    case DMTCP_EVENT_WRITE_CKPT:
      {
        DPRINTF("*** The plugin is being called before checkpointing. ***\n");
        /* Unblock the timer signal, and then start the timer */
        if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1)
          handleError ("sigprocmask");
        start_stop_timer(timerid, g_interval, START_TIMER);
        break;
      }
    case DMTCP_EVENT_THREADS_SUSPEND:
      {
        DPRINTF("*** The plugin is being called after suspend. ***\n");
        break;
      }
    case DMTCP_EVENT_RESUME:
      {
        DPRINTF("*** The plugin has now been checkpointed. ***\n");
#ifdef CKPTTIMER_PLUGIN_TEST
        static int dummy = 0;
        while (!dummy);
#endif
        start_stop_timer(timerid, g_interval, STOP_TIMER);
        DPRINTF("*** Cancelled the ckpt timer! ***\n");
        break;
      }
    case DMTCP_EVENT_THREADS_RESUME:
      {
        if (data->resumeInfo.isRestart) {
          DPRINTF("The plugin is now restarting from checkpointing.\n");
#ifdef CKPTTIMER_PLUGIN_TEST
          static int dummy = 0;
          while (!dummy);
#endif
          /* Need to stop the timer on restart. */
          start_stop_timer(timerid, g_interval, STOP_TIMER);
          DPRINTF("*** Cancelled the ckpt timer! ***\n");
        } else {
          DPRINTF("The process is now resuming after checkpoint.\n");
        }
        break;
      }
    case DMTCP_EVENT_EXIT:
      DPRINTF("The plugin is being called before exiting.\n");
      break;
    /* These events are unused and could be omitted.  See dmtcp.h for
     * complete list.
     */
    case DMTCP_EVENT_RESTART:
    case DMTCP_EVENT_ATFORK_CHILD:
    case DMTCP_EVENT_LEADER_ELECTION:
    case DMTCP_EVENT_DRAIN:
    default:
      break;
  }
  DMTCP_NEXT_EVENT_HOOK(event, data);
}
