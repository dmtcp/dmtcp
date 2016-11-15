// grantpt, posix_openpt, etc., needs _XOPEN_SOURCE set to 600
#define _XOPEN_SOURCE 600
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <errno.h>
#include <pty.h>
#include <stdio.h>
#include <termios.h>

#define PACKET_MODE 1

// If using openpty, forkpty, or login_tty, compile with:  -lutil

int
main()
{
  int next_char[16] = { 'a', '\n' };

  // 'man setsid' says:
  // A  process group leader is a process with process group ID equal to its
  // PID.  In order to be sure  that  setsid()  will  succeed,  fork(2)  and
  // _exit(2), and have the child do setsid().
  // if (fork())
  // _exit(0);
  // setsid();

#if 1
  int master_fd = open("/dev/ptmx", O_RDWR /*| O_NOCTTY*/);
  grantpt(master_fd);
  unlockpt(master_fd);
  char *slave_pty = ptsname(master_fd);
  int slave_fd = open(slave_pty, O_RDWR);
  struct termios term;
  tcgetattr(slave_fd, &term);
  term.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); /* Turn off echo */
  tcsetattr(slave_fd, TCSANOW, &term);
  fcntl(master_fd, F_SETFL, O_NONBLOCK); /* Unblock master_fd, so we can poll
                                        for packet bytes ; Not needed? */
# ifdef PACKET_MODE
  int packetMode = 1;
  ioctl(master_fd, TIOCPKT, &packetMode); /* Place into packet mode */
  tcflush(master_fd, TCIOFLUSH); /* Flush in case in middle of old packet??? */
# endif /* ifdef PACKET_MODE */
#else /* if 1 */
  int master_fd;
  int slave_fd;
  openpty(&master_fd, &slave_fd, NULL, NULL, NULL); /* Use -lutil for openpty */
#endif /* if 1 */

#if 0

  // Optionally inherit original terminal type
  tcgetattr(STDIN_FILENO, &term);
  tcsetattr(slave_fd, TCSANOW, &term);

  // Optionally inherit original window type
  ioctl(STDIN_FILENO, TIOCGWINSZ, (char *)&wsize);
  ioctl(slave_fd, TIOCSWINSZ, &wsize);

  // Verify that master_fd and slave_fd are connected
  int len = write(master_fd, "abc\n", 4);
  len = read(slave_fd, tmp_buf, 50);
  printf("slave read from master:  len=%d, tmp_buf: %s\n", len, tmp_buf);
  len = write(slave_fd, "def\n", 4);
  len = read(master_fd, tmp_buf, 50);
  printf("master read from slave:  len=%d, tmp_buf: %s\n", len, tmp_buf);
#endif /* if 0 */

  if (fork()) { /* if parent */
    close(slave_fd);
    while (1) {
      char in_buffer[100];
      int len;
      len = write(master_fd, next_char, 2);  /* Tell slave to go again. */
      next_char[0] = (next_char[0] >= 'z' ? 'a' : next_char[0] + 1);
      sleep(1);
#ifdef PACKET_MODE
      do {
        /* Packet mode should guarantee we get a full packet or nothing. */

        // SHOULD 'select()' AND EXIT IF READ UNAVAILABLE SOON.
        len = read(master_fd, in_buffer, 100);
        if (in_buffer[0] == '\000') {
          int i;
          printf("packet mode character seen\n");
          for (i = 1; i < len; i++) {
            in_buffer[i - 1] = in_buffer[i];
          }
          len--;
        }
      } while (len <= 0);
#else /* ifdef PACKET_MODE */
      len = read(master_fd, in_buffer, 100);
#endif /* ifdef PACKET_MODE */
      if (len == -1) {
        perror("read on ptmx");
        sleep(2);
      }
      len = write(STDOUT_FILENO, in_buffer, len);
    }
  } else { /* else child */
    close(master_fd);

    // Optionally:  setsid(); ioctl(slave_fd, TIOCSCTTY);
    while (1) {
      char in_buffer[100];

      // tcgetattr(slave_fd, &term);
      if (read(slave_fd, in_buffer, 2) != 2) {
        perror("read");
        exit(1);
      }
      if (in_buffer[0] == next_char[0]) {
        next_char[0] = (next_char[0] >= 'z' ? 'a' : next_char[0] + 1);
      } else {
        exit(100);
      }
      in_buffer[0] -= 32; /* lower case to upper case */
      if (write(slave_fd, in_buffer, 2) != 2) {
        perror("write");
        exit(1);
      }
    }
  }
  return 0; /* never returns */
}
