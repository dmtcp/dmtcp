#ifndef PTRACE_H
#define PTRACE_H

#include "constants.h"

#ifdef PTRACE
#include "syscallwrappers.h"

#ifndef EXTERNC
# ifdef __cplusplus
#  define EXTERNC extern "C"
# else
#  define EXTERNC
# endif
#endif

void ptraceProcessCloneStartFn();
void ptraceCallbackPreCheckpoint();
EXTERNC struct ptrace_info get_next_ptrace_info(int index);
EXTERNC sigset_t signals_set;
EXTERNC void jalib_ckpt_unlock();

#endif

#endif

