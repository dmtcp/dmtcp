#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dmtcp.h"

static const char checkpoint_content[] = "checkpointed-open-file\n";

static void
fail(const char *message)
{
  fprintf(stderr, "allow-file-overwrite: %s\n", message);
  exit(1);
}

static void
write_all(int fd, const char *buf, size_t len)
{
  while (len > 0) {
    ssize_t written = write(fd, buf, len);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      perror("allow-file-overwrite write");
      exit(1);
    }
    buf += written;
    len -= (size_t)written;
  }
}

static void
write_success(const char *path)
{
  FILE *fp = fopen(path, "w");
  if (fp == NULL) {
    perror("allow-file-overwrite fopen");
    exit(1);
  }
  fprintf(fp, "allow-file-overwrite ok\n");
  fclose(fp);
}

static void
verify_checkpoint_content(const char *path)
{
  char buf[sizeof(checkpoint_content)];
  FILE *fp = fopen(path, "r");
  if (fp == NULL) {
    perror("allow-file-overwrite fopen");
    exit(1);
  }

  size_t count = fread(buf, 1, sizeof(buf) - 1, fp);
  if (ferror(fp)) {
    perror("allow-file-overwrite fread");
    exit(1);
  }
  buf[count] = '\0';
  fclose(fp);

  if (strcmp(buf, checkpoint_content) != 0) {
    fprintf(stderr,
            "allow-file-overwrite: expected %s but found %s\n",
            checkpoint_content, buf);
    exit(1);
  }
}

int
main(void)
{
  const char *path = getenv("DMTCP_ALLOW_FILE_OVERWRITE_PATH");
  const char *success_file =
    getenv("DMTCP_ALLOW_FILE_OVERWRITE_SUCCESS_FILE");
  if (path == NULL) {
    fail("DMTCP_ALLOW_FILE_OVERWRITE_PATH is not set");
  }
  if (success_file == NULL) {
    fail("DMTCP_ALLOW_FILE_OVERWRITE_SUCCESS_FILE is not set");
  }

  int fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0600);
  if (fd < 0) {
    perror("allow-file-overwrite open");
    return 1;
  }
  write_all(fd, checkpoint_content, strlen(checkpoint_content));

  while (1) {
    int num_checkpoints = 0;
    int num_restarts = 0;
    if (dmtcp_get_local_status(&num_checkpoints, &num_restarts) ==
        DMTCP_NOT_PRESENT) {
      fail("not running under DMTCP");
    }

    if (num_restarts > 0) {
      verify_checkpoint_content(path);
      write_success(success_file);
    }

    usleep(100000);
  }

  return 0;
}
