#define _POSIX_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef LIB

// Generates libpthread_atfork.so
void
prepare(void) { printf("Before fork\n"); }

void
parent(void) { printf("After fork in parent\n"); }

void
child(void) { printf("After fork in child\n"); }

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
