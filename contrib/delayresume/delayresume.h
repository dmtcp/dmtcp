#ifndef _DELAYRESUME_H_
#define _DELAYRESUME_H_

#include "dmtcp.h"

/*
 * Invoked by a user thread to initiate a checkpoint
 *
 * Returns: The result of dmtcp_checkpoint()
 *
 * Side-effects: Sets the global canResume variable to false; the current
 * user thread is designated as the ckpt-initiator; other user threads will
 * not resume until the dmtcp_checkpoint_unblock_resume() is called by the
 * current thread.
 */
EXTERNC int dmtcp_checkpoint_block_resume() __attribute__ ((weak));
#define dmtcp_checkpoint_block_resume() \
  (dmtcp_checkpoint_block_resume ? dmtcp_checkpoint_block_resume() : DMTCP_NOT_PRESENT)

/*
 * Invoked by the checkpoint initiator user thread to unblock other user threads
 *
 * Returns: void
 *
 * Side-effects: The current thread looses its ckpt-initiator designation
 */
EXTERNC void dmtcp_checkpoint_unblock_resume() __attribute__ ((weak));
#define dmtcp_checkpoint_unblock_resume() \
  (dmtcp_checkpoint_unblock_resume ? dmtcp_checkpoint_unblock_resume() : DMTCP_NOT_PRESENT)

#endif /* _DELAYRESUME_H_ */
