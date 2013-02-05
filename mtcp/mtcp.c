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

// Set _GNU_SOURCE in order to expose glibc-defined sigandset()
#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif
// Set _BSD_SOURCE in order to expose glibc-defined fsync()
#ifndef _BSD_SOURCE
# define _BSD_SOURCE
#endif
// Set _POSIX_C_SOURCE in order to expose sigsetjmp(), siglongjmp()
#ifndef _POSIX_C_SOURCE
# define _POSIX_C_SOURCE 200112L
#endif
//#include <asm/ldt.h>      // for struct user_desc for GDT_ENTRY_TLS_...
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
#include <time.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>       // for tcdrain, tcsetattr, etc.
#include <sys/types.h>     // for gettid, tgkill, waitpid
#include <sys/wait.h>	   // for waitpid
#include <linux/unistd.h>  // for gettid, tgkill
#include <fenv.h>          // for fegetround, fesetround
#include <gnu/libc-version.h>

#define MTCP_SYS_GET_SET_THREAD_AREA
#include "mtcp_internal.h"
#include "mtcp_util.h"

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

/* TLS segment registers used differently in i386 and x86_64. - Gene */
#ifdef __i386__
# define TLSSEGREG gs
#elif __x86_64__
# define TLSSEGREG fs
#elif __arm__
/* FIXME: fs IS NOT AN arm REGISTER.  BUT THIS IS USED ONLY AS A FIELD NAME.
 *   ARM uses a register in coprocessor 15 as the thread-pointer (TLS Register)
 */
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

// NOTE: TLS_TID_OFFSET, TLS_PID_OFFSET determine offset independently of
//     glibc version.  These STATIC_... versions serve as a double check.
// Calculate offsets of pid/tid in pthread 'struct user_desc'
// The offsets are needed for two reasons:
//  1. glibc pthread functions cache the pid; must update this after restart
//  2. glibc pthread functions cache the tid; pthread functions pass address
//     of cached tid to clone, and MTCP grabs it; But MTCP is still missing
//     the address where pthread cached the tid of motherofall.  So, it can't update.
static int STATIC_TLS_TID_OFFSET()
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
     * pthread_desc below is defined as 'struct pthread' in glibc:nptl/descr.h
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

static sem_t sem_start;

/*
 * struct MtcpRestartThreadArg
 *
 * DMTCP requires the virtual_tids of the threads being created during
 *  the RESTARTING phase.  We use a MtcpRestartThreadArg struct to pass
 *  the virtual_tid of the thread being created from MTCP to DMTCP.
 *
 * actual clone call: clone (fn, child_stack, flags, void *, ... )
 * new clone call   : clone (fn, child_stack, flags,
 *                           (struct MtcpRestartThreadArg *), ...)
 *
 * DMTCP automatically extracts arg from this structure and passes that
 * to the _real_clone call.
 *
 * IMPORTANT NOTE: While updating, this struct must be kept in sync
 * with the struct of the same name in mtcpinterface.cpp
 */
struct MtcpRestartThreadArg {
  void *arg;
  pid_t virtual_tid;
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
#define ST_ZOMBIE 7          // zombie state - state about to exit.  If MTCP
			     //   sees this state, it should call threadisdead()

/* Global data */
void *mtcp_libc_dl_handle = NULL;  // dlopen handle for whatever libc.so is
                                   //   loaded with application program
Area mtcp_libc_area;               // some area of that libc.so

/* DMTCP Info Variables */

/* These are reset by dmtcphijack.so at initialization. */
int dmtcp_exists = 0; /* Are we running under DMTCP? */
int dmtcp_info_pid_virtualization_enabled = 0;
/* The following two DMTCP Info variables are defined in mtcp_printf.c */
extern int dmtcp_info_stderr_fd;
extern int dmtcp_info_jassertlog_fd;
int dmtcp_info_restore_working_directory = -1;

char* mtcp_restore_argv_start_addr = NULL;
int mtcp_verify_count;  // number of checkpoints to go
int mtcp_verify_total;  // value given by environ var

	/* Static data */

static int STOPSIGNAL;     // signal to use to signal other threads to stop for
                           //   checkpointing
static sigset_t sigpending_global;  // pending signals for the process
static char progname_str[128] = ""; /* used with MTCP_RESTART_PAUSE */
static char perm_checkpointfilename[PATH_MAX];
static char temp_checkpointfilename[PATH_MAX];
static size_t checkpointsize;
static int intervalsecs;
static pid_t motherpid = 0;
static int showtiming;
static int threadenabledefault;
static int volatile checkpointhreadstarting = 0;
static MtcpState restoreinprog = MTCP_STATE_INITIALIZER;
static MtcpState threadslocked = MTCP_STATE_INITIALIZER;
static pid_t threads_lock_owner = -1;
static pthread_t checkpointhreadid;
static struct timeval restorestarted;
static int DEBUG_RESTARTING = 0;
static Thread *motherofall = NULL;
static Thread *ckpthread = NULL;
static Thread *threads = NULL;
static Thread *threads_freelist = NULL;
/* NOTE:  NSIG == SIGRTMAX+1 == 65 on Linux; NSIG is const, SIGRTMAX isn't */
static struct sigaction sigactions[NSIG];  /* signal handlers */
static int originalstartup = 1;

static void *saved_sysinfo;
VA mtcp_saved_heap_start = NULL;
static void (*callback_sleep_between_ckpt)(int sec) = NULL;
static void (*callback_pre_ckpt)() = NULL;
static void (*callback_post_ckpt)(int is_restarting, char* argv_start) = NULL;
int  (*mtcp_callback_ckpt_fd)(int fd) = NULL;
void (*mtcp_callback_write_ckpt_header)(int fd) = NULL;

static void (*callback_holds_any_locks)(int *retval) = NULL;
static void (*callback_pre_suspend_user_thread)() = NULL;
static void (*callback_pre_resume_user_thread)(int is_restart) = NULL;

static int (*clone_entry) (int (*fn) (void *arg),
                           void *child_stack,
                           int flags,
                           void *arg,
                           int *parent_tidptr,
                           struct user_desc *newtls,
                           int *child_tidptr) = NULL;

int (*mtcp_sigaction_entry) (int sig, const struct sigaction *act,
                             struct sigaction *oact);

static void *(*malloc_entry) (size_t size) = NULL;
static void (*free_entry) (void *ptr) = NULL;

/* temp stack used internally by restore so we don't go outside the
 *   libmtcp.so address range for anything;
 * including "+ 1" since will set %esp/%rsp to tempstack+STACKSIZE
 */
static long long tempstack[STACKSIZE + 1];

	/* Internal routines */

static long set_tid_address (int *tidptr);

static int threadcloned (void *threadv);
static void setupthread (Thread *thread);
static void setup_clone_entry (void);
static void threadisdead (Thread *thread);
static void *checkpointhread (void *dummy);
static void update_checkpoint_filename(const char *ckptfilename);
static void stopthisthread (int signum);
static void wait_for_all_restored (Thread *thisthread);
static void save_sig_state (Thread *thisthread);
static void restore_sig_state (Thread *thisthread);
static void save_sig_handlers (void);
static void restore_sig_handlers (Thread *thisthread);
static void save_tls_state (Thread *thisthread);
static Thread *getcurrenthread (void);
static void lock_threads (void);
static void unlk_threads (void);
static int is_thread_locked (void);
static void restore_heap(void);
static int restarthread (void *threadv);
static void restore_tls_state (Thread *thisthread);
static void setup_sig_handler (sighandler_t handler);
static void sync_shared_mem(void);

static Thread *mtcp_get_thread_from_freelist();
static void mtcp_put_thread_on_freelist(Thread *thread);
static void mtcp_empty_threads_freelist();
static void mtcp_remove_duplicate_thread_descriptors(Thread *cur_thread);

static void *mtcp_safe_malloc(size_t size);
static void mtcp_safe_free(void *ptr);

void mtcp_thread_start(void *arg);
void mtcp_thread_return();

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
 *  This routine must be called before mtcp_init if running under DMTCP
 *
 *****************************************************************************/
void mtcp_init_dmtcp_info (int pid_virtualization_enabled,
                           int stderr_fd,
                           int jassertlog_fd,
                           int restore_working_directory,
                           void *clone_fnptr,
                           void *sigaction_fnptr,
                           void *malloc_fnptr,
                           void *free_fnptr)
{
  dmtcp_exists = 1;
  dmtcp_info_pid_virtualization_enabled = pid_virtualization_enabled;
  dmtcp_info_stderr_fd = stderr_fd;
  dmtcp_info_jassertlog_fd = jassertlog_fd;
  dmtcp_info_restore_working_directory = restore_working_directory;
  clone_entry = clone_fnptr;
  mtcp_sigaction_entry = sigaction_fnptr;
  malloc_entry = malloc_fnptr;
  free_entry = free_fnptr;
}

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
 *	environ var MTCP_WRAPPER_LIBC_SO = what library to use for inner wrappers
 *	                             (default libc.??.so)
 *	environ var MTCP_VERIFY_CHECKPOINT = every n checkpoints, verify by
 *	                                  doing a restore to resume;
 *	                               default is 0, ie, don't ever verify
 *
 *****************************************************************************/
void mtcp_init (char const *checkpointfilename,
                int interval,
                int clonenabledefault)
{
  char *p, *tmp, *endp;
  Thread *ckptThreadDescriptor = & ckptThreadStorage;

  mtcp_saved_pid = mtcp_sys_getpid ();

  //mtcp_segreg_t TLSSEGREG;

  /* Initialize the static curbrk variable in sbrk wrapper. */
  sbrk(0);

  if (sizeof(void *) != sizeof(long)) {
    MTCP_PRINTF("ERROR: sizeof(void *) != sizeof(long) on this architecture.\n"
                "       This code assumes they are equal.\n");
    mtcp_abort ();
  }

  if (((PROT_READ|PROT_WRITE|PROT_EXEC)&MTCP_PROT_ZERO_PAGE) != 0) {
    MTCP_PRINTF("ERROR: PROT_READ|PROT_WRITE|PROT_EXEC and MTCP_PROT_ZERO_PAGE "
                "shouldn't overlap\n");
    mtcp_abort();
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

  intervalsecs = interval;

  update_checkpoint_filename(checkpointfilename);

  DPRINTF("main tid %d\n", mtcp_sys_kernel_gettid ());
  /* If MTCP_INIT_PAUSE set, sleep 15 seconds and allow for gdb attach. */
  if (getenv("MTCP_INIT_PAUSE")) {
    struct timespec delay = {15, 0}; /* 15 seconds */
    MTCP_PRINTF("Pausing 15 seconds. Do:  gdb attach %d\n", mtcp_sys_getpid());
    mtcp_sys_nanosleep(&delay, NULL);
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

  /* Get verify environ var */

  tmp = getenv ("MTCP_VERIFY_CHECKPOINT");
  mtcp_verify_total = 0;
  if (tmp != NULL) {
    mtcp_verify_total = strtol (tmp, &p, 0);
    if ((*p != '\0') || (mtcp_verify_total < 0)) {
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

  /* Setup clone_entry to point to glibc's __clone routine
   * NOTE: This also sets up mtcp_sigaction_entry to point to glibc's sigaction
   * therefore, it must be called before setup_sig_handler();
   */
  if (clone_entry == NULL) setup_clone_entry ();

  /* Set up signal handler so we can interrupt the thread for checkpointing */
  setup_sig_handler(&stopthisthread);

  mtcp_writeckpt_init((VA)mtcp_restore_start, (VA) mtcp_finishrestore);

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
                        void (*write_ckpt_header)(int fd))
{
  callback_sleep_between_ckpt = sleep_between_ckpt;
  callback_pre_ckpt = pre_ckpt;
  callback_post_ckpt = post_ckpt;
  mtcp_callback_ckpt_fd = ckpt_fd;
  mtcp_callback_write_ckpt_header = write_ckpt_header;
}

void mtcp_set_dmtcp_callbacks(void (*holds_any_locks)(int *retval),
                              void (*pre_suspend_user_thread)(),
                              void (*pre_resume_user_thread)(int is_restart))
{
  callback_holds_any_locks = holds_any_locks;
  callback_pre_suspend_user_thread = pre_suspend_user_thread;
  callback_pre_resume_user_thread = pre_resume_user_thread;
}

/*************************************************************************
 *
 *  Reset various data structures and variable after fork.
 *
 *************************************************************************/
void mtcp_reset_on_fork()
{
  lock_threads();
  // motherofall can't be placed on freelist, it's a static buffer.
  while (threads != motherofall && threads != NULL) {
    Thread *th = threads;
    threads = threads->next;
    mtcp_put_thread_on_freelist(th);
  }
  unlk_threads();

  sem_destroy(&sem_start);
  sem_init(&sem_start, 0, 0);
  motherpid = 0;
  checkpointhreadstarting = 0;
  mtcp_state_init(&threadslocked, 0);
  mtcp_state_init(&restoreinprog, 0);
  threads_lock_owner = -1;
  checkpointhreadid = -1;
  originalstartup = 1;

  motherofall = NULL;
  ckpthread = NULL;
  threads = NULL;
  setup_sig_handler(SIG_DFL);
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
    mtcp_sys_kernel_futex (&mutex, FUTEX_WAIT, 1, NULL, NULL, 0);
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
      if (i == -1) {
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
  mtcp_sys_kernel_futex (&mutex, FUTEX_WAKE, 1, NULL, NULL, 0);
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

void *mtcp_prepare_for_clone (int (*fn) (void *arg), void *child_stack,
                                int *flags, void *arg, int *parent_tidptr,
                                struct user_desc *newtls, int **child_tidptr)
{
  Thread *thread = NULL;

  /* Maybe they decided not to call mtcp_init */
  if (motherofall != NULL) {

    /* They called mtcp_init meaning we are to do checkpointing.
     * So we are going to track this thread.
     */

    thread = mtcp_get_thread_from_freelist();
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

    DPRINTF("calling clone thread=%p, fn=%p, flags=0x%X\n", thread, fn, *flags);
    DPRINTF("parent_tidptr=%p, newtls=%p, child_tidptr=%p\n",
            parent_tidptr, newtls, *child_tidptr);
    //asm volatile ("int3");

    /* Save exactly what the caller is supplying */

    thread -> clone_flags   = *flags;
    thread -> parent_tidptr = parent_tidptr;
    thread -> given_tidptr  = *child_tidptr;

    /* We need the CLEARTID feature so we can detect
     *   when the thread has exited
     * So if the caller doesn't want it, we enable it
     * Retain what the caller originally gave us so we can pass the tid back
     */

    if (!(*flags & CLONE_CHILD_CLEARTID)) {
      *child_tidptr = &(thread -> child_tid);

      /* Force CLEARTID */
      *flags |= CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID;
    }
    thread -> actual_tidptr = *child_tidptr;
    DPRINTF("thread %p -> actual_tidptr %p\n", thread, thread -> actual_tidptr);
  }
  return (void*) thread;
}

int __clone (int (*fn) (void *arg), void *child_stack, int flags, void *arg,
	     int *parent_tidptr, struct user_desc *newtls, int *child_tidptr)
{
  int rc;
  Thread *thread;
  /* thread is allocated via mtcp_get_thread_from_freelist() */
  thread = mtcp_prepare_for_clone(fn, child_stack, &flags, arg,
                                  parent_tidptr, newtls, &child_tidptr);

  if (motherofall != NULL) {
    /* Alter call parameters, forcing CLEARTID and make it call the wrapper
     * routine
     */
    fn = threadcloned;
    arg = thread;
  } else if (clone_entry == NULL) {
    /* mtcp_init not called, no checkpointing, but make sure clone_entry is */
    /* set up so we can call the real clone                                 */

    setup_clone_entry ();
  }

  /* Now create the thread */

  DPRINTF("clone fn=%p, child_stack=%p, flags=%X, arg=%p\n",
          fn, child_stack, flags, arg);
  DPRINTF("parent_tidptr=%p, newtls=%p, child_tidptr=%p\n",
          parent_tidptr, newtls, child_tidptr);
  rc = (*clone_entry) (fn, child_stack, flags, arg, parent_tidptr, newtls,
                       child_tidptr);
  if (rc < 0) {
    DPRINTF("clone rc=%d, errno=%d\n", rc, errno);
    lock_threads();
    mtcp_put_thread_on_freelist(thread);
    unlk_threads();
  } else {
    DPRINTF("clone rc=%d\n", rc);
  }

  return (rc);
}

#if defined(__i386__) || defined(__x86_64__)
asm (".global clone ; .type clone,@function ; clone = __clone");
#elif defined(__arm__)
// In arm, '@' is a comment character;  Arm uses '%' in type directive
asm (".global clone ; .type clone,%function ; clone = __clone");
#else
# error Not implemented on this achitecture
#endif

/*****************************************************************************
 *
 *  This routine is called (via clone) as the top-level routine of a thread
 *  that we are tracking.
 *
 *  It fills in remaining items of our thread struct, calls the user function,
 *  then cleans up the thread struct before exiting.
 *
 *****************************************************************************/

void mtcp_thread_start(void *threadv)
{
  Thread *const thread = threadv;

  DPRINTF("starting thread %p\n", thread);

  /*
   * libpthread may recycle the thread stacks after the thread exits (due to
   * return, pthread_exit, or pthread_cancel) by reusing them for a different
   * thread created by a subsequent call to pthread_create().
   *
   * Part of thread-stack also contains the "struct pthread" with pid and tid
   * as member fields. While reusing the stack for the new thread, the tid
   * field is reset but the pid field is left unchanged (under the assumption
   * that pid never changes). This causes a problem if the thread exited before
   * checkpoint and the new thread is created after restart and hence the pid
   * field contains the wrong value (pre-ckpt pid as opposed to current-pid).
   *
   * The solution is to put the motherpid in the pid slot every time a new
   * thread is created to make sure that struct pthread has the correct value.
   */
  {
    pid_t  *tls_pid = (pid_t *) (mtcp_get_tls_base_addr() + TLS_PID_OFFSET());
    *tls_pid = motherpid;
  }

  setupthread (thread);

  // Check and remove any thread descriptor which has the same tid as ours.
  // Also, remove any dead threads from the list.
  mtcp_remove_duplicate_thread_descriptors(thread);

  /* If the caller wants the child tid but didn't have CLEARTID, pass the tid
   * back to it
   */

  if ((thread -> clone_flags & (CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID)) ==
          CLONE_CHILD_SETTID) {
    *(thread -> given_tidptr) = thread -> child_tid;
  }

  /* Maybe enable checkpointing by default */

  if (threadenabledefault) mtcp_ok ();
}


static int threadcloned (void *threadv)
{
  int rc;
  Thread *const thread = threadv;
  /* Call the user's function for whatever processing they want done */

  mtcp_thread_start(threadv);

  DPRINTF("calling %p (%p)\n", thread -> fn, thread -> arg);
  rc = (*(thread -> fn)) (thread -> arg);
  DPRINTF("returned %d\n", rc);

  mtcp_thread_return();

  /* Return the user's status as the exit code */

  return (rc);
}

void mtcp_thread_return()
{
  /* Make sure checkpointing is inhibited while we clean up and exit */
  /* Otherwise, checkpointer might wait forever for us to re-enable  */

  mtcp_no ();

  /* Do whatever to unlink and free thread block */
  lock_threads();
  threadisdead(getcurrenthread());
  unlk_threads();
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
  if (dmtcp_info_pid_virtualization_enabled == 1) {
    thread -> virtual_tid = GETTID ();
  }

  DPRINTF("thread %p -> tid %d\n", thread, thread->tid);

  lock_threads ();

  if ((thread -> next = threads) != NULL) {
    threads -> prev = thread;
  }
  thread -> prev = NULL;
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
  const char *p, *tmp;
  int mapsfd;

  if (dmtcp_exists) {
    MTCP_PRINTF("Error: NOT REACHED!\n");
    mtcp_abort();
  }

  /* Get name of whatever concoction we have for a libc shareable image */
  /* This is used by the wrapper routines                               */

  tmp = getenv ("MTCP_WRAPPER_LIBC_SO");
  if (tmp != NULL) {
    if (mtcp_strlen(tmp) >= sizeof(mtcp_libc_area.name)) {
      MTCP_PRINTF("libc area name (%s) too long (>=1024 chars)\n", tmp);
      mtcp_abort();
    }
    mtcp_strncpy (mtcp_libc_area.name, tmp, sizeof mtcp_libc_area.name);
  } else {
    mapsfd = mtcp_sys_open2 ("/proc/self/maps", O_RDONLY);
    if (mapsfd < 0) {
      MTCP_PRINTF("error opening /proc/self/maps: %s\n",
                  strerror(mtcp_sys_errno));
      mtcp_abort ();
    }
    p = NULL;
    while (mtcp_readmapsline (mapsfd, &mtcp_libc_area, NULL)) {
      p = mtcp_strstr (mtcp_libc_area.name, "/libc-");
      /* We can't do a dlopen on the debug version of libc. */
      if (((p != NULL) && ((p[5] == '-') || (p[5] == '.'))) &&
          !mtcp_strstr(mtcp_libc_area.name, "debug")) break;
    }
    mtcp_sys_close (mapsfd);
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
  mtcp_sigaction_entry = mtcp_get_libc_symbol ("sigaction");
}


static void mtcp_remove_duplicate_thread_descriptors(Thread *cur_thread)
{
  int tid;
  Thread *thread;
  Thread *next_thread;

  lock_threads();

  tid = cur_thread->tid;
  if (tid == 0) {
    MTCP_PRINTF("MTCP Internal Error!\n");
    mtcp_abort();
  }
  for (thread = threads; thread != NULL; thread = next_thread) {
    next_thread = thread->next;
    if (thread != cur_thread && thread->tid == tid) {
      DPRINTF("Removing duplicate thread descriptor: tid:%d, orig_tid:%d\n",
              thread->tid, thread->virtual_tid);
      // There will be at most one duplicate descriptor.
      threadisdead(thread);
      continue;
    }
    /* NOTE:  ST_ZOMBIE is used only for the sake of efficiency.  We
     *   test threads in state ST_ZOMBIE using tgkill to remove them
     *   early (before reaching a checkpoint) so that the MTCP
     *   threadrdescriptor list does not grow too long.
     */
    if (mtcp_state_value (&thread -> state) == ST_ZOMBIE) {
      /* if no thread with this tid, then we can remove zombie descriptor */
      if (-1 == mtcp_sys_kernel_tgkill(motherpid, thread -> tid, 0)) {
        DPRINTF("Killing zombie thread: motherpid, thread->tid: %d, %d\n",
	        motherpid, thread->tid);
        threadisdead(thread);
      }
    }
  }

  unlk_threads();
  return;
}

/*****************************************************************************
 *
 *  Thread has exited, put the struct on free list
 *
 *  threadisdead() used to free() the Thread struct before returning. However,
 *  if we do that while in the middle of a checkpoint, the call to free() might
 *  deadlock in JAllocator. For this reason, we put the to-be-removed threads
 *  on this threads_freelist and call free() only when it is safe to do so.
 *
 *  This has an added benefit of reduced number of calls to malloc() as the
 *  Thread structs in the freelist can be recycled.
 *
 *****************************************************************************/

static void mtcp_put_thread_on_freelist(Thread *thread)
{
  if (! is_thread_locked()) {
    DPRINTF("MTCP: Internal error: %s called without thread lock\n",
            __FUNCTION__);
    mtcp_abort ();
  }

  if (thread == NULL) {
    MTCP_PRINTF("Internal Error: Not Reached\n");
    mtcp_abort();
  }
  thread->next = threads_freelist;
  threads_freelist = thread;
}

/*****************************************************************************
 *
 *  Return thread from freelist.
 *
 *****************************************************************************/

static Thread *mtcp_get_thread_from_freelist()
{
  Thread *thread;

  lock_threads ();
  if (threads_freelist == NULL) {
    thread = (Thread*) mtcp_safe_malloc(sizeof(Thread));
    if (thread == NULL) {
      MTCP_PRINTF("Error allocating thread struct\n");
      mtcp_abort();
    }
  } else {
    thread = threads_freelist;
    threads_freelist = threads_freelist->next;
  }
  unlk_threads ();
  memset(thread, 0, sizeof (*thread));
  return thread;
}

/*****************************************************************************
 *
 *  call free() on all threads_freelist items
 *
 *****************************************************************************/

static void mtcp_empty_threads_freelist()
{
  lock_threads ();

  while (threads_freelist != NULL) {
    Thread *thread = threads_freelist;
    threads_freelist = threads_freelist->next;
    mtcp_safe_free(thread);
  }

  unlk_threads ();
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
 *	thread removed from 'threads' list and motherofall tree and placed on
 *	freelist
 *	thread pointer no longer valid
 *	checkpointer woken if waiting for this thread
 *
 *****************************************************************************/

/* Declare current thread to be zombie. */
void mtcp_threadiszombie(void)
{
  /* NOTE:  ST_ZOMBIE is used only for the sake of efficiency.  See further
   *   comment in getcurrenthread().
   */
  /* Would use mtcp_state_set(), except we don't know previous, old state. */
  getcurrenthread()->state.value = ST_ZOMBIE;
}

static void threadisdead (Thread *thread)
{
  Thread *next_sibling, *parent, *xthread;

  if (! is_thread_locked()) {
    DPRINTF("MTCP: Internal error: threadisdead called without thread lock\n");
    mtcp_abort ();
  }
  DPRINTF("thread %p -> tid %d\n", thread, thread -> tid);

  /* Remove thread block from 'threads' list */
  if (thread -> prev != NULL) {
    thread -> prev -> next = thread -> next;
  }
  if (thread -> next != NULL) {
    thread -> next -> prev = thread -> prev;
  }
  if (thread == threads) {
    threads = threads->next;
  }

  parent = thread -> parent;
  if (parent == NULL) {
    MTCP_PRINTF("MTCP Internal Error: Orphaned Child Thread found: %d\n",
                thread->tid);
    mtcp_abort();
  }
  /* Remove thread block from parent's list of children */
  if (parent->children == thread) {
    parent->children = thread->siblings;
  } else {
    for (xthread = parent->children;
         xthread->siblings != thread;
         xthread = xthread->siblings) {}
    xthread->siblings = thread -> siblings;
  }

  /* If this thread has children, give them to its parent */
  for (xthread = thread->children; xthread != NULL; xthread = next_sibling) {
    next_sibling = xthread->siblings;
    xthread->parent = parent;
    xthread->siblings = parent->children;
    parent->children = xthread;
  }

  /* If checkpoint thread is waiting for us, wake it to let it see
   * that this thread is no longer in list
   */
  mtcp_state_futex (&(thread -> state), FUTEX_WAKE, 1, NULL);
  mtcp_state_destroy( &(thread -> state) );
  mtcp_put_thread_on_freelist(thread);
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
      mtcp_sys_kernel_tgkill(motherpid, thread->tid, STOPSIGNAL);
      return;
    }
  }
  unlk_threads ();
}

/*************************************************************************
 *
 *  MTCP should use DMTCP Allocator for doing malloc/free.
 *
 *************************************************************************/

/* MTCP can now use the DMTCP allocator for creating and destroying threads.
 * This ensures that when creating and destroying elements of type Thread (for
 * each thread created or exited), we will not use the user's malloc arena.
 * Otherwise, there could be deadlock between the ckpt thread inside MTCP, and
 * a user thread inside malloc or free.
*/
static void *mtcp_safe_malloc(size_t size)
{
  if (malloc_entry == NULL) {
    malloc_entry = malloc;
  }
  return malloc_entry(size);
}

static void mtcp_safe_free(void *ptr)
{
  if (free_entry == NULL) {
    free_entry = free;
  }
  free_entry(ptr);
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
static int safe_tcsetattr(int fd, int optional_actions,
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
// FIXME: Handle Virtual Pids
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

// We will use the region beyond the end of stack for our temporary stack.
// glibc sigsetjmp will mangle pointers;  We need the unmangled pointer.
// So, we can't rely on parsing the jmpbuf for the saved sp.
void save_sp(Thread *thread) {
#if defined(__i386__) || defined(__x86_64__)
  asm volatile (CLEAN_FOR_64_BIT(mov %%esp,%0)
		: "=g" (thread->saved_sp)
                : : "memory");
#elif defined(__arm__)
  asm volatile ("mov %0,sp"
		: "=r" (thread->saved_sp)
                : : "memory");
#else
# error "assembly instruction not translated"
#endif
}

static void *checkpointhread (void *dummy)
{
  int needrescan;
  struct timespec sleeperiod;
  struct timeval started, stopped;
  Thread *thread;
  char * dmtcp_checkpoint_filename = NULL;
  int rounding_mode = -1;

  /* This is the start function of the checkpoint thread.
   * We also call sigsetjmp/getcontext to get a snapshot of this call frame,
   * since we will never exit this call frame.  We always return
   * to this call frame at time of startup, on restart.  Hence, restart
   * will forget any modifications to our local variables since restart.
   */

  /* We put a timeout in case the thread being waited for exits whilst we are
   * waiting
   */

  static struct timespec const enabletimeout = { 10, 0 };

  DPRINTF("%d started\n", mtcp_sys_kernel_gettid ());

  ckpthread = getcurrenthread ();

  save_sig_state( ckpthread );
  save_tls_state (ckpthread);
  /* Release user thread after we've initialized. */
  sem_post(&sem_start);

  /* Set up our restart point, ie, we get jumped to here after a restore */
#ifdef SETJMP
  /* After we restart, we return here. */
  if (sigsetjmp (ckpthread -> jmpbuf, 1) < 0) mtcp_abort ();
  save_sp(ckpthread);
  DPRINTF("after sigsetjmp. current_tid %d, virtual_tid:%d\n",
          mtcp_sys_kernel_gettid(), ckpthread->virtual_tid);
#else
  /* After we restart, we return here. */
  if (getcontext (&(ckpthread -> savctx)) < 0) mtcp_abort ();
  save_sp(ckpthread);
  DPRINTF("after getcontext. current_tid %d, virtual_tid:%d\n",
          mtcp_sys_kernel_gettid(), ckpthread->virtual_tid);
#endif

  if (originalstartup)
    originalstartup = 0;
  else {

    /* We are being restored.  Wait for all other threads to finish being
     * restored before resuming checkpointing.
     */

    DPRINTF("waiting for other threads after restore\n");
    wait_for_all_restored(ckpthread);
    DPRINTF("resuming after restore\n");
  }

  /* Reset the verification counter - on init, this will set it to it's start
   * value. After a verification, it will reset it to its start value.  After a
   * normal restore, it will set it to its start value.  So this covers all
   * cases.
   */

  mtcp_verify_count = mtcp_verify_total;
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

    /* Halt all other threads - force them to call stopthisthread
     * If any have blocked checkpointing, wait for them to unblock before
     * signalling
     */

rescan:
    needrescan = 0;
    lock_threads ();
    for (thread = threads; thread != NULL; thread = thread -> next) {
again:
      /* If thread no longer running, remove it from thread list */
#if 1
      if (mtcp_sys_kernel_tgkill(motherpid, thread->tid, 0) == -1
          && mtcp_sys_errno == ESRCH) {
#else
      if (*(thread -> actual_tidptr) == 0) {
#endif
        DPRINTF("thread %d disappeared\n", thread -> tid);
        threadisdead (thread);
        unlk_threads ();
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

        case ST_ZOMBIE: {
          break;
        }

        case ST_RUNENABLED: {
          if (!mtcp_state_set(&(thread -> state), ST_SIGENABLED, ST_RUNENABLED))
            goto again;
          int retval = mtcp_sys_kernel_tgkill(motherpid, thread->tid,
                                              STOPSIGNAL);
          if (retval < 0 && mtcp_sys_errno != ESRCH) {
            MTCP_PRINTF ("error signalling thread %d: %s\n",
                         thread -> tid, strerror(mtcp_sys_errno));
          }

          if (retval < 0) {
            threadisdead(thread);
            unlk_threads();
            goto rescan;
          }

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
          static struct timespec const timeout = { 1, 0 };
          mtcp_state_futex (&(thread -> state), FUTEX_WAIT, ST_SIGENABLED,
			    &timeout);
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

    lock_threads();
    Thread *th;
    for (thread = threads; thread != NULL; thread = th) {
      th = thread->next;
      if (mtcp_state_value(&thread->state) == ST_ZOMBIE) {
        int rv;
        do {
          rv = mtcp_sys_kernel_tgkill(motherpid, thread->tid, 0);
        } while (rv != -1);
        threadisdead(thread);
      }
    }
    unlk_threads ();

    RMB; // matched by WMB in stopthisthread
    DPRINTF("everything suspended\n");

    /* Remove STOPSIGNAL from pending signals list:
     * Under Ptrace, STOPSIGNAL is sent to the inferior threads once by the
     * superior thread and once by the ckpt-thread of the inferior. STOPSIGNAL
     * is blocked while the inferior thread is executing the signal handler and
     * so the signal is becomes pending and is delivered right after returning
     * from stopthisthread.
     * To tackle this, we disable/re-enable signal handler for STOPSIGNAL.
     */
    setup_sig_handler(SIG_IGN);
    setup_sig_handler(&stopthisthread);

    /* If no threads, we're all done */

    if (threads == NULL) {
      DPRINTF("exiting (no threads)\n");
      return (NULL);
    }

    rounding_mode = fegetround();
    int tmp = mtcp_sys_readlink("/proc/self/exe",
			    progname_str, sizeof(progname_str)-1);
    if (tmp > 0)
      progname_str[tmp] = '\0';
    else
      progname_str[0] = '\0';
    /* Call weak symbol of this file, possibly overridden by the user's
     *   strong symbol.  User must compile his/her code with
     *   -Wl,-export-dynamic to make it visible.
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
          mtcp_strcmp(dmtcp_checkpoint_filename, "/dev/null") != 0) {
        update_checkpoint_filename(dmtcp_checkpoint_filename);
      }
    }

    mtcp_empty_threads_freelist();

    // kernel returns mm->brk when passed zero
    mtcp_saved_break = (void*) mtcp_sys_brk(NULL);

    /* Do this once, same for all threads.  But restore for each thread. */
    if (mtcp_have_thread_sysinfo_offset())
      saved_sysinfo = mtcp_get_thread_sysinfo();
    /* Do this once.  It's the same for all threads. */
    save_term_settings();

    if (getcwd(mtcp_saved_working_directory, PATH_MAX) == NULL) {
      // buffer wasn't large enough
      perror("getcwd");
      MTCP_PRINTF ("getcwd failed.");
      mtcp_abort ();
    }

    DPRINTF("mtcp_saved_break=%p\n", mtcp_saved_break);

    if ( dmtcp_checkpoint_filename == NULL ||
         mtcp_strcmp (dmtcp_checkpoint_filename, "/dev/null") != 0) {
      mtcp_checkpointeverything(temp_checkpointfilename, perm_checkpointfilename);
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

    /* This function is: checkpointhread();  So, only ckpt thread executes */
    fesetround(rounding_mode);
    /* Call weak symbol of this file, possibly overridden by the user's
     *   strong symbol.  User must compile his/her code with
     *   -Wl,-export-dynamic to make it visible.
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
        if (!mtcp_state_set (&(thread -> state), ST_RUNENABLED, ST_SUSPENDED)) {
          MTCP_PRINTF("DMTCP:  Internal error: thread->state.value: %d\n",
                      thread->state.value);
          mtcp_abort();
        }
        mtcp_state_futex(&(thread -> state), FUTEX_WAKE, 1, NULL);
      }
    }
    unlk_threads ();
    DPRINTF("everything resumed\n");
    /* But if we're doing a restore verify, just exit.  The main thread is doing
     * the exec to start the restore.
     */
    if ((mtcp_verify_total != 0) && (mtcp_verify_count == 0)) return (NULL);
  }
}

static void update_checkpoint_filename(const char *checkpointfilename)
{
  size_t len = mtcp_strlen(checkpointfilename);
  if (len >= PATH_MAX) {
    MTCP_PRINTF("checkpoint filename (%s) too long (>=%d bytes)\n",
                checkpointfilename, PATH_MAX);
    mtcp_abort();
  }
  // this is what user wants the checkpoint file called
  mtcp_strncpy(perm_checkpointfilename, checkpointfilename, PATH_MAX);
  // make up another name, same as that, with ".temp" on the end ... we use it
  // to write to in case we crash while writing, we will leave the previous good
  // one intact
  memcpy(temp_checkpointfilename, perm_checkpointfilename, len);
  if (len == PATH_MAX) {
    MTCP_PRINTF("ckpt filename is %d bytes long, temp file will have the same name",
                checkpointfilename, PATH_MAX);
  }
  mtcp_strncpy(temp_checkpointfilename + len, ".temp", PATH_MAX - len);
  DPRINTF("Checkpoint filename changed to %s\n", perm_checkpointfilename);
}


/*****************************************************************************
 *
 *  This signal handler is forced by the main thread doing a
 *  'mtcp_sys_kernel_tgkill' to stop these threads so it can do a
 *  checkpoint
 *
 * Grow the stack by kbStack*1024 so that large stack is allocated on restart
 * The kernel won't do it automatically for us any more, since it thinks
 * the stack is in a different place after restart.
 *
 * growstackValue is volatile so compiler doesn't optimize away growstack
 * Maybe it's not needed if we use ((optimize(0))) .
 *****************************************************************************/
static int growstackrlimit(size_t kbStack) {
  size_t size = kbStack * 1024;  /* kbStack was in units of kilobytes */
  struct rlimit rlim;
  mtcp_sys_getrlimit(RLIMIT_STACK, &rlim);
  if (rlim.rlim_cur == RLIM_INFINITY)
    return 1;
  if (rlim.rlim_max == RLIM_INFINITY || rlim.rlim_max - rlim.rlim_cur > size) {
    rlim.rlim_cur += size;
    mtcp_sys_setrlimit(RLIMIT_STACK, &rlim);
    return 1;
  } else {
    mtcp_printf("MTCP:  Warning: couldn't extend stack limit for growstack.\n");
  }
  return 0;
}

static volatile unsigned int growstackValue = 0;
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ > 3)
static void __attribute__ ((optimize(0))) growstack (int kbStack)
#else
static void growstack (int kbStack) /* opimize attribute not implemented */
#endif
{
  /* With the split stack option (-fsplit-stack in gcc-4.6.0 and later),
   * we can only hope for 64 KB of free stack.  (Also, some users will use:
   * ld --stack XXX.)  We'll use half of the 64 KB here.
   */
  const int kbIncrement = 1024; /* half the size of kbStack */
  char array[kbIncrement * 1024] __attribute__ ((unused));
  /* Again, try to prevent compiler optimization */
  volatile int dummy_value __attribute__ ((unused)) = 1;
  if (kbStack > 0)
    growstack(kbStack - kbIncrement);
  else
    growstackValue++;
}

static void stopthisthread (int signum)
{
  int rc;
  int is_restart = 0;
  Thread *thread;

  thread = getcurrenthread (); // see which thread this is

  // If this is checkpoint thread - exit immidiately
  if ( mtcp_state_value(&thread -> state) == ST_CKPNTHREAD ) {
    return ;
  }

  /* Possible state change scenarios:
   * 1. STOPSIGNAL received from ckpt-thread. In this case, the ckpt-thread
   * already changed the state to ST_SIGENABLED. No need to check for locks.
   * Proceed normally.
   *
   * 2. STOPSIGNAL received from Superior thread. In this case we change the
   * state to ST_SIGENABLED, if currently in ST_RUNENABLED. If we are holding
   * any locks (callback_holds_any_locks), we return from the signal handler.
   *
   * 3. STOPSIGNAL raised by this thread itself, after releasing all the locks.
   * In this case, we had already changed the state to ST_SIGENABLED as a
   * result of step (2), so the ckpt-thread will never send us a signal.
   *
   * 4. STOPSIGNAL received from Superior thread. Ckpt-threads sends a signal
   * before we had a chance to change state from ST_RUNENABLED ->
   * ST_SIGENABLED. This puts the STOPSIGNAL in the queue. The ckpt-thread will
   * later call sigaction(STOPSIGNAL, SIG_IGN) followed by
   * sigaction(STOPSIGNAL, stopthisthread) to discard all pending signals.
   */
  if (mtcp_state_set(&(thread -> state), ST_SIGENABLED, ST_RUNENABLED)) {
    if (callback_holds_any_locks != NULL) {
      int retval;
      callback_holds_any_locks(&retval);
      if (retval) return;
    }
  }

  DPRINTF("tid %d returns to %p\n",
          mtcp_sys_kernel_gettid (), __builtin_return_address (0));

#if 0
#define BT_SIZE 1024
#define STDERR_FD 826
#define LOG_FD 826
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
      /* FIXME:
       * Some apps will use "ld --stack XXX" with a small stack.  This
       * trend will become more common with the introduction of split stacks.
       * BUT NOTE PROBLEM PREV. COMMENT ON KERNEL NOT GROWING STACK ON RESTART
       * Grow the stack by kbStack*1024 so that large stack is allocated oni
       * restart.  The kernel won't do it automatically for us any more,
       * since it thinks the stack is in a different place after restart.
       */
      int kbStack = 2048; /* double the size of kbIncrement in growstack */
      if (is_first_checkpoint) {
	orig_stack_ptr = (char *)&kbStack;
        is_first_checkpoint = 0;
        DPRINTF("temp. grow main stack by %d kilobytes\n", kbStack);
        growstackrlimit(kbStack);
        growstack(kbStack);
      } else if (orig_stack_ptr - (char *)&kbStack > 3 * kbStack*1024 / 4) {
        MTCP_PRINTF("WARNING:  Stack within %d bytes of end;\n"
		    "  Consider increasing 'kbStack' at line %d of mtcp/%s\n",
		    kbStack*1024/4, __LINE__-9, __FILE__);
      }
    }

    ///JA: new code ported from v54b
#ifdef SETJMP
    rc = sigsetjmp (thread -> jmpbuf, 1);
    save_sp(thread);
    if (rc < 0) {
      MTCP_PRINTF("sigsetjmp rc %d errno %d\n", rc, strerror(mtcp_sys_errno));
      mtcp_abort ();
    }
    DPRINTF("after sigsetjmp\n");
#else
    rc = getcontext (&(thread -> savctx));
    save_sp(thread);
    if (rc < 0) {
      MTCP_PRINTF("getcontext rc %d errno %d\n", rc, strerror(mtcp_sys_errno));
      mtcp_abort ();
    }
    DPRINTF("after getcontext\n");
#endif
    if (mtcp_state_value(&restoreinprog) == 0) {
      is_restart = 0;
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

      if (callback_pre_suspend_user_thread != NULL) {
        callback_pre_suspend_user_thread();
      }

      /* Then we wait for the checkpoint thread to write the checkpoint file
       * then wake us up
       */

      DPRINTF("thread %d suspending\n", thread -> tid);
      while (mtcp_state_value(&thread -> state) == ST_SUSPENDED) {
        mtcp_state_futex (&(thread -> state), FUTEX_WAIT, ST_SUSPENDED, NULL);
      }

      /* Maybe there is to be a checkpoint verification.  If so, and we're the
       * main thread, exec the restore program.  If so and we're not the main
       * thread, exit.
       */

      if ((mtcp_verify_total != 0) && (mtcp_verify_count == 0)) {

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
      is_restart = 1;
      if (!mtcp_state_set (&(thread -> state), ST_RUNENABLED, ST_SUSPENDED))
	mtcp_abort ();  // checkpoint was written when thread in SUSPENDED state
      wait_for_all_restored(thread);
      DPRINTF("thread %d restored\n", thread -> tid);

      if (thread == motherofall && mtcp_restore_verify) {
        /* If we're a restore verification, rename the temp file over the
         * permanent one
	 */
        mtcp_rename_ckptfile(temp_checkpointfilename, perm_checkpointfilename);
      }

    }
    DPRINTF("tid %d returning to %p\n",
            mtcp_sys_kernel_gettid (), __builtin_return_address (0));

    if (callback_pre_resume_user_thread != NULL) {
      callback_pre_resume_user_thread(is_restart);
    }
  }
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

static void wait_for_all_restored (Thread *thisthread)

{
  int rip;

  // dec number of threads cloned but have not completed longjmp/getcontext
  do {
    rip = mtcp_state_value(&restoreinprog);
  } while (!mtcp_state_set (&restoreinprog, rip - 1, rip));

  if (thisthread == ckpthread) {
    // Wait for all other threads to reach this function
    while (mtcp_state_value(&restoreinprog) != 1) {
      const struct timespec timeout = {(time_t) 0, (long)10 * 1000};
      nanosleep(&timeout, NULL);
    }

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

    if (callback_post_ckpt != NULL) {
        DPRINTF("before callback_post_ckpt(1=restarting) (&%x,%x) \n",
                &callback_post_ckpt, callback_post_ckpt);
        (*callback_post_ckpt)(1, mtcp_restore_argv_start_addr);
        DPRINTF("after callback_post_ckpt(1=restarting)\n");
    }

    // Restore terminal settings.
    restore_term_settings();

    // NOTE:  This is last safe moment for hook.  All previous threads
    //   have executed the "else" and are waiting on the futex.
    //   This last thread has not yet unlocked the threads: unlk_threads()
    //   So, no race condition occurs.
    //   By comparison, *callback_post_ckpt() is called before creating
    //   additional threads.  Only first thread (usually motherofall) exists.
    /* If MTCP_RESTART_PAUSE set, sleep 15 seconds and allow gdb attach. */
    if (getenv("MTCP_RESTART_PAUSE")) {
      struct timespec delay = {15, 0}; /* 15 seconds */
      MTCP_PRINTF("Pausing 15 seconds. Do:  gdb %s %d\n",
                  progname_str, mtcp_sys_getpid());
      mtcp_sys_nanosleep(&delay, NULL);
    }
    /* Call weak symbol of this file, possibly overridden by the user's
     *   strong symbol.  User must compile his/her code with
     *   -Wl,-export-dynamic to make it visible.
     */
    mtcpHookRestart();

    lock_threads ();
    // if this was last of all, wake everyone up
    /* The restoreinprog is set to numThreads+1. At this point, all other
     * threads are in the else part. We do a FUTEX_WAKE for all those who are
     * doing FUTEX_WAIT. Any late comers are either executing FUTEX_WAIT or
     * busy waiting on restoreinprog to become 0. Thus, we set restoreinprog to
     * 0 before doing another FUTEX_WAKE.
     */
    mtcp_state_futex (&restoreinprog, FUTEX_WAKE, 999999999, NULL);
    if (!mtcp_state_set (&restoreinprog, 0, 1)) {
      MTCP_PRINTF("NOT REACHED\n");
      mtcp_abort();
    }
    mtcp_state_futex (&restoreinprog, FUTEX_WAKE, 999999999, NULL);
    unlk_threads (); // ... and release the thread list
  } else {
    // otherwise, wait for last of all to wake this one up
    while ((rip = mtcp_state_value(&restoreinprog)) > 0) {
      mtcp_state_futex (&restoreinprog, FUTEX_WAIT, rip, NULL);
    }
    while (mtcp_state_value(&restoreinprog) != 0) {
      const struct timespec timeout = {(time_t) 0, (long)100 * 1000};
      nanosleep(&timeout, NULL);
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
      MTCP_PRINTF("error getting signal mask: %s\n", strerror(errno));
      mtcp_abort ();
    }
  }
  // Save signal block mask
  if (pthread_sigmask (SIG_SETMASK, NULL, &(thisthread -> sigblockmask)) < 0) {
    MTCP_PRINTF("error getting signal mask: %s\n", strerror(errno));
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
  DPRINTF("restoring handlers for thread %d\n", thisthread->virtual_tid);
  if (pthread_sigmask (SIG_SETMASK, &(thisthread -> sigblockmask), NULL) < 0) {
    MTCP_PRINTF("error setting signal mask: %s\n", strerror(errno));
    mtcp_abort ();
  }

  // Raise the signals which were pending for only this thread at the time of
  // checkpoint.
  for (i = SIGRTMAX; i > 0; --i) {
    if (sigismember(&(thisthread -> sigpending), i)  == 1  &&
        sigismember(&(thisthread -> sigblockmask), i) == 1 &&
        sigismember(&(sigpending_global), i) == 0 &&
        i != STOPSIGNAL) {
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

    if (sigactions[i].sa_handler != SIG_DFL) {
      DPRINTF("saving signal handler (non-default) for %d -> %p\n",
              i, (sigactions[i].sa_flags & SA_SIGINFO ?
                  (void *)(sigactions[i].sa_sigaction) :
                  (void *)(sigactions[i].sa_handler)));
    }
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
#elif __x86_64__
  //asm volatile ("movl %%fs,%0" : "=m" (thisthread -> fs));
  //asm volatile ("movl %%gs,%0" : "=m" (thisthread -> gs));
#elif __arm__
  // Follow x86_64 for arm.
#endif

  memset (thisthread -> gdtentrytls, 0, sizeof thisthread -> gdtentrytls);

/* FIXME:  IF %fs IS NOT READ into thisthread -> fs AT BEGINNING OF THIS
 *   FUNCTION, HOW CAN WE REFER TO IT AS  thisthread -> TLSSEGREG?
 *   WHAT IS THIS CODE DOING?
 */
  i = thisthread -> TLSSEGREG / 8;
  thisthread -> gdtentrytls[0].entry_number = i;
  rc = mtcp_sys_get_thread_area (&(thisthread -> gdtentrytls[0]));
  if (rc == -1) {
    MTCP_PRINTF("error saving GDT TLS entry[%d]: %s\n",
                i, strerror(mtcp_sys_errno));
    mtcp_abort ();
  }
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
#elif __x86_64__
  /* q = a,b,c,d for i386; 8 low bits of r class reg for x86_64 */
  asm volatile ("movl %%fs,%0" : "=q" (tlssegreg));
#elif __arm__
  asm volatile ("mrc     p15, 0, %0, c13, c0, 3  @ load_tp_hard\n\t"
                : "=r" (tlssegreg) );
#endif
  return (int)tlssegreg;
}
static void *mtcp_get_tls_base_addr(void)
{
  struct user_desc gdtentrytls;

  gdtentrytls.entry_number = mtcp_get_tls_segreg() / 8;
  if ( mtcp_sys_get_thread_area ( &gdtentrytls ) == -1 ) {
    MTCP_PRINTF("error getting GDT TLS entry: %s\n", strerror(mtcp_sys_errno));
    mtcp_abort ();
  }
  return (void *)(*(unsigned long *)&(gdtentrytls.base_addr));
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
  Thread *next_thread;

  lock_threads();

  tid = mtcp_sys_kernel_gettid();
  for (thread = threads; thread != NULL; thread = next_thread) {
    next_thread = thread -> next;
    if (thread -> tid == tid) {
      unlk_threads ();
      return (thread);
    }
  }
  MTCP_PRINTF("can't find thread id %d\n", tid);
  mtcp_abort();
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
  if (threads_lock_owner != -1) {
    MTCP_PRINTF("Internal Error: Not Reached! \n");
    mtcp_abort();
  }
  threads_lock_owner = mtcp_sys_kernel_gettid();
}

static void unlk_threads (void)
{
  WMB; // flush data written before unlocking
  if (threads_lock_owner != mtcp_sys_kernel_gettid()) {
    MTCP_PRINTF("Error: Thread not holding lock, yet trying to unlock it! \n");
    mtcp_abort();
  }
  threads_lock_owner = -1;
  if (mtcp_state_set(&threadslocked , 0, 1) == 0) {
    MTCP_PRINTF("Error releasing threads lock! \n");
    mtcp_abort();
  }
  mtcp_state_futex (&threadslocked, FUTEX_WAKE, 1, NULL);
}

static int is_thread_locked (void)
{
  return mtcp_state_value(&threadslocked) &&
         threads_lock_owner == mtcp_sys_kernel_gettid();
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
static int should_mmap_ckpt_image = 0;
void mtcp_restore_start (int fd, int verify, int mmap_ckpt_image,
                         pid_t gzip_child_pid,
                         char *ckpt_newname, char *cmd_file,
                         char *argv[], char *envp[] )
{
#ifndef __x86_64__
  int i;
  char *strings = STRINGS;
#endif
  should_mmap_ckpt_image = mmap_ckpt_image;

  DEBUG_RESTARTING = 1;
  /* If we just replace extendedStack by (tempstack+STACKSIZE) in "asm"
   * below, the optimizer generates non-PIC code if it's not -O0 - Gene
   */
  long long * extendedStack = tempstack + STACKSIZE;

  /* Not used until we do longjmp/getcontext, but get it out of way now
   * restoreinprog needs to be set to 1 + numThreads to use in
   * wait_for_all_restored().
   */
  mtcp_state_init(&restoreinprog, 2);

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
	  mtcp_strcpy(strings,y); \
	  x = strings; \
	  strings += mtcp_strlen(y) + 1; \
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

#if defined(__i386__) || defined(__x86_64__)
  asm volatile (CLEAN_FOR_64_BIT(mov %0,%%esp\n\t)
                /* This next assembly language confuses gdb,
		   but seems to work fine anyway */
                CLEAN_FOR_64_BIT(xor %%ebp,%%ebp\n\t)
                : : "g" (extendedStack) : "memory");
#elif defined(__arm__)
  asm volatile ("mov sp,%0\n\t"
                /* FIXME:  DO WE NEED THIS "xor"? */
                // CLEAN_FOR_64_BIT(xor %%ebp,%%ebp\n\t)
                : : "r" (extendedStack) : "memory");
#else
# error "assembly instruction not translated"
#endif

  /* Once we're on the new stack, we can't access any local variables or
   * parameters.
   * Call the restoreverything to restore files and memory areas
   */

  /* This should never return */
  mtcp_restoreverything(should_mmap_ckpt_image, (void*)mtcp_finishrestore);
  mtcp_abort();
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
    size_t oldsize = mtcp_saved_break - mtcp_saved_heap_start;
    size_t newsize = current_break - mtcp_saved_heap_start;

    void* addr = mtcp_sys_mremap (mtcp_saved_heap_start, oldsize, newsize, 0);
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

void mtcp_finishrestore (void)
{
  struct timeval stopped;

  DPRINTF("mtcp_printf works; libc should work\n");

  restore_heap();

  if (mtcp_strlen(mtcp_ckpt_newname) > 0 &&
      mtcp_strcmp(mtcp_ckpt_newname, perm_checkpointfilename)) {
    // we start from different place - change it!
    DPRINTF("checkpoint file name was changed\n");
    update_checkpoint_filename(mtcp_ckpt_newname);
  }

  mtcp_sys_gettimeofday (&stopped, NULL);
  stopped.tv_usec += (stopped.tv_sec - restorestarted.tv_sec) * 1000000
                         - restorestarted.tv_usec;
  TPRINTF (("mtcp mtcp_finishrestore*: time %u uS\n", stopped.tv_usec));

  /* Now we can access all our files and memory that existed at the time of the
   * checkpoint.
   * We are still on the temporary stack, though.
   */

  /* Fill in the new mother process id */
  motherpid = mtcp_sys_getpid();

  /* Call another routine because our internal stack is whacked and we can't
   * have local vars.
   */

  ///JA: v54b port
  // so restarthread will have a big stack
#if defined(__i386__) || defined(__x86_64__)
  asm volatile (CLEAN_FOR_64_BIT(mov %0,%%esp)
# if 1
		: : "g" (motherofall->saved_sp - 128) //-128 for red zone
# else
// remove this and code associated with JMPBUF_SP when other version is robust.
		: : "g" (motherofall->JMPBUF_SP - 128) //-128 for red zone
# endif
                : "memory");
#elif defined(__arm__)
  asm volatile ("mov sp,%0"
# if 1
		: : "r" (thread->saved_sp - 128) //-128 for red zone
# else
// remove this and code associated with JMPBUF_SP when other version is robust.
		: : "r" (motherofall->JMPBUF_SP - 128) //-128 for red zone
# endif
                : "memory");
#else
#  error "assembly instruction not translated"
#endif
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

    setup_sig_handler(&stopthisthread);

    set_tid_address (&(thread -> child_tid));
    /* Do it once only, in motherofall thread. */

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

    ///JA: v54b port
    errno = -1;

    void *clone_arg = child;

    /*
     * DMTCP needs to know virtual_tid of the thread being recreated by the
     *  following clone() call.
     *
     * Threads are created by using syscall which is intercepted by DMTCP and
     *  the virtual_tid is sent to DMTCP as a field of MtcpRestartThreadArg
     *  structure. DMTCP will automatically extract the actual argument
     *  (clone_arg -> arg) from clone_arg and will pass it on to the real
     *  clone call.
     *                                                           (--Kapil)
     */
    if (dmtcp_info_pid_virtualization_enabled == 1) {
      mtcpRestartThreadArg.arg = child;
      mtcpRestartThreadArg.virtual_tid = child -> virtual_tid;
      clone_arg = &mtcpRestartThreadArg;
    }

    /* Create the thread so it can finish restoring itself. */
    pid_t tid = (*clone_entry)(restarthread,
                               // -128 for red zone
#if 1
                               (void*)(child -> saved_sp - 128), // -128 for red zone
#else
// remove this and code associated with JMPBUF_SP when other version is robust.
                               (void*)(child -> JMPBUF_SP - 128), // -128 for red zone
#endif
                               /* Don't do CLONE_SETTLS (it'll puke).  We do it
                                * later via restore_tls_state. */
                               ((child -> clone_flags & ~CLONE_SETTLS) |
                                CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID),
                               clone_arg, child -> parent_tidptr, NULL,
                               child -> actual_tidptr);

    if (tid < 0) {
      MTCP_PRINTF("error %d recreating thread\n", errno);
      MTCP_PRINTF("clone_flags %X, savedsp %p\n",
                   child -> clone_flags, child -> JMPBUF_SP);
      mtcp_abort ();
    }
    DPRINTF("Parent:%d, tid of newly created thread:%d\n", thread->tid, tid);
  }

  /* All my children have been created, jump to the stopthisthread routine just
   * after sigsetjmp/getcontext call.
   * Note that if this is the restored checkpointhread, it jumps to the
   * checkpointhread routine
   */

  if (mtcp_have_thread_sysinfo_offset())
    mtcp_set_thread_sysinfo(saved_sysinfo);
  ///JA: v54b port
#ifdef SETJMP
  DPRINTF("calling siglongjmp: thread->tid: %d, virtual_tid:%d\n",
          thread->tid, thread->virtual_tid);
  siglongjmp (thread -> jmpbuf, 1); /* Shouldn't return */
#else
  DPRINTF("calling setcontext: thread->tid: %d, virtual_tid:%d\n",
          thread->tid, thread->virtual_tid);
  setcontext (&(thread -> savctx)); /* Shouldn't return */
#endif
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

  rc = mtcp_sys_set_thread_area (&(thisthread -> gdtentrytls[0]));
  if (rc < 0) {
    MTCP_PRINTF("error %d restoring GDT TLS entry[%d]\n",
                mtcp_sys_errno, thisthread -> gdtentrytls[0].entry_number);
    mtcp_abort ();
  }

  /* Restore the rest of the stuff */

#ifdef __i386__
  asm volatile ("movw %0,%%fs" : : "m" (thisthread -> fs));
  asm volatile ("movw %0,%%gs" : : "m" (thisthread -> gs));
#elif __x86_64__
/* Don't directly set fs.  It would only set 32 bits, and we just
 *  set the full 64-bit base of fs, using sys_set_thread_area,
 *  which called arch_prctl.
 *asm volatile ("movl %0,%%fs" : : "m" (thisthread -> fs));
 *asm volatile ("movl %0,%%gs" : : "m" (thisthread -> gs));
 */
#elif __arm__
/* ARM treats this same as x86_64 above. */
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

static void setup_sig_handler (sighandler_t handler)

{
  struct sigaction act, old_act;

  memset(&act, 0, sizeof act);
  memset(&act, 0, sizeof act);
  act.sa_handler = handler;
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
    MTCP_PRINTF("error opening /proc/self/maps: %s\n",strerror(mtcp_sys_errno));
    mtcp_abort ();
  }

  while (mtcp_readmapsline (mapsfd, &area, NULL)) {
    /* Skip anything that has no read or execute permission.  This occurs on one
     * page in a Linux 2.6.9 installation.  No idea why.  This code would also
     * take care of kernel sections since we don't have read/execute permission
     * there.
     */

    if (!((area.prot & PROT_READ) || (area.prot & PROT_WRITE))) continue;

    if (!(area.flags & MAP_SHARED)) continue;

    if (mtcp_strendswith(area.name, DELETED_FILE_SUFFIX)) continue;

#ifdef IBV
    // TODO: Don't checkpoint infiniband shared area for now.
   if (mtcp_strstartswith(area.name, INFINIBAND_SHMEM_FILE)) {
     continue;
   }
#endif

    DPRINTF("syncing %X at %p from %s + %X\n",
            area.size, area.addr, area.name, area.offset);

    if ( msync(area.addr, area.size, MS_SYNC) < 0 ){
      MTCP_PRINTF("error syncing %X at %p from %s + %X\n",
                   area.size, area.addr, area.name, area.offset);
      mtcp_abort();
    }
  }

  mtcp_sys_close (mapsfd);
}
