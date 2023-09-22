#ifndef MTCP_RESTART_H
#define MTCP_RESTART_H

#include "procmapsarea.h"
#include "mtcp_header.h"
#include <linux/limits.h>

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

#define MAX_REGIONS_TO_MUNMAP 16

typedef struct MemRegion_t {
  VA startAddr;
  VA endAddr;
} MemRegion;

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

  // void (*post_restart)();
  // void (*restorememoryareas_fptr)();
  int use_gdb;
  VA mtcp_restart_text_addr;
#ifdef TIMING
  struct timeval startValue;
#endif
  volatile int restart_pause;  // Used by env. var. DMTCP_RESTART_PAUSE_WHILE

  // Set to the value of DMTCP_DEBUG_MTCP_RESTART env var.
  int skipMremap;

  // The following fields are only valid until mtcp_restart memory is unmapped,
  // and checkpoint image is mapped in.
  int argc;
  char **argv;
  char **environ;

  MemRegion regions_to_munmap[MAX_REGIONS_TO_MUNMAP];
  int num_regions_to_munmap;

  int simulate;
  int mpiMode;

  char ckptImage[PATH_MAX];
} RestoreInfo;

int mtcp_restart_process_args(RestoreInfo *rinfo, int argc, char *argv[], char **environ);
void mtcp_restart(RestoreInfo *rinfo, MtcpHeader *mtcpHdr);
void mtcp_simulateread(RestoreInfo *rinfo);
void mtcp_check_vdso(char **environ);
int mtcp_open_ckpt_image_and_read_header(RestoreInfo *rinfo, MtcpHeader *mtcpHdr);

// Usage: DMTCP_RESTART_PAUSE_WHILE(*&rinfo)->restart_pause == <LEVEL>);
#define DMTCP_RESTART_PAUSE_WHILE(condition)                                   \
  do {                                                                         \
    while (condition);                                                         \
  } while (0)

#endif // #ifndef MTCP_RESTART_H
