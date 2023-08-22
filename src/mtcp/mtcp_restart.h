#ifndef MTCP_RESTART_H
#define MTCP_RESTART_H

#include "procmapsarea.h"
#include <linux/limits.h>

#ifdef MTCP_PLUGIN_H
#include MTCP_PLUGIN_H
#else
#define PluginInfo char
#define mtcp_plugin_hook(args)
#define mtcp_plugin_skip_memory_region_munmap(area, rinfo) 0
#endif

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define MB                 1024 * 1024
#define RESTORE_STACK_SIZE 16 * MB
#define RESTORE_MEM_SIZE   16 * MB
#define RESTORE_TOTAL_SIZE (RESTORE_STACK_SIZE + RESTORE_MEM_SIZE)

/* The use of NO_OPTIMIZE is deprecated and will be removed, since we
 * compile mtcp_restart.c with the -O0 flag already.
 */
#ifdef __clang__
# define NO_OPTIMIZE __attribute__((optnone)) /* Supported only in late 2014 */
#else /* ifdef __clang__ */
# define NO_OPTIMIZE __attribute__((optimize(0)))
#endif /* ifdef __clang__ */

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

  // VDSO/VVAR regions for the mtcp_restart program.
  VA currentVdsoStart;
  VA currentVdsoEnd;
  VA currentVvarStart;
  VA currentVvarEnd;

  VA old_stack_addr;
  size_t old_stack_size;
  VA new_stack_addr;
  size_t stack_offset;

  VA minLibsStart;
  VA maxLibsEnd;
  VA minHighMemStart;
  VA maxHighMemEnd;
  char* restartDir;

  // void (*post_restart)();
  // void (*restorememoryareas_fptr)();
  int use_gdb;
  VA mtcp_restart_text_addr;
#ifdef TIMING
  struct timeval startValue;
#endif
  int restart_pause;  // Used by env. var. DMTCP_RESTART_PAUSE

  // Set to the value of DMTCP_DEBUG_MTCP_RESTART env var.
  int skipMremap;

  // The following fields are only valid until mtcp_restart memory is unmapped,
  // and checkpoint image is mapped in.
  int argc;
  char **argv;
  char **environ;

  PluginInfo pluginInfo;

  char ckptImage[PATH_MAX];
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
