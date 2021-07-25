/*****************************************************************************
 * Copyright (C) 2006-2008 Michael Rieker <mrieker@nii.net>                  *
 * Copyright (C) 2014 Kapil Arya <kapil@ccs.neu.edu>                         *
 * Copyright (C) 2014 Gene Cooperman <gene@ccs.neu.edu>                      *
 *                                                                           *
 * DMTCP is free software: you can redistribute it and/or                    *
 * modify it under the terms of the GNU Lesser General Public License as     *
 * published by the Free Software Foundation, either version 3 of the        *
 * License, or (at your option) any later version.                           *
 *                                                                           *
 * DMTCP is distributed in the hope that it will be useful,                  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of            *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
 * GNU Lesser General Public License for more details.                       *
 *                                                                           *
 * You should have received a copy of the GNU Lesser General Public          *
 * License along with DMTCP.  If not, see <http://www.gnu.org/licenses/>.    *
 *****************************************************************************/

/* README!!!!
 * For ARM, you can ignore almost all of this code.  Only the
 *  mtcp_sys_XXX() macros are needed for ARM, along with the code at the end
 *  of this file that overrides the definition of mtcp_inline_syscall(),
 *  which directly calls syscall, defined in syscall-arm.S.
 *  Even the name mtcp_inline_syscall() is bad, now that we call
 *  a syscall function defined in syscall-arm.S.
 * After the DMTCP-2.2 release, we will do the same thing for Intel.
 */

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
# define _MTCP_SYS_H

# include <asm/unistd.h>
# include <fcntl.h>
# include <linux/version.h>
# include <stdio.h>
# include <sys/mman.h>
# include <sys/stat.h>
# include <sys/syscall.h> /* For SYS_xxx definitions needed in expansions. */
# include <sys/types.h>
# include <unistd.h>

// Source code is taken from:  glibc-2.5/sysdeps/generic

/* Type to use for aligned memory operations.
   This should normally be the biggest type supported by a single load
   and store.  */
# undef opt_t
# undef OPSIZ
# undef  __ptr_t

// #define op_t    unsigned long int
// #define OPSIZ   (sizeof(op_t))
// # define __ptr_t        void *

/* Type to use for unaligned operations.  */

// typedef unsigned char byte;

/* Optimal type for storing bytes in registers.  */

// #define reg_char        char


// ======================================================================

// Rename it for cosmetic reasons.  We export mtcp_inline_syscall.
// In gcc-4.8, we now need "<space>, ##args" below.
// SEE:  http://gcc.gnu.org/onlinedocs/cpp/Variadic-Macros.html
# define mtcp_inline_syscall(name, num_args, args ...) \
  INLINE_SYSCALL(name, num_args, ## args)

/* We allocate this in mtcp_safemmap.c.  Files using mtcp_sys.h
 * are also linking with mtcp_safemmap.c.
 */
extern int mtcp_sys_errno;

// Define INLINE_SYSCALL.  In i386, need patch for 6 args

// sysdep-x86_64.h:
// From glibc-2.5/sysdeps/unix/sysv/linux/x86_64/sysdep.h:
// (define INLINE_SYSCALL)
// sysdep-i386.h:
// Or glibc-2.5/sysdeps/unix/sysv/linux/i386/sysdep.h: (define INLINE_SYSCALL)
// But all further includes from sysdep-XXX.h have been commented out.

# ifdef __i386__

/* AFTER DMTCP RELEASE 1.2.5, MAKE USE_PROC_MAPS CASE THE ONLY CASE AND DO:
 * mv sysdep-i386-new.h sysdep-i386.h
 */

// THIS CASE fOR i386 NEEDS PATCHING FOR 6 ARGUMENT CASE, SUCH AS MMAP.
// IT ONLY TRIES TO HANDLE UP TO 5 ARGS.
#  include "sysdep/sysdep-i386.h"

#  ifndef __PIC__

// NOTE:  Some misinformation on web and newer glibc:sysdep-i386.h says 6-arg
// syscalls use:  eax, ebx, ecx, edx, esi, edi, ebp
// Maybe this was true historically, but it really uses eax for syscall
// number, and sets ebx to point to the 6 args (which typically are on stack).
#   define EXTRAVAR_6
#   define LOADARGS_6                                                \
  "sub $24,%%esp; mov %2,(%%esp); mov %3,4(%%esp); mov %4,8(%%esp);" \
  " mov %5,12(%%esp); mov %6,16(%%esp); mov %7,20(%%esp);"           \
  " mov %%esp,%%ebx\n\t"   /* sysdep-i386 then does:  mov %1,%%eax */
#   define RESTOREARGS_6 \
  RESTOREARGS_5          \
  "add $24,%%esp\n\t"
#   define ASMFMT_6(arg1, arg2, arg3, arg4, arg5, arg6) \
  ASMFMT_5(arg1, arg2, arg3, arg4, arg5), "0" (arg6)
#  else // ifndef __PIC__

// TO SEE EXAMPLES OF MACROS, TRY:
// cpp -fPIC -DPIC -dM mtcp_safemmap.c | grep '_5 '
// MODEL TEMPLATE IN sysdep-i386.h:
// #define INTERNAL_SYSCALL(name, err, nr, args...)
// ({
// register unsigned int resultvar;
// EXTRAVAR_##nr
// asm volatile (
// LOADARGS_##nr
// "movl %1, %%eax\n\t"
// "int $0x80\n\t"
// RESTOREARGS_##nr
// : "=a" (resultvar)
// : "i" (__NR_##name) ASMFMT_##nr(args) : "memory", "cc");
// (int) resultvar; })
// PIC uses ebp as base pointer for variables.  Save it last, retore it first.
#   if 0
#    define EXTRAVAR_6 int _xv1, _xv2;
#    define LOADARGS_6                                 \
                       LOADARGS_5 "movl %%esp, %4\n\t" \
             "movl %%ebp, %%esp\n\t" "movl %9, %%ebp\n\t"
#    define RESTOREARGS_6                      \
                       "movl %%esp, %%ebp\n\t" \
  "movl %4, %%esp\n\t" RESTOREARGS_5
#    define ASMFMT_6(arg1, arg2, arg3, arg4, arg5, arg6)                  \
  , "0" (arg1),                                                           \
  "m" (_xv1), "m" (_xv2), "c" (arg2), "d" (arg3), "S" (arg4), "D" (arg5), \
  "rm" (arg6)
#   else // if 0

// NOTE:  Some misinformation on web and newer glibc:sysdep-i386.h says 6-arg
// syscalls use:  eax, ebx, ecx, edx, esi, edi, ebp
// Maybe this was true historically, but it really uses eax for syscall
// number, and sets ebx to point to the 6 args (which typically are on stack).
// eax is free register, since it is set to syscall number just before: int 0x80
#    define EXTRAVAR_6
#    define LOADARGS_6                                                      \
  "sub $28,%%esp; mov %2,(%%esp); mov %3,4(%%esp); mov %4,8(%%esp);"        \
  " mov %5,12(%%esp); mov %6,16(%%esp); mov %7,%%eax; mov %%eax,20(%%esp);" \
  " mov %%ebx,24(%%esp); mov %%esp,%%ebx\n\t"                               \
  /* sysdep-i386 then does:  mov %1,%%eax */
#    define RESTOREARGS_6 "mov 24(%%esp),%%ebx\n\t" "add $28,%%esp\n\t"

/*
#  define ASMFMT_6(arg1, arg2, arg3, arg4, arg5, arg6) \
    ASMFMT_5(arg1, arg2, arg3, arg4, arg5), "rm" (arg6)
*/
#    define ASMFMT_6(arg1, arg2, arg3, arg4, arg5, arg6) \
  , "0" (arg1),                                          \
  "c" (arg2), "d" (arg3), "S" (arg4), "D" (arg5), "rm" (arg6)
#   endif // if 0
#  endif // ifndef __PIC__

# elif __x86_64__
#  include "sysdep/sysdep-x86_64.h"

# elif __arm__

// COPIED FROM:  glibc-ports-2.14/sysdeps/unix/sysv/linux/arm/eabi/sysdep.h
// In turn, this calls "sysdep-arm.h" from .../linux/arm/sysdep.h
// We are removing this dependency, now that we're introducing syscall-arm.S
// In the future, we'll do the same for i386 and x86_64 to simplify the logic.
// # include "sysdep/sysdep-arm-eabi.h"

# elif defined(__aarch64__)

// FIXME:  Now that we're introducing syscall-aarch64.S, we should abandon this.
// # include "sysdep-aarch64.h"
# else // ifdef __i386__
#  error "Missing sysdep.h file for this architecture."
# endif /* end __arm__ */

// FIXME:  Get rid of mtcp_sys_errno
// Must first define multi-threaded errno when glibc not present.
# define __set_errno(Val) (mtcp_sys_errno = (Val))  /* required for sysdep-XXX.h
                                                       */

// #define __set_errno(Val) ( errno = mtcp_sys_errno = (Val) ) /* required for
// sysdep-XXX.h */

// #include <sysdeps/unix/x86_64/sysdep.h>  is not needed.
// translate __NR_getpid to syscall # using i386 or x86_64
# include <asm/unistd.h>

/* getdents() fills up the buffer not with 'struct dirent's as might be
 * expected, but with custom 'struct linux_dirent's.  This structure, however,
 * must be manually defined.  This definition is taken from the getdents(2) man
 * page.
 */
struct linux_dirent {
  long d_ino;
  off_t d_off;
  unsigned short d_reclen;
  char d_name[];
};

// ==================================================================

/* USAGE:  mtcp_inline_syscall:  second arg is number of args of system call */
# define mtcp_sys_read(args ...)  mtcp_inline_syscall(read, 3, args)
# define mtcp_sys_write(args ...) mtcp_inline_syscall(write, 3, args)
# define mtcp_sys_lseek(args ...) mtcp_inline_syscall(lseek, 3, args)

/*
 * As of glibc-2.18, open() has been replaced by openat(). glibc converts
 * calls to open() to openat(), but NOT for Aarch64
 */
# if defined(__aarch64__)
#  define mtcp_sys_open(args ...) mtcp_inline_syscall(openat, 4, AT_FDCWD, args)
# else // if defined(__aarch64__)
#  define mtcp_sys_open(args ...) mtcp_inline_syscall(open, 3, args)
# endif // if defined(__aarch64__)

// mode  must  be  specified  when O_CREAT is in the flags, and is ignored
// otherwise.
# define mtcp_sys_open2(args ...)     mtcp_sys_open(args, 0777)
# define mtcp_sys_ftruncate(args ...) mtcp_inline_syscall(ftruncate, 2, args)
# define mtcp_sys_close(args ...)     mtcp_inline_syscall(close, 1, args)
# if defined(__aarch64__)
#  define mtcp_sys_access(path, mode) \
  mtcp_inline_syscall(faccessat, 4, AT_FDCWD, path, mode, AT_EACCESS)
#  define mtcp_sys_unlink(path)                                     \
                                      mtcp_inline_syscall(unlinkat, \
                      3,                                            \
                      AT_FDCWD,                                     \
                      path,                                         \
                      0)
# else // if defined(__aarch64__)
#  define mtcp_sys_unlink(args ...)   mtcp_inline_syscall(unlink, 1, args)
#  define mtcp_sys_access(args ...)   mtcp_inline_syscall(access, 2, args)
# endif // if defined(__aarch64__)
# define mtcp_sys_fchmod(args ...)    mtcp_inline_syscall(fchmod, 2, args)
# if defined(__aarch64__)
#  define mtcp_sys_rename(oldpath, newpath) \
  mtcp_inline_syscall(renameat, 4, AT_FDCWD, oldpath, AT_FDCWD, newpath)
# else // if defined(__aarch64__)
#  define mtcp_sys_rename(args ...) mtcp_inline_syscall(rename, 2, args)
# endif // if defined(__aarch64__)
# define mtcp_sys_exit(args ...)    mtcp_inline_syscall(exit, 1, args)
# if defined(__aarch64__)
#  define mtcp_sys_pipe(fd)         mtcp_inline_syscall(pipe2, 2, fd, 0)
# else // if defined(__aarch64__)
#  define mtcp_sys_pipe(args ...)   mtcp_inline_syscall(pipe, 1, args)
# endif // if defined(__aarch64__)
# define mtcp_sys_dup(args ...)     mtcp_inline_syscall(dup, 1, args)
# if defined(__aarch64__)
#  define mtcp_sys_dup2(oldfd, newfd) \
  mtcp_inline_syscall(dup3, 3, oldfd, newfd, NULL)
# else // if defined(__aarch64__)
#  define mtcp_sys_dup2(args ...)   mtcp_inline_syscall(dup2, 2, args)
# endif // if defined(__aarch64__)
# define mtcp_sys_getpid(args ...)  mtcp_inline_syscall(getpid, 0)
# define mtcp_sys_getppid(args ...) mtcp_inline_syscall(getppid, 0)
# if defined(__aarch64__)
#  define mtcp_sys_fork(args ...) \
  mtcp_inline_syscall(clone, 4, SIGCHLD, NULL, NULL, NULL)
# else // if defined(__aarch64__)
#  define mtcp_sys_fork(args ...)  mtcp_inline_syscall(fork, 0)
# endif // if defined(__aarch64__)
# define mtcp_sys_vfork(args ...)  mtcp_inline_syscall(vfork, 0)
# define mtcp_sys_execve(args ...) mtcp_inline_syscall(execve, 3, args)
# define mtcp_sys_wait4(args ...)  mtcp_inline_syscall(wait4, 4, args)
# define mtcp_sys_gettimeofday(args ...)                             \
                                   mtcp_inline_syscall(gettimeofday, \
                      2,                                             \
                      args)
# if defined(__i386__) || defined(__x86_64__) || defined(__aarch64__)
#  define mtcp_sys_mmap(args ...)                                    \
                                   (void *)mtcp_inline_syscall(mmap, \
                              6,                                     \
                              args)
# elif defined(__arm__)

/* ARM Linux kernel doesn't support mmap: translate to newer mmap2 */
#  define mtcp_sys_mmap(addr, length, prot, flags, fd, offset) \
  (void *)mtcp_inline_syscall(mmap2,                           \
                              6,                               \
                              addr,                            \
                              length,                          \
                              prot,                            \
                              flags,                           \
                              fd,                              \
                              offset / 4096)
# elif defined(__aarch64__)
#  define mtcp_sys_mmap(args ...) (void *)mtcp_inline_syscall(mmap, 6, args)
# else // if defined(__i386__) || defined(__x86_64__) || defined(__aarch64__)
#  error "getrlimit kernel call not implemented in this architecture"
# endif // if defined(__i386__) || defined(__x86_64__) || defined(__aarch64__)
# define mtcp_sys_mremap(args ...)                                        \
                                      (void *)mtcp_inline_syscall(mremap, \
                              5,                                          \
                              args)
# define mtcp_sys_munmap(args ...)    mtcp_inline_syscall(munmap, 2, args)
# define mtcp_sys_msync(args ...)    mtcp_inline_syscall(msync, 3, args)
# define mtcp_sys_mprotect(args ...)  mtcp_inline_syscall(mprotect, 3, args)
# define mtcp_sys_nanosleep(args ...) mtcp_inline_syscall(nanosleep, 2, args)
# define mtcp_sys_brk(args ...)                                            \
                                      (void *)(mtcp_inline_syscall(brk, 1, \
                               args))
# define mtcp_sys_rt_sigaction(args ...)                                \
                                      mtcp_inline_syscall(rt_sigaction, \
                      4,                                                \
                      args)
# define mtcp_sys_set_tid_address(args ...) \
  mtcp_inline_syscall(set_tid_address, 1, args)

// #define mtcp_sys_stat(args...) mtcp_inline_syscall(stat, 2, args)
# define mtcp_sys_getuid(args ...)  mtcp_inline_syscall(getuid, 0)
# define mtcp_sys_geteuid(args ...) mtcp_inline_syscall(geteuid, 0)

# define mtcp_sys_personality(args ...)                                 \
                                    mtcp_inline_syscall(personality, 1, \
                      args)
# if defined(__aarch64__)

// As of glibc-2.18, readlink() has been replaced by readlinkat()
// glibc includes checks for old call, except in Aarch64
#  define mtcp_sys_readlink(args ...) mtcp_inline_syscall(readlinkat, 3, args)
# else // if defined(__aarch64__)
#  define mtcp_sys_readlink(args ...) mtcp_inline_syscall(readlink, 3, args)
# endif // if defined(__aarch64__)
# if defined(__i386__) || defined(__x86_64__) || defined(__aarch64__)

/* Should this be changed to use newer ugetrlimit kernel call? */
#  define mtcp_sys_getrlimit(args ...) mtcp_inline_syscall(getrlimit, 2, args)
# elif defined(__arm__)

/* EABI ARM exclusively uses newer ugetrlimit kernel API, and not getrlimit */
#  define mtcp_sys_getrlimit(args ...) mtcp_inline_syscall(ugetrlimit, 2, args)
# elif defined(__aarch64__)
#  define mtcp_sys_getrlimit(args ...) mtcp_inline_syscall(getrlimit, 2, args)
# else // if defined(__i386__) || defined(__x86_64__) || defined(__aarch64__)
#  error "getrlimit kernel call not implemented in this architecture"
# endif // if defined(__i386__) || defined(__x86_64__) || defined(__aarch64__)
# define mtcp_sys_setrlimit(args ...) mtcp_inline_syscall(setrlimit, 2, args)

# ifdef __NR_getdents
#  define mtcp_sys_getdents(args ...) mtcp_inline_syscall(getdents, 3, args)

/* Note that getdents() does not fill the buf with 'struct dirent's, but
 * instead with 'struct linux_dirent's.  These must be defined manually, and
 * in our case have been defined earlier in this file. */
# endif // ifdef __NR_getdents
# ifdef __NR_getdents64
#  define mtcp_sys_getdents64(args ...) mtcp_inline_syscall(getdents64, 3, args)
# endif // ifdef __NR_getdents64

# define mtcp_sys_fcntl2(args ...)      mtcp_inline_syscall(fcntl, 2, args)
# define mtcp_sys_fcntl3(args ...)      mtcp_inline_syscall(fcntl, 3, args)
# if defined(__aarch64__)
#  define mtcp_sys_mkdir(args ...)                                   \
                                        mtcp_inline_syscall(mkdirat, \
                      3,                                             \
                      AT_FDCWD,                                      \
                      args)
# else // if defined(__aarch64__)
#  define mtcp_sys_mkdir(args ...)      mtcp_inline_syscall(mkdir, 2, args)
# endif // if defined(__aarch64__)

# ifdef __i386__
#  define mtcp_sys_get_thread_area(args ...) \
  mtcp_inline_syscall(get_thread_area, 1, args)
#  define mtcp_sys_set_thread_area(args ...) \
  mtcp_inline_syscall(set_thread_area, 1, args)
# endif // ifdef __i386__

# if defined(__aarch64__)

// FIXME:  Is this code still needed?
#  ifdef MTCP_SYS_GET_SET_THREAD_AREA

static unsigned int myinfo_gs;

#   define mtcp_sys_get_thread_area(uinfo)                             \
  ({ asm volatile ("mrs   %0, tpidr_el0"                               \
                   : "=r" (myinfo_gs));                                \
     myinfo_gs = myinfo_gs - 1216; /* sizeof(struct pthread) = 1216 */ \
     *(unsigned long int *)&(((struct user_desc *)uinfo)->base_addr)   \
       = myinfo_gs;                                                    \
     myinfo_gs; })
#   define mtcp_sys_set_thread_area(uinfo)                              \
  ({ myinfo_gs =                                                        \
       *(unsigned long int *)&(((struct user_desc *)uinfo)->base_addr); \
     myinfo_gs = myinfo_gs + 1216;                                      \
     asm volatile ("msr     tpidr_el0, %[gs]" : :[gs] "r" (myinfo_gs)); \
     0;  })
#  endif /* end MTCP_SYS_GET_SET_THREAD_AREA */
# endif /* end __aarch64__ */

/*****************************************************************************
 * mtcp_sys_kernel_XXX() indicates it's particular to Linux, or glibc uses
 * a different version than the kernel version of the function.
 *
 * NOTE:  this calls kernel version of stat, not the glibc wrapper for stat
 * Needs: glibc_kernel_stat.h = glibc-2.5/sysdeps/unix/sysv/linux/kernel_stat.h
 *			for sake of st_mode, st_def, st_inode fields.
 *   For:	int stat(const char *file_name, struct kernel_stat *buf);
 *   For:	int lstat(const char *file_name, struct kernel_stat *buf);
 *
 * See glibc:/var/tmp/cooperma/glibc-2.5/sysdeps/unix/sysv/linux/i386/lxstat.c
 *   for other concerns about using stat in a 64-bit environment.
 *****************************************************************************/

/* NOTE:  MTCP no longer needs the following two mtcp_sys_kernel_stat so
 * commenting them out.                                            --Kapil
 *
 * #define mtcp_sys_kernel_stat(args...)  mtcp_inline_syscall(stat,2,args)
 * #define mtcp_sys_kernel_lstat(args...)  mtcp_inline_syscall(lstat,2,args)
 */

/* NOTE:  this calls kernel version of futex, not glibc sys_futex ("man futex")
 *   There is no library supporting futex.  Syscall is the only way to call it.
 *   For:	int futex(int *uaddr, int op, int val,
 *			 const struct timespec *timeout, int *uaddr2, int val3)
 *   "man 2 futex" and "man 4/7 futex" have limited descriptions.
 *   mtcp_internal.h has the macro defines used with futex.
 */
# define mtcp_sys_kernel_futex(args ...)  mtcp_inline_syscall(futex, 6, args)

/* NOTE:  this calls kernel version of gettid, not glibc gettid ("man gettid")
 *   There is no library supporting gettid.  Syscall is the only way to call it.
 *   For:	pid_t gettid(void);
 */
# define mtcp_sys_kernel_gettid(args ...) mtcp_inline_syscall(gettid, 0)

/* NOTE:  this calls kernel version of tkill, not glibc tkill ("man tkill")
 *   There is no library supporting tkill.  Syscall is the only way to call it.
 *   For:	pid_t tkill(void);
 */
# define mtcp_sys_kernel_tkill(args ...)  mtcp_inline_syscall(tkill, 2, args)
# define mtcp_sys_kernel_tgkill(args ...) mtcp_inline_syscall(tgkill, 3, args)

# if defined(__arm__)

/* NOTE:  set_tls is an ARM-specific call, with only a kernel API.
 *   We use the _RAW form;  otherwise, set_tls would expand to __SYS_set_tls
 *   This is a modification of sysdep-arm.h:INLINE_SYSCALL()
 */
#  define INLINE_SYSCALL_RAW(name, nr, args ...)                         \
  ({ unsigned int _sys_result = INTERNAL_SYSCALL_RAW(name, , nr, args);  \
     if (__builtin_expect(INTERNAL_SYSCALL_ERROR_P(_sys_result, ), 0)) { \
       __set_errno(INTERNAL_SYSCALL_ERRNO(_sys_result, ));               \
       _sys_result = (unsigned int)-1;                                   \
     }                                                                   \
     (int)_sys_result; })

/* Next macro uses 'mcr', a kernel-mode instr. on ARM */
#  define mtcp_sys_kernel_set_tls(args ...) \
  INLINE_SYSCALL_RAW(__ARM_NR_set_tls, 1, args)
# endif // if defined(__arm__)

# ifdef __arm__
// https://elixir.bootlin.com/linux/v3.1/source/arch/arm/include/asm/unistd.h#L411
#  define __ARM_NR_cacheflush 0x0f0002
#  define mtcp_sys_kernel_cacheflush(args ...) \
  INLINE_SYSCALL_RAW(__ARM_NR_cacheflush, 3, args)
# endif

// ==================================================================

# ifdef __x86_64__
#  define eax rax
#  define ebx rbx
#  define ecx rcx
#  define edx rax
#  define ebp rbp
#  define esi rsi
#  define edi rdi
#  define esp rsp
#  define CLEAN_FOR_64_BIT(args ...)        CLEAN_FOR_64_BIT_HELPER(args)
#  define CLEAN_FOR_64_BIT_HELPER(args ...) # args
# elif __i386__
#  define CLEAN_FOR_64_BIT(args ...)        # args
# else // ifdef __x86_64__
#  define CLEAN_FOR_64_BIT(args ...)        "CLEAN_FOR_64_BIT_undefined"
# endif // ifdef __x86_64__

# if defined(__arm__) || defined(__aarch64__)

/*
 * NOTE:  The following are not defined for __ARM_EABI__.  Each redirects
 * to a different kernel call.  See __NR_getrlimit__ for an example.
 * __NR_time, __NR_umount, __NR_stime, __NR_alarm, __NR_utime, __NR_getrlimit,
 * __NR_select, __NR_readdir, __NR_mmap, __NR_socketcall, __NR_syscall,
 * __NR_ipc, __NR_get_thread_area, __NR_set_thread_area
 */
# endif // if defined(__arm__) || defined(__aarch64__)

// gcc-3.4 issues a warning that noreturn function returns, if declared noreturn
static inline void mtcp_abort(void) __attribute__((noreturn));
static inline void
mtcp_abort(void)
{
# if defined(__i386__) || defined(__x86_64__)
  asm volatile (CLEAN_FOR_64_BIT(hlt; xor %eax, %eax; mov (%eax), %eax));
# elif defined(__arm__)
  asm volatile ("mov r0, #0 ; str r0, [r0]");
# elif defined(__aarch64__)
  asm volatile ("mov x0, #0 ; str x0, [X0]");
# endif // if defined(__i386__) || defined(__x86_64__)
  for (;;) { /* Without this, gcc emits warning:  `noreturn' fnc does return */
  }
}
#endif // ifndef _MTCP_SYS_H

// For DMTCP-2.2, we are defining MTCP_SYS_ERRNO_ON_STACK on command line.
// After that, we will move this code to libmtcp.so, where we can again
// use global variables, and so will not need this defined.
#if defined(__arm__) || defined(__aarch64__)
# undef mtcp_inline_syscall
# undef INLINE_SYSCALL_RAW
# if defined(__arm__)
extern unsigned int mtcp_syscall();
# elif defined(__aarch64__)
extern unsigned long mtcp_syscall();
# endif // if defined(__arm__)
# ifdef MTCP_SYS_ERRNO_ON_STACK
#  define mtcp_inline_syscall(name, num_args, ...) \
  mtcp_syscall(SYS_ ## name, &mtcp_sys_errno, ## __VA_ARGS__)
#  define INLINE_SYSCALL_RAW(name, nr, ...) \
  mtcp_syscall(name, &mtcp_sys_errno, ## __VA_ARGS__)
# else // ifdef MTCP_SYS_ERRNO_ON_STACK
#  define mtcp_inline_syscall(name, num_args, args ...) \
  mtcp_syscall(SYS_ ## name, args)
#  define INLINE_SYSCALL_RAW(name, nr, ...) \
  mtcp_syscall(name, ## __VA_ARGS__)
# endif // ifdef MTCP_SYS_ERRNO_ON_STACK
#endif // if defined(__arm__) || defined(__aarch64__)
