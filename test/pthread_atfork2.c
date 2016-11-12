// This test is motivated by the usage in tclsh version 8.6

// It appears as if tclsh-8.6 creates a new thread within the child
// routine registered by pthread_atfork().  However, some simple gdb tests
// have not shown pthread_atfork() being used so far.  We should look
// closely at the tclsh source code.

#define _POSIX_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

void *
busy_loop(void *arg)
{
  while (1) {
    sleep(5);
  }
}

#ifdef LIB

// Generates libpthread_atfork.so
void
prepare(void) { printf("Before fork\n"); }

void
parent(void) { printf("After fork in parent\n"); }

void
child(void)
{
  printf("After fork in child\n");
  pthread_t thread;
  pthread_create(&thread, NULL, busy_loop, NULL);

  // ENABLING THIS CODE CAUSES BUG IN dmtcp-2.4.2
  // Test if we reach here (new process), or if early thread creation hangs.
  // if (fork() == 0) { // if child
  // busy_loop(NULL);
  // }
}

void myconstructor(void) __attribute__((constructor));
void
myconstructor(void)
{
  pthread_atfork(prepare, parent, child);
}

void
foo(void) {}

#else /* ifdef LIB */

// Generates executable:  pthread_atfork
void foo(void);

int
main(int argc, char **argv)
{
  // Force linking of library compiled from this file with 'gcc -DLIB ...'
  foo();

  int childpid = fork();

  // ENABLING THIS CODE CAUSES BUG IN dmtcp-2.4.2
  // Test if we reach here (new process), or if early thread creation hangs.
  // if (fork() == 0) { // if child
  // busy_loop(NULL);
  // }
  if (childpid == 0) {    // if child
    int count = 0;
    while (1) {
      printf(" %2d ", count++);
      fflush(stdout);
      sleep(2);
    }
  } else { // else parent
    waitpid(childpid, NULL, 0);
    printf("*** ERROR: child finished early.");
    return 1;
  }
  return 0;
}
#endif /* ifdef LIB */
