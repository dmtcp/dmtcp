#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int
main()
{
  int pipefd[2];

  if (pipe(pipefd) == -1) {
    perror("pipe");
    exit(1);
  }
  int fd = open("/dev/null", O_RDONLY);
  struct pollfd pfd[1];
  pfd[0].fd = pipefd[0];
  pfd[0].events = POLLIN;

  /* poll should never return, since we can't read from /dev/in.
   * 'man 7 signal' says that any arriving signal (e.g. CKPT signal)
   *   will cause poll to return -1 with EINTR.
   * The DMTCP poll wrapper should restart poll() before it can return.
   */
  int rc = poll(pfd, 1, -1); /* -1 means infinite timeout */
  printf("ERROR:  rc = %d; errno = %d\n", rc, errno);
  close(fd);
  return 1;
}
