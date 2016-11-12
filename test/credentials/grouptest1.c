#include <signal.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

/*
  Test#1 for GROUP restore logic
  p1 - parent of all others. BASH creates a group for it and brings it at foreground
  p2 - child of p1. Creates its own group. p1 brings this group at foreground
  p3 - child of p2. Sits in p2 group.
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
    // p2 part
    setpgid(0, 0); // create new group
    sleep(1);
    fork();
  } else {
    // p1 part
    sleep(1);
    tcsetpgrp(0, pid);
  }

  // part of all
  while (1) {
    sleep(1);
  }
}
