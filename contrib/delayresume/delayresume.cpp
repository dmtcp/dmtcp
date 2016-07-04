#include "jassert.h"
#include "dmtcp.h"
#include "delayresume.h"

#undef dmtcp_checkpoint_block_resume
#undef dmtcp_checkpoint_unblock_resume

/* Set to true for the user thread that invokes a checkpoint */
__thread bool ckptInitiator = false;

/*
 * Global lock; direct accesses outside of block/unblock functions (below)
 * should be avoided to prevent data races
 */
bool canResume = true;

/*
 * Invoked by the user thread that initiated the checkpoint
 * to acquire the global canResume lock
 *
 * Side-effects: Sets the global canResume variable to false
 *
 */
static void
blockUserThreads()
{
  JASSERT(ckptInitiator)
         .Text("Block called by a thread that didn't initiate checkpointing");
  JTRACE("Blocking all user threads")(ckptInitiator);
  while(!__sync_bool_compare_and_swap(&canResume, true, false));
  JTRACE("Blocked all user threads")(ckptInitiator);
}

/*
 * Invoked by the user thread that initiated the checkpoint
 * to let the other user threads resume.
 *
 * Side-effects: Sets the global canResume variable to true
 *
 */
static void
unblockUserThreads()
{
  JASSERT(ckptInitiator)
         .Text("Unblock called by a thread that didn't initiate checkpointing");
  JTRACE("Unblocking all user threads")(ckptInitiator);
  while(!__sync_bool_compare_and_swap(&canResume, false, true));
  JTRACE("Unblocked all user threads")(ckptInitiator);
}

/*
 * Invoked by each user thread that didn't initiate the checkpoint
 *
 * Side-effects: None
 */
static void
waitUntilResumeCb()
{
  JASSERT(!ckptInitiator)
         .Text("Ckpt initiator cannot block!");
  JTRACE("Waiting for resume msg from ckpt initiator")(ckptInitiator);
  while(__sync_bool_compare_and_swap(&canResume, false, false));
  JTRACE("Resumed user thread")(ckptInitiator);
}

/*
 * Invoked by a user thread to initiate a checkpoint
 *
 * Side-effects: Sets the global canResume variable to false; the current
 * user thread is designated as the ckpt-initiator; other user threads will
 * not resume until the dmtcp_checkpoint_unblock_resume() is called by the
 * current thread.
 */
EXTERNC int
dmtcp_checkpoint_block_resume()
{
  int ret = DMTCP_NOT_PRESENT;
  ckptInitiator = true;
  blockUserThreads();
  ret = dmtcp_checkpoint();
  if (ret == DMTCP_NOT_PRESENT || ret == DMTCP_AFTER_RESTART) {
    unblockUserThreads();
    ckptInitiator = false;
  }
  return ret;
}

/*
 * Invoked by the checkpoint initiator user thread to unblock other user threads
 *
 * Side-effects: The current thread looses its ckpt-initiator designation
 */
EXTERNC void
dmtcp_checkpoint_unblock_resume()
{
  JASSERT(ckptInitiator)
         .Text("Unblock called by a thread that didn't initiate checkpointing");
  unblockUserThreads();
  ckptInitiator = false;
}

EXTERNC void
dmtcp_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
    case DMTCP_EVENT_RESUME_USER_THREAD:
      if (!ckptInitiator) {
        waitUntilResumeCb();
      }
      break;

    default:
      break;
  }

  DMTCP_NEXT_EVENT_HOOK(event, data);
  return;
}
