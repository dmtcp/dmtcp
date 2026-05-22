/*
 * Runtime preload probe for the built-in IPC wrappers.
 *
 * Run under bin/dmtcp_launch.  The probe validates launch-time and post-exec
 * preload state and touches representative socket, event, POSIX mqueue, and pty
 * wrapper paths.  It is intentionally safe to run in normal and
 * --disable-all-plugins modes.
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __linux__
# include <sys/epoll.h>
# include <sys/eventfd.h>
#endif

#define ENV_HIJACK_LIBS "DMTCP_HIJACK_LIBS"
#define ENV_DISABLE_ALL_PLUGINS "DMTCP_DISABLE_ALL_PLUGINS"
#define ENV_AFTER_EXEC "DMTCP_IPC_BUILT_IN_PRELOAD_AFTER_EXEC"
#define LIB_DMTCP "libdmtcp.so"
#define LIB_DMTCP_IPC "libdmtcp_ipc.so"

#define EXIT_ENV 2
#define EXIT_IPC 3
#define EXIT_EXEC 4

static int
fail_errno(const char *phase, const char *operation)
{
  fprintf(stderr, "%s: %s failed: %s\n", phase, operation, strerror(errno));
  return EXIT_IPC;
}

static int
require_env_contains(const char *phase, const char *env_name, const char *token)
{
  const char *value = getenv(env_name);
  if (value == NULL || strstr(value, token) == NULL) {
    fprintf(stderr, "%s: %s missing required token '%s'; value=%s\n",
            phase, env_name, token, value == NULL ? "<unset>" : value);
    return EXIT_ENV;
  }
  return 0;
}

static int
require_env_omits(const char *phase, const char *env_name, const char *token)
{
  const char *value = getenv(env_name);
  if (value != NULL && strstr(value, token) != NULL) {
    fprintf(stderr, "%s: %s contains unexpected token '%s'; value=%s\n",
            phase, env_name, token, value);
    return EXIT_ENV;
  }
  return 0;
}

static int
check_ld_preload_env(const char *phase)
{
  const char *value = getenv("LD_PRELOAD");

  if (value == NULL) {
    fprintf(stderr, "%s: LD_PRELOAD missing; expected '%s' or DMTCP-hidden empty value\n",
            phase, LIB_DMTCP);
    return EXIT_ENV;
  }
  if (strstr(value, LIB_DMTCP_IPC) != NULL) {
    fprintf(stderr, "%s: LD_PRELOAD contains unexpected token '%s'; value=%s\n",
            phase, LIB_DMTCP_IPC, value);
    return EXIT_ENV;
  }
  if (value[0] != '\0' && strstr(value, LIB_DMTCP) == NULL) {
    fprintf(stderr, "%s: LD_PRELOAD missing required token '%s'; value=%s\n",
            phase, LIB_DMTCP, value);
    return EXIT_ENV;
  }
  return 0;
}

static int
check_loaded_libraries(const char *phase)
{
  FILE *maps;
  char line[4096];
  int found_libdmtcp = 0;
  int found_ipc = 0;

  maps = fopen("/proc/self/maps", "r");
  if (maps == NULL) {
    fprintf(stderr, "%s: /proc/self/maps could not be read: %s\n",
            phase, strerror(errno));
    return EXIT_ENV;
  }
  while (fgets(line, sizeof(line), maps) != NULL) {
    if (strstr(line, LIB_DMTCP) != NULL) {
      found_libdmtcp = 1;
    }
    if (strstr(line, LIB_DMTCP_IPC) != NULL) {
      found_ipc = 1;
    }
  }
  fclose(maps);

  if (found_ipc) {
    fprintf(stderr, "%s: loaded libraries contain unexpected '%s'; %s=%s; LD_PRELOAD=%s\n",
            phase, LIB_DMTCP_IPC, ENV_HIJACK_LIBS,
            getenv(ENV_HIJACK_LIBS) == NULL ? "<unset>" : getenv(ENV_HIJACK_LIBS),
            getenv("LD_PRELOAD") == NULL ? "<unset>" : getenv("LD_PRELOAD"));
    return EXIT_ENV;
  }
  if (!found_libdmtcp) {
    fprintf(stderr, "%s: loaded libraries missing '%s'; %s=%s; LD_PRELOAD=%s\n",
            phase, LIB_DMTCP, ENV_HIJACK_LIBS,
            getenv(ENV_HIJACK_LIBS) == NULL ? "<unset>" : getenv(ENV_HIJACK_LIBS),
            getenv("LD_PRELOAD") == NULL ? "<unset>" : getenv("LD_PRELOAD"));
    return strcmp(phase, "after-exec") == 0 ? EXIT_EXEC : EXIT_ENV;
  }
  return 0;
}

static int
check_preload_env(const char *phase)
{
  int rc;
  rc = require_env_contains(phase, ENV_HIJACK_LIBS, LIB_DMTCP);
  if (rc != 0) return rc;
  rc = require_env_omits(phase, ENV_HIJACK_LIBS, LIB_DMTCP_IPC);
  if (rc != 0) return rc;
  rc = check_ld_preload_env(phase);
  if (rc != 0) return rc;
  return check_loaded_libraries(phase);
}

static int
exercise_socket_poll_select(const char *phase)
{
  int sv[2] = { -1, -1 };
  struct pollfd pfd;
  fd_set rfds;
  struct timeval tv;
  char ch = 'x';
  int rc;

  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1) {
    return fail_errno(phase, "socketpair(AF_UNIX)");
  }
  if (write(sv[0], &ch, 1) != 1) {
    rc = fail_errno(phase, "write(socketpair)");
    close(sv[0]); close(sv[1]);
    return rc;
  }
  memset(&pfd, 0, sizeof(pfd));
  pfd.fd = sv[1];
  pfd.events = POLLIN;
  rc = poll(&pfd, 1, 0);
  if (rc == -1 || !(pfd.revents & POLLIN)) {
    fprintf(stderr, "%s: poll(socketpair) did not report POLLIN; rc=%d revents=%d errno=%s\n",
            phase, rc, pfd.revents, strerror(errno));
    close(sv[0]); close(sv[1]);
    return EXIT_IPC;
  }
  FD_ZERO(&rfds);
  FD_SET(sv[1], &rfds);
  tv.tv_sec = 0;
  tv.tv_usec = 0;
  rc = select(sv[1] + 1, &rfds, NULL, NULL, &tv);
  if (rc == -1 || !FD_ISSET(sv[1], &rfds)) {
    fprintf(stderr, "%s: select(socketpair) did not report readability; rc=%d errno=%s\n",
            phase, rc, strerror(errno));
    close(sv[0]); close(sv[1]);
    return EXIT_IPC;
  }
  if (read(sv[1], &ch, 1) != 1) {
    rc = fail_errno(phase, "read(socketpair)");
    close(sv[0]); close(sv[1]);
    return rc;
  }
  close(sv[0]);
  close(sv[1]);
  return 0;
}

static int
exercise_epoll_eventfd(const char *phase)
{
#ifdef __linux__
  int efd = -1;
  int epfd = -1;
  struct epoll_event ev;
  uint64_t value = 1;
  int rc;

  efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (efd == -1) {
    if (errno == ENOSYS) return 0;
    return fail_errno(phase, "eventfd");
  }
  epfd = epoll_create1(EPOLL_CLOEXEC);
  if (epfd == -1) {
    close(efd);
    if (errno == ENOSYS || errno == EINVAL) return 0;
    return fail_errno(phase, "epoll_create1");
  }
  memset(&ev, 0, sizeof(ev));
  ev.events = EPOLLIN;
  ev.data.fd = efd;
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, efd, &ev) == -1) {
    rc = fail_errno(phase, "epoll_ctl(ADD eventfd)");
    close(epfd); close(efd);
    return rc;
  }
  if (write(efd, &value, sizeof(value)) != (ssize_t)sizeof(value)) {
    rc = fail_errno(phase, "write(eventfd)");
    close(epfd); close(efd);
    return rc;
  }
  rc = epoll_wait(epfd, &ev, 1, 0);
  if (rc != 1) {
    fprintf(stderr, "%s: epoll_wait(eventfd) returned %d, expected 1; errno=%s\n",
            phase, rc, strerror(errno));
    close(epfd); close(efd);
    return EXIT_IPC;
  }
  if (read(efd, &value, sizeof(value)) != (ssize_t)sizeof(value)) {
    rc = fail_errno(phase, "read(eventfd)");
    close(epfd); close(efd);
    return rc;
  }
  close(epfd);
  close(efd);
#endif
  return 0;
}

static int
exercise_posix_mqueue(const char *phase)
{
  char name[64];
  struct mq_attr attr;
  mqd_t mq;
  char buf[32];
  int rc = 0;

  snprintf(name, sizeof(name), "/dmtcp-ipc-probe-%ld", (long)getpid());
  mq_unlink(name);
  memset(&attr, 0, sizeof(attr));
  attr.mq_maxmsg = 4;
  attr.mq_msgsize = sizeof(buf);
  mq = mq_open(name, O_CREAT | O_RDWR | O_EXCL, 0600, &attr);
  if (mq == (mqd_t)-1) {
    if (errno == ENOSYS || errno == EACCES || errno == ENOENT) return 0;
    return fail_errno(phase, "mq_open");
  }
  if (mq_send(mq, "ipc", 4, 0) == -1) {
    rc = fail_errno(phase, "mq_send");
    goto out;
  }
  memset(buf, 0, sizeof(buf));
  if (mq_receive(mq, buf, sizeof(buf), NULL) == -1) {
    rc = fail_errno(phase, "mq_receive");
    goto out;
  }
  if (strcmp(buf, "ipc") != 0) {
    fprintf(stderr, "%s: mq_receive payload mismatch: %s\n", phase, buf);
    rc = EXIT_IPC;
  }
out:
  if (mq_close(mq) == -1 && rc == 0) rc = fail_errno(phase, "mq_close");
  if (mq_unlink(name) == -1 && errno != ENOENT && rc == 0) rc = fail_errno(phase, "mq_unlink");
  return rc;
}

static int
exercise_pty(const char *phase)
{
  int master;
  char *slave;

  master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
  if (master == -1) {
    if (errno == ENOENT || errno == ENOSYS || errno == EACCES) return 0;
    return fail_errno(phase, "posix_openpt");
  }
  if (grantpt(master) == -1) {
    int saved = errno;
    close(master);
    errno = saved;
    return fail_errno(phase, "grantpt");
  }
  if (unlockpt(master) == -1) {
    int saved = errno;
    close(master);
    errno = saved;
    return fail_errno(phase, "unlockpt");
  }
  slave = ptsname(master);
  if (slave == NULL) {
    int saved = errno;
    close(master);
    errno = saved;
    return fail_errno(phase, "ptsname");
  }
  close(master);
  return 0;
}

static int
exercise_ipc_wrappers(const char *phase)
{
  int rc;
  rc = exercise_socket_poll_select(phase);
  if (rc != 0) return rc;
  rc = exercise_epoll_eventfd(phase);
  if (rc != 0) return rc;
  rc = exercise_posix_mqueue(phase);
  if (rc != 0) return rc;
  return exercise_pty(phase);
}

int
main(int argc, char *argv[])
{
  const char *after_exec = getenv(ENV_AFTER_EXEC);
  const char *phase = after_exec != NULL && strcmp(after_exec, "1") == 0 ?
                      "after-exec" : "launch";
  int rc;

  (void)argc;

  rc = check_preload_env(phase);
  if (rc != 0) return rc;
  rc = exercise_ipc_wrappers(phase);
  if (rc != 0) return rc;

  if (strcmp(phase, "launch") == 0) {
    if (setenv(ENV_AFTER_EXEC, "1", 1) == -1) {
      fprintf(stderr, "launch: %s could not be set before exec: %s\n",
              ENV_AFTER_EXEC, strerror(errno));
      return EXIT_EXEC;
    }
    fflush(NULL);
    execvp(argv[0], argv);
    fprintf(stderr, "launch: exec reconstruction probe failed for argv[0]=%s: %s\n",
            argv[0], strerror(errno));
    return EXIT_EXEC;
  }

  printf("PASS ipc-built-in-preload: launch and exec preload state valid%s\n",
         getenv(ENV_DISABLE_ALL_PLUGINS) != NULL &&
         strcmp(getenv(ENV_DISABLE_ALL_PLUGINS), "1") == 0 ?
         " (disable-all)" : "");
  return EXIT_SUCCESS;
}
