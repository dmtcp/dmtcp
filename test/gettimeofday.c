/********************************************************************************************************************************/

/*

















                                                                             */
/*  Simple single-threaded test program





                                                                            */
/*  Uses gettimeofday(), which appears in vdso page in Linux 2.6.25
                                                               */
/*  Putting NO_GETTIMEOFDAY in environment stops call to gettimeofday()
                                                           */
/*

















                                                                             */
/*  Print gettimeofday










                                                                              */
/*

















                                                                             */
/********************************************************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

int
main()
{
  while (1) {
    struct timeval tv;
    if (!getenv("NO_GETTIMEOFDAY")) {
      gettimeofday(&tv, NULL);
    }
    printf("TIME (gettimeofday): %d\n", (int)tv.tv_sec);
    sleep(2);
  }
  return 0;
}
