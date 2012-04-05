#ifndef _MTCP_FUTEX_H
#define _MTCP_FUTEX_H

#include <linux/futex.h>
#include <sys/time.h>
#include "mtcp_sys.h"  /* for mtcp_sys_kernel.h */

/* FIXME:  BUG:  The call to mtcp_sys_kernel_futex() should replace
	 the inline assembly;  See comment near bottom. */
/* Glibc does not provide a futex routine, so provide one here... */

static inline int mtcp_futex (int *uaddr, int op, int val,
                              const struct timespec *timeout)
{
#if defined(__x86_64__)
  int rc;

  register long int a1 asm ("rdi") = (long int)uaddr;
  register long int a2 asm ("rsi") = (long int)op;
  register long int a3 asm ("rdx") = (long int)val;
  register long int a4 asm ("r10") = (long int)timeout;
  register long int a5 asm ("r8")  = (long int)0;

  asm volatile ("syscall"
                : "=a" (rc)
                : "0" (__NR_futex), 
                  "r" (a1), "r" (a2), "r" (a3), "r" (a4), "r" (a5)
                : "memory", "cc", "r11", "cx");
  return (rc);
#elif defined(__i386__) && ! defined(__PIC__)
  int rc;

  asm volatile ("int $0x80"
                : "=a" (rc)
                : "0" (__NR_futex), 
                  "b" (uaddr), "c" (op), "d" (val), "S" (timeout), "D" (0)
                : "memory", "cc");
  return (rc);
#elif defined(__i386__) && defined(__PIC__)
  // FIXME:  After DMTCP-1.2.5, make this case universal and remove assembly.
  int rc; /* raw return code as returned by kernel */
  int uaddr2=0, val3=0; /* These last two args of futex not used by MTCP. */
  rc = INTERNAL_SYSCALL(futex,err,6,uaddr, op, val, timeout, &uaddr2, val3);
  return (rc);
#elif defined(__arm__)
  int rc;

  register int r0 asm("r0") = (int)uaddr;
  register int r1 asm("r1") = (int)op;
  register int r2 asm("r2") = (int)val;
  register int r3 asm("r3") = (unsigned int)timeout;
  // Use of "r7" requires gcc -fomit-frame-pointer
  // Maybe could restrict to non-thumb mode here, and gcc might not complain.
  asm volatile ("mov r7, %1\n\t swi 0x0"
                : "=r" (rc)
		: "I" __NR_futex,
                  "r" (r0), "r" (r1), "r" (r2), "r" (r3)
                : "memory", "cc", "r7");
  return (rc);
#else  /* defined(i386) && defined(__PIC__), but see bug below. */
/* FIXME:  BUG:  mtcp_sys_errno is global.  So, in multi-threaded code,
 *   this can fail.  Would need mtcp_sys_kernel_futex_raw that directly returns
 *   the return code from the kernel (with the embedded errno).
 */

/* mtcp_internal.h defines the macros associated with futex(). */
  int uaddr2=0, val3=0; /* These last two args of futex not used by MTCP. */
  int rc = mtcp_sys_kernel_futex(uaddr, op, val, timeout, &uaddr2, val3);
  if (rc == -1) {
    rc = - mtcp_sys_errno;
  }
  return rc;
#endif
}

#endif
