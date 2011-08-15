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

extern pid_t saved_pid;

#define MTCP_PRINTF(args...) \
  do { \
    mtcp_printf("[%d] %s:%d %s:\n  ", \
                saved_pid, __FILE__, __LINE__, __FUNCTION__); \
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
#endif
#ifdef __x86_64__
typedef unsigned int mtcp_segreg_t;
#endif

// MTCP_PAGE_SIZE must be page-aligned:  multiple of sysconf(_SC_PAGESIZE).
#define MTCP_PAGE_SIZE 4096
#define MTCP_PAGE_MASK (~(MTCP_PAGE_SIZE-1))
#define RMB asm volatile ("xorl %%eax,%%eax ; cpuid" \
                          : : : "eax", "ebx", "ecx", "edx", "memory")
#define WMB asm volatile ("xorl %%eax,%%eax ; cpuid" \
                          : : : "eax", "ebx", "ecx", "edx", "memory")

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

#define DELETED_FILE_SUFFIX " (deleted)"

#define STACKSIZE 1024      // size of temporary stack (in quadwords)
//#define MTCP_MAX_PATH 256   // maximum path length for mtcp_find_executable

typedef struct Area Area;
typedef struct Jmpbuf Jmpbuf;

struct Area { char *addr;   // args required for mmap to restore memory area
              size_t size;
              off_t filesize;
              int prot;
              int flags;
              off_t offset;
              char name[FILENAMESIZE];
#ifdef FAST_CKPT_RST_VIA_MMAP
              size_t mem_region_offset;
#endif
            };

#ifdef FAST_CKPT_RST_VIA_MMAP
# define MTCP_CKPT_IMAGE_VERSION 1.3
typedef struct mtcp_ckpt_image_header {
  float ckpt_image_version;

  VA start_addr;
  size_t hdr_offset_in_file;
  size_t total_size;
  size_t maps_offset;
  size_t num_memory_regions;
  size_t VmSize;

  size_t restore_size;
  VA restore_begin;
  VA restore_start_fncptr; /* will be bound to fnc, mtcp_restore_start */
  VA finish_retore_fncptr; /* will be bound to fnc, finishrestore */

  struct rlimit stack_rlimit;
} mtcp_ckpt_image_header_t;

VA fastckpt_mmap_addr();
void fastckpt_write_mem_region(int fd, Area *area);
void fastckpt_get_mem_region_info(size_t *vmsize, size_t *num_mem_regions);
void fastckpt_prepare_for_ckpt(int ckptfd, VA restore_start, VA finishrestore);
void fastckpt_save_restore_image(int fd, VA restore_begin, size_t restore_size);
void fastckpt_finish_ckpt(int ckptfd);
void fastckpt_read_header(int fd, struct rlimit *stack_rlimit, Area *area,
                          VA *restore_start);
void fastckpt_load_restore_image(int fd, Area *area);
void fastckpt_prepare_for_restore(int fd);
VA fastckpt_get_finishrestore();
void fastckpt_finish_restore();
int fastckpt_get_next_area_dscr(Area *area);
void fastckpt_restore_mem_region(int fd, const Area *area);
void fastckpt_populate_shared_file_from_ckpt_image(int ckptfd, int imagefd,
                                                   Area* area);
#endif

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

/* cmpxchgl is only supported on Intel 486 processors and later. */
static inline int atomic_setif_int(int volatile *loc, int newval, int oldval)
{
  char rc;

  asm volatile ("lock ; cmpxchgl %2,%1 ; sete %%al"
                : "=a" (rc)
                : "m" (*loc), "r" (newval), "a" (oldval)
                : "cc", "memory");
  return (rc);
}

static inline int atomic_setif_ptr(void *volatile *loc, void *newval,
                                    void *oldval)
{
  char rc;

  asm volatile ("lock ; cmpxchg %2,%1 ; sete %%al"
                : "=a" (rc)
                : "m" (*loc), "r" (newval), "a" (oldval)
                : "cc", "memory");
  return (rc);
}

// gcc-3.4 issues a warning that noreturn function returns, if declared noreturn
static inline void mtcp_abort (void) __attribute__ ((noreturn));
static inline void mtcp_abort (void)
{
  asm volatile (CLEAN_FOR_64_BIT(hlt ; xor %eax,%eax ; mov (%eax),%eax) );
  for (;;);  /* Without this, gcc emits warning:  `noreturn' fnc does return */
}

extern char mtcp_shareable_begin[];
extern char mtcp_shareable_end[];

extern __attribute__ ((visibility ("hidden")))
int mtcp_sigaction(int sig, const struct sigaction *act,
		   struct sigaction *oact);

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

typedef struct MtcpState MtcpState;
struct MtcpState { int volatile value;
                   pthread_cond_t cond;
                   pthread_mutex_t mutex;};

#define MTCP_STATE_INITIALIZER \
  {0, PTHREAD_COND_INITIALIZER, PTHREAD_MUTEX_INITIALIZER }

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


void mtcp_readcs (int fd, char cs);
void mtcp_readfile(int fd, void *buf, size_t size);
void mtcp_writecs (int fd, char cs);
void mtcp_writefile (int fd, void const *buff, size_t size);
void mtcp_check_vdso_enabled(void);
int mtcp_have_thread_sysinfo_offset();
void *mtcp_get_thread_sysinfo(void);
void mtcp_set_thread_sysinfo(void *);
void mtcp_dump_tls (char const *file, int line);
int mtcp_is_executable(const char *exec_path);
char *mtcp_find_executable(char *filename, const char* path_env,
                           char exec_path[PATH_MAX]);
char mtcp_readchar (int fd);
char mtcp_readdec (int fd, VA *value);
char mtcp_readhex (int fd, VA *value);
ssize_t mtcp_read_all(int fd, void *buf, size_t count);
ssize_t mtcp_write_all(int fd, const void *buf, size_t count);
size_t mtcp_strlen (const char *s1);
const void *mtcp_strstr(const char *string, const char *substring);
void mtcp_strncpy(char *targ, const char* source, size_t len);
void mtcp_strncat(char *dest, const char *src, size_t n);
int mtcp_memcmp(char *targ, const char* source, size_t len);
void mtcp_memset(char *targ, int c, size_t n);
void mtcp_check_vdso_enabled();
int mtcp_strncmp (const char *s1, const char *s2, size_t n);
int mtcp_strcmp (const char *s1, const char *s2);
int mtcp_strstartswith (const char *s1, const char *s2);
int mtcp_strendswith (const char *s1, const char *s2);
int mtcp_atoi(const char *nptr);
int mtcp_get_controlling_term(char* ttyName, size_t len);
const char* mtcp_getenv(const char* name);

int mtcp_readmapsline (int mapsfd, Area *area);
__attribute__ ((visibility ("hidden")))
void mtcp_restoreverything (void);
__attribute__ ((visibility ("hidden")))
void mtcp_printf (char const *format, ...);
void mtcp_maybebpt (void);
__attribute__ ((visibility ("hidden")))
void *mtcp_safemmap(void *start, size_t length, int prot, int flags, int fd,
                    off_t offset);
int mtcp_setjmp (Jmpbuf *jmpbuf);
void mtcp_longjmp (Jmpbuf *jmpbuf, int retval);
int mtcp_safe_open(char const *filename, int flags, mode_t mode);

#endif
