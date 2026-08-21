// Regression target for a TSAN+DMTCP deadlock: 16 threads continuously
// malloc()/free() with no throttling, making it near-certain some thread
// holds TSAN's own allocator lock when DMTCP's checkpoint signal suspends
// it. A DMTCP_EVENT_PRECHECKPOINT plugin hook then calls malloc()/free()
// itself from the checkpoint thread, forcing a collision with that same
// lock. Plugin registration is a direct call in main(), not a dlsym()'d
// symbol; no special link flags (e.g. -rdynamic) are needed.
//   gcc -fsanitize=thread -g -O0 -pthread tsan_target_deadlock_hook.c -o tsan_target_deadlock_hook
//   setarch -R ./tsan_target_deadlock_hook          # ASLR off: TSAN requires it
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "dmtcp.h"

#define NUM_WORKERS 16

static void *thrash_allocator(void *arg) {
  while (1) {
    void *p = malloc(1024);
    free(p);
  }
  return NULL;
}

static void deadlock_hook(DmtcpEvent_t event, DmtcpEventData_t *data) {
  if (event == DMTCP_EVENT_PRECHECKPOINT) {
    printf("[DMTCP Hook] Checkpoint triggered. Forcing TSAN lock "
           "acquisition...\n");
    fflush(stdout);
    void *poison = malloc(16);
    free(poison);
  }
}

static DmtcpPluginDescriptor_t deadlock_hook_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  DMTCP_PACKAGE_VERSION,
  "deadlock-hook",
  "test",
  "test@test",
  "Forces a TSAN allocator-lock collision at PRECHECKPOINT",
  deadlock_hook
};

int main(void) {
  dmtcp_register_plugin(deadlock_hook_plugin);

  printf("[*] Starting guaranteed deadlock test...\n");
  fflush(stdout);

  pthread_t workers[NUM_WORKERS];
  for (int i = 0; i < NUM_WORKERS; i++) {
    pthread_create(&workers[i], NULL, thrash_allocator, NULL);
  }
  for (int i = 0; i < NUM_WORKERS; i++) {
    pthread_join(workers[i], NULL);
  }
  return 0;
}
