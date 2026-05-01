#include <stdio.h>
#define __USE_GNU
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define DMTCP_PACKAGE_VERSION "test"
#include "dmtcp.h"

#define EXPECTED_MODE 0640

int
main()
{
  long int count = 0;
  int rc;
  int fd;
  char filename[100];
  char oldFilename[100];
  struct stat statbuf;
  int dmtcpEnabled = dmtcp_is_enabled();
  uint32_t generation = 0;
  int renamedAfterCkpt = 0;
  int sawRecreatedFile = 0;

  char *dir = getenv("DMTCP_TMPDIR");

  if (!dir) {
    dir = getenv("TMPDIR");
  }
  if (!dir) {
    dir = "/tmp";
  }

  if (sizeof(filename) < strlen(dir) + sizeof("/file_recreate.out.0")) {
    printf("Directory string too large.\n");
    return 1;
  }
  strcpy(filename, dir);
  strcat(filename, "/file_recreate.out");
  strcpy(oldFilename, dir);
  strcat(oldFilename, "/file_recreate.out.0");

  unlink(filename);
  unlink(oldFilename);

  fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, EXPECTED_MODE);
  if (fd < 0) {
    abort();
  }

  rc = stat(filename, &statbuf);
  if (rc == -1) {
    abort();
  }
  if ((statbuf.st_mode & 07000) != 0 ||
      (statbuf.st_mode & 0777) != EXPECTED_MODE) {
    abort();
  }

  if (dmtcpEnabled) {
    generation = dmtcp_get_generation();
  }

  while (1) {
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%ld\n", count++);
    if (write(fd, buf, n) != n) {
      abort();
    }

    if (!renamedAfterCkpt && dmtcpEnabled && dmtcp_get_generation() > generation) {
      // Rename the file after checkpoint so restart must recreate it.
      renamedAfterCkpt = 1;
      printf("Renaming %s to %s\n", filename, oldFilename);
      rc = rename(filename, oldFilename);
      if (rc == -1) {
        abort();
      }
      fflush(stdout);
    }

    if (renamedAfterCkpt) {
      rc = stat(filename, &statbuf);
      if (rc == 0) {
        sawRecreatedFile = 1;
        if ((statbuf.st_mode & 07000) != 0 ||
            (statbuf.st_mode & 0777) != EXPECTED_MODE) {
          abort();
        }
      } else if (errno != ENOENT) {
        abort();
      }
    }

    if (count % 1000 == 0) {
      // Print count every second.
      printf("%ld ", count);
      fflush(stdout);
    }

    // Sleep for a millisecond so we don't fill up the disk too quickly.
    usleep(1000);
  }

  return 0;
}
