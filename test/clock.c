#define _XOPEN_SOURCE 600
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
  clockid_t id;
  struct timespec ts;

  if (clock_getcpuclockid(getpid(), &id) != 0) {
    perror("clock_getcpuclockid");
    exit(EXIT_FAILURE);
  }

  while (1) {
    if (clock_gettime(id, &ts) == -1) {
      perror("clock_gettime");
      exit(EXIT_FAILURE);
    }

    printf("CPU-time clock (%d) for PID %s is %ld.%09ld seconds\n",
           id, argv[1], (long)ts.tv_sec, (long)ts.tv_nsec);
    fflush(stdout);
    sleep(1);
  }
  exit(EXIT_SUCCESS);
}
