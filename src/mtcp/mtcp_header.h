#ifndef MTCP_HEADER_H
#define MTCP_HEADER_H

#include "ldt.h"

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
    void *restore_addr;
    size_t restore_size;
    void *vdsoStart;
    void *vdsoEnd;
    void *vvarStart;
    void *vvarEnd;
    void (*post_restart)(double);
    void (*post_restart_debug)(double, int);
    ThreadTLSInfo motherofall_tls_info;
    int tls_pid_offset;
    int tls_tid_offset;
    MYINFO_GS_T myinfo_gs;
  };

  char _padding[4096];
} MtcpHeader;
#endif // ifndef MTCP_HEADER_H
