#ifndef PTRACE_H
#define PTRACE_H

#ifdef PTRACE
#include "syscallwrappers.h"
#include "mtcpinterface.h"

void initializeMtcpPtraceEngine();
void ptraceProcessCloneStartFn();
void ptraceCallbackPreCheckpoint();

extern "C" {
  typedef void (*mtcp_set_ptrace_callbacks_t)
    (struct ptrace_info (*get_next_ptrace_info)(int index),
     void (*ptrace_info_list_command)(struct cmd_info cmd),
     void (*jalib_ckpt_unlock)(),
     int  (*ptrace_info_list_size)());

  typedef struct ptrace_waitpid_info (*mtcp_get_ptrace_waitpid_info_t)();
  typedef void (*mtcp_init_thread_local_t)();
  typedef int (*mtcp_is_ptracing_t)();
  typedef void (*mtcp_init_ptrace_t)(const char *tmp_dir);


  typedef struct MtcpPtraceFuncPtrs {
    mtcp_set_ptrace_callbacks_t set_ptrace_callbacks;
    mtcp_get_ptrace_waitpid_info_t get_ptrace_waitpid_info;
    mtcp_init_ptrace_t init_ptrace;
    mtcp_init_thread_local_t init_thread_local;
    mtcp_is_ptracing_t is_ptracing;
  } MtcpPtraceFuncPtrs_t;
  extern sigset_t signals_set;
}

extern MtcpPtraceFuncPtrs_t mtcpPtraceFuncPtrs;

#endif

#endif

