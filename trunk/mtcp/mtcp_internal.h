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

#ifndef _MTCP_INTERNAL_H
#define _MTCP_INTERNAL_H

#define IBV

#if defined(__x86_64__) || defined(__arm__)
// The alternative to using futex is to load in the pthread library,
//  which would be a real pain.  The __i386__ arch doesn't seem to be bothered
//  by this.
# define USE_FUTEX 1
#else
/* This used to be zero, but in Debian Etch (2.6.18 kernel), it complains
 * about missing pthread library.
 */
# define USE_FUTEX 1
#endif

#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <linux/version.h>
#include <linux/limits.h>

#if USE_FUTEX
# ifndef __user
// this is needed to compile futex.h on LXR/Suse10.2
#  define __user
# endif
# define u32 unsigned int
# include <linux/futex.h>
#else
# define FUTEX_WAIT 0
# define FUTEX_WAKE 1
#endif

// For i386 and x86_64, SETJMP currently has bugs.  Don't turn this
//   on for them until they are debugged.
// Default is to use  setcontext/getcontext.
#if defined(__arm__)
# define SETJMP /* setcontext/getcontext not defined for ARM glibc */
#endif

#ifdef SETJMP
# include <setjmp.h>
#else
# include <ucontext.h>
#endif

extern pid_t mtcp_saved_pid;
//extern int STOPSIGNAL;     // signal to use to signal other threads to stop for

#define MTCP_PRINTF(args...) \
  do { \
    mtcp_printf("[%d] %s:%d %s:\n  ", \
                mtcp_saved_pid, __FILE__, __LINE__, __FUNCTION__); \
    mtcp_printf(args); \
  } while (0)

#define MTCP_ASSERT(condition) \
  if (! (condition)) { \
    MTCP_PRINTF("Assertion failed: %s\n", #condition); \
    mtcp_abort(); \
  }

#ifdef DEBUG
# define DPRINTF(args...) MTCP_PRINTF(args)
#else
# define DPRINTF(args...) // debug printing
#endif

#ifdef TIMING
# define TPRINTF(x) mtcp_printf x  // timing printing
#else
# define TPRINTF(x) // timing printing
#endif

#ifndef DMTCP_VERSION
# define PACKAGE_VERSION ""
#else
# define PACKAGE_VERSION DMTCP_VERSION
#endif

#define VERSION_AND_COPYRIGHT_INFO                                              \
  BINARY_NAME " (DMTCP + MTCP) " PACKAGE_VERSION "\n"                           \
  "Copyright (C) 2006-2011  Jason Ansel, Michael Rieker, Kapil Arya, and\n"     \
  "                                                       Gene Cooperman\n"     \
  "This program comes with ABSOLUTELY NO WARRANTY.\n"                           \
  "This is free software, and you are welcome to redistribute it\n"             \
  "under certain conditions; see COPYING file for details.\n"

// ARM is missing asm/ldt.h in Ubuntu 11.10 (Linux 3.0, glibc-2.13)
#if defined(__arm__)
/* Structure passed to `modify_ldt', 'set_thread_area', and 'clone' calls.
   This seems to have been stable since the beginning of Linux 2.6  */
struct user_desc
{
  unsigned int entry_number;
  unsigned long int base_addr;
  unsigned int limit;
  unsigned int seg_32bit:1;
  unsigned int contents:2;
  unsigned int read_exec_only:1;
  unsigned int limit_in_pages:1;
  unsigned int seg_not_present:1;
  unsigned int useable:1;
  unsigned int empty:25;  /* Some variations leave this out. */
};
#else
# ifndef _LINUX_LDT_H
   // Define struct user_desc
#  include <asm/ldt.h>
# endif
  // WARNING: /usr/include/linux/version.h often has out-of-date version.
/* SuSE Linux Enterprise Server 9 uses Linux 2.6.5 and requires original
 * struct user_desc from /usr/include/.../ldt.h
 * Perhaps kernel was patched by backport.  Let's not re-define user_desc.
 */
/* RHEL 4 (Update 3) / Rocks 4.1.1-2.0 has <linux/version.h> saying
 *  LINUX_VERSION_CODE is 2.4.20 (and UTS_RELEASE=2.4.20)
 *  while uname -r says 2.6.9-34.ELsmp.  Here, it acts like a version earlier
 *  than the above 2.6.9.  So, we conditionalize on its 2.4.20 version.
 */
# if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
   /* struct modify_ldt_ldt_s   was defined instead of   struct user_desc   */
#  define user_desc modify_ldt_ldt_s
# endif
#endif

#include "mtcp.h"
#include "mtcp_sys.h"

typedef unsigned int uLong;
typedef unsigned short uWord;
typedef unsigned char uByte;
typedef char * VA; /* VA = virtual address */
#ifdef __i386__
typedef unsigned short mtcp_segreg_t;
#elif __x86_64__
typedef unsigned int mtcp_segreg_t;
#elif __arm__
typedef unsigned int mtcp_segreg_t;
#endif

#ifdef SETJMP
/* Retrieve saved stack pointer saved by sigsetjmp () at jmpbuf[SAVEDSP] */
# ifdef __i386__
// In eglibc-2.13/sysdeps/i386/jmpbuf-offsets.h, JB_SP = 4
#  define SAVEDSP 4
# elif __x86_64__
// In eglibc-2.13/sysdeps/x86_64/jmpbuf-offsets.h, JB_RSP = 6
#  define SAVEDSP 6
# elif __arm__
// #define SAVEDSP uc_mcontext.arm_sp
// In glibc-ports-2.14/sysdeps/arm/eabi/jmpbuf-offsets.h, __JMP_BUF_SP = 8
#  define SAVEDSP 8
# else
#  error "register for stack pointer not defined"
# endif
#else /* else:  use getcontext */
/* Retrieve saved stack pointer saved by setcontext () at jmpbuf[SAVEDSP] */
# ifdef __i386__
#  define SAVEDSP 7 /* ESP */
# elif __x86_64__
#  define SAVEDSP 15 /* RSP */
# elif __arm__
// NOT YET IMPLEMENTED /* SP */
# else
#  error "register for stack pointer not defined"
# endif
#endif

#ifdef SETJMP
// In field of 'struct Thread':
# define JMPBUF_SP jmpbuf[0].__jmpbuf[SAVEDSP]
#else
// In field of 'struct Thread':
# define JMPBUF_SP savctx.uc_mcontext.gregs[SAVEDSP]
#endif


typedef struct MtcpState MtcpState;
#if USE_FUTEX
struct MtcpState { int volatile value; };
# define MTCP_STATE_INITIALIZER {0}
#else
struct MtcpState { int volatile value;
                   pthread_cond_t cond;
                   pthread_mutex_t mutex;};
# define MTCP_STATE_INITIALIZER \
  {0, PTHREAD_COND_INITIALIZER, PTHREAD_MUTEX_INITIALIZER }
#endif

typedef struct Thread Thread;

struct Thread { Thread *next;         // next thread in 'threads' list
                Thread *prev;        // prev thread in 'threads' list
                int tid;              // this thread's id as returned by
                                      //   mtcp_sys_kernel_gettid ()
                int virtual_tid;      // this is the thread's "virtual" tid
                MtcpState state;      // see ST_... below
                Thread *parent;       // parent thread (or NULL if top-level
                                      //   thread)
                Thread *children;     // one of this thread's child threads
                Thread *siblings;     // one of this thread's sibling threads

                int clone_flags;      // parameters to __clone that created this
                                      //   thread
                int *parent_tidptr;
                int *given_tidptr;    // (this is what __clone caller passed in)
                int *actual_tidptr;   // (this is what we passed to the system
                                      //   call, either given_tidptr or
                                      //   &child_tid)
                int child_tid; // this is used for child_tidptr if the
                               //   original call did not ... have both
                               //   CLONE_CHILD_SETTID and CLONE_CHILD_CLEARTID
                int (*fn) (void *arg); // thread's initial function entrypoint
                void *arg;             //   and argument

                sigset_t sigblockmask; // blocked signals
                sigset_t sigpending;   // pending signals

                ///JA: new code ported from v54b
#ifdef SETJMP
                sigjmp_buf jmpbuf;     // sigjmp_buf saved by sigsetjmp on ckpt
#else
                ucontext_t savctx;     // context saved on suspend
#endif

                mtcp_segreg_t fs, gs;  // thread local storage pointers
                struct user_desc gdtentrytls[1];
              };

// MTCP_PAGE_SIZE must be page-aligned:  multiple of sysconf(_SC_PAGESIZE).
#define MTCP_PAGE_SIZE 4096
#define MTCP_PAGE_MASK (~(MTCP_PAGE_SIZE-1))
#define MTCP_PAGE_OFFSET_MASK (MTCP_PAGE_SIZE-1)
#if defined(__i386__) || defined(__x86_64__)
# if defined(__i386__) && defined(__PIC__)
// FIXME:  After DMTCP-1.2.5, this can be made only case for i386/x86_64
#  define RMB asm volatile ("lfence" \
                           : : : "eax", "ecx", "edx", "memory")
#  define WMB asm volatile ("sfence" \
                           : : : "eax", "ecx", "edx", "memory")
# else
#  define RMB asm volatile ("xorl %%eax,%%eax ; cpuid" \
                           : : : "eax", "ebx", "ecx", "edx", "memory")
#  define WMB asm volatile ("xorl %%eax,%%eax ; cpuid" \
                           : : : "eax", "ebx", "ecx", "edx", "memory")
# endif
#elif defined(__arm__)
# define RMB asm volatile ("dmb" : : : "memory")
# define WMB asm volatile ("dsb" : : : "memory")
#else
# error "instruction architecture not implemented"
#endif

#ifndef HIGHEST_VA
// If 32-bit process in 64-bit Linux, then Makefile overrides this address,
// with correct address for that case.
# ifdef __x86_64__
 /* There's a segment, 7fbfffb000-7fc0000000 rw-p 7fbfffb000 00:00 0;
  * What is it?  It's busy (EBUSY) when we try to unmap it.
  */
// #  define HIGHEST_VA ((VA)0xFFFFFF8000000000)
// #  define HIGHEST_VA ((VA)0x8000000000)
#  define HIGHEST_VA ((VA)0x7f00000000)
# else
#  define HIGHEST_VA ((VA)0xC0000000)
# endif
#endif
#define FILENAMESIZE 1024

#ifdef __x86_64__
# define MAGIC "MTCP64-V1.0"      // magic number at beginning of uncompressed
                                  //  checkpoint file
# define MAGIC_LEN 12             // length of magic number (including \0)
#else
# define MAGIC "MTCP-V1.0"        // magic number at beginning of checkpoint
                                  //  file (uncompressed)
# define MAGIC_LEN 10             // length of magic number (including \0)
#endif
#define MAGIC_FIRST 'M'
#define GZIP_FIRST 037
#ifdef HBICT_DELTACOMP
  #define HBICT_FIRST 'H'
#endif

#define NSCD_MMAP_STR1 "/var/run/nscd/"   /* OpenSUSE*/
#define NSCD_MMAP_STR2 "/var/cache/nscd"  /* Debian / Ubuntu*/
#define NSCD_MMAP_STR3 "/var/db/nscd"     /* RedHat / Fedora*/
#define DEV_ZERO_DELETED_STR "/dev/zero (deleted)"
#define DEV_NULL_DELETED_STR "/dev/null (deleted)"
#define SYS_V_SHMEM_FILE "/SYSV"
#ifdef IBV
# define INFINIBAND_SHMEM_FILE "/dev/infiniband/uverbs"
#endif
#define DELETED_FILE_SUFFIX " (deleted)"

/* Let MTCP_PROT_ZERO_PAGE be a unique bit mask
 * This assumes: PROT_READ == 0x1, PROT_WRITE == 0x2, and PROT_EXEC == 0x4
 */
#define MTCP_PROT_ZERO_PAGE (PROT_EXEC << 1)

#define STACKSIZE 1024      // size of temporary stack (in quadwords)
//#define MTCP_MAX_PATH 256   // maximum path length for mtcp_find_executable

typedef struct Jmpbuf Jmpbuf;

typedef union Area {
  struct {
  int type; // Content type (CS_XXX
  char *addr;   // args required for mmap to restore memory area
  size_t size;
  off_t filesize;
  int prot;
  int flags;
  off_t offset;
  struct {
    int fdnum;
    off_t offset;
    struct stat statbuf;
  } fdinfo;
  char name[FILENAMESIZE];
  };
  char _padding[4096];
} Area;

typedef struct DeviceInfo {
  unsigned int long devmajor;
  unsigned int long devminor;
  unsigned int long inodenum;
} DeviceInfo;

typedef struct mtcp_ckpt_image_hdr {
  int version;
  VA libmtcp_begin;
  size_t libmtcp_size;
  VA restore_start_fptr; /* will be bound to fnc, mtcp_restore_start */
  VA finish_restore_fptr; /* will be bound to fnc, finishrestore */
  struct rlimit stack_rlimit;
} mtcp_ckpt_image_hdr_t;

// order must match that in mtcp_jmpbuf.s
// struct Jmpbuf { uLong ebx, esi, edi, ebp, esp;
//                 uLong eip;
//                 uByte fpusave[232];
//               };

#define CS_STACKRLIMIT 101   // saved stack resource limit of this process
#define CS_RESTOREBEGIN 1    // beginning address of restore shareable image
#define CS_RESTORESIZE 2     // size (in bytes) of restore shareable image
#define CS_RESTORESTART 3    // start address of restore routine
#define CS_RESTOREIMAGE 4    // the actual restore image
#define CS_FINISHRESTORE 5   // mtcp.c's finishrestore routine entrypoint
#define CS_AREADESCRIP 6     // memory area descriptor (Area struct)
#define CS_AREACONTENTS 7    // memory area contents for an area
#define CS_AREAFILEMAP 8     // memory area file mapping info
#define CS_FILEDESCRS 9      // file descriptors follow
#define CS_THEEND 10         // end of checkpoint file

void mtcp_printf (char const *format, ...);

// gcc-3.4 issues a warning that noreturn function returns, if declared noreturn
static inline void mtcp_abort (void) __attribute__ ((noreturn));
static inline void mtcp_abort (void)
{
#if defined(__i386__) || defined(__x86_64__)
  asm volatile (CLEAN_FOR_64_BIT(hlt ; xor %eax,%eax ; mov (%eax),%eax) );
#elif defined(__arm__)
  asm volatile ("mov r0, #0 ; str r0, [r0]");
#endif
  for (;;);  /* Without this, gcc emits warning:  `noreturn' fnc does return */
}

/* cmpxchgl is only supported on Intel 486 processors and later. */
static inline int atomic_setif_int(int volatile *loc, int newval, int oldval)
{
  char rc;

#if defined(__i386__) || defined(__x86_64__)
  /* cmpxchgl:  compares "a" register (oldval) with operand2/dest (*loc).
   *  If they are equal, store operand1/src (newval) into operand2/dest (*loc).
   *  Set zero (equal) flag if equal, and so sete sets "a" register if equal.
   *  "=a" means compiler writes "a" register to "rc" on output.
   */
  asm volatile ("lock ; cmpxchgl %2,%1 ; sete %%al"
                : "=a" (rc)
                : "m" (*loc), "r" (newval), "a" (oldval)
                : "cc", "memory");
#elif defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_4)
  if (sizeof(int) != 4)
    mtcp_abort();
  rc = __sync_bool_compare_and_swap(loc, oldval, newval);
#elif defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_8)
  if (sizeof(int) != 8)
    mtcp_abort();
  rc = __sync_bool_compare_and_swap(loc, oldval, newval);
#else
  atomic_setif_int not defined for this architecture
#endif
  return (rc);
}

static inline int atomic_setif_ptr(void *volatile *loc, void *newval,
                                    void *oldval)
{
  char rc;

#if defined(__i386__) || defined(__x86_64__)
  asm volatile ("lock ; cmpxchg %2,%1 ; sete %%al"
                : "=a" (rc)
                : "m" (*loc), "r" (newval), "a" (oldval)
                : "cc", "memory");
#elif defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_4)
  if (sizeof(int) != 4)
    mtcp_abort();
  rc = __sync_bool_compare_and_swap(loc, oldval, newval);
#elif defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_8)
  if (sizeof(int) != 8)
    mtcp_abort();
  rc = __sync_bool_compare_and_swap(loc, oldval, newval);
#else
  atomic_setif_int not defined for this architecture
#endif
  return (rc);
}

extern VA mtcp_shareable_begin;
extern VA mtcp_shareable_end;

extern __attribute__ ((visibility ("hidden")))
int mtcp_sigaction(int sig, const struct sigaction *act,
		   struct sigaction *oact);

int  mtcp_have_thread_sysinfo_offset();
void *mtcp_get_thread_sysinfo(void);
void mtcp_set_thread_sysinfo(void *);
void mtcp_dump_tls (char const *file, int line);
//#ifndef MAXPATHLEN
//# define MAXPATHLEN 512
//#endif
// mtcp_restore_XXX are globals for arguments to mtcp_restore_start();
extern __attribute__ ((visibility ("hidden"))) int mtcp_restore_cpfd;
extern __attribute__ ((visibility ("hidden"))) int mtcp_restore_verify;
extern __attribute__ ((visibility ("hidden"))) pid_t mtcp_restore_gzip_child_pid;
extern __attribute__ ((visibility ("hidden"))) char mtcp_ckpt_newname[];
extern __attribute__ ((visibility ("hidden"))) char *mtcp_restore_cmd_file;
extern __attribute__ ((visibility ("hidden"))) char *mtcp_restore_argv[];
extern __attribute__ ((visibility ("hidden"))) char *mtcp_restore_envp[];

extern __attribute__ ((visibility ("hidden"))) VA mtcp_saved_break;
__attribute__ ((visibility ("hidden")))
char mtcp_saved_working_directory[PATH_MAX + 1];
extern void *mtcp_libc_dl_handle;
extern void *mtcp_old_dl_sysinfo_0;

void *mtcp_get_libc_symbol (char const *name);

__attribute__ ((visibility ("hidden")))
   void mtcp_state_init(MtcpState * state, int value);
__attribute__ ((visibility ("hidden")))
   void mtcp_state_destroy(MtcpState * state);
__attribute__ ((visibility ("hidden")))
   void mtcp_state_futex(MtcpState * state, int func, int val,
                         struct timespec const *timeout);
__attribute__ ((visibility ("hidden")))
   int mtcp_state_set(MtcpState * state, int value, int oldval);
__attribute__ ((visibility ("hidden")))
   int mtcp_state_value(MtcpState * state);

__attribute__ ((visibility ("hidden")))
void mtcp_restoreverything (int should_mmap_ckpt_image, VA finishrestore_fptr);
__attribute__ ((visibility ("hidden")))
void mtcp_printf (char const *format, ...);
void mtcp_maybebpt (void);
__attribute__ ((visibility ("hidden")))
void *mtcp_safemmap(void *start, size_t length, int prot, int flags, int fd,
                    off_t offset);
int mtcp_setjmp (Jmpbuf *jmpbuf);
void mtcp_longjmp (Jmpbuf *jmpbuf, int retval);
int mtcp_safe_open(char const *filename, int flags, mode_t mode);

__attribute__ ((visibility ("hidden")))
void mtcp_get_memory_region_of_this_library(VA *startaddr, VA *endaddr);
__attribute__ ((visibility ("hidden")))
int mtcp_selfmap_readline(int selfmapfd, VA *startaddr, VA *endaddr,
	                  off_t *file_offset);
__attribute__ ((visibility ("hidden")))
int mtcp_selfmap_open();
__attribute__ ((visibility ("hidden")))
int mtcp_selfmap_close(int selfmapfd);


void mtcp_checkpointeverything(const char *temp_ckpt_filename,
                               const char *perm_ckpt_filename);
void mtcp_finishrestore(void);
void mtcp_restore_start(int fd, int verify, int should_mmap_ckpt_image,
                        pid_t gzip_child_pid,
                        char *ckpt_newname, char *cmd_file,
                        char *argv[], char *envp[]);
void mtcp_writeckpt_init(VA restore_start_fptr, VA finishrestore_fptr);
#endif
