/* NOTE:  if you just want to insert your own code at the time of checkpoint
 *  and restart, there are two simpler additional mechanisms:
 *  dmtcpaware, and the MTCP special hook functions:
 *    mtcpHookPreCheckpoint, mtcpHookPostCheckpoint, mtcpHookRestart
 */

#include <stdio.h>
#include <sys/time.h>
#define __USE_GNU
#include <dlfcn.h>
#include "dmtcpmodule.h"

void print_time() {
  struct timeval val;
  gettimeofday(&val, NULL);
  printf("%ld %ld", (long)val.tv_sec, (long)val.tv_usec);
}

/* This macro requires a static local declaration of "next_fnc". */
#define NEXT_FNC(symbol) \
  (next_fnc ? *next_fnc : *(next_fnc = dlsym(RTLD_NEXT, #symbol)))

unsigned int sleep(unsigned int seconds) {
  static unsigned int (*next_fnc)() = NULL; /* Same type signature as sleep */

  printf("sleep1: "); print_time(); printf(" ... ");
  unsigned int result = NEXT_FNC(sleep)(seconds);
  print_time(); printf("\n");

  return result;
}

void process_dmtcp_event(DmtcpEvent_t event, void* data)
{
  static void (*next_fnc)() = NULL;/* Same type signature as this fnc */

  /* NOTE:  See warning in module/README about calls to printf here. */
  switch (event) {
  case DMTCP_EVENT_PRE_CHECKPOINT:
    printf("\n*** The module %s is being called before checkpointing. ***\n",
	   __FILE__);
    break;
  case DMTCP_EVENT_POST_CHECKPOINT:
    printf("*** The module %s has now been checkpointed. ***\n", __FILE__);
    break;
  }

  /* Call this next line in order to pass DMTCP events to later modules. */
  NEXT_FNC(process_dmtcp_event)(event, data);
}
