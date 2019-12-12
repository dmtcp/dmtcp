/* Compile with:  gcc THIS_FILE -lpthread */

/****************************************************************************
 * Overview of logic of presuspend plugin (and proof there is no race condition)
 * =============================================================================
 *   0. A checkpoint request causes the PRESUSPEND event to be triggered.
 *   1. The ckpt thread sets in_presuspend to 1 in PRESUSPEND event.
 *   2. All user threads see this through check_if_doing_checkpoint(),
 *      which causes them to enter ckpt_cleanup().  In ckpt_cleanup(),
 *      they all add their tasks to total_num_tasks before next barrier.
 *   3. Inside ckpt_cleanup(), all user threads reach presuspend_barrier.
 *   4a. Inside ckpt_cleanup(), secondary threads exit; and
 *   4b. Inside ckpt_cleanup(), primary thread can print total_num_threads.
 *         The primary thread then reaches exit_presuspend_barrier .
 *   5. The primary thread later reaches resume_barrier in ckpt_cleanup().
 *   6. It is now safe for ckpt thread (in PRESUSPEND event) to reset
 *        in_presuspend to 0, since only the primary user thread is active,
 *        and it stops at the resume_barrier in ckpt_cleanup().
 ****************************************************************************/

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

extern void dmtcp_get_libc_dlsym_add(void)
  __attribute((weak));

#define GETTID() syscall(SYS_gettid)

// Older versions of GCC support __thread.
// So, for portability, we won't use the newer C11 keyword, thread_local.
volatile static int total_num_tasks = 0;
volatile int in_presuspend = 0;

// This information is local to this file (and hence to this library):
static pid_t plugin_childpid = -1;
static pthread_mutex_t mutex;
static pthread_barrier_t presuspend_barrier; // For user threads of parent proc.
static pthread_barrier_t exit_presuspend_barrier; // For primary and ckpt thread
static pthread_barrier_t resume_barrier;// Primary & ckpt thread: resume/restart

// The base executable can call this to set our childpid and barrier info.
void export_childpid_and_num_threads(pid_t childpid, int num_threads) {
  plugin_childpid = childpid;
  pthread_barrier_init(&presuspend_barrier, NULL, num_threads);
  // TO AVOID RACE:  Guarantee initializing barrier before PRESUSPEND event.
}

int is_child() {
  return (plugin_childpid == -1 || plugin_childpid == 0);
}

int is_alive(pid_t tid) {
  int rc;
  if (tid == plugin_childpid) {
    rc = kill(tid, 0);
  } else {
    rc = pthread_kill(tid, 0);
  }
  return (rc == -1 && errno == ESRCH ? 0 : 1);
}

void ckpt_cleanup(int mytasks) {
  // return to normal work if I'm the child process
  if (is_child()) { return; }

  assert(plugin_childpid > 0);
  // If we reach here, we are the parent process.

  pthread_mutex_lock(&mutex);
  total_num_tasks += mytasks;
  pthread_mutex_unlock(&mutex);

  pthread_barrier_wait(&presuspend_barrier);
  // If we reach the other side of the barrier, then each thread
  //   has now added their thread-local value of 'mytasks' to 'total_num_tasks'.

  if (getpid() == GETTID()) {
    // If I'm the primary thread, print total_num_taks, kill child and continue.
    printf("\n*** Parent process's primary thread printing num tasks of all"
      " user threads.\n"
      "************************************\n"
      "* Total number of tasks done is:  %d\n"
      "************************************\n", total_num_tasks);
    total_num_tasks = 0;  // Reset it to 0 for the next use.

    // Kill the child process.
    int rc;
    rc = kill(plugin_childpid, SIGINT);
    if (rc == -1) {perror("kill"); printf("*** FAILURE ***\n"); exit(1);}
    rc = waitpid(plugin_childpid, NULL, 0);
    if (rc == -1) {perror("waitpid"); printf("*** FAILURE ***\n"); exit(1);}
    printf("*** Parent just sent kill signal SIGINT to child;"
           " waitpid() confirms cild gone.");

    // FIXME:  The right way to guarantee that the secondary threads are dead
    //          is to use pthread_join().  We'll simply sleep while they exit.
    sleep(1);
    // Ckpt thread was waiting on this barrier; ckpt thread can now continue.
    pthread_barrier_wait(&exit_presuspend_barrier);
    // Ckpt thread will wait on this during resume or restart.
    // So, we (primary user thread) can finish this routine on resume/restart.
    pthread_barrier_wait(&resume_barrier);
  } else {
    // Non-primary threads exit.  In applic.c, they had reported num tasks done.
    pthread_exit(0);
  }

  return;
}

int check_if_doing_checkpoint(int num_tasks) {
  if (in_presuspend) {
    ckpt_cleanup(num_tasks);
    return 1; // true
  } else {
    return 0; // false
  }
}


// =============================================================
// Define and invoke barrier event functions


// DMTCP_EVENT_PRESUSPEND is a callback from DMTCP.  ckpt thread executes here.
static void
presuspend() 
{
  if (is_child()) { return; } // Child doesn't participate in presuspend.

  printf("\n*** Plugin %s in %s:  Ckpt thread at presuspend barrier. ***\n",
         __FILE__,
         (is_child() ? "child" : "parent"));
}

static void
precheckpoint()
{
  printf("\n*** The plugin %s is being called before checkpointing. ***\n"
         "*** (The thread is at the checkpoint barrier.) ***\n",
         __FILE__);
  printf("*** childpid %d has%s been killed.  Checkpointing...\n\n",
	 (int) plugin_childpid, (is_alive(plugin_childpid) ? " _NOT_" : ""));
  // FIXME: SHOULD ADD SIMILAR LOGIC FOR each thread tid.
}

static void
resume()
{
  printf("*** Applic. for plugin %s has been checkpointed. Resuming...***\n",
         __FILE__);
}

static void
presuspend_eventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
  case DMTCP_EVENT_INIT:
    // Ckpt thread respons to this callback.
    // Primary user thread and ckpt thread synchronize on this barrier.
    pthread_barrier_init(&exit_presuspend_barrier, NULL, 2);
    pthread_barrier_init(&resume_barrier, NULL, 2);
    break;

  case DMTCP_EVENT_PRESUSPEND:
    presuspend();
    in_presuspend = 1;
    // Primary user thread prints number of tasks, from inside ckpt_cleanup()
    pthread_barrier_wait(&exit_presuspend_barrier);
    in_presuspend = 0;
    // If we arrive here, primary user thread can also continue.
    // But primary user thread (in ckpt_cleanup()) then reaches resume_barrier.
    // After ckpt thread returns from here, PRECHECKPOINT event can happen next.
    break;

  case DMTCP_EVENT_PRECHECKPOINT:
    precheckpoint();
    break;

  case DMTCP_EVENT_RESUME:
    resume();
    pthread_barrier_wait(&resume_barrier);
    break;

  case DMTCP_EVENT_RESTART:
    resume();
    pthread_barrier_wait(&resume_barrier);
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
  "Presuspend plugin",
  presuspend_eventHook
};

DMTCP_DECL_PLUGIN(presuspend_plugin);
