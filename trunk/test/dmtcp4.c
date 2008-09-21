#include <stdio.h>
#include <signal.h>
#include <string.h>

void myGroovyHandler(int i){
  printf("yay signals!!!\n");
}

int main(int argc, char* argv[]){
  alarm(1);
  while (1){
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
