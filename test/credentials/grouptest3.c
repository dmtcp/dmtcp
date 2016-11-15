#include <signal.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/*
  Test#1 for GROUP restore logic
  p1 - parent of all others. BASH creates a group for it and brings it at foreground
  p2 - child of p1. Creates its own group. p1 brings this group at foreground
  p3 - child of p2. Sits in p2 group.
  p4 - child of p2. It creates its own group. p2 brings it into foreground
  p5 - child of p4. Sits in p4 group.
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
    if (fork()) {
      if (!(pid = fork())) {
        setpgid(0, 0); // create new group
        // wait while p2 makes us foreground group
        while (tcgetpgrp(0) != getpid()) {
          struct timespec ts = { 0, 100000000 };
          nanosleep(&ts, NULL);
          i++;
          printf("p4: wait for foreground. Iter=%d\n", i);
        }
        fork();
      } else {
        sleep(1);
        tcsetpgrp(0, pid);
      }
    }
  } else {
    sleep(1);
    tcsetpgrp(0, pid);
  }

  while (1) {
    sleep(1);
  }
}
