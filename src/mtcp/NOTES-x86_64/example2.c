// FIX __set_errno BELOW, AND 6 ARG CALL FOR i386

// mtcp_sy.h:  static int mtcp_sy_errno;
// and for inline, will be available in same file only --- okay for typical use
// Doesn't handle __PIC__ (position independent code).
// LOOK AT INTERNAL_SYSCALL  AND DEFINE MISSING ARGS:  EXTRAVAR_6, LOADARGS_6,
// etc.
//
// TESTING:  gcc -pie -fpie example.c
// TESTING:  gcc -DSHARED=1 -shared -o shared.so -pie -fpie example.c
// gcc -DEXECUTABLE=1 -shared -o shared.so -pie -fpie example.c

#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

#include "../mtcp_sy.h"
#include <stdio.h> /* for printf */
#include <sys/mman.h>

#ifdef SHARED
void
test_mmap()
{
  FILE *stream = fopen("/etc/passwd", "r");
  void *ptr = (void *)INLINE_SYSCALL(mmap,
                                     6,
                                     0,
                                     1024,
                                     PROT_READ,
                                     MAP_PRIVATE | MAP_ANONYMOUS,
                                     fileno(stream),
                                     0);

  if (ptr == MAP_FAILED) {
    printf("mmap: %s\n", strerror(mtcp_sy_errno)); exit(1);
  }
  fclose(stream);

  printf("mmap pointer: %x\n", ptr);
}
#endif /* ifdef SHARED */

#ifndef SHARED
int
main()
{
  pid_t x = INLINE_SYSCALL(getpid, 0);
  FILE *stream = fopen("/etc/passwd", "r");
  void *ptr = (void *)INLINE_SYSCALL(mmap,
                                     6,
                                     0,
                                     1024,
                                     PROT_READ,
                                     MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS,
                                     fileno(stream),
                                     0);

  fclose(stream);
  if (ptr == MAP_FAILED) {
    printf("mmap: %s\n", strerror(mtcp_sy_errno)); exit(1);
  }

  printf("mmap pointer: %x\n", ptr);
  printf("pid: %d\n", x);
  printf("pid: %d\n", mtcp_inline_syscall(getpid, 0));

  // printf("pid: %d\n", getpid());

  return 0;
}
#endif /* ifndef SHARED */
