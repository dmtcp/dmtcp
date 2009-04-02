/*****************************************************************************
 *   Copyright (C) 2006-2008 by Michael Rieker, Jason Ansel, Kapil Arya, and *
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

#ifdef __x86_64__
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
#include <linux/version.h>

#if USE_FUTEX
#  ifndef __user
// this is needed to compile futex.h on LXR/Suse10.2
#    define __user
#  endif
#  define u32 unsigned int
#  include <linux/futex.h>
#else
#  define FUTEX_WAIT 0
#  define FUTEX_WAKE 1
#endif

#ifdef DEBUG
#define DPRINTF(x) mtcp_printf x  // debugging printing
#else
#define DPRINTF(x) // debugging printing
#endif

#ifdef TIMING
#define TPRINTF(x) mtcp_printf x  // timing printing
#else
#define TPRINTF(x) // timing printing
#endif

#if 0
/* Structure passed to `modify_ldt', 'set_thread_area', and 'clone' calls.  */
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
  unsigned int empty:25;
};
#else
# ifndef _LINUX_LDT_H
   // Define struct user_desc
#  include <asm/ldt.h>
# endif
  // WARNING: /usr/include/linux/version.h often has out-of-date version.
# if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,9)
   /* struct modify_ldt_ldt_s   was defined instead of   struct user_desc   */
#  define user_desc modify_ldt_ldt_s
# endif
#endif

#include "mtcp.h"
#include "mtcp_sys.h"

typedef unsigned int uLong;
typedef unsigned short uWord;
typedef unsigned char uByte;
typedef unsigned long VA; /* VA = virtual address */
#ifdef __i386__
typedef unsigned short mtcp_segreg_t;
#endif
#ifdef __x86_64__
typedef unsigned int mtcp_segreg_t;
#endif

#define PAGE_SIZE 4096
#define RMB asm volatile ("xorl %%eax,%%eax ; cpuid" : : : "eax", "ebx", "ecx", "edx", "memory")
#define WMB asm volatile ("xorl %%eax,%%eax ; cpuid" : : : "eax", "ebx", "ecx", "edx", "memory")

#ifndef HIGHEST_VA
// If 32-bit process in 64-bit Linux, then Makefile overrides this address,
// with correct address for that case.
# ifdef __x86_64__
 /* There's a segment, 7fbfffb000-7fc0000000 rw-p 7fbfffb000 00:00 0;
  * What is it?  It's busy (EBUSY) when we try to unmap it.
  */
// #  define HIGHEST_VA 0xFFFFFF8000000000
// #  define HIGHEST_VA 0x8000000000
#  define HIGHEST_VA 0x7f00000000
# else
#  define HIGHEST_VA 0xC0000000
# endif
#endif
#define FILENAMESIZE 1024

#ifdef __x86_64__
# define MAGIC "MTCP64-V1.0"      // magic number at beginning of uncompressed checkpoint file
# define MAGIC_LEN 12               // length of magic number (including \0)
#else
# define MAGIC "MTCP-V1.0"        // magic number at beginning of checkpoint file (uncompressed)
# define MAGIC_LEN 10             // length of magic number (including \0)
#endif
#define MAGIC_FIRST 'M'
#define GZIP_FIRST 037

int STOPSIGNAL;     // signal to use to signal other threads to stop for checkpointing
#define STACKSIZE 1024      // size of temporary stack (in quadwords)

typedef struct Area Area;
typedef struct Jmpbuf Jmpbuf;
typedef struct Stat Stat;

struct Area { void *addr;   // args required for mmap to restore memory area
              size_t size;
              int prot;
              int flags;
              off_t offset;
              char name[FILENAMESIZE];
            };

// struct Jmpbuf { uLong ebx, esi, edi, ebp, esp;   // order must match that in mtcp_jmpbuf.s
//                 uLong eip;
//                 uByte fpusave[232];
//               };

struct Stat { uLong st_mode;
              uLong st_dev;
              uLong st_ino;
            };

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

/* cmpxchgl is only supported on Intel 486 processors and later. */
static inline int atomic_setif_int (int volatile *loc, int newval, int oldval)
{
  char rc;

  asm volatile ("lock ; cmpxchgl %2,%1 ; sete %%al" : "=a" (rc) : "m" (*loc), "r" (newval), "a" (oldval) : "cc", "memory");
  return (rc);
}

static inline int atomic_setif_ptr (void *volatile *loc, void *newval, void *oldval)
{
  char rc;

  asm volatile ("lock ; cmpxchg %2,%1 ; sete %%al" : "=a" (rc) : "m" (*loc), "r" (newval), "a" (oldval) : "cc", "memory");
  return (rc);
}

// gcc-3.4 issues a warning that noreturn function returns, if declared noreturn
// static void inline mtcp_abort (void) __attribute__ ((noreturn));
static void inline mtcp_abort (void)
{
  asm volatile (CLEAN_FOR_64_BIT(hlt ; xor %eax,%eax ; mov (%eax),%eax) );
}

extern char mtcp_shareable_begin[];
extern char mtcp_shareable_end[];

#ifndef MAXPATHLEN
# define MAXPATHLEN 512
#endif
// mtcp_restore_XXX are globals for arguments to mtcp_restore_start();
extern __attribute__ ((visibility ("hidden"))) int mtcp_restore_cpfd;
extern __attribute__ ((visibility ("hidden"))) int mtcp_restore_verify;
extern __attribute__ ((visibility ("hidden"))) pid_t mtcp_restore_gzip_child_pid;
extern __attribute__ ((visibility ("hidden"))) char mtcp_ckpt_newname[];
extern __attribute__ ((visibility ("hidden"))) char *mtcp_restore_cmd_file;
extern __attribute__ ((visibility ("hidden"))) char *mtcp_restore_argv[];
extern __attribute__ ((visibility ("hidden"))) char *mtcp_restore_envp[];

extern __attribute__ ((visibility ("hidden")))
  void *mtcp_saved_break;
extern void *mtcp_libc_dl_handle;
extern void *mtcp_old_dl_sysinfo_0;

void *mtcp_get_libc_symbol (char const *name);

typedef struct MtcpState MtcpState;
struct MtcpState { int volatile value;
                   pthread_cond_t cond;
                   pthread_mutex_t mutex;};
                   
#define MTCP_STATE_INITIALIZER {0, PTHREAD_COND_INITIALIZER, PTHREAD_MUTEX_INITIALIZER }

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


void mtcp_check_vdso_enabled(void);
int mtcp_have_thread_sysinfo_offset();
void *mtcp_get_thread_sysinfo(void);
void mtcp_set_thread_sysinfo(void *);
void mtcp_dump_tls (char const *file, int line);
char *mtcp_executable_path(char *filename);
char mtcp_readchar (int fd);
char mtcp_readdec (int fd, VA *value);
char mtcp_readhex (int fd, VA *value);
void mtcp_restore_start (int fd, int verify, pid_t gzip_child_pid,char *ckpt_newname,
			 char *cmd_file, char *argv[], char *envp[]);
__attribute__ ((visibility ("hidden")))
   void mtcp_restoreverything (void);
__attribute__ ((visibility ("hidden")))
   void mtcp_printf (char const *format, ...);
void mtcp_maybebpt (void);
__attribute__ ((visibility ("hidden"))) void * mtcp_safemmap (void *start, size_t length, int prot, int flags, int fd, off_t offset);
int mtcp_safestat (char const *name, Stat *statbuf);
int mtcp_safelstat (char const *name, Stat *statbuf);
int mtcp_setjmp (Jmpbuf *jmpbuf);
void mtcp_longjmp (Jmpbuf *jmpbuf, int retval);
int mtcp_safe_open(char const *filename, int flags, mode_t mode);

#endif
