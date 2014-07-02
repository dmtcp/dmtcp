/* Copyright (C) 2005, 2009 Free Software Foundation, Inc.
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
   License along with the GNU C Library.  If not, see
   <http://www.gnu.org/licenses/>.  */

/* This file was modified from the original below.
 * ../eglibc-2.17/ports/sysdeps/unix/sysv/linux/arm/syscall.S
 */

// #include <sysdep.h>
// Macros copied or modifid from:
// ../eglibc-2.17/ports/sysdeps/arm/sysdep.h
// ../eglibc-2.17/sysdeps/generic/sysdep.h

/* In the EABI syscall interface, we don't need a special syscall to
   implement syscall().  It won't work reliably with 64-bit arguments
   (but that is true on many modern platforms).  */

// Added for DMTCP
#define cfi_adjust_cfa_offset(x)
#define cfi_rel_offset(x,y)
#define cfi_adjust_cfa_offset(x)
#define cfi_restore(x)
#define RETINSTR(cc,lr) bx##cc  lr
#define PSEUDO_END(fnc)
#define ENTRY(fnc) \
.globl fnc ;\
.type fnc,%function ;\
.align 4 ;\
fnc:

/* This define is needed on command line for the DMTCP-2.2 release.  After
 * that, this code will move from the mtcp_restart executable to a library,
 * libmtcp.so.  The current mtcp_restart cannot use global variables.
 * But libmtcp.so will again employ global variables, and so the main branch
 * here will again align well with glibc.
 */
// #define MTCP_SYS_ERRNO_ON_STACK

// Call it mtcp_syscall instead of syscall to avoid any DMTCP wrappers.
ENTRY (mtcp_syscall)
	mov	ip, sp
	stmfd	sp!, {r4, r5, r6, r7, r8}
#ifdef MTCP_SYS_ERRNO_ON_STACK
	mov	r8, r1 /* Save address of mtcp_sys_errno here */
#endif
	cfi_adjust_cfa_offset (16)
	cfi_rel_offset (r4, 0)
	cfi_rel_offset (r5, 4)
	cfi_rel_offset (r6, 8)
	cfi_rel_offset (r7, 12)
	mov	r7, r0
#ifdef MTCP_SYS_ERRNO_ON_STACK
	mov	r0, r2
	mov	r1, r3
	ldmfd	ip, {r2, r3, r4, r5, r6}
#else
	mov	r0, r1
	mov	r1, r2
	mov	r2, r3
	ldmfd	ip, {r3, r4, r5, r6}
#endif
	swi	0x0
#ifdef MTCP_SYS_ERRNO_ON_STACK
	mov	r1, r8 /* Restore address of mtcp_sys_errno to r1 here */
#endif
	ldmfd	sp!, {r4, r5, r6, r7, r8}
	cfi_adjust_cfa_offset (-16)
	cfi_restore (r4)
	cfi_restore (r5)
	cfi_restore (r6)
	cfi_restore (r7)
	cmn	r0, #4096
	RETINSTR(cc, lr)
#ifdef MTCP_SYS_ERRNO_ON_STACK
// r1 has &mtcp_sys_errno and r0 has encoded errno
	neg	r0, r0 /* If r0-4096 was negative, 4096-r0 is the errno. */
	str	r0, [r1, #0]
	mvn	r0, #0 /* return -1 */
	mov	pc, lr
#else
// This will call a DMTCP function in C that sets mtcp_sys_errno :
//	b	PLTJMP(syscall_error)
#endif
PSEUDO_END (mtcp_syscall)
