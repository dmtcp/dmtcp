// SA_ONSTACK and SA_RESETHAND are _XOPEN_SOURCE according to POSIX OpenGroup

// But gcc-4.8 -std=c11 seems to require _GNU_SOURCE for these two macros.
#define _GNU_SOURCE

// sigaction() needs _XOPEN_SOURCE or _POSIX_SOURCE
#define _XOPEN_SOURCE
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void
myGroovyHandler(int i)
{
  printf("yay signals!!!\n");
}

int
main(int argc, char *argv[])
{
  alarm(1);
  while (1) {
    signal(SIGUSR1, &myGroovyHandler);
    signal(SIGUSR2, &myGroovyHandler);
    signal(SIGALRM, &myGroovyHandler);

    sigset_t set;
    sigfillset(&set);
    sigdelset(&set, SIGALRM);
    sigdelset(&set, SIGTERM);
    sigdelset(&set, SIGSTOP);
    sigdelset(&set, SIGSEGV);
    sigdelset(&set, SIGINT);  // Let user and autotest.py kill it.
    sigprocmask(SIG_BLOCK, &set, NULL);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = &myGroovyHandler;
    sigfillset(&act.sa_mask);
    act.sa_flags = SA_ONSTACK | SA_RESETHAND;
    sigaction(SIGUSR1, &act, NULL);
    sigaction(SIGUSR2, &act, NULL);
    sigaction(SIGALRM, &act, NULL);
  }
  return 0;
}
