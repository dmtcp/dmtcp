#define _POSIX_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>

#ifdef LIB
void prepare(void) { printf("Before fork\n"); }
void parent(void) { printf("After fork in parent\n"); }
void child(void) { printf("After fork in child\n"); }

void myconstructor(void) __attribute__ ((constructor));
void myconstructor(void) {
    pthread_atfork(prepare, parent, child);
}

void foo(void) {}
#else
void foo(void);

static void die ( const char* msg )
{
  printf ( "ERROR: %s \n", msg );
  _exit ( -1 );
}

int main ( int argc, char** argv )
{
  FILE* fp = NULL;

  // Force loading of library compiled with -DLIB
  foo();

  int childpid = fork();
  if ( childpid == 0 ) {  // if child
    int count = 0;

    while (1)
    { printf(" %2d ",count++);
      fflush(stdout);
      sleep(2);
    }
  }
  else  // else parent
  { waitpid(childpid, NULL, 0);
    printf("*** ERROR: child finished early.");
  }
}
#endif
