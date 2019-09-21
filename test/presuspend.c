#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <assert.h>

#include "dmtcp.h"

#define NUM_THREADS 10
#define GETTID() syscall(SYS_gettid)

pid_t childPid = 0;

void presuspend1()
{
  if (childPid) {
    printf("Parent: Presuspend1: sending SIGINT to child\n");
    kill(childPid, SIGINT);
    assert(waitpid(childPid, NULL, 0) == childPid);
  } else {
    printf("Child: Presuspend1\n");
  }
  fflush(stdout);
}

void presuspend2()
{
  printf("Parent: Presuspend2\n");
  fflush(stdout);
}

void precheckpoint()
{
  printf("Parent: Precheckpoint\n");
  fflush(stdout);
}

void resume()
{
  printf("Parent: Resume\n");
  fflush(stdout);
}

static void
presuspend_eventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
  case DMTCP_EVENT_INIT:
    break;

  case DMTCP_EVENT_PRESUSPEND:
    presuspend1();
    dmtcp_global_barrier("presuspend:barrier1");
    presuspend2();
    break;

  case DMTCP_EVENT_PRECHECKPOINT:
    precheckpoint();
    break;

  case DMTCP_EVENT_RESUME:
  case DMTCP_EVENT_RESTART:
    resume();
    childPid = 0;
    break;

  default:
    break;
  }
}

DmtcpPluginDescriptor_t presuspend_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  DMTCP_PACKAGE_VERSION,
  "presuspend",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Presuspend test plugin",
  presuspend_eventHook
};

int main()
{
  dmtcp_register_plugin(presuspend_plugin);

  while (1) {
    childPid = fork();
    if (childPid == 0) {
      int childCounter = 1;
      while (1) {
        printf("Child %d\n", childCounter++);
        fflush(stdout);
        sleep(1);
      }
    } else {
      int parentCounter = 1;
      while(childPid && kill(childPid, 0) == 0) {
        printf("Parent %d\n", parentCounter++);
        fflush(stdout);
        sleep(1);
      }
      printf("Parent: Child exited; will fork another child.\n");
      fflush(stdout);

      // Wait for resume before forking another child.
      // TODO(Kapil): Add support for creating  new processes in Presuspend
      //              phase.
      while (childPid != 0) {
        sleep(1);
      }
    }
  }

  return 0;
}
