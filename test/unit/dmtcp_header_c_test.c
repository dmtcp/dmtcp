#include "dmtcp.h"

#include <stddef.h>
#include <string.h>

int
dmtcp_header_c_compile_check(void)
{
  DmtcpCkptHeader header;

  memset(&header, 0, sizeof(header));
  strcpy(header.ckptSignature, DMTCP_CKPT_SIGNATURE);
  dmtcp_init_ckpt_header_bootstrap(&header);
  return sizeof(header.ckptSignature) == 32 &&
         offsetof(DmtcpCkptHeader, ckptSignature) == 0 &&
         header.headerSize == sizeof(DmtcpCkptHeader) &&
         header.wordSize == sizeof(void *) &&
         dmtcp_ckpt_header_has_valid_bootstrap(&header) ? 0 : 1;
}
