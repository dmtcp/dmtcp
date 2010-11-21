#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

/********************************************************************************************************************************/
/*                                                                                                                              */
/*  Simple single-threaded test program                                                                                         */
/*  Uses gettimeofday(), which appears in vdso page in Linux 2.6.15                                                             */
/*  Putting NO_GETTIMEOFDAY in environment stops call to gettimeofday()                                                         */
/*  Checkpoint is written to tmp.mtcp every 4 seconds                                                                           */
/*                                                                                                                              */
/*  Print gettimeofday                                                                                                          */
/*                                                                                                                              */
/********************************************************************************************************************************/

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "mtcp.h"

/* Compile with  -Wl,--export-dynamic to make these functions visible. */
void mtcpHookPreCheckpoint() {
  printf("\n%s: %s: about to checkpoint\n", __FILE__, __func__);
}
void mtcpHookPostCheckpoint() {
  printf("\n%s: %s: done checkpointing\n", __FILE__, __func__);
}
void mtcpHookRestart() {
  printf("\n%s: %s: restarting\n", __FILE__, __func__);
}

int main ()

{
  mtcp_init ("testgettimeofday.mtcp", 4, 0);
  mtcp_ok ();

  while(1) {
   struct timeval tv;

    if (! getenv("NO_GETTIMEOFDAY"))
      gettimeofday(&tv, NULL);
    printf("TIME (gettimeofday): %d\n",   tv.tv_sec);
    sleep(2);
  }
  return (0);
}
