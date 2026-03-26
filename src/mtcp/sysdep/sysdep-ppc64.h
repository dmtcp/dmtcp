/* Assembly macros for PowerPC64.
 *    Copyright (C) 2024
 *       Ported for DMTCP PowerPC64 support
 *
 *          This file is part of DMTCP.
 *
 *             DMTCP is free software; you can redistribute it and/or
 *                modify it under the terms of the GNU Lesser General Public
 *                   License as published by the Free Software Foundation; either
 *                      version 2.1 of the License, or (at your option) any later version.
 *
 *                         DMTCP is distributed in the hope that it will be useful,
 *                            but WITHOUT ANY WARRANTY; without even the implied warranty of
 *                               MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *                                  Lesser General Public License for more details.
 *
 *                                     You should have received a copy of the GNU Lesser General Public
 *                                        License along with DMTCP.  If not, see
 *                                           <http://www.gnu.org/licenses/>.  */

#ifndef _LINUX_PPC64_SYSDEP_H
#define _LINUX_PPC64_SYSDEP_H 1

#ifdef __ASSEMBLER__

# define ENTRY(name) \
  .globl name; \
  .type name,@function; \
  .align 2; \
  name:

# define END(name) \
  .size name,.-name

# define L(label) .L ## label

/* Performs a system call, handling errors by setting errno.  Linux indicates
 *    errors by setting r3 to a value between -1 and -4095.  */
# undef PSEUDO
# define PSEUDO(name, syscall_name, args)			\
	  .text;							\
	    .align 2;							\
	      ENTRY (name);						\
	        li 0, SYS_ify (syscall_name);			\
		  sc;							\
		    bnslr+;						\
		      b .Lsyscall_error ## name;

# undef PSEUDO_END
# define PSEUDO_END(sym) 					\
	  SYSCALL_ERROR_HANDLER (sym)				\
	    blr;						\
	      END (sym)

# define SYSCALL_ERROR_HANDLER(name)				\
	.Lsyscall_error ## name:				\
	        neg 3, 3;					\
		  b __syscall_error;

/* Performs a system call, not setting errno.  */
# undef PSEUDO_NEORRNO
# define PSEUDO_NOERRNO(name, syscall_name, args)	\
	  .align 2;					\
	    ENTRY (name);				\
	      li 0, SYS_ify (syscall_name);		\
	        sc;

# undef PSEUDO_END_NOERRNO
# define PSEUDO_END_NOERRNO(name)			\
	  END (name)

# undef ret_NOERRNO
# define ret_NOERRNO blr

/* Performs a system call, returning the error code.  */
# undef PSEUDO_ERRVAL
# define PSEUDO_ERRVAL(name, syscall_name, args) 	\
	  PSEUDO_NOERRNO (name, syscall_name, args)	\
	    neg 3, 3;

# undef PSEUDO_END_ERRVAL
# define PSEUDO_END_ERRVAL(name)			\
	  END (name)

# undef ret_ERRVAL
# define ret_ERRVAL blr

#endif /* __ASSEMBLER__ */

/* In order to get __set_errno() definition in INLINE_SYSCALL.  */
#ifndef __ASSEMBLER__
# include <errno.h>
#endif

#undef SYS_ify
#define SYS_ify(syscall_name)	__NR_##syscall_name

#ifndef __ASSEMBLER__

/* Define a macro which expands into the inline wrapper code for a system
 *    call.  */
# undef INLINE_SYSCALL
# define INLINE_SYSCALL(name, nr, args...)				\
	  ({ INTERNAL_SYSCALL_DECL (err);				\
	        long int __sys_result = INTERNAL_SYSCALL (name, err, nr, args); \
		     if (__builtin_expect (INTERNAL_SYSCALL_ERROR_P (__sys_result, err), 0))  \
		            {							\
			             __set_errno (INTERNAL_SYSCALL_ERRNO (__sys_result, err)); \
				     	 __sys_result = (unsigned long) -1;		\
					        }						\
						     __sys_result; })

# define INTERNAL_SYSCALL_DECL(err) long int err __attribute__ ((unused))

# define INTERNAL_SYSCALL_ERROR_P(val, err) \
	        ((void)(err), __builtin_expect ((val) >= -4095UL, 0))

# define INTERNAL_SYSCALL_ERRNO(val, err)     (-(val))

# define INTERNAL_SYSCALL(name, err, nr, args...) \
		internal_syscall##nr (SYS_ify (name), err, args)

# define INTERNAL_SYSCALL_NCS(number, err, nr, args...) \
		internal_syscall##nr (number, err, args)

# define internal_syscall0(number, err, dummy...)		\
	({ 							\
	 	long int _sys_result;				\
								\
		{						\
			register long int r0 __asm__ ("r0");	\
			register long int r3 __asm__ ("r3");	\
			r0 = number;				\
			__asm__ __volatile__ (			\
				"sc\n\t"			\
				"mfcr %0"			\
				: "=&r" (r0), "=&r" (r3)	\
				: "0" (r0)			\
				: "memory", "cr0", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12"); \
			err = r0;				\
			_sys_result = r3;			\
		}						\
		_sys_result;					\
	})

# define internal_syscall1(number, err, arg0)			\
	({ 							\
	 	long int _sys_result;				\
								\
		{						\
			register long int r0 __asm__ ("r0");	\
			register long int r3 __asm__ ("r3");	\
			r0 = number;				\
			r3 = (long int) (arg0);			\
			__asm__ __volatile__ (			\
				"sc\n\t"			\
				"mfcr %0"			\
				: "=&r" (r0), "+&r" (r3)	\
				: "0" (r0)			\
				: "memory", "cr0", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12"); \
			err = r0;				\
			_sys_result = r3;			\
		}						\
		_sys_result;					\
	})

# define internal_syscall2(number, err, arg0, arg1)		\
	({ 							\
	 	long int _sys_result;				\
								\
		{						\
			register long int r0 __asm__ ("r0");	\
			register long int r3 __asm__ ("r3");	\
			register long int r4 __asm__ ("r4");	\
			r0 = number;				\
			r3 = (long int) (arg0);			\
			r4 = (long int) (arg1);			\
			__asm__ __volatile__ (			\
				"sc\n\t"			\
				"mfcr %0"			\
				: "=&r" (r0), "+&r" (r3)	\
				: "0" (r0), "r" (r4)		\
				: "memory", "cr0", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12"); \
			err = r0;				\
			_sys_result = r3;			\
		}						\
		_sys_result;					\
	})

# define internal_syscall3(number, err, arg0, arg1, arg2)	\
	({ 							\
	 	long int _sys_result;				\
								\
		{						\
			register long int r0 __asm__ ("r0");	\
			register long int r3 __asm__ ("r3");	\
			register long int r4 __asm__ ("r4");	\
			register long int r5 __asm__ ("r5");	\
			r0 = number;				\
			r3 = (long int) (arg0);			\
			r4 = (long int) (arg1);			\
			r5 = (long int) (arg2);			\
			__asm__ __volatile__ (			\
				"sc\n\t"			\
				"mfcr %0"			\
				: "=&r" (r0), "+&r" (r3)	\
				: "0" (r0), "r" (r4), "r" (r5)	\
				: "memory", "cr0", "r6", "r7", "r8", "r9", "r10", "r11", "r12"); \
			err = r0;				\
			_sys_result = r3;			\
		}						\
		_sys_result;					\
	})

# define internal_syscall4(number, err, arg0, arg1, arg2, arg3)	\
	({ 							\
	 	long int _sys_result;				\
								\
		{						\
			register long int r0 __asm__ ("r0");	\
			register long int r3 __asm__ ("r3");	\
			register long int r4 __asm__ ("r4");	\
			register long int r5 __asm__ ("r5");	\
			register long int r6 __asm__ ("r6");	\
			r0 = number;				\
			r3 = (long int) (arg0);			\
			r4 = (long int) (arg1);			\
			r5 = (long int) (arg2);			\
			r6 = (long int) (arg3);			\
			__asm__ __volatile__ (			\
				"sc\n\t"			\
				"mfcr %0"			\
				: "=&r" (r0), "+&r" (r3)	\
				: "0" (r0), "r" (r4), "r" (r5), "r" (r6) \
				: "memory", "cr0", "r7", "r8", "r9", "r10", "r11", "r12"); \
			err = r0;				\
			_sys_result = r3;			\
		}						\
		_sys_result;					\
	})

# define internal_syscall5(number, err, arg0, arg1, arg2, arg3, arg4) \
	({ 							\
	 	long int _sys_result;				\
								\
		{						\
			register long int r0 __asm__ ("r0");	\
			register long int r3 __asm__ ("r3");	\
			register long int r4 __asm__ ("r4");	\
			register long int r5 __asm__ ("r5");	\
			register long int r6 __asm__ ("r6");	\
			register long int r7 __asm__ ("r7");	\
			r0 = number;				\
			r3 = (long int) (arg0);			\
			r4 = (long int) (arg1);			\
			r5 = (long int) (arg2);			\
			r6 = (long int) (arg3);			\
			r7 = (long int) (arg4);			\
			__asm__ __volatile__ (			\
				"sc\n\t"			\
				"mfcr %0"			\
				: "=&r" (r0), "+&r" (r3)	\
				: "0" (r0), "r" (r4), "r" (r5), "r" (r6), "r" (r7) \
				: "memory", "cr0", "r8", "r9", "r10", "r11", "r12"); \
			err = r0;				\
			_sys_result = r3;			\
		}						\
		_sys_result;					\
	})

# define internal_syscall6(number, err, arg0, arg1, arg2, arg3, arg4, arg5) \
	({ 							\
	 	long int _sys_result;				\
								\
		{						\
			register long int r0 __asm__ ("r0");	\
			register long int r3 __asm__ ("r3");	\
			register long int r4 __asm__ ("r4");	\
			register long int r5 __asm__ ("r5");	\
			register long int r6 __asm__ ("r6");	\
			register long int r7 __asm__ ("r7");	\
			register long int r8 __asm__ ("r8");	\
			r0 = number;				\
			r3 = (long int) (arg0);			\
			r4 = (long int) (arg1);			\
			r5 = (long int) (arg2);			\
			r6 = (long int) (arg3);			\
			r7 = (long int) (arg4);			\
			r8 = (long int) (arg5);			\
			__asm__ __volatile__ (			\
				"sc\n\t"			\
				"mfcr %0"			\
				: "=&r" (r0), "+&r" (r3)	\
				: "0" (r0), "r" (r4), "r" (r5), "r" (r6), "r" (r7), "r" (r8) \
				: "memory", "cr0", "r9", "r10", "r11", "r12"); \
			err = r0;				\
			_sys_result = r3;			\
		}						\
		_sys_result;					\
	})

extern long int __syscall_error (long int neg_errno);

#endif /* ! __ASSEMBLER__ */

/* Pointer mangling is not supported.  */
#define PTR_MANGLE(var) (void) (var)
#define PTR_DEMANGLE(var) (void) (var)

#endif /* linux/ppc64/sysdep.h */

// Made with Bob
