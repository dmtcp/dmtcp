#include <stdio.h>
#define __USE_GNU
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
  FILE *fp;
  char filename[100];    /* "/tmp/ff_jdl_XXXXXX" for current TMPDIR */
  FILE *fp2;

  char *dir = getenv("DMTCP_TMPDIR");

  if (!dir) {
    dir = getenv("TMPDIR");
  }
  if (!dir) {
    dir = "/tmp";
  }
  if (sizeof(filename) < strlen(dir) + sizeof("/ff_jdl_XXXXXX")) {
    printf("Directory string too large.\n");
    return 1;
  }
  strcpy(filename, dir);
  strcat(filename, "/ff_jdl_XXXXXX");

  fp2 = fopen("/proc/self/exe", "r");
  if (fp2 == NULL) {
    abort();
  }

  close(mkstemp(filename));
  fd = open(filename, O_WRONLY | O_CREAT, S_IWUSR | S_IWUSR);
  if (fd == -1) {
    abort();
  }

  // Problematic only when in "w" mode or "a". All + modes and "r" are fine.
  fp = fdopen(fd, "w");

  // fp = fopen("/tmp/ff_jdl", "w");

  fprintf(stdout, "Opened ff_jdl\n");
  fprintf(stdout, "Deleting ff_jdl\n");
  unlink(filename);

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

  fprintf(stdout, "I have returned\n");
  sleep(2);
  return 0;
}
