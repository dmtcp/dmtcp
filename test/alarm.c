#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void
handle_alarm(int sig)
{
  exit(EXIT_SUCCESS);
}

int
main()
{
  signal(SIGALRM, handle_alarm);
  alarm(15);
  while (1) {
    sleep(1);
  }
  return 0;
}
