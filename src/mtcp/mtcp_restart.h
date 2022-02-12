#ifndef MTCP_RESTART_H
#define MTCP_RESTART_H

#include "procmapsarea.h"

#define MB                 1024 * 1024
#define RESTORE_STACK_SIZE 16 * MB
#define RESTORE_MEM_SIZE   16 * MB
#define RESTORE_TOTAL_SIZE (RESTORE_STACK_SIZE + RESTORE_MEM_SIZE)

typedef void (*fnptr_t)();

typedef struct RestoreInfo {
  int fd;
  int stderr_fd;  /* FIXME:  This is never used. */

  // int mtcp_sys_errno;

  VA saved_brk;
  VA restore_addr;
  VA restore_end;
  size_t restore_size;
  VA vdsoStart;
  VA vdsoEnd;
  VA vvarStart;
  VA vvarEnd;
  VA endOfStack;
  fnptr_t post_restart;
  // NOTE: Update the offset when adding fields to the RestoreInfo struct
  // See note below in the restart_fast_path() function.
  fnptr_t restorememoryareas_fptr;

  VA old_stack_addr;
  size_t old_stack_size;
  VA new_stack_addr;
  size_t stack_offset;

  // void (*post_restart)();
  // void (*restorememoryareas_fptr)();
  int use_gdb;
  VA mtcp_restart_text_addr;
#ifdef TIMING
  struct timeval startValue;
#endif
  int restart_pause;  // Used by env. var. DMTCP_RESTART_PAUSE

  // The following fields are only valid until mtcp_restart memory is unmapped,
  // and checkpoint image is mapped in.
  int argc;
  char **argv;
  char **environ;
} RestoreInfo;

void mtcp_check_vdso(char **environ);

#define DMTCP_RESTART_PAUSE(rinfo, level)                                      \
  do {                                                                         \
    if ((rinfo)->restart_pause == (level)) {                                   \
      volatile int dummy = 1;                                                  \
      while (dummy);                                                           \
    }                                                                          \
  } while (0)

#endif // #ifndef MTCP_RESTART_H