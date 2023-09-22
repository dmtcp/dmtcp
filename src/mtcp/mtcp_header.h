#ifndef MTCP_HEADER_H
#define MTCP_HEADER_H

#include <stddef.h>

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
    void (*post_restart)(double, int);
  };

  char _padding[4096];
} MtcpHeader;
#endif // ifndef MTCP_HEADER_H
