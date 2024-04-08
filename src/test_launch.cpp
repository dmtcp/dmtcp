#include "../include/dmtcp.h"
#include <unistd.h>

using namespace dmtcp;

int
main(int argc, const char **argv)
{
  DmtcpInfo info = dmtcp_register_new_process(&argc, &argv);

  execvp(info.argv[0], (char* const*) info.argv);

  perror("Exec failed.\n");

  return -1;
}
