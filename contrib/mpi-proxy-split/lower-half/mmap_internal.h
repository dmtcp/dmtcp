/* Common mmap definition for Linux implementation.
   Copyright (C) 2017-2018 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */

#ifndef MMAP_INTERNAL_LINUX_H
#define MMAP_INTERNAL_LINUX_H 1

/* This is the minimum mmap2 unit size accept by the kernel.  An architecture
   with multiple minimum page sizes (such as m68k) might define it as -1 and
   thus it will queried at runtime.  */
#ifndef MMAP2_PAGE_UNIT
# define MMAP2_PAGE_UNIT 4096ULL
#endif

#if MMAP2_PAGE_UNIT == -1
static uint64_t page_unit;
# define MMAP_CHECK_PAGE_UNIT()                                                \
  if (page_unit == 0)                                                          \
    page_unit = __getpagesize ();
# undef MMAP2_PAGE_UNIT
# define MMAP2_PAGE_UNIT page_unit
#else
# define MMAP_CHECK_PAGE_UNIT()
#endif

// TODO: Add definitions for other architectures?

/* Do not accept offset not multiple of page size.  */
#define MMAP_OFF_LOW_MASK  (MMAP2_PAGE_UNIT - 1)

/* Registers clobbered by syscall.  */
# define REGISTERS_CLOBBERED_BY_SYSCALL "cc", "r11", "cx"

/* Create a variable 'name' based on type 'X' to avoid explicit types.
   This is mainly used set use 64-bits arguments in x32.   */
#define TYPEFY(X, name) __typeof__ ((X) - (X)) name
/* Explicit cast the argument to avoid integer from pointer warning on
   x32.  */
#define ARGIFY(X) ((__typeof__ ((X) - (X))) (X))
#define SYS_ify(syscall_name) __NR_##syscall_name

#define internal_syscall2(number, err, arg1, arg2)      \
({                  \
    unsigned long int resultvar;          \
    TYPEFY (arg2, __arg2) = ARGIFY (arg2);        \
    TYPEFY (arg1, __arg1) = ARGIFY (arg1);        \
    register TYPEFY (arg2, _a2) asm ("rsi") = __arg2;     \
    register TYPEFY (arg1, _a1) asm ("rdi") = __arg1;     \
    asm volatile (              \
    "syscall\n\t"             \
    : "=a" (resultvar)              \
    : "0" (number), "r" (_a1), "r" (_a2)        \
    : "memory", REGISTERS_CLOBBERED_BY_SYSCALL);      \
    (long int) resultvar;           \
})

#define internal_syscall3(number, err, arg1, arg2, arg3)    \
({                  \
    unsigned long int resultvar;          \
    TYPEFY (arg3, __arg3) = ARGIFY (arg3);        \
    TYPEFY (arg2, __arg2) = ARGIFY (arg2);        \
    TYPEFY (arg1, __arg1) = ARGIFY (arg1);        \
    register TYPEFY (arg3, _a3) asm ("rdx") = __arg3;     \
    register TYPEFY (arg2, _a2) asm ("rsi") = __arg2;     \
    register TYPEFY (arg1, _a1) asm ("rdi") = __arg1;     \
    asm volatile (              \
    "syscall\n\t"             \
    : "=a" (resultvar)              \
    : "0" (number), "r" (_a1), "r" (_a2), "r" (_a3)     \
    : "memory", REGISTERS_CLOBBERED_BY_SYSCALL);      \
    (long int) resultvar;           \
})

#define internal_syscall6(number, err, arg1, arg2, arg3, arg4, arg5, arg6)     \
({                                                                             \
    unsigned long int resultvar;                                               \
    TYPEFY (arg6, __arg6) = ARGIFY (arg6);                                     \
    TYPEFY (arg5, __arg5) = ARGIFY (arg5);                                     \
    TYPEFY (arg4, __arg4) = ARGIFY (arg4);                                     \
    TYPEFY (arg3, __arg3) = ARGIFY (arg3);                                     \
    TYPEFY (arg2, __arg2) = ARGIFY (arg2);                                     \
    TYPEFY (arg1, __arg1) = ARGIFY (arg1);                                     \
    register TYPEFY (arg6, _a6) asm ("r9") = __arg6;                           \
    register TYPEFY (arg5, _a5) asm ("r8") = __arg5;                           \
    register TYPEFY (arg4, _a4) asm ("r10") = __arg4;                          \
    register TYPEFY (arg3, _a3) asm ("rdx") = __arg3;                          \
    register TYPEFY (arg2, _a2) asm ("rsi") = __arg2;                          \
    register TYPEFY (arg1, _a1) asm ("rdi") = __arg1;                          \
    asm volatile (                                                             \
    "syscall\n\t"                                                              \
    : "=a" (resultvar)                                                         \
    : "0" (number), "r" (_a1), "r" (_a2), "r" (_a3), "r" (_a4),                \
      "r" (_a5), "r" (_a6)                                                     \
    : "memory", REGISTERS_CLOBBERED_BY_SYSCALL);                               \
    (long int) resultvar;                                                      \
})

#define INTERNAL_SYSCALL(name, err, nr, args...)                               \
  internal_syscall##nr (SYS_ify (name), err, args)

# define INTERNAL_SYSCALL_ERROR_P(val, err) \
  ((unsigned long int) (long int) (val) >= -4095L)

# define INTERNAL_SYSCALL_ERRNO(val, err) (-(val))

# define INLINE_SYSCALL(name, nr, args...) \
  ({                                                                           \
    unsigned long int resultvar = INTERNAL_SYSCALL (name, , nr, args);         \
    if (__glibc_unlikely (INTERNAL_SYSCALL_ERROR_P (resultvar, )))             \
    {                                                                          \
      __set_errno (INTERNAL_SYSCALL_ERRNO (resultvar, ));                      \
      resultvar = (unsigned long int) -1;                                      \
    }                                                                          \
    (long int) resultvar; })

/* Wrappers around system calls should normally inline the system call code.
   But sometimes it is not possible or implemented and we use this code.  */
#ifndef INLINE_SYSCALL
#define INLINE_SYSCALL(name, nr, args...) __syscall_##name (args)
#endif

#define __INLINE_SYSCALL2(name, a1, a2)                        \
  INLINE_SYSCALL (name, 2, a1, a2)

#define __INLINE_SYSCALL3(name, a1, a2, a3)                        \
  INLINE_SYSCALL (name, 3, a1, a2, a3)

#define __INLINE_SYSCALL6(name, a1, a2, a3, a4, a5, a6)                        \
  INLINE_SYSCALL (name, 6, a1, a2, a3, a4, a5, a6)

#define __SYSCALL_CONCAT_X(a, b)     a##b
#define __SYSCALL_CONCAT(a, b)       __SYSCALL_CONCAT_X (a, b)

#define __INLINE_SYSCALL_NARGS_X(a, b, c, d, e, f, g, h, n, ...) n
#define __INLINE_SYSCALL_NARGS(...) \
  __INLINE_SYSCALL_NARGS_X (__VA_ARGS__, 7, 6, 5, 4, 3, 2, 1, 0, )
#define __INLINE_SYSCALL_DISP(b, ...) \
  __SYSCALL_CONCAT (b, __INLINE_SYSCALL_NARGS(__VA_ARGS__))(__VA_ARGS__)

/* Issue a syscall defined by syscall number plus any other argument
   required.  Any error will be handled using arch defined macros and errno
   will be set accordingly.
   It is similar to INLINE_SYSCALL macro, but without the need to pass the
   expected argument number as second parameter.  */
#define INLINE_SYSCALL_CALL(...) \
  __INLINE_SYSCALL_DISP (__INLINE_SYSCALL, __VA_ARGS__)

/* An architecture may override this.  */
#ifndef MMAP_CALL
# define MMAP_CALL(__nr, __addr, __len, __prot, __flags, __fd, __offset) \
    INLINE_SYSCALL_CALL (__nr, __addr, __len, __prot, __flags, __fd, __offset)
#endif

#include "lower_half_api.h"

int getMmapIdx(const void *);
void* getNextAddr(size_t );

// TODO:
//  1. Make the size of list dynamic
#define MAX_TRACK   1000
extern int numRegions;
extern MmapInfo_t mmaps[MAX_TRACK];
extern void *nextFreeAddr;

#endif /* MMAP_INTERNAL_LINUX_H  */
