#include <stdio.h>
#define __USE_GNU
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int
main()
{
  long int count = 0;
  int fd;
  int rc;
  FILE *fp;
  char filename[100];
  char dir1[100];
  char dir2[100];

  char *dir = getenv("DMTCP_TMPDIR");

  if (!dir) {
    dir = getenv("TMPDIR");
  }
  if (!dir) {
    dir = "/tmp";
  }
  if (sizeof(filename) < strlen(dir) + 3 * sizeof("/dmtcp_file_XXXXXX")) {
    printf("Directory string too large.\n");
    return 1;
  }
  strcpy(filename, dir);
  strcat(filename, "/dmtcp_dir1_XXXXXX");
  strcpy(dir1, filename);
  strcat(filename, "/dmtcp_dir2_XXXXXX");
  strcpy(dir2, filename);
  strcat(filename, "/dmtcp_file_XXXXXX");

  // Create dir1, dir2, and filename
  if (mkdtemp(dir1) == NULL) {
    abort();
  }

  memcpy(dir2, dir1, strlen(dir1));      // Update prefix, only, for dir2 path
  if (mkdtemp(dir2) == NULL) {
    abort();
  }
  memcpy(filename, dir2, strlen(dir2));  // Update prefix; dest: char[]
  fd = mkstemp(filename);
  if (fd == -1) {
    abort();
  }

  // Problematic only when in "w" mode or "a". All + modes and "r" are fine.
  fp = fdopen(fd, "w");

  fprintf(stdout, "Opened %s\n", filename);
  fprintf(stdout, "Deleting %s\n", filename);
  rc = unlink(filename);
  if (rc == -1) {
    abort();
  }
  fprintf(stdout, "Deleting %s\n", dir2);
  rc = rmdir(dir2);
  if (rc == -1) {
    abort();
  }
  fprintf(stdout, "Deleting %s\n", dir1);
  rc = rmdir(dir1);
  if (rc == -1) {
    abort();
  }

  while (1) {
    // Print count every second.
    if (count % 1000 == 0) {
      printf("%ld ", count);
      fflush(stdout);
    }
    fprintf(fp, "%ld", count++);
    // Sleep for a millisecond so we don't fill up the disk too quickly.
    usleep(1000);
  }

  return 0;
}
