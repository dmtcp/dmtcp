/*****************************************************************************
 *   Copyright (C) 2006-2009 by Michael Rieker, Jason Ansel, Kapil Arya, and *
 *                                                            Gene Cooperman *
 *   mrieker@nii.net, jansel@csail.mit.edu, kapil@ccs.neu.edu, and           *
 *                                                          gene@ccs.neu.edu *
 *                                                                           *
 *   This file is part of the MTCP module of DMTCP (DMTCP:mtcp).             *
 *                                                                           *
 *  DMTCP:mtcp is free software: you can redistribute it and/or              *
 *  modify it under the terms of the GNU Lesser General Public License as    *
 *  published by the Free Software Foundation, either version 3 of the       *
 *  License, or (at your option) any later version.                          *
 *                                                                           *
 *  DMTCP:dmtcp/src is distributed in the hope that it will be useful,       *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *  GNU Lesser General Public License for more details.                      *
 *                                                                           *
 *  You should have received a copy of the GNU Lesser General Public         *
 *  License along with DMTCP:dmtcp/src.  If not, see                         *
 *  <http://www.gnu.org/licenses/>.                                          *
 *****************************************************************************/

/*
 * The goal of including this file is to define most of the external
 *   symbols used in mtcp_sharetemp.c .  This to insure that the linker
 *   does not try to resolve any symbols by linking in libc.  That would
 *   fail at restart time, when there is no libc.
 * mtcp_sharetemp.c is a concatenation
 *   of other files, allowing us to compile a single file into a static
 *   object.  (Is it still necessary to use sharetemp.c?)
 *
 *Could have used ideas from statifier?:
 *  http://statifier.sourceforge.net/
 *But that still depends too much on ELF
 *At least it has information on changing initialization, which we can use
 * later to remove the requirement to add a line to the `main' routine.
 *
 * Note that /usr/include/asm/unistd.h defines syscall using either:
 *   /usr/include/asm-i386/unistd.h
 * or:
 *   /usr/include/asm-x86_64/unistd.h
 */

#ifndef _MTCP_SYS_H
#define _MTCP_SYS_H

#include <stdio.h>
#include <asm/unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <linux/version.h>

// Source code is taken from:  glibc-2.5/sysdeps/generic
/* Type to use for aligned memory operations.
   This should normally be the biggest type supported by a single load
   and store.  */
#undef opt_t
#undef OPSIZ
#define op_t    unsigned long int
#define OPSIZ   (sizeof(op_t))
#undef  __ptr_t
# define __ptr_t        void *

/* Type to use for unaligned operations.  */
typedef unsigned char byte;

/* Optimal type for storing bytes in registers.  */
#define reg_char        char


// From glibc-2.5/sysdeps/generic/memcopy.h:BYTE_COPY_FWD
// From glibc-2.5/sysdeps/generic/memcopy.h:BYTE_COPY_BWD
#define MTCP_BYTE_COPY_FWD(dst_bp, src_bp, nbytes)                                 \
  do                                                                          \
    {                                                                         \
      size_t __nbytes = (nbytes);                                             \
      while (__nbytes > 0)                                                    \
        {                                                                     \
          byte __x = ((byte *) src_bp)[0];                                    \
          src_bp += 1;                                                        \
          __nbytes -= 1;                                                      \
          ((byte *) dst_bp)[0] = __x;                                         \
          dst_bp += 1;                                                        \
        }                                                                     \
    } while (0)
#define MTCP_BYTE_COPY_BWD(dst_ep, src_ep, nbytes)                            \
  do                                                                          \
    {                                                                         \
      size_t __nbytes = (nbytes);                                             \
      while (__nbytes > 0)                                                    \
        {                                                                     \
          byte __x;                                                           \
          src_ep -= 1;                                                        \
          __x = ((byte *) src_ep)[0];                                         \
          dst_ep -= 1;                                                        \
          __nbytes -= 1;                                                      \
          ((byte *) dst_ep)[0] = __x;                                         \
        }                                                                     \
    } while (0)

#ifdef MTCP_SYS_MEMMOVE
# ifndef _MTCP_MEMMOVE_
#  define _MTCP_MEMMOVE_
// From glibc-2.5/string/memmove.c
static void *
mtcp_sys_memmove (a1, a2, len)
     void *a1;
     const void *a2;
     size_t len;
{
  unsigned long int dstp = (long int) a1 /* dest */;
  unsigned long int srcp = (long int) a2 /* src */;

  /* This test makes the forward copying code be used whenever possible.
     Reduces the working set.  */
  if (dstp - srcp >= len)       /* *Unsigned* compare!  */
    {
      /* Copy from the beginning to the end.  */

      /* There are just a few bytes to copy.  Use byte memory operations.  */
      MTCP_BYTE_COPY_FWD (dstp, srcp, len);
    }
  else
    {
      /* Copy from the end to the beginning.  */
      srcp += len;
      dstp += len;

      /* There are just a few bytes to copy.  Use byte memory operations.  */
      MTCP_BYTE_COPY_BWD (dstp, srcp, len);
    }

  return (a1 /* dest */);
}
# endif
#endif

#ifdef MTCP_SYS_MEMCPY
# ifndef _MTCP_MEMCPY_
#  define _MTCP_MEMCPY_
// From glibc-2.5/string/memcpy.c; and
/* Copy exactly NBYTES bytes from SRC_BP to DST_BP,
   without any assumptions about alignment of the pointers.  */
static void *
mtcp_sys_memcpy (dstpp, srcpp, len)
     void *dstpp;
     const void *srcpp;
     size_t len;
{
  unsigned long int dstp = (long int) dstpp;
  unsigned long int srcp = (long int) srcpp;

  /* SHOULD DO INITIAL WORD COPY BEFORE THIS. */
  /* There are just a few bytes to copy.  Use byte memory operations.  */
  MTCP_BYTE_COPY_FWD(dstp, srcp, len);
  return dstpp;
}
# endif
#endif

#if 0 /*  DEMONSTRATE_BUG */

// From glibc-2.5/string/memcmp.c:memcmp at end.
#ifndef _MTCP_MEMCMP_
# define _MTCP_MEMCMP_
static int
mtcp_sys_memcmp (s1, s2, len)
     const __ptr_t s1;
     const __ptr_t s2;
     size_t len;
{
  op_t a0;
  op_t b0;
  long int srcp1 = (long int) s1;
  long int srcp2 = (long int) s2;
  op_t res;

  /* There are just a few bytes to compare.  Use byte memory operations.  */
  while (len != 0)
    {
      a0 = ((byte *) srcp1)[0];
      b0 = ((byte *) srcp2)[0];
      srcp1 += 1;
      srcp2 += 1;
      res = a0 - b0;
      if (res != 0)
        return res;
      len -= 1;
    }

  return 0;
}
#endif

#endif /* DEMONSTRATE_BUG */

#ifdef MTCP_SYS_STRCHR
# ifndef _MTCP_STRCHR_
#  define _MTCP_STRCHR_
//   The  strchr() function from earlier C library returns a ptr to the first
//   occurrence  of  c  (converted  to a  char) in string s, or a
//   null pointer  if  c  does  not  occur  in  the  string. 
static char *mtcp_sys_strchr(const char *s, int c) {
  for (; *s != (char)'\0'; s++)
    if (*s == (char)c)
      return (char *)s;
  return NULL;
}
# endif
#endif

#ifdef MTCP_SYS_STRLEN
# ifndef _MTCP_STRLEN_
#  define _MTCP_STRLEN_
//   The  strlen() function from earlier C library calculates  the  length 
//     of  the string s, not including the terminating `\0' character.
static size_t mtcp_sys_strlen(const char *s) {
  size_t size = 0;
  for (; *s != (char)'\0'; s++)
    size++;
  return size;
}
# endif
#endif

#ifdef MTCP_SYS_STRCPY
# ifndef _MTCP_STRCPY_
#  define _MTCP_STRCPY_
static char * mtcp_sys_strcpy(char *dest, const char *source) {
  char *d = dest;
  for (; *source != (char)'\0'; source++)
    *d++ = *source;
  *d = '\0';
  return dest;
}
# endif
#endif

//======================================================================

// Rename it for cosmetic reasons.  We export mtcp_inline_syscall.
#define mtcp_inline_syscall(name, num_args, args...) \
                                        INLINE_SYSCALL(name, num_args, args)

/* We allocate this in mtcp_safemmap.c.  Files using mtcp_sys.h
 * are also linking with mtcp_safemmap.c.
 */
extern int mtcp_sys_errno;

// Define INLINE_SYSCALL.  In i386, need patch for 6 args

// sysdep-x86_64.h:
//   From glibc-2.5/sysdeps/unix/sysv/linux/x86_64/sysdep.h :  (define INLINE_SYSCALL)
// sysdep-i386.h:
//   Or glibc-2.5/sysdeps/unix/sysv/linux/i386/sysdep.h :  (define INLINE_SYSCALL)
// But all further includes from sysdep-XXX.h have been commented out.

#ifdef __i386__
// THIS CASE fOR i386 NEEDS PATCHING FOR 6 ARGUMENT CASE, SUCH AS MMAP.
// IT ONLY TRIES TO HANDLE UP TO 5 ARGS.
# include "sysdep-i386.h"

# ifndef __PIC__
#  define EXTRAVAR_6
#  define LOADARGS_6 \
    "sub $24,%%esp; mov %2,(%%esp); mov %3,4(%%esp); mov %4,8(%%esp);" \
    " mov %5,12(%%esp); mov %6,16(%%esp); mov %7,20(%%esp);" \
    " mov %%esp,%%ebx\n\t" /* sysdep-i386 then does:  mov %1,%%eax */
#  define RESTOREARGS_6 \
    RESTOREARGS_5 \
    "add $24,%%esp\n\t"
#  define ASMFMT_6(arg1, arg2, arg3, arg4, arg5, arg6) \
    ASMFMT_5(arg1, arg2, arg3, arg4, arg5), "0" (arg6)
# else
   not_implemented: "Nothing implemented for ! defined __PIC__"
# endif
#endif /* end __i386__ */

#ifdef __x86_64__
# include "sysdep-x86_64.h"
#endif /* end __x86_64__ */

#define __set_errno(Val) mtcp_sys_errno = (Val) /* required for sysdep-XXX.h */

// #include <sysdeps/unix/x86_64/sysdep.h>  is not needed.
#include <asm/unistd.h> /* translate __NR_getpid to syscall # using i386 or x86_64 */

//==================================================================

/* USAGE:  mtcp_inline_syscall:  second arg is number of args of system call */
#define mtcp_sys_read(args...)  mtcp_inline_syscall(read,3,args)
#define mtcp_sys_write(args...)  mtcp_inline_syscall(write,3,args)
#define mtcp_sys_lseek(args...)  mtcp_inline_syscall(lseek,3,args)
#define mtcp_sys_open(args...)  mtcp_inline_syscall(open,3,args)
    // mode  must  be  specified  when O_CREAT is in the flags, and is ignored
    //   otherwise.
#define mtcp_sys_open2(args...)  mtcp_sys_open(args,0777)
#define mtcp_sys_close(args...)  mtcp_inline_syscall(close,1,args)
#define mtcp_sys_access(args...)  mtcp_inline_syscall(access,2,args)
#define mtcp_sys_exit(args...)  mtcp_inline_syscall(exit,1,args)
#define mtcp_sys_pipe(args...)  mtcp_inline_syscall(pipe,1,args)
#define mtcp_sys_dup(args...)  mtcp_inline_syscall(dup,1,args)
#define mtcp_sys_dup2(args...)  mtcp_inline_syscall(dup2,2,args)
#define mtcp_sys_getpid(args...)  mtcp_inline_syscall(getpid,0)
#define mtcp_sys_getppid(args...)  mtcp_inline_syscall(getppid,0)
#define mtcp_sys_fork(args...)   mtcp_inline_syscall(fork,0)
#define mtcp_sys_vfork(args...)   mtcp_inline_syscall(vfork,0)
#define mtcp_sys_execve(args...)  mtcp_inline_syscall(execve,3,args)
#define mtcp_sys_wait4(args...)  mtcp_inline_syscall(wait4,4,args)
#define mtcp_sys_gettimeofday(args...)  mtcp_inline_syscall(gettimeofday,2,args)
#define mtcp_sys_mmap(args...)  mtcp_inline_syscall(mmap,6,args)
#define mtcp_sys_munmap(args...)  mtcp_inline_syscall(munmap,2,args)
#define mtcp_sys_mprotect(args...)  mtcp_inline_syscall(mprotect,3,args)
#define mtcp_sys_set_tid_address(args...)  mtcp_inline_syscall(set_tid_address,1,args)
#define mtcp_sys_brk(args...)  (void *)(mtcp_inline_syscall(brk,1,args))
#ifdef __NR_getdents
#define mtcp_sys_getdents(args...)  mtcp_inline_syscall(getdents,3,args)
#endif
#ifdef __NR_getdents64
#define mtcp_sys_getdents64(args...)  mtcp_inline_syscall(getdents64,3,args)
#endif

#define mtcp_sys_fcntl2(args...) mtcp_inline_syscall(fcntl,2,args)
#define mtcp_sys_fcntl3(args...) mtcp_inline_syscall(fcntl,3,args)
#define mtcp_sys_mkdir(args...) mtcp_inline_syscall(mkdir,2,args)

/* These functions are not defined for x86_64. */
#ifdef __i386__
# define mtcp_sys_get_thread_area(args...) \
    mtcp_inline_syscall(get_thread_area,1,args)
# define mtcp_sys_set_thread_area(args...) \
    mtcp_inline_syscall(set_thread_area,1,args)
#endif
#ifdef __x86_64__
# include <asm/prctl.h>
# include <sys/prctl.h>
  /* struct user_desc * uinfo; */
  /* In Linux 2.6.9 for i386, uinfo->base_addr is
   *   correctly typed as unsigned long int.
   * In Linux 2.6.9, uinfo->base_addr is  incorrectly typed as
   *   unsigned int.  So, we'll just lie about the type. 
   */
# if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,9)
   /* struct modify_ldt_ldt_s   was defined instead of   struct user_desc   */
#  define user_desc modify_ldt_ldt_s
# endif

/* This allocation hack will work only if calls to mtcp_sys_get_thread_area
 * and mtcp_sys_get_thread_area are both inside the same file (mtcp.c).
 * This is all because get_thread_area is not implemented for x86_64.
 */
static unsigned long int myinfo_gs;

# define mtcp_sys_get_thread_area(uinfo) \
    ( mtcp_inline_syscall(arch_prctl,2,ARCH_GET_FS, \
         (unsigned long int)(&(((struct user_desc *)uinfo)->base_addr))), \
      mtcp_inline_syscall(arch_prctl,2,ARCH_GET_GS, &myinfo_gs) \
    )
# define mtcp_sys_set_thread_area(uinfo) \
    ( mtcp_inline_syscall(arch_prctl,2,ARCH_SET_FS, \
	*(unsigned long int *)&(((struct user_desc *)uinfo)->base_addr)), \
      mtcp_inline_syscall(arch_prctl,2,ARCH_SET_GS, myinfo_gs) \
    )
#endif

/* ==================================================================
 * mtcp_sys_kernel_XXX() indicates it's particular to Linux, or glibc uses
 * a different version than the kernel version of the function.
 */

/* NOTE:  this calls kernel version of stat, not the glibc wrapper for stat
 * Needs: glibc_kernel_stat.h = glibc-2.5/sysdeps/unix/sysv/linux/kernel_stat.h
 *			for sake of st_mode, st_def, st_inode fields.
 *   For:	int stat(const char *file_name, struct kernel_stat *buf);
 *   For:	int lstat(const char *file_name, struct kernel_stat *buf);
 *
 * See glibc:/var/tmp/cooperma/glibc-2.5/sysdeps/unix/sysv/linux/i386/lxstat.c
 *   for other concerns about using stat in a 64-bit environment.
 * 
 * NOTE:  MTCP no longer needs the following two mtcp_sys_kernel_stat so
 * commenting them out.                                            --Kapil
 */
//#define mtcp_sys_kernel_stat(args...)  mtcp_inline_syscall(stat,2,args)
//#define mtcp_sys_kernel_lstat(args...)  mtcp_inline_syscall(lstat,2,args)

/* NOTE:  this calls kernel version of futex, not glibc sys_futex ("man futex")
 *   There is no library supporting futex.  Syscall is the only way to call it.
 *   For:	int futex(int *uaddr, int op, int val,
 *			 const struct timespec *timeout, int *uaddr2, int val3)
 *   "man 2 futex" and "man 4/7 futex" have limited descriptions.
 *   mtcp_internal.h has the macro defines used with futex.
 *
 */
#define mtcp_sys_kernel_futex(args...)  mtcp_inline_syscall(futex,6,args)

/* NOTE:  this calls kernel version of gettid, not glibc gettid ("man gettid")
 *   There is no library supporting gettid.  Syscall is the only way to call it.
 *   For:	pid_t gettid(void);
 */
#define mtcp_sys_kernel_gettid(args...)  mtcp_inline_syscall(gettid,0)

/* NOTE:  this calls kernel version of tkill, not glibc tkill ("man tkill")
 *   There is no library supporting tkill.  Syscall is the only way to call it.
 *   For:	pid_t tkill(void);
 */
#define mtcp_sys_kernel_tkill(args...)  mtcp_inline_syscall(tkill,2,args)

//==================================================================

#ifdef __x86_64__
# define eax rax
# define ebx rbx
# define ecx rcx
# define edx rax
# define ebp rbp
# define esi rsi
# define edi rdi
# define esp rsp
# define CLEAN_FOR_64_BIT(args...) CLEAN_FOR_64_BIT_HELPER(args)
# define CLEAN_FOR_64_BIT_HELPER(args...) #args
#else
# define CLEAN_FOR_64_BIT(args...) #args
#endif

#endif
