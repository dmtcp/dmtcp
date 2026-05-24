#ifndef PIDHELPERS_H
#define PIDHELPERS_H

#include <signal.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "dmtcp.h"

#ifdef __cplusplus
extern "C"
{
#endif

int dmtcp_pid_is_enabled();
pid_t dmtcp_pid_gettid();
int dmtcp_tkill(int tid, int sig);
int dmtcp_tgkill(int tgid, int tid, int sig);
LIB_PRIVATE int dmtcp_pid_on_waitid_syscall(idtype_t idtype,
                                            id_t id,
                                            siginfo_t *infop,
                                            int options,
                                            struct rusage *rusage);
void dmtcp_pid_update_mapping(pid_t virtualPid, pid_t realPid)
  __attribute__((weak));

#ifdef __cplusplus
}
#endif

#endif
