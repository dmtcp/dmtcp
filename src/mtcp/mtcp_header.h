#ifndef MTCP_HEADER_H
#define MTCP_HEADER_H

#include <stddef.h>
#include "../../include/dmtcp.h"

typedef DmtcpCkptHeader MtcpHeader;
// typedef union _MtcpHeader {
//   struct {
//     char signature[MTCP_SIGNATURE_LEN];
//     void *saved_brk;
//     void *end_of_stack;
//     void *restore_addr;
//     size_t restore_size;
//     void *vdsoStart;
//     void *vdsoEnd;
//     void *vvarStart;
//     void *vvarEnd;
//     void (*post_restart)(double, int);
//   };
//
//   char _padding[4096];
// } MtcpHeader;
#endif // ifndef MTCP_HEADER_H
