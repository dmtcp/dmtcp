#ifndef MTCP_HEADER_H
#define MTCP_HEADER_H

#include "ldt.h"
#include "procmapsarea.h"

#ifdef __i386__
typedef unsigned short segreg_t;
#elif __x86_64__
typedef unsigned short segreg_t;
#elif __arm__
typedef unsigned int segreg_t;
#elif defined(__aarch64__)
typedef unsigned long int segreg_t;
#endif // ifdef __i386__

/* TLS segment registers used differently in i386 and x86_64. - Gene */
#ifdef __i386__
# define TLSSEGREG gs
#elif __x86_64__
# define TLSSEGREG fs
#elif (__arm__ || __aarch64__)

/* FIXME: fs IS NOT AN arm REGISTER.  BUT THIS IS USED ONLY AS A FIELD NAME.
 *   ARM uses a register in coprocessor 15 as the thread-pointer (TLS Register)
 */
# define TLSSEGREG fs
#endif // ifdef __i386__

#if defined(__x86_64__) || defined(__aarch64__)
# define MYINFO_GS_T unsigned long int
#else // if defined(__x86_64__) || defined(__aarch64__)
# define MYINFO_GS_T unsigned int
#endif // if defined(__x86_64__) || defined(__aarch64__)

typedef struct _ThreadTLSInfo {
  segreg_t fs, gs;  // thread local storage pointers
  struct user_desc gdtentrytls[1];
} ThreadTLSInfo;

#define MTCP_SIGNATURE     "MTCP_HEADER_v2.2\n"
#define MTCP_SIGNATURE_LEN 32
typedef union _MtcpHeader {
  struct {
    char signature[MTCP_SIGNATURE_LEN];
    void *saved_brk;
    void *end_of_stack;
    void *restore_addr;
    size_t restore_size;
    void *vdsoStart;
    void *vdsoEnd;
    void *vvarStart;
    void *vvarEnd;
    void *libsStart;   // used by MPI: start of where kernel mmap's libraries
    void *libsEnd;     // used by MPI: end of where kernel mmap's libraries
    void *highMemStart;// used by MPI: start of memory beyond libraries
    void (*post_restart)(double);
    void (*post_restart_debug)(double, int);
    ThreadTLSInfo motherofall_tls_info;
    int tls_pid_offset;
    int tls_tid_offset;
    MYINFO_GS_T myinfo_gs;
  };

  char _padding[4096];
} MtcpHeader;

typedef void (*fnptr_t)();
typedef struct RestoreInfo {
  int fd;
  int stderr_fd;  /* FIXME:  This is never used. */

  // int mtcp_sys_errno;
  VA text_addr;
  size_t text_size;
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
  fnptr_t post_restart_debug;
  // NOTE: Update the offset when adding fields to the RestoreInfo struct
  // See note below in the restart_fast_path() function.
  fnptr_t restorememoryareas_fptr;

  // void (*post_restart)();
  // void (*post_restart_debug)();
  // void (*restorememoryareas_fptr)();
  int use_gdb;
  int text_offset;
  ThreadTLSInfo motherofall_tls_info;
  int tls_pid_offset;
  int tls_tid_offset;
#ifdef TIMING
  struct timeval startValue;
#endif
  MYINFO_GS_T myinfo_gs;
  int mtcp_restart_pause;  // Used by env. var. DMTCP_RESTART_PAUSE0
  const char *restart_dir; // Directory to search for checkpoint files
} RestoreInfo;

extern RestoreInfo rinfo;

#endif // ifndef MTCP_HEADER_H
