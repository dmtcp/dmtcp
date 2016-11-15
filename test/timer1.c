// Compile as follows for debugging failure modes: gcc -DDEBUG <THIS_FILE> -lrt

// NOTE: man timer_create: "The timer IDs presented at user level are maintained
// by glibc, which maps these IDs to the timer IDs employed by the kernel.
// Therefore, one should delete the timers at ckpt time and restore
// them at resume/restart time.  This requires virtualizing the timerid.

// _POSIX_C_SOURCE is for timer_create()
#define _POSIX_C_SOURCE 199309L

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define CLOCKID CLOCK_REALTIME
#define SIG     SIGRTMIN

#define errExit(msg) \
  do { perror(msg); exit(EXIT_FAILURE); } while (0)

static void
print_siginfo(siginfo_t *si)
{
  timer_t *tidp;
  int or;

  tidp = si->si_value.sival_ptr;

  printf("    sival_ptr = %p; ", si->si_value.sival_ptr);
  printf("    *sival_ptr = 0x%lx\n", (long)*tidp);

  or = timer_getoverrun(*tidp);
  if (or == -1) {
    errExit("timer_getoverrun");
  } else {
    printf("    overrun count = %d\n", or);
  }
}

static void
handler(int sig, siginfo_t *si, void *uc)
{
  /* Note: calling printf() from a signal handler is not
     strictly correct, since printf() is not async-signal-safe;
     see signal(7) */

#ifdef DEBUG
  printf("Caught signal %d\n", sig);
#endif /* ifdef DEBUG */
  print_siginfo(si);
}

int
main(int argc, char *argv[])
{
  timer_t timerid1, timerid2;
  struct sigevent sev1, sev2;
  struct itimerspec its;
  struct timespec ts;
  long long freq_nanosecs;
  sigset_t mask;
  struct sigaction sa;
  int sleeptime;

  /* Establish handler for timer signal */

  printf("Establishing handler for signal %d\n", SIG);
  sa.sa_flags = SA_SIGINFO;
  sa.sa_sigaction = handler;
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIG, &sa, NULL) == -1) {
    errExit("sigaction");
  }

  /* Block timer signal temporarily */

  printf("Blocking signal %d\n", SIG);
  sigemptyset(&mask);
  sigaddset(&mask, SIG);
  if (sigprocmask(SIG_SETMASK, &mask, NULL) == -1) {
    errExit("sigprocmask");
  }

  /* Create the timer */

  sev1.sigev_notify = SIGEV_SIGNAL;
  sev1.sigev_signo = SIG;
  sev1.sigev_value.sival_ptr = &timerid1;
  if (timer_create(CLOCKID, &sev1, &timerid1) == -1) {
    errExit("timer_create");
  }
  sev2.sigev_notify = SIGEV_SIGNAL;
  sev2.sigev_signo = SIG;
  sev2.sigev_value.sival_ptr = &timerid2;
  if (timer_create(CLOCKID, &sev2, &timerid2) == -1) {
    errExit("timer_create");
  }

  printf("timer ID is 0x%lx\n", (long)timerid1);
  printf("timer ID is 0x%lx\n", (long)timerid2);

  /* Start the timer */

#ifdef DEBUG
  freq_nanosecs = 2000000000;    /* 2.0 seconds */
#else /* ifdef DEBUG */
  freq_nanosecs = 100000000;    /* 0.1 seconds */
#endif /* ifdef DEBUG */
  its.it_value.tv_sec = freq_nanosecs / 1000000000;
  its.it_value.tv_nsec = freq_nanosecs % 1000000000;
  its.it_interval.tv_sec = its.it_value.tv_sec;
  its.it_interval.tv_nsec = its.it_value.tv_nsec;

  if (timer_settime(timerid1, 0, &its, NULL) == -1) {
    errExit("timer_settime");
  }
  ts.tv_sec = its.it_value.tv_sec / 2;
  ts.tv_nsec = its.it_value.tv_nsec / 2;
  nanosleep(&ts, NULL);
  if (timer_settime(timerid2, 0, &its, NULL) == -1) {
    errExit("timer_settime");
  }

#ifdef DEBUG
  printf("Timer has started.  Set for %.2f seconds.\n",
         1.0 * freq_nanosecs / 1000000000);
#endif /* ifdef DEBUG */

  while (1) {
    /* Unlock the timer signal, so that timer notification
       can be delivered */
#ifdef DEBUG
    printf("Unblocking signal %d\n", SIG);
#endif /* ifdef DEBUG */
    if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1) {
      errExit("sigprocmask");
    }

    /* Sleep for a while; meanwhile, the timer may expire
       multiple times */
    sleeptime = 5;
#ifdef DEBUG
    printf("Sleeping %d seconds\n", sleeptime);
#endif /* ifdef DEBUG */

    /* If interrupted, go back and sleep remaining time. */
    while (sleeptime) {
      sleeptime = sleep(sleeptime);
    }
  }

  exit(EXIT_SUCCESS);
}

/* TEST timer_delete
       *  timer_delete(2): Disarm and delete a timer.
*/
