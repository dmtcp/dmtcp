#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <asm/prctl.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <pthread.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#define gettid() syscall(SYS_gettid)

unsigned long fs_base1;
unsigned long fs_base2;

void * start_routine2(void * arg)
{
  pid_t tid = gettid();
  printf ("TID of thread 2 before switching fs = %d\n", tid);
  // save fs of second thread;
  int res = syscall(SYS_arch_prctl, ARCH_GET_FS, &fs_base2);
  if (res != 0) {
    fprintf(stderr, "error setting the FS base register: %s\n", strerror(res));
  }
  printf ("FS Base of thread2 before switch = %p\n", (void *)fs_base2);
  fflush(stdout);
  pthread_exit(NULL);
}


void * start_routine1(void * arg)
{
  pid_t tid = gettid();
  printf ("TID of thread 1 before switching fs = %d\n", tid);
  // save fs of first thread;
  int res = syscall(SYS_arch_prctl, ARCH_GET_FS, &fs_base1);
  if (res != 0) {
    fprintf(stderr, "error getting the FS base register: %s\n", strerror(res));
  }
  printf ("FS Base of thread1 before switch = %p\n", (void *)fs_base1);
  // sleep a little to make suer  that another thread saves its base register
  sleep(10);

  // switch FS base to thread2
  res = syscall(SYS_arch_prctl, ARCH_SET_FS, fs_base2);
  if (res != 0) {
    fprintf(stderr, "error setting the FS base register: %s\n", strerror(res));
  }

  unsigned long fs_base;
  res = syscall(SYS_arch_prctl, ARCH_GET_FS, &fs_base);
  if (res != 0) {
    fprintf(stderr, "error getting the FS base register: %s\n", strerror(res));
  }
  assert(fs_base == fs_base2);
  printf ("FS Base of thread1 after switching = %p\n", (void *)fs_base);
  assert(tid == gettid());
  fflush(stdout);
  /*
    Currently, checkpoint after fs base register switch doesn't work until it
    switches back to original.
    TODO: find the root cause and fix 
  */
  // now switch back to original
  res = syscall(SYS_arch_prctl, ARCH_SET_FS, fs_base1);
  if (res != 0) {
    fprintf(stderr, "error setting the FS base register: %s\n", strerror(res));
  }
  res = syscall(SYS_arch_prctl, ARCH_GET_FS, &fs_base);
  if (res != 0) {
    fprintf(stderr, "error getting the FS base register: %s\n", strerror(res));
  }
  assert(fs_base == fs_base1);
  printf ("FS Base of thread1 after switched back = %p\n", (void *)fs_base);
  fflush(stdout);
  assert(tid == gettid());
  sleep(10);
  printf("done!\n");
  pthread_exit(NULL);
}

int main()
{
  pthread_t thread1;
  pthread_t thread2;
  void *arg = NULL;
  while(1) {
    int res = pthread_create(&thread1, NULL, start_routine1, arg);
    if (res != 0) {
      fprintf(stderr, "error creating thread: %s\n", strerror(res));
      return -1;
    }
    res = pthread_create(&thread2, NULL, start_routine2, arg);
    if (res != 0) {
      fprintf(stderr, "error creating thread: %s\n", strerror(res));
      return -1;
    }

    res = pthread_join(thread1, arg);
    if (res != 0) {
      fprintf(stderr, "pthread_join() failed: %s\n", strerror(res));
      return -1;
    }
    res = pthread_join(thread2, arg);
    if (res != 0) {
      fprintf(stderr, "pthread_join() failed: %s\n", strerror(res));
      return -1;
    }
  }
  return 0;
}

