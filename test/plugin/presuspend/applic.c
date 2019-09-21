/* Compile with:  gcc THIS_FILE -lpthread */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <assert.h>

#ifndef NUM_THREADS
# define NUM_THREADS 10
#endif
#define GETTID() syscall(SYS_gettid)

pid_t childpid = -1;

void *do_thread_work(void * dummy) {
  int mytasks = 0;
  while (1) {
    sleep(1);
    mytasks += 1;
    if (getpid() == GETTID()) {
      printf("."); fflush(stdout);
    }
    // This has effect only if running under dmtcp_launch with presuspend plugin
    extern int check_if_doing_checkpoint(int num_tasks) __attribute((weak));
    if (check_if_doing_checkpoint) {
      if (check_if_doing_checkpoint(mytasks)) {
        break;
      }
    }
  }
}

void do_child_work() {
  // This child process is killed by primary thread by end of PRESUSPEND event..
  while (1);
}

int main() {
  while (1) {
    // Start up a new round of work; finish with a checkpoint; and then come
    // back here for the next round of work.
    childpid = fork();
    if (childpid == 0) {
      do_child_work();
    } else {
      printf("*** Parent just forked a child process. ***\n");
      // This is a weak function.  We call it only if DMTCP plugin is present.
      // The DMTCP plugin then discovers childpid and num_threads, as below.
      extern void
      export_childpid_and_num_threads(pid_t mychildpid, int num_threads)
      __attribute((weak));
      if (export_childpid_and_num_threads) {
        export_childpid_and_num_threads(childpid, NUM_THREADS);
      }
    }

    pthread_t thread;
    int i;
    for (i = 0; i<(NUM_THREADS-1); i++) {
      pthread_create(&thread, NULL, do_thread_work, NULL);
    }
    printf("*** %d helper threads were just started. ***\n", NUM_THREADS-1);
    do_thread_work(NULL); // Now, NUM_THREADS are working, including primary.
    // We return here after the checkpoint.  Let's create more threads and child
  }

  return 0;

}
