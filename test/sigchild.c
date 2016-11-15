#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

void
sigchldHandler(int i)
{
  printf("DMTCP is a guest.  It should never generate a SIGCHLD for user.\n");
  printf("*************************** This is a DMTCP bug.\n");
  exit(1);
}

int
main()
{
  while (1) {
    signal(SIGCHLD, &sigchldHandler);
  }

  /* NOTREACHED */
}
