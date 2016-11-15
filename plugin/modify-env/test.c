#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dmtcp.h"

int
main(int argc, char *argv[])
{
  char *user = (char *)malloc(strlen(getenv("USER")) + 1);

  strcpy(user, getenv("USER"));
  printf("These tests depend on dmtcp_env.txt in local directory.\n");

  int retval = dmtcp_checkpoint();
  switch (retval) {
  case DMTCP_NOT_PRESENT:
    printf("" " test.c:  Not launched under DMTCP.  Please try again.\n");
    return 1;

  case DMTCP_AFTER_CHECKPOINT:
    return 0;  // Continue upon restart

  case DMTCP_AFTER_RESTART:
    break;
  default:
    printf("*** test.c:  Internal error");
    return 1;
  }

  int newuser_len = strlen("new-") + strlen(getenv("HOME")) + 1 + strlen(user) +
    1;
  char *newuser = (char *)malloc(newuser_len);
  strcpy(newuser, "new-");
  strcpy(newuser + strlen(newuser), getenv("HOME"));
  strcpy(newuser + strlen(newuser), "-");
  strcpy(newuser + strlen(newuser), user);
  printf("getenv(\"HOME\"): %s\ngetenv(\"FOO\"): %s\ngetenv(\"USER\"): %s\n",
         getenv("HOME"), getenv("FOO"), getenv("USER"));
  if (getenv("EDITOR")) {
    printf("Failure!  EDITOR (was): %s; EDITOR (should be): NULL\n",
           getenv("EDITOR"));
    return 1;
  }
  if (strcmp(newuser, getenv("USER")) == 0) {
    printf("Success!\n");
    return 0;
  } else {
    printf("Failure!  USER (was): %s; USER (should be): %s\n",
           getenv("USER"), newuser);
    return 1;
  }
}
