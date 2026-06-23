#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dmtcp.h"

static int
str_eq(const char *left, const char *right)
{
  return left != NULL && strcmp(left, right) == 0;
}

static void
fail(const char *message)
{
  fprintf(stderr, "modify-env1: %s\n", message);
  exit(1);
}

static void
write_success(const char *path)
{
  FILE *fp = fopen(path, "w");
  if (fp == NULL) {
    perror("modify-env1 fopen");
    exit(1);
  }
  fprintf(fp, "modify-env ok\n");
  fclose(fp);
}

int
main(void)
{
  const char *success_file = getenv("DMTCP_MODIFY_ENV_SUCCESS_FILE");
  if (success_file == NULL) {
    fail("DMTCP_MODIFY_ENV_SUCCESS_FILE is not set");
  }

  int wrote_success = 0;
  while (1) {
    int num_checkpoints = 0;
    int num_restarts = 0;
    if (dmtcp_get_local_status(&num_checkpoints, &num_restarts) ==
        DMTCP_NOT_PRESENT) {
      fail("not running under DMTCP");
    }

    if (num_restarts == 0) {
      if (!str_eq(getenv("DMTCP_MODIFY_ENV_TARGET"), "before-restart")) {
        fail("initial DMTCP_MODIFY_ENV_TARGET was not preserved");
      }
      if (getenv("DMTCP_MODIFY_ENV_REMOVE") == NULL) {
        fail("initial DMTCP_MODIFY_ENV_REMOVE is missing");
      }
    } else {
      if (!str_eq(getenv("DMTCP_MODIFY_ENV_TARGET"), "after-restart")) {
        fail("DMTCP_MODIFY_ENV_TARGET was not updated after restart");
      }
      if (getenv("DMTCP_MODIFY_ENV_REMOVE") != NULL) {
        fail("DMTCP_MODIFY_ENV_REMOVE was not removed after restart");
      }
      if (!wrote_success) {
        write_success(success_file);
        wrote_success = 1;
      }
    }

    sleep(1);
  }
}
