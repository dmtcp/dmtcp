#ifndef MTCP_HEADER_H
#define MTCP_HEADER_H

#define MTCP_SIGNATURE "MTCP_HEADER_v2.2\n"
#define MTCP_SIGNATURE_LEN 32
typedef union _MtcpHeader {
  struct {
    char signature[MTCP_SIGNATURE_LEN];
    void *saved_brk;
    void *restore_addr;
    size_t restore_size;
    void (*post_restart) ();
  };

  char _padding[4096];
} MtcpHeader;

#endif
