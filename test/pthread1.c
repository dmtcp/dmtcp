/* Compile with:  gcc THIS_FILE -lpthread */

// __USE_GNU is needed for pthread_getattr_np().
// assert.h is undefining __USE_GNU as of glibc-2.35.  Arguably, a bug.
// #define __USE_GNU
#define _GNU_SOURCE     /* To get pthread_getattr_np() declaration */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *start_routine(void *);

int
main()
{
  pthread_t thread;
  void *arg;

  pthread_attr_t attr;
  void *stackaddr;
  size_t stacksize;
  int ret = pthread_getattr_np(pthread_self(), &attr);
  if (ret != 0) {fprintf(stderr, "error: ret: %d\n", ret);}
  assert(ret == 0);

  ret = pthread_attr_getstack(&attr, &stackaddr, &stacksize);
  assert(stacksize > 0);

  while (1) {
    arg = malloc(10);
    int res = pthread_create(&thread, NULL, start_routine, arg);
    if (res != 0) {
      fprintf(stderr, "error creating thread: %s\n", strerror(res));
      return -1;
    }

    /* thread will free arg, and pass back to us a different arg */
    res = pthread_join(thread, &arg);
    if (res != 0) {
      fprintf(stderr, "pthread_join() failed: %s\n", strerror(res));
      return -1;
    }
    free(arg);
  }
}

void *
start_routine(void *arg)
{
  free(arg);

  pthread_attr_t attr;
  void *stackaddr;
  size_t stacksize;
  int ret = pthread_getattr_np(pthread_self(), &attr);
  assert(ret == 0);
  ret = pthread_attr_getstack(&attr, &stackaddr, &stacksize);
  assert(stacksize > 0);

  void *valuePtr = malloc(20);
  pthread_exit(valuePtr);
}
