#include <unistd.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <assert.h>

#define MAX(a,b) ((a) < (b) ? (b) : (a))

#define MAX_BUFFER_SIZE (64*1024)

struct Buffer {
  char *buf;
  int  off;
  int  end;
  int  len;
};

static void buffer_init(struct Buffer *buf);
static void buffer_free(struct Buffer *buf);
static void buffer_read(struct Buffer *buf, int fd);
static void buffer_write(struct Buffer *buf, int fd);

int quit_pending = 0;
pid_t childPid = -1;
int remoteSock;
static struct Buffer stdin_buffer, stdout_buffer, stderr_buffer;

static void buffer_init(struct Buffer *buf)
{
  assert(buf != NULL);
  buf->buf = (char*) malloc(MAX_BUFFER_SIZE);
  assert(buf->buf != NULL);
  buf->off = 0;
  buf->end = 0;
  buf->len = MAX_BUFFER_SIZE;
}

static void buffer_free(struct Buffer *buf)
{
  free(buf->buf);
  buf->buf = NULL;
  buf->len = 0;
}

static void buffer_readjust(struct Buffer *buf)
{
  memmove(buf->buf, &buf->buf[buf->off], buf->end - buf->off);
  buf->end -= buf->off;
  buf->off = 0;
}

static bool buffer_ready_for_read(struct Buffer *buf)
{
  assert(buf->buf != NULL && buf->len != 0);
  return buf->end < buf->len - 1;
}

static void buffer_read(struct Buffer *buf, int fd)
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

static bool buffer_ready_for_write(struct Buffer *buf)
{
  assert(buf->buf != NULL && buf->len != 0);
  return buf->end > buf->off;
}

static void buffer_write(struct Buffer *buf, int fd)
{
  assert(buf->buf != NULL && buf->len != 0);

  assert(buf->end > buf->off);
  size_t max = buf->end - buf->off;
  ssize_t rc = write(fd,  &buf->buf[buf->off], max);
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
static void set_nonblock(int fd)
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
static void signal_handler(int sig)
{
  quit_pending = 1;
  if (childPid != -1) {
    kill(childPid, sig);
  }
}

void client_loop(int ssh_stdin, int ssh_stdout, int ssh_stderr, int sock)
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
   * but don't overwrite SIG_IGN, matches behaviour from rsh(1)
   */
  if (signal(SIGHUP, SIG_IGN) != SIG_IGN)
    signal(SIGHUP, signal_handler);
  if (signal(SIGINT, SIG_IGN) != SIG_IGN)
    signal(SIGINT, signal_handler);
  if (signal(SIGQUIT, SIG_IGN) != SIG_IGN)
    signal(SIGQUIT, signal_handler);
  if (signal(SIGTERM, SIG_IGN) != SIG_IGN)
    signal(SIGTERM, signal_handler);
  //signal(SIGWINCH, window_change_handler);

  fd_set readset, writeset, errorset;
  int max_fd = 0;

  max_fd = MAX(ssh_stdin, ssh_stdout);
  max_fd = MAX(max_fd, ssh_stderr);

  /* Main loop of the client for the interactive session mode. */
  while (!quit_pending) {
    struct timeval tv = {10, 0};
    FD_ZERO(&readset);
    FD_ZERO(&writeset);
    FD_ZERO(&errorset);
    FD_SET(remoteSock, &errorset);

    if (buffer_ready_for_read(&stdin_buffer)) {
      FD_SET(STDIN_FILENO, &readset);
    }
    if (buffer_ready_for_read(&stdout_buffer)) {
      FD_SET(ssh_stdout, &readset);
    }
    if (buffer_ready_for_read(&stderr_buffer)) {
      FD_SET(ssh_stderr, &readset);
    }

    if (buffer_ready_for_write(&stdin_buffer)) {
      FD_SET(ssh_stdin, &writeset);
    }
    if (buffer_ready_for_write(&stdout_buffer)) {
      FD_SET(STDOUT_FILENO, &writeset);
    }
    if (buffer_ready_for_write(&stderr_buffer)) {
      FD_SET(STDERR_FILENO, &writeset);
    }

    int ret = select(max_fd, &readset, &writeset, &errorset, &tv);
    if (ret == -1 && errno == EINTR) {
      continue;
    }
    if (ret == -1) {
      perror("select failed");
      return;
    }

    if (quit_pending)
      break;

    //Read from our STDIN or stdout/err of ssh
    if (FD_ISSET(STDIN_FILENO, &readset)) {
      buffer_read(&stdin_buffer, STDIN_FILENO);
    }
    if (FD_ISSET(ssh_stdout, &readset)) {
      buffer_read(&stdout_buffer, ssh_stdout);
    }
    if (FD_ISSET(ssh_stderr, &readset)) {
      buffer_read(&stderr_buffer, ssh_stderr);
    }

    // Write to our stdout/err or stdin of ssh
    if (FD_ISSET(ssh_stdin, &writeset)) {
      buffer_write(&stdin_buffer, ssh_stdin);
    }
    if (FD_ISSET(STDOUT_FILENO, &writeset)) {
      buffer_write(&stdout_buffer, STDOUT_FILENO);
    }
    if (FD_ISSET(STDERR_FILENO, &writeset)) {
      buffer_write(&stderr_buffer, STDERR_FILENO);
    }

    if (FD_ISSET(remoteSock, &errorset)) {
      break;
    }

    if (quit_pending)
      break;
  }

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
