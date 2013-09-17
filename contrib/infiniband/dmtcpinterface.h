#ifndef DMTCPINTERFACE_H
# define DMTCPINTERFACE_H

# ifdef __cplusplus
  extern "C" {
# endif

typedef enum eDmtcpEvent {
  DMTCP_EVENT_INIT,
  DMTCP_EVENT_PRE_EXIT,
  DMTCP_EVENT_RESET_ON_FORK,
  DMTCP_EVENT_POST_SUSPEND,
  DMTCP_EVENT_POST_LEADER_ELECTION,
  DMTCP_EVENT_POST_DRAIN,
  DMTCP_EVENT_PRE_CHECKPOINT,
  DMTCP_EVENT_POST_CHECKPOINT,
  DMTCP_EVENT_REGISTER_NAME_SERVICE_DATA,
  DMTCP_EVENT_SEND_QUERIES,
  DMTCP_EVENT_POST_CHECKPOINT_RESUME,
  DMTCP_EVENT_POST_RESTART,
  DMTCP_EVENT_POST_RESTART_RESUME,
  nDmtcpEvents
} DmtcpEvent_t;

void process_dmtcp_event(DmtcpEvent_t event, void* data);
int  dmtcp_get_ckpt_signal();
const char* dmtcp_get_tmpdir();
const char* dmtcp_get_uniquepid_str();
int  dmtcp_is_running_state();

# ifdef __cplusplus
 }
# endif
#endif

