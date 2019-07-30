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
    // Child process
    printf("Child process initialized.  (parent pid: %d)\n", getpid());
    while (1);
  }

  // Parent DMTCP  lib was already initizlied using default RLIMIT_NOFILE.
  // This fd_limit should cause the child to initialize with different limits.
  // Verify that this does not create inconcsistency in DMTCP protectedFdBase.
  struct rlimit fd_limit = {4096/2, 4096/2} ;
  int retStatus = getrlimit(RLIMIT_NOFILE, &fd_limit) ;
  if( retStatus == -1 ) {
    perror("getrlimit"); exit(1);
  }
  if (fd_limit.rlim_cur < fd_limit.rlim_max) {
    fd_limit.rlim_cur = fd_limit.rlim_max;
  } else {
    fd_limit.rlim_cur = fd_limit.rlim_cur/2;
    fd_limit.rlim_max = fd_limit.rlim_cur;
  }
  retStatus = setrlimit(RLIMIT_NOFILE, &fd_limit) ;
  if( retStatus == -1 ) {
    perror("setrlimit"); exit(1);
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
