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
/* NOT USED (if this were C++, next_fnc would require a full type signature) */
#define NEXT_FNC_CXX(symbol) \
  (next_fnc ? *next_fnc : \
   *(next_fnc = (__typeof__(next_fnc))dlsym(RTLD_NEXT, #symbol)))

unsigned int sleep(unsigned int seconds) {
  static unsigned int (*next_fnc)() = NULL; /* Same type signature as sleep */

  printf("(sleep2: "); print_time(); printf(" ... ");
  unsigned int result = NEXT_FNC(sleep)(seconds);
  print_time(); printf(") ");

  return result;
}

/* If your code wants to avoid the wrapper above, call this version */
unsigned int real_sleep(unsigned int seconds) {
  static unsigned int (*real_fnc)() = NULL; /* Same type signature as sleep */
  static void *handle = NULL;

  if (! handle)
    handle = dlopen("libc.so.6", RTLD_NOW);
  if (! real_fnc)
    real_fnc = (__typeof__(real_fnc)) dlsym(handle, "sleep");
  return (*real_fnc)(seconds);
}

void dmtcp_process_event(DmtcpEvent_t event, void* data)
{
  static void (*next_fnc)() = NULL;/* Same type signature as this fnc */

  /* NOTE:  See warning in module/README about calls to printf here. */
  switch (event) {
  case DMTCP_EVENT_PRE_CHECKPOINT:
    printf("*** The module %s is being called before checkpointing. ***\n",
	   __FILE__);
    real_sleep(1);
    printf("*** Finished calling real_sleep() for 1 second. ***\n");
    break;
  case DMTCP_EVENT_POST_CHECKPOINT:
    printf("*** The module %s has now been checkpointed. ***\n", __FILE__);
    break;
  }

  /* Call this next line in order to pass DMTCP events to later modules. */
  NEXT_FNC(dmtcp_process_event)(event, data);
}
