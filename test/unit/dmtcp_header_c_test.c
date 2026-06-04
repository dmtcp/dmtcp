#include "dmtcp.h"

#include <stddef.h>

int
dmtcp_header_c_compile_check(void)
{
  DmtcpCkptHeader header;

  dmtcp_init_ckpt_header_bootstrap(&header);
  return sizeof(header.ckptSignature) == 32 &&
         offsetof(DmtcpCkptHeader, ckptSignature) == 0 &&
         header.headerSize == sizeof(DmtcpCkptHeader) &&
         header.wordSize == sizeof(void *) ? 0 : 1;
}
