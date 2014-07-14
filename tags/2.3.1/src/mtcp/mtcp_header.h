#ifndef MTCP_HEADER_H
#define MTCP_HEADER_H

#include "ldt.h"

#ifdef __i386__
typedef unsigned short segreg_t;
#elif __x86_64__
typedef unsigned short segreg_t;
#elif __arm__
typedef unsigned int segreg_t;
#endif

/* TLS segment registers used differently in i386 and x86_64. - Gene */
#ifdef __i386__
# define TLSSEGREG gs
#elif __x86_64__
# define TLSSEGREG fs
#elif __arm__
/* FIXME: fs IS NOT AN arm REGISTER.  BUT THIS IS USED ONLY AS A FIELD NAME.
 *   ARM uses a register in coprocessor 15 as the thread-pointer (TLS Register)
 */
# define TLSSEGREG fs
#endif

#ifdef __x86_64__
# define MYINFO_GS_T unsigned long int
#else
# define MYINFO_GS_T unsigned int
#endif

typedef struct _ThreadTLSInfo {
  segreg_t fs, gs;  // thread local storage pointers
  struct user_desc gdtentrytls[1];
} ThreadTLSInfo;

#define MTCP_SIGNATURE "MTCP_HEADER_v2.2\n"
#define MTCP_SIGNATURE_LEN 32
typedef union _MtcpHeader {
  struct {
    char signature[MTCP_SIGNATURE_LEN];
    void *saved_brk;
    void *restore_addr;
    size_t restore_size;
    void (*post_restart) ();
    ThreadTLSInfo motherofall_tls_info;
    int tls_pid_offset;
    int tls_tid_offset;
    MYINFO_GS_T myinfo_gs;
  };

  char _padding[4096];
} MtcpHeader;

#endif
