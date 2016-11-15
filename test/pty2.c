// Compile with -DDEBUG for debugging failure modes.

// grantpt, posix_openpt, etc., needs _XOPEN_SOURCE set to 600
#define _XOPEN_SOURCE 600

// Using _XOPEN_SOURCE to ensure ptsname returns 'char *' (recommended by Open
// Group)
#define _DEFAULT_SOURCE

// _DEFAULT_SOURCE used to expose sys_errlist[]
#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

// Define DEBUG when running manually, to see what part of terminal
// was not restored properly:
// #define DEBUG

#ifdef DEBUG

// printing to stdout won't work, when it goes to /dev/pts/XX
# define PERROR(str)                \
  fprintf(fdopen(orig_stdout, "w"), \
          # str ": %s\n", sys_errlist[errno])
#else /* ifdef DEBUG */
# define PERROR(str)
#endif /* ifdef DEBUG */

int setupslave(char *slavedevice, int orig_stdout);
int testslave(char *slavedevice, struct termios *orig_termios_p,
              int orig_stdout);

int
main()
{
  int masterfd, slavefd, pid;
  char *slavedevice;

#ifdef DEBUG
  int orig_stdout = dup(1);
#else /* ifdef DEBUG */
  int orig_stdout = -1;
#endif /* ifdef DEBUG */

  masterfd = posix_openpt(O_RDWR | O_NOCTTY);
  if (masterfd == -1
      || grantpt(masterfd) == -1
      || unlockpt(masterfd) == -1
      || (slavedevice = ptsname(masterfd)) == NULL) {
    return 1;
  }

  // Calling ptsname only once.  So, it's safe to continue using slavedevice.
  printf("slave device is: %s\n", slavedevice);
  slavefd = open(slavedevice, O_RDWR | O_NOCTTY);
  if (slavefd < 0) {
    return 2;
  }
  close(slavefd);
  if ((pid = fork()) < 0) {
    PERROR("fork");
    return 3;
  }
  if (pid == 0) {
    if (setupslave(slavedevice, orig_stdout) == 0) {
      struct termios orig_termios;
      tcgetattr(1, &orig_termios); /* fd 1 is now the slave terminal */
      int ppid = getppid();
      while (1) {
        testslave(slavedevice, &orig_termios, orig_stdout);
        if (kill(ppid, 0) == -1) { /* If parent process died, then exit. */
          return 0;
        }
      }
    }
  } else if (waitpid(pid, NULL, 0) == -1) {
    PERROR("waitpid");
  }
  return 0; /* Never returns */
}

int
setupslave(char *slavedevice, int orig_stdout)
{
  int fd;

  alarm(150); /* For safety; will not die when controlling terminal removed. */

  /* We are neither a session leader nor process group leader.
   * So, we are eligible to call setsid and become a new session/proc. grp ldr.
   */
  if (setsid() == -1) { /* set new sid and pgid */
    PERROR("setsid");
    return -1;
  }
  fd = open("/dev/tty", O_RDWR);
  if (fd != -1) { /* if we have a controlling terminai, get rid of it. */
    ioctl(fd, TIOCNOTTY);
  }

  /* We are now leader of a session and process group, without a
   * controlling terminal.  We are now eligible for controlling terminal.
   */
  close(0);
  close(1);
  close(2);
  fd = open(slavedevice, O_RDWR); /* Gains new controlling terminal */
  /* Alternative way to set controlling terminal:  ioctl(fd, TIOCSCTTY) */
  if (dup(fd) == -1) {
    return 1;
  }
  if (dup(fd) == -1) {
    return 1;
  }
  return 0;
}

int
testslave(char *slavedevice, struct termios *orig_termios_p, int orig_stdout)
{
  struct termios curr_termios;
  int fd = open(slavedevice, O_RDWR);

  if (fd == -1) {
    exit(1);
  }

#ifdef DEBUG
  FILE *stream = fdopen(orig_stdout, "w");
  fprintf(stream, "pid: %d, ppid: %d, sid: %d, pgid: %d\n",
          getpid(), getppid(), getsid(getpid()), getpgid(getpid()));
  fprintf(stream, "tcgetsid:  session id of %s is: %d\n",
          slavedevice, tcgetsid(fd));
  if (tcgetattr(1, &curr_termios) == 0) { /* fd 1 is now the slave terminal */
  }
  fprintf(stream, "c_iflag: %d, c_oflag: %d, c_cflag: %d, c_lflag: %d\n",
          curr_termios.c_iflag, curr_termios.c_oflag,
          curr_termios.c_cflag, curr_termios.c_lflag);

  /* input, output, control, local modes */
  sleep(2);
#else /* ifdef DEBUG */
  if (getpid() != getsid(getpid()) || getpid() != getpgid(getpid())) {
    exit(1);
  }
  if (getpid() != tcgetsid(fd)) {
    exit(1);
  }
  if (tcgetattr(1, &curr_termios) == -1) { /* fd 1 is now the slave terminal */
    exit(1);
  }
  if (curr_termios.c_iflag != orig_termios_p->c_iflag
      || curr_termios.c_oflag != orig_termios_p->c_oflag
      || curr_termios.c_cflag != orig_termios_p->c_cflag
      || curr_termios.c_lflag != orig_termios_p->c_lflag) {
    exit(1);
  }
#endif /* ifdef DEBUG */
  close(fd);
  return 0;
}
