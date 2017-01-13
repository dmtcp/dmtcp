#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
  int childpid = fork();

  if (childpid == 0) {     /* if child */
    char *newargv[3];

    char dmtcp1Path[4096];
    strcpy (dmtcp1Path, argv[0]);
    dirname(dmtcp1Path);
    strcat(dmtcp1Path, "/dmtcp1");

    /* sleep for a long time */
    newargv[0] = "dmtcp1";
    newargv[1] = "100";
    newargv[2] = NULL;
    execv(dmtcp1Path, newargv);
    perror("execv");
  } else {
    int rc;
    rc = waitpid(childpid, NULL, 0);
    if (rc == -1) {
      perror("waitpid");
    }
  }
  return 0;
}
