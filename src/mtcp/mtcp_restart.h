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

// Read the current value of sp and bp/fp registers and subtract the
// stack_offset to compute the new sp and bp values. We have already copied
// all the bits from old stack to the new one and so any one referring to
// stack data using sp/bp should be fine.
// NOTE: changing the value of bp/fp register is optional and only useful for
// doing a return from this function or to access any local variables. Since
// we don't use any local variables from here on, we can ignore bp/fp
// registers.
// NOTE: 32-bit ARM doesn't have an fp register.

#if defined(__i386__) || defined(__x86_64__)
# define RELOCATE_STACK(addr)                                                 \
  asm volatile ("mfence" ::: "memory");                                       \
  asm volatile (CLEAN_FOR_64_BIT(sub %0, %%esp; )                             \
                CLEAN_FOR_64_BIT(sub %0, %%ebp; )                             \
                : : "r" (addr) : "memory");

#elif defined(__arm__)

# define RELOCATE_STACK(addr)                                                 \
  asm volatile ("sub sp, sp, %0" : : "r" (addr) : "memory");

#elif defined(__aarch64__)
  // Use x29 instead of fp because GCC's inline assembler does not recognize fp.
# define RELOCATE_STACK(addr)                                                 \
  asm volatile ("sub sp, sp, %0\n\t"                                          \
                "sub x29, x29, %0"                                            \
                : : "r" (addr) : "memory");

#else /* if defined(__i386__) || defined(__x86_64__) */

# error "assembly instruction not translated"

#endif /* if defined(__i386__) || defined(__x86_64__) */


#define RELOCATE_STACK_AND_JUMP_TO_RESTORE_FUNCTION(rinfoPtr)                 \
  /* Copy over old stack to new location; */                                  \
  mtcp_memcpy(rinfoPtr->new_stack_addr,                                       \
              rinfoPtr->old_stack_addr,                                       \
              rinfoPtr->old_stack_size);                                      \
                                                                              \
  DPRINTF("We have copied mtcp_restart to higher address.  We will now\n"     \
          "    jump into a copy of restorememoryareas().\n");                 \
                                                                              \
  RELOCATE_STACK(rinfoPtr->stack_offset);                                     \
                                                                              \
  /* IMPORTANT:  We just changed to a new stack.  The call frame for this     \
   * function on the old stack is no longer available.  The only way to pass  \
   * rinfo into the next function is by passing a pointer to a global variable\
   * We call restorememoryareas_fptr(), which points to the copy of the       \
   * function in higher memory.  We will be unmapping the original fnc.       \
   */                                                                         \
  rinfoPtr->restorememoryareas_fptr(rinfoPtr)

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
  fnptr_t restore_func;
  fnptr_t mtcp_restart_new_stack;

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

// int mtcp_restart_process_args(RestoreInfo *rinfo, int argc, char *argv[], char **environ);
void mtcp_restart_process_args(int argc, char *argv[], char **environ, void (*func)(RestoreInfo *));
void mtcp_restart_process_header(RestoreInfo *rinfoIn, MtcpHeader *mtcpHdr);
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
