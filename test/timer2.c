/* Inspired from:
 * http://ptgmedia.pearsoncmg.com/images/0201633922/sourcecode/sigev_thread.c
 */

#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/signal.h>
#include <sys/time.h>
#include <unistd.h>

timer_t timer_id;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
int counter = 0;

void
timer_thread(union sigval arg)
{
  sleep(1);

  assert_perror(pthread_mutex_lock(&mutex));
  printf("Timer %d\n", counter++);
  assert_perror(pthread_mutex_unlock(&mutex));
}

int
main()
{
  int status;
  struct itimerspec ts;
  struct sigevent se;

  /* Set the sigevent structure to cause the signal to be
   * delivered by creating a new thread.
   */
  se.sigev_notify = SIGEV_THREAD;
  se.sigev_value.sival_ptr = &timer_id;
  se.sigev_notify_function = timer_thread;
  se.sigev_notify_attributes = NULL;

  /* Specify a repeating timer that fires each 5 seconds.
  */
  ts.it_value.tv_sec = 1;
  ts.it_value.tv_nsec = 0;
  ts.it_interval.tv_sec = 1;
  ts.it_interval.tv_nsec = 0;

  printf("Creating timer\n");
  status = timer_create(CLOCK_REALTIME, &se, &timer_id);
  assert_perror(status);

  printf("Setting timer %p for 1-second expiration...\n", (void *)timer_id);
  status = timer_settime(timer_id, 0, &ts, 0);
  assert_perror(status);

  while (1) {
    sleep(1);
  }

  return 0;
}
