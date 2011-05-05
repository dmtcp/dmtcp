/*****************************************************************************
 *   Copyright (C) 2006-2010 by Michael Rieker, Jason Ansel, Kapil Arya, and *
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

/*****************************************************************************
 *
 *  Multi-threaded checkpoint library
 *
 *  Link this in as part of your program that you want checkpoints taken
 *  Call the mtcp_init routine at the beginning of your program
 *  Call the mtcp_ok routine when it's OK to do checkpointing
 *  Call the mtcp_no routine when you want checkpointing inhibited
 *
 *  This module also contains a __clone wrapper routine
 *
 *****************************************************************************/


// Set _GNU_SOURCE in order to expose glibc-defined sigandset()
#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif
#include <asm/ldt.h>      // for struct user_desc
//#include <asm/segment.h>  // for GDT_ENTRY_TLS_... stuff
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <termios.h>       // for tcdrain, tcsetattr, etc.
#include <unistd.h>
#include <ucontext.h>
#include <sys/types.h>     // for gettid, tkill, waitpid
#include <sys/wait.h>	   // for waitpid
#include <linux/unistd.h>  // for gettid, tkill
#include <gnu/libc-version.h>

#define MTCP_SYS_STRCPY
#define MTCP_SYS_STRLEN
#include "mtcp_internal.h"

/* required for ptrace sake */
#include <sys/user.h>
#include "mtcp_ptrace.h" 

// static int WAIT=1;
// static int WAIT=0;

#if 0
// Force thread to stop, without use of a system call.
static int WAIT=1;
# define DEBUG_WAIT \
if (DEBUG_RESTARTING) \
  {int i,j; \
    for (i = 0; WAIT && i < 1000000000; i++) \
      for (j = 0; j < 1000000000; j++) ; \
  }
#else
# define DEBUG_WAIT
#endif

#if defined(GDT_ENTRY_TLS_ENTRIES) && !defined(__x86_64__)
#define MTCP__SAVE_MANY_GDT_ENTRIES 1
#else
#define MTCP__SAVE_MANY_GDT_ENTRIES 0
#endif

/* Retrieve saved stack pointer saved by getcontext () */
#ifdef __x86_64__
#define MYREG_RSP 15
#define SAVEDSP uc_mcontext.gregs[MYREG_RSP]
#else
#define MYREG_ESP 7
#define SAVEDSP uc_mcontext.gregs[MYREG_ESP]
#endif

/* TLS segment registers used differently in i386 and x86_64. - Gene */
#ifdef __i386__
# define TLSSEGREG gs
#endif
#ifdef __x86_64__
# define TLSSEGREG fs
#endif

/* Offset computed (&x.pid - &x) for
 *   struct pthread x;
 * as found in:  glibc-2.5/nptl/descr.h
 * It was 0x4c and 0x48 for pid and tid for i386.
 * Roughly, the definition is:
 *glibc-2.5/nptl/descr.h:
 * struct pthread
 * {
 *  union {
 *   tcbheader_t tcbheader;
 *   void *__padding[16];
 *  };
 *  list_t list;
 *  pid_t tid;
 *  pid_t pid;
 *  ...
 * } __attribute ((aligned (TCB_ALIGNMENT)));
 *
 *glibc-2.5/nptl/sysdeps/pthread/list.h:
 * typedef struct list_head
 * {
 *  struct list_head *next;
 *  struct list_head *prev;
 * } list_t;
 *
 * NOTE: glibc-2.10 changes the size of __padding from 16 to 24.  --KAPIL
 *
 * NOTE: glibc-2.11 further changes the size tcphead_t without updating the
 *       size of __padding in struct pthread. We need to add an extra 512 bytes
 *       to accommodate this.                                    -- KAPIL
 */

#if !__GLIBC_PREREQ (2,1)
# error "glibc version too old"
#endif

// Calculate offsets of pid/tid in pthread 'struct user_desc'
int STATIC_TLS_TID_OFFSET()
{
  static int offset = -1;
  if (offset != -1) 
    return offset;

  char *ptr;
  long major = strtol(gnu_get_libc_version(), &ptr, 10);
  long minor = strtol(ptr+1, NULL, 10);
  if (major != 2) {
    MTCP_PRINTF("**** Error:: Incompatible glibc version: %s\n",
                gnu_get_libc_version());
    mtcp_abort();
  }

  if (minor >= 11) {
#ifdef __x86_64__
    offset = 26*sizeof(void *) + 512;
#else
    offset = 26*sizeof(void *);
#endif
  } else if (minor == 10) {
    offset = 26*sizeof(void *);
  } else {
    offset = 18*sizeof(void *);
  }

  return offset;
}

#if 0
# if __GLIBC_PREREQ (2,11)
#  ifdef __x86_64__
#   define STATIC_TLS_TID_OFFSET() (26*sizeof(void *) + 512)
#  else
#   define STATIC_TLS_TID_OFFSET() (26*sizeof(void *))
#  endif

# elif __GLIBC_PREREQ (2,10)
#   define STATIC_TLS_TID_OFFSET() (26*sizeof(void *))

# else
#   define STATIC_TLS_TID_OFFSET() (18*sizeof(void *))
# endif
#endif

# define STATIC_TLS_PID_OFFSET() (STATIC_TLS_TID_OFFSET() + sizeof(pid_t))

#if 1
/* WHEN WE HAVE CONFIDENCE IN THIS VERSION, REMOVE ALL OTHER __GLIBC_PREREQ
 * AND MAKE THIS THE ONLY VERSION.  IT SHOULD BE BACKWARDS COMPATIBLE.
 */
/* These function definitions should succeed independently of the glibc version.
 * They use get_thread_area() to match (tid, pid) and find offset.
 * In other code, on restart, that offset is used to set (tid,pid) to
 *   the latest tid and pid of the new thread, instead of the (tid,pid)
 *   of the original thread.
 * SEE: "struct pthread" in glibc-2.XX/nptl/descr.h for 'struct pthread'.
 */
static int TLS_TID_OFFSET(void);

/* Can remove the unused attribute when this __GLIBC_PREREQ is the only one. */
static char *memsubarray (char *array, char *subarray, int len)
					 __attribute__ ((unused));
static int mtcp_get_tls_segreg(void);
static void *mtcp_get_tls_base_addr(void);

static int TLS_TID_OFFSET(void) {
  static int tid_offset = -1;
  if (tid_offset == -1) {
    struct {pid_t tid; pid_t pid;} tid_pid;
    /* struct pthread has adjacent fields, tid and pid, in that order.
     * Try to find at what offset that bit patttern occurs in struct pthread.
     */
    char * tmp;
    tid_pid.tid = mtcp_sys_kernel_gettid();
    tid_pid.pid = mtcp_sys_getpid();
    /* Get entry number of current thread descriptor from its segment register:
     * Segment register / 8 is the entry_number for the "thread area", which
     * is of type 'struct user_desc'.   The base_addr field of that struct
     * points to the struct pthread for the thread with that entry_number.
     * The tid and pid are contained in the 'struct pthread'.
     *   So, to access the tid/pid fields, first find the entry number.
     * Then fill in the entry_number field of an empty 'struct user_desc', and
     * get_thread_area(struct user_desc *uinfo) will fill in the rest.
     * Then use the filled in base_address field to get the 'struct pthread'.
     * The function mtcp_get_tls_base_addr() returns this 'struct pthread' addr.
     */
    void * pthread_desc = mtcp_get_tls_base_addr();
    /* A false hit for tid_offset probably can't happen since a new
     * 'struct pthread' is zeroed out before adding tid and pid.
     */
    tmp = memsubarray((char *)pthread_desc, (char *)&tid_pid, sizeof(tid_pid));
    if (tmp == NULL) {
      MTCP_PRINTF("Couldn't find offsets of tid/pid in thread_area.\n"
                  "  Now relying on the value determined using the\n"
                  "  glibc version with which DMTCP was compiled.\n");
      return STATIC_TLS_TID_OFFSET();
      //mtcp_abort();
    }

    tid_offset = tmp - (char *)pthread_desc;
    if (tid_offset != STATIC_TLS_TID_OFFSET()) {
      MTCP_PRINTF("Warning:  tid_offset = %d; different from expected.\n"
                  "  It is possible that DMTCP was compiled with a different\n"
                  "  glibc version than the one it's dynamically linking to.\n"
                  "  Continuing anyway.  If this fails, please try again.\n",
                  tid_offset);
    }
    DPRINTF("tid_offset: %d\n", tid_offset);
    if (tid_offset % sizeof(int) != 0) {
      MTCP_PRINTF("tid_offset is not divisible by sizeof(int).\n"
                  "  Now relying on the value determined using the\n"
                  "  glibc version with which DMTCP was compiled.\n");
      return STATIC_TLS_TID_OFFSET();
      //mtcp_abort();
    }
    /* Should we do a double-check, and spawn a new thread and see
     *  if its TID matches at this tid_offset?  This would give greater
     *  confidence, but for the reasons above, it's probably not necessary.
     */
  }
  return tid_offset;
}

static int TLS_PID_OFFSET(void) {
  static int pid_offset = -1;
  struct {pid_t tid; pid_t pid;} tid_pid;
  if (pid_offset == -1) {
    int tid_offset = TLS_TID_OFFSET();
    pid_offset = tid_offset + (char *)&(tid_pid.pid) - (char *)&tid_pid;
    DPRINTF("pid_offset: %d\n", pid_offset);
  }
  return pid_offset;
}
#endif

/* this call to gettid is hijacked by DMTCP for PID/TID-Virtualization */
#define GETTID() (int)syscall(SYS_gettid)

sem_t sem_start;

typedef struct Thread Thread;

struct Thread { Thread *next;         // next thread in 'threads' list
                Thread **prev;        // prev thread in 'threads' list
                int tid;              // this thread's id as returned by
                                      //   mtcp_sys_kernel_gettid ()
                int original_tid;     // this is the thread's "original" tid
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
                ucontext_t savctx;     // context saved on suspend

                mtcp_segreg_t fs, gs;  // thread local storage pointers
                pthread_t pth;         // added for pthread_join
#if MTCP__SAVE_MANY_GDT_ENTRIES
                struct user_desc gdtentrytls[GDT_ENTRY_TLS_ENTRIES];
#else
                struct user_desc gdtentrytls[1];
#endif
              };

/*
 * struct MtcpRestartThreadArg
 *
 * DMTCP requires the original_tids  of the threads being created during
 *  the RESTARTING phase. We use MtcpRestartThreadArg structure is to pass
 *  the original_tid of the thread being created from MTCP to DMTCP.
 *
 * actual clone call: clone (fn, child_stack, flags, void *, ... )
 * new clone call   : clone (fn, child_stack, flags,
 *                           (struct MtcpRestartThreadArg *), ...)
 *
 * DMTCP automatically extracts arg from this structure and passes that
 * to the _real_clone call.
 *
 * IMPORTANT NOTE: While updating, this structure must be kept in sync
 * with the structure defined with the same name in mtcpinterface.cpp
 */
struct MtcpRestartThreadArg {
  void *arg;
  pid_t original_tid;
};

// thread status
#define ST_RUNDISABLED 0     // running normally but with checkpointing disabled
#define ST_RUNENABLED 1      // running normally and has checkpointing enabled
#define ST_SIGDISABLED 2     // running normally with cp disabled, but
                             //   checkpoint thread is waiting for it to enable
#define ST_SIGENABLED 3      // running normally with cp enabled, and checkpoint
                             //   thread has signalled it to stop
#define ST_SUSPINPROG 4      // thread context being saved (very brief)
#define ST_SUSPENDED 5       // suspended waiting for checkpoint to complete
#define ST_CKPNTHREAD 6      // checkpointing thread (special state just for
                             //   that thread)

	/* Global data */

void *mtcp_libc_dl_handle = NULL;  // dlopen handle for whatever libc.so is
                                   //   loaded with application program
Area mtcp_libc_area;               // some area of that libc.so

/* DMTCP Info Variables */

/* These are reset by dmtcphijack.so at initialization. */
int dmtcp_exists = 0; /* Are we running under DMTCP? */
int dmtcp_info_pid_virtualization_enabled = 0;
/* The following two DMTCP Info variables are defined in mtcp_printf.c */
//int dmtcp_info_stderr_fd = 2;
//int dmtcp_info_jassertlog_fd = -1;
int dmtcp_info_restore_working_directory = -1;

char* mtcp_restore_argv_start_addr = NULL;
    
	/* Static data */

static sigset_t sigpending_global;  // pending signals for the process
static char const *nscd_mmap_str = "/var/run/nscd/";    // OpenSUSE
static char const *nscd_mmap_str2 = "/var/cache/nscd";  // Debian / Ubuntu
static char const *nscd_mmap_str3 = "/var/db/nscd";     // RedHat (Linux 2.6.9)
static char const *dev_zero_deleted_str = "/dev/zero (deleted)";
static char const *dev_null_deleted_str = "/dev/null (deleted)";
static char const *sys_v_shmem_file = "/SYSV";
//static char const *perm_checkpointfilename = NULL;
//static char const *temp_checkpointfilename = NULL;
static char perm_checkpointfilename[MAXPATHLEN];
static char temp_checkpointfilename[MAXPATHLEN];
static size_t checkpointsize;
static int intervalsecs;
static pid_t motherpid = 0;
pid_t saved_pid = 0;
static int showtiming;
static int threadenabledefault;
static int verify_count;  // number of checkpoints to go
static int verify_total;  // value given by envar
static pid_t mtcp_ckpt_extcomp_child_pid = -1;
static int volatile checkpointhreadstarting = 0;
static MtcpState restoreinprog = MTCP_STATE_INITIALIZER;
static MtcpState threadslocked = MTCP_STATE_INITIALIZER;
static pthread_t checkpointhreadid;
static struct timeval restorestarted;
static int DEBUG_RESTARTING = 0;
static Thread *motherofall = NULL;
static Thread *ckpthread = NULL;
static Thread *threads = NULL;
/* NOTE:  NSIG == SIGRTMAX+1 == 65 on Linux; NSIG is const, SIGRTMAX isn't */
struct sigaction sigactions[NSIG];  /* signal handlers */
static size_t restore_size;
static VA restore_begin, restore_end;
static void *restore_start; /* will be bound to fnc, mtcp_restore_start */
static void *saved_sysinfo;
static VA saved_heap_start = NULL;
static void (*callback_sleep_between_ckpt)(int sec) = NULL;
static void (*callback_pre_ckpt)() = NULL;
static void (*callback_post_ckpt)(int is_restarting, char* argv_start) = NULL;
static int  (*callback_ckpt_fd)(int fd) = NULL;
static void (*callback_write_dmtcp_header)(int fd) = NULL;
static void (*callback_restore_virtual_pid_table)() = NULL;
#ifdef PTRACE
__attribute__ ((visibility ("hidden"))) struct ptrace_info
  (*callback_get_next_ptrace_info)(int index) = NULL;
__attribute__ ((visibility ("hidden"))) void
  (*callback_ptrace_info_list_command)(struct cmd_info cmd) = NULL;
__attribute__ ((visibility ("hidden"))) void
  (*callback_jalib_ckpt_unlock)() = NULL;
__attribute__ ((visibility ("hidden"))) int 
  (*callback_ptrace_info_list_size)() = NULL;
int motherofall_done_reading = 0;
pthread_mutex_t motherofall_done_reading_lock = PTHREAD_MUTEX_INITIALIZER;
int has_new_ptrace_shared_file = 0;
int jalib_ckpt_unlock_ready = 0;
pthread_mutex_t jalib_ckpt_unlock_lock = PTHREAD_MUTEX_INITIALIZER;
int proceed_to_checkpoint = 0;
pthread_mutex_t proceed_to_checkpoint_lock = PTHREAD_MUTEX_INITIALIZER;
int nthreads = 0;
pthread_mutex_t nthreads_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

static int (*clone_entry) (int (*fn) (void *arg),
                           void *child_stack,
                           int flags,
                           void *arg,
                           int *parent_tidptr,
                           struct user_desc *newtls,
                           int *child_tidptr);

/* temp stack used internally by restore so we don't go outside the
 *   libmtcp.so address range for anything;
 * including "+ 1" since will set %esp/%rsp to tempstack+STACKSIZE
 */
static long long tempstack[STACKSIZE + 1];

	/* Internal routines */

static long set_tid_address (int *tidptr);

static char *memsubarray (char *array, char *subarray, int len)
					 __attribute__ ((unused));
static int mtcp_get_tls_segreg(void);
static void *mtcp_get_tls_base_addr(void);
static int threadcloned (void *threadv);
static void setupthread (Thread *thread);
static void setup_clone_entry (void);
static void threadisdead (Thread *thread);
static void *checkpointhread (void *dummy);
static int test_use_compression(char *compressor, char *command, char *path,
                                int def);
static int open_ckpt_to_write(int fd, int pipe_fds[2], char **args);
static void checkpointeverything (void);
static void writefiledescrs (int fd);
static void writememoryarea (int fd, Area *area,
			     int stack_was_seen, int vsyscall_exists);
static void writecs (int fd, char cs);
//static void writefile (int fd, void const *buff, size_t size);
static void preprocess_special_segments(int *vsyscall_exists);
static void stopthisthread (int signum);
static void wait_for_all_restored (void);
static void save_sig_state (Thread *thisthread);
static void restore_sig_state (Thread *thisthread);
static void save_sig_handlers (void);
static void restore_sig_handlers (Thread *thisthread);
static void save_tls_state (Thread *thisthread);
static void renametempoverperm (void);
static Thread *getcurrenthread (void);
static void lock_threads (void);
static void unlk_threads (void);
//static int readmapsline (int mapsfd, Area *area);
static void restore_heap(void);
static void finishrestore (void);
static int restarthread (void *threadv);
static void restore_tls_state (Thread *thisthread);
static void setup_sig_handler (void);
static void sync_shared_mem(void);

#define FORKED_CKPT_FAILED 0
#define FORKED_CKPT_MASTER 1
#define FORKED_CKPT_WORKER 2

#ifdef HBICT_DELTACOMP
static int open_ckpt_to_write_hbict(int fd, int pipe_fds[2], char *hbict_path,
                                    char *gzip_path);
#endif
static int open_ckpt_to_write_gz(int fd, int pipe_fds[2], char *gzip_path);

int perform_callback_write_dmtcp_header();
int test_and_prepare_for_forked_ckpt(int tmpDMTCPHeaderFd);
int test_and_prepare_for_compression(int *fd);
void write_ckpt_to_file(int fd, int tmpDMTCPHeaderFd);;

/* FIXME:
 * dmtcp/src/syscallsreal.c has wrappers around signal, sigaction, sigprocmask
 * The wrappers go to these mtcp_real_XXX versions so that MTCP can call
 * the actual system calls and avoid the wrappers.
 *
 * Update: 
 * mtcp_sigaction below is implemented as a direct kernel call avoiding glibc.
 *   This allows us to manipulate SIGSETXID and SIGCANCEL/SIGTIMER
 * sigprocmask should not be used in multi-threaded process, use
 *   pthread_sigmask instead.
 */


/* These hook functions provide an alternative to DMTCP callbacks, using
 * weak symbols.  While MTCP is immature, let's allow both, in case
 * the flexibility of a second hook mechanism is useful in the future.
 * The mechanism is invisible unless end user compiles w/ -Wl,-export-dynamic
 */
__attribute__ ((weak)) void mtcpHookPreCheckpoint( void ) { }

__attribute__ ((weak)) void mtcpHookPostCheckpoint( void ) { }

__attribute__ ((weak)) void mtcpHookRestart( void ) { }

/* Statically allocate this.  Malloc is dangerous here if application is
 *   defining its own (possibly not thread-safe) malloc routine.
 */
static Thread ckptThreadStorage;

/*****************************************************************************
 *
 *  This routine must be called at startup time to initiate checkpointing
 *
 *    Input:
 *
 *	checkpointfilename = name to give the checkpoint file
 *	interval = interval, in seconds, to write the checkpoint file
 *	clonenabledefault = 0 : clone checkpointing blocked by default (call
 *	                          mtcp_ok in the thread to enable)
 *	                    1 : clone checkpointing enabled by default (call
 *	                          mtcp_no in the thread to block if you want)
 *
 *	envar MTCP_WRAPPER_LIBC_SO = what library to use for inner wrappers
 *	                             (default libc.??.so)
 *	envar MTCP_VERIFY_CHECKPOINT = every n checkpoints, verify by doing a
 *	                                 restore to resume
 *	                               default is 0, ie, don't ever verify
 *
 *****************************************************************************/
void mtcp_init (char const *checkpointfilename,
                int interval,
                int clonenabledefault)
{
  char *p, *tmp, *endp;
  int len;
  Thread *ckptThreadDescriptor = & ckptThreadStorage;

  saved_pid = mtcp_sys_getpid ();

  //mtcp_segreg_t TLSSEGREG;
#ifdef PTRACE 
  DPRINTF("begin init_thread_local\n");
  init_thread_local();
#endif
  /* Initialize the static curbrk variable in sbrk wrapper. */
  sbrk(0);

  if (sizeof(void *) != sizeof(long)) {
    MTCP_PRINTF("ERROR: sizeof(void *) != sizeof(long) on this architecture.\n"
                "       This code assumes they are equal.\n");
    mtcp_abort ();
  }

#ifndef __x86_64__
  /* Nobody else has a right to preload on internal processes generated
   * by mtcp_check_XXX() -- not even DMTCP, if it's currently operating.
   *
   * Saving LD_PRELOAD in a temp env var and restoring it later --Kapil.
   *
   * TODO: To insert some sort of error checking to make sure that we
   *       are correctly setting LD_PRELOAD after we are done with
   *       vdso check.
   */

  // Shouldn't this removal of LD_PRELOAD be around fork/exec of gzip ?
  // setenv( "MTCP_TMP_LD_PRELOAD", getenv("LD_PRELOAD"), 1);
  // unsetenv("LD_PRELOAD");
  // Allow user program to run with randomize_va
  // mtcp_check_vdso_enabled();
  // setenv("LD_PRELOAD", getenv("MTCP_TMP_LD_PRELOAD"), 1);
  // unsetenv("MTCP_TMP_LD_PRELOAD");
#endif

#if 0
  { struct user_desc u_info;
    u_info.entry_number = 12;
    if (-1 == mtcp_sys_get_thread_area(&u_info) && mtcp_sys_errno == ENOSYS)
      mtcp_printf(
        "Apparently, get_thread_area is not implemented in your kernel.\n"
        "  If this doesn't work, please try on a more recent kernel,\n"
        "  or one configured to support get_thread_area.\n"
      );
  }
#endif

  intervalsecs = interval;

  if (strlen(mtcp_ckpt_newname) >= MAXPATHLEN) {
    MTCP_PRINTF("new ckpt file name (%s) too long (>=512 bytes)\n",
                mtcp_ckpt_newname);
    mtcp_abort();
  }
  // this is what user wants the checkpoint file called
  strncpy(perm_checkpointfilename,checkpointfilename,MAXPATHLEN);
  // make up another name, same as that, with ".temp" on the end ... we use it
  // to write to in case we crash while writing, we will leave the previous good
  // one intact
  len = strlen (perm_checkpointfilename);
  memcpy(temp_checkpointfilename, perm_checkpointfilename, len);
  strncpy(temp_checkpointfilename + len, ".temp",MAXPATHLEN-len);

#ifdef PTRACE
  memset(ptrace_shared_file, '\0', MAXPATHLEN);
  sprintf(ptrace_shared_file, "%s/ptrace_shared.txt", dmtcp_tmp_dir);
  memset(ptrace_setoptions_file, '\0', MAXPATHLEN);
  sprintf(ptrace_setoptions_file, "%s/ptrace_setoptions.txt", dmtcp_tmp_dir);
  memset(checkpoint_threads_file, '\0', MAXPATHLEN);
  sprintf(checkpoint_threads_file, "%s/ptrace_ckpthreads.txt", dmtcp_tmp_dir);
  memset(new_ptrace_shared_file, '\0', MAXPATHLEN);
  sprintf(new_ptrace_shared_file, "%s/new_ptrace_shared.txt", dmtcp_tmp_dir);
  memset(ckpt_leader_file, '\0', MAXPATHLEN);
  sprintf(ckpt_leader_file, "%s/ckpt_leader_file.txt", dmtcp_tmp_dir);
#endif

  DPRINTF("main tid %d\n", mtcp_sys_kernel_gettid ());
  /* If MTCP_INIT_PAUSE set, sleep 15 seconds and allow for gdb attach. */
  if (getenv("MTCP_INIT_PAUSE")) {
    MTCP_PRINTF("Pausing 15 seconds. Do:  gdb attach %d\n", mtcp_sys_getpid());
    sleep(15);
  }

  // save this away where it's easy to get
  threadenabledefault = clonenabledefault;

  p = getenv ("MTCP_SHOWTIMING");
  showtiming = ((p != NULL) && (*p & 1));

  /* Maybe dump out some stuff about the TLS */

  mtcp_dump_tls (__FILE__, __LINE__);

  /* Save this process's pid.  Then verify that the TLS has it where it should
   * be. When we do a restore, we will have to modify each thread's TLS with the
   * new motherpid. We also assume that GS uses the first GDT entry for its
   * descriptor.
   */

  motherpid = mtcp_sys_getpid (); /* libc/getpid can lie if we had
				   * used kernel fork() instead of libc fork().
				   */
  {
    pid_t tls_pid, tls_tid;
    tls_pid = *(pid_t *) (mtcp_get_tls_base_addr() + TLS_PID_OFFSET());
    tls_tid = *(pid_t *) (mtcp_get_tls_base_addr() + TLS_TID_OFFSET());

    if ((tls_pid != motherpid) || (tls_tid != motherpid)) {
      MTCP_PRINTF("getpid %d, tls pid %d, tls tid %d, must all match\n",
                  motherpid, tls_pid, tls_tid);
      mtcp_abort ();
    }
  }

  /* Get verify envar */

  tmp = getenv ("MTCP_VERIFY_CHECKPOINT");
  verify_total = 0;
  if (tmp != NULL) {
    verify_total = strtol (tmp, &p, 0);
    if ((*p != '\0') || (verify_total < 0)) {
      MTCP_PRINTF ("bad MTCP_VERIFY_CHECKPOINT %s\n", tmp);
      mtcp_abort ();
    }
  }

  /* If the user has defined a signal, use that to suspend.  Otherwise, use
   * MTCP_DEFAULT_SIGNAL */

  tmp = getenv("MTCP_SIGCKPT");
  if (tmp == NULL) {
      STOPSIGNAL = MTCP_DEFAULT_SIGNAL;
  } else {
    errno = 0;
    STOPSIGNAL = strtol(tmp, &endp, 0);

    if ((errno != 0) || (tmp == endp)) {
      MTCP_PRINTF("Your chosen SIGCKPT of \"%s\" does not "
                  "translate to a number,\n"
                  "  and cannot be used.  Signal %d "
                  "will be used instead.\n", tmp, MTCP_DEFAULT_SIGNAL);
      STOPSIGNAL = MTCP_DEFAULT_SIGNAL;
    } else if (STOPSIGNAL < 1 || STOPSIGNAL > 31) {
      MTCP_PRINTF("Your chosen SIGCKPT of \"%d\" is not a valid "
                  "signal, and cannot be used.\n"
                  "  Signal %d will be used instead.\n",
                  STOPSIGNAL, MTCP_DEFAULT_SIGNAL);
      STOPSIGNAL = MTCP_DEFAULT_SIGNAL;
    }
  }

  /* Set up signal handler so we can interrupt the thread for checkpointing */
  setup_sig_handler ();

  /* Get size and address of the shareable - used to separate it from the rest
   * of the stuff. All routines needed to perform restore must be within this
   * address range
   */

  restore_begin = (VA)((unsigned long int)mtcp_shareable_begin
		       & -MTCP_PAGE_SIZE);
  restore_size  = ((mtcp_shareable_end - restore_begin) + MTCP_PAGE_SIZE - 1)
		   & -MTCP_PAGE_SIZE;
  restore_end   = restore_begin + restore_size;
  restore_start = mtcp_restore_start;

  /* Setup clone_entry to point to glibc's __clone routine */

  setup_clone_entry ();

  /* Set up caller as one of our threads so we can work on it */

  memset (ckptThreadDescriptor, 0, sizeof *ckptThreadDescriptor);
  setupthread (ckptThreadDescriptor);

  /* need to set this up so the checkpointhread can see we haven't exited */
  ckptThreadDescriptor -> child_tid = mtcp_sys_kernel_gettid ();

  /* we are assuming mtcp_init has been called before application may have
   * called set_tid_address ... or else we will end up overwriting that
   * set_tid_address value
   */
  set_tid_address (&(ckptThreadDescriptor -> child_tid));

  motherofall = ckptThreadDescriptor;

  /* Spawn off a thread that will perform the checkpoints from time to time */

  checkpointhreadstarting = 1;
  /* If we return from a fork(), we don't know what is the semaphore value. */
  errno = 0;
  while (sem_trywait(&sem_start) == -1 && (errno == EAGAIN || errno == EINTR)) {
    if ( errno == EAGAIN )
      sem_post(&sem_start);
    errno = 0;
  }
  if (errno != 0)
    perror("ERROR: continue anyway from " __FILE__ ":mtcp_init:sem_trywait()");
  /* Now we successfully locked it.  The sempaphore value is zero. */
  if (pthread_create (&checkpointhreadid, NULL, checkpointhread, NULL) < 0) {
    MTCP_PRINTF ("error creating checkpoint thread: %s\n", strerror(errno));
    mtcp_abort ();
  }

  /* make sure the clone wrapper executed (ie, not just the standard clone) */
  if (checkpointhreadstarting) mtcp_abort ();

  /* Stop until checkpoint thread has finished initializing.
   * Some programs (like gcl) implement their own glibc functions in
   * a non-thread-safe manner.  In case we're using non-thread-safe glibc,
   * don't run the checkpoint thread and user thread at the same time.
   */
  errno = 0;
  while (-1 == sem_wait(&sem_start) && errno == EINTR)
    errno = 0;
  /* The child thread checkpointhread will now wake us. */
}

/*****************************************************************************
 *
 *  The routine mtcp_set_callbacks below may be called BEFORE the first
 *  MTCP checkpoint, to add special functionality to checkpointing
 *
 *    Its arguments (callback functions) are:
 *
 * sleep_between_ckpt:  Called in between checkpoints to replace the default
 *                       "sleep(sec)" functionality, when this function returns
 *                       checkpoint will start
 * pre_ckpt:            Called after all user threads are suspended, but BEFORE
 *                       checkpoint written
 * post_ckpt:           Called after checkpoint, and after restore.
 *                       is_restarting will be 1 for restore 0 for after
 *                       checkpoint
 * ckpt_fd:             Called to test if mtcp should checkpoint a given FD
 *                       returns 1 if it should
 *
 *****************************************************************************/

void mtcp_set_callbacks(void (*sleep_between_ckpt)(int sec),
                        void (*pre_ckpt)(),
                        void (*post_ckpt)(int is_restarting,
                                          char* mtcp_restore_argv_start_addr),
                        int  (*ckpt_fd)(int fd),
                        void (*write_dmtcp_header)(int fd),
                        void (*restore_virtual_pid_table)()
#ifdef PTRACE
                      , struct ptrace_info (*get_next_ptrace_info)(),
                        void (*ptrace_info_list_command)(struct cmd_info cmd),
                        void (*jalib_ckpt_unlock)(),
                        int (*ptrace_info_list_size)()
#endif 
                       )
{
    callback_sleep_between_ckpt = sleep_between_ckpt;
    callback_pre_ckpt = pre_ckpt;
    callback_post_ckpt = post_ckpt;
    callback_ckpt_fd = ckpt_fd;
    callback_write_dmtcp_header = write_dmtcp_header;
    callback_restore_virtual_pid_table = restore_virtual_pid_table;
#ifdef PTRACE
    callback_get_next_ptrace_info = get_next_ptrace_info;
    callback_ptrace_info_list_command = ptrace_info_list_command;
    callback_jalib_ckpt_unlock = jalib_ckpt_unlock;
    callback_ptrace_info_list_size = ptrace_info_list_size;
#endif
}

/*************************************************************************
 *
 *  Dump out the TLS stuff pointed to by %gs
 *
 *************************************************************************/

void mtcp_dump_tls (char const *file, int line)
{
#if 000
  int i, j, mypid;
  sigset_t blockall, oldsigmask;
  struct user_desc gdtentry;
  unsigned char byt;
  unsigned short gs;

  static int mutex = 0;

  /* Block all signals whilst we have the futex */

  memset (&blockall, -1, sizeof blockall);
  if (sigprocmask (SIG_SETMASK, &blockall, &oldsigmask) < 0) {
    abort ();
  }

  /* Block other threads from doing this so the output doesn't mix */

  while (!atomic_setif_int (&mutex, 1, 0)) {
    mtcp_sys_futex (&mutex, FUTEX_WAIT, 1, NULL, NULL, 0);
  }

  /* Get the segment for the TLS stuff */

  asm volatile ("movw %%gs,%0" : "=g" (gs));
  MTCP_PRINTF("gs=%X at %s:%d\n", gs, file, line);
  if (gs != 0) {

    /* We only handle GDT based stuff */

    if (gs & 4) MTCP_PRINTF("   *** part of LDT\n");

    /* It's in the GDT */

    else {

      /* Read the TLS descriptor */

      gdtentry.entry_number = gs / 8;
      i = mtcp_sys_get_thread_area (&gdtentry);
      if (i < 0) {
        MTCP_PRINTF("  error getting GDT entry %d: %d\n",
                    gdtentry.entry_number, mtcp_sys_errno);
      } else {

        /* Print out descriptor and first 80 bytes of data */

        mtcp_printf("  limit %X, baseaddr %X\n",
                    gdtentry.limit, gdtentry.base_addr);
        for (i = 0; i < 80; i += 16) {
          for (j = 16; -- j >= 0;) {
            if ((j & 3) == 3) fputc (' ', stderr);
            asm volatile ("movb %%gs:(%1),%0" : "=r" (byt) : "r" (i + j));
            mtcp_printf("%2.2X", byt);
          }
          mtcp_printf(" : gs+%2.2X\n", i);
        }
        for (i = 0; i < 80; i += 16) {
          for (j = 16; -- j >= 0;) {
            if ((j & 3) == 3) fputc (' ', stderr);
            byt = ((unsigned char *)gdtentry.base_addr)[i+j];
            mtcp_printf("%2.2X", byt);
          }
          mtcp_printf(" : %8.8X\n", gdtentry.base_addr + i);
        }

        /* Offset 4C should be the process id */

        asm volatile ("mov %%gs:0x4C,%0" : "=r" (i));
        MTCP_PRINTF("mtcp_init: getpid=%d, gettid=%d, tls=%d\n",
                    getpid (), mtcp_sys_kernel_gettid (), i);
      }
    }
  }

  /* Release mutex and restore signal delivery */

  mutex = 0;
  mtcp_sys_futex (&mutex, FUTEX_WAKE, 1, NULL, NULL, 0);
  if (_real_sigprocmask (SIG_SETMASK, &oldsigmask, NULL) < 0) {
    abort ();
  }
#endif
}

/*****************************************************************************
 *
 *  This is our clone system call wrapper
 *
 *    Note:
 *
 *      pthread_create eventually calls __clone to create threads
 *      It uses flags = 0x3D0F00:
 *	      CLONE_VM = VM shared between processes
 *	      CLONE_FS = fs info shared between processes (root, cwd, umask)
 *	   CLONE_FILES = open files shared between processes (fd table)
 *	 CLONE_SIGHAND = signal handlers and blocked signals shared
 *	 			 (sigaction common to parent and child)
 *	  CLONE_THREAD = add to same thread group
 *	 CLONE_SYSVSEM = share system V SEM_UNDO semantics
 *	  CLONE_SETTLS = create a new TLS for the child from newtls parameter
 *	 CLONE_PARENT_SETTID = set the TID in the parent (before MM copy)
 *	CLONE_CHILD_CLEARTID = clear the TID in the child and do
 *				 futex wake at that address
 *	      CLONE_DETACHED = create clone detached
 *
 *****************************************************************************/

int __clone (int (*fn) (void *arg), void *child_stack, int flags, void *arg,
	     int *parent_tidptr, struct user_desc *newtls, int *child_tidptr)
{
  int rc;
  Thread *thread;

  /* Maybe they decided not to call mtcp_init */
  if (motherofall != NULL) {

    /* They called mtcp_init meaning we are to do checkpointing.
     * So we are going to track this thread.
     */

    thread = malloc (sizeof *thread);
    memset (thread, 0, sizeof *thread);
    thread -> fn     = fn;   // this is the user's function
    thread -> arg    = arg;  // ... and the parameter
    thread -> parent = getcurrenthread ();
    if (checkpointhreadstarting) {
      checkpointhreadstarting = 0;
      mtcp_state_init(&thread->state, ST_CKPNTHREAD);
    } else {
      mtcp_state_init(&thread->state, ST_RUNDISABLED);
    }

    DPRINTF("calling clone thread=%p, fn=%p, flags=0x%X\n", thread, fn, flags);
    DPRINTF("parent_tidptr=%p, newtls=%p, child_tidptr=%p\n",
            parent_tidptr, newtls, child_tidptr);
    //asm volatile ("int3");

    /* Save exactly what the caller is supplying */

    thread -> clone_flags   = flags;
    thread -> parent_tidptr = parent_tidptr;
    thread -> given_tidptr  = child_tidptr;

    /* We need the CLEARTID feature so we can detect
     *   when the thread has exited
     * So if the caller doesn't want it, we enable it
     * Retain what the caller originally gave us so we can pass the tid back
     */

    if (!(flags & CLONE_CHILD_CLEARTID)) {
      child_tidptr = &(thread -> child_tid);
    }
    thread -> actual_tidptr = child_tidptr;
    DPRINTF("thread %p -> actual_tidptr %p\n", thread, thread -> actual_tidptr);

    /* Alter call parameters, forcing CLEARTID and make it call the wrapper
     * routine
     */

    flags |= CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID;
    fn = threadcloned;
    arg = thread;
  }

  /* mtcp_init not called, no checkpointing, but make sure clone_entry is */
  /* set up so we can call the real clone                                 */

  else if (clone_entry == NULL) setup_clone_entry ();

  /* Now create the thread */

  DPRINTF("clone fn=%p, child_stack=%p, flags=%X, arg=%p\n",
          fn, child_stack, flags, arg);
  DPRINTF("parent_tidptr=%p, newtls=%p, child_tidptr=%p\n",
          parent_tidptr, newtls, child_tidptr);
  rc = (*clone_entry) (fn, child_stack, flags, arg, parent_tidptr, newtls,
                       child_tidptr);
  if (rc < 0) {
    DPRINTF("clone rc=%d, errno=%d\n", rc, errno);
  } else {
    DPRINTF("clone rc=%d\n", rc);
  }

#ifdef PTRACE
  /* Initialize the following semaphore needed by superior to wait for inferior
   * to be created on restart. */
  if (!__init_does_inferior_exist_sem) {
    sem_init(&__does_inferior_exist_sem, 0, 1);
    __init_does_inferior_exist_sem = 1;
  }
  /* Record new pairs to files: newly traced threads to ptrace_shared_file;
   * also, write the checkpoint thread to checkpoint_threads_file. */
  if (is_ptrace_setoptions == TRUE) {
    mtcp_ptrace_info_list_insert(setoptions_superior, rc,
                                 PTRACE_UNSPECIFIED_COMMAND,
                                 FALSE, 'u', PTRACE_SHARED_FILE_OPTION);
  }
  else read_ptrace_setoptions_file(TRUE, rc);
#endif

  return (rc);
}

void fill_in_pthread (pid_t tid, pthread_t pth) {
  struct Thread *thread;
  for (thread = threads; thread != NULL; thread = thread -> next) {
    if (thread -> tid == tid) {
      thread -> pth = pth;
      break;
    }
  }
}

void delete_thread_on_pthread_join (pthread_t pth) {
  struct Thread *thread;
  for (thread = threads; thread != NULL; thread = thread -> next) {
    if (thread -> pth == pth) {
      threadisdead (thread);
      break;
    }
  }
}

asm (".global clone ; .type clone,@function ; clone = __clone");

/*****************************************************************************
 *
 *  This routine is called (via clone) as the top-level routine of a thread
 *  that we are tracking.
 *
 *  It fills in remaining items of our thread struct, calls the user function,
 *  then cleans up the thread struct before exiting.
 *
 *****************************************************************************/

static int threadcloned (void *threadv)

{
  int rc;
  Thread *const thread = threadv;

  DPRINTF("starting thread %p\n", thread);

  setupthread (thread);

  /* The new TLS should have the process ID in place at TLS_PID_OFFSET() */
  /* This is a verification step and is therefore optional as such     */
  {
    pid_t  tls_pid = *(pid_t *) (mtcp_get_tls_base_addr() + TLS_PID_OFFSET());
    if ((tls_pid != motherpid) && (tls_pid != (pid_t)-1)) {
      MTCP_PRINTF ("getpid %d, tls pid %d at offset %d, must match\n",
                    motherpid, tls_pid, TLS_PID_OFFSET());
      mtcp_printf ("      %X\n", motherpid);
      for (rc = 0; rc <= TLS_PID_OFFSET(); rc += 4) {
        tls_pid = *(pid_t *) (mtcp_get_tls_base_addr() + rc);
        mtcp_printf ("   %d: %X", rc, tls_pid);
        if ((rc & 31) == 28) mtcp_printf ("\n");
      }
      mtcp_abort ();
    }
  }

  /* If the caller wants the child tid but didn't have CLEARTID, pass the tid
   * back to it
   */

  if ((thread -> clone_flags & (CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID)) ==
          CLONE_CHILD_SETTID) {
    *(thread -> given_tidptr) = thread -> child_tid;
  }

  /* Maybe enable checkpointing by default */

  if (threadenabledefault) mtcp_ok ();

  /* Call the user's function for whatever processing they want done */

  DPRINTF("calling %p (%p)\n", thread -> fn, thread -> arg);
  rc = (*(thread -> fn)) (thread -> arg);
  DPRINTF("returned %d\n", rc);

  /* Make sure checkpointing is inhibited while we clean up and exit */
  /* Otherwise, checkpointer might wait forever for us to re-enable  */

  mtcp_no ();

  /* Do whatever to unlink and free thread block */

  threadisdead (thread);

  /* Return the user's status as the exit code */

  return (rc);
}

/*****************************************************************************
 *
 *  set_tid_address wrapper routine
 *
 *  We save the new address of the tidptr that will get cleared when the
 *  thread exits
 *
 *****************************************************************************/

static long set_tid_address (int *tidptr)

{
  long rc;
  Thread *thread;

  thread = getcurrenthread ();
  DPRINTF("thread %p -> tid %d, tidptr %p\n", thread, thread -> tid, tidptr);
  /* save new tidptr so subsequent restore will create with new pointer */
  thread -> actual_tidptr = tidptr;
  rc = mtcp_sys_set_tid_address(tidptr);
  return (rc); // now we tell kernel to change it for the current thread
}

/*****************************************************************************
 *
 *  Link thread struct to the lists and finish filling it in
 *
 *    Input:
 *
 *	thread = thread to set up
 *
 *    Output:
 *
 *	thread linked to 'threads' list and 'motherofall' tree
 *	thread -> tid = filled in with thread id
 *	thread -> state = ST_RUNDISABLED (thread initially has checkpointing
 *        disabled)
 *	signal handler set up
 *
 *****************************************************************************/

static void setupthread (Thread *thread)

{
  Thread *parent;

  /* Save the thread's ID number and put in threads list so we can look it up.
   * Set state to disable checkpointing so checkpointer won't race between
   *  adding to list and setting up handler
   */

  thread -> tid = mtcp_sys_kernel_gettid ();
  thread -> original_tid = GETTID ();

  DPRINTF("thread %p -> tid %d\n", thread, thread->tid);

  lock_threads ();

  if ((thread -> next = threads) != NULL) {
    thread -> next -> prev = &(thread -> next);
  }
  thread -> prev = &threads;
  threads = thread;

  parent = thread -> parent;
  if (parent != NULL) {
    thread -> siblings = parent -> children;
    parent -> children = thread;
  }

  unlk_threads ();
}

/*****************************************************************************
 *
 *  Set up 'clone_entry' variable
 *
 *    Output:
 *
 *	clone_entry = points to clone routine within libc.so
 *
 *****************************************************************************/

static void setup_clone_entry (void)

{
  char *p, *tmp;
  int mapsfd;

  /* Get name of whatever concoction we have for a libc shareable image */
  /* This is used by the wrapper routines                               */

  tmp = getenv ("MTCP_WRAPPER_LIBC_SO");
  if (tmp != NULL) {
    if (strlen(tmp) >= sizeof(mtcp_libc_area.name)) {
      MTCP_PRINTF("libc area name (%s) too long (>=1024 chars)\n", tmp);
      mtcp_abort();
    }
    strncpy (mtcp_libc_area.name, tmp, sizeof mtcp_libc_area.name);
  } else {
    mapsfd = mtcp_sys_open2 ("/proc/self/maps", O_RDONLY);
    if (mapsfd < 0) {
      MTCP_PRINTF("error opening /proc/self/maps: %s\n", MTCP_STR_ERRNO);
      mtcp_abort ();
    }
    p = NULL;
    while (readmapsline (mapsfd, &mtcp_libc_area)) {
      p = strstr (mtcp_libc_area.name, "/libc");
      /* We can't do a dlopen on the debug version of libc. */
      if (((p != NULL) && ((p[5] == '-') || (p[5] == '.'))) &&
          !strstr(mtcp_libc_area.name, "debug")) break;
    }
    close (mapsfd);
    if (p == NULL) {
      MTCP_PRINTF("cannot find */libc[-.]* in /proc/self/maps\n");
      mtcp_abort ();
    }
  }
  mtcp_libc_dl_handle = dlopen (mtcp_libc_area.name, RTLD_LAZY | RTLD_GLOBAL);
  if (mtcp_libc_dl_handle == NULL) {
    MTCP_PRINTF("error opening libc shareable %s: %s\n",
                mtcp_libc_area.name, dlerror ());
    mtcp_abort ();
  }

  /* Find the clone routine therein */

  clone_entry = mtcp_get_libc_symbol ("__clone");
}

/*****************************************************************************
 *
 *  Thread has exited - unlink it from lists and free struct
 *
 *    Input:
 *
 *	thread = thread that has exited
 *
 *    Output:
 *
 *	thread removed from 'threads' list and motherofall tree
 *	thread pointer no longer valid
 *	checkpointer woken if waiting for this thread
 *
 *****************************************************************************/

static void threadisdead (Thread *thread)

{
  Thread **lthread, *parent, *xthread;

  lock_threads ();

  DPRINTF("thread %p -> tid %d\n", thread, thread -> tid);

  /* Remove thread block from 'threads' list */

  if ((*(thread -> prev) = thread -> next) != NULL) {
    thread -> next -> prev = thread -> prev;
  }

  /* Remove thread block from parent's list of children */

  parent = thread -> parent;
  if (parent != NULL) {
    for (lthread = &(parent -> children);
         (xthread = *lthread) != thread;
         lthread = &(xthread -> siblings)) {}
    *lthread = xthread -> siblings;
  }

  /* If this thread has children, give them to its parent */

  if (parent != NULL) {
    while ((xthread = thread -> children) != NULL) {
      thread -> children = xthread -> siblings;
      xthread -> siblings = parent -> children;
      parent -> children = xthread;
    }
  } else {
    while ((xthread = thread -> children) != NULL) {
      thread -> children = xthread -> siblings;
      xthread -> siblings = motherofall;
      motherofall = xthread;
    }
  }

  unlk_threads ();

  /* If checkpointer is waiting for us, wake it to see this thread no longer in
   * list
   */

  mtcp_state_futex (&(thread -> state), FUTEX_WAKE, 1, NULL);

  mtcp_state_destroy( &(thread -> state) );

  free (thread);
}

void *mtcp_get_libc_symbol (char const *name)

{
  void *temp;

  temp = dlsym (mtcp_libc_dl_handle, name);
  if (temp == NULL) {
    MTCP_PRINTF ("error getting %s from %s: %s\n",
                 name, mtcp_libc_area.name, dlerror ());
    mtcp_abort ();
  }
  return (temp);
}

/*****************************************************************************
 *
 *  Call this when it's OK to checkpoint
 *
 *****************************************************************************/

int mtcp_ok (void)

{
  Thread *thread;

  if (getenv("MTCP_NO_CHECKPOINT"))
    return 0;
  thread = getcurrenthread ();

again:
  switch (mtcp_state_value(&thread -> state)) {

    /* Thread was running normally with checkpointing disabled.  Enable
     * checkpointing then just return.
     */

    case ST_RUNDISABLED: {
      if (!mtcp_state_set (&(thread -> state), ST_RUNENABLED, ST_RUNDISABLED))
        goto again;
      return (0);
    }

    /* Thread was running normally with checkpointing already enabled.  So just
     * return as is.
     */

    case ST_RUNENABLED: {
      return (1);
    }

    /* Thread was running with checkpointing disabled, but the checkpointhread
     * wants to write a checkpoint.  So mark the thread as having checkpointing
     * enabled, then just 'manually' call the signal handler as if the signal to
     * suspend were just sent.
     */

    case ST_SIGDISABLED: {
      if (!mtcp_state_set (&(thread -> state), ST_SIGENABLED, ST_SIGDISABLED))
        goto again;
      stopthisthread (0);
      return (0);
    }

    /* Thread is running with checkpointing enabled, but the checkpointhread
     * wants to write a checkpoint and has sent a signal telling the thread to
     * call 'stopthisthread'.  So we'll just keep going as is until the signal
     * is actually delivered.
     */

    case ST_SIGENABLED: {
      return (1);
    }

    /* Thread is the checkpointhread so we just ignore the call (from
     * threadcloned routine).
     */

    case ST_CKPNTHREAD: {
      return (-1);
    }

    /* How'd we get here? */

    default: {
      mtcp_abort ();
      return (0); /* NOTREACHED : stop compiler warning */
    }
  }
}

/* Likewise, disable checkpointing */

int mtcp_no (void)
{
  Thread *thread;

  if (getenv("MTCP_NO_CHECKPOINT"))
    return 0;
  thread = getcurrenthread ();

again:
  switch (mtcp_state_value(&thread -> state)) {
    case ST_RUNDISABLED: {
      return (0);
    }

    case ST_RUNENABLED: {
      if (!mtcp_state_set (&(thread -> state), ST_RUNDISABLED, ST_RUNENABLED))
        goto again;
      return (1);
    }

    case ST_SIGDISABLED: {
      return (0);
    }

    case ST_SIGENABLED: {
      stopthisthread (0);
      goto again;
    }

    default: {
      mtcp_abort ();
      return (0); /* NOTREACHED : stop compiler warning */
    }
  }
}

/* This is used by ../dmtcp/src/mtcpinterface.cpp */
void mtcp_kill_ckpthread (void)
{
  Thread *thread;

  lock_threads ();
  for (thread = threads; thread != NULL; thread = thread -> next) {
    if ( mtcp_state_value(&thread -> state) == ST_CKPNTHREAD ) {
      unlk_threads ();
      DPRINTF("Kill checkpinthread, tid=%d\n",thread->tid);
      mtcp_sys_kernel_tkill(thread -> tid, STOPSIGNAL);
      return;
    }
  }
  unlk_threads ();
}


/*************************************************************************
 *
 *  Save and restore terminal settings.
 *
 *************************************************************************/

static int saved_termios_exists = 0;
static struct termios saved_termios;
static struct winsize win;

static void save_term_settings() {
  saved_termios_exists = ( isatty(STDIN_FILENO)
  		           && tcgetattr(STDIN_FILENO, &saved_termios) >= 0 );
  if (saved_termios_exists)
    ioctl (STDIN_FILENO, TIOCGWINSZ, (char *) &win);
}
int safe_tcsetattr(int fd, int optional_actions,
		   const struct termios *termios_p) {
  struct termios old_termios, new_termios;
  /* We will compare old and new, and we don't want uninitialized data */
  memset(&new_termios, 0, sizeof(new_termios));
  /* tcgetattr returns success as long as at least one of requested
   * changes was executed.  So, repeat until no more changes.
   */ 
  do {
    memcpy(&old_termios, &new_termios, sizeof(new_termios));
    if (tcsetattr(fd, TCSANOW, termios_p) == -1) return -1;
    if (tcgetattr(fd, &new_termios) == -1) return -1;
  } while (memcmp(&new_termios, &old_termios, sizeof(new_termios)) != 0);
  return 0;
}
static void restore_term_settings() {
  if (saved_termios_exists){
    /* First check if we are in foreground. If not, skip this and print
     *   warning.  If we try to call tcsetattr in background, we will hang up.
     */
    int foreground = (tcgetpgrp(STDIN_FILENO) == getpgrp());
    DPRINTF("restore terminal attributes, check foreground status first: %d\n",
            foreground);
    if (foreground) {
      if ( ( ! isatty(STDIN_FILENO)
             || safe_tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios) == -1) )
        DPRINTF("WARNING: failed to restore terminal\n");
      else {
        struct winsize cur_win;
        DPRINTF("restored terminal\n");
        ioctl (STDIN_FILENO, TIOCGWINSZ, (char *) &cur_win);
	/* ws_row/ws_col was probably not 0/0 prior to checkpoint.  We change
	 * it back to last known row/col prior to checkpoint, and then send a
	 * SIGWINCH (see below) to notify process that window might have changed
	 */
        if (cur_win.ws_row == 0 && cur_win.ws_col == 0)
          ioctl (STDIN_FILENO, TIOCSWINSZ, (char *) &win);
      }
    } else {
      DPRINTF("WARNING:skip restore terminal step -- we are in BACKGROUND\n");
    }
  }
  if (kill(getpid(), SIGWINCH) == -1) {}  /* No remedy if error */
}

/*************************************************************************
 *
 *  This executes as a thread.  It sleeps for the checkpoint interval
 *    seconds, then wakes to write the checkpoint file.
 *
 *************************************************************************/

static void *checkpointhread (void *dummy)
{
  int needrescan;
  struct timespec sleeperiod;
  struct timeval started, stopped;
  Thread *thread;
  char * dmtcp_checkpoint_filename = NULL;

  /* This is the start function of the checkpoint thread.
   * We also call getcontext to get a snapshot of this call frame,
   * since we will never exit this call frame.  We always return
   * to this call frame at time of startup, on restart.  Hence, restart
   * will forget any modifications to our local variables since restart.
   */
  static int originalstartup = 1;

#ifdef PTRACE
  DPRINTF("begin init_thread_local\n");
  init_thread_local();

  mtcp_ptrace_info_list_insert(getpid(), mtcp_sys_kernel_gettid(), 
                               PTRACE_UNSPECIFIED_COMMAND, FALSE,
                               'u', PTRACE_CHECKPOINT_THREADS_FILE_OPTION);
#endif

  /* We put a timeout in case the thread being waited for exits whilst we are
   * waiting
   */

  static struct timespec const enabletimeout = { 10, 0 };

  DPRINTF("%d started\n", mtcp_sys_kernel_gettid ());

  /* Set up our restart point, ie, we get jumped to here after a restore */

  ckpthread = getcurrenthread ();

  save_sig_state( ckpthread );
  save_tls_state (ckpthread);
  /* Release user thread after we've initialized. */
  sem_post(&sem_start);
  /* After we restart, we return here. */
  if (getcontext (&(ckpthread -> savctx)) < 0) mtcp_abort ();

  DPRINTF("after getcontext. current_tid %d, original_tid:%d\n",
          mtcp_sys_kernel_gettid(), ckpthread->original_tid);
  if (originalstartup)
    originalstartup = 0;
  else {

    /* We are being restored.  Wait for all other threads to finish being
     * restored before resuming checkpointing.
     */

    DPRINTF("waiting for other threads after restore\n");
    wait_for_all_restored ();
#ifdef PTRACE
    create_file (GETTID());
#endif
    DPRINTF("resuming after restore\n");
  }

  /* Reset the verification counter - on init, this will set it to it's start
   * value. After a verification, it will reset it to its start value.  After a
   * normal restore, it will set it to its start value.  So this covers all
   * cases.
   */

  verify_count = verify_total;
  DPRINTF("After verify count mtcp checkpointhread*: %d started\n",
	  mtcp_sys_kernel_gettid ());

  while (1) {
    /* Wait a while between writing checkpoint files */

    if (callback_sleep_between_ckpt == NULL)
    {
        memset (&sleeperiod, 0, sizeof sleeperiod);
        sleeperiod.tv_sec = intervalsecs;
        while (nanosleep (&sleeperiod, &sleeperiod) < 0 && errno == EINTR) {}
    }
    else
    {
        DPRINTF("before callback_sleep_between_ckpt(%d)\n",intervalsecs);
        (*callback_sleep_between_ckpt)(intervalsecs);
        DPRINTF("after callback_sleep_between_ckpt(%d)\n",intervalsecs);
    }

    mtcp_sys_gettimeofday (&started, NULL);
    checkpointsize = 0;

#ifdef PTRACE
    pid_t ckpt_leader = 0;
    if (ptracing()) {
      /* One of the threads is the ckpt thread. Don't count that in. */
      nthreads = -1;
      for (thread = threads; thread != NULL; thread = thread -> next) {
        nthreads++;
      }
      proceed_to_checkpoint = 0;

      read_ptrace_setoptions_file(FALSE, 0);

      int ckpt_leader_fd = -1;

      if (possible_ckpt_leader(GETTID())) {
        ckpt_leader_fd = open(ckpt_leader_file, O_CREAT|O_EXCL|O_WRONLY, 0644);
        if (ckpt_leader_fd != -1) {
          ckpt_leader = 1;    
          close(ckpt_leader_fd);
        }
      }
 
      /* Is the checkpoint thread being traced? If yes, wait for the superior
       * to arrive at stopthisthread. */
      if (callback_get_next_ptrace_info) {
        int ckpt_ptraced_by = 0;
        int index = 0;
        struct ptrace_info pt_info;
        while (empty_ptrace_info(
                 pt_info = (*callback_get_next_ptrace_info)(index++))) {
          if (pt_info.inferior == GETTID()) {
            ckpt_ptraced_by = pt_info.superior;
            break;
          }
        }
        /* The checkpoint thread might not be in the list of threads yet.
         * However, we have one more chance of finding it, by reading
         * ptrace_shared_file. */
        if (!ckpt_ptraced_by) {
          ckpt_ptraced_by = is_ckpt_in_ptrace_shared_file(GETTID());  
        }
        if (ckpt_ptraced_by) {
          DPRINTF("ckpt %d is being traced by %d.\n",
                  GETTID(), ckpt_ptraced_by);
          ptrace_wait4(ckpt_ptraced_by);
          DPRINTF("ckpt %d is done waiting for its "
                  "superior %d: superior is in stopthisthread.\n",
                  GETTID(), ckpt_ptraced_by);
        } else DPRINTF("ckpt %d not being traced.\n", GETTID());
      }
    }
#endif 

    /* Halt all other threads - force them to call stopthisthread
     * If any have blocked checkpointing, wait for them to unblock before
     * signalling
     */

rescan:
    needrescan = 0;
    lock_threads ();
    for (thread = threads; thread != NULL; thread = thread -> next) {

      /* If thread no longer running, remove it from thread list */

again:
      if (*(thread -> actual_tidptr) == 0) {
        DPRINTF("thread %d disappeared\n", thread -> tid);
        unlk_threads ();
        threadisdead (thread);
        goto rescan;
      }

      /* Do various things based on thread's state */

      switch (mtcp_state_value (&thread -> state) ) {

        /* Thread is running but has checkpointing disabled    */
        /* Tell the mtcp_ok routine that we are waiting for it */
        /* We will need to rescan so we will see it suspended  */

        case ST_RUNDISABLED: {
          if (!mtcp_state_set (&(thread -> state), ST_SIGDISABLED,
                               ST_RUNDISABLED))
            goto again;
          needrescan = 1;
          break;
        }

        /* Thread is running and has checkpointing enabled                 */
        /* Send it a signal so it will call stopthisthread                 */
        /* We will need to rescan (hopefully it will be suspended by then) */

        case ST_RUNENABLED: {
          if (!mtcp_state_set(&(thread -> state), ST_SIGENABLED, ST_RUNENABLED))
            goto again;
#ifdef PTRACE
          if (ptracing()) {
            has_new_ptrace_shared_file = 0;
  
            char inferior_st;
            int ptrace_fd = open(ptrace_shared_file, O_RDONLY);
            if (ptrace_fd == -1) {
              /* There is no ptrace_shared_file. All ptrace pairs are in memory.
               * Thus we only need to update the ptrace pairs. */
              mtcp_ptrace_info_list_save_threads_state();
              inferior_st = retrieve_inferior_state(thread -> original_tid);
            } else {
              has_new_ptrace_shared_file = 1;
              int new_ptrace_fd = -1;
  
              if (ckpt_leader)
                new_ptrace_fd = open(new_ptrace_shared_file,
                                     O_CREAT|O_APPEND|O_WRONLY|O_FSYNC, 0644);
              pid_t superior, inferior;
              char inf_st;
              while (read_no_error(ptrace_fd, &superior, sizeof(pid_t)) > 0) {
                read_no_error(ptrace_fd, &inferior, sizeof(pid_t));
                inf_st = procfs_state(inferior);
                if (inferior == thread -> original_tid) inferior_st = inf_st;
                if (ckpt_leader && new_ptrace_fd != -1) {
                  if (write(new_ptrace_fd, &superior, sizeof(pid_t)) == -1) {
                    MTCP_PRINTF("Error writing to file: %s\n", strerror(errno));
                    mtcp_abort();
                  }
                  if (write(new_ptrace_fd, &inferior, sizeof(pid_t)) == -1) {
                    MTCP_PRINTF("Error writing to file: %s\n", strerror(errno));
                    mtcp_abort();
                  }
                  if (write(new_ptrace_fd, &inf_st, sizeof(char)) == -1) {
                    MTCP_PRINTF("Error writing to file: %s\n", strerror(errno));
                    mtcp_abort();
                  }
                }
              }
              if (ckpt_leader && new_ptrace_fd != -1) {
                if (close(new_ptrace_fd) != 0) {
                  MTCP_PRINTF("error while closing file: %s\n", strerror(errno));
                  mtcp_abort();
                }
              }
              if (close(ptrace_fd) != 0) {
                MTCP_PRINTF("error while closing file, %s\n", strerror(errno));
                mtcp_abort();
              }
            }
            DPRINTF("%d %c\n", GETTID(), inferior_st);
            if (inferior_st == 'N') {
              /* If the state is unknown, send a stop signal to inferior. */
              if (mtcp_sys_kernel_tkill(thread -> tid, STOPSIGNAL) < 0) {
                if (mtcp_sys_errno != ESRCH) {
                  MTCP_PRINTF("error signalling thread %d: %s\n",
                              thread -> tid, strerror (mtcp_sys_errno));
                }
                unlk_threads();
                threadisdead(thread);
                goto rescan;
              }
            } else {
              DPRINTF("%c %d\n", inferior_st, thread -> original_tid);
              /* If the state is not stopped, then send a stop signal to
               * the inferior. */
              if (inferior_st != 'T') {
                if (mtcp_sys_kernel_tkill(thread -> tid, STOPSIGNAL) < 0) {
                  if (mtcp_sys_errno != ESRCH) {
                    MTCP_PRINTF("error signalling thread %d: %s\n",
                                thread -> tid, strerror(mtcp_sys_errno));
                  }
                  unlk_threads();
                  threadisdead(thread);
                  goto rescan;
                }
              }
              create_file(thread -> original_tid);
            }
          } else {
            if (mtcp_sys_kernel_tkill (thread -> tid, STOPSIGNAL) < 0) {
              if (mtcp_sys_errno != ESRCH) {
                MTCP_PRINTF ("error signalling thread %d:  %s\n",
                             thread -> tid, strerror (mtcp_sys_errno));
              }
              unlk_threads ();
              threadisdead (thread);
              goto rescan;
            }
          }
#else
          if (mtcp_sys_kernel_tkill (thread -> tid, STOPSIGNAL) < 0) {
            if (mtcp_sys_errno != ESRCH) {
              MTCP_PRINTF ("error signalling thread %d: %s\n",
                           thread -> tid, strerror (mtcp_sys_errno));
            }
            unlk_threads ();
            threadisdead (thread);
            goto rescan;
          }
#endif
          needrescan = 1;
          break;
        }

        /* Thread is running, we have signalled it to stop, but it has
	 * checkpointing disabled.  So we wait for it to change state.
         * We have to unlock because it may need lock to change state.
	 */

        case ST_SIGDISABLED: {
          unlk_threads ();
          mtcp_state_futex (&(thread -> state), FUTEX_WAIT, ST_SIGDISABLED,
			    &enabletimeout);
          goto rescan;
        }

        /* Thread is running and we have sent signal to stop it             */
        /* So we have to wait for it to change state (enter signal handler) */
        /* We have to unlock because it may try to use lock meanwhile       */

        case ST_SIGENABLED: {
          unlk_threads ();
          mtcp_state_futex (&(thread -> state), FUTEX_WAIT, ST_SIGENABLED,
			    &enabletimeout);
          goto rescan;
        }

        /* Thread has entered signal handler and is saving its context.
         * So we have to wait for it to finish doing so.  We don't need
	 * to unlock because it won't use lock before changing state.
	 */

        case ST_SUSPINPROG: {
          mtcp_state_futex (&(thread -> state), FUTEX_WAIT, ST_SUSPINPROG,
			    &enabletimeout);
          goto again;
        }

        /* Thread is suspended and all ready for us to write checkpoint file */

        case ST_SUSPENDED: {
          break;
        }

        /* Don't do anything to the checkpointhread (this) thread */

        case ST_CKPNTHREAD: {
          break;
        }

        /* Who knows? */

        default: {
          mtcp_abort ();
        }
      }
    }
    unlk_threads ();

    /* If need to rescan (ie, some thread possibly not in ST_SUSPENDED STATE),
     * check them all again
     */

    if (needrescan) goto rescan;
#ifdef PTRACE
    if (ptracing())
      /* No need for a mutex. We're before the barrier. */
      jalib_ckpt_unlock_ready = 0;
#endif
    RMB; // matched by WMB in stopthisthread
    DPRINTF("everything suspended\n");

    /* If no threads, we're all done */

    if (threads == NULL) {
      DPRINTF("exiting (no threads)\n");
      return (NULL);
    }

#ifdef PTRACE
    int cont = 1;
    if (ptracing()) {
      if (callback_jalib_ckpt_unlock) (*callback_jalib_ckpt_unlock)();
      else {
        MTCP_PRINTF("invalid callback_jalib_ckpt_unlock.\n");
        mtcp_abort();
      }
      /* Allow user threads to process new_ptrace_shared_file. */
      pthread_mutex_lock(&jalib_ckpt_unlock_lock);
      jalib_ckpt_unlock_ready = 1;
      pthread_mutex_unlock(&jalib_ckpt_unlock_lock);

      /* Wait for new_ptrace_shared_file to be read by motherofall. Detach. */
      while (cont) {
        pthread_mutex_lock(&nthreads_lock);
        if (nthreads == 0) {
          pthread_mutex_lock(&proceed_to_checkpoint_lock);
          proceed_to_checkpoint = 1;
          pthread_mutex_unlock(&proceed_to_checkpoint_lock);
          cont = 0;
        }
        pthread_mutex_unlock(&nthreads_lock);
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 100000000;
        nanosleep(&ts, NULL);
      }
    }
#endif

    /* Call weak symbol of this file, possibly overridden by user's strong
     * symbol. User must compile his/her code with -Wl,-export-dynamic to make
     * it visible.
     */
    mtcpHookPreCheckpoint();

    save_sig_handlers();

    /* All other threads halted in 'stopthisthread' routine (they are all
     * in state ST_SUSPENDED).  It's safe to write checkpoint file now.
     */
    if (callback_pre_ckpt != NULL){
      // Here we want to sync the shared memory pages with the backup files
      DPRINTF("syncing shared memory with backup files\n");
      sync_shared_mem();

      DPRINTF("before callback_pre_ckpt() (&%x,%x) \n",
	       &callback_pre_ckpt, callback_pre_ckpt);
      (*callback_pre_ckpt)(&dmtcp_checkpoint_filename);
      if (dmtcp_checkpoint_filename &&
          strcmp(dmtcp_checkpoint_filename, "/dev/null") != 0) {
        mtcp_sys_strcpy(perm_checkpointfilename, dmtcp_checkpoint_filename);
        DPRINTF("Checkpoint filename changed to %s\n", perm_checkpointfilename);
      }
    }

    // kernel returns mm->brk when passed zero
    mtcp_saved_break = (void*) mtcp_sys_brk(NULL);

    /* Do this once, same for all threads.  But restore for each thread. */
    if (mtcp_have_thread_sysinfo_offset())
      saved_sysinfo = mtcp_get_thread_sysinfo();
    /* Do this once.  It's the same for all threads. */
    save_term_settings();

    if (getcwd(mtcp_saved_working_directory, MAXPATHLEN) == NULL) {
      // buffer wasn't large enough
      perror("getcwd");
      MTCP_PRINTF ("getcwd failed.");
      mtcp_abort ();
    }

    DPRINTF("mtcp_saved_break=%p\n", mtcp_saved_break);

    if ( dmtcp_checkpoint_filename == NULL ||
         strcmp (dmtcp_checkpoint_filename, "/dev/null") != 0) {
      checkpointeverything ();
    } else {
      MTCP_PRINTF("received \'/dev/null\' as ckpt filename.\n"
                  "*** Skipping checkpoint. ***\n");
    }

    if (callback_post_ckpt != NULL){
        DPRINTF("before callback_post_ckpt() (&%x,%x) \n",
		 &callback_post_ckpt, callback_post_ckpt);
        (*callback_post_ckpt)(0, NULL);
    }
    if (showtiming) {
      mtcp_sys_gettimeofday (&stopped, NULL);
      stopped.tv_usec +=
        (stopped.tv_sec - started.tv_sec) * 1000000 - started.tv_usec;
      MTCP_PRINTF("time %u uS, size %u megabytes, avg rate %u MB/s\n",
                  stopped.tv_usec, (unsigned int)(checkpointsize / 1000000),
                  (unsigned int)(checkpointsize / stopped.tv_usec));
    }

    /* Call weak symbol of this file, possibly overridden by user's strong
     * symbol. User must compile his/her code with -Wl,-export-dynamic to make
     * it visible.
     */
    mtcpHookPostCheckpoint();

    /* Resume all threads.  But if we're doing a checkpoint verify,
     * abort all threads except the main thread, as we don't want them
     * running when we exec the mtcp_restore program.
     */

    DPRINTF("resuming everything\n");
    lock_threads();
    for (thread = threads; thread != NULL; thread = thread -> next) {
      if (mtcp_state_value(&(thread -> state)) != ST_CKPNTHREAD) {
        if (!mtcp_state_set (&(thread -> state), ST_RUNENABLED, ST_SUSPENDED))
	  mtcp_abort();
        mtcp_state_futex(&(thread -> state), FUTEX_WAKE, 1, NULL);
      }
    }
    unlk_threads ();
    DPRINTF("everything resumed\n");
    /* But if we're doing a restore verify, just exit.  The main thread is doing
     * the exec to start the restore.
     */
#ifdef PTRACE
    create_file(GETTID());
#endif
    if ((verify_total != 0) && (verify_count == 0)) return (NULL);
  }
}

/*
 * This function returns the fd to which the checkpoint file should be written.
 * The purpose of using this function over mtcp_sys_open() is that this
 * function will handle compression and gzipping.
 */
static int test_use_compression(char *compressor, char *command, char *path,
                                int def)
{
  char *default_val;
  char env_var1[256] = "MTCP_";
  char env_var2[256] = "DMTCP_";
  char *do_we_compress;
  
  int env_var1_len = sizeof(env_var1);
  int env_var2_len = sizeof(env_var2);

  if (def)
    default_val = "1";
  else
    default_val = "0";

  strncat(env_var1,compressor,env_var1_len);
  strncat(env_var2,compressor,env_var2_len);
  do_we_compress = getenv(env_var1);
  // allow alternate name for env var
  if (do_we_compress == NULL){
    do_we_compress = getenv(env_var2);
  }
  // env var is unset, let's default to enabled
  // to disable compression, run with MTCP_GZIP=0
  if (do_we_compress == NULL)
    do_we_compress = default_val;

  char *endptr;
  strtol(do_we_compress, &endptr, 0);
  if ( *do_we_compress == '\0' || *endptr != '\0' ) {
    mtcp_printf("WARNING: %s/%s defined as %s (not a number)\n"
	        "  Checkpoint image will not be compressed.\n",
	        env_var1, env_var2, do_we_compress);
    do_we_compress = "0";
  }

  if ( 0 == strcmp(do_we_compress, "0") )
    return 0;

  /* Check if the executable exists. */
  if (mtcp_find_executable(command, path) == NULL) {
    MTCP_PRINTF("WARNING: %s cannot be executed. Compression will "
                "not be used.\n", command);
    return 0;
  }

  /* If we arrive down here, it's safe to compress. */
  return 1;
}

#ifdef HBICT_DELTACOMP
static int open_ckpt_to_write_hbict(int fd, int pipe_fds[2], char *hbict_path,
                                    char *gzip_path)
{
  char *hbict_args[] = { "hbict", "-a", NULL, NULL };
  hbict_args[0] = hbict_path;
  DPRINTF("open_ckpt_to_write_hbict\n");

  if (gzip_path != NULL){
    hbict_args[2] = "-z100";
  }
  return open_ckpt_to_write(fd,pipe_fds,hbict_args);
}
#endif

static int open_ckpt_to_write_gz(int fd, int pipe_fds[2], char *gzip_path)
{
  char *gzip_args[] = { "gzip", "-1", "-", NULL };
  gzip_args[0] = gzip_path;
  DPRINTF("open_ckpt_to_write_gz\n");

  return open_ckpt_to_write(fd,pipe_fds,gzip_args);
}

static int 
open_ckpt_to_write(int fd, int pipe_fds[2], char **extcomp_args)
{
  pid_t cpid;

  cpid = mtcp_sys_fork();
  if (cpid == -1) {
    MTCP_PRINTF("WARNING: error forking child process `%s`.  Compression will "
                "not be used [%s].\n", extcomp_args[0],
                strerror(mtcp_sys_errno));
    mtcp_sys_close(pipe_fds[0]);
    mtcp_sys_close(pipe_fds[1]);
    //fall through to return fd
  } else if (cpid > 0) { /* parent process */
    //Before running gzip in child process, we must not use LD_PRELOAD.
    // See revision log 342 for details concerning bash.
    mtcp_ckpt_extcomp_child_pid = cpid;
    if (mtcp_sys_close(pipe_fds[0]) == -1)
      MTCP_PRINTF("WARNING: close failed: %s\n", MTCP_STR_ERRNO);
    if (mtcp_sys_close(fd) == -1)
      MTCP_PRINTF("WARNING: close failed: %s\n", MTCP_STR_ERRNO);
    fd=pipe_fds[1];//change return value
  } else { /* child process */
    static int (*libc_unsetenv) (const char *name);
    static int (*libc_execvp) (const char *path, char *const argv[]);

    mtcp_sys_close(pipe_fds[1]);
    dup2(pipe_fds[0], STDIN_FILENO);
    mtcp_sys_close(pipe_fds[0]);
    dup2(fd, STDOUT_FILENO);
    mtcp_sys_close(fd);

    // Don't load dmtcphijack.so, etc. in exec.
    unsetenv("LD_PRELOAD"); // If in bash, this is bash env. var. version
    libc_unsetenv = mtcp_get_libc_symbol("unsetenv");
    (*libc_unsetenv)("LD_PRELOAD");

    DPRINTF("open_ckpt_to_write: exec\n");

    libc_execvp = mtcp_get_libc_symbol("execvp");
    (*libc_execvp)(extcomp_args[0], extcomp_args);

    /* should not arrive here */
    MTCP_PRINTF("ERROR: compression failed!  No checkpointing will be "
                "performed!  Cancel now!\n");
    mtcp_sys_exit(1);
  }

  return fd;
}


/*****************************************************************************
 *
 *  This routine is called from time-to-time to write a new checkpoint file.
 *  It assumes all the threads are suspended.
 *
 *****************************************************************************/

static void checkpointeverything (void)
{
  DPRINTF("thread:%d performing checkpoint.\n", mtcp_sys_kernel_gettid ());

  int tmpDMTCPHeaderFd = perform_callback_write_dmtcp_header();

  int forked_ckpt_status = test_and_prepare_for_forked_ckpt(tmpDMTCPHeaderFd);
  if (forked_ckpt_status == FORKED_CKPT_MASTER) {
    return;
  }

  /* Note:  Must use mtcp_sys_pipe(), to go to kernel, since
   *   DMTCP has a wrapper around glibc promoting pipes to socketpairs,
   *   DMTCP doesn't directly checkpoint/restart pipes.
   */
  /* 4. Open fd to checkpoint image on disk */
  /* Create temp checkpoint file and write magic number to it */
  /* This is a callback to DMTCP.  DMTCP writes header and returns fd. */
#ifdef FAST_CKPT_RST_VIA_MMAP
  int flags = O_CREAT | O_TRUNC | O_RDWR;
#else
  int flags = O_CREAT | O_TRUNC | O_WRONLY;
#endif
  int fd = mtcp_safe_open(temp_checkpointfilename, flags, 0600);
  if (fd < 0) {
    MTCP_PRINTF("error creating %s: %s\n",
                temp_checkpointfilename, strerror(mtcp_sys_errno));
    mtcp_abort();
  }

#ifndef FAST_CKPT_RST_VIA_MMAP
  int use_compression = test_and_prepare_for_compression(&fd);
#endif

  write_ckpt_to_file(fd, tmpDMTCPHeaderFd);

#ifndef FAST_CKPT_RST_VIA_MMAP
  if (use_compression) {
    /* IF OUT OF DISK SPACE, REPORT IT HERE. */
    /* In test_and_prepare_for_compression(), we set SIGCHLD to SIG_IGN, i.e.
     * we ignore the signal. This is done to avoid calling the user SIGCHLD
     * handler (if the user has installed one). As a result, mtcp_sys_wait4()
     * will always fail for the child pid. */
#if 0
    if ( mtcp_sys_wait4(mtcp_ckpt_extcomp_child_pid, NULL, 0, NULL ) == -1 ) {
      DPRINTF("(grandchild): waitpid: %s\n", strerror(errno));
    }
#endif
    mtcp_ckpt_extcomp_child_pid = -1;
    sigaction(SIGCHLD, &sigactions[SIGCHLD], NULL);
  }
#endif

  /* Maybe it's time to verify the checkpoint.
   * If so, exec an mtcp_restore with the temp file (in case temp file is bad,
   *   we'll still have the last one).
   * If the new file is good, mtcp_restore will rename it over the last one.
   */

  if (verify_total != 0) -- verify_count;

  /* Now that temp checkpoint file is complete, rename it over old permanent
   * checkpoint file.  Uses rename() syscall, which doesn't change i-nodes.
   * So, gzip process can continue to write to file even after renaming.
   */

  else renametempoverperm ();

  if (forked_ckpt_status == FORKED_CKPT_WORKER)
    mtcp_sys_exit (0); /* grandchild exits */

  DPRINTF("checkpoint complete\n");
}

int perform_callback_write_dmtcp_header()
{
  char tmpDMTCPHeaderBuf[] = "/tmp/dmtcp.XXXXXX";
  char *tmpDMTCPHeaderFileName = tmpDMTCPHeaderBuf;

  int tmpfd = -1;
  if (callback_write_dmtcp_header != 0) {
    /* Temp file for DMTCP header; will be written into the checkpoint file. */
    tmpfd = mkstemp(tmpDMTCPHeaderFileName);
    if (tmpfd < 0) {
      MTCP_PRINTF("error %d creating temp file: %s\n", errno, strerror(errno));
      mtcp_abort();
    }

    if (unlink(tmpDMTCPHeaderFileName) == -1) {
      MTCP_PRINTF("error %d unlinking temp file: %s\n", errno, strerror(errno));
    }

    /* Better to do this in parent, not child, for most accurate header info */
    (*callback_write_dmtcp_header)(tmpfd);
  }
  return tmpfd;
}

int test_and_prepare_for_compression(int *fd)
{
  char *gzip_cmd = "gzip";
  char gzip_path[MTCP_MAX_PATH];

  int use_gzip_compression = 0;
  int use_deltacompression = 0;

  /* 1. Test if using GZIP compression */
  use_gzip_compression = test_use_compression("GZIP", gzip_cmd, gzip_path, 1);

#ifdef HBICT_DELTACOMP
  char *hbict_cmd = "hbict";
  char hbict_path[MTCP_MAX_PATH];
  MTCP_PRINTF("NOTICE: hbict compression is enabled\n");

  /* 2. Test if using HBICT compression */
  use_deltacompression = test_use_compression("HBICT", hbict_cmd, hbict_path,
                                              1);
#endif

  if (!use_gzip_compression && !use_deltacompression) {
    return 0;
  }

  /* 3. Open pipe */ 
  int pipe_fds[2];
  if (mtcp_sys_pipe(pipe_fds) == -1) {
    MTCP_PRINTF("WARNING: error creating pipe. Compression will "
                "not be used.\n");
    use_gzip_compression = use_deltacompression = 0;
    return 0;
  }

  /* 5. We now have the information to pipe to gzip, or directly to fd
  *     We do it this way, so that gzip will be direct child of forked process
  *       when using forked checkpointing.
  */

  /* disable SIGCHLD handling; will be restored after gzip finishes */
  struct sigaction ignore_sigchld_action;
  ignore_sigchld_action.sa_handler = SIG_IGN;
  sigaction(SIGCHLD, &ignore_sigchld_action, NULL);

  if (use_deltacompression) { /* fork a hbict process */
#ifdef HBICT_DELTACOMP
    *fd = open_ckpt_to_write_hbict(*fd, pipe_fds, hbict_path, gzip_path);
#endif
  } else if (use_gzip_compression) {/* fork a gzip process */
    *fd = open_ckpt_to_write_gz(*fd, pipe_fds, gzip_path);
  }

  return 1;
}

int test_and_prepare_for_forked_ckpt(int tmpDMTCPHeaderFd)
{
#ifdef TEST_FORKED_CHECKPOINTING
  return 1;
#endif

  if (getenv("MTCP_FORKED_CHECKPOINT") == NULL) {
    return 0;
  }

  int fds[2];
  if (mtcp_sys_pipe(fds) == -1) {
    MTCP_PRINTF("Error creating pipe :%s\n", MTCP_STR_ERRNO);
    mtcp_abort();
  }

  pid_t forked_cpid = mtcp_sys_fork();
  if (forked_cpid == -1) {
    MTCP_PRINTF("WARNING: Failed to do forked checkpointing,"
                " trying normal checkpoint\n");
    return FORKED_CKPT_FAILED;
  } else if (forked_cpid > 0) {
    // Calling mtcp_sys_waitpid here, but on 32-bit Linux, libc:waitpid()
    // calls wait4()
    if ( mtcp_sys_wait4(forked_cpid, NULL, 0, NULL) == -1 ) {
      DPRINTF("error mtcp_sys_wait4: errno: %d", mtcp_sys_errno);
    }
    DPRINTF("checkpoint complete\n");
    return FORKED_CKPT_MASTER;
  } else {
    pid_t grandchild_pid = mtcp_sys_fork();
    if (grandchild_pid == -1) {
      MTCP_PRINTF("WARNING: Forked checkpoint failed,"
                  " no checkpoint available\n");
    } else if (grandchild_pid > 0) {
      mtcp_sys_exit(0); /* child exits */
    }
    /* grandchild continues; no need now to mtcp_sys_wait4() on grandchild */
    DPRINTF("inside grandchild process\n");
  }
  return FORKED_CKPT_WORKER;
}

void write_ckpt_to_file(int fd, int tmpDMTCPHeaderFd)
{
  Area area;
  int stack_was_seen = 0;
  static void *const frpointer = finishrestore;

  if (tmpDMTCPHeaderFd != -1 ) {
    char tmpBuff[1024];
    int retval = -1;
    lseek(tmpDMTCPHeaderFd, 0, SEEK_SET);

    while (retval != 0) {
      retval = read (tmpDMTCPHeaderFd, tmpBuff, 1024);
      if (retval == -1 && (errno == EAGAIN || errno == EINTR))
        continue;
      if (retval == -1) {
        MTCP_PRINTF("Error writing checkpoint file: %s\n", strerror(errno));
        mtcp_abort();
      }
      mtcp_writefile(fd, tmpBuff, retval);
    }
    close(tmpDMTCPHeaderFd);
  }

  /* Drain stdin and stdout before checkpoint */
  tcdrain(STDOUT_FILENO);
  tcdrain(STDERR_FILENO);

  int vsyscall_exists = 0;
  // Preprocess special segments like vsyscall, stack, heap etc.
  preprocess_special_segments(&vsyscall_exists);

  mtcp_writefile (fd, MAGIC, MAGIC_LEN);

  DPRINTF("restore_begin %X at %p from [libmtcp.so]\n",
          restore_size, restore_begin);

#ifdef FAST_CKPT_RST_VIA_MMAP
  fastckpt_prepare_for_ckpt(fd, restore_start, frpointer);
  fastckpt_save_restore_image(fd, restore_begin, restore_size);
#else
  struct rlimit stack_rlimit;
  getrlimit(RLIMIT_STACK, &stack_rlimit);

  DPRINTF("saved stack resource limit: soft_lim:%p, hard_lim:%p\n",
          stack_rlimit.rlim_cur, stack_rlimit.rlim_max);

  writecs (fd, CS_STACKRLIMIT);
  mtcp_writefile (fd, &stack_rlimit, sizeof stack_rlimit);

  writecs (fd, CS_RESTOREBEGIN);
  mtcp_writefile (fd, &restore_begin, sizeof restore_begin);
  writecs (fd, CS_RESTORESIZE);
  mtcp_writefile (fd, &restore_size, sizeof restore_size);
  writecs (fd, CS_RESTORESTART);
  mtcp_writefile (fd, &restore_start, sizeof restore_start);
  writecs (fd, CS_RESTOREIMAGE);
  mtcp_writefile (fd, restore_begin, restore_size);
  writecs (fd, CS_FINISHRESTORE);
  mtcp_writefile (fd, &frpointer, sizeof frpointer);

  /* Write out file descriptors */
  writefiledescrs (fd);
#endif

  /* Finally comes the memory contents */

  /**************************************************************************/
  /* We can't do any more mallocing at this point because malloc stuff is   */
  /* outside the limits of the libmtcp.so image, so it won't get            */
  /* checkpointed, and it's possible that we would checkpoint an            */
  /* inconsistent state.  See note in restoreverything routine.             */
  /**************************************************************************/

  int mapsfd = mtcp_sys_open2 ("/proc/self/maps", O_RDONLY);

  while (readmapsline (mapsfd, &area)) {
    VA area_begin = area.addr;
    VA area_end   = area_begin + area.size;

    /* Original comment:  Skip anything in kernel address space ---
     *   beats me what's at FFFFE000..FFFFFFFF - we can't even read it;
     * Added: That's the vdso section for earlier Linux 2.6 kernels.  For later
     *  2.6 kernels, vdso occurs at an earlier address.  If it's unreadable,
     *  then we simply won't copy it.  But let's try to read all areas, anyway.
     * **COMMENTED OUT:** if (area_begin >= HIGHEST_VA) continue;
     */
    /* If it's readable, but it's VDSO, it will be dangerous to restore it.
     * In 32-bit mode later Red Hat RHEL Linux 2.6.9 releases use 0xffffe000,
     * the last page of virtual memory.  Note 0xffffe000 >= HIGHEST_VA
     * implies we're in 32-bit mode.
     */
    if (area_begin >= HIGHEST_VA && area_begin == (VA)0xffffe000)
      continue;
#ifdef __x86_64__
    /* And in 64-bit mode later Red Hat RHEL Linux 2.6.9 releases
     * use 0xffffffffff600000 for VDSO.
     */
    if (area_begin >= HIGHEST_VA && area_begin == (VA)0xffffffffff600000)
      continue;
#endif

#ifdef FAST_CKPT_RST_VIA_MMAP
    /* We don't want to ckpt the mmap()'d area */
    if (area_begin == fastckpt_mmap_addr()) {
      continue;
    }
#endif

    /* Skip anything that has no read or execute permission.  This occurs
     * on one page in a Linux 2.6.9 installation.  No idea why.  This code
     * would also take care of kernel sections since we don't have read/execute
     * permission there.
     */

    if (!((area.prot & PROT_READ) || (area.prot & PROT_WRITE))) continue;

    // If the process has an area labelled as "/dev/zero (deleted)", we mark
    //   the area as Anonymous and save the contents to the ckpt image file.
    // IF this area has a MAP_SHARED attribute, it should be replaced with
    //   MAP_PRIVATE and we won't do any harm because, the /dev/zero file is an
    //   absolute source and sink. Anything written to it will be discarded and
    //   anything read from it will be all zeros.
    // The following call to mmap will create "/dev/zero (deleted)" area
    //         mmap(addr, size, protection, MAP_SHARED | MAP_ANONYMOUS, 0, 0)
    //
    // The above explanation also applies to "/dev/null (deleted)"

    if ( mtcp_strstartswith(area.name, dev_zero_deleted_str) ||
         mtcp_strstartswith(area.name, dev_null_deleted_str) ) {
      DPRINTF("saving area \"%s\" as Anonymous\n", area.name);
      area.flags = MAP_PRIVATE | MAP_ANONYMOUS;
      area.name[0] = '\0';
    }

    if (mtcp_strstartswith(area.name, sys_v_shmem_file)) {
      DPRINTF("saving area \"%s\" as Anonymous\n", area.name);
      area.flags = MAP_PRIVATE | MAP_ANONYMOUS;
      area.name[0] = '\0';
    }

    /* Special Case Handling: nscd is enabled*/
    if ( mtcp_strstartswith(area.name, nscd_mmap_str) ||
         mtcp_strstartswith(area.name, nscd_mmap_str2) ||
         mtcp_strstartswith(area.name, nscd_mmap_str3) ) {
      DPRINTF("NSCD daemon shared memory area present. MTCP will now try to\n"
              "  remap this area in read/write mode and then will fill it \n"
              "  with zeros so that glibc will automatically ask NSCD daemon\n"
              "  for new shared area\n\n");
      area.prot = PROT_READ | PROT_WRITE;
      area.flags = MAP_PRIVATE | MAP_ANONYMOUS;

      if ( munmap(area.addr, area.size) == -1) {
        MTCP_PRINTF("error unmapping NSCD shared area: %s\n",
                     strerror(mtcp_sys_errno));
        mtcp_abort();
      }

      if ( mmap(area.addr, area.size, area.prot, area.flags, 0, 0)
           == MAP_FAILED ){
        MTCP_PRINTF("error remapping NSCD shared area: %s\n", strerror(errno));
        mtcp_abort();
      }

      memset(area.addr, 0, area.size);
    }

    /* Force the anonymous flag if it's a private writeable section, as the
     * data has probably changed from the contents of the original images.
     */

    /* We also do this for read-only private sections as it's possible
     * to modify a page there, too (via mprotect).
     */

    if ((area.flags & MAP_PRIVATE) /*&& (area.prot & PROT_WRITE)*/) {
      area.flags |= MAP_ANONYMOUS;
    }

    if ( area.flags & MAP_SHARED ) {
      /* invalidate shared memory pages so that the next read to it (when we are
       * writing them to ckpt file) will cause them to be reloaded from the
       * disk.
       */
      if ( msync(area.addr, area.size, MS_INVALIDATE) < 0 ){
        MTCP_PRINTF("error %d Invalidating %X at %p from %s + %X\n",
                     mtcp_sys_errno, area.size,
                     area.addr, area.name, area.offset);
        mtcp_abort();
      }
    }


    /* Skip any mapping for this image - it got saved as CS_RESTOREIMAGE
     * at the beginning.
     */

    if (area_begin < restore_begin) {
      if (area_end <= restore_begin) {
        // the whole thing is before the restore image
        writememoryarea (fd, &area, 0, vsyscall_exists);
      } else if (area_end <= restore_end) {
        // we just have to chop the end part off
        area.size = restore_begin - area_begin;
        writememoryarea (fd, &area, 0, vsyscall_exists);
      } else {
        // we have to write stuff that comes before restore image
        area.size = restore_begin - area_begin;
        writememoryarea (fd, &area, 0, vsyscall_exists);
        // ... and we have to write stuff that comes after restore image
        area.offset += restore_end - area_begin;
        area.size = area_end - restore_end;
        area.addr = restore_end;
        writememoryarea (fd, &area, 0, vsyscall_exists);
      }
    } else if (area_begin < restore_end) {
      if (area_end > restore_end) {
        // we have to write stuff that comes after restore image
        area.offset += restore_end - area_begin;
        area.size = area_end - restore_end;
        area.addr = restore_end;
        writememoryarea (fd, &area, 0, vsyscall_exists);
      }
    } else {
      if ( strstr (area.name, "[stack]") )
        stack_was_seen = 1;
      // the whole thing comes after the restore image
      writememoryarea (fd, &area, stack_was_seen, vsyscall_exists);
    }
  }

  close (mapsfd);

  /* That's all folks */
#ifdef FAST_CKPT_RST_VIA_MMAP
  fastckpt_finish_ckpt(fd);
#else
  writecs (fd, CS_THEEND);
#endif

  if (mtcp_sys_close (fd) < 0) {
    MTCP_PRINTF("(grandchild): error closing checkpoint file: %s\n",
                strerror(errno));
    mtcp_abort ();
  }
}

/* True if the given FD should be checkpointed */
static int should_ckpt_fd (int fd)
{
   if ( callback_ckpt_fd!=NULL )
     return (*callback_ckpt_fd)(fd); //delegate to callback
   else if (fd > 2)
     return 1;
   else
   {
     /* stdin/stdout/stderr */
     /* we only want to checkpoint these if they are from a file */
     struct stat statbuf;
     fstat(fd, &statbuf);
     return S_ISREG(statbuf.st_mode);
   }
}

/* Write list of open files to the checkpoint file */
static void writefiledescrs (int fd)
{
  char dbuf[BUFSIZ], linkbuf[FILENAMESIZE], *p, procfdname[64];
  int doff, dsiz, fddir, fdnum, linklen, rc;
  off_t offset;
  struct linux_dirent *dent;
  struct stat lstatbuf, statbuf;

  writecs (fd, CS_FILEDESCRS);

  /* Open /proc/self/fd directory - it contains a list of files I have open */

  fddir = mtcp_sys_open ("/proc/self/fd", O_RDONLY, 0);
  if (fddir < 0) {
    MTCP_PRINTF("error opening directory /proc/self/fd: %s\n", MTCP_STR_ERRNO);
    mtcp_abort ();
  }

  /* Check each entry */

  while (1) {
    dsiz = -1;
    if (sizeof dent -> d_ino == 4)
      dsiz = mtcp_sys_getdents (fddir, dbuf, sizeof dbuf);
    if (sizeof dent -> d_ino == 8)
      dsiz = mtcp_sys_getdents64 (fddir, dbuf, sizeof dbuf);
    if (dsiz <= 0) break;

    for (doff = 0; doff < dsiz; doff += dent -> d_reclen) {
      dent = (struct linux_dirent *) (dbuf + doff);

      /* The filename should just be a decimal number = the fd it represents.
       * Also, skip the entry for the checkpoint and directory files
       * as we don't want the restore to know about them.
       */

      fdnum = strtol (dent -> d_name, &p, 10);
      if ((*p == '\0') && (fdnum >= 0) && (fdnum != fd) && (fdnum != fddir)
	  && (should_ckpt_fd (fdnum) > 0)) {

#ifdef FAST_CKPT_RST_VIA_MMAP
        MTCP_PRINTF("FAST ckpt restart not supported without DMTCP");
        mtcp_abort();
#endif

        // Read the symbolic link so we get the filename that's open on the fd
        sprintf (procfdname, "/proc/self/fd/%d", fdnum);
        linklen = readlink (procfdname, linkbuf, sizeof linkbuf - 1);
        if ((linklen >= 0) || (errno != ENOENT)) {
          // probably was the proc/self/fd directory itself
          if (linklen < 0) {
            MTCP_PRINTF("error reading %s: %s\n", procfdname, strerror(errno));
            mtcp_abort ();
          }
          linkbuf[linklen] = '\0';

          DPRINTF("checkpointing fd %d -> %s\n", fdnum, linkbuf);

          /* Read about the link itself so we know read/write open flags */

          rc = lstat (procfdname, &lstatbuf);
          if (rc < 0) {
            MTCP_PRINTF("error statting %s -> %s: %s\n",
	                 procfdname, linkbuf, strerror (-rc));
            mtcp_abort ();
          }

          /* Read about the actual file open on the fd */

          rc = stat (linkbuf, &statbuf);
          if (rc < 0) {
            MTCP_PRINTF("error statting %s -> %s: %s\n",
	                 procfdname, linkbuf, strerror (-rc));
          }

          /* Write state information to checkpoint file.
           * Replace file's permissions with current access flags
	   * so restore will know how to open it.
	   */

          else {
            offset = 0;
            if (S_ISREG (statbuf.st_mode))
	      offset = mtcp_sys_lseek (fdnum, 0, SEEK_CUR);
            statbuf.st_mode = (statbuf.st_mode & ~0777)
			       | (lstatbuf.st_mode & 0777);
            mtcp_writefile (fd, &fdnum, sizeof fdnum);
            mtcp_writefile (fd, &statbuf, sizeof statbuf);
            mtcp_writefile (fd, &offset, sizeof offset);
            mtcp_writefile (fd, &linklen, sizeof linklen);
            mtcp_writefile (fd, linkbuf, linklen);
          }
        }
      }
    }
  }
  if (dsiz < 0) {
    MTCP_PRINTF("error reading /proc/self/fd: %s\n", strerror(mtcp_sys_errno));
    mtcp_abort ();
  }

  mtcp_sys_close (fddir);

  /* Write end-of-fd-list marker to checkpoint file */

  fdnum = -1;
  mtcp_writefile (fd, &fdnum, sizeof fdnum);
}


static void writememoryarea (int fd, Area *area, int stack_was_seen,
			     int vsyscall_exists)
{
  static void * orig_stack = NULL;

  /* Write corresponding descriptor to the file */

  if (orig_stack == NULL && 0 == strcmp(area -> name, "[stack]"))
    orig_stack = area -> addr + area -> size;

  if (0 == strcmp(area -> name, "[vdso]") && !stack_was_seen)
    DPRINTF("skipping over [vdso] section"
            " %p at %p\n", area -> size, area -> addr);
  else if (0 == strcmp(area -> name, "[vsyscall]") && !stack_was_seen)
    DPRINTF("skipping over [vsyscall] section"
    	    " %p at %p\n", area -> size, area -> addr);
  else if (0 == strcmp(area -> name, "[stack]") &&
	   orig_stack != area -> addr + area -> size)
    /* Kernel won't let us munmap this.  But we don't need to restore it. */
    DPRINTF("skipping over [stack] segment"
            " %X at %pi (not the orig stack)\n", area -> size, area -> addr);
  else if (!(area -> flags & MAP_ANONYMOUS))
    DPRINTF("save %p at %p from %s + %X\n",
            area -> size, area -> addr, area -> name, area -> offset);
  else if (area -> name[0] == '\0')
    DPRINTF("save anonymous %p at %p\n", area -> size, area -> addr);
  else DPRINTF("save anonymous %p at %p from %s + %X\n",
               area -> size, area -> addr, area -> name, area -> offset);

  if ((area -> name[0]) == '\0') {
    char *brk = mtcp_sys_brk(NULL);
    if (brk > area -> addr && brk <= area -> addr + area -> size)
      mtcp_sys_strcpy(area -> name, "[heap]");
  }

  if ( 0 != strcmp(area -> name, "[vsyscall]")
       && ( (0 != strcmp(area -> name, "[vdso]")
             || vsyscall_exists /* which implies vdso can be overwritten */
             || !stack_was_seen ))) /* If vdso appeared before stack, it can be
                                       replaced */
  {
#ifndef FAST_CKPT_RST_VIA_MMAP
    writecs (fd, CS_AREADESCRIP);
    mtcp_writefile (fd, area, sizeof *area);
#endif

    /* Anonymous sections need to have their data copied to the file,
     *   as there is no file that contains their data
     * We also save shared files to checkpoint file to handle shared memory
     *   implemented with backing files
     */
    if (area -> flags & MAP_ANONYMOUS || area -> flags & MAP_SHARED) {
#ifdef FAST_CKPT_RST_VIA_MMAP
      fastckpt_write_mem_region(fd, area);
#else
      writecs (fd, CS_AREACONTENTS);
      mtcp_writefile (fd, area -> addr, area -> size);
#endif
    } else {
      MTCP_PRINTF("UnImplemented");
      mtcp_abort();
    }
  }
}

/* Write checkpoint section number to checkpoint file */
static void writecs (int fd, char cs)
{
  mtcp_writefile (fd, &cs, sizeof cs);
}

static void preprocess_special_segments(int *vsyscall_exists)
{
  Area area;
  int mapsfd = mtcp_sys_open2 ("/proc/self/maps", O_RDONLY);
  if (mapsfd < 0) {
    MTCP_PRINTF("error opening /proc/self/maps: %s\n",
                strerror(mtcp_sys_errno));
    mtcp_abort ();
  }

  while (readmapsline (mapsfd, &area)) {
    if (0 == strcmp(area.name, "[vsyscall]")) {
      /* Determine if [vsyscall] exists.  If [vdso] and [vsyscall] exist,
       * [vdso] will be saved and restored.
       * NOTE:  [vdso] is relocated if /proc/sys/kernel/randomize_va_space == 2.
       * We must restore old [vdso] and also keep [vdso] in that case.
       * On Linux 2.6.25:
       *   32-bit Linux has:  [heap], /lib/ld-2.7.so, [vdso], libs, [stack].
       *   64-bit Linux has:  [stack], [vdso], [vsyscall].
       * and at least for gcl, [stack], libmtcp.so, [vsyscall] seen.
       * If 32-bit process in 64-bit Linux:
       *     [stack] (0xffffd000), [vdso] (0xffffe0000)
       * On 32-bit Linux, mtcp_restart has [vdso], /lib/ld-2.7.so, [stack]
       * Need to restore old [vdso] into mtcp_restart, to restart.
       * With randomize_va_space turned off, libraries start at high address
       *     0xb8000000 and are loaded progressively at lower addresses.
       * mtcp_restart loads vdso (which looks like a shared library) first.
       * But libpthread/libdl/libc libraries are loaded above vdso in user
       * image.
       * So, we must use the opposite of the user's setting (no randomization if
       *     user turned it on, and vice versa).  We must also keep the
       *     new vdso segment, provided by mtcp_restart.
       */
      *vsyscall_exists = 1;
    } else if (!saved_heap_start && strcmp(area.name, "[heap]") == 0) {
      // Record start of heap which will later be used in finishrestore()
      saved_heap_start = area.addr;
    } else if (strcmp(area.name, "[stack]") == 0) {
      /*
       * When using Matlab with dmtcp_checkpoint, sometimes the bottom most
       * page of stack (the page with highest address) which contains the
       * environment strings and the argv[] was not shown in /proc/self/maps.
       * This is arguably a bug in the Linux kernel as of version 2.6.32, etc.
       * This happens on some odd combination of environment passed on to
       * Matlab process. As a result, the page was not checkpointed and hence
       * the process segfaulted on restart. The fix is to try to mprotect this
       * page with RWX permission to make the page visible again. This call
       * will fail if no stack page was invisible to begin with.
       */
      int ret = mprotect(area.addr + area.size, 0x1000, 
                         PROT_READ | PROT_WRITE | PROT_EXEC);
      if (ret == 0) {
        MTCP_PRINTF("bottom-most page of stack (page with highest address)\n"
                    "  was invisible in /proc/self/maps.\n"
                    "  It is made visible again now.\n");
      }
    }
  }
  close(mapsfd);
}

/*****************************************************************************
 *
 *  This signal handler is forced by the main thread doing a
 *  'mtcp_sys_kernel_tkill' to stop these threads so it can do a
 *  checkpoint
 *
 * Grow the stack by kbStack*1024 so that large stack is allocated on restart
 * The kernel won't do it automatically for us any more, since it thinks
 * the stack is in a different place after restart.
 * 
 * growstackValue is volatile so compiler doesn't optimize away growstack
 * Maybe it's not needed if we use ((optimize(0))) .
 *****************************************************************************/
static volatile unsigned int growstackValue = 0;
static void __attribute__ ((optimize(0))) growstack (int kbStack) {
  const int kBincrement = 1024;
  char array[kBincrement * 1024] __attribute__ ((unused));
  /* Again, try to prevent compiler optimization */
  volatile int dummy_value __attribute__ ((unused)) = 1;
  if (kbStack > 0)
    growstack(kbStack - kBincrement);
  else
    growstackValue++;
}

static void stopthisthread (int signum)

{
  int rc;
  Thread *thread;
#define BT_SIZE 1024
#define STDERR_FD 826
#define LOG_FD 826

  DPRINTF("tid %d returns to %p\n",
          mtcp_sys_kernel_gettid (), __builtin_return_address (0));

  thread = getcurrenthread (); // see which thread this is

  // If this is checkpoint thread - exit immidiately
  if ( mtcp_state_value(&thread -> state) == ST_CKPNTHREAD ) {
    return ;
  }

#if 0
  if (0 && thread == motherofall) {
#include <execinfo.h>
    void *buffer[BT_SIZE];
    int nptrs;

    DPRINTF("printing stacktrace of the motherofall Thread\n\n");
    nptrs = backtrace (buffer, BT_SIZE);
    backtrace_symbols_fd ( buffer, nptrs, STDERR_FD );
    backtrace_symbols_fd ( buffer, nptrs, LOG_FD );
  }
#endif
  if (mtcp_state_set (&(thread -> state), ST_SUSPINPROG, ST_SIGENABLED)) {
    // make sure we don't get called twice for same thread
    static int is_first_checkpoint = 1;

    save_sig_state (thread);   // save signal state (and block signal delivery)
    save_tls_state (thread);   // save thread local storage state

    /* Grow stack only on first ckpt.  Kernel agrees this is main stack and
     * will mmap it.  On second ckpt and later, we would segfault if we tried
     * to grow the former stack beyond the portion that is already mmap'ed.
     */
    if (thread == motherofall) {
      static char *orig_stack_ptr;
      int kbStack = 2048;
      if (is_first_checkpoint) {
	orig_stack_ptr = (char *)&kbStack;
        is_first_checkpoint = 0;
        DPRINTF("temp. grow main stack by %d kilobytes\n", kbStack);
        growstack(kbStack);
      } else if (orig_stack_ptr - (char *)&kbStack > 3 * kbStack*1024 / 4) {
        MTCP_PRINTF("WARNING:  Stack within %d bytes of end;\n"
		    "  Consider increasing 'kbStack' at line %d of mtcp/%s\n",
		    kbStack*1024/4, __LINE__-9, __FILE__);
      }
    }

    ///JA: new code ported from v54b
    rc = getcontext (&(thread -> savctx));
    if (rc < 0) {
      MTCP_PRINTF("getcontext rc %d errno %d\n", rc, strerror(mtcp_sys_errno));
      mtcp_abort ();
    }
    DPRINTF("after getcontext\n");
    if (mtcp_state_value(&restoreinprog) == 0) {

      /* We are the original process and all context is saved
       * restoreinprog is 0 ; wait for ckpt thread to write ckpt, and resume.
       */

      WMB; // matched by RMB in checkpointhread

      /* Next comes the first time we use the old stack. */
      /* Tell the checkpoint thread that we're all saved away */
      if (!mtcp_state_set (&(thread -> state), ST_SUSPENDED, ST_SUSPINPROG))
	mtcp_abort ();  // tell checkpointhread all our context is saved

      // wake checkpoint thread if it's waiting for me
      mtcp_state_futex (&(thread -> state), FUTEX_WAKE, 1, NULL);
#ifdef PTRACE
  int cont = 1;
  if (ptracing()) {

    /* Wait for JALIB_CKPT_UNLOCK to have been called. */
    while (cont) {
      pthread_mutex_lock(&jalib_ckpt_unlock_lock);
      if (jalib_ckpt_unlock_ready) cont = 0;
      pthread_mutex_unlock(&jalib_ckpt_unlock_lock);
      struct timespec ts;
      ts.tv_sec = 0;
      ts.tv_nsec = 100000000;
      nanosleep(&ts, NULL);
    }

    /* Wait for new_ptrace_shared_file to have been written, if there is one. */
    if (has_new_ptrace_shared_file) {
      struct stat buf;
      while (stat(new_ptrace_shared_file, &buf)) {
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 100000000;
        if (errno != ENOENT) {
          MTCP_PRINTF("Unexpected error in stat: %s\n",
                      strerror(mtcp_sys_errno));
          mtcp_abort();
        }
        nanosleep(&ts,NULL);
      }
    }

    motherofall_done_reading = 0;

    /* Motherofall reads in new_ptrace_shared_file and
     * checkpoint_threads_file. */
    if (getpid() == GETTID()) {
      read_new_ptrace_shared_file();
      read_checkpoint_threads_file();
      mtcp_ptrace_info_list_remove_pairs_with_dead_tids();
      mtcp_ptrace_info_list_sort();
      pthread_mutex_lock(&motherofall_done_reading_lock);
      motherofall_done_reading = 1;
      pthread_mutex_unlock(&motherofall_done_reading_lock);
    }

    /* Wait for motherofall to have read in the ptrace related files. */
    cont = 1;
    while (cont) {
      pthread_mutex_lock(&motherofall_done_reading_lock);
      if (motherofall_done_reading) cont = 0;
      pthread_mutex_unlock(&motherofall_done_reading_lock);
      struct timespec ts;
      ts.tv_sec = 0;
      ts.tv_nsec = 100000000;
      nanosleep(&ts, NULL);
    }
  }

  /* Save to detach - we have all the information in memory. */
  ptrace_unlock_inferiors();
  ptrace_detach_checkpoint_threads();
  ptrace_detach_user_threads();

  if (ptracing()) {
    pthread_mutex_lock(&nthreads_lock);
    nthreads--;
    pthread_mutex_unlock(&nthreads_lock);

    cont = 1;
    while(cont) {
      pthread_mutex_lock(&proceed_to_checkpoint_lock);
      if (proceed_to_checkpoint) cont = 0;
      pthread_mutex_unlock(&proceed_to_checkpoint_lock);
      struct timespec ts;
      ts.tv_sec = 0;
      ts.tv_nsec = 100000000;
      nanosleep(&ts, NULL);
    }
  }
#endif

      /* Then we wait for the checkpoint thread to write the checkpoint file
       * then wake us up
       */

      DPRINTF("thread %d suspending\n", thread -> tid);
      while (mtcp_state_value(&thread -> state) == ST_SUSPENDED) {
        mtcp_state_futex (&(thread -> state), FUTEX_WAIT, ST_SUSPENDED, NULL);
      }

#ifdef PTRACE
      ptrace_attach_threads(0);
      /* All user threads will try to delete these files. It doesn't matter if
       * unlink fails. The sole purpose of these unlinks is to prevent these
       * files to become too big. Big files imply an increased processing time
       * of the ptrace pairs. */
      unlink(ptrace_shared_file);
      unlink(ptrace_setoptions_file);
      unlink(checkpoint_threads_file);
      unlink(new_ptrace_shared_file);
      unlink(ckpt_leader_file);
#endif

      /* Maybe there is to be a checkpoint verification.  If so, and we're the
       * main thread, exec the restore program.  If so and we're not the main
       * thread, exit.
       */

      if ((verify_total != 0) && (verify_count == 0)) {

        /* If not the main thread, exit.  Either normal exit() or _exit()
         * seems to cause other threads to exit.
         */

        if (thread != motherofall) {
          mtcp_sys_exit(0);
        }

        /* This is the main thread, verify checkpoint then restart by doing
         * a restart.
         * The restore will rename the file after it has done the restart.
         */

        DPRINTF("verifying checkpoint...\n");
        execlp ("mtcp_restart", "mtcp_restart", "--verify",
                temp_checkpointfilename, NULL);
        MTCP_PRINTF("error execing mtcp_restart %s: %s\n",
                    temp_checkpointfilename, strerror(errno));
        mtcp_abort ();
      }

      /* No verification, resume where we left off */

      DPRINTF("thread %d resuming\n", thread -> tid);
    }

    /* Else restoreinprog >= 1;  This stuff executes to do a restart */

    else {
      if (!mtcp_state_set (&(thread -> state), ST_RUNENABLED, ST_SUSPENDED))
	mtcp_abort ();  // checkpoint was written when thread in SUSPENDED state
      wait_for_all_restored ();
      DPRINTF("thread %d restored\n", thread -> tid);

      if (thread == motherofall) {

        /* If we're a restore verification, rename the temp file
	 * over the permanent one
	 */

        if (mtcp_restore_verify) renametempoverperm ();
      }

#ifdef PTRACE
      ptrace_attach_threads(1);
#endif 
    }
  }
  DPRINTF("tid %d returning to %p\n",
          mtcp_sys_kernel_gettid (), __builtin_return_address (0));
#ifdef PTRACE
  ptrace_lock_inferiors();
#endif
}

/*****************************************************************************
 *
 *  Wait for all threads to finish restoring their context, then release them
 *  all to continue on their way.
 *
 *    Input:
 *
 *	restoreinprog = number of threads, including this, that hasn't called
 *                      'wait_for_all_restored' yet
 *      thread list locked
 *
 *    Output:
 *
 *	restoreinprog = decremented
 *	                if now zero, all threads woken and thread list unlocked
 *
 *****************************************************************************/

static void wait_for_all_restored (void)

{
  int rip;

  // dec number of threads cloned but not completed longjmp'ing
  do {
    rip = mtcp_state_value(&restoreinprog);
  } while (!mtcp_state_set (&restoreinprog, rip - 1, rip));
  if (-- rip == 0) {

    /* raise the signals which were pending for the entire process at the time
     * of checkpoint. It is assumed that if a signal is pending for all threads
     * including the ckpt-thread, then it was sent to the process as opposed to
     * sent to individual threads.
     */
    int i;
    for (i = SIGRTMAX; i > 0; --i) {
      if (sigismember(&sigpending_global, i) == 1) {
        kill(getpid(), i);
      }
    }

    if (callback_restore_virtual_pid_table != NULL) {
      DPRINTF("Before callback_restore_virtual_pid_table: Thread:%d \n", 
              mtcp_sys_kernel_gettid());
      (*callback_restore_virtual_pid_table)();
      DPRINTF("After callback_restore_virtual_pid_table: Thread:%d \n",
              mtcp_sys_kernel_gettid());
    }

    // if this was last of all, wake everyone up
    mtcp_state_futex (&restoreinprog, FUTEX_WAKE, 999999999, NULL);

    // NOTE:  This is last safe moment for hook.  All previous threads
    //   have executed the "else" and are waiting on the futex.
    //   This last thread has not yet unlocked the threads: unlk_threads()
    //   So, no race condition occurs.
    //   By comparison, *callback_post_ckpt() is called before creating
    //   additional user threads.  Only motherofall (checkpoint thread existed)
    /* call weak symbol of this file, possibly overridden by the user's strong
     * symbol user must compile his/her code with -Wl,-export-dynamic to make it
     * visible
     */
    mtcpHookRestart();
    unlk_threads (); // ... and release the thread list
  } else {
    // otherwise, wait for last of all to wake this one up
    while ((rip = mtcp_state_value(&restoreinprog)) > 0) {
      mtcp_state_futex (&restoreinprog, FUTEX_WAIT, rip, NULL);
    }
  }
}

/*****************************************************************************
 *
 *  Save signal mask and list of pending signals delivery
 *
 *****************************************************************************/

static void save_sig_state (Thread *thisthread)
{
  /* For checkpoint thread, we want to block delivery of all but some special
   * signals
   */
  if (thisthread == ckpthread) {
    /* 
     * For the checkpoint thread, we should not block SIGSETXID which is used
     * by the setsid family of system calls to change the session leader. Glibc
     * uses this signal to notify the process threads of the change in session
     * leader information. This signal is not documented and is used internally
     * by glibc. It is defined in <glibc-src-root>/nptl/pthreadP.h
     * screen was getting affected by this since it used setsid to change the
     * session leaders.
     * Similarly, SIGCANCEL/SIGTIMER is undocumented, but used by glibc.
     */
#define SIGSETXID (__SIGRTMIN + 1)
#define SIGCANCEL (__SIGRTMIN) /* aka SIGTIMER */
    sigset_t set;

    sigfillset(&set);
    sigdelset(&set, SIGSETXID);
    sigdelset(&set, SIGCANCEL);

    if (pthread_sigmask(SIG_SETMASK, &set, NULL) < 0) {
      MTCP_PRINTF("error getting sigal mask: %s\n", strerror(errno));
      mtcp_abort ();
    }
  }
  // Save signal block mask
  if (pthread_sigmask (SIG_SETMASK, NULL, &(thisthread -> sigblockmask)) < 0) {
    MTCP_PRINTF("error getting sigal mask: %s\n", strerror(errno));
    mtcp_abort ();
  }

  // Save pending signals
  sigpending ( &(thisthread->sigpending) );
}

/*****************************************************************************
 *
 *  Restore signal mask and all pending signals
 *
 *****************************************************************************/

static void restore_sig_state (Thread *thisthread)
{
  int i;
  DPRINTF("restoring handlers for thread %d\n", thisthread->original_tid);
  if (pthread_sigmask (SIG_SETMASK, &(thisthread -> sigblockmask), NULL) < 0) {
    MTCP_PRINTF("error setting sigal mask: %s\n", strerror(errno));
    mtcp_abort ();
  }

  // Raise the signals which were pending for only this thread at the time of
  // checkpoint.
  for (i = SIGRTMAX; i > 0; --i) {
    if (sigismember(&(thisthread -> sigpending), i)  == 1  &&
        sigismember(&(thisthread -> sigblockmask), i) == 1 &&
        sigismember(&(sigpending_global), i) == 0) {
      raise(i);
    }
  }
}

/*****************************************************************************
 *
 *  Save all signal handlers
 *
 *****************************************************************************/
static void save_sig_handlers (void)
{
  int i;
  /* Now save all the signal handlers */
  DPRINTF("saving signal handlers\n");
  for (i = SIGRTMAX; i > 0; --i) {
    if (mtcp_sigaction (i, NULL, &sigactions[i]) < 0) {
      if (errno == EINVAL)
         memset (&sigactions[i], 0, sizeof sigactions[i]);
      else {
        MTCP_PRINTF("error saving signal %d action: %s\n", i, strerror(errno));
        mtcp_abort ();
      }
    }

    DPRINTF("saving signal handler for %d -> %p\n",
            i, (sigactions[i].sa_flags & SA_SIGINFO ?
                (void *)(sigactions[i].sa_sigaction) :
                (void *)(sigactions[i].sa_handler)));
  }
}

/*****************************************************************************
 *
 *  Restore all saved signal handlers
 *
 *****************************************************************************/
static void restore_sig_handlers (Thread *thisthread)
{
  int i;
  DPRINTF("restoring signal handlers\n");
#if 0
# define VERBOSE_DEBUG
#endif
  for(i = SIGRTMAX; i > 0; --i) {
#ifdef VERBOSE_DEBUG
    DPRINTF("restore signal handler for %d -> %p\n",
            i, (sigactions[i].sa_flags & SA_SIGINFO ?
                sigactions[i].sa_sigaction :
                sigactions[i].sa_handler));
#endif

    if (mtcp_sigaction(i, &sigactions[i], NULL) < 0) {
        if (errno != EINVAL) {
          MTCP_PRINTF("error restoring signal %d handler: %s\n",
		      i, strerror(errno));
          mtcp_abort ();
        }
    }
  }
}

/*****************************************************************************
 *
 *  Save state necessary for TLS restore
 *  Linux saves stuff in the GDT, switching it on a per-thread basis
 *
 *****************************************************************************/

static void save_tls_state (Thread *thisthread)
{
  int i, rc;

#ifdef __i386__
  asm volatile ("movw %%fs,%0" : "=m" (thisthread -> fs));
  asm volatile ("movw %%gs,%0" : "=m" (thisthread -> gs));
#endif
#ifdef __x86_64__
  //asm volatile ("movl %%fs,%0" : "=m" (thisthread -> fs));
  //asm volatile ("movl %%gs,%0" : "=m" (thisthread -> gs));
#endif

  memset (thisthread -> gdtentrytls, 0, sizeof thisthread -> gdtentrytls);

  /* On older Linuxes, we must save several GDT entries available to threads. */

#if MTCP__SAVE_MANY_GDT_ENTRIES
  for (i = GDT_ENTRY_TLS_MIN; i <= GDT_ENTRY_TLS_MAX; i ++) {
    thisthread -> gdtentrytls[i-GDT_ENTRY_TLS_MIN].entry_number = i;
    int offset = i - GDT_ENTRY_TLS_MIN;
    rc = mtcp_sys_get_thread_area(&(thisthread -> gdtentrytls[offset]));
    if (rc < 0) {
      MTCP_PRINTF("error saving GDT TLS entry[%d]: %s\n",
                  i, strerror (mtcp_sys_errno));
      mtcp_abort ();
    }
  }

  /* With newer Linuxes, we just save the one GDT entry indexed by GS so we
   * don't need the GDT_ENTRY_TLS_... definitions. 
   * We get the particular index of the GDT entry to save by reading GS.
   */

#else
  i = thisthread -> TLSSEGREG / 8;
  thisthread -> gdtentrytls[0].entry_number = i;
  rc = mtcp_sys_get_thread_area (&(thisthread -> gdtentrytls[0]));
  if (rc < 0) {
    MTCP_PRINTF("error saving GDT TLS entry[%d]: %s\n",
                i, strerror (mtcp_sys_errno));
    mtcp_abort ();
  }
#endif
}

static char *memsubarray (char *array, char *subarray, int len) {
   char *i_ptr;
   int j;
   int word1 = *(int *)subarray;
   // Assume subarray length is at least sizeof(int) and < 2048.
   if (len < sizeof(int))
     mtcp_abort();
   for (i_ptr = array; i_ptr < array+2048; i_ptr++) {
     if (*(int *)i_ptr == word1) {
       for (j=0; j < len; j++)
	 if (i_ptr[j] != subarray[j])
	   break;
	if (j == len)
	  return i_ptr;
     }
   }
   return NULL;
}
static int mtcp_get_tls_segreg(void)
{ mtcp_segreg_t tlssegreg;
#ifdef __i386__
  asm volatile ("movw %%gs,%0" : "=g" (tlssegreg)); /* any general register */
#endif
#ifdef __x86_64__
  /* q = a,b,c,d for i386; 8 low bits of r class reg for x86_64 */
  asm volatile ("movl %%fs,%0" : "=q" (tlssegreg));
#endif
  return (int)tlssegreg;
}
static void *mtcp_get_tls_base_addr(void)
{
  struct user_desc gdtentrytls;

#if MTCP__SAVE_MANY_GDT_ENTRIES
  if (mtcp_get_tls_segreg() / 8 != GDT_ENTRY_TLS_MIN) {
    MTCP_PRINTF("gs %X not set to first TLS GDT ENTRY %X\n",
                 gs, GDT_ENTRY_TLS_MIN * 8 + 3);
    mtcp_abort ();
  }
#endif

  gdtentrytls.entry_number = mtcp_get_tls_segreg() / 8;
  if ( mtcp_sys_get_thread_area ( &gdtentrytls ) < 0 ) {
    MTCP_PRINTF("error getting GDT TLS entry: %s\n", strerror(mtcp_sys_errno));
    mtcp_abort ();
  }
  return (void *)(*(unsigned long *)&(gdtentrytls.base_addr));
}

static void renametempoverperm (void)

{
  if (rename (temp_checkpointfilename, perm_checkpointfilename) < 0) {
    MTCP_PRINTF("error renaming %s to %s: %s\n",
                temp_checkpointfilename, perm_checkpointfilename,
                strerror(errno));
    mtcp_abort ();
  }
}

/*****************************************************************************
 *
 *  Get current thread struct pointer
 *  It is keyed by the calling thread's gettid value
 *  Maybe improve someday by using TLS
 *
 *****************************************************************************/

static Thread *getcurrenthread (void)

{
  int tid;
  Thread *thread;

  tid = mtcp_sys_kernel_gettid ();
  lock_threads ();
  for (thread = threads; thread != NULL; thread = thread -> next) {
    if (thread -> tid == tid) {
      unlk_threads ();
      return (thread);
    }
  }
  MTCP_PRINTF("can't find thread id %d\n", tid);
  mtcp_abort ();
  return thread; /* NOTREACHED : stop compiler warning */
}

/*****************************************************************************
 *
 *  Lock and unlock the 'threads' list
 *
 *****************************************************************************/

static void lock_threads (void)

{
  while (!mtcp_state_set (&threadslocked, 1, 0)) {
    mtcp_state_futex (&threadslocked, FUTEX_WAIT, 1, NULL);
  }
  RMB; // don't prefetch anything until we have the lock
}

static void unlk_threads (void)

{
  WMB; // flush data written before unlocking
  // FIXME: Should we be checking return value of mtcp_state_set? Can it ever
  //        fail?
  mtcp_state_set(&threadslocked , 0, 1);
  mtcp_state_futex (&threadslocked, FUTEX_WAKE, 1, NULL);
}

/*****************************************************************************
 *
 *  Read /proc/self/maps line, converting it to an Area descriptor struct
 *
 *    Input:
 *
 *	mapsfd = /proc/self/maps file, positioned to beginning of a line
 *
 *    Output:
 *
 *	readmapsline = 0 : was at end-of-file, nothing read
 *	               1 : read and processed one line
 *	*area = filled in
 *
 *    Note:
 *
 *	Line from /procs/self/maps is in form:
 *
 *	<startaddr>-<endaddrexclusive> rwxs <fileoffset> <devmaj>:<devmin>
 *	    <inode>    <filename>\n
 *	all numbers in hexadecimal except inode is in decimal
 *	anonymous will be shown with offset=devmaj=devmin=inode=0 and
 *	    no '     filename'
 *
 *****************************************************************************/

int readmapsline (int mapsfd, Area *area)

{
  char c, rflag, sflag, wflag, xflag;
  int i, rc;
  struct stat statbuf;
  unsigned int long devmajor, devminor, devnum, inodenum;
  VA startaddr, endaddr;

  c = mtcp_readhex (mapsfd, &startaddr);
  if (c != '-') {
    if ((c == 0) && (startaddr == 0)) return (0);
    goto skipeol;
  }
  c = mtcp_readhex (mapsfd, &endaddr);
  if (c != ' ') goto skipeol;
  if (endaddr < startaddr) goto skipeol;

  rflag = c = mtcp_readchar (mapsfd);
  if ((c != 'r') && (c != '-')) goto skipeol;
  wflag = c = mtcp_readchar (mapsfd);
  if ((c != 'w') && (c != '-')) goto skipeol;
  xflag = c = mtcp_readchar (mapsfd);
  if ((c != 'x') && (c != '-')) goto skipeol;
  sflag = c = mtcp_readchar (mapsfd);
  if ((c != 's') && (c != 'p')) goto skipeol;

  c = mtcp_readchar (mapsfd);
  if (c != ' ') goto skipeol;

  c = mtcp_readhex (mapsfd, (VA *)&devmajor);
  if (c != ' ') goto skipeol;
  area -> offset = (off_t)devmajor;

  c = mtcp_readhex (mapsfd, (VA *)&devmajor);
  if (c != ':') goto skipeol;
  c = mtcp_readhex (mapsfd, (VA *)&devminor);
  if (c != ' ') goto skipeol;
  c = mtcp_readdec (mapsfd, (VA *)&inodenum);
  area -> name[0] = '\0';
  while (c == ' ') c = mtcp_readchar (mapsfd);
  if (c == '/' || c == '[') { /* absolute pathname, or [stack], [vdso], etc. */
    i = 0;
    do {
      area -> name[i++] = c;
      if (i == sizeof area -> name) goto skipeol;
      c = mtcp_readchar (mapsfd);
    } while (c != '\n');
    area -> name[i] = '\0';
  }
  if (mtcp_strstartswith(area -> name, nscd_mmap_str)  ||
      mtcp_strstartswith(area -> name, nscd_mmap_str2) ||
      mtcp_strstartswith(area -> name, nscd_mmap_str3)) {
    /* if nscd is active */
  } else if ( mtcp_strstartswith(area -> name, sys_v_shmem_file) ) {
    /* System V Shared-Memory segments are handled by DMTCP. */
  } else if ( mtcp_strendswith(area -> name, DELETED_FILE_SUFFIX) ) {
    /* Deleted File */
  } else if (area -> name[0] == '/') {  /* if an absolute pathname */
    rc = stat (area -> name, &statbuf);
    if (rc < 0) {
      MTCP_PRINTF("ERROR: error %d statting %s\n", -rc, area -> name);
      return (1); /* 0 would mean last line of maps; could do mtcp_abort() */
    }
    devnum = makedev (devmajor, devminor);
    if ((devnum != statbuf.st_dev) || (inodenum != statbuf.st_ino)) {
      MTCP_PRINTF("ERROR: image %s dev:inode %X:%u not eq maps %X:%u\n",
                   area -> name, statbuf.st_dev, statbuf.st_ino,
		   devnum, inodenum);
      return (1); /* 0 would mean last line of maps; could do mtcp_abort() */
    }
  } else {
    /* Special area like [heap] or anonymous area. */
  }

  if (c != '\n') goto skipeol;

  area -> addr = startaddr;
  area -> size = endaddr - startaddr;
  area -> prot = 0;
  if (rflag == 'r') area -> prot |= PROT_READ;
  if (wflag == 'w') area -> prot |= PROT_WRITE;
  if (xflag == 'x') area -> prot |= PROT_EXEC;
  area -> flags = MAP_FIXED;
  if (sflag == 's') area -> flags |= MAP_SHARED;
  if (sflag == 'p') area -> flags |= MAP_PRIVATE;
  if (area -> name[0] == '\0') area -> flags |= MAP_ANONYMOUS;

  return (1);

skipeol:
  DPRINTF("ERROR:  mtcp readmapsline*: bad maps line <%c", c);
  while ((c != '\n') && (c != '\0')) {
    c = mtcp_readchar (mapsfd);
    mtcp_printf ("%c", c);
  }
  mtcp_printf (">\n");
  mtcp_abort ();
  return (0);  /* NOTREACHED : stop compiler warning */
}

/*****************************************************************************
 *
 *  Do restore from checkpoint file
 *  This routine is called from the mtcp_restart program to perform the restore
 *  It resides in the libmtcp.so image in exactly the same spot that the
 *  checkpointed process had its libmtcp.so loaded at, so this can't possibly
 *  interfere with restoring the checkpointed process
 *
 *  The restore can't use malloc because that might create memory sections.
 *  Strerror seems to mess up with its Locale stuff in here too.
 *
 *    Input:
 *
 *	fd = checkpoint file, positioned just after the CS_RESTOREIMAGE data
 *
 *****************************************************************************/

#ifdef __x86_64__
# define UNUSED_IN_64_BIT __attribute__ ((unused))
#else
# define UNUSED_IN_64_BIT
#endif

#define STRINGS_LEN 10000
static char UNUSED_IN_64_BIT STRINGS[STRINGS_LEN];
void mtcp_restore_start (int fd, int verify, pid_t gzip_child_pid,
                         char *ckpt_newname, char *cmd_file,
                         char *argv[], char *envp[] )
{
#ifndef __x86_64__
  int i;
  char *strings = STRINGS;
#endif

  DEBUG_RESTARTING = 1;
  /* If we just replace extendedStack by (tempstack+STACKSIZE) in "asm"
   * below, the optimizer generates non-PIC code if it's not -O0 - Gene
   */
  long long * extendedStack = tempstack + STACKSIZE;

  /* Not used until we do longjmps, but get it out of the way now */

  // FIXME: Should we be checking return value of mtcp_state_set? Can it ever
  // fail?
  mtcp_state_set(&restoreinprog ,1, 0);

  mtcp_sys_gettimeofday (&restorestarted, NULL);

  /* Save parameter away in a static memory location as we're about to wipe the
   * stack
   */

  mtcp_restore_cpfd   = fd;
  mtcp_restore_verify = verify;
  mtcp_restore_gzip_child_pid = gzip_child_pid;
  // Copy newname to save it too
  {
    int i;
    for(i=0;ckpt_newname[i];i++){
      mtcp_ckpt_newname[i] = ckpt_newname[i];
    }
    mtcp_ckpt_newname[i] = '\0';
  }


#ifndef __x86_64__
  // Copy command line to libmtcp.so, so that we can re-exec if randomized vdso
  //   steps on us.  This won't be needed when we use the linker to map areas.
  strings = STRINGS;
  // This version of STRCPY copies source string into STRINGS,
  // and sets destination string to point there.
# define STRCPY(x,y) \
	if (strings + 256 < STRINGS + STRINGS_LEN) { \
	  mtcp_sys_strcpy(strings,y); \
	  x = strings; \
	  strings += mtcp_sys_strlen(y) + 1; \
	} else { \
	  DPRINTF("ran out of string space. Trying to continue anyway\n"); \
	}
  STRCPY(mtcp_restore_cmd_file, cmd_file);
  for (i = 0; argv[i] != NULL; i++) {
    STRCPY(mtcp_restore_argv[i], argv[i]);
  }
  mtcp_restore_argv[i] = NULL;
  for (i = 0; envp[i] != NULL; i++) {
    STRCPY(mtcp_restore_envp[i], envp[i]);
  }
  mtcp_restore_envp[i] = NULL;
#endif
  mtcp_restore_argv_start_addr = argv[0];

  /* Switch to a stack area that's part of the shareable's memory address range
   * and thus not used by the checkpointed program
   */

  asm volatile (CLEAN_FOR_64_BIT(mov %0,%%esp\n\t)
                /* This next assembly language confuses gdb,
		   but seems to work fine anyway */
                CLEAN_FOR_64_BIT(xor %%ebp,%%ebp\n\t)
                : : "g" (extendedStack) : "memory");

  /* Once we're on the new stack, we can't access any local variables or
   * parameters.
   * Call the restoreverything to restore files and memory areas
   */

  /* This should never return */
  mtcp_restoreverything();
  asm volatile ("hlt");
}


/*****************************************************************************
 *
 *  Restore proper heap
 *
 *****************************************************************************/
static void restore_heap()
{
  /*
   * If the original start of heap is lower than the current end of heap, we
   * want to mmap the area between mtcp_saved_break and current break. This
   * happens when the size of checkpointed program is smaller then the size of
   * mtcp_restart program.
   */
  VA current_break = mtcp_sys_brk (NULL);
  if (current_break > mtcp_saved_break) {
    DPRINTF("Area between mtcp_saved_break:%p and "
            "Current_break:%p not mapped, mapping it now\n", 
            mtcp_saved_break, current_break);
    size_t oldsize = mtcp_saved_break - saved_heap_start;
    size_t newsize = current_break - saved_heap_start;

    void* addr = mtcp_sys_mremap (saved_heap_start, oldsize, newsize, 0);
    if (addr == NULL) {
      MTCP_PRINTF("mremap failed to map area between "
                  "mtcp_saved_break (%p) and current_break (%p)\n",
                  mtcp_saved_break, current_break);
      mtcp_abort();
    }
  }
}

/*****************************************************************************
 *
 *  The original program's memory and files have been restored
 *
 *****************************************************************************/

static void finishrestore (void)
{
  struct timeval stopped;
  int nnamelen;

  DPRINTF("mtcp_printf works; libc should work\n");

  restore_heap();

  if ( (nnamelen = strlen(mtcp_ckpt_newname))
       && strcmp(mtcp_ckpt_newname,perm_checkpointfilename) ) {
    // we start from different place - change it!
    DPRINTF("checkpoint file name was changed\n");
    if (strlen(mtcp_ckpt_newname) >= MAXPATHLEN) {
      MTCP_PRINTF("new ckpt file name (%s) too long (>=512 bytes)\n",
                  mtcp_ckpt_newname);
      mtcp_abort();
    }
    strncpy(perm_checkpointfilename,mtcp_ckpt_newname,MAXPATHLEN);
    memcpy(temp_checkpointfilename,perm_checkpointfilename,MAXPATHLEN);
    strncpy(temp_checkpointfilename + nnamelen, ".temp",MAXPATHLEN - nnamelen);
  }

  mtcp_sys_gettimeofday (&stopped, NULL);
  stopped.tv_usec += (stopped.tv_sec - restorestarted.tv_sec) * 1000000
                         - restorestarted.tv_usec;
  TPRINTF (("mtcp finishrestore*: time %u uS\n", stopped.tv_usec));

  /* Now we can access all our files and memory that existed at the time of the
   * checkpoint.
   * We are still on the temporary stack, though
   */

  /* Fill in the new mother process id */
  motherpid = mtcp_sys_getpid();

  /* Call another routine because our internal stack is whacked and we can't
   * have local vars
   */

  ///JA: v54b port
  // so restarthread will have a big stack
  asm volatile (CLEAN_FOR_64_BIT(mov %0,%%esp)
		: : "g" (motherofall->savctx.SAVEDSP - 128) //-128 for red zone
                : "memory");  
  restarthread (motherofall);
}

static int restarthread (void *threadv)
{
  int rip;
  Thread *child;
  Thread *const thread = threadv;
  struct MtcpRestartThreadArg mtcpRestartThreadArg;

  restore_tls_state (thread);


  if (thread == motherofall) {
    // Compute the set of signals which was pending for all the threads at the
    // time of checkpoint. This is a heuristic to compute the set of signals
    // which were pending for the entire process at the time of checkpoint.
    sigset_t tmp;
    sigfillset ( &tmp );
    Thread *th;
    for (th = threads; th != NULL; th = th -> next) {
      sigandset ( &sigpending_global, &tmp, &(th->sigpending) );
      tmp = sigpending_global;
    }

    setup_sig_handler ();

    set_tid_address (&(thread -> child_tid));

    if (callback_post_ckpt != NULL) {
        DPRINTF("before callback_post_ckpt(1=restarting) (&%x,%x) \n",
                &callback_post_ckpt, callback_post_ckpt);
        (*callback_post_ckpt)(1, mtcp_restore_argv_start_addr);
        DPRINTF("after callback_post_ckpt(1=restarting)\n");
    }
    /* Do it once only, in motherofall thread. */

    restore_term_settings();

    if (dmtcp_info_restore_working_directory
        && chdir(mtcp_saved_working_directory) == -1) {
      perror("chdir");
      mtcp_abort ();
    }

    restore_sig_handlers(thread);
  }

  restore_sig_state (thread);

  for (child = thread -> children; child != NULL; child = child -> siblings) {

    /* Increment number of threads created but haven't completed their longjmp */

    do rip = mtcp_state_value(&restoreinprog);
    while (!mtcp_state_set (&restoreinprog, rip + 1, rip));

    /* Create the thread so it can finish restoring itself.
     * Don't do CLONE_SETTLS (it'll puke).  We do it later via
     * restore_tls_state.
     */

    ///JA: v54b port
    errno = -1;

    void *clone_arg = child;

    /*
     * DMTCP needs to know original_tid of the thread being created by the
     *  following clone() call.
     *
     * Threads are created by using syscall which is intercepted by DMTCP and
     *  the original_tid is sent to DMTCP as a field of MtcpRestartThreadArg
     *  structure. DMTCP will automatically extract the actual argument
     *  (clone_arg -> arg) from clone_arg and will pass it on to the real
     *  clone call.
     *                                                           (--Kapil)
     */
    mtcpRestartThreadArg.arg = child;
    mtcpRestartThreadArg.original_tid = child -> original_tid;
    clone_arg = &mtcpRestartThreadArg;

   /*
    * syscall is wrapped by DMTCP when configured with PID-Virtualization.
    * It calls __clone which goes to DMTCP:__clone which then calls
    * MTCP:__clone. DMTCP:__clone checks for tid-conflict with any original tid.
    * If conflict, it replaces the thread with a new one with a new tid.
    * DMTCP:__clone wrapper calls the glibc:__clone if the computation is not in
    * RUNNING state (must be restarting), it calls the mtcp:__clone otherwise.
    * IF No PID-Virtualization, call glibc:__clone because threads created
    * during mtcp_restart should not go to MTCP:__clone; MTCP remembers those
    * threads from the checkpoint image.
    */

    /* If running under DMTCP */
    pid_t tid;
    if (dmtcp_info_pid_virtualization_enabled == 1) {
      tid = syscall(SYS_clone, restarthread,
                    (void*)(child -> savctx.SAVEDSP - 128), // -128 for red zone
                    ((child -> clone_flags & ~CLONE_SETTLS) |
                     CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID),
                    clone_arg, child -> parent_tidptr, NULL,
                    child -> actual_tidptr);
    } else {
      tid = ((*clone_entry)(restarthread,
                            // -128 for red zone
                            (void *)(child -> savctx.SAVEDSP - 128),
                            ((child -> clone_flags & ~CLONE_SETTLS)
                             | CLONE_CHILD_SETTID
                             | CLONE_CHILD_CLEARTID),
                            child, child -> parent_tidptr, NULL,
                            child -> actual_tidptr));
    }

    if (tid < 0) {
      MTCP_PRINTF("error %d recreating thread\n", errno);
      MTCP_PRINTF("clone_flags %X, savedsp %p\n",
                   child -> clone_flags, child -> savctx.SAVEDSP);
      mtcp_abort ();
    }
    DPRINTF("Parent:%d, tid of newly created thread:%d\n", thread->tid, tid);
  }

  /* All my children have been created, jump to the stopthisthread routine just
   * after getcontext call.
   * Note that if this is the restored checkpointhread, it jumps to the
   * checkpointhread routine
   */

  if (mtcp_have_thread_sysinfo_offset())
    mtcp_set_thread_sysinfo(saved_sysinfo);
  ///JA: v54b port
  DPRINTF("calling setcontext: thread->tid: %d, original_tid:%d\n",
          thread->tid, thread->original_tid);
  setcontext (&(thread -> savctx)); /* Shouldn't return */
  mtcp_abort ();
  return (0); /* NOTREACHED : stop compiler warning */
}

/*****************************************************************************
 *
 *  Restore the GDT entries that are part of a thread's state
 *
 *  The kernel provides set_thread_area system call for a thread to alter a
 *  particular range of GDT entries, and it switches those entries on a
 *  per-thread basis.  So from our perspective, this is per-thread state that is
 *  saved outside user addressable memory that must be manually saved.
 *
 *****************************************************************************/

static void restore_tls_state (Thread *thisthread)

{
  int rc;
#if MTCP__SAVE_MANY_GDT_ENTRIES
  int i;
#endif

  /* The assumption that this points to the pid was checked by that tls_pid crap
   * near the beginning
   */

  *(pid_t *)(*(unsigned long *)&(thisthread -> gdtentrytls[0].base_addr)
             + TLS_PID_OFFSET()) = motherpid;

  /* Likewise, we must jam the new pid into the mother thread's tid slot
   * (checked by tls_tid carpola)
   */

  if (thisthread == motherofall) {
    *(pid_t *)(*(unsigned long *)&(thisthread -> gdtentrytls[0].base_addr)
               + TLS_TID_OFFSET()) = motherpid;
  }

  /* Restore all three areas */

#if MTCP__SAVE_MANY_GDT_ENTRIES
  for (i = GDT_ENTRY_TLS_MIN; i <= GDT_ENTRY_TLS_MAX; i ++) {
    int offset = i - GDT_ENTRY_TLS_MIN;
    rc = mtcp_sys_set_thread_area (&(thisthread -> gdtentrytls[offset]));
    if (rc < 0) {
      MTCP_PRINTF("error %d restoring GDT TLS entry[%d]\n", mtcp_sys_errno, i);
      mtcp_abort ();
    }
  }

  // For newer Linuces, we just restore the one GDT entry that was indexed by GS

#else
  rc = mtcp_sys_set_thread_area (&(thisthread -> gdtentrytls[0]));
  if (rc < 0) {
    MTCP_PRINTF("error %d restoring GDT TLS entry[%d]\n",
                mtcp_sys_errno, thisthread -> gdtentrytls[0].entry_number);
    mtcp_abort ();
  }
#endif

  /* Restore the rest of the stuff */

#ifdef __i386__
  asm volatile ("movw %0,%%fs" : : "m" (thisthread -> fs));
  asm volatile ("movw %0,%%gs" : : "m" (thisthread -> gs));
#endif
#ifdef __x86_64__
/* Don't directly set fs.  It would only set 32 bits, and we just
 *  set the full 64-bit base of fs, using sys_set_thread_area,
 *  which called arch_prctl.
 *asm volatile ("movl %0,%%fs" : : "m" (thisthread -> fs));
 *asm volatile ("movl %0,%%gs" : : "m" (thisthread -> gs));
 */
#endif

  thisthread -> tid = mtcp_sys_kernel_gettid ();
}

/*****************************************************************************
 *
 *  Set the thread's STOPSIGNAL handler.  Threads are sent STOPSIGNAL when they
 *  are to suspend execution the application, save their state and wait for the
 *  checkpointhread to write the checkpoint file.
 *
 *    Output:
 *
 *	Calling thread will call stopthisthread () when sent a STOPSIGNAL
 *
 *****************************************************************************/

static void setup_sig_handler (void)
{
  struct sigaction act, old_act;

  act.sa_handler = &stopthisthread;
  sigfillset(&act.sa_mask);
  act.sa_flags = SA_RESTART;

  // We can't use standard sigaction here, because DMTCP has a wrapper around
  //  it that will not allow anyone to set a signal handler for SIGUSR2.
  if (mtcp_sigaction(STOPSIGNAL, &act, &old_act) == -1) {
    MTCP_PRINTF("error setting up signal handler: %s\n", strerror(errno));
    mtcp_abort ();
  }

  if ((old_act.sa_handler != SIG_IGN) && (old_act.sa_handler != SIG_DFL) && 
      (old_act.sa_handler != stopthisthread)) {
    MTCP_PRINTF("signal handler %d already in use (%p).\n"
                "  You may employ a different signal by setting the\n"
                "  environment variable MTCP_SIGCKPT (or DMTCP_SIGCKPT)"
		"  to the number\n of the signal MTCP should "
                "  use for checkpointing.\n", STOPSIGNAL, old_act.sa_handler);
    mtcp_abort ();
  }
}

/*****************************************************************************
 *
 *  Sync shared memory pages with backup files on disk
 *
 *****************************************************************************/
static void sync_shared_mem(void)
{
  int mapsfd;
  Area area;

  mapsfd = mtcp_sys_open2 ("/proc/self/maps", O_RDONLY);
  if (mapsfd < 0) {
    MTCP_PRINTF("error opening /proc/self/maps: %s\n", MTCP_STR_ERRNO);
    mtcp_abort ();
  }

  while (readmapsline (mapsfd, &area)) {
    /* Skip anything that has no read or execute permission.  This occurs on one
     * page in a Linux 2.6.9 installation.  No idea why.  This code would also
     * take care of kernel sections since we don't have read/execute permission
     * there.
     */

    if (!((area.prot & PROT_READ) || (area.prot & PROT_WRITE))) continue;

    if (!(area.flags & MAP_SHARED)) continue;

    if (strstr(area.name, DELETED_FILE_SUFFIX)) continue;

    DPRINTF("syncing %X at %p from %s + %X\n",
            area.size, area.addr, area.name, area.offset);

    if ( msync(area.addr, area.size, MS_SYNC) < 0 ){
      MTCP_PRINTF("error syncing %X at %p from %s + %X\n",
                   area.size, area.addr, area.name, area.offset);
      mtcp_abort();
    }
  }

  close (mapsfd);
}
