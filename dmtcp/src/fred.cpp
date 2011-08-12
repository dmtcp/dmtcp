#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <linux/version.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <dlfcn.h>

#include "constants.h"
#include "syscallwrappers.h"
#include "dmtcpmodule.h"
#include "../jalib/jfilesystem.h"

#ifdef RECORD_REPLAY
//#include "fred_wrappers.h"
#include "synchronizationlogging.h"
#include "log.h"

static inline void memfence() {  asm volatile ("mfence" ::: "memory"); }
void fred_setup_trampolines();

static void recordReplayInit()
{
  // As of rev. 816, this line caused DMTCP with standard ./configure
  //   (no command line flags) to segfault.
  // To see bug, do:  gdb --args bin/dmtcp_checkpoint ls
  // NOTe: This comment may not be true anymore.
  fred_setup_trampolines();

  /* This is called only on exec(). We reset the global clone counter for this
     process, assign the first thread (this one) clone_id 1, and increment the
     counter. */
  JTRACE ( "resetting global clone counter." );
  global_clone_counter = GLOBAL_CLONE_COUNTER_INIT;
  my_clone_id = global_clone_counter;
  global_clone_counter++;

  my_log = new dmtcp::SynchronizationLog();
  clone_id_to_tid_table.clear();
  clone_id_to_log_table.clear();
  tid_to_clone_id_table.clear();
  clone_id_to_tid_table[my_clone_id] = pthread_self();
  clone_id_to_log_table[my_clone_id] = my_log;

  /* Other initialization for sync log/replay specific to this process. */
  initializeLogNames();
  if (getenv(ENV_VAR_LOG_REPLAY) == NULL) {
    /* If it is NULL, this is the very first exec. We unset => set to 0
       (meaning no logging, no replay) */
    // FIXME: setenv is known to cause issues when interacting with bash.
    setenv(ENV_VAR_LOG_REPLAY, "0", 1);
  }
  sync_logging_branch = atoi(getenv(ENV_VAR_LOG_REPLAY));
  /* Synchronize this constructor, if this is not the very first exec. */
  log_entry_t my_entry = create_exec_barrier_entry();
  if (SYNC_IS_REPLAY) {
    memfence();
    waitForExecBarrier();
    getNextLogEntry();
  } else if (SYNC_IS_RECORD) {
    addNextLogEntry(my_entry);
  }
}

/* This code used to be called from preCheckpoint hooks. However, in order to
 * modularize record/replay code, we want this to be separate from any hooks.
 * As a result, this code has moved later in the code where we are processing
 * during the POST_SUSPEND state.
 * This move has to make sure that the events that can happen between
 * preCheckpointHook() and dmtcp_process_event(DMTCP_EVENT_POST_SUSPEND) do not
 * cause any memory allocations/deallocations or any other calls that could
 * result in a synchronization event that should otherwise be logged.
 */
void fred_post_suspend ()
{
  //printf("fredhijack preckpt*****\n\n");

  if (SYNC_IS_REPLAY) {
    /* Checkpointing during replay -- we will truncate the log to the current
       position, and begin recording again. */
    truncate_all_logs();
  }
  set_sync_mode(SYNC_NOOP);
  log_all_allocs = 0;
  // Write the logs to disk, if any are in memory, and unmap them.
  close_all_logs();
  // Remove the threads which aren't alive anymore.
  {
    dmtcp::map<clone_id_t, pthread_t>::iterator it;
    dmtcp::vector<clone_id_t> stale_clone_ids;
    for (it = clone_id_to_tid_table.begin();
         it != clone_id_to_tid_table.end();
         it++) {
      if (_real_pthread_kill(it->second, 0) != 0) {
        stale_clone_ids.push_back(it->first);
      }
    }
    for (size_t i = 0; i < stale_clone_ids.size(); i++) {
      clone_id_to_tid_table.erase(stale_clone_ids[i]);
      clone_id_to_log_table.erase(stale_clone_ids[i]);
    }
  }
}

void fred_post_checkpoint_resume()
{
  recordDataStackLocations();
  set_sync_mode(SYNC_RECORD);
  initLogsForRecordReplay();
  log_all_allocs = 1;
}

void fred_post_restart_resume()
{
  recordDataStackLocations();
  set_sync_mode(SYNC_REPLAY);
  initLogsForRecordReplay();
  log_all_allocs = 1;
}

void fred_reset_on_fork()
{
  // This is called only on fork() by the new child process. We reset the
  // global clone counter for this process, assign the first thread (this one)
  // clone_id 1, and increment the counter.
  _real_pthread_mutex_lock(&global_clone_counter_mutex);
  JTRACE ( "resetting global counter in new process." );
  global_clone_counter = GLOBAL_CLONE_COUNTER_INIT;
  my_clone_id = global_clone_counter;
  global_clone_counter++;
  _real_pthread_mutex_unlock(&global_clone_counter_mutex);

  // Perform other initialization for sync log/replay specific to this process.
  recordDataStackLocations();
  initializeLogNames();
}

EXTERNC void fred_process_dmtcp_event(DmtcpEvent_t event, void* data)
{
  switch (event) {
    case DMTCP_EVENT_INIT:
      recordReplayInit();
      break;
    case DMTCP_EVENT_RESET_ON_FORK:
      fred_reset_on_fork();
      break;
    case DMTCP_EVENT_POST_SUSPEND:
      fred_post_suspend();
      break;

    case DMTCP_EVENT_POST_CHECKPOINT_RESUME:
      fred_post_checkpoint_resume();
      break;
    case DMTCP_EVENT_POST_RESTART_RESUME:
      fred_post_restart_resume();
      break;

    case DMTCP_EVENT_PRE_EXIT:
    case DMTCP_EVENT_PRE_CHECKPOINT:
    case DMTCP_EVENT_POST_LEADER_ELECTION:
    case DMTCP_EVENT_POST_DRAIN:
    case DMTCP_EVENT_POST_CHECKPOINT:
    case DMTCP_EVENT_POST_RESTART:
    default:
      break;
  }

  DMTCP_CALL_NEXT_PROCESS_DMTCP_EVENT(event, data);
  return;
}


#if 1

//FIXME: Do we need these definitions?
int __real_dmtcp_userSynchronizedEvent()
{
  userSynchronizedEvent();
  return 1;
}
EXTERNC int _real_dmtcp_userSynchronizedEventBegin()
{
  userSynchronizedEventBegin();
  return 1;
}
EXTERNC int _real_dmtcp_userSynchronizedEventEnd()
{
  userSynchronizedEventEnd();
  return 1;
}

EXTERNC int dmtcp_userSynchronizedEvent()
{
  return __real_dmtcp_userSynchronizedEvent();
}
EXTERNC int dmtcp_userSynchronizedEventBegin()
{
  _real_dmtcp_userSynchronizedEventBegin();
  return 1;
}
EXTERNC int dmtcp_userSynchronizedEventEnd()
{
  _real_dmtcp_userSynchronizedEventEnd();
  return 1;
}

//These dummy trampolines support static linking of user code to libdmtcpaware.a
//See dmtcpaware.c .
//FIXME: Are these needed anymore?
EXTERNC int __dyn_dmtcp_userSynchronizedEvent()
{
  return __real_dmtcp_userSynchronizedEvent();
}

EXTERNC int __dyn_dmtcp_userSynchronizedEventBegin()
{
  return dmtcp_userSynchronizedEventBegin();
}

EXTERNC int __dyn_dmtcp_userSynchronizedEventEnd()
{
  return dmtcp_userSynchronizedEventEnd();
}
#endif

#endif
