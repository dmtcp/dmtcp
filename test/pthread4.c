#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <unistd.h>

#define NUM_THREADS   4
#define NUM_THREADS_2 1000 * 1000

pthread_t child_thread_id[NUM_THREADS];
pthread_mutex_t count_mutex = PTHREAD_MUTEX_INITIALIZER;

int counter = 0;

void *
child_thread_2(void *arg)
{
  pthread_mutex_lock(&count_mutex);
  counter++;
  pthread_mutex_unlock(&count_mutex);
  return NULL;
}

void *
child_thread(void *arg)
{
  int i;

  printf("Child thread...\n");
  for (i = 0; i < NUM_THREADS_2; i++) {
    pthread_t my_child;
    int rc = pthread_create(&my_child, NULL, child_thread_2, NULL);
    if (rc == 0) {
      pthread_join(my_child, NULL);
    }
  }
  printf("\nThread increment completed\n");
  return NULL;
}

int
main(int argc, char *argv[])
{
  int i, rc;

  printf("\nCreating threads...\n");
  for (i = 0; i < NUM_THREADS; i++) {
    rc = pthread_create(&child_thread_id[i], NULL, child_thread, NULL);
    if (rc < 0) {
      child_thread_id[i] = -1;
    }
  }
  printf("\nJoining threads...\n");
  for (i = 0; i < NUM_THREADS; i++) {
    if (child_thread_id[i] == -1) {
      continue;
    }
    int ret = pthread_join(child_thread_id[i], NULL);
    if (ret != 0) {
      printf("\n\n pthread_join returned an error :%d\n", ret);
    }
  }
  printf("\nEnd counter value: %d\n", counter);
  fflush(stdout);
  return 0;
}
