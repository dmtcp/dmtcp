#include <assert.h>
#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/types.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unistd.h>
#include "rm_main.h"
#include "slurm_helper.h"

extern "C" void slurm_srun_handler_register(int in, int out, int err,
                                            int *pid) __attribute((weak));

int quit_pending = 0;
int pipe_in[2], pipe_out[2], pipe_err[2];
int srun_stdin = -1;
int srun_stdout = -1;
int srun_stderr = -1;
pid_t srun_pid = -1, helper_pid = -1;
int restart_fd = -1;

#define MAX(a, b) ((a) > (b) ? (a) : (b))

// FIXME: this is exactly the same code as in src/plugin/ipc/ssh/util_ssh.cpp
// We need to use the same code base in the future.
// TODO: Put this in some shared location (e.g., util directory).


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

// End of FIXME

/*
 * Signal handler for signals that cause the program to terminate.  These
 * signals must be trapped to restore the terminal modes.
 */
static void
signal_handler_ini(int sig)
{
  quit_pending = 1;
  if (sig == SIGCHLD) {
    // TODO: wait stuff here?
    return;
  }

  // Forward signals only during the initial run
  if (srun_pid != -1) {
    kill(srun_pid, sig);
  }
}

static void
setup_signals_ini()
{
  if (signal(SIGHUP, SIG_IGN) != SIG_IGN) {
    signal(SIGHUP, signal_handler_ini);
  }
  if (signal(SIGINT, SIG_IGN) != SIG_IGN) {
    signal(SIGINT, signal_handler_ini);
  }
  if (signal(SIGQUIT, SIG_IGN) != SIG_IGN) {
    signal(SIGQUIT, signal_handler_ini);
  }
  if (signal(SIGTERM, SIG_IGN) != SIG_IGN) {
    signal(SIGTERM, signal_handler_ini);
  }

  if (signal(SIGTERM, SIG_IGN) != SIG_IGN) {
    signal(SIGTERM, signal_handler_ini);
  }

  // Track srun
  signal(SIGCHLD, signal_handler_ini);
}

static void
create_stdio_fds(int *in, int *out, int *err)
{
  struct stat buf, dev_null;
  bool have_dev_null = true;

  in[0] = in[1] = -1;
  out[0] = out[1] = -1;
  err[0] = err[1] = -1;

  // Close all open file descriptors.
  int maxfd = sysconf(_SC_OPEN_MAX);
  for (int i = 3; i < maxfd; i++) {
    close(i);
  }

  if (stat("/dev/null", &dev_null) == -1) {
    have_dev_null = false;
    perror("stat(/dev/null");
  }

  if (fstat(STDIN_FILENO, &buf) == -1) {
    FWD_TO_DEV_NULL(STDIN_FILENO);
    goto stdout_setup;
  }
  if (have_dev_null && buf.st_ino == dev_null.st_ino) {
    goto stdout_setup;
  }
  if (pipe(in) != 0) {
    perror("Error creating STDIN pipe for srun: ");
  }

stdout_setup:
  if (fstat(STDOUT_FILENO, &buf) == -1) {
    FWD_TO_DEV_NULL(STDOUT_FILENO);
    goto stderr_setup;
  }
  if (have_dev_null && buf.st_ino == dev_null.st_ino) {
    goto stderr_setup;
  }
  if (pipe(out) != 0) {
    perror("Error creating pipe: ");
  }

stderr_setup:
  if (fstat(STDERR_FILENO, &buf) == -1) {
    FWD_TO_DEV_NULL(STDERR_FILENO);
    goto exit;
  }
  if (have_dev_null && buf.st_ino == dev_null.st_ino) {
    goto exit;
  }
  if (pipe(err) != 0) {
    perror("Error creating pipe: ");
  }
exit:
  return;
}

pid_t
fork_srun(int argc, char **argv)
{
  int *in = pipe_in, *out = pipe_out, *err = pipe_err;

  unsetenv("LD_PRELOAD");
  pid_t pid = fork();
  if (pid == 0) {
    if (in[0] >= 0 && in[1] >= 0) {
      dup2(in[0], STDIN_FILENO);
      close(in[0]);
      close(in[1]);
    } else {
      FWD_TO_DEV_NULL(STDIN_FILENO);
    }

    if (out[0] >= 0 && out[1] >= 0) {
      dup2(out[1], STDOUT_FILENO);
      close(out[0]);
      close(out[1]);
    } else {
      FWD_TO_DEV_NULL(STDOUT_FILENO);
    }

    if (err[0] >= 0 && err[1] >= 0) {
      dup2(err[1], STDERR_FILENO);
      close(err[0]);
      close(err[1]);
    } else {
      FWD_TO_DEV_NULL(STDERR_FILENO);
    }
    unsetenv("LD_PRELOAD");
    execvp(argv[1], &argv[1]);
    printf("%s:%d DMTCP Error detected. Failed to exec.", __FILE__, __LINE__);
    abort();
  }

  if (in[0] >= 0) {
    close(in[0]);
  }
  if (out[1] >= 0) {
    close(out[1]);
  }
  if (err[1] >= 0) {
    close(err[1]);
  }

  srun_stdin = in[1];
  srun_stdout = out[0];
  srun_stderr = err[0];

  return pid;
}

#define BUF_INIT(ifd, ofd, buf) \
  if (ifd >= 0 && ofd >= 0) {   \
    set_nonblock(ifd);          \
    set_nonblock(ofd);          \
    buffer_init(&buf);          \
  }

#define BUF_PREPARE(ifd, ofd, buf, rset, wset) \
  if (ifd >= 0 && ofd >= 0) {                  \
    if (buffer_ready_for_read(&buf)) {         \
      FD_SET(ifd, &readset);                   \
    }                                          \
    if (buffer_ready_for_write(&buf)) {        \
      FD_SET(ofd, &writeset);                  \
    }                                          \
  }

#define BUF_PROCESS(ifd, ofd, buf, rset, wset) \
  if (ifd >= 0 && ofd >= 0) {                  \
    if (FD_ISSET(ifd, &rset)) {                \
      buffer_read(&buf, ifd);                  \
    }                                          \
    if (FD_ISSET(ofd, &wset)) {                \
      buffer_write(&buf, ofd);                 \
    }                                          \
  }

#define BUF_FINI(ifd, ofd, buf, drain)    \
  if (ifd >= 0 && ofd >= 0) {             \
    if (drain) {                          \
      if (buffer_ready_for_write(&buf)) { \
        buffer_write(&buf, ofd);          \
      }                                   \
      buffer_free(&buf);                  \
    }                                     \
  }

bool
is_empty_loop()
{
  if (srun_stdin < 0 && srun_stdout < 0 && srun_stderr < 0) {
    return true;
  }
  return false;
}

static void
fwd_loop()
{
  static struct Buffer stdin_buffer, stdout_buffer, stderr_buffer;

  BUF_INIT(STDIN_FILENO, srun_stdin, stdin_buffer);
  BUF_INIT(srun_stdout, STDOUT_FILENO, stdout_buffer);
  BUF_INIT(srun_stderr, STDERR_FILENO, stderr_buffer);

  fd_set readset, writeset, errorset;
  int max_fd = 0;

  max_fd = MAX(MAX(srun_stdin, srun_stdout), srun_stderr);

  /* Loop before checkpoint. */
  while (!quit_pending) {
    struct timeval tv = { 10, 0 };

    // Handle errors on all fds of interest.
    FD_ZERO(&errorset);
    FD_ZERO(&readset);
    FD_ZERO(&writeset);

    BUF_PREPARE(STDIN_FILENO, srun_stdin, stdin_buffer, readset, writeset);
    BUF_PREPARE(srun_stdout, STDOUT_FILENO, stdout_buffer, readset, writeset);
    BUF_PREPARE(srun_stderr, STDERR_FILENO, stderr_buffer, readset, writeset);

    int ret = select(max_fd, &readset, &writeset, &errorset, &tv);
    if (ret == -1 && errno == EINTR) {
      continue;
    }
    if (ret == -1) {
      perror("select failed");
      return;
    }

    if (quit_pending) {
      break;
    }

    BUF_PROCESS(STDIN_FILENO, srun_stdin, stdin_buffer, readset, writeset);
    BUF_PROCESS(srun_stdout, STDOUT_FILENO, stdout_buffer, readset, writeset);
    BUF_PROCESS(srun_stderr, STDERR_FILENO, stderr_buffer, readset, writeset);
  }

  BUF_FINI(STDIN_FILENO, srun_stdin, stdin_buffer, 0);
  BUF_FINI(srun_stdout, STDOUT_FILENO, stdout_buffer, 1);
  BUF_FINI(srun_stderr, STDERR_FILENO, stderr_buffer, 1);
}

// -------------------- restore helper ----------------------------//

static void
signal_handler_rstr(int sig)
{
  quit_pending = 1;
  if (sig == SIGCHLD) {
    // TODO: wait stuff here?
    // Forward termination to the initial handler,
    // so that mpirun/mpiexec will know that it was terminated.
    kill(helper_pid, SIGCHLD);
    return;
  }
  if (srun_pid != -1) {
    kill(srun_pid, sig);
  }
}

static void
setup_signals_rstr()
{
  if (signal(SIGHUP, SIG_IGN) != SIG_IGN) {
    signal(SIGHUP, signal_handler_rstr);
  }
  if (signal(SIGINT, SIG_IGN) != SIG_IGN) {
    signal(SIGINT, signal_handler_rstr);
  }
  if (signal(SIGQUIT, SIG_IGN) != SIG_IGN) {
    signal(SIGQUIT, signal_handler_rstr);
  }
  if (signal(SIGTERM, SIG_IGN) != SIG_IGN) {
    signal(SIGTERM, signal_handler_rstr);
  }

  if (signal(SIGTERM, SIG_IGN) != SIG_IGN) {
    signal(SIGTERM, signal_handler_rstr);
  }

  // Track srun
  signal(SIGCHLD, signal_handler_rstr);
}

static void
create_usock_file(char *path, int maxlen)
{
  memset(path, 0, maxlen);

  // TODO: change /tmp to tmpdir env
  snprintf(path, maxlen - 1, "/tmp/srun_helper_usock.XXXXXX");
  int fd;
  assert((fd = mkstemp(path)) >= 0);
  close(fd);
  unlink(path);
}

static int
create_listen_socket(char **path)
{
  static struct sockaddr_un sa;
  static socklen_t salen;

  memset(&sa, 0, sizeof(sa));
  int sfd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sfd <= STDERR_FILENO) {
    dup2(STDERR_FILENO + 1, sfd);
    close(sfd);
    sfd = STDERR_FILENO + 1;
  }
  assert(sfd >= 0);
  sa.sun_family = AF_UNIX;
  create_usock_file((char *)&sa.sun_path, sizeof(sa.sun_path));
  if (bind(sfd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
    printf("error: %s\n", strerror(errno));
  }
  salen = sizeof(sa);
  assert(getsockname(sfd, (struct sockaddr *)&sa, &salen) == 0);
  assert(listen(sfd, 1) == 0);
  *path = strdup(sa.sun_path);

  return sfd;
}

static void
prepare_initial_helper(char *sync_path)
{
  char *path;
  char buf[MAX_BUFFER_SIZE];

  restart_fd = create_listen_socket(&path);
  FILE *fp = fopen(sync_path, "w");
  fprintf(fp, "export %s=%s\n", DMTCP_SRUN_HELPER_ADDR_ENV, path);
  fsync(fileno(fp));
  fclose(fp);

  // Make sure that the file is readable to others.
  int i = 0, repmax = 10;
  while (i < repmax) {
    fp = fopen(sync_path, "r");
    assert(fp != NULL);
    assert(fgets(buf, MAX_BUFFER_SIZE, fp) != NULL);
    if (strstr(buf, path) != NULL) {
      i = repmax;
    } else {
      i++;
      sleep(1); // TODO: change to nanosleep
    }
    fclose(fp);
  }
}

static void
create_stdio_fds_rstr(int *in, int *out, int *err)
{
  struct stat buf;

  in[0] = in[1] = -1;
  out[0] = out[1] = -1;
  err[0] = err[1] = -1;

  // Close all open file descriptors
  int maxfd = sysconf(_SC_OPEN_MAX);
  for (int i = 3; i < maxfd; i++) {
    if (i != restart_fd) {
      close(i);
    }
  }

  if (fstat(STDIN_FILENO, &buf) == -1) {
    int fd = open("/dev/null", O_RDWR);
    if (fd != STDIN_FILENO) {
      dup2(fd, STDIN_FILENO);
      close(fd);
    }
  }
  assert(pipe(in) == 0);

  if (fstat(STDOUT_FILENO, &buf) == -1) {
    int fd = open("/dev/null", O_RDWR);
    if (fd != STDOUT_FILENO) {
      dup2(fd, STDOUT_FILENO);
      close(fd);
    }
  }
  assert(pipe(out) == 0);

  if (fstat(STDERR_FILENO, &buf) == -1) {
    int fd = open("/dev/null", O_RDWR);
    if (fd != STDERR_FILENO) {
      dup2(fd, STDERR_FILENO);
      close(fd);
    }
  }
  assert(pipe(err) == 0);
}

static void
setup_initial_helper()
{
  int sd;
  struct sockaddr_un sa;
  socklen_t slen = sizeof(sa);

  if ((sd = accept(restart_fd, (struct sockaddr *)&sa, &slen)) < 0) {
    perror("accept from initial handler");
    exit(-1);
  }
  pid_t pid = getpid();

  // exchange pid info
  assert(read(sd, &helper_pid, sizeof(helper_pid)) == sizeof(helper_pid));
  assert(write(sd, &pid, sizeof(pid)) == sizeof(pid));

  assert(read(sd, &sa, sizeof(sa)) == sizeof(sa));
  int fd_sock = socket(AF_UNIX, SOCK_DGRAM, 0);
  socklen_t len = sizeof(sa.sun_family) + strlen(sa.sun_path + 1) + 1;
  slurm_sendFd(fd_sock, srun_stdin, &srun_stdin, sizeof(int), sa, len);
  slurm_sendFd(fd_sock, srun_stdout, &srun_stdout, sizeof(int), sa, len);
  slurm_sendFd(fd_sock, srun_stderr, &srun_stderr, sizeof(int), sa, len);
  close(fd_sock);
  close(sd);
  close(restart_fd);
}

void
daemonize()
{
  int pid = fork();

  assert(pid >= 0);
  if (pid > 0) {
    exit(0); // detach from the terminal
  }
  pid = fork();
  assert(pid >= 0);
  if (pid > 0) {
    exit(0); // detach from the terminal
  }
  setsid();
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);
}

// -------------------- main -------------------------------------//

int
main(int argc, char **argv, char **envp)
{
  int status;
  bool restart_helper = false;

  if (argc < 2) {
    printf("***ERROR: This program shouldn't be used directly.\n");
    exit(1);
  }

  if (strcmp("-r", argv[1]) == 0) {
    if (argc < 3) {
      printf("***ERROR: This program shouldn't be used directly.\n");
      exit(1);
    }
    char *sync_file_name = getenv(DMTCP_SRUN_HELPER_SYNC_ENV);
    restart_helper = true;

    // skip "-r" flag
    argc--;
    argv++;

    // Prepare the evironment and siganls
    prepare_initial_helper(sync_file_name);
    daemonize();
    create_stdio_fds_rstr(pipe_in, pipe_out, pipe_err);
  } else {
    // {
    // int delay = 1;
    // while (delay) {
    // sleep(1);
    // }
    // }

    create_stdio_fds(pipe_in, pipe_out, pipe_err);
  }

  srun_pid = fork_srun(argc, argv);

  if (!restart_helper) {
    setup_signals_ini();

    // This is the initial helper.
    assert(slurm_srun_handler_register != NULL);
    slurm_srun_handler_register(srun_stdin, srun_stdout, srun_stderr,
                                &srun_pid);
    if (is_empty_loop()) {
      while (!quit_pending) {
        sleep(1);
      }
    } else {
      fwd_loop();
    }
  } else {
    setup_signals_rstr();

    // {
    // int delay = 1;
    // while (delay) {
    // sleep(1);
    // }
    // }
    setup_initial_helper();
    while (!quit_pending) {
      sleep(1);
    }
  }

  wait(&status);
  return status;
}
