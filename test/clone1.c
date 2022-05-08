/* Compile with:  gcc THIS_FILE -lpthread */

#define _GNU_SOURCE
#include <sched.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int start_routine(void *);

sem_t sem;


int
main()
{
  void *arg;
  void *stack;
  size_t stack_size = 2 << 20; /* 2 MB */

  sem_init(&sem, 0, 0);

  while (1) {
    arg = malloc(10);
    *(int *)arg = 42;
    stack = malloc(stack_size);

    // Emulate a true thread, but return a SIGCHLD to parent's sig. handler
    pid_t tid = clone(start_routine, stack + stack_size /* top of stack */,
           CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SYSVSEM | CLONE_SIGHAND |
           CLONE_THREAD, arg);

    if (tid == -1) {
      perror("clone");
      return 1;
    }

    // A child thread is _not_ a child process.  We can't use waitpid(),
    // and we can't look at the thread's exit status.;
    //    int status;
    //    int res = waitpid(tid, &status, __WCLONE);
    sem_wait(&sem);

    free(arg);
    free(stack);
  }
}

int
start_routine(void *arg)
{
  printf("Child thread executing.\n");
  if (*(int *)arg != 42) {
    fprintf(stderr, "child threqd received arg %d instead of 42.\n",
                    *(int *)arg);
    return -1;
  }
  sleep(1);
  sem_post(&sem);
  return 99;
}
