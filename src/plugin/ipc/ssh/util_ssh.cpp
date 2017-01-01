#include <assert.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#define MAX_BUFFER_SIZE (64 * 1024)

struct Buffer {
  char *buf;
  int off;
  int end;
  int len;
};

static void buffer_init(struct Buffer *buf);
static void buffer_free(struct Buffer *buf);
static void buffer_read(struct Buffer *buf, int fd);
static void buffer_write(struct Buffer *buf, int fd);

int quit_pending = 0;
pid_t childPid = -1;
int remoteSock;
static struct Buffer stdin_buffer, stdout_buffer, stderr_buffer;

static void
buffer_init(struct Buffer *buf)
{
  assert(buf != NULL);
  buf->buf = (char *)malloc(MAX_BUFFER_SIZE);
  assert(buf->buf != NULL);
  buf->off = 0;
  buf->end = 0;
  buf->len = MAX_BUFFER_SIZE;
}

static void
buffer_free(struct Buffer *buf)
{
  free(buf->buf);
  buf->buf = NULL;
  buf->len = 0;
}

static void
buffer_readjust(struct Buffer *buf)
{
  memmove(buf->buf, &buf->buf[buf->off], buf->end - buf->off);
  buf->end -= buf->off;
  buf->off = 0;
}

static bool
buffer_ready_for_read(struct Buffer *buf)
{
  assert(buf->buf != NULL && buf->len != 0);
  return buf->end < buf->len - 1;
}

static void
buffer_read(struct Buffer *buf, int fd)
{
  assert(buf->buf != NULL && buf->len != 0);

  if (buf->end < buf->len) {
    size_t max = buf->len - buf->end;
    ssize_t rc = read(fd, &buf->buf[buf->end], max);
    if (rc == 0 || (rc == -1 && errno != EINTR)) {
      quit_pending = 1;
      return;
    }
    buf->end += rc;
  }
}

static bool
buffer_ready_for_write(struct Buffer *buf)
{
  assert(buf->buf != NULL && buf->len != 0);
  return buf->end > buf->off;
}

static void
buffer_write(struct Buffer *buf, int fd)
{
  assert(buf->buf != NULL && buf->len != 0);

  assert(buf->end > buf->off);
  size_t max = buf->end - buf->off;
  ssize_t rc = write(fd, &buf->buf[buf->off], max);
  if (rc == -1 && errno != EINTR) {
    quit_pending = 1;
    return;
  }
  buf->off += rc;
  if (buf->off > buf->len / 2) {
    buffer_readjust(buf);
  }
}

/* set/unset filedescriptor to non-blocking */
static void
set_nonblock(int fd)
{
  int val;

  val = fcntl(fd, F_GETFL, 0);
  if (val < 0) {
    perror("fcntl failed");
  }
  val |= O_NONBLOCK;
  if (fcntl(fd, F_SETFL, val) == -1) {
    perror("fcntl failed");
  }
}

/*
 * Signal handler for signals that cause the program to terminate.  These
 * signals must be trapped to restore terminal modes.
 */
static void
signal_handler(int sig)
{
  quit_pending = 1;
  if (childPid != -1) {
    kill(childPid, sig);
  }
}

void
client_loop(int ssh_stdin, int ssh_stdout, int ssh_stderr, int sock)
{
  remoteSock = sock;

  /* Initialize buffers. */
  buffer_init(&stdin_buffer);
  buffer_init(&stdout_buffer);
  buffer_init(&stderr_buffer);

  /* enable nonblocking unless tty */
  set_nonblock(fileno(stdin));
  set_nonblock(fileno(stdout));
  set_nonblock(fileno(stderr));

  /*
   * Set signal handlers, (e.g. to restore non-blocking mode)
   * but don't overwrite SIG_IGN, matches behavior from rsh(1)
   */
  if (signal(SIGHUP, SIG_IGN) != SIG_IGN) {
    signal(SIGHUP, signal_handler);
  }
  if (signal(SIGINT, SIG_IGN) != SIG_IGN) {
    signal(SIGINT, signal_handler);
  }
  if (signal(SIGQUIT, SIG_IGN) != SIG_IGN) {
    signal(SIGQUIT, signal_handler);
  }
  if (signal(SIGTERM, SIG_IGN) != SIG_IGN) {
    signal(SIGTERM, signal_handler);
  }

  // signal(SIGWINCH, window_change_handler);

  std::vector<struct pollfd>fds;

  /* Main loop of the client for the interactive session mode. */
  while (!quit_pending) {
    struct pollfd socketFd = { 0 };

    fds.clear();
    socketFd.fd = remoteSock;
    socketFd.events = POLLRDHUP;
    fds.push_back(socketFd);

    if (buffer_ready_for_read(&stdin_buffer)) {
      socketFd.fd = STDIN_FILENO;
      socketFd.events = POLLIN;
      fds.push_back(socketFd);
    }
    if (buffer_ready_for_read(&stdout_buffer)) {
      socketFd.fd = ssh_stdout;
      socketFd.events = POLLIN;
      fds.push_back(socketFd);
    }
    if (buffer_ready_for_read(&stderr_buffer)) {
      socketFd.fd = ssh_stderr;
      socketFd.events = POLLIN;
      fds.push_back(socketFd);
    }

    if (buffer_ready_for_write(&stdin_buffer)) {
      socketFd.fd = ssh_stdin;
      socketFd.events = POLLOUT;
      fds.push_back(socketFd);
    }
    if (buffer_ready_for_write(&stdout_buffer)) {
      socketFd.fd = STDOUT_FILENO;
      socketFd.events = POLLOUT;
      fds.push_back(socketFd);
    }
    if (buffer_ready_for_write(&stderr_buffer)) {
      socketFd.fd = STDERR_FILENO;
      socketFd.events = POLLOUT;
      fds.push_back(socketFd);
    }

    int ret = poll((struct pollfd *)&fds[0], fds.size(), 10 * 1000);
    if (ret == -1 && errno == EINTR) {
      continue;
    }
    if (ret == -1) {
      perror("poll failed");
      return;
    }

    if (quit_pending) {
      break;
    }

    std::vector<struct pollfd>::iterator it;
    for (it = fds.begin(); it != fds.end(); it++) {
      // Read from our STDIN or stdout/err of ssh
      if (it->fd == STDIN_FILENO && it->revents & POLLIN) {
        buffer_read(&stdin_buffer, STDIN_FILENO);
      }
      if (it->fd == ssh_stdout && it->revents & POLLIN) {
        buffer_read(&stdout_buffer, ssh_stdout);
      }
      if (it->fd == ssh_stderr && it->revents & POLLIN) {
        buffer_read(&stderr_buffer, ssh_stderr);
      }

      // Write to our stdout/err or stdin of ssh
      if (it->fd == ssh_stdin && it->revents & POLLOUT) {
        buffer_write(&stdin_buffer, ssh_stdin);
      }
      if (it->fd == STDOUT_FILENO && it->revents & POLLOUT) {
        buffer_write(&stdout_buffer, STDOUT_FILENO);
      }
      if (it->fd == STDERR_FILENO && it->revents & POLLOUT) {
        buffer_write(&stderr_buffer, STDERR_FILENO);
      }

      if (it->fd == remoteSock &&
          (it->revents & (POLLHUP | POLLERR | POLLNVAL))) {
        goto end;
      }
    }

    if (quit_pending) {
      break;
    }
  }

end:

  /* Write pending data to our stdout/stderr */
  if (buffer_ready_for_write(&stdout_buffer)) {
    buffer_write(&stdout_buffer, STDOUT_FILENO);
  }
  if (buffer_ready_for_write(&stderr_buffer)) {
    buffer_write(&stderr_buffer, STDERR_FILENO);
  }

  /* Clear and free any buffers. */
  buffer_free(&stdin_buffer);
  buffer_free(&stdout_buffer);
  buffer_free(&stderr_buffer);
}
