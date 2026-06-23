#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "dmtcp.h"

static void
fail(const char *message)
{
  fprintf(stderr, "pathvirt1: %s\n", message);
  exit(1);
}

static const char *
required_env(const char *name)
{
  const char *value = getenv(name);
  if (value == NULL || value[0] == '\0') {
    fprintf(stderr, "pathvirt1: %s is not set\n", name);
    exit(1);
  }
  return value;
}

static void
seed_real_file(const char *real_dir, const char *real_file)
{
  if (mkdir(real_dir, 0700) != 0 && errno != EEXIST) {
    perror("pathvirt1 mkdir");
    exit(1);
  }

  FILE *fp = fopen(real_file, "w");
  if (fp == NULL) {
    perror("pathvirt1 fopen real file");
    exit(1);
  }
  fprintf(fp, "pathvirt-data\n");
  fclose(fp);
}

static void
check_virtual_file(const char *virtual_file)
{
  char buffer[64];
  FILE *fp = fopen(virtual_file, "r");
  if (fp == NULL) {
    perror("pathvirt1 fopen virtual file");
    exit(1);
  }
  if (fgets(buffer, sizeof buffer, fp) == NULL) {
    fclose(fp);
    fail("virtual file was empty");
  }
  fclose(fp);

  if (strcmp(buffer, "pathvirt-data\n") != 0) {
    fail("virtual file contents did not match real file contents");
  }
}

static void
write_success(const char *path)
{
  FILE *fp = fopen(path, "w");
  if (fp == NULL) {
    perror("pathvirt1 fopen success file");
    exit(1);
  }
  fprintf(fp, "pathvirt ok\n");
  fclose(fp);
}

int
main(void)
{
  const char *real_dir = required_env("DMTCP_PATHVIRT_REAL_DIR");
  const char *real_file = required_env("DMTCP_PATHVIRT_REAL_FILE");
  const char *virtual_file = required_env("DMTCP_PATHVIRT_VIRTUAL_FILE");
  const char *success_file = required_env("DMTCP_PATHVIRT_SUCCESS_FILE");

  seed_real_file(real_dir, real_file);

  int wrote_success = 0;
  while (1) {
    int num_checkpoints = 0;
    int num_restarts = 0;
    if (dmtcp_get_local_status(&num_checkpoints, &num_restarts) ==
        DMTCP_NOT_PRESENT) {
      fail("not running under DMTCP");
    }

    check_virtual_file(virtual_file);
    if (num_restarts > 0 && !wrote_success) {
      write_success(success_file);
      wrote_success = 1;
    }

    sleep(1);
  }
}
