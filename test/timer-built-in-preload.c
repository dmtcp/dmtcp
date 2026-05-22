/*
 * Runtime preload probe for the built-in timer wrappers.
 *
 * Run under bin/dmtcp_launch.  The probe validates both the launch-time
 * environment and the post-exec environment reconstructed by execwrappers.cpp.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define ENV_HIJACK_LIBS "DMTCP_HIJACK_LIBS"
#define ENV_DISABLE_ALL_PLUGINS "DMTCP_DISABLE_ALL_PLUGINS"
#define ENV_AFTER_EXEC "DMTCP_TIMER_BUILT_IN_PRELOAD_AFTER_EXEC"
#define LIB_DMTCP "libdmtcp.so"
#define LIB_DMTCP_TIMER "libdmtcp_timer.so"

#define EXIT_ENV 2
#define EXIT_TIMER 3
#define EXIT_EXEC 4

static int
require_env_contains(const char *phase, const char *env_name,
                     const char *token)
{
  const char *value = getenv(env_name);
  if (value == NULL || strstr(value, token) == NULL) {
    fprintf(stderr,
            "%s: %s missing required token '%s'; value=%s\n",
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
    fprintf(stderr,
            "%s: %s contains unexpected token '%s'; value=%s\n",
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
    fprintf(stderr,
            "%s: LD_PRELOAD missing; expected '%s' or DMTCP-hidden empty "
            "value; value=<unset>\n",
            phase, LIB_DMTCP);
    return EXIT_ENV;
  }

  if (strstr(value, LIB_DMTCP_TIMER) != NULL) {
    fprintf(stderr,
            "%s: LD_PRELOAD contains unexpected token '%s'; value=%s\n",
            phase, LIB_DMTCP_TIMER, value);
    return EXIT_ENV;
  }

  if (value[0] != '\0' && strstr(value, LIB_DMTCP) == NULL) {
    fprintf(stderr,
            "%s: LD_PRELOAD missing required token '%s'; value=%s\n",
            phase, LIB_DMTCP, value);
    return EXIT_ENV;
  }

  return 0;
}

static int
check_loaded_libdmtcp(const char *phase)
{
  FILE *maps;
  char line[4096];
  int found = 0;

  maps = fopen("/proc/self/maps", "r");
  if (maps == NULL) {
    fprintf(stderr,
            "%s: /proc/self/maps could not be read while checking '%s': %s\n",
            phase, LIB_DMTCP, strerror(errno));
    return EXIT_ENV;
  }

  while (fgets(line, sizeof(line), maps) != NULL) {
    if (strstr(line, LIB_DMTCP) != NULL) {
      found = 1;
      break;
    }
  }
  fclose(maps);

  if (!found) {
    fprintf(stderr,
            "%s: loaded libraries missing '%s'; %s=%s; LD_PRELOAD=%s\n",
            phase, LIB_DMTCP, ENV_HIJACK_LIBS,
            getenv(ENV_HIJACK_LIBS) == NULL ?
            "<unset>" : getenv(ENV_HIJACK_LIBS),
            getenv("LD_PRELOAD") == NULL ?
            "<unset>" : getenv("LD_PRELOAD"));
    return strcmp(phase, "after-exec") == 0 ? EXIT_EXEC : EXIT_ENV;
  }

  return 0;
}

static int
check_preload_env(const char *phase)
{
  int rc;

  rc = require_env_contains(phase, ENV_HIJACK_LIBS, LIB_DMTCP);
  if (rc != 0) {
    return rc;
  }
  rc = require_env_omits(phase, ENV_HIJACK_LIBS, LIB_DMTCP_TIMER);
  if (rc != 0) {
    return rc;
  }
  rc = check_ld_preload_env(phase);
  if (rc != 0) {
    return rc;
  }
  rc = check_loaded_libdmtcp(phase);
  if (rc != 0) {
    return rc;
  }

  return 0;
}

static int
touch_timer_path(const char *phase)
{
  timer_t timerid;
  struct itimerspec its;

  memset(&its, 0, sizeof(its));

  if (timer_create(CLOCK_REALTIME, NULL, &timerid) == -1) {
    fprintf(stderr,
            "%s: timer_create(CLOCK_REALTIME) failed: %s\n",
            phase, strerror(errno));
    return EXIT_TIMER;
  }

  if (timer_settime(timerid, 0, &its, NULL) == -1) {
    int saved_errno = errno;
    timer_delete(timerid);
    fprintf(stderr,
            "%s: timer_settime(CLOCK_REALTIME) failed: %s\n",
            phase, strerror(saved_errno));
    return EXIT_TIMER;
  }

  if (timer_delete(timerid) == -1) {
    fprintf(stderr,
            "%s: timer_delete(CLOCK_REALTIME) failed: %s\n",
            phase, strerror(errno));
    return EXIT_TIMER;
  }

  return 0;
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
  if (rc != 0) {
    return rc;
  }

  rc = touch_timer_path(phase);
  if (rc != 0) {
    return rc;
  }

  if (strcmp(phase, "launch") == 0) {
    if (setenv(ENV_AFTER_EXEC, "1", 1) == -1) {
      fprintf(stderr,
              "launch: %s could not be set before exec: %s\n",
              ENV_AFTER_EXEC, strerror(errno));
      return EXIT_EXEC;
    }
    fflush(NULL);
    execvp(argv[0], argv);
    fprintf(stderr,
            "launch: exec reconstruction probe failed for argv[0]=%s: %s\n",
            argv[0], strerror(errno));
    return EXIT_EXEC;
  }

  printf("PASS timer-built-in-preload: launch and exec preload state valid%s\n",
         getenv(ENV_DISABLE_ALL_PLUGINS) != NULL &&
         strcmp(getenv(ENV_DISABLE_ALL_PLUGINS), "1") == 0 ?
         " (disable-all)" : "");
  return EXIT_SUCCESS;
}
