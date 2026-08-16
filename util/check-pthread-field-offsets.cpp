#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include "glibc_pthread.h"

#include <gnu/libc-version.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

static int verbose;
static pthread_mutex_t detachedMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t detachedCond = PTHREAD_COND_INITIALIZER;
static int detachedDone;
static int detachedResult;

static int
fail(const char *msg)
{
  fprintf(stderr, "pthread field offset check failed: %s\n", msg);
  return 1;
}

static int
glibcMinorVersion()
{
  char *end = NULL;
  long major = strtol(gnu_get_libc_version(), &end, 10);
  if (major != 2 || end == NULL || *end != '.') {
    fail("unsupported glibc major version");
    exit(1);
  }

  return (int)strtol(end + 1, NULL, 10);
}

static int
checkTidAndPid(LibcPthreadShim shim, int libcMinor)
{
  pid_t tid = (pid_t)syscall(SYS_gettid);
  if (shim.tid() != tid) {
    return fail("tid field does not match SYS_gettid");
  }

  if (libcMinor <= 24) {
    if (shim.pidAddr() == NULL) {
      return fail("pid field missing for glibc <= 2.24");
    }
    if (*shim.pidAddr() != getpid()) {
      return fail("pid field does not match getpid");
    }
  }

  return 0;
}

static int
checkSchedFields(LibcPthreadShim shim)
{
  int policy = 0;
  struct sched_param param;
  memset(&param, 0, sizeof(param));

  int rc = pthread_getschedparam(pthread_self(), &policy, &param);
  if (rc != 0) {
    return fail("pthread_getschedparam failed");
  }

  int shimPolicy = 0;
  struct sched_param shimParam;
  memset(&shimParam, 0, sizeof(shimParam));
  shim.getSchedParam(&shimPolicy, &shimParam);

  if (shimPolicy != policy ||
      memcmp(&shimParam, &param, sizeof(param)) != 0) {
    return fail("scheduler fields do not match pthread_getschedparam");
  }

  int schedFlags = ATTR_FLAG_SCHED_SET | ATTR_FLAG_POLICY_SET;
  if ((shim.flags() & schedFlags) != schedFlags) {
    return fail("scheduler flags were not populated by glibc");
  }

  return 0;
}

static int
checkStackFields(LibcPthreadShim shim)
{
  if (shim.stackBlock() == NULL) {
    return fail("stackblock field is unexpectedly NULL");
  }

  pthread_attr_t attr;
  int rc = pthread_getattr_np(pthread_self(), &attr);
  if (rc != 0) {
    return fail("pthread_getattr_np failed");
  }

  void *stackAddr = NULL;
  size_t stackSize = 0;
  size_t guardSize = 0;
  rc = pthread_attr_getstack(&attr, &stackAddr, &stackSize);
  if (rc == 0) {
    rc = pthread_attr_getguardsize(&attr, &guardSize);
  }
  pthread_attr_destroy(&attr);
  if (rc != 0) {
    return fail("pthread_attr_getstack/getguardsize failed");
  }

#if _STACK_GROWS_DOWN
  void *expectedStackAddr =
    (char *)shim.stackBlock() + shim.guardSize();
#else
  void *expectedStackAddr = shim.stackBlock();
#endif
  size_t expectedStackSize = shim.stackBlockSize() - shim.guardSize();
  size_t expectedGuardSize = shim.reportedGuardSize();

  if (stackAddr != expectedStackAddr ||
      stackSize != expectedStackSize ||
      guardSize != expectedGuardSize) {
    if (verbose) {
      fprintf(stderr,
              "stackAddr=%p expected=%p stackSize=%zu expected=%zu "
              "guardSize=%zu expected=%zu stackBlock=%p "
              "stackBlockSize=%zu guard=%zu reportedGuard=%zu\n",
              stackAddr, expectedStackAddr, stackSize, expectedStackSize,
              guardSize, expectedGuardSize, shim.stackBlock(),
              shim.stackBlockSize(), shim.guardSize(),
              shim.reportedGuardSize());
    }
    return fail("stack fields do not match pthread_getattr_np");
  }

  return 0;
}

static int
checkCurrentThread(int libcMinor, int requireStack)
{
  LibcPthreadShim shim =
    LibcPthreadShim::from(pthread_self(), libcMinor);

  if (checkTidAndPid(shim, libcMinor) != 0) {
    return 1;
  }

  if (checkSchedFields(shim) != 0) {
    return 1;
  }

  if (requireStack && checkStackFields(shim) != 0) {
    return 1;
  }

  return 0;
}

static void *
checkJoinableThread(void *arg)
{
  int libcMinor = *(int *)arg;
  return (void *)(intptr_t)checkCurrentThread(libcMinor, 1);
}

static void *
checkDetachedThread(void *arg)
{
  int libcMinor = *(int *)arg;
  int result = checkCurrentThread(libcMinor, 1);

  if (result == 0) {
    LibcPthreadShim shim =
      LibcPthreadShim::from(pthread_self(), libcMinor);
    if (shim.joinId() != pthread_self()) {
      result = fail("joinid field does not mark detached thread");
    }
  }

  pthread_mutex_lock(&detachedMutex);
  detachedResult = result;
  detachedDone = 1;
  pthread_cond_signal(&detachedCond);
  pthread_mutex_unlock(&detachedMutex);
  return NULL;
}

static int
checkJoinableChild(int libcMinor)
{
  pthread_t thread;
  int rc = pthread_create(&thread, NULL, checkJoinableThread, &libcMinor);
  if (rc != 0) {
    return fail("pthread_create failed");
  }

  void *result = NULL;
  rc = pthread_join(thread, &result);
  if (rc != 0) {
    return fail("pthread_join failed");
  }

  return (int)(intptr_t)result;
}

static int
checkDetachedChild(int libcMinor)
{
  pthread_attr_t attr;
  int rc = pthread_attr_init(&attr);
  if (rc != 0) {
    return fail("pthread_attr_init failed");
  }

  rc = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  if (rc != 0) {
    pthread_attr_destroy(&attr);
    return fail("pthread_attr_setdetachstate failed");
  }

  pthread_t thread;
  rc = pthread_create(&thread, &attr, checkDetachedThread, &libcMinor);
  pthread_attr_destroy(&attr);
  if (rc != 0) {
    return fail("pthread_create for detached thread failed");
  }

  pthread_mutex_lock(&detachedMutex);
  while (!detachedDone) {
    pthread_cond_wait(&detachedCond, &detachedMutex);
  }
  pthread_mutex_unlock(&detachedMutex);

  return detachedResult;
}

int
main(int argc, char **argv)
{
  if (argc == 2) {
    verbose = strcmp(argv[1], "-v") == 0 ||
              strcmp(argv[1], "--verbose") == 0;
  }
  if (argc > 1 && !verbose) {
    fprintf(stderr, "USAGE: %s [-v|--verbose]\n", argv[0]);
    return 1;
  }

  int libcMinor = glibcMinorVersion();
  if (verbose) {
    printf("Checking pthread fields for glibc %s\n",
           gnu_get_libc_version());
  }

  if (checkCurrentThread(libcMinor, 0) != 0) {
    return 1;
  }
  if (checkJoinableChild(libcMinor) != 0) {
    return 1;
  }
  if (checkDetachedChild(libcMinor) != 0) {
    return 1;
  }

  return 0;
}
