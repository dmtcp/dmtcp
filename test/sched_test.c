#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int
main()
{
  cpu_set_t cset;
  int i = 0;
  int numOfCPUs = sysconf(_SC_NPROCESSORS_ONLN);

  pid_t ret = fork();

  if (ret < 0) {
    perror("Error forking a child: ");
    return -1;
  } else if (ret == 0) {
    prctl(PR_SET_PDEATHSIG, SIGHUP);
    while (1) {
      printf("Child: %d\n", i);
      sleep(1);
      i++;
    }
  } else {
    while (1) {
      i = i % numOfCPUs;

      CPU_ZERO(&cset);
      CPU_SET(i, &cset);
      printf("Trying to set CPU affinity for (%d) to (%d)\n", ret, i);
      if (sched_setaffinity(ret, sizeof(cset), &cset) < 0) {
        perror("Error setting CPU affinity for child");

        // return -1;  Turning this off for Travis containers
      } else {
        printf("CPU affinity for (%d) set to (%d)\n", ret, i);
      }
      sleep(1);

      CPU_ZERO(&cset);
      printf("Trying to read CPU affinity for (%d)\n", ret);
      if (sched_getaffinity(ret, sizeof(cset), &cset) < 0) {
        perror("Error getting CPU affinity for child");

        // return -1;
      } else {
        // assert(CPU_ISSET(i, &cset));
        printf("CPU affinity for (%d) is (%d)\n", ret, i);
      }
      sleep(1);

      i++;
    }
    wait(&i);
  }
  return 0;
}
