/* Compile with:  gcc THIS_FILE -lpthread */

// _GNU_SOURCE required for MAP_ANONYMOUS
#define _GNU_SOURCE

// pthread_attr_setstack() needs _POSIX_C_SOURCE >= 200112L
#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

void *start_routine(void *);

int
main()
{
  pthread_t thread;
  void *arg;
  size_t stacksize = 1024 * 1024;
  void *stackaddr = mmap(0, stacksize, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  assert(stackaddr != MAP_FAILED);
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstack(&attr, stackaddr, stacksize);

  while (1) {
    arg = malloc(10);
    int res = pthread_create(&thread, &attr, start_routine, arg);
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
  void *valuePtr = malloc(20);
  pthread_exit(valuePtr);
}
