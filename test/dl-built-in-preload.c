/*
 * Runtime preload probe for the built-in DL wrapper group.
 *
 * Run under bin/dmtcp_launch.  The probe validates launch-time and post-exec
 * preload state, prints only bounded DMTCP preload/control diagnostics on
 * failure, checks loaded-library map evidence, and exercises cheap
 * dlopen/dlsym/dlclose paths.  It is safe to run in normal, DL-disabled,
 * disable-all, original-user-LD_PRELOAD, and external-plugin modes.
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ENV_HIJACK_LIBS "DMTCP_HIJACK_LIBS"
#define ENV_HIJACK_LIBS_M32 "DMTCP_HIJACK_LIBS_M32"
#define ENV_ORIG_LD_PRELOAD "DMTCP_ORIG_LD_PRELOAD"
#define ENV_PLUGIN "DMTCP_PLUGIN"
#define ENV_DL_PLUGIN "DMTCP_DL_PLUGIN"
#define ENV_DISABLE_ALL_PLUGINS "DMTCP_DISABLE_ALL_PLUGINS"
#define ENV_AFTER_EXEC "DMTCP_DL_BUILT_IN_PRELOAD_AFTER_EXEC"
#define LIB_DMTCP "libdmtcp.so"
#define LIB_DMTCP_DL "libdmtcp_dl.so"
#define LIB_DLOPEN_HELPER "libdlopen-lib1.so"
#define PATH_DLOPEN_HELPER "test/libdlopen-lib1.so"
#define LIB_USER_PRELOAD "libdl-built-in-user-preload.so"
#define LIB_EXTERNAL_PLUGIN "libdmtcp_plugin-init.so"
#define USER_PRELOAD_MARKER "dmtcp_dl_built_in_user_preload_marker"

#define EXIT_ENV 2
#define EXIT_DL 3
#define EXIT_EXEC 4

typedef int (*helper_fnc_t)(int result[2]);
typedef int (*user_marker_t)(void);

struct map_evidence {
  int found_libdmtcp;
  int found_dl;
  int found_user_preload;
  int found_external_plugin;
  char libdmtcp_line[4096];
  char dl_line[4096];
  char user_preload_line[4096];
  char external_plugin_line[4096];
};

static const char *
env_or_unset(const char *name)
{
  const char *value = getenv(name);
  return value == NULL ? "<unset>" : value;
}

static void
print_limited_env(const char *phase, const char *mode)
{
  fprintf(stderr,
          "%s: mode=%s env LD_PRELOAD=%s; %s=%s; %s=%s; %s=%s; %s=%s; %s=%s; %s=%s\n",
          phase, mode,
          env_or_unset("LD_PRELOAD"),
          ENV_HIJACK_LIBS, env_or_unset(ENV_HIJACK_LIBS),
          ENV_HIJACK_LIBS_M32, env_or_unset(ENV_HIJACK_LIBS_M32),
          ENV_ORIG_LD_PRELOAD, env_or_unset(ENV_ORIG_LD_PRELOAD),
          ENV_PLUGIN, env_or_unset(ENV_PLUGIN),
          ENV_DL_PLUGIN, env_or_unset(ENV_DL_PLUGIN),
          ENV_DISABLE_ALL_PLUGINS, env_or_unset(ENV_DISABLE_ALL_PLUGINS));
}

static int
fail_with_env(const char *phase, const char *mode, int code, const char *fmt, ...)
{
  va_list ap;

  fprintf(stderr, "%s: mode=%s: ", phase, mode);
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fprintf(stderr, "\n");
  print_limited_env(phase, mode);
  return code;
}

static int
fail_env_name_only(const char *phase, const char *mode, const char *env_name)
{
  fprintf(stderr,
          "%s: mode=%s: %s has malformed value; expected 0, 1, or unset\n",
          phase, mode, env_name);
  return EXIT_ENV;
}

static int
fail_dlerror(const char *phase, const char *mode, const char *operation)
{
  const char *err = dlerror();
  return fail_with_env(phase, mode, EXIT_DL, "%s failed: %s", operation,
                       err == NULL ? "<no dlerror>" : err);
}

static int
require_env_contains(const char *phase, const char *mode,
                     const char *env_name, const char *token)
{
  const char *value = getenv(env_name);
  if (value == NULL || strstr(value, token) == NULL) {
    return fail_with_env(phase, mode, EXIT_ENV,
                         "%s missing required token '%s'", env_name, token);
  }
  return 0;
}

static int
require_env_omits(const char *phase, const char *mode,
                  const char *env_name, const char *token)
{
  const char *value = getenv(env_name);
  if (value != NULL && strstr(value, token) != NULL) {
    return fail_with_env(phase, mode, EXIT_ENV,
                         "%s contains unexpected token '%s'", env_name, token);
  }
  return 0;
}

static int
require_binary_env(const char *phase, const char *mode, const char *env_name)
{
  const char *value = getenv(env_name);
  if (value != NULL && strcmp(value, "0") != 0 && strcmp(value, "1") != 0) {
    return fail_env_name_only(phase, mode, env_name);
  }
  return 0;
}

static int
check_disable_all_preload_shape(const char *phase, const char *mode)
{
  const char *disabled = getenv(ENV_DISABLE_ALL_PLUGINS);
  const char *hijack = getenv(ENV_HIJACK_LIBS);
  const char *cursor;
  int count = 0;

  if (disabled == NULL || strcmp(disabled, "1") != 0) {
    return 0;
  }
  if (hijack == NULL) {
    return fail_with_env(phase, mode, EXIT_ENV,
                         "disable-all mode missing %s", ENV_HIJACK_LIBS);
  }
  cursor = hijack;
  while ((cursor = strstr(cursor, "libdmtcp")) != NULL) {
    count++;
    cursor += strlen("libdmtcp");
  }
  if (count != 1 || strstr(hijack, LIB_DMTCP) == NULL) {
    return fail_with_env(phase, mode, EXIT_ENV,
                         "disable-all mode expected only %s in %s",
                         LIB_DMTCP, ENV_HIJACK_LIBS);
  }
  return 0;
}

static int
check_ld_preload_env(const char *phase, const char *mode)
{
  const char *value = getenv("LD_PRELOAD");

  if (value == NULL) {
    return fail_with_env(phase, mode, EXIT_ENV,
                         "LD_PRELOAD missing; expected DMTCP-hidden empty value, '%s', or restored user preload",
                         LIB_DMTCP);
  }
  if (strstr(value, LIB_DMTCP_DL) != NULL) {
    return fail_with_env(phase, mode, EXIT_ENV,
                         "LD_PRELOAD contains unexpected token '%s'",
                         LIB_DMTCP_DL);
  }
  if (value[0] != '\0' && strstr(value, LIB_DMTCP) == NULL) {
    if (strcmp(mode, "user-preload") == 0 &&
        strstr(value, LIB_USER_PRELOAD) != NULL) {
      return 0;
    }
    return fail_with_env(phase, mode, EXIT_ENV,
                         "LD_PRELOAD missing required token '%s' or restored user preload token '%s'",
                         LIB_DMTCP, LIB_USER_PRELOAD);
  }
  return 0;
}

static void
remember_line(char *dst, size_t dst_size, const char *line)
{
  if (dst[0] == '\0') {
    snprintf(dst, dst_size, "%s", line);
  }
}

static int
read_loaded_libraries(const char *phase, const char *mode,
                      struct map_evidence *evidence)
{
  FILE *maps;
  char line[4096];

  memset(evidence, 0, sizeof(*evidence));
  maps = fopen("/proc/self/maps", "r");
  if (maps == NULL) {
    return fail_with_env(phase, mode, EXIT_ENV,
                         "/proc/self/maps could not be read: %s",
                         strerror(errno));
  }
  while (fgets(line, sizeof(line), maps) != NULL) {
    if (strstr(line, LIB_DMTCP) != NULL) {
      evidence->found_libdmtcp = 1;
      remember_line(evidence->libdmtcp_line, sizeof(evidence->libdmtcp_line), line);
    }
    if (strstr(line, LIB_DMTCP_DL) != NULL) {
      evidence->found_dl = 1;
      remember_line(evidence->dl_line, sizeof(evidence->dl_line), line);
    }
    if (strstr(line, LIB_USER_PRELOAD) != NULL) {
      evidence->found_user_preload = 1;
      remember_line(evidence->user_preload_line,
                    sizeof(evidence->user_preload_line), line);
    }
    if (strstr(line, LIB_EXTERNAL_PLUGIN) != NULL) {
      evidence->found_external_plugin = 1;
      remember_line(evidence->external_plugin_line,
                    sizeof(evidence->external_plugin_line), line);
    }
  }
  if (ferror(maps)) {
    int saved_errno = errno;
    fclose(maps);
    errno = saved_errno;
    return fail_with_env(phase, mode, EXIT_ENV,
                         "/proc/self/maps read failed: %s", strerror(errno));
  }
  fclose(maps);
  return 0;
}

static void
print_map_line(const char *phase, const char *mode,
               const char *label, const char *line)
{
  fprintf(stderr, "%s: mode=%s maps evidence %s=%s", phase, mode, label,
          line[0] == '\0' ? "<missing>\n" : line);
}

static int
check_loaded_libraries(const char *phase, const char *mode,
                       struct map_evidence *evidence)
{
  int rc = read_loaded_libraries(phase, mode, evidence);
  if (rc != 0) {
    return rc;
  }
  if (evidence->found_dl) {
    fprintf(stderr, "%s: mode=%s: loaded libraries contain unexpected '%s'\n",
            phase, mode, LIB_DMTCP_DL);
    print_map_line(phase, mode, "dl", evidence->dl_line);
    print_map_line(phase, mode, "libdmtcp", evidence->libdmtcp_line);
    print_limited_env(phase, mode);
    return EXIT_ENV;
  }
  if (!evidence->found_libdmtcp) {
    fprintf(stderr, "%s: mode=%s: loaded libraries missing '%s'\n",
            phase, mode, LIB_DMTCP);
    print_map_line(phase, mode, "libdmtcp", evidence->libdmtcp_line);
    print_limited_env(phase, mode);
    return strcmp(phase, "after-exec") == 0 ? EXIT_EXEC : EXIT_ENV;
  }
  return 0;
}

static int
check_user_preload_mode(const char *phase, const char *mode,
                        const struct map_evidence *evidence)
{
  user_marker_t marker;
  void *sym;
  const char *orig;

  if (strcmp(mode, "user-preload") != 0) {
    return 0;
  }

  orig = getenv(ENV_ORIG_LD_PRELOAD);
  if (orig == NULL || strstr(orig, LIB_USER_PRELOAD) == NULL) {
    return fail_with_env(phase, mode, EXIT_ENV,
                         "%s missing required token '%s'",
                         ENV_ORIG_LD_PRELOAD, LIB_USER_PRELOAD);
  }
  if (!evidence->found_user_preload) {
    fprintf(stderr, "%s: mode=%s: loaded libraries missing '%s'\n",
            phase, mode, LIB_USER_PRELOAD);
    print_map_line(phase, mode, "user-preload", evidence->user_preload_line);
    print_limited_env(phase, mode);
    return EXIT_ENV;
  }

  dlerror();
  sym = dlsym(RTLD_DEFAULT, USER_PRELOAD_MARKER);
  if (sym == NULL || dlerror() != NULL) {
    return fail_dlerror(phase, mode, "dlsym(user preload marker)");
  }
  memcpy(&marker, &sym, sizeof(marker));
  if (marker() != 4242) {
    return fail_with_env(phase, mode, EXIT_ENV,
                         "user preload marker returned unexpected value");
  }
  return 0;
}

static int
check_external_plugin_mode(const char *phase, const char *mode,
                           const struct map_evidence *evidence)
{
  const char *plugin;

  if (strcmp(mode, "external-plugin") != 0) {
    return 0;
  }

  plugin = getenv(ENV_PLUGIN);
  if (plugin == NULL || strstr(plugin, LIB_EXTERNAL_PLUGIN) == NULL) {
    return fail_with_env(phase, mode, EXIT_ENV,
                         "%s missing required token '%s'",
                         ENV_PLUGIN, LIB_EXTERNAL_PLUGIN);
  }
  if (!evidence->found_external_plugin) {
    fprintf(stderr, "%s: mode=%s: loaded libraries missing '%s'\n",
            phase, mode, LIB_EXTERNAL_PLUGIN);
    print_map_line(phase, mode, "external-plugin",
                   evidence->external_plugin_line);
    print_limited_env(phase, mode);
    return EXIT_ENV;
  }
  return 0;
}

static int
check_preload_env(const char *phase, const char *mode)
{
  struct map_evidence evidence;
  int rc;

  rc = require_binary_env(phase, mode, ENV_DL_PLUGIN);
  if (rc != 0) return rc;
  rc = require_binary_env(phase, mode, ENV_DISABLE_ALL_PLUGINS);
  if (rc != 0) return rc;
  rc = require_env_contains(phase, mode, ENV_HIJACK_LIBS, LIB_DMTCP);
  if (rc != 0) return rc;
  rc = require_env_omits(phase, mode, ENV_HIJACK_LIBS, LIB_DMTCP_DL);
  if (rc != 0) return rc;
  rc = require_env_omits(phase, mode, ENV_HIJACK_LIBS_M32, LIB_DMTCP_DL);
  if (rc != 0) return rc;
  rc = check_disable_all_preload_shape(phase, mode);
  if (rc != 0) return rc;
  rc = check_ld_preload_env(phase, mode);
  if (rc != 0) return rc;
  rc = check_loaded_libraries(phase, mode, &evidence);
  if (rc != 0) return rc;
  rc = check_user_preload_mode(phase, mode, &evidence);
  if (rc != 0) return rc;
  return check_external_plugin_mode(phase, mode, &evidence);
}

static int
exercise_dl_wrappers(const char *phase, const char *mode)
{
  void *main_handle;
  void *helper_handle;
  void *sym;
  helper_fnc_t fnc;
  int result[2] = { 0, 0 };
  int answer;

  dlerror();
  main_handle = dlopen(NULL, RTLD_NOW);
  if (main_handle == NULL) {
    return fail_dlerror(phase, mode, "dlopen(NULL)");
  }
  if (dlclose(main_handle) != 0) {
    return fail_dlerror(phase, mode, "dlclose(dlopen(NULL))");
  }

  dlerror();
  helper_handle = dlopen(PATH_DLOPEN_HELPER, RTLD_NOW);
  if (helper_handle == NULL) {
    return fail_dlerror(phase, mode, "dlopen(" PATH_DLOPEN_HELPER ")");
  }

  dlerror();
  sym = dlsym(helper_handle, "fnc");
  if (sym == NULL || dlerror() != NULL) {
    dlclose(helper_handle);
    return fail_dlerror(phase, mode, "dlsym(fnc)");
  }
  memcpy(&fnc, &sym, sizeof(fnc));
  answer = fnc(result);
  if (answer != 1 || result[0] != 1) {
    dlclose(helper_handle);
    return fail_with_env(phase, mode, EXIT_DL,
                         "fnc returned wrong answer: answer=%d result0=%d",
                         answer, result[0]);
  }

  if (dlclose(helper_handle) != 0) {
    return fail_dlerror(phase, mode, "dlclose(" LIB_DLOPEN_HELPER ")");
  }
  return 0;
}

int
main(int argc, char *argv[])
{
  const char *after_exec = getenv(ENV_AFTER_EXEC);
  const char *phase = after_exec != NULL && strcmp(after_exec, "1") == 0 ?
                      "after-exec" : "launch";
  const char *mode = argc > 1 ? argv[1] : "normal";
  int rc;

  rc = check_preload_env(phase, mode);
  if (rc != 0) return rc;
  rc = exercise_dl_wrappers(phase, mode);
  if (rc != 0) return rc;

  if (strcmp(phase, "launch") == 0) {
    if (setenv(ENV_AFTER_EXEC, "1", 1) == -1) {
      return fail_with_env("launch", mode, EXIT_EXEC,
                           "%s could not be set before exec: %s",
                           ENV_AFTER_EXEC, strerror(errno));
    }
    fflush(NULL);
    execvp(argv[0], argv);
    return fail_with_env("launch", mode, EXIT_EXEC,
                         "exec reconstruction probe failed for argv[0]=%s: %s",
                         argv[0], strerror(errno));
  }

  printf("PASS dl-built-in-preload: mode=%s launch and exec preload state valid\n",
         mode);
  return EXIT_SUCCESS;
}
