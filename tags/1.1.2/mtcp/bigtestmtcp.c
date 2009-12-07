
#include <unistd.h>
#include "mtcp.h"

double fill_bss[20000][20000];

int main () {
  mtcp_init ("testmtcp.mtcp", 3, 0);
  mtcp_ok ();

  while (1) {
     sleep (1);
  }
  return 0;
}

