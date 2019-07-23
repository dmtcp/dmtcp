#include <time.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/resource.h>

int CUR_LIMIT = -1;

void recursive_fnc2(char x) {
  // In one second, this will force 32 MB, and Linux default stack size is 8 MB
  struct timespec time = {0, 2<<27}; // 1/8 seconds
  nanosleep(&time, NULL);
  printf("."); fflush(stdout);

  // Restore rlim_cur on restart.
  // FIXME:  DMTCP should restore rlim_cur automatically, without this code.
  //   Instead, we rely on hack: ckpt usually happening during the 'nanosleep'
  struct rlimit stack_limit = {0, 0};
  getrlimit(RLIMIT_STACK, &stack_limit);
  stack_limit.rlim_cur = CUR_LIMIT;
  setrlimit(RLIMIT_STACK, &stack_limit);

  // Could use alloca(), but it's not POSIX.
  char extra_stack_memory[2<<20]; // 1 MB
  extra_stack_memory[0] = x;
  if (extra_stack_memory[0] != '\0') {
    return; // avoid -Wunused-but-set-variable
  }

  recursive_fnc2(x);
}

void recursive_fnc1(char x) {
  char extra_stack_memory[2<<22]; // 4 MB
  extra_stack_memory[0] = '\0';
  if (extra_stack_memory[0] != '\0') {
    while (1); // just loop; rlim_max not large enough for valid test.
  }
  recursive_fnc2(x);
}

int main() {
  struct rlimit stack_limit = {0, 0};
  getrlimit(RLIMIT_STACK, &stack_limit);
  printf("RLIMIT_STACK(soft,hard):   %u, %u\n",
         (unsigned)stack_limit.rlim_cur, (unsigned)stack_limit.rlim_max);
  if (stack_limit.rlim_max >= stack_limit.rlim_cur * 16) {
    // Default stack size is 8 MB; so, we set it to 128 MB
    // It will exceed 8 MB in less than a second, and exceed stack limit in 16 s
    stack_limit.rlim_cur *= 16;
    CUR_LIMIT = stack_limit.rlim_cur;
    int rc = setrlimit(RLIMIT_STACK, &stack_limit);
    if (rc == -1) {
      perror("setrlimit");
      while (1);
    }
    printf("Setting rlim_cur for RLIMIT_STACK to %u.\n", 2<<26);
  } else {
    printf("Can't test MAP_STACKGROWSDOWN for '[stack]'.\n");
    while (1); // just loop; rlim_max not large enough for valid test.
  }

  recursive_fnc1('\0');
  return 0;
}
