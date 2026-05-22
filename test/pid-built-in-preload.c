/*
 * Runtime preload probe for the built-in PID plugin.
 *
 * Run under bin/dmtcp_launch.  The probe validates launch-time and post-exec
 * preload state and touches cheap PID-sensitive wrapper paths.  It is safe to
 * run in normal and --disable-all-plugins modes.
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define ENV_HIJACK_LIBS "DMTCP_HIJACK_LIBS"
#define ENV_HIJACK_LIBS_M32 "DMTCP_HIJACK_LIBS_M32"
#define ENV_DISABLE_ALL_PLUGINS "DMTCP_DISABLE_ALL_PLUGINS"
#define ENV_AFTER_EXEC "DMTCP_PID_BUILT_IN_PRELOAD_AFTER_EXEC"
#define LIB_DMTCP "libdmtcp.so"
#define LIB_DMTCP_PID "libdmtcp_pid.so"

#define EXIT_ENV 2
#define EXIT_PID 3
#define EXIT_EXEC 4

static int
fail_errno(const char *phase, const char *operation)
{
  fprintf(stderr, "%s: %s failed: %s\n", phase, operation, strerror(errno));
  return EXIT_PID;
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
check_disable_all_preload_shape(const char *phase)
{
  const char *disabled = getenv(ENV_DISABLE_ALL_PLUGINS);
  const char *hijack = getenv(ENV_HIJACK_LIBS);
  const char *cursor;
  int count = 0;

  if (disabled == NULL || strcmp(disabled, "1") != 0) {
    return 0;
  }
  if (hijack == NULL) {
    fprintf(stderr, "%s: disable-all mode missing %s\n", phase, ENV_HIJACK_LIBS);
    return EXIT_ENV;
  }
  cursor = hijack;
  while ((cursor = strstr(cursor, "libdmtcp")) != NULL) {
    count++;
    cursor += strlen("libdmtcp");
  }
  if (count != 1 || strstr(hijack, LIB_DMTCP) == NULL) {
    fprintf(stderr, "%s: disable-all mode expected only %s in %s; value=%s\n",
            phase, LIB_DMTCP, ENV_HIJACK_LIBS, hijack);
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
  if (strstr(value, LIB_DMTCP_PID) != NULL) {
    fprintf(stderr, "%s: LD_PRELOAD contains unexpected token '%s'; value=%s\n",
            phase, LIB_DMTCP_PID, value);
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
  int found_pid = 0;

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
    if (strstr(line, LIB_DMTCP_PID) != NULL) {
      found_pid = 1;
    }
  }
  fclose(maps);

  if (found_pid) {
    fprintf(stderr, "%s: loaded libraries contain unexpected '%s'; %s=%s; LD_PRELOAD=%s\n",
            phase, LIB_DMTCP_PID, ENV_HIJACK_LIBS,
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
  rc = require_env_omits(phase, ENV_HIJACK_LIBS, LIB_DMTCP_PID);
  if (rc != 0) return rc;
  rc = require_env_omits(phase, ENV_HIJACK_LIBS_M32, LIB_DMTCP_PID);
  if (rc != 0) return rc;
  rc = check_disable_all_preload_shape(phase);
  if (rc != 0) return rc;
  rc = check_ld_preload_env(phase);
  if (rc != 0) return rc;
  return check_loaded_libraries(phase);
}

static int
exercise_pid_identity(const char *phase)
{
  pid_t libc_pid = getpid();
  pid_t libc_ppid = getppid();
  long syscall_pid = syscall(SYS_getpid);
#ifdef SYS_gettid
  long syscall_tid = syscall(SYS_gettid);
#endif

  if (libc_pid <= 0 || libc_ppid <= 0) {
    fprintf(stderr, "%s: invalid getpid/getppid result: pid=%ld ppid=%ld\n",
            phase, (long)libc_pid, (long)libc_ppid);
    return EXIT_PID;
  }
  if (syscall_pid <= 0) {
    fprintf(stderr, "%s: invalid syscall(SYS_getpid) result: %ld\n",
            phase, syscall_pid);
    return EXIT_PID;
  }
#ifdef SYS_gettid
  if (syscall_tid <= 0) {
    fprintf(stderr, "%s: invalid syscall(SYS_gettid) result: %ld\n",
            phase, syscall_tid);
    return EXIT_PID;
  }
#endif
  return 0;
}

static int
exercise_fork_wait(const char *phase)
{
  pid_t child;
  int status = 0;

  child = fork();
  if (child == -1) {
    return fail_errno(phase, "fork");
  }
  if (child == 0) {
    _exit(17);
  }
  if (waitpid(child, &status, 0) != child) {
    return fail_errno(phase, "waitpid(child)");
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 17) {
    fprintf(stderr, "%s: child status mismatch: status=%d\n", phase, status);
    return EXIT_PID;
  }
  return 0;
}

static int
exercise_fcntl_owner(const char *phase)
{
  int sv[2] = { -1, -1 };
  int owner;
  int rc = 0;
  pid_t self = getpid();

  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1) {
    return fail_errno(phase, "socketpair(AF_UNIX)");
  }
  if (fcntl(sv[0], F_SETOWN, self) == -1) {
    rc = fail_errno(phase, "fcntl(F_SETOWN)");
    goto out;
  }
  owner = fcntl(sv[0], F_GETOWN);
  if (owner == -1) {
    rc = fail_errno(phase, "fcntl(F_GETOWN)");
    goto out;
  }
  if (owner != self) {
    fprintf(stderr, "%s: fcntl owner mismatch: got=%ld expected=%ld\n",
            phase, (long)owner, (long)self);
    rc = EXIT_PID;
  }
out:
  close(sv[0]);
  close(sv[1]);
  return rc;
}

static int
exercise_pid_wrappers(const char *phase)
{
  int rc;
  rc = exercise_pid_identity(phase);
  if (rc != 0) return rc;
  rc = exercise_fork_wait(phase);
  if (rc != 0) return rc;
  return exercise_fcntl_owner(phase);
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
  rc = exercise_pid_wrappers(phase);
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

  printf("PASS pid-built-in-preload: launch and exec preload state valid%s\n",
         getenv(ENV_DISABLE_ALL_PLUGINS) != NULL &&
         strcmp(getenv(ENV_DISABLE_ALL_PLUGINS), "1") == 0 ?
         " (disable-all)" : "");
  return EXIT_SUCCESS;
}
