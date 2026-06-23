#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DMTCP_IS_PRESENT 4

extern int dmtcp_get_local_status(int *num_checkpoints, int *num_restarts)
  __attribute__((weak));
extern const char *dmtcp_get_tmpdir(void) __attribute__((weak));

static void
fail(const char *message)
{
  fprintf(stderr, "restart-tmpdir: %s\n", message);
  exit(1);
}

static const char *
required_env(const char *name)
{
  const char *value = getenv(name);
  if (value == NULL || value[0] == '\0') {
    fprintf(stderr, "restart-tmpdir: %s is not set\n", name);
    exit(1);
  }
  return value;
}

static int
path_is_under(const char *path, const char *root)
{
  size_t root_len = strlen(root);
  return strncmp(path, root, root_len) == 0 &&
         (path[root_len] == '/' || path[root_len] == '\0');
}

static void
write_success(const char *path)
{
  FILE *fp = fopen(path, "w");
  if (fp == NULL) {
    perror("restart-tmpdir fopen");
    exit(1);
  }
  fprintf(fp, "restart-tmpdir ok\n");
  fclose(fp);
}

int
main(void)
{
  const char *tmpdir_root = required_env("DMTCP_RESTART_TMPDIR_ROOT");
  const char *success_file =
    required_env("DMTCP_RESTART_TMPDIR_SUCCESS_FILE");
  int wrote_success = 0;

  while (1) {
    int num_checkpoints = 0;
    int num_restarts = 0;
    if (dmtcp_get_local_status == NULL ||
        dmtcp_get_local_status(&num_checkpoints, &num_restarts) !=
          DMTCP_IS_PRESENT) {
      fail("not running under DMTCP");
    }

    if (num_restarts > 0 && !wrote_success) {
      if (dmtcp_get_tmpdir == NULL) {
        fail("dmtcp_get_tmpdir is unavailable");
      }
      const char *tmpdir = dmtcp_get_tmpdir();
      if (tmpdir == NULL || !path_is_under(tmpdir, tmpdir_root)) {
        fprintf(stderr,
                "restart-tmpdir: tmpdir %s is not under requested root %s\n",
                tmpdir == NULL ? "(null)" : tmpdir,
                tmpdir_root);
        exit(1);
      }
      write_success(success_file);
      wrote_success = 1;
    }

    sleep(1);
  }
}
