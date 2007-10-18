#ifndef _MTCP_FUTEX_H
#define _MTCP_FUTEX_H

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
#elif defined(__i386__)
  int rc;

  asm volatile ("int $0x80"
                : "=a" (rc)
                : "0" (__NR_futex), 
                  "b" (uaddr), "c" (op), "d" (val), "S" (timeout), "D" (0)
                : "memory", "cc");
#else
#error need to define mtcp_futex
#endif
}

#endif
