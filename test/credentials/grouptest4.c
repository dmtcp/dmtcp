#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>


/*
  Test#1 for GROUP restore logic
  p1 - parent of all others. BASH creates a group for it and brings it at foreground
  p2 - child of p1. Creates its own group. p1 brings this group at foreground
  p3 - child of p2. Sits in p2 group.
  p4 - child of p2. Sits in p2 group.
*/
void
inthandler(int __attribute__((unused)) sig)
{
  printf("%d: SIGINT\n", getpid());
}

int
main()
{
  pid_t pid;

  signal(SIGINT, inthandler);

  if (!(pid = fork())) {
    int i = 0;
    setpgid(0, 0); // create new group
    // wait while p1 makes us foreground group
    while (tcgetpgrp(0) != getpid()) {
      struct timespec ts = { 0, 100000000 };
      nanosleep(&ts, NULL);
      i++;
      printf("p2: wait for foreground. Iter=%d\n", i);
    }
    if (!fork()) {
      if (!(pid = fork())) {
        fork();
      }
    } else {
      exit(0);
    }
  } else {
    int status;
    sleep(1);
    tcsetpgrp(0, pid);
    wait(&status);
  }

  while (1) {
    sleep(1);
  }
}
