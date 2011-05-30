
#include <unistd.h>
#include "mtcp.h"

/* Test on a data segment > 2GB 
 * GCC won't compile if the single array is of size >2GB so we split it
 */
double fill_bss[10000][15000];
double fill_bss2[10000][15000];

int main () {
  mtcp_init ("testmtcp.mtcp", 3, 0);
  mtcp_ok ();

  while (1) {
     sleep (1);
  }
  return 0;
}

