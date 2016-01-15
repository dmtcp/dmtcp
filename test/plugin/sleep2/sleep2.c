#include <stdio.h>
#include <sys/time.h>
#include "dmtcp.h"
#include "config.h"

void print_time() {
  struct timeval val;
  gettimeofday(&val, NULL);
  printf("%ld %ld", (long)val.tv_sec, (long)val.tv_usec);
}

unsigned int sleep(unsigned int seconds) {
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


static void checkpoint()
{
  printf("*** The plugin %s is being called before checkpointing. ***\n",
         __FILE__);
  real_sleep(1);
  printf("*** Finished calling real_sleep() for 1 second. ***\n");
}

static void resume()
{
  printf("*** The plugin %s has now been checkpointed. ***\n", __FILE__);
}

static DmtcpBarrier barriers[] = {
  {DMTCP_GLOBAL_BARRIER_PRE_CKPT, checkpoint, "checkpoint"},
  {DMTCP_GLOBAL_BARRIER_RESUME, resume, "resume"}
};

DmtcpPluginDescriptor_t sleep2_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "sleep2",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Sleep2 plugin",
  DMTCP_DECL_BARRIERS(barriers),
  NULL
};

DMTCP_DECL_PLUGIN(sleep2_plugin);
