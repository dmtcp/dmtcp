// Setting _POSIX_C_SOURCE to 200112 changes the semantics of this program.
//  Why?

// #define _POSIX_C_SOURCE 200112
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define BT_SIZE 1024

// #define ENABLE_BACKTRACE
#ifdef ENABLE_BACKTRACE
# include <execinfo.h>
#endif /* ifdef ENABLE_BACKTRACE */

// See comment in autotest.py; This can trigger bug in 32-bit Linux
// because original app places vdso and libs in low memory when kernel uses
// legacy_va_layout (which is forced when stacksize is small), and the new
// vdso collides with the old libs in mtcp_restart.  So, it gets unmapped.

void
myHandler(int i)
{
#ifdef ENABLE_BACKTRACE
  int nptrs;
  void *buffer[BT_SIZE];
#endif /* ifdef ENABLE_BACKTRACE */

  printf("signal %d received.\n", i);
#ifdef ENABLE_BACKTRACE
  nptrs = backtrace(buffer, BT_SIZE);
  backtrace_symbols_fd(buffer, nptrs, STDOUT_FILENO);
  printf("\n");
#endif /* ifdef ENABLE_BACKTRACE */
}

int
main(int argc, char *argv[])
{
  char cmd_file[256];
  int cmd_len = readlink("/proc/self/exe", cmd_file, 255);

  if (cmd_len == -1) {
    printf("WARNING:  Couldn't find /proc/self/exe."
           "  Trying to continue anyway.\n");
  } else {
    cmd_file[cmd_len] = '\0';
    signal(SIGUSR1, &myHandler);
    signal(SIGUSR2, &myHandler); // DMTCP should not enable this.
    if (!getenv("IS_CHILD") && 0 != fork()) {   /* if child, do exec */
      setenv("IS_CHILD", "true", 1);
      execv(cmd_file, argv);
    }
  }

  while (1) {
    sleep(1);
    raise(SIGUSR1);
  }
}
