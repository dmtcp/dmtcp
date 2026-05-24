#ifndef PIDHELPERS_H
#define PIDHELPERS_H

#include <signal.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/types.h>

#include "dmtcp.h"

#ifdef __cplusplus
extern "C"
{
#endif

int dmtcp_pid_is_enabled();
pid_t dmtcp_pid_gettid();
int dmtcp_tkill(int tid, int sig);
int dmtcp_tgkill(int tgid, int tid, int sig);
pid_t dmtcp_pid_virtual_to_real(pid_t virtualPid);
pid_t dmtcp_pid_real_to_virtual(pid_t realPid);
void dmtcp_pid_update_mapping(pid_t virtualPid, pid_t realPid)
  __attribute__((weak));
// Fork helpers are exposed because fork combines core lifecycle mechanics with
// PID virtual mapping updates. Ordinary pid translation must use
// dmtcp_pid_virtual_to_real() and dmtcp_pid_real_to_virtual().
void dmtcp_pid_on_fork_prepare();
pid_t dmtcp_pid_on_fork_parent(pid_t realPid);
void dmtcp_pid_on_fork_child();

#ifdef __cplusplus
}
#endif

#endif
