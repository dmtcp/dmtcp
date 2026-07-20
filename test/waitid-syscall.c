#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void
burn_cpu(void)
{
  volatile unsigned long long value = 0;
  unsigned long long i;

  for (i = 0; i < 200000000ULL; i++) {
    value += i;
  }
  _exit((int)(value & 0));
}

static int
rusage_was_filled(const struct rusage *usage)
{
  return usage->ru_utime.tv_sec >= 0 &&
         usage->ru_utime.tv_usec >= 0 &&
         usage->ru_utime.tv_usec < 1000000 &&
         usage->ru_stime.tv_sec >= 0 &&
         usage->ru_stime.tv_usec >= 0 &&
         usage->ru_stime.tv_usec < 1000000;
}

static void
run_forever(void)
{
  while (1) {
    printf("waitid-syscall ok\n");
    fflush(stdout);
    sleep(1);
  }
}

int
main(void)
{
#ifndef SYS_waitid
  run_forever();
#else
  pid_t child = fork();
  if (child == -1) {
    perror("fork");
    return 1;
  }
  if (child == 0) {
    burn_cpu();
  }

  siginfo_t info;
  struct rusage usage;
  memset(&info, 0, sizeof(info));
  memset(&usage, 0x5a, sizeof(usage));

  long ret = syscall(SYS_waitid, P_PID, child, &info, WEXITED, &usage);
  if (ret == -1) {
    perror("syscall(SYS_waitid)");
    return 1;
  }
  if (info.si_pid != child || info.si_code != CLD_EXITED) {
    fprintf(stderr, "unexpected waitid result: pid=%ld code=%d\n",
            (long)info.si_pid, info.si_code);
    return 1;
  }
  if (!rusage_was_filled(&usage)) {
    fprintf(stderr, "SYS_waitid did not populate struct rusage\n");
    return 1;
  }
  run_forever();
#endif
}
