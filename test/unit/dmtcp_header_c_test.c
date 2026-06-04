#include "dmtcp.h"

#include <stddef.h>

int
dmtcp_header_c_compile_check(void)
{
  DmtcpCkptHeader header;

  return sizeof(header.ckptSignature) == 32 &&
         offsetof(DmtcpCkptHeader, ckptSignature) == 0 ? 0 : 1;
}
