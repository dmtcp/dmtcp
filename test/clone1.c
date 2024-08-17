/* Compile with:  gcc THIS_FILE [-lpthread not needed]*/

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <sched.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

void sighandler(int sig) { // for SIGUSR1
}

int start_routine(void *arg);

int is_detached = -1;
sem_t sem;
sem_t sem_wait_for_child;

int
main(int argc, char *argv[])
{
  if (argc != 2) {
    fprintf(stderr, "USAGE: %s <IS_DETACHED>\n", argv[0]);
    fprintf(stderr, "       (where iS_DETACHED is 1 (detached thread) or 0)\n");
    exit(1);
  }

  is_detached = atoi(argv[1]);
  if (is_detached) {
    signal(SIGUSR1, &sighandler);
  }

  int pid = getpid();
  size_t stack_size = 2 << 20; /* 2 MB */

  sem_init(&sem, 0, 0);
  if (! is_detached) {
    sem_init(&sem_wait_for_child, 0, 0);
  }

  while (1) {
    void *arg = malloc(10);
    char *stack = malloc(stack_size);
    *(int *)arg = 42;

    int flags = 0;
    if (is_detached) {
      // TODO: Check if detached thread has common signal hanlder
      //       with the other threads in process (i.e.: CLONE_SIGHAND).
      flags = CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SYSVSEM |
              CLONE_SIGHAND | SIGUSR1;
    } else {
      flags = CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SYSVSEM |
              CLONE_SIGHAND | CLONE_THREAD;
    }
    // Emulate a true thread, whether detached or not detached.
    pid_t tid = clone(start_routine, stack + stack_size, flags, arg);

    if (tid == -1) {
      perror("clone");
      return 1;
    }

    sem_wait(&sem);

    // Important!  Wait for child before starting new thread on same stack.
    if (is_detached) {
      int status;
      int rc = waitpid(tid, &status, __WCLONE);
      assert(rc == tid || errno == ECHILD);
      if (rc == -1) {
        printf("Detached child exited before waitpid\n");
      } else {
        printf("Detached child exited: status returned is: %d\n",
               WEXITSTATUS(status));
      }
    } else { // else not detached; simulate pthread_join() here
      sem_wait(&sem_wait_for_child);
      int num_failed_tries = 9;
      while (1) {
#ifdef SYS_tgkill 
        long rc = syscall(SYS_tgkill, pid, tid, 0); // pid is tgid in this case
#else
        long rc = syscall(SYS_tkill, tid, 0);
#endif
        if (rc == -1 && errno == ESRCH) { break; } // tid no longer exists
        num_failed_tries++;
      }
      if (num_failed_tries == 0) {
        printf("Child exited before the parent began waiting.\n");
      } else {
        printf("Child exited after the parent began waiting.\n");
      }
    }

    free(stack);
    free(arg);
  }
}

int
start_routine(void *arg)
{
  printf("%s thread executing.\n", (is_detached ? "Detached child" : "Child"));
  if (*(int *)arg != 42) {
    fprintf(stderr, "child threqd received arg %d instead of 42.\n",
                    *(int *)arg);
    return -1;
  }

  sleep(1);
  sem_post(&sem); // Tell parent we're alive.
  int time_remaining = 2;
  while (time_remaining) {
    time_remaining = sleep(time_remaining); // If we ckpt'ed, time_remaining > 0
  }
  if (! is_detached) {
    sem_post(&sem_wait_for_child);
  }
  return 99;
}
