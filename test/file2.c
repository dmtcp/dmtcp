#include <stdio.h>
#define __USE_GNU
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

int main()
{
    long int count = 0;
    int fd;
    int rc;
    FILE *fp;
    char filename[100];
    char dir1[100];
    char dir2[100];

    char *dir = getenv("DMTCP_TMPDIR");
    if (!dir) dir = getenv("TMPDIR");
    if (!dir) dir = "/tmp";
    if (sizeof(filename) < strlen(dir) + 3*sizeof("/dmtcp_file_XXXXXX")) {
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
    if (mkdtemp(dir1) == NULL) abort();
    strncpy(dir2, dir1, strlen(dir1));  // Update new prefix
    if (mkdtemp(dir2) == NULL) abort();
    strncpy(filename, dir2, strlen(dir2));  // Update new prefix
    fd = mkostemp(filename, O_WRONLY|O_CREAT);
    if (fd == -1)
      abort();

    // Problematic only when in "w" mode or "a". All + modes and "r" are fine.
    fp = fdopen(fd, "w");

    fprintf(stdout, "Opened %s\n", filename);
    fprintf(stdout, "Deleting %s\n", filename);
    rc = unlink(filename);
    if (rc == -1) abort();
    fprintf(stdout, "Deleting %s\n", dir2);
    rc = rmdir(dir2);
    if (rc == -1) abort();
    fprintf(stdout, "Deleting %s\n", dir1);
    rc = rmdir(dir1);
    if (rc == -1) abort();

    while (1) {
      if (count % (int)1e6 == 0) {
        printf("%ld ", count);
        fflush(stdout);
      }
      fprintf(fp, "%ld", count++);
    }

    return 0;
}
