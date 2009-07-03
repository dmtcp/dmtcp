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

/********************************************************************************************************************************/
/*																*/
/*  Multi-threaded checkpoint library												*/
/*																*/
/*  Link this in as part of your program that you want checkpoints taken							*/
/*  Call the mtcp_init routine at the beginning of your program									*/
/*  Call the mtcp_ok routine when it's OK to do checkpointing									*/
/*  Call the mtcp_no routine when you want checkpointing inhibited								*/
/*																*/
/*  This module also contains a __clone wrapper routine										*/
/*																*/
/********************************************************************************************************************************/


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
#include <termios.h>
#include <unistd.h>
#include <termios.h>       // for tcdrain
#include <ucontext.h>
#include <sys/types.h>     // for gettid, tkill, waitpid
#include <sys/wait.h>	   // for waitpid
#include <linux/unistd.h>  // for gettid, tkill

#define MTCP_SYS_STRCPY
#define MTCP_SYS_STRLEN
#include "mtcp_internal.h"

static int WAIT=1;
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
#define REG_RSP 15
#define SAVEDSP uc_mcontext.gregs[REG_RSP]
#else
#define REG_ESP 7
#define SAVEDSP uc_mcontext.gregs[REG_ESP]
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
 *  void *__padding[16];
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
 */
#define TLS_PID_OFFSET \
	 (18*sizeof(void *)+sizeof(pid_t))  // offset of pid in pthread struct
#define TLS_TID_OFFSET (18*sizeof(void *))  // offset of tid in pthread struct

sem_t sem_start;

typedef struct Thread Thread;

struct Thread { Thread *next;                       // next thread in 'threads' list
                Thread **prev;                      // prev thread in 'threads' list
                int tid;                            // this thread's id as returned by mtcp_sys_kernel_gettid ()
                MtcpState state;                    // see ST_... below
                Thread *parent;                     // parent thread (or NULL if top-level thread)
                Thread *children;                   // one of this thread's child threads
                Thread *siblings;                   // one of this thread's sibling threads

                int clone_flags;                    // parameters to __clone that created this thread
                int *parent_tidptr;
                int *given_tidptr;                  // (this is what __clone caller passed in)
                int *actual_tidptr;                 // (this is what we passed to the system call, either given_tidptr or &child_tid)
                int child_tid;                      // this is used for child_tidptr if the original call did not
                                                    // ... have both CLONE_CHILD_SETTID and CLONE_CHILD_CLEARTID
                int (*fn) (void *arg);              // thread's initial function entrypoint and argument
                void *arg;

                sigset_t sigblockmask;              // blocked signals
                struct sigaction sigactions[NSIG];  // signal handlers

                ///JA: new code ported from v54b
                ucontext_t savctx;                  // context saved on suspend

                mtcp_segreg_t fs, gs;               // thread local storage pointers
#if MTCP__SAVE_MANY_GDT_ENTRIES
                struct user_desc gdtentrytls[GDT_ENTRY_TLS_ENTRIES];
#else
                struct user_desc gdtentrytls[1];
#endif
              };

#define ST_RUNDISABLED 0     // thread is running normally but with checkpointing disabled
#define ST_RUNENABLED 1      // thread is running normally and has checkpointing enabled
#define ST_SIGDISABLED 2     // thread is running normally with cp disabled, but checkpoint thread is waiting for it to enable
#define ST_SIGENABLED 3      // thread is running normally with cp enabled, and checkpoint thread has signalled it to stop
#define ST_SUSPINPROG 4      // thread context being saved (very brief)
#define ST_SUSPENDED 5       // thread is suspended waiting for checkpoint to complete
#define ST_CKPNTHREAD 6      // thread is the checkpointing thread (special state just for that thread)

	/* Global data */

void *mtcp_libc_dl_handle = NULL;  // dlopen handle for whatever libc.so is loaded with application program
Area mtcp_libc_area;               // some area of that libc.so

	/* Static data */

static char const *nscd_mmap_str = "/var/run/nscd/";
static char const *nscd_mmap_str2 = "/var/cache/nscd";
//static char const *perm_checkpointfilename = NULL;
//static char const *temp_checkpointfilename = NULL;
static char perm_checkpointfilename[MAXPATHLEN];
static char temp_checkpointfilename[MAXPATHLEN];
static unsigned long long checkpointsize;
static int intervalsecs;
static pid_t motherpid;
static int restore_size;
static int showtiming;
static int threadenabledefault;
static int verify_count;  // number of checkpoints to go
static int verify_total;  // value given by envar
static pid_t mtcp_ckpt_gzip_child_pid = -1;
static int volatile checkpointhreadstarting = 0;
static MtcpState restoreinprog = MTCP_STATE_INITIALIZER;
static MtcpState threadslocked = MTCP_STATE_INITIALIZER;
static pthread_t checkpointhreadid;
static struct timeval restorestarted;
static int DEBUG_RESTARTING = 0;
static Thread *motherofall = NULL;
static Thread *threads = NULL;
static VA restore_begin, restore_end;
static void *restore_start; /* will be bound to fnc, mtcp_restore_start */
static void *saved_sysinfo;
static struct termios saved_termios;
static int saved_termios_exists = 0;
static void (*callback_sleep_between_ckpt)(int sec) = NULL;
static void (*callback_pre_ckpt)() = NULL;
static void (*callback_post_ckpt)(int is_restarting) = NULL;
static int  (*callback_ckpt_fd)(int fd) = NULL;
static void (*callback_write_ckpt_prefix)(int fd) = NULL;

static int (*clone_entry) (int (*fn) (void *arg),
                           void *child_stack,
                           int flags,
                           void *arg,
                           int *parent_tidptr,
                           struct user_desc *newtls,
                           int *child_tidptr);
static int (*execvp_entry) (const char *path, char *const argv[]);

/* temp stack used internally by restore so we don't go outside the
 *   mtcp.so address range for anything;
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
static int test_use_compression(void);
static int open_ckpt_to_write(int fd, int pipe_fds[2], char *gzip_path);
static void checkpointeverything (void);
static void writefiledescrs (int fd);
static void writememoryarea (int fd, Area *area,
			     int stack_was_seen, int vsyscall_exists);
static void writecs (int fd, char cs);
static void writefile (int fd, void const *buff, int size);
static void stopthisthread (int signum);
static void wait_for_all_restored (void);
static void save_sig_state (Thread *thisthread);
static void save_tls_state (Thread *thisthread);
static void renametempoverperm (void);
static Thread *getcurrenthread (void);
static void lock_threads (void);
static void unlk_threads (void);
static int readmapsline (int mapsfd, Area *area);
static void finishrestore (void);
static int restarthread (void *threadv);
static void restore_tls_state (Thread *thisthread);
static void setup_sig_handler (void);
static void sync_shared_mem(void);

typedef void (*sighandler_t)(int);

sighandler_t _real_signal(int signum, sighandler_t handler){
  return signal(signum, handler);
}
int _real_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact){
  return sigaction(signum, act, oldact);
}
int _real_sigprocmask(int how, const sigset_t *set, sigset_t *oldset){
  return sigprocmask(how, set, oldset);
}



/********************************************************************************************************************************/
/*																*/
/*  This routine must be called at startup time to initiate checkpointing							*/
/*																*/
/*    Input:															*/
/*																*/
/*	checkpointfilename = name to give the checkpoint file									*/
/*	interval = interval, in seconds, to write the checkpoint file								*/
/*	clonenabledefault = 0 : clone checkpointing blocked by default (call mtcp_ok in the thread to enable)			*/
/*	                    1 : clone checkpointing enabled by default (call mtcp_no in the thread to block if you want)	*/
/*																*/
/*	envar MTCP_WRAPPER_LIBC_SO = what library to use for inner wrappers (default libc.??.so)				*/
/*	envar MTCP_VERIFY_CHECKPOINT = every n checkpoints, verify by doing a restore to resume					*/
/*	                               default is 0, ie, don't ever verify							*/
/*																*/
/********************************************************************************************************************************/
/* These hook functions provide an alternative to DMTCP callbacks, using
 * weak symbols.  While MTCP is immature, let's allow both, in case
 * the flexibility of a second hook mechanism is useful in the future.
 * The mechanism is invisible unless end user compiles w/ -Wl,-export-dynamic
 */
__attribute__ ((weak)) void mtcpHookPreCheckpoint( void ) { }

__attribute__ ((weak)) void mtcpHookPostCheckpoint( void ) { }

__attribute__ ((weak)) void mtcpHookRestart( void ) { }


void mtcp_init (char const *checkpointfilename, int interval, int clonenabledefault)
{
  char *p, *tmp, *endp;
  pid_t tls_pid, tls_tid;
  int len;
  Thread *thread;
  mtcp_segreg_t TLSSEGREG;

  if (sizeof(void *) != sizeof(long)) {
    mtcp_printf("ERROR: sizeof(void *) != sizeof(long) on this architecture.\n"
	   "       This code assumes they are equal.\n");
    mtcp_abort ();
  }

  /* Nobody else has a right to preload on internal processes generated
   * by mtcp_check_XXX() -- not even DMTCP, if it's currently operating.
   *
   * Saving LD_PRELOAD in a temp env var and restoring it later --Kapil.
   *
   * TODO: To insert some sort of error checking to make sure that we
   *       are correctly setting LD_PRELOAD after we are done with
   *       vdso check.
   */

#ifndef __x86_64__
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

  strncpy(perm_checkpointfilename,checkpointfilename,MAXPATHLEN);  // this is what user wants the checkpoint file called
  len = strlen (perm_checkpointfilename);        // make up another name, same as that, with ".temp" on the end
  memcpy(temp_checkpointfilename, perm_checkpointfilename, len);
  strncpy(temp_checkpointfilename + len, ".temp",MAXPATHLEN-len);
                                                 // ... we use it to write to in case we crash while writing
                                                 //     we will leave the previous good one intact

  DPRINTF (("mtcp_init*: main tid %d\n", mtcp_sys_kernel_gettid ()));

  threadenabledefault = clonenabledefault;       // save this away where it's easy to get

  p = getenv ("MTCP_SHOWTIMING");
  showtiming = ((p != NULL) && (*p & 1));

  /* Maybe dump out some stuff about the TLS */

  mtcp_dump_tls (__FILE__, __LINE__);

  /* Save this process's pid.  Then verify that the TLS has it where it should be.           */
  /* When we do a restore, we will have to modify each thread's TLS with the new motherpid. */
  /* We also assume that GS uses the first GDT entry for its descriptor.                    */

#ifdef __i386__
  asm volatile ("movw %%gs,%0" : "=g" (TLSSEGREG)); /* any general register */
#endif
#ifdef __x86_64__
  asm volatile ("movl %%fs,%0" : "=q" (TLSSEGREG)); /* q = a,b,c,d for i386; 8 low bits of r class reg for x86_64 */
#endif
#if MTCP__SAVE_MANY_GDT_ENTRIES
  if (TLSSEGREG / 8 != GDT_ENTRY_TLS_MIN) {
    mtcp_printf ("mtcp_init: gs %X not set to first TLS GDT ENTRY %X\n",
                 gs, GDT_ENTRY_TLS_MIN * 8 + 3);
    mtcp_abort ();
  }
#endif

  motherpid = mtcp_sys_getpid (); /* libc/getpid can lie if we had
				   * used kernel fork() instead of libc fork().
				   */
#ifdef __i386__
  asm volatile ("movl %%gs:%c1,%0" : "=r" (tls_pid) : "i" (TLS_PID_OFFSET));
  asm volatile ("movl %%gs:%c1,%0" : "=r" (tls_tid) : "i" (TLS_TID_OFFSET));
#endif
#ifdef __x86_64__
  asm volatile ("movl %%fs:%c1,%0" : "=r" (tls_pid) : "i" (TLS_PID_OFFSET));
  asm volatile ("movl %%fs:%c1,%0" : "=r" (tls_tid) : "i" (TLS_TID_OFFSET));
#endif
  if ((tls_pid != motherpid) || (tls_tid != motherpid)) {
    mtcp_printf ("mtcp_init: getpid %d, tls pid %d, tls tid %d, must all match\n", motherpid, tls_pid, tls_tid);
    mtcp_abort ();
  }

  /* Get verify envar */

  tmp = getenv ("MTCP_VERIFY_CHECKPOINT");
  verify_total = 0;
  if (tmp != NULL) {
    verify_total = strtol (tmp, &p, 0);
    if ((*p != '\0') || (verify_total < 0)) {
      mtcp_printf ("mtcp_init: bad MTCP_VERIFY_CHECKPOINT %s\n", tmp);
      mtcp_abort ();
    }
  }

  /* If the user has defined a signal, use that to suspend.  Otherwise, use MTCP_DEFAULT_SIGNAL */

  tmp = getenv("MTCP_SIGCKPT");
  if(tmp == NULL)
      STOPSIGNAL = MTCP_DEFAULT_SIGNAL;
  else
  {
      errno = 0;
      STOPSIGNAL = strtol(tmp, &endp, 0);

      if((errno != 0) || (tmp == endp))
      {
          mtcp_printf("mtcp_init: Your chosen SIGCKPT of \"%s\" does not "
                        "translate to a number,\n"
			"  and cannot be used.  Signal %d "
                        "will be used instead.\n", tmp, MTCP_DEFAULT_SIGNAL);
          STOPSIGNAL = MTCP_DEFAULT_SIGNAL;
      }
      else if(STOPSIGNAL < 1 || STOPSIGNAL > 31)
      {
          mtcp_printf("mtcp_init: Your chosen SIGCKPT of \"%d\" is not a valid "
                        "signal, and cannot be used.\n"
			"  Signal %d will be used instead.\n",
		       STOPSIGNAL, MTCP_DEFAULT_SIGNAL);
          STOPSIGNAL = MTCP_DEFAULT_SIGNAL;
      }
  }

  /* Get size and address of the shareable - used to separate it from the rest of the stuff */
  /* All routines needed to perform restore must be within this address range               */

  restore_begin = (((VA)mtcp_shareable_begin) & -PAGE_SIZE);
  restore_size  = ((VA)mtcp_shareable_end - restore_begin + PAGE_SIZE - 1) & -PAGE_SIZE;
  restore_end   = restore_begin + restore_size;
  restore_start = mtcp_restore_start;

  /* Setup clone_entry to point to glibc's __clone routine */

  setup_clone_entry ();

  /* Set up caller as one of our threads so we can work on it */

  thread = malloc (sizeof *thread);
  memset (thread, 0, sizeof *thread);
  setupthread (thread);
  thread -> child_tid = mtcp_sys_kernel_gettid (); // need to set this up so the checkpointhread can see we haven't exited
  set_tid_address (&(thread -> child_tid));  // we are assuming mtcp_init has been called before application may have called set_tid_address
                                             // ... or else we will end up overwriting that set_tid_address value
  motherofall = thread;

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
    mtcp_printf ("mtcp_init: error creating checkpoint thread: %s\n", strerror (errno));
    mtcp_abort ();
  }
  if (checkpointhreadstarting) mtcp_abort ();  // make sure the clone wrapper executed (ie, not just the standard clone)
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

/********************************************************************************************************************************/
/*																*/
/*  The routine mtcp_set_callbacks below may be called BEFORE the first
 *  MTCP checkpoint, to add special functionality to checkpointing
 */
/*    Its arguments (callback functions) are:  */
//
// sleep_between_ckpt:  Called in between checkpoints to replace the default "sleep(sec)" functionality,
//                      when this function returns checkpoint will start
// pre_ckpt:            Called after all user threads are suspended, but BEFORE checkpoint written
// post_ckpt:           Called after checkpoint, and after restore.  is_restarting will be 1 for restore 0 for after checkpoint
// ckpt_fd:             Called to test if mtcp should checkpoint a given FD returns 1 if it should
//
/********************************************************************************************************************************/
void mtcp_set_callbacks(void (*sleep_between_ckpt)(int sec),
                        void (*pre_ckpt)(),
                        void (*post_ckpt)(int is_restarting),
                        int  (*ckpt_fd)(int fd),
                        void (*write_ckpt_prefix)(int fd))
{
    callback_sleep_between_ckpt = sleep_between_ckpt;
    callback_pre_ckpt = pre_ckpt;
    callback_post_ckpt = post_ckpt;
    callback_ckpt_fd = ckpt_fd;
    callback_write_ckpt_prefix = write_ckpt_prefix;
}

/*************************************************************************/
/*						                         */
/*  Dump out the TLS stuff pointed to by %gs	                         */
/*						                         */
/*************************************************************************/

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
  mtcp_printf("mtcp_init: gs=%X at %s:%d\n", gs, file, line);
  if (gs != 0) {

    /* We only handle GDT based stuff */

    if (gs & 4) mtcp_printf("   *** part of LDT\n");

    /* It's in the GDT */

    else {

      /* Read the TLS descriptor */

      gdtentry.entry_number = gs / 8;
      i = mtcp_sys_get_thread_area (&gdtentry);
      if (i < 0) mtcp_printf("  error getting GDT entry %d: %d\n", gdtentry.entry_number, mtcp_sys_errno);
      else {

        /* Print out descriptor and first 80 bytes of data */

        mtcp_printf("  limit %X, baseaddr %X\n", gdtentry.limit, gdtentry.base_addr);
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
        mtcp_printf("mtcp_init: getpid=%d, gettid=%d, tls=%d\n", getpid (), mtcp_sys_kernel_gettid (), i);
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

/*****************************************************************************/
/*									     */
/*  This is our clone system call wrapper				     */
/*									     */
/*    Note:								     */
/*									     */
/*      pthread_create eventually calls __clone to create threads	     */
/*      It uses flags = 0x7D0F00:					     */
/*	      CLONE_VM = VM shared between processes		             */
/*	      CLONE_FS = fs info shared between processes (root, cwd, umask) */
/*	   CLONE_FILES = open files shared between processes (fd table)	     */
/*	 CLONE_SIGHAND = signal handlers and blocked signals shared	     */
/*	 			 (sigaction common to parent and child)      */
/*	  CLONE_THREAD = add to same thread group			     */
/*	 CLONE_SYSVSEM = share system V SEM_UNDO semantics		     */
/*	  CLONE_SETTLS = create a new TLS for the child from newtls parameter*/
/*	 CLONE_PARENT_SETTID = set the TID in the parent (before MM copy)    */
/*	CLONE_CHILD_CLEARTID = clear the TID in the child and do a	     */
/*				 futex wake at that address		     */
/*	      CLONE_DETACHED = create clone detached			     */
/*									     */
/*****************************************************************************/

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

    DPRINTF (("mtcp wrapper clone*: calling clone thread=%p,"
	      " fn=%p, flags=0x%X\n", thread, fn, flags));
    DPRINTF (("mtcp wrapper clone*: parent_tidptr=%p, newtls=%p,"
	      " child_tidptr=%p\n", parent_tidptr, newtls, child_tidptr));
    //asm volatile ("int3");

    /* Save exactly what the caller is supplying */

    thread -> clone_flags   = flags;
    thread -> parent_tidptr = parent_tidptr;
    thread -> given_tidptr  = child_tidptr;

    /* We need the CLEARTID feature so we can detect			     */
    /*   when the thread has exited					     */
    /* So if the caller doesn't want it, we enable it                        */
    /* Retain what the caller originally gave us so we can pass the tid back */

    if (!(flags & CLONE_CHILD_CLEARTID)) {
      child_tidptr = &(thread -> child_tid);
    }
    thread -> actual_tidptr = child_tidptr;
    DPRINTF (("mtcp wrapper clone*: thread %p -> actual_tidptr %p\n",
	      thread, thread -> actual_tidptr));

    /* Alter call parameters, forcing CLEARTID and make it call the wrapper routine */

    flags |= CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID;
    fn = threadcloned;
    arg = thread;
  }

  /* mtcp_init not called, no checkpointing, but make sure clone_entry is */
  /* set up so we can call the real clone                                 */

  else if (clone_entry == NULL) setup_clone_entry ();

  /* Now create the thread */

  DPRINTF (("mtcp wrapper clone*: clone fn=%p, child_stack=%p, flags=%X, arg=%p\n", fn, child_stack, flags, arg));
  DPRINTF (("mtcp wrapper clone*: parent_tidptr=%p, newtls=%p, child_tidptr=%p\n", parent_tidptr, newtls, child_tidptr));
  rc = (*clone_entry) (fn, child_stack, flags, arg, parent_tidptr, newtls, child_tidptr);
  if (rc < 0) {
    DPRINTF (("mtcp wrapper clone*: clone rc=%d, errno=%d\n", rc, errno));
  } else {
    DPRINTF (("mtcp wrapper clone*: clone rc=%d\n", rc));
  }

  return (rc);
}

asm (".global clone ; .type clone,@function ; clone = __clone");

/********************************************************************************************************************************/
/*																*/
/*  This routine is called (via clone) as the top-level routine of a thread that we are tracking.				*/
/*																*/
/*  It fills in remaining items of our thread struct, calls the user function, then cleans up the thread struct before exiting.	*/
/*																*/
/********************************************************************************************************************************/

static int threadcloned (void *threadv)

{
  int rc;
  pid_t tls_pid;
  Thread *const thread = threadv;
  mtcp_segreg_t TLSSEGREG;

  DPRINTF (("mtcp threadcloned*: starting thread %p\n", thread));

  setupthread (thread);

  /* The new TLS should have the process ID in place at TLS_PID_OFFSET */
  /* This is a verification step and is therefore optional as such     */

#ifdef __i386__
  asm volatile ("mov %%gs,%0" : "=g" (TLSSEGREG));
#endif
#ifdef __x86_64__
  asm volatile ("mov %%fs,%0" : "=g" (TLSSEGREG));
#endif
#if MTCP__SAVE_MANY_GDT_ENTRIES
  if (TLSSEGREG / 8 != GDT_ENTRY_TLS_MIN) {
    mtcp_printf ("mtcp threadcloned: gs/fs %X not set to first TLS GDT ENTRY %X\n", TLSSEGREG, GDT_ENTRY_TLS_MIN * 8 + 3);
    mtcp_abort ();
  }
#endif

#ifdef __i386__
  asm volatile ("movl %%gs:%c1,%0" : "=r" (tls_pid) : "i" (TLS_PID_OFFSET));
#endif
#ifdef __x86_64__
  asm volatile ("movl %%fs:%c1,%0" : "=r" (tls_pid) : "i" (TLS_PID_OFFSET));
#endif
  if ((tls_pid != motherpid) && (tls_pid != (pid_t)-1)) {
    mtcp_printf ("mtcp threadcloned: getpid %d, tls pid %d at offset %d, must match\n", motherpid, tls_pid, TLS_PID_OFFSET);
    mtcp_printf ("      %X\n", motherpid);
    for (rc = 0; rc < 256; rc += 4) {
      asm volatile ("movl %%gs:(%1),%0" : "=r" (tls_pid) : "r" (rc));
      mtcp_printf ("   %d: %X", rc, tls_pid);
      if ((rc & 31) == 28) mtcp_printf ("\n");
    }
    mtcp_abort ();
  }

  /* If the caller wants the child tid but didn't have CLEARTID, pass the tid back to it */

  if ((thread -> clone_flags & (CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID)) == CLONE_CHILD_SETTID) {
    *(thread -> given_tidptr) = thread -> child_tid;
  }

  /* Maybe enable checkpointing by default */

  if (threadenabledefault) mtcp_ok ();

  /* Call the user's function for whatever processing they want done */

  DPRINTF (("mtcp threadcloned*: calling %p (%p)\n", thread -> fn, thread -> arg));
  rc = (*(thread -> fn)) (thread -> arg);
  DPRINTF (("mtcp threadcloned*: returned %d\n", rc));

  /* Make sure checkpointing is inhibited while we clean up and exit */
  /* Otherwise, checkpointer might wait forever for us to re-enable  */

  mtcp_no ();

  /* Do whatever to unlink and free thread block */

  threadisdead (thread);

  /* Return the user's status as the exit code */

  return (rc);
}

/********************************************************************************************************************************/
/*																*/
/*  set_tid_address wrapper routine												*/
/*																*/
/*  We save the new address of the tidptr that will get cleared when the thread exits						*/
/*																*/
/********************************************************************************************************************************/

static long set_tid_address (int *tidptr)

{
  long rc;
  Thread *thread;

  thread = getcurrenthread ();
  DPRINTF (("set_tid_address wrapper*: thread %p -> tid %d, tidptr %p\n", thread, thread -> tid, tidptr));
  thread -> actual_tidptr = tidptr;  // save new tidptr so subsequent restore will create with new pointer
  mtcp_sys_set_tid_address(tidptr);
  return (rc);                       // now we tell kernel to change it for the current thread
}

/********************************************************************************************************************************/
/*																*/
/*  Link thread struct to the lists and finish filling it in									*/
/*																*/
/*    Input:															*/
/*																*/
/*	thread = thread to set up												*/
/*																*/
/*    Output:															*/
/*																*/
/*	thread linked to 'threads' list and 'motherofall' tree									*/
/*	thread -> tid = filled in with thread id										*/
/*	thread -> state = ST_RUNDISABLED (thread initially has checkpointing disabled)						*/
/*	signal handler set up													*/
/*																*/
/********************************************************************************************************************************/

static void setupthread (Thread *thread)

{
  Thread *parent;

  /* Save the thread's ID number and put in threads list so we can look it up                                    */
  /* Set state to disable checkpointing so checkpointer won't race between adding to list and setting up handler */

  thread -> tid = mtcp_sys_kernel_gettid ();

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

  /* Set up signal handler so we can interrupt the thread for checkpointing */

  setup_sig_handler ();
}

/********************************************************************************************************************************/
/*																*/
/*  Set up 'clone_entry' variable												*/
/*																*/
/*    Output:															*/
/*																*/
/*	clone_entry = points to clone routine within libc.so									*/
/*																*/
/********************************************************************************************************************************/

static void setup_clone_entry (void)

{
  char *p, *tmp;
  int mapsfd;

  /* Get name of whatever concoction we have for a libc shareable image */
  /* This is used by the wrapper routines                               */

  tmp = getenv ("MTCP_WRAPPER_LIBC_SO");
  if (tmp != NULL) {
    strncpy (mtcp_libc_area.name, tmp, sizeof mtcp_libc_area.name);
  } else {
    mapsfd = mtcp_sys_open2 ("/proc/self/maps", O_RDONLY);
    if (mapsfd < 0) {
      mtcp_printf ("mtcp_init: error opening /proc/self/maps: %s\n", strerror (mtcp_sys_errno));
      mtcp_abort ();
    }
    p = NULL;
    while (readmapsline (mapsfd, &mtcp_libc_area)) {
      p = strstr (mtcp_libc_area.name, "/libc");
      if ((p != NULL) && ((p[5] == '-') || (p[5] == '.'))) break;
    }
    close (mapsfd);
    if (p == NULL) {
      mtcp_printf ("mtcp_init: cannot find */libc[-.]* in /proc/self/maps\n");
      mtcp_abort ();
    }
  }
  mtcp_libc_dl_handle = dlopen (mtcp_libc_area.name, RTLD_LAZY | RTLD_GLOBAL);
  if (mtcp_libc_dl_handle == NULL) {
    mtcp_printf ("mtcp_init: error opening libc shareable %s: %s\n", mtcp_libc_area.name, dlerror ());
    mtcp_abort ();
  }

  /* Find the clone routine therein */

  clone_entry = mtcp_get_libc_symbol ("__clone");
}

/********************************************************************************************************************************/
/*																*/
/*  Thread has exited - unlink it from lists and free struct									*/
/*																*/
/*    Input:															*/
/*																*/
/*	thread = thread that has exited												*/
/*																*/
/*    Output:															*/
/*																*/
/*	thread removed from 'threads' list and motherofall tree									*/
/*	thread pointer no longer valid												*/
/*	checkpointer woken if waiting for this thread										*/
/*																*/
/********************************************************************************************************************************/

static void threadisdead (Thread *thread)

{
  Thread **lthread, *parent, *xthread;

  lock_threads ();

  DPRINTF (("mtcp threadisdead*: thread %p -> tid %d\n", thread, thread -> tid));

  /* Remove thread block from 'threads' list */

  if ((*(thread -> prev) = thread -> next) != NULL) {
    thread -> next -> prev = thread -> prev;
  }

  /* Remove thread block from parent's list of children */

  parent = thread -> parent;
  if (parent != NULL) {
    for (lthread = &(parent -> children); (xthread = *lthread) != thread; lthread = &(xthread -> siblings)) {}
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

  /* If checkpointer is waiting for us, wake it to see this thread no longer in list */

  mtcp_state_futex (&(thread -> state), FUTEX_WAKE, 1, NULL);

  mtcp_state_destroy( &(thread -> state) );

  free (thread);
}

void *mtcp_get_libc_symbol (char const *name)

{
  void *temp;

  temp = dlsym (mtcp_libc_dl_handle, name);
  if (temp == NULL) {
    mtcp_printf ("mtcp_get_libc_symbol: error getting %s from %s: %s\n",
                 name, mtcp_libc_area.name, dlerror ());
    mtcp_abort ();
  }
  return (temp);
}

/********************************************************************************************************************************/
/*																*/
/*  Call this when it's OK to checkpoint											*/
/*																*/
/********************************************************************************************************************************/

int mtcp_ok (void)

{
  Thread *thread;

  if (getenv("MTCP_NO_CHECKPOINT"))
    return 0;
  thread = getcurrenthread ();

again:
  switch (mtcp_state_value(&thread -> state)) {

    /* Thread was running normally with checkpointing disabled.  Enable checkpointing then just return. */

    case ST_RUNDISABLED: {
      if (!mtcp_state_set (&(thread -> state), ST_RUNENABLED, ST_RUNDISABLED)) goto again;
      return (0);
    }

    /* Thread was running normally with checkpointing already enabled.  So just return as is. */

    case ST_RUNENABLED: {
      return (1);
    }

    /* Thread was running with checkpointing disabled, but the checkpointhread wants to write a checkpoint.  So mark the thread  */
    /* as having checkpointing enabled, then just 'manually' call the signal handler as if the signal to suspend were just sent. */

    case ST_SIGDISABLED: {
      if (!mtcp_state_set (&(thread -> state), ST_SIGENABLED, ST_SIGDISABLED)) goto again;
      stopthisthread (0);
      return (0);
    }

    /* Thread is running with checkpointing enabled, but the checkpointhread wants to write a checkpoint and has sent a signal */
    /* telling the thread to call 'stopthisthread'.  So we'll just keep going as is until the signal is actually delivered.    */

    case ST_SIGENABLED: {
      return (1);
    }

    /* Thread is the checkpointhread so we just ignore the call (from threadcloned routine). */

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
      if (!mtcp_state_set (&(thread -> state), ST_RUNDISABLED, ST_RUNENABLED)) goto again;
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

/********************************************************************************************************************************/
/*																*/
/*  This executes as a thread.  It sleeps for the checkpoint interval seconds, then wakes to write the checkpoint file.		*/
/*																*/
/********************************************************************************************************************************/

static void *checkpointhread (void *dummy)
{
  int needrescan;
  struct timespec sleeperiod;
  struct timeval started, stopped;
  Thread *ckpthread, *thread;

  /* This is the start function of the checkpoint thread.
   * We also call getcontext to get a snapshot of this call frame,
   * since we will never exit this call frame.  We always return
   * to this call frame at time of startup, on restart.  Hence, restart
   * will forget any modifications to our local variables since restart.
   */
  static int originalstartup = 1;

  /* We put a timeout in case the thread being waited for exits whilst we are waiting */

  static struct timespec const enabletimeout = { 10, 0 };

  DPRINTF (("mtcp checkpointhread*: %d started\n", mtcp_sys_kernel_gettid ()));

  /* Set up our restart point, ie, we get jumped to here after a restore */

  ckpthread = getcurrenthread ();
  save_sig_state (ckpthread);
  save_tls_state (ckpthread);
  /* Release user thread after we've initialized. */
  sem_post(&sem_start);
  if (getcontext (&(ckpthread -> savctx)) < 0) mtcp_abort ();
  DPRINTF (("mtcp checkpointhread*: after getcontext\n"));
  if (originalstartup)
    originalstartup = 0;
  else {

    /* We are being restored.  Wait for all other threads to finish being restored before resuming checkpointing. */

    DPRINTF (("mtcp checkpointhread*: waiting for other threads after restore\n"));
    wait_for_all_restored ();
    DPRINTF (("mtcp checkpointhread*: resuming after restore\n"));
  }

  /* Reset the verification counter - on init, this will set it to it's start value. */
  /* After a verification, it will reset it to its start value.  After a normal      */
  /* restore, it will set it to its start value.  So this covers all cases.          */

  verify_count = verify_total;
  DPRINTF (("After verify count mtcp checkpointhread*: %d started\n",
	    mtcp_sys_kernel_gettid ()));

  while (1) {

    /* Wait a while between writing checkpoint files */

    if(callback_sleep_between_ckpt == NULL)
    {
        memset (&sleeperiod, 0, sizeof sleeperiod);
        sleeperiod.tv_sec = intervalsecs;
        while ((nanosleep (&sleeperiod, &sleeperiod) < 0) && (errno == EINTR)) {}
    }
    else
    {
        DPRINTF(("mtcp checkpointhread*: before callback_sleep_between_ckpt(%d)\n",intervalsecs));
        (*callback_sleep_between_ckpt)(intervalsecs);
        DPRINTF(("mtcp checkpointhread*: after callback_sleep_between_ckpt(%d)\n",intervalsecs));
    }

    setup_sig_handler();
    mtcp_sys_gettimeofday (&started, NULL);
    checkpointsize = 0;

    /* Halt all other threads - force them to call stopthisthread                    */
    /* If any have blocked checkpointing, wait for them to unblock before signalling */

rescan:
    needrescan = 0;
    lock_threads ();
    for (thread = threads; thread != NULL; thread = thread -> next) {

      /* If thread no longer running, remove it from thread list */

again:
      setup_sig_handler(); //keep pounding the signal handler in

      if (*(thread -> actual_tidptr) == 0) {
        DPRINTF (("mtcp checkpointhread*: thread %d disappeared\n", thread -> tid));
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
          if (!mtcp_state_set (&(thread -> state), ST_SIGDISABLED, ST_RUNDISABLED)) goto again;
          needrescan = 1;
          break;
        }

        /* Thread is running and has checkpointing enabled                 */
        /* Send it a signal so it will call stopthisthread                 */
        /* We will need to rescan (hopefully it will be suspended by then) */

        case ST_RUNENABLED: {
          if (!mtcp_state_set (&(thread -> state), ST_SIGENABLED, ST_RUNENABLED)) goto again;
          if (mtcp_sys_kernel_tkill (thread -> tid, STOPSIGNAL) < 0) {
            if (mtcp_sys_errno != ESRCH) {
              mtcp_printf ("mtcp checkpointhread: error signalling thread %d: %s\n",
                           thread -> tid, strerror (mtcp_sys_errno));
            }
            unlk_threads ();
            threadisdead (thread);
            goto rescan;
          }
          needrescan = 1;
          break;
        }

        /* Thread is running, we have signalled it to stop, but it has checkpointing disabled */
        /* So we wait for it to change state                                                  */
        /* We have to unlock because it may need lock to change state                         */

        case ST_SIGDISABLED: {
          unlk_threads ();
          mtcp_state_futex (&(thread -> state), FUTEX_WAIT, ST_SIGDISABLED, &enabletimeout);
          goto rescan;
        }

        /* Thread is running and we have sent signal to stop it             */
        /* So we have to wait for it to change state (enter signal handler) */
        /* We have to unlock because it may try to use lock meanwhile       */

        case ST_SIGENABLED: {
          unlk_threads ();
          mtcp_state_futex (&(thread -> state), FUTEX_WAIT, ST_SIGENABLED, &enabletimeout);
          goto rescan;
        }

        /* Thread has entered signal handler and is saving its context             */
        /* So we have to wait for it to finish doing so                            */
        /* We don't need to unlock because it won't use lock before changing state */

        case ST_SUSPINPROG: {
          mtcp_state_futex (&(thread -> state), FUTEX_WAIT, ST_SUSPINPROG, &enabletimeout);
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

    /* If need to rescan (ie, some thread possibly not in ST_SUSPENDED STATE), check them all again */

    if (needrescan) goto rescan;
    RMB; // matched by WMB in stopthisthread
    DPRINTF (("mtcp checkpointhread*: everything suspended\n"));

    /* If no threads, we're all done */

    if (threads == NULL) {
      DPRINTF (("mtcp checkpointhread*: exiting (no threads)\n"));
      return (NULL);
    }

    /* call weak symbol of this file, possibly overridden by the user's strong symbol  */
    /* user must compile his/her code with -Wl,-export-dynamic to make it visible */
    mtcpHookPreCheckpoint();

    /* All other threads halted in 'stopthisthread' routine (they are all ST_SUSPENDED) - it's safe to write checkpoint file now */
    if (callback_pre_ckpt != NULL){
      // Here we want to synchronize the shared memory pages with the backup files
      DPRINTF(("mtcp checkpointhread*: syncing shared memory with backup files\n"));
      sync_shared_mem();

      DPRINTF(("mtcp checkpointhread*: before callback_pre_ckpt() (&%x,%x) \n",
	       &callback_pre_ckpt,callback_pre_ckpt));
      (*callback_pre_ckpt)();
    }

    mtcp_saved_break = (void*) mtcp_sys_brk(0);  // kernel returns mm->brk when passed zero
    /* Do this once, same for all threads.  But restore for each thread. */
    if (mtcp_have_thread_sysinfo_offset())
      saved_sysinfo = mtcp_get_thread_sysinfo();
    /* Do this once.  It's the same for all threads. */
    saved_termios_exists = ( isatty(STDIN_FILENO)
    			     && tcgetattr(STDIN_FILENO, &saved_termios) >= 0 );

    DPRINTF (("mtcp checkpointhread*: mtcp_saved_break=%p\n", mtcp_saved_break));

    checkpointeverything ();

    if(callback_post_ckpt != NULL){
        DPRINTF(("mtcp checkpointhread*: before callback_post_ckpt() (&%x,%x) \n"
				,&callback_post_ckpt,callback_post_ckpt));
        (*callback_post_ckpt)(0);
    }
    if (showtiming) {
      mtcp_sys_gettimeofday (&stopped, NULL);
      stopped.tv_usec += (stopped.tv_sec - started.tv_sec) * 1000000 - started.tv_usec;
      mtcp_printf ("mtcp checkpoint: time %u uS, size %u megabytes, avg rate %u MB/s\n",
                   stopped.tv_usec, (unsigned int)(checkpointsize / 1000000),
                   (unsigned int)(checkpointsize / stopped.tv_usec));
    }

    /* call weak symbol of this file, possibly overridden by the user's strong symbol  */
    /* user must compile his/her code with -Wl,-export-dynamic to make it visible */
    mtcpHookPostCheckpoint();

    /* Resume all threads.  But if we're doing a checkpoint verify, abort all threads except */
    /* the main thread, as we don't want them running when we exec the mtcp_restore program. */

    DPRINTF (("mtcp checkpointhread*: resuming everything\n"));
    lock_threads ();
    for (thread = threads; thread != NULL; thread = thread -> next) {
      if (mtcp_state_value (&(thread -> state)) != ST_CKPNTHREAD) {
        if (!mtcp_state_set (&(thread -> state), ST_RUNENABLED, ST_SUSPENDED)) mtcp_abort ();
        mtcp_state_futex (&(thread -> state), FUTEX_WAKE, 1, NULL);
      }
    }
    unlk_threads ();
    DPRINTF (("mtcp checkpointhread*: everything resumed\n"));
    /* But if we're doing a restore verify, just exit.  The main thread is doing the exec to start the restore. */

    if ((verify_total != 0) && (verify_count == 0)) return (NULL);
  }
}

/**
 * This function returns the fd to which the checkpoint file should be written.
 * The purpose of using this function over mtcp_sys_open() is that this
 * function will handle compression and gzipping.
 */
static int test_use_compression(void)
{
  char *do_we_compress;

  do_we_compress = getenv("MTCP_GZIP");
  // allow alternate name for env var
  if( do_we_compress == NULL ) do_we_compress = getenv("DMTCP_GZIP");
  // env var is unset, let's default to enabled
  // to disable compression, run with MTCP_GZIP=0
  if( do_we_compress == NULL) do_we_compress = "1";

  char *endptr;
  strtol(do_we_compress, &endptr, 0);
  if ( *do_we_compress == '\0' || *endptr != '\0' ) {
    mtcp_printf("WARNING: MTCP_GZIP/DMTCP_GZIP defined as %s (not a number)\n"
	        "  Checkpoint image will not be compressed.\n",
	        do_we_compress);
    do_we_compress = "0";
  }
  if ( 0 == strcmp(do_we_compress, "0") )
    return 0;
  /* If we arrive down here, it's safe to ccompress. */
  return 1;
}

static int open_ckpt_to_write(int fd, int pipe_fds[2], char *gzip_path)
{
  pid_t cpid;
  char *gzip_args[] = { "gzip", "-1", "-", NULL };

  gzip_args[0] = gzip_path;

  cpid = mtcp_sys_fork();
  if (cpid == -1) {
    mtcp_printf("WARNING: error forking child.  Compression will "
                "not be used.\n");
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    return fd;
  } else if (cpid > 0) { /* parent process */
    mtcp_ckpt_gzip_child_pid = cpid;
    close(pipe_fds[0]);
    close(fd);
    return pipe_fds[1];
  } else { /* child process */
    close(pipe_fds[1]);
    pipe_fds[0] = dup(dup(dup(pipe_fds[0])));
    fd = dup(fd);
    dup2(pipe_fds[0], STDIN_FILENO);
    close(pipe_fds[0]);
    dup2(fd, STDOUT_FILENO);
    close(fd);
    //make sure DMTCP doesn't catch gzip
    unsetenv("LD_PRELOAD");

    execvp_entry = mtcp_get_libc_symbol("execvp");
    (*execvp_entry)(gzip_path, gzip_args);
    /* should not get here */
    mtcp_printf("ERROR: compression failed!  No checkpointing will be"
                "performed!  Cancel now!\n");
    mtcp_sys_exit(1);
  }

  return fd;
}



/********************************************************************************************************************************/
/*																*/
/*  This routine is called from time-to-time to write a new checkpoint file.							*/
/*  It assumes all the threads are suspended.											*/
/*																*/
/********************************************************************************************************************************/

static void checkpointeverything (void)
{
  Area area;
  int fd, mapsfd;
  VA area_begin, area_end;
  int stack_was_seen = 0;
  int vsyscall_exists = 0;
  int forked_checkpointing = 0;
  int forked_cpid;
  int use_compression = -1; /* decide later */
  int pipe_fds[2]; /* for potential piping */
  char *gzip_path = "gzip";

  static void *const frpointer = finishrestore;

  DPRINTF (("mtcp checkpointeverything*: tid %d\n", mtcp_sys_kernel_gettid ()));

  if (getenv("MTCP_FORKED_CHECKPOINT") != NULL)
    forked_checkpointing = 1;
#ifdef TEST_FORKED_CHECKPOINTING
  forked_checkpointing = 1;
#endif

  /* 1. Test if using compression */
  use_compression = test_use_compression();
  /* 2. Create pipe */
  /* Note:  Must use mtcp_sys_pipe(), to go to kernel, since
   *   DMTCP has a wrapper around glibc promoting pipes to socketpairs,
   *   DMTCP doesn't directly checkpoint/restart pipes.
   */
  if ( use_compression && mtcp_sys_pipe(pipe_fds) == -1 ) {
    mtcp_printf("WARNING: error creating pipe. Compression will "
                "not be used.\n");
    use_compression = 0;
  }
  /* 3. Get gzip path */
  if ((gzip_path = mtcp_executable_path(gzip_path)) == NULL) {
    mtcp_printf("WARNING: gzip cannot be executed.  Compression will "
                "not be used.\n");
    use_compression = 0;
  }
  /* 4. Open fd to checkpoint image on disk */
  /* Create temp checkpoint file and write magic number to it */
  /* This is a callback to DMTCP.  DMTCP writes header and returns fd. */
  fd = mtcp_safe_open(temp_checkpointfilename,
		      O_CREAT | O_TRUNC | O_WRONLY, 0600);
  if (fd < 0) {
    mtcp_printf("mtcp.c: checkpointeverything: error creating %s: %s\n",
                temp_checkpointfilename, strerror(mtcp_sys_errno));
    mtcp_abort();
  }
  /* 5. We now have the information to pipe to gzip, or directly to fd
  *     We do it this way, so that gzip will be direct child of forked process
  *       when using forked checkpointing.
  */

  /* Better to do this in parent, not child, for most accurate prefix info. */
  /* In Linux, pipe always has at least one page (4KB), which is enough
   * for DMTCP header.  This could be more portable by having DMTCP write
   * to buffer, and we would later write the buffer to the pipe.
   */
  if(callback_write_ckpt_prefix != 0)
    (*callback_write_ckpt_prefix)(use_compression ? pipe_fds[1] : fd);

#if 1
  /* Temporary fix, until DMTCP uses its own separate allocator.
   * The else code should really go lower down, just before we checkpoint
   * the heap.
   */
#else
  if (mtcp_sys_break(0) != mtcp_saved_break)
    mtcp_printf("\n\n*** ERROR:  End of heap grew."
		"  Continue at your own risk. ***\n\n\n");
#endif

  /* Drain stdout and stderr before checkpoint */
  tcdrain(STDOUT_FILENO);
  tcdrain(STDERR_FILENO);

  /* if no forked checkpointing, or if child with forked checkpointing */
  if (forked_checkpointing && ((forked_cpid = mtcp_sys_fork()) == 0)
      || ! forked_checkpointing) {

    /* grandchild continues; no need now to waitpid() on grandchild */
    if (forked_checkpointing && mtcp_sys_fork() != 0) 
      mtcp_sys_exit(0); /* child exits */

    if (forked_checkpointing) {
      DPRINTF (("mtcp checkpointeverything*: inside grandchild process\n"));
    }

    if (use_compression) /* if use_compression, fork a gzip process */
      fd = open_ckpt_to_write(fd, pipe_fds, gzip_path);

    writefile (fd, MAGIC, MAGIC_LEN);

    /* Write out the resource limits from stack, shareable parameters and the image   */
    /* Put this all at the front to make the restore easy */

    struct rlimit stack_rlimit;
    getrlimit(RLIMIT_STACK, &stack_rlimit);

    DPRINTF(("mtcp_restart: saved stack resource limit: soft_lim:%p, hard_lim:%p\n", stack_rlimit.rlim_cur, stack_rlimit.rlim_max));

    writecs (fd, CS_STACKRLIMIT);
    writefile (fd, &stack_rlimit, sizeof stack_rlimit);

    DPRINTF (("mtcp checkpointeverything*: restore image %X at %p from [mtcp.so]\n", restore_size, restore_begin));

    writecs (fd, CS_RESTOREBEGIN);
    writefile (fd, &restore_begin, sizeof restore_begin);
    writecs (fd, CS_RESTORESIZE);
    writefile (fd, &restore_size, sizeof restore_size);
    writecs (fd, CS_RESTORESTART);
    writefile (fd, &restore_start, sizeof restore_start);
    writecs (fd, CS_RESTOREIMAGE);
    writefile (fd, (void *)restore_begin, restore_size);
    writecs (fd, CS_FINISHRESTORE);
    writefile (fd, &frpointer, sizeof frpointer);

    /* Write out file descriptors */

    writefiledescrs (fd);

    /* Finally comes the memory contents */

    /**************************************************************************/
    /* We can't do any more mallocing at this point because malloc stuff is   */
    /* outside the limits of the mtcp.so image, so it won't get checkpointed, */
    /* and it's possible that we would checkpoint an inconsistent state.      */
    /* See note in restoreverything routine.                                  */
    /**************************************************************************/

    mapsfd = mtcp_sys_open2 ("/proc/self/maps", O_RDONLY);
    if (mapsfd < 0) {
      mtcp_printf ("mtcp checkpointeverything: error opening"
		   " /proc/self/maps: %s\n", strerror (mtcp_sys_errno));
      mtcp_abort ();
    }

    /* Determine if [vsyscall] exists.  If [vdso] and [vsyscall] exist,
     * [vdso] will be saved and restored.
     * NOTE:  [vdso] is relocated if /proc/sys/kernel/randomize_va_space == 2.
     * We must restore old [vdso] and also keep [vdso] in that case.
     * On Linux 2.6.25, 32-bit Linux has:  [heap], /lib/ld-2.7.so, [vdso], libs, [stack].
     * On Linux 2.6.25, 64-bit Linux has:  [stack], [vdso], [vsyscall].
     *   and at least for gcl, [stack], mtcp.so, [vsyscall] seen.
     * If 32-bit process in 64-bit Linux:  [stack] (0xffffd000), [vdso] (0xffffe0000)
     * On 32-bit Linux, mtcp_restart has [vdso], /lib/ld-2.7.so, [stack]
     * Need to restore old [vdso] into mtcp_restart, to restart.
     * With randomize_va_space turned off, libraries start at high address
     *     0xb8000000 and are loaded progressively at lower addresses.
     * mtcp_restart loads vdso (which looks like a shared library) first.
     * But libpthread/libdl/libc libraries are loaded above vdso in user image.
     * So, we must use the opposite of the user's setting (no randomization if
     *     user turned it on, and vice versa).  We must also keep the
     *     new vdso section, provided by mtcp_restart.
     */
    while (readmapsline (mapsfd, &area)) {
      if (0 == strcmp(area.name, "[vsyscall]"))
        vsyscall_exists = 1;
    }
    close(mapsfd);
    mapsfd = mtcp_sys_open2 ("/proc/self/maps", O_RDONLY);

    while (readmapsline (mapsfd, &area)) {
      area_begin = (VA)area.addr;
      area_end   = area_begin + area.size;

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
       if (area_begin >= HIGHEST_VA && area_begin == 0xffffe000) continue;
#ifdef __x86_64__
       /* And in 64-bit mode later Red Hat RHEL Linux 2.6.9 releases
        * use 0xffffffffff600000 for VDSO.
        */
       if (area_begin >= HIGHEST_VA && area_begin == 0xffffffffff600000) continue;
#endif

      /* Skip anything that has no read or execute permission.  This occurs on one page in a Linux 2.6.9 installation.  No idea why.  This code would also take care of kernel sections since we don't have read/execute permission there.  */

      if (!((area.prot & PROT_READ) || (area.prot & PROT_WRITE))) continue;

      // Consider skipping deleted sections when we know they're so labelled
      // bash creates "/dev/zero (deleted)" after checkpoint in Ubuntu 8.04
      // if (strstr(area.name, " (deleted)")) continue;

      /* Special Case Handling: nscd is enabled*/
      if ( strncmp (area.name, nscd_mmap_str, strlen(nscd_mmap_str)) == 0 
          || strncmp (area.name, nscd_mmap_str2, strlen(nscd_mmap_str2)) == 0 ) {
        DPRINTF(("mtcp checkpointeverything: NSCD daemon shared memory area present. MTCP will now try to remap\n" \
               "                           this area in read/write mode and then will fill it with zeros so that\n" \
               "                           glibc will automatically ask NSCD daemon for new shared area\n\n"));
        area.prot = PROT_READ | PROT_WRITE;
        area.flags = MAP_PRIVATE | MAP_ANONYMOUS;

        if ( munmap(area.addr, area.size) == -1) {
          mtcp_printf ("mtcp checkpointeverything: error unmapping NSCD shared area: %s\n", strerror (mtcp_sys_errno));
          mtcp_abort();
        }

        if ( mmap(area.addr, area.size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, 0, 0) == MAP_FAILED ){
          mtcp_printf ("mtcp checkpointeverything: error remapping NSCD shared area: %s\n", strerror (mtcp_sys_errno));
          mtcp_abort();
        }

        memset(area.addr, 0, area.size);
      }

      /* Force the anonymous flag if it's a private writeable section, as the */
      /* data has probably changed from the contents of the original images   */

      /* We also do this for read-only private sections as it's possible      */
      /* to modify a page there, too (via mprotect)                           */

      if ((area.flags & MAP_PRIVATE) /*&& (area.prot & PROT_WRITE)*/) {
        area.flags |= MAP_ANONYMOUS;
      }

      if ( area.flags & MAP_SHARED ) {
        /* invalidate shared memory pages so that the next read to it (when we are writing them to ckpt file) will cause them to be reloaded from the disk */
        if ( msync(area.addr, area.size, MS_INVALIDATE) < 0 ){
          mtcp_printf ("mtcp sync_shared_memory: error %d Invalidating %X"
                       " at %p from %s + %X\n", mtcp_sys_errno, area.size,
		       area.addr, area.name, area.offset);
          mtcp_abort();
        }
      }


      /* Skip any mapping for this mtcp.so image - it got saved as CS_RESTOREIMAGE at the beginning */

      if (area_begin < restore_begin) {
        if (area_end <= restore_begin) {
          writememoryarea (fd, &area, 0, vsyscall_exists); // the whole thing is before the restore image
        } else if (area_end <= restore_end) {
          area.size = restore_begin - area_begin;    // we just have to chop the end part off
          writememoryarea (fd, &area, 0, vsyscall_exists);
        } else {
          area.size = restore_begin - area_begin;    // we have to write stuff that comes before restore image
          writememoryarea (fd, &area, 0, vsyscall_exists);
          area.offset += restore_end - area_begin;   // ... and we have to write stuff that comes after restore image
          area.size = area_end - restore_end;
          area.addr = (void *)restore_end;
          writememoryarea (fd, &area, 0, vsyscall_exists);
        }
      } else if (area_begin < restore_end) {
        if (area_end > restore_end) {
          area.offset += restore_end - area_begin;   // we have to write stuff that comes after restore image
          area.size = area_end - restore_end;
          area.addr = (void *)restore_end;
          writememoryarea (fd, &area, 0, vsyscall_exists);
        }
      } else {
        if ( strstr (area.name, "[stack]") )
          stack_was_seen = 1;
        writememoryarea (fd, &area, stack_was_seen, vsyscall_exists); // the whole thing comes after the restore image
      }
    }

    close (mapsfd);

    /* That's all folks */

    writecs (fd, CS_THEEND);
    if (close (fd) < 0) {
      mtcp_printf ("mtcp checkpointeverything(grandchild):"
                   " error closing checkpoint file: %s\n", strerror (errno));
      mtcp_abort ();
    }
    if (use_compression) {
      /* IF OUT OF DISK SPACE, REPORT IT HERE. */
      if( waitpid(mtcp_ckpt_gzip_child_pid, NULL, 0 ) == -1 )
	  mtcp_printf ("mtcp checkpointeverything(grandchild): waitpid: %s\n",
                       strerror (errno));
      mtcp_ckpt_gzip_child_pid = -1;
    }

    /* Maybe it's time to verify the checkpoint                                                                 */
    /* If so, exec an mtcp_restore with the temp file (in case temp file is bad, we'll still have the last one) */
    /* If the new file is good, mtcp_restore will rename it over the last one                                   */

    if (verify_total != 0) -- verify_count;

    /* Now that temp checkpoint file is complete, rename it over old permanent
     * checkpoint file.  Uses rename() syscall, which doesn't change i-nodes.
     * So, gzip process can continue to write to file even after renaming.
     */

    else renametempoverperm ();

    if (forked_checkpointing)
      mtcp_sys_exit (0); /* grandchild exits */
  }

  /* For forked checkpointing, only the original parent process executes here */

  if (forked_checkpointing) {
    if (close (fd) < 0) { /* parent and grandchild must both close fd. */
      mtcp_printf ("mtcp checkpointeverything: error closing checkpoint file: %s\n", strerror (errno));
      mtcp_abort ();
    }
    /* parent and grandchild both close pipe_fds[] (parent didn't use it) */
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    // Calling waitpid here, but on 32-bit Linux, libc:waitpid() calls wait4()
    if( waitpid(forked_cpid, NULL, 0) == -1 )
      DPRINTF (("mtcp restoreverything*: error waitpid: errno: %d",
	        mtcp_sys_errno));
  }

  DPRINTF (("mtcp checkpointeverything*: checkpoint complete\n"));
}

/* True if the given FD should be checkpointed */
static int should_ckpt_fd (int fd)
{
   if( callback_ckpt_fd!=NULL )
     return (*callback_ckpt_fd)(fd); //delegate to callback
   else
     return fd>2; //ignore stdin/stdout
}

/* Write list of open files to the checkpoint file */

static void writefiledescrs (int fd)

{
  char dbuf[BUFSIZ], linkbuf[FILENAMESIZE], *p, procfdname[64];
  int doff, dsiz, fddir, fdnum, linklen, rc;
  off_t offset;
  struct dirent *dent;
  struct Stat lstatbuf, statbuf;

  writecs (fd, CS_FILEDESCRS);

  /* Open /proc/self/fd directory - it contains a list of files I have open */

  fddir = mtcp_sys_open ("/proc/self/fd", O_RDONLY, 0);
  if (fddir < 0) {
    mtcp_printf ("mtcp writefiledescrs: error opening directory /proc/self/fd: %s\n", strerror (errno));
    mtcp_abort ();
  }

  /* Check each entry */

  while (1) {
    dsiz = -1;
    if (sizeof dent -> d_ino == 4) dsiz = mtcp_sys_getdents (fddir, dbuf, sizeof dbuf);
    if (sizeof dent -> d_ino == 8) dsiz = mtcp_sys_getdents64 (fddir, dbuf, sizeof dbuf);
    if (dsiz <= 0) break;
    for (doff = 0; doff < dsiz; doff += dent -> d_reclen) {
      dent = (void *)(dbuf + doff);

      /* The filename should just be a decimal number = the fd it represents                                         */
      /* Also, skip the entry for the checkpoint and directory files as we don't want the restore to know about them */

      fdnum = strtol (dent -> d_name, &p, 10);
      if ((*p == 0) && (fdnum >= 0) && (fdnum != fd) && (fdnum != fddir) && (should_ckpt_fd (fdnum) > 0)) {

        /* Read the symbolic link so we get the filename that's open on the fd */

        sprintf (procfdname, "/proc/self/fd/%d", fdnum);
        linklen = readlink (procfdname, linkbuf, sizeof linkbuf - 1);
        if ((linklen >= 0) || (errno != ENOENT)) { // probably was the proc/self/fd directory itself
          if (linklen < 0) {
            mtcp_printf ("mtcp writefiledescrs: error reading %s: %s\n",
	                 procfdname, strerror (errno));
            mtcp_abort ();
          }
          linkbuf[linklen] = '\0';

          /* Read about the link itself so we know read/write open flags */

          rc = mtcp_safelstat (procfdname, &lstatbuf);
          if (rc < 0) {
            mtcp_printf ("mtcp writefiledescrs: error statting %s -> %s: %s\n",
	                 procfdname, linkbuf, strerror (-rc));
            mtcp_abort ();
          }

          /* Read about the actual file open on the fd */

          rc = mtcp_safestat (linkbuf, &statbuf);
          if (rc < 0) {
            mtcp_printf ("mtcp writefiledescrs: error statting %s -> %s: %s\n",
	                 procfdname, linkbuf, strerror (-rc));
          }

          /* Write state information to checkpoint file                                               */
          /* Replace file's permissions with current access flags so restore will know how to open it */

          else {
            offset = 0;
            if (S_ISREG (statbuf.st_mode)) offset = mtcp_sys_lseek (fdnum, 0, SEEK_CUR);
            statbuf.st_mode = (statbuf.st_mode & ~0777) | (lstatbuf.st_mode & 0777);
            writefile (fd, &fdnum, sizeof fdnum);
            writefile (fd, &statbuf, sizeof statbuf);
            writefile (fd, &offset, sizeof offset);
            writefile (fd, &linklen, sizeof linklen);
            writefile (fd, linkbuf, linklen);
          }
        }
      }
    }
  }
  if (dsiz < 0) {
    mtcp_printf ("mtcp writefiledescrs: error reading /proc/self/fd: %s\n",
                 strerror (mtcp_sys_errno));
    mtcp_abort ();
  }

  mtcp_sys_close (fddir);

  /* Write end-of-fd-list marker to checkpoint file */

  fdnum = -1;
  writefile (fd, &fdnum, sizeof fdnum);
}

static void writememoryarea (int fd, Area *area, int stack_was_seen,
			     int vsyscall_exists)

{ static void * orig_stack = NULL;

  /* Write corresponding descriptor to the file */

  if (orig_stack == NULL && 0 == strcmp(area -> name, "[stack]"))
    orig_stack = area -> addr + area -> size;

  if (0 == strcmp(area -> name, "[vdso]") && !stack_was_seen)
    DPRINTF (("mtcp checkpointeverything*: skipping over [vdso] section"
              " %X at %p\n", area -> size, area -> addr));
  else if (0 == strcmp(area -> name, "[vsyscall]") && !stack_was_seen)
    DPRINTF (("mtcp checkpointeverything*: skipping over [vsyscall] section"
    	      " %X at %p\n", area -> size, area -> addr));
  else if (0 == strcmp(area -> name, "[stack]") &&
	   orig_stack != area -> addr + area -> size)
    /* Kernel won't let us munmap this.  But we don't need to restore it. */
    DPRINTF (("mtcp checkpointeverything*: skipping over [stack] segment"
    	      " %X at %pi (not the orig stack)\n", area -> size, area -> addr));
  else if (!(area -> flags & MAP_ANONYMOUS))
    DPRINTF (("mtcp checkpointeverything*: save %X at %p from %s + %X\n",
              area -> size, area -> addr, area -> name, area -> offset));
  else if (area -> name[0] == '\0')
    DPRINTF (("mtcp checkpointeverything*: save anonymous %X at %p\n",
              area -> size, area -> addr));
  else DPRINTF (("mtcp checkpointeverything*: save anonymous %X at %p"
                 " from %s + %X\n",
		 area -> size, area -> addr, area -> name, area -> offset));

  if ( 0 != strcmp(area -> name, "[vsyscall]")
       && ( (0 != strcmp(area -> name, "[vdso]")
             || vsyscall_exists /* which implies vdso can be overwritten */
             || !stack_was_seen ))) /* If vdso appeared before stack, it can be replaced */
  {
    writecs (fd, CS_AREADESCRIP);
    writefile (fd, area, sizeof *area);

    /* Anonymous sections need to have their data copied to the file,
     *   as there is no file that contains their data
     * We also save shared files to checkpoint file to handle shared memory
     *   implemented with backing files
     */
    if (area -> flags & MAP_ANONYMOUS || area -> flags & MAP_SHARED) {
      writecs (fd, CS_AREACONTENTS);
      writefile (fd, area -> addr, area -> size);
    }
  }
}

/* Write checkpoint section number to checkpoint file */

static void writecs (int fd, char cs)

{
  writefile (fd, &cs, sizeof cs);
}

/* Write something to checkpoint file */

static char const zeroes[PAGE_SIZE] = { 0 };

static void writefile (int fd, void const *buff, int size)

{
  char const *bf;
  int rc, sz, wt;

  checkpointsize += size;

  bf = buff;
  sz = size;
  while (sz > 0) {
    for (wt = sz; wt > 0; wt /= 2) {
      rc = write (fd, bf, wt);
      if ((rc >= 0) || (errno != EFAULT)) break;
    }

    /* Sometimes image page alignment will leave a hole in the middle of an image */
    /* ... but the idiot proc/self/maps will include it anyway                    */

    if (wt == 0) {
      rc = (sz > sizeof zeroes ? sizeof zeroes : sz);
      checkpointsize -= rc; /* Correct now, since writefile will add rc back */
      writefile (fd, zeroes, rc);
    }

    /* Otherwise, check for real error */

    else {
      if (rc == 0) errno = EPIPE;
      if (rc <= 0) {
        mtcp_printf ("mtcp writefile: error writing from %p to %s: %s\n",
	             bf, temp_checkpointfilename, strerror (errno));
        mtcp_abort ();
      }
    }

    /* It's ok, we're on to next part */

    sz -= rc;
    bf += rc;
  }
}

/********************************************************************************************************************************/
/*																*/
/*  This signal handler is forced by the main thread doing a 'mtcp_sys_kernel_tkill' to stop these threads so it can do a 	*/
/*  checkpoint															*/
/*																*/
/********************************************************************************************************************************/

static void stopthisthread (int signum)

{
  int rc;
  Thread *thread;

  DPRINTF (("mtcp stopthisthread*: tid %d returns to %p\n",
            mtcp_sys_kernel_gettid (), __builtin_return_address (0)));

  setup_sig_handler ();  // re-establish in case of another STOPSIGNAL so we don't abort by default

  thread = getcurrenthread ();                                              // see which thread this is
  if (mtcp_state_set (&(thread -> state), ST_SUSPINPROG, ST_SIGENABLED)) {  // make sure we don't get called twice for same thread
    save_sig_state (thread);                                                // save signal state (and block signal delivery)
    save_tls_state (thread);                                                // save thread local storage state

    ///JA: new code ported from v54b
    rc = getcontext (&(thread -> savctx));
    if (rc < 0) {
      mtcp_printf ("mtcp stopthisthread: getcontext rc %d errno %d\n",
                   rc, errno);
      mtcp_abort ();
    }
    DPRINTF (("mtcp stopthisthread*: after getcontext\n"));
    if (mtcp_state_value(&restoreinprog) == 0) {

      /* We are the original process and all context is saved */

      WMB; // matched by RMB in checkpointhread

      /* Next comes the first time we use the old stack. */
      /* Tell the checkpoint thread that we're all saved away */
      if (!mtcp_state_set (&(thread -> state), ST_SUSPENDED, ST_SUSPINPROG)) mtcp_abort ();  // tell checkpointhread all our context is saved
      mtcp_state_futex (&(thread -> state), FUTEX_WAKE, 1, NULL);                            // wake checkpoint thread if it's waiting for me

      /* Then we wait for the checkpoint thread to write the checkpoint file then wake us up */

      DPRINTF (("mtcp stopthisthread*: thread %d suspending\n", thread -> tid));
      while (mtcp_state_value(&thread -> state) == ST_SUSPENDED) {
        mtcp_state_futex (&(thread -> state), FUTEX_WAIT, ST_SUSPENDED, NULL);
      }

      /* Maybe there is to be a checkpoint verification.  If so, and we're the main    */
      /* thread, exec the restore program.  If so and we're not the main thread, exit. */

      if ((verify_total != 0) && (verify_count == 0)) {

        /* If not the main thread, exit.  Either normal exit() or _exit() seems to cause other threads to exit. */

        if (thread != motherofall) {
          mtcp_sys_exit(0);
        }

        /* This is the main thread, verify checkpoint then restart by doing
         * a restart.
         * The restore will rename the file after it has done the restart
         */

        DPRINTF (("mtcp checkpointeverything*: verifying checkpoint...\n"));
        execlp ("mtcp_restart", "mtcp_restart", "-verify", temp_checkpointfilename, NULL);
        mtcp_printf ("mtcp checkpointeverything: error execing mtcp_restart %s: %s\n", temp_checkpointfilename, strerror (errno));
        mtcp_abort ();
      }

      /* No verification, resume where we left off */

      DPRINTF (("mtcp stopthisthread*: thread %d resuming\n", thread -> tid));
    }

    /* This stuff executes on restart */

    else {
      if (!mtcp_state_set (&(thread -> state), ST_RUNENABLED, ST_SUSPENDED)) mtcp_abort ();  // checkpoint was written when thread in SUSPENDED state
      wait_for_all_restored ();
      DPRINTF (("mtcp stopthisthread*: thread %d restored\n", thread -> tid));

      if (thread == motherofall) {

        /* If we're a restore verification, rename the temp file over the permanent one */

        if (mtcp_restore_verify) renametempoverperm ();
      }
    }
  }
  DPRINTF (("mtcp stopthisthread*: tid %d returning to %p\n",
	    mtcp_sys_kernel_gettid (), __builtin_return_address (0)));
}

/********************************************************************************************************************************/
/*																*/
/*  Wait for all threads to finish restoring their context, then release them all to continue on their way.			*/
/*																*/
/*    Input:															*/
/*																*/
/*	restoreinprog = number of threads, including this, that hasn't called 'wait_for_all_restored' yet			*/
/*	thread list locked													*/
/*																*/
/*    Output:															*/
/*																*/
/*	restoreinprog = decremented												*/
/*	                if now zero, all threads woken and thread list unlocked							*/
/*																*/
/********************************************************************************************************************************/

static void wait_for_all_restored (void)

{
  int rip;

  do rip = mtcp_state_value(&restoreinprog);                         // dec number of threads cloned but not completed longjmp'ing
  while (!mtcp_state_set (&restoreinprog, rip - 1, rip));
  if (-- rip == 0) {
    mtcp_state_futex (&restoreinprog, FUTEX_WAKE, 999999999, NULL);  // if this was last of all, wake everyone up
    // NOTE:  This is last safe moment for hook.  All previous threads
    //   have executed the "else" and are waiting on the futex.
    //   This last thread has not yet unlocked the threads: unlk_threads()
    //   So, no race condition occurs.
    //   By comparison, *callback_post_ckpt() is called before creating
    //   additional user threads.  Only motherofall (checkpoint thread existed)
    /* call weak symbol of this file, possibly overridden by the user's strong symbol  */
    /* user must compile his/her code with -Wl,-export-dynamic to make it visible */
    mtcpHookRestart();
    unlk_threads ();                                                 // ... and release the thread list
  } else {
    while ((rip = mtcp_state_value(&restoreinprog)) > 0) {           // otherwise, wait for last of all to wake this one up
      mtcp_state_futex (&restoreinprog, FUTEX_WAIT, rip, NULL);
    }
  }
}

/********************************************************************************************************************************/
/*																*/
/*  Save signal handlers and block signal delivery										*/
/*																*/
/********************************************************************************************************************************/

static void save_sig_state (Thread *thisthread)

{
  int i;
  sigset_t blockall;

  /* Block signal delivery first so signal handlers can't change state of signal handlers on us */

  memset (&blockall, -1, sizeof blockall);
  if (_real_sigprocmask (SIG_SETMASK, &blockall, &(thisthread -> sigblockmask)) < 0) {
    mtcp_abort ();
  }

  /* Now save all the signal handlers */

  for (i = NSIG; -- i >= 0;) {
    if (_real_sigaction (i, NULL, thisthread -> sigactions + i) < 0) {
      if (errno == EINVAL) memset (thisthread -> sigactions + i, 0, sizeof thisthread -> sigactions[i]);
      else {
        mtcp_printf ("mtcp save_sig_state: error saving signal %d action: %s\n", i, strerror (errno));
        mtcp_abort ();
      }
    }
  }
}

/********************************************************************************************************************************/
/*																*/
/*  Save state necessary for TLS restore											*/
/*  Linux saves stuff in the GDT, switching it on a per-thread basis								*/
/*																*/
/********************************************************************************************************************************/

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

  /* On older Linuces, we must save several GDT entries available to threads. */

#if MTCP__SAVE_MANY_GDT_ENTRIES
  for (i = GDT_ENTRY_TLS_MIN; i <= GDT_ENTRY_TLS_MAX; i ++) {
    thisthread -> gdtentrytls[i-GDT_ENTRY_TLS_MIN].entry_number = i;
    rc = mtcp_sys_get_thread_area (&(thisthread -> gdtentrytls[i-GDT_ENTRY_TLS_MIN]));
    if (rc < 0) {
      mtcp_printf ("mtcp checkpointeverything: error saving GDT TLS entry[%d]: %s\n", i, strerror (mtcp_sys_errno));
      mtcp_abort ();
    }
  }

  /* With newer Linuces, we just save the one GDT entry indexed by GS so we don't need the GDT_ENTRY_TLS_... definitions. */
  /* We get the particular index of the GDT entry to save by reading GS.                                                  */

#else
  i = thisthread -> TLSSEGREG / 8;
  thisthread -> gdtentrytls[0].entry_number = i;
  rc = mtcp_sys_get_thread_area (&(thisthread -> gdtentrytls[0]));
  if (rc < 0) {
    mtcp_printf ("mtcp checkpointeverything: error saving GDT TLS entry[%d]: %s\n", i, strerror (mtcp_sys_errno));
    mtcp_abort ();
  }
#endif
}

static void renametempoverperm (void)

{
  if (rename (temp_checkpointfilename, perm_checkpointfilename) < 0) {
    mtcp_printf ("mtcp checkpointeverything: error renaming %s to %s: %s\n", temp_checkpointfilename, perm_checkpointfilename, strerror (errno));
    mtcp_abort ();
  }
}

/********************************************************************************************************************************/
/*																*/
/*  Get current thread struct pointer												*/
/*  It is keyed by the calling thread's gettid value										*/
/*  Maybe improve someday by using TLS												*/
/*																*/
/********************************************************************************************************************************/

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
  mtcp_printf ("mtcp getcurrenthread: can't find thread id %d\n", tid);
  mtcp_abort ();
  return thread; /* NOTREACHED : stop compiler warning */
}

/********************************************************************************************************************************/
/*																*/
/*  Lock and unlock the 'threads' list												*/
/*																*/
/********************************************************************************************************************************/

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
  mtcp_state_set(&threadslocked , 0, 1);
  mtcp_state_futex (&threadslocked, FUTEX_WAKE, 1, NULL);
}

/********************************************************************************************************************************/
/*																*/
/*  Read /proc/self/maps line, converting it to an Area descriptor struct							*/
/*																*/
/*    Input:															*/
/*																*/
/*	mapsfd = /proc/self/maps file, positioned to beginning of a line							*/
/*																*/
/*    Output:															*/
/*																*/
/*	readmapsline = 0 : was at end-of-file, nothing read									*/
/*	               1 : read and processed one line										*/
/*	*area = filled in													*/
/*																*/
/*    Note:															*/
/*																*/
/*	Line from /procs/self/maps is in form:											*/
/*																*/
/*	<startaddr>-<endaddrexclusive> rwxs <fileoffset> <devmaj>:<devmin> <inode>    <filename>\n				*/
/*	all numbers in hexadecimal except inode is in decimal									*/
/*	anonymous will be shown with offset=devmaj=devmin=inode=0 and no '     filename'					*/
/*																*/
/********************************************************************************************************************************/

static int readmapsline (int mapsfd, Area *area)

{
  char c, rflag, sflag, wflag, xflag;
  int i, rc;
  struct Stat statbuf;
  VA devmajor, devminor, devnum, endaddr, inodenum, startaddr;

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

  c = mtcp_readhex (mapsfd, &devmajor);
  if (c != ' ') goto skipeol;
  area -> offset = devmajor;

  c = mtcp_readhex (mapsfd, &devmajor);
  if (c != ':') goto skipeol;
  c = mtcp_readhex (mapsfd, &devminor);
  if (c != ' ') goto skipeol;
  c = mtcp_readdec (mapsfd, &inodenum);
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
  if ( strncmp(area -> name, nscd_mmap_str, strlen(nscd_mmap_str)) == 0 
      || strncmp(area -> name, nscd_mmap_str2, strlen(nscd_mmap_str2)) == 0  ) { /* if nscd active*/
  }
  else if (area -> name[0] == '/'                  /* if an absolute pathname */
	   && ! strstr(area -> name, " (deleted)")) { /* and it's not deleted */
    rc = mtcp_safestat (area -> name, &statbuf);
    if (rc < 0) {
      mtcp_printf ("ERROR:  mtcp readmapsline: error %d statting %s\n",
                   -rc, area -> name);
      return (1); /* 0 would mean last line of maps; could do mtcp_abort() */
    }
    devnum = makedev (devmajor, devminor);
    if ((devnum != statbuf.st_dev) || (inodenum != statbuf.st_ino)) {
      mtcp_printf ("ERROR:  mtcp readmapsline: image %s dev:inode %X:%u"
		   " not eq maps %X:%u\n",
                area -> name, statbuf.st_dev, statbuf.st_ino, devnum, inodenum);
      return (1); /* 0 would mean last line of maps; could do mtcp_abort() */
    }
  }
  else if (c == '[') {
    while ((c != '\n') && (c != '\0')) {
      c = mtcp_readchar (mapsfd);
    }
  }
  if (c != '\n') goto skipeol;

  area -> addr = (void *)startaddr;
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
  DPRINTF (("ERROR:  mtcp readmapsline*: bad maps line <%c", c));
  while ((c != '\n') && (c != '\0')) {
    c = mtcp_readchar (mapsfd);
    mtcp_printf ("%c", c);
  }
  mtcp_printf (">\n");
  mtcp_abort ();
  return (0);  /* NOTREACHED : stop compiler warning */
}

/********************************************************************************************************************************/
/*																*/
/*  Do restore from checkpoint file												*/
/*  This routine is called from the mtcp_restore program to perform the restore							*/
/*  It resides in the mtcp.so image in exactly the same spot that the checkpointed process had its mtcp.so loaded at, so this 	*/
/*    can't possibly interfere with restoring the checkpointed process								*/
/*  The restore can't use malloc because that might create memory sections.							*/
/*  Strerror seems to mess up with its Locale stuff in here too.								*/
/*																*/
/*    Input:															*/
/*																*/
/*	fd = checkpoint file, positioned just after the CS_RESTOREIMAGE data							*/
/*																*/
/********************************************************************************************************************************/

#define STRINGS_LEN 10000
static char STRINGS[STRINGS_LEN];
void mtcp_restore_start (int fd, int verify, pid_t gzip_child_pid,char *ckpt_newname,
			 char *cmd_file, char *argv[], char *envp[] )

{ int i;
  char *strings = STRINGS;

  DEBUG_RESTARTING = 1;
  /* If we just replace extendedStack by (tempstack+STACKSIZE) in "asm"
   * below, the optimizer generates non-PIC code if it's not -O0 - Gene
   */
  long long * extendedStack = tempstack + STACKSIZE;

  /* Not used until we do longjmps, but get it out of the way now */

  mtcp_state_set(&restoreinprog ,1, 0);

  mtcp_sys_gettimeofday (&restorestarted, NULL);

  /* Save parameter away in a static memory location as we're about to wipe the stack */

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
  // Copy command line to mtcp.so, so that we can re-exec if randomized vdso
  //   steps on us.  This won't be needed when we use the linker to map areas.
  strings = STRINGS;
  // This version of STRCPY copies source string into STRINGS bugger,
  // and sets destination string to point there.
# define STRCPY(x,y) \
	if (strings + 256 < STRINGS + STRINGS_LEN) { \
	  mtcp_sys_strcpy(strings,y); \
	  x = strings; \
	  strings += mtcp_sys_strlen(y) + 1; \
	} else { \
	  DPRINTF(("MTCP:  ran out of string space." \
		   "  Trying to continue anyway\n")); \
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

  /* Switch to a stack area that's part of the shareable's memory address range
   * and thus not used by the checkpointed program
   */

  asm volatile (CLEAN_FOR_64_BIT(mov %0,%%esp\n\t)
                /* This next assembly language confuses gdb,
		   but seems to work fine anyway */
                CLEAN_FOR_64_BIT(xor %%ebp,%%ebp\n\t)
                : : "g" (extendedStack) : "memory");

  /* Once we're on the new stack, we can't access any local variables or parameters */
  /* Call the restoreverything to restore files and memory areas                    */

  /* This should never return */
  mtcp_restoreverything();
  asm volatile ("hlt");
}

/********************************************************************************************************************************/
/*																*/
/*  The original program's memory and files have been restored									*/
/*																*/
/********************************************************************************************************************************/

static void finishrestore (void)

{
  struct timeval stopped;
  int nnamelen;

  DPRINTF (("mtcp finishrestore*: mtcp_printf works\n"));

  if( (nnamelen = strlen(mtcp_ckpt_newname)) && strcmp(mtcp_ckpt_newname,perm_checkpointfilename) ){
  	// we start from different place - change it!
	char *tmp;
    DPRINTF(("mtcp finishrestore*: checkpoint file name was changed\n"));
    strncpy(perm_checkpointfilename,mtcp_ckpt_newname,MAXPATHLEN);
    memcpy (temp_checkpointfilename,perm_checkpointfilename,MAXPATHLEN);
    strncpy(temp_checkpointfilename + nnamelen, ".temp",MAXPATHLEN - nnamelen);
  }

  mtcp_sys_gettimeofday (&stopped, NULL);
  stopped.tv_usec += (stopped.tv_sec - restorestarted.tv_sec) * 1000000 - restorestarted.tv_usec;
  TPRINTF (("mtcp finishrestore*: time %u uS\n", stopped.tv_usec));

  /* Now we can access all our files and memory that existed at the time of the checkpoint  */
  /* We are still on the temporary stack, though                                            */

  /* Fill in the new mother process id */
  motherpid = mtcp_sys_getpid();

  /* Call another routine because our internal stack is wacked and we can't have local vars */

  ///JA: v54b port
  // so restarthread will have a big stack
  asm volatile (CLEAN_FOR_64_BIT(mov %0,%%esp)
		: : "g" (motherofall -> savctx.SAVEDSP - 128 ) : "memory");  // -128 for red zone
  restarthread (motherofall);
}

static int restarthread (void *threadv)

{
  int rip;
  Thread *child;
  Thread *const thread = threadv;

  restore_tls_state (thread);

  setup_sig_handler ();

  if (thread == motherofall) {
    set_tid_address (&(thread -> child_tid));

    if (callback_post_ckpt != NULL) {
        DPRINTF(("mtcp finishrestore*: before callback_post_ckpt(1=restarting)"
		 " (&%x,%x) \n",
		 &callback_post_ckpt, callback_post_ckpt));
        (*callback_post_ckpt)(1);
        DPRINTF(("mtcp finishrestore*: after callback_post_ckpt(1=restarting)\n"));
    }
    /* Do it once only, in motherofall thread. */
    if (saved_termios_exists)
      if ( ! isatty(STDIN_FILENO)
           || tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios) < 0 )
        DPRINTF(("WARNING: mtcp finishrestore*: failed to restore terminal\n"));
  }

  for (child = thread -> children; child != NULL; child = child -> siblings) {

    /* Increment number of threads created but haven't completed their longjmp */

    do rip = mtcp_state_value(&restoreinprog);
    while (!mtcp_state_set (&restoreinprog, rip + 1, rip));

    /* Create the thread so it can finish restoring itself.                       */
    /* Don't do CLONE_SETTLS (it'll puke).  We do it later via restore_tls_state. */

    ///JA: v54b port
    errno = -1;
    if ((*clone_entry) (restarthread, (void *)(child -> savctx.SAVEDSP - 128),  // -128 for red zone
                        (child -> clone_flags & ~CLONE_SETTLS) | CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID,
                        child, child -> parent_tidptr, NULL, child -> actual_tidptr) < 0) {

      mtcp_printf ("mtcp restarthread: error %d recreating thread\n", errno);
      mtcp_printf ("mtcp restarthread:   clone_flags %X, savedsp %p\n",
                   child -> clone_flags, child -> savctx.SAVEDSP);
      mtcp_abort ();
    }
  }

  /* All my children have been created, jump to the stopthisthread routine just after getcontext call */
  /* Note that if this is the restored checkpointhread, it jumps to the checkpointhread routine       */

  if (mtcp_have_thread_sysinfo_offset())
    mtcp_set_thread_sysinfo(saved_sysinfo);
  ///JA: v54b port
  DPRINTF (("mtcp restarthread*: calling setcontext: thread->tid: %d\n",
	    thread->tid));
  setcontext (&(thread -> savctx)); /* Shouldn't return */
  mtcp_abort ();
  return (0); /* NOTREACHED : stop compiler warning */
}

/********************************************************************************************************************************/
/*																*/
/*  Restore the GDT entries that are part of a thread's state									*/
/*																*/
/*  The kernel provides set_thread_area system call for a thread to alter a particular range of GDT entries, and it switches 	*/
/*  those entries on a per-thread basis.  So from our perspective, this is per-thread state that is saved outside user 		*/
/*  addressable memory that must be manually saved.										*/
/*																*/
/********************************************************************************************************************************/

static void restore_tls_state (Thread *thisthread)

{
  int rc;
#if MTCP__SAVE_MANY_GDT_ENTRIES
  int i;
#endif

  /* The assumption that this points to the pid was checked by that tls_pid crap near the beginning */

  *(pid_t *)(*(unsigned long *)&(thisthread -> gdtentrytls[0].base_addr) + TLS_PID_OFFSET) = motherpid;

  /* Likewise, we must jam the new pid into the mother thread's tid slot (checked by tls_tid carpola) */

  if (thisthread == motherofall) {
    *(pid_t *)(*(unsigned long *)&(thisthread -> gdtentrytls[0].base_addr) + TLS_TID_OFFSET) = motherpid;
  }

  /* Restore all three areas */

#if MTCP__SAVE_MANY_GDT_ENTRIES
  for (i = GDT_ENTRY_TLS_MIN; i <= GDT_ENTRY_TLS_MAX; i ++) {
    rc = mtcp_sys_set_thread_area (&(thisthread -> gdtentrytls[i-GDT_ENTRY_TLS_MIN]));
    if (rc < 0) {
      mtcp_printf ("mtcp restore_tls_state: error %d restoring GDT TLS entry[%d]\n", mtcp_sys_errno, i);
      mtcp_abort ();
    }
  }

  /* For newer Linuces, we just restore the one GDT entry that was indexed by GS */

#else
  rc = mtcp_sys_set_thread_area (&(thisthread -> gdtentrytls[0]));
  if (rc < 0) {
    mtcp_printf ("mtcp restore_tls_state: error %d restoring GDT TLS entry[%d]\n", mtcp_sys_errno, thisthread -> gdtentrytls[0].entry_number);
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

/********************************************************************************************************************************/
/*																*/
/*  Set the thread's STOPSIGNAL handler.  Threads are sent STOPSIGNAL when they are to suspend execution the application, save 	*/
/*  their state and wait for the checkpointhread to write the checkpoint file.							*/
/*																*/
/*    Output:															*/
/*																*/
/*	Calling thread will call stopthisthread () when sent a STOPSIGNAL							*/
/*																*/
/********************************************************************************************************************************/

static void setup_sig_handler (void)

{
  void (*oldhandler) (int signum);

  oldhandler = _real_signal (STOPSIGNAL, &stopthisthread);
  if (oldhandler == SIG_ERR) {
    mtcp_printf ("mtcp setupthread: error setting up signal handler: %s\n",
                 strerror (errno));
    mtcp_abort ();
  }
  if ((oldhandler != SIG_IGN) && (oldhandler != SIG_DFL) && (oldhandler != stopthisthread)) {
    mtcp_printf ("mtcp setupthread: signal handler %d already in use (%p).\n"
                 " You may employ a different signal by setting the\n"
                 " environment variable MTCP_SIGCKPT (or DMTCP_SIGCKPT)"
		 " to the number\n of the signal MTCP should "
                 "use for checkpointing.\n", STOPSIGNAL, oldhandler);
    mtcp_abort ();
  }
}

/* Code Added by Kapil Arya */
/********************************************************************************************************************************/
/*                                                                                                                              */
/*  Sync shared memory pages with backup files on disk                                                                          */
/*                                                                                                                              */
/********************************************************************************************************************************/
static void sync_shared_mem(void)
{
  int mapsfd;
  Area area;

  mapsfd = mtcp_sys_open2 ("/proc/self/maps", O_RDONLY);
  if (mapsfd < 0) {
    mtcp_printf ("mtcp sync_shared_memory: error opening /proc/self/maps: %s\n",
                 strerror (mtcp_sys_errno));
    mtcp_abort ();
  }

  while (readmapsline (mapsfd, &area)) {
    /* Skip anything that has no read or execute permission.  This occurs on one page in a Linux 2.6.9 installation.  No idea why.  This code would also take care of kernel sections since we don't have read/execute permission there.  */

    if (!((area.prot & PROT_READ) || (area.prot & PROT_WRITE))) continue;

    if (!(area.flags & MAP_SHARED)) continue;

    if (strstr(area.name, " (deleted)")) continue;

    DPRINTF(("mtcp sync_shared_memory: syncing %X at %p from %s + %X\n", area.size, area.addr, area.name, area.offset));

    if ( msync(area.addr, area.size, MS_SYNC) < 0 ){
      mtcp_printf ("mtcp sync_shared_memory: error syncing %X at %p from %s + %X\n", area.size, area.addr, area.name, area.offset);
      mtcp_abort();
    }
  }

  close (mapsfd);
}


