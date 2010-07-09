#include <stdio.h>
#include <signal.h>
#include <unistd.h>

#define BT_SIZE 1024
// #define ENABLE_BACKTRACE
#ifdef ENABLE_BACKTRACE
# include <execinfo.h>
#endif

// See comment in autotest.py; This can trigger bug in 32-bit Linux
// because original app places vdso and libs in low memory when kernel uses
// legacy_va_layout (which is forced when stacksize is small), and the new
// vdso collides with the old libs in mtcp_restart.  So, it gets unmapped.

void myHandler(int i){
  int nptrs;
  void * buffer[BT_SIZE];

  printf("signal %d received.\n", i);
#ifdef ENABLE_BACKTRACE
  nptrs = backtrace (buffer, BT_SIZE);
  backtrace_symbols_fd ( buffer, nptrs, STDOUT_FILENO );
  printf("\n");
#endif
}

int main(int argc, char* argv[]){
  signal(SIGUSR1, &myHandler);
  signal(SIGUSR2, &myHandler); // DMTCP should not enable this.
  while (1){
    sleep(1);
    raise(SIGUSR1);
  }
}
