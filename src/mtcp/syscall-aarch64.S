/* Copyright (C) 2005-2014 Free Software Foundation, Inc.

   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public License as
   published by the Free Software Foundation; either version 2.1 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */

/* This file was modified from the original below.
 * 
/* This file was modified from the original below.
 * ../glibc-2.19/ports/sysdeps/unix/sysv/linux/aarch64/syscall.S
 */

// #include <sysdep.h>
// Macros copied or modifid from:
// ../glibc-2.19/ports/sysdeps/arm/sysdep.h
// ../glibc-2.19/sysdeps/generic/sysdep.h (USED?)

// Added for DMTCP
#define PSEUDO_END(name)
#define ENTRY(name)	\
  .globl name ;		\
  .type name,%function ;\
  .align 4 ;		\
name:

/* syscall (int nr, ...)

   AArch64 system calls take between 0 and 7 arguments. On entry here nr
   is in w0 and any other system call arguments are in register x1..x7.

   For kernel entry we need to move the system call nr to x8 then
   load the remaining arguments to register. */
/*
 * When using MTCP_SYS_ERRNO_ON_STACK, mtcp_sys.h adds the address of
 * mtcp_sys_errno as the second argument after the syscall number:
 *   mtcp_syscall(int nr, int *mtcp_sys_errno, x2, x3, x4, x5, x6, x7, x8)
 * nr is in x0/w0, and &mtcp_sys_errno is in x1. The logic below converts
 * this call to:
 *   syscall(x8, x0, x1, x2, x3, x4, x5, x6)
 * and saves the &mtcp_sys_errno in x10.
 * When returning from sycall, kernel puts the return value in x0.
 */

ENTRY (mtcp_syscall)
#ifdef MTCP_SYS_ERRNO_ON_STACK
	mov	x9, x8 /* Save last argument here temporarily */
	mov	x10, x1 /* Save address of mtcp_sys_errno here */
	uxtw	x8, w0
	mov	x0, x2
	mov	x1, x3
	mov	x2, x4
	mov	x3, x5
	mov	x4, x6
	mov	x5, x7
	mov	x6, x9 /* last argument had been moved here */
#else
	uxtw	x8, w0
	mov	x0, x1
	mov	x1, x2
	mov	x2, x3
	mov	x3, x4
	mov	x4, x5
	mov	x5, x6
	mov	x6, x7
#endif
	svc	0x0
	cmn	x0, #4095
	b.cs	1f   /* Branch on error */
	RET        /* assembler treats this as "ret" */
1:
#ifdef MTCP_SYS_ERRNO_ON_STACK
// x10 has &mtcp_sys_errno and x0 has encoded errno
	neg	x0, x0 /* If x0-4096 was negative, 4096-x0 is the errno. */
	str	x0, [x10, #0] /* Save the errno in mtcp_sys_errno */
	mov	x0, #-1 /* return -1 */
	RET        /* assembler treats this as "ret" */
#else
// This will call a DMTCP function in C that sets mtcp_sys_errno :
//	b	PLTJMP(syscall_error)
#endif
PSEUDO_END (syscall)
