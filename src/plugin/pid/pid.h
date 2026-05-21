#ifndef PID_H
#define PID_H

#include "dmtcp.h"

#ifdef __cplusplus
extern "C" {
#endif

pid_t dmtcp_pid_get_virtual_tid(void) __attribute((weak));
void dmtcp_pid_erase_virtual_pid(pid_t virtualPid) __attribute((weak));

#ifdef __cplusplus
}
#endif

#endif // ifndef PID_H
