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

#include <stdio.h> /* for printf */
#include <sys/mman.h>

// Rename it for cosmetic reasons.
#define mtcp_inline_syscall(name, num_args, args ...) \
  INLINE_SYSCALL(name, num_args, args)

// sysdep-x86_64.h:
// From glibc-2.5/sysdeps/unix/sysv/linux/x86_64/sysdep.h :  (define
// INLINE_SYSCALL)
// sysdep-i386.h:
// Or glibc-2.5/sysdeps/unix/sysv/linux/i386/sysdep.h :  (define INLINE_SYSCALL)
// But all further includes from sysdep-XXX.h have been commented out.
#ifdef __i386__

// THIS CASE fOR i386 NEEDS PATCHING FOR 6 ARGUMENT CASE, SUCH AS MMAP.
// IT ONLY TRIES TO HANDLE UP TO 5 ARGS.
# include "sysdep-i386.h"

# ifdef __PIC__
#  define EXTRAVAR_6 int _xv;
#  define ASMFMT_6(arg1, arg2, arg3, arg4, arg5)               \
  , "0" (arg1), "m" (_xv), "c" (arg2), "d" (arg3), "S" (arg4), \
  "D" (arg5), "0" (arg6)
#  if defined I386_USE_SYSENTER && defined SHARED
#   define LOADARGS_6  \
  "movl %%ebx, %4\n\t" \
  "movl %3, %%ebx\n\t" \
  "push %%ebp\n\t" "movl %%eax,%%ebp\n\t"
#   define RESTOREARGS_6 \
  "movl %4, %%ebx"       \
  "pop %%ebp\n\t"
#  else /* if defined I386_USE_SYSENTER && defined SHARED */
#   define LOADARGS_6  \
  "movl %%ebx, %3\n\t" \
  "movl %2, %%ebx\n\t" \
  "push %%ebp\n\t" "movl %%eax,%%ebp\n\t"
#   define RESTOREARGS_6 \
  "movl %3, %%ebx"       \
  "pop %%ebp\n\t"
#  endif /* if defined I386_USE_SYSENTER && defined SHARED */
# else /* ifdef __PIC__ */
#  define EXTRAVAR_6
#  define LOADARGS_6    "push %%ebp\n\t" "movl %%eax,%%ebp\n\t"
#  define RESTOREARGS_6 "pop %%ebp\n\t"
#  define ASMFMT_6(arg1, arg2, arg3, arg4, arg5, arg6) \
  , "b" (arg1), "c" (arg2), "d" (arg3), "S" (arg4), "D" (arg5), "0" (arg6)
# endif /* ifdef __PIC__ */
# define LOAD_ARGS_6(a1, a2, a3, a4, a5, a6) \
  long int __arg6 = (long)(a6);              \
  LOAD_ARGS_5(a1, a2, a3, a4, a5)
# define LOAD_REGS_6                         \
  register long int _a6 asm ("r9") = __arg6; \
  LOAD_REGS_5
# define ASM_ARGS_6 ASM_ARGS_5, "r" (_a6)
#endif /* ifdef __i386__ */
#ifdef __x86_64__
# include "sysdep-x86_64.h"
#endif /* ifdef __x86_64__ */

// #include <sysdeps/unix/x86_64/sysdep.h>
#include <asm/unistd.h> /* translate __NR_getpid to syscall # using i386 or
                           x86_64 */
#define __set_errno(Val) errno = (Val)

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
    printf("mmap: %s\n", strerror(errno)); exit(1);
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
                                     MAP_PRIVATE | MAP_ANONYMOUS,
                                     fileno(stream),
                                     0);

  fclose(stream);
  if (ptr == MAP_FAILED) {
    printf("mmap: %s\n", strerror(errno)); exit(1);
  }

  printf("mmap pointer: %x\n", ptr);
  printf("pid: %d\n", x);
  printf("pid: %d\n", mtcp_inline_syscall(getpid, 0));

  // printf("pid: %d\n", getpid());

  return 0;
}
#endif /* ifndef SHARED */
