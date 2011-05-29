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

unsigned int sleep(unsigned int seconds) {
  static unsigned int (*target)() = (__typeof__(unsigned int (*)())) 0x1;
  if (target == (void *)0x1)
    target = dlsym(RTLD_NEXT, "sleep");
  printf("(sleep2: "); print_time(); printf(" ... ");
  unsigned int result = (*target)(seconds);
  print_time(); printf(") ... ");
  return result;
}
void process_dmtcp_event(DmtcpEvent_t event, void* data)
{
  switch (event) {
  case DMTCP_EVENT_PRE_CHECKPOINT:
    printf("*** The module sleep2 is about to be checkpointed. ***\n");
    break;
  }
}
