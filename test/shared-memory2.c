#include <stdio.h>
#define __USE_GNU
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <errno.h>


void reader(int fd);
void writer(int fd);

int main()
{
  // Most of main() copied from file2.c
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
  fd = mkstemp(filename);
  if (fd == -1)
    abort();

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

  /* Extend file to needed size */
  int initValue = -1;
  while ( write(fd, &initValue, sizeof(initValue)) != sizeof(initValue) )
    continue;

  if (fork())
    writer(fd);
  else
    reader(fd);
  return 0;
}

void reader(int fd) {
  int *sharedMemory;
  int i;
  sharedMemory = mmap(0, sizeof(int), PROT_READ, MAP_SHARED, fd, 0);
  if (sharedMemory == MAP_FAILED) {
    perror("mmap");
    exit(1);
  }
  for (i = -1; ; ) {
    if (*sharedMemory > i) {
      i = *sharedMemory;
      printf("%d ", i);
      fflush(stdout);
    }
    sleep(1);
  }
}

void writer(int fd) {
  int *sharedMemory;
  int i;
  sharedMemory = mmap(0, sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)
;
  if (sharedMemory == MAP_FAILED) {
    perror("mmap");
    exit(1);
  }
  for (i = 0; ; i++) {
    *sharedMemory = i;
    sleep(2);
  }
}
