#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>

int main() {
  struct rlimit rlim_nofile = {0, 0};
  struct rlimit rlim_stack = {0, 0};
  rlim_t rlim_cur_nofile = 0;
  rlim_t rlim_cur_stack = 0;

  getrlimit(RLIMIT_NOFILE, &rlim_nofile);
  getrlimit(RLIMIT_STACK, &rlim_stack);
  rlim_cur_nofile = rlim_nofile.rlim_cur;
  rlim_cur_stack = rlim_stack.rlim_cur;
  // Reduce resource limits by a small amount.
  // rlim_nofile.rlim_cur -= 10;
  rlim_nofile.rlim_cur = rlim_nofile.rlim_max - 10;
  rlim_stack.rlim_cur += (2 << 20);
  setrlimit(RLIMIT_NOFILE, &rlim_nofile);
  setrlimit(RLIMIT_STACK, &rlim_stack);

  struct timespec eighth_second = {0, 2<<26};
  int i;
  for (i = 0; ; i++) {
    getrlimit(RLIMIT_NOFILE, &rlim_nofile);
    getrlimit(RLIMIT_STACK, &rlim_stack);
    // Were the current values restored to the original values after restart?
    if ((rlim_nofile.rlim_cur == rlim_cur_nofile) ||
        (rlim_stack.rlim_cur == rlim_cur_stack)) {
      // Yes!  They were.
      printf("Some resource limits were reset back to the original values!\n"
             "  original( nofile, stack ): %d, %d\n"
             "  latest( nofile, stack ):   %d, %d\n",
             (int)rlim_cur_nofile, (int)rlim_cur_stack,
             (int)rlim_nofile.rlim_cur, (int)rlim_stack.rlim_cur);
      return 1; // error 1
    }
    nanosleep(& eighth_second, NULL);
    if (i%8 == 0) {
      printf("."); fflush(stdout);
    }
  }
  return 0;
}
