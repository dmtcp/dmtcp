/*
 * Runtime preload probe for the built-in alloc wrapper group.
 *
 * Run under bin/dmtcp_launch.  The probe validates launch-time and post-exec
 * preload state, prints only DMTCP preload/control diagnostics on failure, and
 * touches cheap malloc-family and mmap-family wrapper paths.  It is safe to run
 * in normal, alloc-disabled, and --disable-all-plugins modes.
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define ENV_HIJACK_LIBS "DMTCP_HIJACK_LIBS"
#define ENV_HIJACK_LIBS_M32 "DMTCP_HIJACK_LIBS_M32"
#define ENV_ALLOC_PLUGIN "DMTCP_ALLOC_PLUGIN"
#define ENV_DISABLE_ALL_PLUGINS "DMTCP_DISABLE_ALL_PLUGINS"
#define ENV_AFTER_EXEC "DMTCP_ALLOC_BUILT_IN_PRELOAD_AFTER_EXEC"
#define LIB_DMTCP "libdmtcp.so"
#define LIB_DMTCP_ALLOC "libdmtcp_alloc.so"

#define EXIT_ENV 2
#define EXIT_ALLOC 3
#define EXIT_EXEC 4

struct map_evidence {
  int found_libdmtcp;
  int found_alloc;
  char libdmtcp_line[4096];
  char alloc_line[4096];
};

static const char *
env_or_unset(const char *name)
{
  const char *value = getenv(name);
  return value == NULL ? "<unset>" : value;
}

static void
print_limited_env(const char *phase)
{
  fprintf(stderr,
          "%s: env LD_PRELOAD=%s; %s=%s; %s=%s; %s=%s; %s=%s\n",
          phase,
          env_or_unset("LD_PRELOAD"),
          ENV_HIJACK_LIBS, env_or_unset(ENV_HIJACK_LIBS),
          ENV_HIJACK_LIBS_M32, env_or_unset(ENV_HIJACK_LIBS_M32),
          ENV_ALLOC_PLUGIN, env_or_unset(ENV_ALLOC_PLUGIN),
          ENV_DISABLE_ALL_PLUGINS, env_or_unset(ENV_DISABLE_ALL_PLUGINS));
}

static int
fail_with_env(const char *phase, int code, const char *fmt, ...)
{
  va_list ap;

  fprintf(stderr, "%s: ", phase);
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fprintf(stderr, "\n");
  print_limited_env(phase);
  return code;
}

static int
fail_errno(const char *phase, const char *operation)
{
  return fail_with_env(phase, EXIT_ALLOC, "%s failed: %s", operation,
                       strerror(errno));
}

static int
require_env_contains(const char *phase, const char *env_name, const char *token)
{
  const char *value = getenv(env_name);
  if (value == NULL || strstr(value, token) == NULL) {
    return fail_with_env(phase, EXIT_ENV,
                         "%s missing required token '%s'", env_name, token);
  }
  return 0;
}

static int
require_env_omits(const char *phase, const char *env_name, const char *token)
{
  const char *value = getenv(env_name);
  if (value != NULL && strstr(value, token) != NULL) {
    return fail_with_env(phase, EXIT_ENV,
                         "%s contains unexpected token '%s'", env_name, token);
  }
  return 0;
}

static int
require_binary_env(const char *phase, const char *env_name)
{
  const char *value = getenv(env_name);
  if (value != NULL && strcmp(value, "0") != 0 && strcmp(value, "1") != 0) {
    return fail_with_env(phase, EXIT_ENV,
                         "%s has malformed value; expected 0, 1, or unset",
                         env_name);
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
    return fail_with_env(phase, EXIT_ENV, "disable-all mode missing %s",
                         ENV_HIJACK_LIBS);
  }
  cursor = hijack;
  while ((cursor = strstr(cursor, "libdmtcp")) != NULL) {
    count++;
    cursor += strlen("libdmtcp");
  }
  if (count != 1 || strstr(hijack, LIB_DMTCP) == NULL) {
    return fail_with_env(phase, EXIT_ENV,
                         "disable-all mode expected only %s in %s",
                         LIB_DMTCP, ENV_HIJACK_LIBS);
  }
  return 0;
}

static int
check_ld_preload_env(const char *phase)
{
  const char *value = getenv("LD_PRELOAD");

  if (value == NULL) {
    return fail_with_env(phase, EXIT_ENV,
                         "LD_PRELOAD missing; expected '%s' or DMTCP-hidden empty value",
                         LIB_DMTCP);
  }
  if (strstr(value, LIB_DMTCP_ALLOC) != NULL) {
    return fail_with_env(phase, EXIT_ENV,
                         "LD_PRELOAD contains unexpected token '%s'",
                         LIB_DMTCP_ALLOC);
  }
  if (value[0] != '\0' && strstr(value, LIB_DMTCP) == NULL) {
    return fail_with_env(phase, EXIT_ENV,
                         "LD_PRELOAD missing required token '%s'", LIB_DMTCP);
  }
  return 0;
}

static int
read_loaded_libraries(const char *phase, struct map_evidence *evidence)
{
  FILE *maps;
  char line[4096];

  memset(evidence, 0, sizeof(*evidence));
  maps = fopen("/proc/self/maps", "r");
  if (maps == NULL) {
    return fail_with_env(phase, EXIT_ENV,
                         "/proc/self/maps could not be read: %s",
                         strerror(errno));
  }
  while (fgets(line, sizeof(line), maps) != NULL) {
    if (strstr(line, LIB_DMTCP) != NULL) {
      evidence->found_libdmtcp = 1;
      if (evidence->libdmtcp_line[0] == '\0') {
        snprintf(evidence->libdmtcp_line, sizeof(evidence->libdmtcp_line),
                 "%s", line);
      }
    }
    if (strstr(line, LIB_DMTCP_ALLOC) != NULL) {
      evidence->found_alloc = 1;
      if (evidence->alloc_line[0] == '\0') {
        snprintf(evidence->alloc_line, sizeof(evidence->alloc_line),
                 "%s", line);
      }
    }
  }
  if (ferror(maps)) {
    int saved_errno = errno;
    fclose(maps);
    errno = saved_errno;
    return fail_with_env(phase, EXIT_ENV,
                         "/proc/self/maps read failed: %s", strerror(errno));
  }
  fclose(maps);
  return 0;
}

static int
check_loaded_libraries(const char *phase)
{
  struct map_evidence evidence;
  int rc = read_loaded_libraries(phase, &evidence);
  if (rc != 0) {
    return rc;
  }
  if (evidence.found_alloc) {
    fprintf(stderr, "%s: loaded libraries contain unexpected '%s'\n",
            phase, LIB_DMTCP_ALLOC);
    fprintf(stderr, "%s: maps evidence alloc=%s", phase,
            evidence.alloc_line[0] == '\0' ? "<missing>\n" : evidence.alloc_line);
    fprintf(stderr, "%s: maps evidence libdmtcp=%s", phase,
            evidence.libdmtcp_line[0] == '\0' ? "<missing>\n" : evidence.libdmtcp_line);
    print_limited_env(phase);
    return EXIT_ENV;
  }
  if (!evidence.found_libdmtcp) {
    fprintf(stderr, "%s: loaded libraries missing '%s'\n", phase, LIB_DMTCP);
    fprintf(stderr, "%s: maps evidence libdmtcp=<missing>\n", phase);
    print_limited_env(phase);
    return strcmp(phase, "after-exec") == 0 ? EXIT_EXEC : EXIT_ENV;
  }
  return 0;
}

static int
check_preload_env(const char *phase)
{
  int rc;

  rc = require_binary_env(phase, ENV_ALLOC_PLUGIN);
  if (rc != 0) return rc;
  rc = require_binary_env(phase, ENV_DISABLE_ALL_PLUGINS);
  if (rc != 0) return rc;
  rc = require_env_contains(phase, ENV_HIJACK_LIBS, LIB_DMTCP);
  if (rc != 0) return rc;
  rc = require_env_omits(phase, ENV_HIJACK_LIBS, LIB_DMTCP_ALLOC);
  if (rc != 0) return rc;
  rc = require_env_omits(phase, ENV_HIJACK_LIBS_M32, LIB_DMTCP_ALLOC);
  if (rc != 0) return rc;
  rc = check_disable_all_preload_shape(phase);
  if (rc != 0) return rc;
  rc = check_ld_preload_env(phase);
  if (rc != 0) return rc;
  return check_loaded_libraries(phase);
}

static int
exercise_alloc_wrappers(const char *phase)
{
  char *p;
  char *c;
  char *r;
  void *aligned = NULL;
  void *posix_aligned = NULL;
  void *page_aligned;
  long page_size;
  void *mapping;

  p = (char *)malloc(32);
  if (p == NULL) return fail_errno(phase, "malloc(32)");
  memset(p, 0x41, 32);

  c = (char *)calloc(4, 16);
  if (c == NULL) {
    free(p);
    return fail_errno(phase, "calloc(4, 16)");
  }

  r = (char *)realloc(p, 64);
  if (r == NULL) {
    free(c);
    free(p);
    return fail_errno(phase, "realloc(p, 64)");
  }
  p = r;
  memset(p + 32, 0x42, 32);

  aligned = memalign(2 * sizeof(void *), 128);
  if (aligned == NULL) {
    free(c);
    free(p);
    return fail_errno(phase, "memalign(2 * sizeof(void *), 128)");
  }

  if (posix_memalign(&posix_aligned, 64, 128) != 0 || posix_aligned == NULL) {
    free(aligned);
    free(c);
    free(p);
    return fail_errno(phase, "posix_memalign(&ptr, 64, 128)");
  }

  page_aligned = valloc(128);
  if (page_aligned == NULL) {
    free(posix_aligned);
    free(aligned);
    free(c);
    free(p);
    return fail_errno(phase, "valloc(128)");
  }

  page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0) {
    page_size = 4096;
  }
  mapping = mmap(NULL, (size_t)page_size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mapping == MAP_FAILED) {
    free(page_aligned);
    free(posix_aligned);
    free(aligned);
    free(c);
    free(p);
    return fail_errno(phase, "mmap(MAP_ANONYMOUS)");
  }
  memset(mapping, 0x5a, (size_t)page_size < 64 ? (size_t)page_size : 64);
  if (munmap(mapping, (size_t)page_size) != 0) {
    free(page_aligned);
    free(posix_aligned);
    free(aligned);
    free(c);
    free(p);
    return fail_errno(phase, "munmap(mapping)");
  }

  free(page_aligned);
  free(posix_aligned);
  free(aligned);
  free(c);
  free(p);
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
  if (rc != 0) return rc;
  rc = exercise_alloc_wrappers(phase);
  if (rc != 0) return rc;

  if (strcmp(phase, "launch") == 0) {
    if (setenv(ENV_AFTER_EXEC, "1", 1) == -1) {
      return fail_with_env("launch", EXIT_EXEC,
                           "%s could not be set before exec: %s",
                           ENV_AFTER_EXEC, strerror(errno));
    }
    fflush(NULL);
    execvp(argv[0], argv);
    return fail_with_env("launch", EXIT_EXEC,
                         "exec reconstruction probe failed for argv[0]=%s: %s",
                         argv[0], strerror(errno));
  }

  printf("PASS alloc-built-in-preload: launch and exec preload state valid%s%s\n",
         getenv(ENV_ALLOC_PLUGIN) != NULL && strcmp(getenv(ENV_ALLOC_PLUGIN), "0") == 0 ?
         " (alloc-disabled)" : "",
         getenv(ENV_DISABLE_ALL_PLUGINS) != NULL &&
         strcmp(getenv(ENV_DISABLE_ALL_PLUGINS), "1") == 0 ?
         " (disable-all)" : "");
  return EXIT_SUCCESS;
}
