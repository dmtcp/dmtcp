/* Compile with:  gcc THIS_FILE -lpthread */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <semaphore.h>

sem_t threadInitialized;
sem_t dummySem;

void *
start_routine(void *arg)
{
  int threadId = *(int*)arg;
  free(arg);

  if (threadId % 2) {
    printf("ThreadId: %d alive; setting cancellation mode to ASYNC\n", threadId);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
  } else {
    printf("ThreadId: %d alive; setting cancellation mode to DEFERRED\n", threadId);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
  }

  sem_post(&threadInitialized);

  // Will get canceled in here as sem_wait() is a cancellation point.
  sem_wait(&dummySem);

  // Shouldn't reach here.
  pthread_exit(NULL);
}

pthread_t create_thread(int threadId)
{
  pthread_t thread;
  /* thread will free arg, and pass back to us a different arg */
  int *arg = (int*) malloc(sizeof(int));
  *arg = threadId;

  int res = pthread_create(&thread, NULL, start_routine, arg);
  if (res != 0) {
    fprintf(stderr, "error creating thread: %s\n", strerror(res));
    exit(-1);
  }

  return thread;
}

void cancel_and_join_thread(pthread_t th)
{
  int res = pthread_cancel(th);
  if (res != 0) {
    fprintf(stderr, "pthread_cancel failed.");
    exit(-1);
  }

  void *threadRet = NULL;
  res = pthread_join(th, &threadRet);
  if (res != 0) {
    fprintf(stderr, "pthread_join() failed: %s\n", strerror(res));
    exit(-1);
  }

  if (threadRet != PTHREAD_CANCELED) {
    fprintf(stderr, "Thread didn't get canceled.");
    exit(-1);
  }
}

int
main()
{
  pthread_t thread;
  int *arg;

  pthread_attr_t attr;
  pthread_attr_init(&attr);

  sem_init(&threadInitialized, 0, 0);
  sem_init(&dummySem, 0, 0);

  int threadId = 0;
  while (1) {
    // Create two threads; one with async cancellation and another with deferred.
    pthread_t th1 = create_thread(threadId++);
    pthread_t th2 = create_thread(threadId++);

    // Wait for threads to finish initialization.
    sem_wait(&threadInitialized);
    sem_wait(&threadInitialized);

    cancel_and_join_thread(th1);
    cancel_and_join_thread(th2);
  }
}
