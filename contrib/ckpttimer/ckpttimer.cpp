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

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "jassert.h"
#include "config.h"
#include "dmtcp.h"

#ifdef CKPTTIMER_PLUGIN_DEBUG
# undef JTRACE
# define JTRACE JNOTE
#endif // ifdef CKPTTIMER_PLUGIN_DEBUG

#define PRINTF(fmt, ...) \
  do { fprintf(stdout, DEBUG_SIGNATURE fmt, ## __VA_ARGS__); } while (0)

#define handleError(msg) \
  do { perror(msg); exit(EXIT_FAILURE); } while (0)


#define CLOCKID                CLOCK_REALTIME
#define DEFAULT_SIGNAL         SIGRTMIN + 2
#define SAMPLE_INTERVAL        1 // 1 second
#define START_TIMER            true
#define STOP_TIMER             false

#define PRINT_WARNING          0
#define PRINT_WARNING_AND_EXIT 1

/* Globals */
static long g_interval = SAMPLE_INTERVAL;
static int g_action = PRINT_WARNING_AND_EXIT;
static int g_sig_num = DEFAULT_SIGNAL;

/* File local functions  */
static void
get_and_save_envvars()
{
  const char *signal = getenv("DMTCP_CKPTTIMER_SIGNAL");
  const char *action = getenv("DMTCP_CKPTTIMER_ACTION");
  const char *interval = getenv("DMTCP_CKPTTIMER_INTERVAL");

  if (signal) {
    g_sig_num = atoi(signal);
    JTRACE("Using signal for ckpt timer") (g_sig_num);
  } else {
    g_sig_num = DEFAULT_SIGNAL;
  }

  if (action) {
    g_action = atoi(action);
  }

  if (interval) {
    g_interval = atol(interval);
  }
}

static void
timeout_handler(int sig, siginfo_t *si, void *uc)
{
  JWARNING(false).Text("Checkpoint took longer than expected.");
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
start_stop_timer(timer_t timerid, long interval, bool start)
{
  struct itimerspec its;

  if (start) {
    its.it_value.tv_sec = interval;
    its.it_value.tv_nsec = 0;
  } else {
#ifdef CKPTTIMER_PLUGIN_DEBUG
    if (timer_gettime(timerid, &its) == 0) {
      JTRACE("Time left: it_value, it_interval") (its.it_value.tv_sec)
        (its.it_value.tv_nsec)
        (its.it_interval.tv_sec)
        (its.it_interval.tv_nsec);
    } else {
      handleError("timer_gettime");
    }
#endif // ifdef CKPTTIMER_PLUGIN_DEBUG
    its.it_value.tv_sec = 0;
    its.it_value.tv_nsec = 0;
  }

  its.it_interval.tv_sec = its.it_value.tv_sec;
  its.it_interval.tv_nsec = its.it_value.tv_nsec;

  if (timer_settime(timerid, 0, &its, NULL) == -1) {
    handleError("timer_settime");
  }
#ifdef CKPTTIMER_PLUGIN_DEBUG
  if (!start) {
    if (timer_gettime(timerid, &its) == 0) {
      JTRACE("After disabling: it_value, it_interval") (its.it_value.tv_sec)
        (its.it_value.tv_nsec)
        (its.it_interval.tv_sec)
        (its.it_interval.tv_nsec);
    } else {
      handleError("timer_gettime");
    }
  }
#endif // ifdef CKPTTIMER_PLUGIN_DEBUG
}

static timer_t timerid = 0;
static int doneInitialization = 0;

static void
pre_ckpt()
{
  sigset_t mask;

  JTRACE("*** The plugin is being called before checkpointing. ***");

  /* Unblock the timer signal, and then start the timer */
  if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1) {
    handleError("sigprocmask");
  }
  start_stop_timer(timerid, g_interval, START_TIMER);
}

static void
resume()
{
  JTRACE("The process is now resuming after checkpoint.");

  /* Need to stop the timer on resume/restart. */
  start_stop_timer(timerid, g_interval, STOP_TIMER);
  JTRACE("*** Canceled the ckpt timer! ***");
}

static void
restart()
{
  JTRACE("The plugin is now being restarting from a checkpoint.");

  /* Need to stop the timer on resume/restart. */
  start_stop_timer(timerid, g_interval, STOP_TIMER);
  JTRACE("*** Canceled the ckpt timer! ***");
}

static void
ckpttimer_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
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

    JTRACE("The plugin has been initialized.");
    break;
  }

  case DMTCP_EVENT_PRECHECKPOINT:
    pre_ckpt();
    break;

  case DMTCP_EVENT_RESUME:
    resume();
    break;

  case DMTCP_EVENT_RESTART:
    restart();
    break;

  default:
    break;
  }
}

DmtcpPluginDescriptor_t ckpttimer_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "ckpttimer",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Ckpttimer plugin",
  ckpttimer_event_hook
};

DMTCP_DECL_PLUGIN(ckpttimer_plugin);
