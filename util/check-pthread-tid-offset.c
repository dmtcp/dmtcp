// Compile as:  gcc THIS_FILE -pthread

#define _GNU_SOURCE
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <pthread.h>
#include <ucontext.h>
#include <gnu/libc-version.h> // for gnu_get_libc_version()

int pthread_tid_hit[2048];
int num_elts = sizeof(pthread_tid_hit)/sizeof(pthread_tid_hit[0]);
pthread_t thread;
ucontext_t context;

int segfault = 0;
void segfault_handler(int signal) {
  if (signal == SIGSEGV || signal == SIGBUS) {
    segfault = 1;
    unsigned long pagesize= sysconf(_SC_PAGESIZE);
    char *last_page_address =  (char *)(thread & ~(pagesize-1)) + pagesize;
    num_elts = (last_page_address - (char *)thread) / sizeof(int);
    setcontext(&context);
  } else {
    exit(2);
  }
}

// From src/plugin/pid/libc_pthread.h
struct libc_tcbhead_t {
#if defined(__x86__) || defined(__x86_64__)
  char pad[704];
#elif defined(__arm__) || defined(__aarch64__)
  char pad[24 * sizeof(void*)];
#elif defined(__riscv)
  char pad[192];
#else
# warning "Unsupported architecture; Call executable with '-v' flag for info."
#endif
};

struct libc_list_t {
  struct list_head *next;
  struct list_head *prev;
};

int tid_offset = sizeof(struct libc_tcbhead_t) + sizeof(struct libc_list_t);

static int glibcMajorVersion()
{
  static int major = 0;
  if (major == 0) {
    major = (int) strtol(gnu_get_libc_version(), NULL, 10);
    assert(major == 2);
  }
  return major;
}

static int glibcMinorVersion()
{
  static long minor = 0;
  if (minor == 0) {
    char *ptr;
    int major = (int) strtol(gnu_get_libc_version(), &ptr, 10);
    assert(major == 2);
    minor = (int) strtol(ptr+1, NULL, 10);
  }
  return minor;
}

 __attribute__((optimize(0)))
void *do_task(void *dummy) {
  thread = pthread_self(); // Same as 'thread' in 'main'
  signal(SIGSEGV, &segfault_handler);
  signal(SIGBUS, &segfault_handler);
  getcontext(&context);
  int test_if_segfault = ((int *)thread)[num_elts-1]; // may cause segfault
  // If segfault happens, we set segfault=1, and return to getcontext above

  pid_t tid = syscall(SYS_gettid); // Early glibc doesn't have gettid()
  for (int i = 0; i < num_elts; i++) {
    if ( ((int *)thread)[i] != tid ) {
      pthread_tid_hit[i] = 0;
    }
  }
  return NULL;
}

int main(int argc, const char *argv[]) {
  int verbose = 0;
  if (argc == 2) {
    verbose = (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "-verbose") == 0);
  }
  if (argc > 1 && ! verbose) {
    fprintf(stderr, "USAGE:  %s [-v|--verbose]\n", argv[0]);
    exit(1);
  }

  for (int i = 0; i < num_elts; i++) {
    pthread_tid_hit[i] = 1;
  }

  for (int i = 0; i < 5; i++) {
    pthread_t thread;
    pthread_create(&thread, NULL, &do_task, NULL);
    pthread_join(thread, NULL);
  }

  int num_matches = 0;
  int match_offset = -1;
  for (int i = 0; i < num_elts; i++) {
    if (pthread_tid_hit[i] == 1) {
      num_matches++;
      match_offset = i * sizeof(int);
    }
  }
  if (num_matches != 1) {
    printf("%d matches found for tid in pthread descriptor.\n", num_matches);
  }

  if (verbose && num_matches >= 1) {
    printf("Match found for tid_offset; tid_offset = %d;\n"
           "  See glibc_pthread.h:struct libc_tcbhead_t.\n", match_offset);
    printf("  This is for the offset in 'pthread_t', or 'struct pthread'.\n"
           "  In src/plugin/pid/glibc_pthread.h, for pad[], use:\n");
    printf("struct libc_tcbhead_t {\n"
           "  char pad[%ld];\n"
           "};\n", match_offset - sizeof(struct libc_list_t));
    printf("The tid offset in that file is calculated based on:\n"
           "  struct libc2_33_pthread {\n"
           "    libc_tcbhead_t header;\n"
           "    libc_list_t list;\n"
           "    pid_t tid;\n"
           "    ..\n"
           "  };\n");
    printf("So, the size of pad[] is based on:"
           "  tid_offset - sizeof(struct libc_list_t)\n");
  }

  // SEE: src/tls.cpp and src/plugin/pid/glibc_pthread.h
  // FIXME: Is the code in src/tls.cpp for 'offset' now obsolete?
  // This is from src/tls.cpp;  Make sure it stays in sync with future glibc.
  // tcbhead_t, etc., were introduced in glibc 2.4. We don't support earlier
  // versions.
  assert(glibcMajorVersion() == 2);
  int  libcMinor = glibcMinorVersion();
  int offset = -1;
  if (glibcMinorVersion() < 11) {
    printf("glibc-2.%d is less than glibc-2.10.  Not checking.\n",
           glibcMinorVersion());
    return 0;
  }

#if 0
  // This is from src/tls.cpp.  Is this correct?
  if (glibcMinorVersion() >= 33) {
    offset = 26 * sizeof(void *); // sizeof(__padding) + sizeof(list_t)
  } else {
    offset = 18 * sizeof(void *); // sizeof(__padding) + sizeof(list_t)
  }
#endif

  // Return 0 if the tid_offset matches.
  return (match_offset == tid_offset ? 0 : 1);
}
