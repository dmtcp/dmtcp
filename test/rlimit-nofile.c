#include <stdio.h>
#include <sys/resource.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

#define CHILD "child_process"

int main(int argc, char* argv[])
{
  if (argc == 2 && strcmp(argv[1], CHILD) == 0) {
    printf("Second process pid: %d\n", getppid());
    while (1);
  }

  struct rlimit fd_limit = {4096*2, 4096*2} ;
  int retStatus = setrlimit(RLIMIT_NOFILE, &fd_limit) ;
  if( retStatus == -1 )
  {
    printf("Error setrlimit...\n") ;
    exit(1);
  }
  int childpid = fork();
  if (childpid == 0) {
    char *myargv[] = {NULL, CHILD, NULL};
    myargv[0] = argv[0];
    execvp(argv[0], myargv);
  } else {
    waitpid(childpid, NULL, 0);
  }
}
