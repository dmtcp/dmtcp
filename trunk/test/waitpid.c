#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int main ( int argc, char** argv )
{
    int childpid = fork();
    if ( childpid == 0 ) { /* if child */
      char *newargv[2];
      /* sleep for a long time */
      newargv[0] = "sleep";
      newargv[1] = "100";
      newargv[2] = NULL;
      execv ( "/bin/sleep", newargv );
      perror ( "execv");
    } else {
      int rc;
      rc = waitpid(childpid, NULL, 0);
      if (rc == -1)
        perror("waitpid");
    }
  return 0;
}
