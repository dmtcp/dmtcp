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

#include "mtcp_ptrace.h"
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

#ifdef PTRACE

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sched.h>
#include <sys/user.h>
#include <sys/syscall.h>

#define GETTID() (int)syscall(SYS_gettid)

int only_once = 0;

const unsigned char DMTCP_SYS_sigreturn =  0x77;
const unsigned char DMTCP_SYS_rt_sigreturn = 0xad;

static const unsigned char linux_syscall[] = { 0xcd, 0x80 };

static __thread int is_waitpid_local = 0;
static __thread int is_ptrace_local = 0;
static __thread pid_t saved_pid = -1;
static __thread int saved_status = -1;
static __thread int has_status_and_pid = 0;

__thread pid_t setoptions_superior = -1;
__thread int is_ptrace_setoptions = FALSE;

static pthread_mutex_t ptrace_pairs_mutex = PTHREAD_MUTEX_INITIALIZER;

struct ckpt_thread {
  pid_t pid;
  pid_t tid;
};

sem_t ptrace_read_pairs_sem;
int init_ptrace_read_pairs_sem = 0;

sem_t __sem;
int init__sem = 0;

char ptrace_shared_file[MAXPATHLEN];
char ptrace_setoptions_file[MAXPATHLEN];
char checkpoint_threads_file[MAXPATHLEN];

int has_ptrace_file = 0;
pid_t delete_ptrace_leader = -1;
int has_setoptions_file = 0;
pid_t delete_setoptions_leader = -1;
int has_checkpoint_file = 0;
pid_t delete_checkpoint_leader = -1;

struct ptrace_tid_pairs ptrace_pairs[MAX_PTRACE_PAIRS_COUNT];
int ptrace_pairs_count = 0;
int init_ptrace_pairs = 0;

/***************************************************************************/
/* THIS CODE MUST BE CHANGED TO CHECK TO SEE IF THE USER CREATES EVEN MORE */
/* THREADS.                                                                */
/***************************************************************************/
#define MAX_CKPT_THREADS 100
static struct ckpt_thread ckpt_threads[MAX_CKPT_THREADS];
static int ckpt_threads_count = 0;

static void have_file(pid_t pid);

static int is_checkpoint_thread (pid_t tid);

static int ptrace_detach_ckpthread(pid_t tgid, pid_t tid, pid_t supid);

static void sort_ptrace_pairs ();

static void print_ptrace_pairs ();

static void reset_ptrace_pairs_entry ( int i );

void check_size_for_ptrace_file (const char *file) {
  struct stat buf;
  if (!stat (file, &buf)) {
    mtcp_printf ("WARNING: %s has %d bytes.\n", file, buf.st_size);
  } else {
    if (errno != ENOENT) {
      mtcp_printf ("WARNING: stat failed for %s with an error different than ENOENT.\n",
                   ptrace_shared_file);
    }
  }
}


void init_thread_local()
{
  is_waitpid_local = 0; // no crash on pre-access
  is_ptrace_local = 0; // no crash on pre-access
  saved_pid = -1; // no crash on pre-access
  saved_status = -1; // no crash on pre-access
  has_status_and_pid = 0; // crash

  setoptions_superior = -1;
  is_ptrace_setoptions = FALSE;
}


/* FIXME:  BAD FUNCTION NAME:  readall(..., ..., count) would guarantee
 * to read 'count' characters.  This reads zero or more characters
 * but does EAGAIN/EINTR processing so that the caller doesn't need to do it.
 * Maybe a name like:  read_no_error() ?
 */
ssize_t readall(int fd, void *buf, size_t count)
{
  int rc;
  do
    rc = read(fd, buf, count);
  while (rc == -1 && (errno == EAGAIN  || errno == EINTR));
  if (rc == -1) { /* if not harmless error */
    mtcp_printf("readall: Internal error\n");
    mtcp_abort();
  }
  return rc; /* else rc >= 0; success */
}

void delete_file (int file, int delete_leader, int has_file)
{
  if ((delete_leader == GETTID()) && has_file) {
    switch (file) {
      case 0: {
        if (unlink(ptrace_shared_file) == -1 && errno != ENOENT) {
          mtcp_printf("delete_file: unlink failed: %s\n",
                      strerror(errno));
          mtcp_abort();
        }
        break;
      }
      case 1: {
        if (unlink(ptrace_setoptions_file) == -1 && errno != ENOENT) {
          mtcp_printf("delete_file: unlink failed: %s\n",
                      strerror(errno));
          mtcp_abort();
        }
        break;
      }
      case 2: {
        if (unlink(checkpoint_threads_file) == -1 && errno != ENOENT) {
          mtcp_printf("delete_file: unlink failed: %s\n",
                      strerror(errno));
          mtcp_abort();
        }
        break;
      }
      default: {
        mtcp_printf ("delete_file: unknown option\n");
      }
    }
  }
}

void ptrace_remove_notexisted()
{
  int i;
  struct ptrace_tid_pairs temp;

  DPRINTF(("<<<<<<<<<<< start ptrace_remove_notexisted %d\n", GETTID()));

  for (i = 0; i < ptrace_pairs_count; i++) {
    int tid = ptrace_pairs[i].inferior;
    char pstate = procfs_state(tid);
    DPRINTF(("checking status of %d = %c\n",tid,pstate));
    if( pstate == 0) {
      // process not exist
      if ( i != (ptrace_pairs_count - 1)) {
        temp = ptrace_pairs[i];
        ptrace_pairs[i] = ptrace_pairs[ptrace_pairs_count - 1];
        ptrace_pairs[ptrace_pairs_count - 1] = temp;
        reset_ptrace_pairs_entry (ptrace_pairs_count - 1);
        i--;
        ptrace_pairs_count--;
      }
      else {
        reset_ptrace_pairs_entry (ptrace_pairs_count - 1);
        ptrace_pairs_count --;
        break;
      }
    }
  }

  print_ptrace_pairs ();
  DPRINTF((">>>>>>>>>>> done ptrace_remove_notexisted %d\n", GETTID()));
}

void ptrace_set_controlling_term(pid_t superior, pid_t inferior)
{
  if (getsid(inferior) == getsid(superior)) {
    char tty_name[80];
    if (mtcp_get_controlling_term(tty_name, 80) == -1) {
      mtcp_printf("ptrace_set_controlling_term: unable to find controlling term\n");
      mtcp_abort();
    }

    int fd = open(tty_name, O_RDONLY);
    if (fd < 0) {
      mtcp_printf("ptrace_set_controlling_term: error %s opening controlling term: %s\n",
                  strerror(errno), tty_name);
      mtcp_abort();
    }

    if (tcsetpgrp(fd, inferior) == -1) {
      mtcp_printf("ptrace_set_controlling_term: tcsetpgrp failed, tty:%s %s\n\n\n\n\n\n",
                  tty_name, strerror(errno));
      mtcp_abort();
    }
    close(fd);
  }
}

void ptrace_attach_threads(int isRestart)
{
  pid_t superior;
  pid_t inferior;
  int last_command;
  int singlestep_waited_on;
  struct user_regs_struct regs;
  long peekdata;
  long low, upp;
  int status;
  unsigned long addr;
  unsigned long int eflags;
  int i;

  DPRINTF(("attach started %d\n", GETTID()));

  /*
  for (i = 0; i < ptrace_pairs_count; i++) {
    mtcp_printf("tid = %d superior = %d inferior = %d last_command = %d\n", GETTID(), ptrace_pairs[i].superior, ptrace_pairs[i].inferior, ptrace_pairs[i].last_command); 
  }
  */
  for (i = 0; i < ptrace_pairs_count; i++) {

    superior = ptrace_pairs[i].superior;
    inferior = ptrace_pairs[i].inferior;
    last_command = ptrace_pairs[i].last_command;
    singlestep_waited_on = ptrace_pairs[i].singlestep_waited_on;

    char inferior_st = ptrace_pairs[i].inferior_st;

//    kill(inferior,0);
    if (superior == GETTID()) {

      DPRINTF (("(attach) tid = %d superior = %d inferior = %d\n",
              GETTID(), (int)superior, (int)inferior));
      // we must make sure the inferior process was created 

      sem_wait( &__sem);
      if (only_once == 0) {
        have_file (superior);
        only_once = 1;
      }
    sem_post( &__sem);
    if(  is_checkpoint_thread(inferior)) {
      have_file (inferior);
      DPRINTF(("ptrace_attach_threads: attach to checkpoint thread: %d\n",inferior));
        //sleep(5);
      is_ptrace_local = 1;
      if (ptrace(PTRACE_ATTACH, inferior, 0, 0) == -1) {
        DPRINTF(("PTRACE_ATTACH failed for parent = %d child = %d\n", (int)superior, (int)inferior));
        perror("ptrace_attach_threads: PTRACE_ATTACH for CKPT failed");
        while(1);
        mtcp_abort();
      }
      is_waitpid_local = 1;
      if (waitpid(inferior, &status, __WCLONE) == -1) {
          perror("ptrace_attach_threads: waitpid for ckpt failed\n");
          mtcp_abort();
      }
      if (WIFEXITED(status)) {
        DPRINTF(("The reason for ckpt child death was %d\n",WEXITSTATUS(status)));
      }else if(WIFSIGNALED(status)) {
        DPRINTF(("The reason for ckpt child death was signal %d\n",WTERMSIG(status)));
      }

      DPRINTF(("ptrace_attach_threads: preCheckpoint state = %c\n",inferior_st));
      if( inferior_st != 'T' ){
        is_ptrace_local = 1;
        if (ptrace(PTRACE_CONT, inferior, 0, 0) < 0) {
          perror("ptrace_attach_threads: PTRACE_CONT failed");
          mtcp_abort();
        }
      }      continue;
    }


      is_ptrace_local = 1;
      if (ptrace(PTRACE_ATTACH, inferior, 0, 0) == -1) {
        mtcp_printf("PTRACE_ATTACH failed for parent = %d child = %d\n", (int)superior, (int)inferior);
        perror("ptrace_attach_threads: PTRACE_ATTACH failed");
          mtcp_abort();
      }
      create_file (inferior);
      while(1) {
//        mtcp_printf("new iter for sup=%d, inf=%d\n",superior,inferior);
        is_waitpid_local = 1;
        if( waitpid(inferior, &status, 0 ) == -1) {
          is_waitpid_local = 1;
          if( waitpid(inferior, &status, __WCLONE ) == -1) {
            while(1);
            perror("ptrace_attach_threads: waitpid failed\n");
            mtcp_abort();
          }
        }
        if (WIFEXITED(status)) {
          DPRINTF(("The reason for childs death was %d\n",WEXITSTATUS(status)));
        }else if(WIFSIGNALED(status)) {
          DPRINTF(("The reason for child's death was signal %d\n",WTERMSIG(status)));
        }

        if (ptrace(PTRACE_GETREGS, inferior, 0, &regs) < 0) {
          perror("ptrace_attach_threads: PTRACE_GETREGS failed");
          mtcp_abort();
        }
#ifdef __x86_64__ 
        peekdata = ptrace(PTRACE_PEEKDATA, inferior, regs.rip, 0);
#else
        peekdata = ptrace(PTRACE_PEEKDATA, inferior, regs.eip, 0);
#endif
        low = peekdata & 0xff;
        peekdata >>=8;
        upp = peekdata & 0xff;

#ifdef __x86_64__
        if ((low == 0xf) && (upp == 0x05) && (regs.rax == 0xf)) {
          /* This code is yet to be written */
          if ( isRestart ) {
            if (last_command == PTRACE_SINGLESTEP_COMMAND ) {
              if (regs.eax == DMTCP_SYS_sigreturn) {
                addr = regs.esp;
              }
              else {
                DPRINTF(("SYS_RT_SIGRETURN\n"));
                //UNTESTED -> TODO; gdb very unclear
                addr = regs.esp + 8;
                addr = ptrace(PTRACE_PEEKDATA, inferior, addr, 0);
                addr += 20;
              }
              addr += EFLAGS_OFFSET;
              errno = 0;
              if ((eflags = ptrace(PTRACE_PEEKDATA, inferior, (void *)addr, 0)) < 0) {
                if (errno != 0) {
                  perror ("ptrace_attach_threads: PTRACE_PEEKDATA failed");
                  mtcp_abort ();
                }
              }
              eflags |= 0x0100;
              printf("inferior = %d addr = %ld eflags = %ld \n", inferior, addr, eflags);
              if (ptrace(PTRACE_POKEDATA, inferior, addr, eflags) < 0) {
                perror("ptrace_attach_threads: PTRACE_POKEDATA failed");
                mtcp_abort();
              }
            }
            else if (inferior_st != 'T' ) {
              /* 
               * TODO: remove in future as GROUP restore becames stable
               *                                                    - Artem              
               */
              //ptrace_set_controlling_term(superior, inferior);

              is_ptrace_local = 1;
              if (ptrace(PTRACE_CONT, inferior, 0, 0) < 0) {
                perror("ptrace_attach_threads: PTRACE_CONT failed");
                mtcp_abort();
              }
            }
          } else {
            if (inferior_st != 'T')
            {
              ptrace_set_controlling_term(superior, inferior);
              is_ptrace_local = 1;
              if (ptrace(PTRACE_CONT, inferior, 0, 0) < 0) {
                perror("ptrace_attach_threads: PTRACE_CONT failed");
                mtcp_abort();
              }
            }
          }

        if (inferior_st == 'T') {
          /* this is needed because we are hitting the same breakpoint 
             twice if we were ckpt at a breakpoint
             info breakpoint was giving incorrect values 
           */
          is_ptrace_local = 1;
          if (ptrace(PTRACE_SINGLESTEP, inferior, 0, 0) < 0) {
            perror("ptrace_attach_threads: PTRACE_SINGLESTEP failed");
            mtcp_abort();
          }
          is_waitpid_local = 1;
          if( waitpid(inferior, &status, 0 ) == -1) {
            is_waitpid_local = 1;
            if( waitpid(inferior, &status, __WCLONE ) == -1) {
              while(1);
              perror("ptrace_attach_threads: waitpid failed\n");
              mtcp_abort();
            }
          }
        }
          break;
        }
        #else
        if (((low == 0xcd) && (upp == 0x80)) &&
                  ((regs.eax == DMTCP_SYS_sigreturn) ||
                   (regs.eax == DMTCP_SYS_rt_sigreturn))) {
          if ( isRestart ) {
            if (last_command == PTRACE_SINGLESTEP_COMMAND ) {
              if (regs.eax == DMTCP_SYS_sigreturn) {
                addr = regs.esp;
              }
              else {
                DPRINTF(("SYS_RT_SIGRETURN\n"));
                //UNTESTED -> TODO; gdb very unclear
                addr = regs.esp + 8;
                addr = ptrace(PTRACE_PEEKDATA, inferior, addr, 0);
                addr += 20;
              }
              addr += EFLAGS_OFFSET;
              errno = 0;
              if ((eflags = ptrace(PTRACE_PEEKDATA, inferior, (void *)addr, 0)) < 0) {
                if (errno != 0) {
                  perror ("ptrace_attach_threads: PTRACE_PEEKDATA failed");
                  mtcp_abort ();
                }
              }
              eflags |= 0x0100;
              if (ptrace(PTRACE_POKEDATA, inferior, (void *)addr, eflags) < 0) {
                perror("ptrace_attach_threads: PTRACE_POKEDATA failed");
                mtcp_abort();
              }
            } else if (inferior_st != 'T') {
              ptrace_set_controlling_term(superior, inferior);
              is_ptrace_local = 1;
              if (ptrace(PTRACE_CONT, inferior, 0, 0) < 0) {
                perror("ptrace_attach_threads: PTRACE_CONT failed");
                mtcp_abort();
              }
            }
          } else {
            if (inferior_st != 'T') {
              ptrace_set_controlling_term(superior, inferior);
              is_ptrace_local = 1;
              if (ptrace(PTRACE_CONT, inferior, 0, 0) < 0) {
                perror("ptrace_attach_threads: PTRACE_CONT failed");
                mtcp_abort();
              }
            }
          }


        if (inferior_st == 'T') {
          /* this is needed because we are hitting the same breakpoint 
             twice if we were ckpt at a breakpoint
             info breakpoint was giving incorrect values 
           */
          is_ptrace_local = 1;
          if (ptrace(PTRACE_SINGLESTEP, inferior, 0, 0) < 0) {
            perror("ptrace_attach_threads: PTRACE_SINGLESTEP failed");
            mtcp_abort();
          }
          is_waitpid_local = 1;
          if( waitpid(inferior, &status, 0 ) == -1) {
            is_waitpid_local = 1;
            if( waitpid(inferior, &status, __WCLONE ) == -1) {
              while(1);
              perror("ptrace_attach_threads: waitpid failed\n");
              mtcp_abort();
            }
          }
        }
          break;
        }
        #endif
        is_ptrace_local = 1;
        if (ptrace(PTRACE_SINGLESTEP, inferior, 0, 0) < 0) {
          perror("ptrace_attach_threads: PTRACE_SINGLESTEP failed");
          mtcp_abort();
        }
      }
    }
    else if (inferior == GETTID()) {

      create_file (superior);
      have_file (inferior);
    }
  }
  DPRINTF(("ptrace_attach_threads: finished for %d\n", GETTID()));
}

void ptrace_detach_checkpoint_threads ()
{
  int i,ret;
  pid_t tgid;

  // Release only checkpoint threads
  for (i = 0; i < ptrace_pairs_count; i++) {
    int tid = ptrace_pairs[i].inferior;
    int sup = ptrace_pairs[i].superior;
    tgid = is_checkpoint_thread (tid);
    if ((sup == GETTID()) && tgid ) {
      DPRINTF(("ptrace_detach_checkpoint_threads: ptrace_detach_ckpthread(%d,%d,%d)\n",
            tgid,tid,sup));
      if( ret = ptrace_detach_ckpthread(tgid,tid,sup) ){
        if( ret == -ENOENT ){
          DPRINTF(("%s: process not exist %d\n",__FUNCTION__,tid));
        }
        mtcp_abort();
      }
    }
  }
  DPRINTF((">>>>>>>>> done ptrace_detach_checkpoint_threads %d\n", GETTID()));
}

void ptrace_detach_user_threads ()
{
  int i;
  int status = 0;

  for(i = 0; i < ptrace_pairs_count; i++) {
    /*
    mtcp_printf("tid = %d superior = %d inferior = %d last_command = %d\n", GETTID(), ptrace_pairs[i].superior, 
        ptrace_pairs[i].inferior, ptrace_pairs[i].last_command);
    */
    if( is_checkpoint_thread(ptrace_pairs[i].inferior) ){
      DPRINTF(("ptrace_detach_user_threads: SKIP checkpoint thread %d\n",ptrace_pairs[i].inferior));
      continue;
    }

    if( ptrace_pairs[i].superior == GETTID()) {
      char pstate;
      // required for all user threads to get SIGUSR2 from their checkpoint thread
      // TODO: to be removed by waiting for the signal to have been delivered
      // sleep(PTRACE_SLEEP_INTERVAL);
      int tid = ptrace_pairs[i].inferior, tpid;
      DPRINTF(("start waiting on %d\n",tid));

      // Check if status of this thread already read by debugger
      pstate = procfs_state(tid);
      DPRINTF(("procfs_state(%d) = %c\n",tid,pstate));
      if( pstate == 0){
      // process not exist
        mtcp_printf("%s: process not exist %d\n",__FUNCTION__,tid);
        mtcp_abort();
      } else if( pstate == 'T'){
        // There can be posibility that GDB (or other) reads status of this
        // thread before us. So we will block. We don't want that.
        // Read anyway but without hang
        DPRINTF(("!!!! Process already stopped !!!!\n"));

        is_waitpid_local = 1;
        tpid = waitpid (tid, &status, WNOHANG);
        if(tpid == -1 && errno == ECHILD){
          DPRINTF(("Check cloned process\n"));
          // Try again with __WCLONE to check cloned processes.
is_waitpid_local = 1;
          if( (tpid = waitpid (tid, &status, __WCLONE | WNOHANG ) ) == -1 ){
            DPRINTF(("ptrace_detach_checkpoint_threads: waitpid(..,__WCLONE): : %s\n",
                        strerror(errno)));
          }
        }

        DPRINTF(("tgid = %d, tpid=%d,stopped=%d is_sigstop=%d,signal=%d\n",
            tid,tpid,WIFSTOPPED(status),WSTOPSIG(status) == SIGSTOP,WSTOPSIG(status)));
      }else{
        // Process not in stopped state. We are in signal handler of GDB thread which waits for status change 
        // for this process. Now it is safe to call blocking waitpid.

        DPRINTF(("!!!! Process is not stopped yet !!!!\n"));
        is_waitpid_local = 1;
        tpid = waitpid (tid, &status, 0);
        if(tpid == -1 && errno == ECHILD){
          DPRINTF(("Check cloned process\n"));
          // Try again with __WCLONE to check cloned processes.
          is_waitpid_local = 1;
          if( (tpid = waitpid (tid, &status, __WCLONE ) ) == -1 ){
            mtcp_printf("ptrace_detach_checkpoint_threads: waitpid(..,__WCLONE): %s\n",
                        strerror(errno));
          }
        }
        DPRINTF(("tgid = %d, tpid=%d,stopped=%d is_sigstop=%d,signal=%d\n",
            tid,tpid,WIFSTOPPED(status),WSTOPSIG(status) == SIGSTOP,WSTOPSIG(status)));
        if(WIFSTOPPED(status)) {
          if (WSTOPSIG(status) == MTCP_DEFAULT_SIGNAL)
            DPRINTF(("user thread %d was stopped by the delivery of MTCP_DEFAULT_SIGNAL\n",tid));          else{  //we should never get here  
            DPRINTF(("user thread %d was stopped by the delivery of %d\n", tid, WSTOPSIG(status))
);
          }
        }else  //we should never end up here 
          DPRINTF(("user thread %d was NOT stopped by a signal\n", ptrace_pairs[i].inferior));
      }

      if (( ptrace_pairs[i].last_command == PTRACE_SINGLESTEP_COMMAND ) &&
          ( ptrace_pairs[i].singlestep_waited_on == FALSE )) {
        //is_waitpid_local = 1;
        has_status_and_pid = 1;
        saved_status = status;
        DPRINTF(("+++++++++++++++++++++++++ptrace_detach_user_threads: AFTER WAITPID %d\n",
                 status));
        ptrace_pairs[i].singlestep_waited_on = TRUE;
        ptrace_pairs[i].last_command = PTRACE_UNSPECIFIED_COMMAND;
      }

      DPRINTF(("tid = %d detaching superior = %d from inferior = %d\n",
               GETTID(), (int)ptrace_pairs[i].superior, (int)ptrace_pairs[i].inferior));
      have_file (ptrace_pairs[i].inferior);
      is_ptrace_local = 1;
      if (ptrace(PTRACE_DETACH, ptrace_pairs[i].inferior, 0, MTCP_DEFAULT_SIGNAL) == -1) {
        DPRINTF(("ptrace_detach_user_threads: parent = %d child = %d\n",
                (int)ptrace_pairs[i].superior,
                (int)ptrace_pairs[i].inferior));
        DPRINTF(("ptrace_detach_user_threads: PTRACE_DETACH failed with error=%d",errno));
      }
    }
  }
  DPRINTF((">>>>>>>>> done ptrace_detach_user_threads %d\n", GETTID()));
}

void ptrace_lock_inferiors()
{
    char file[SYNCHRONIZATIONPATHLEN];
    snprintf(file,SYNCHRONIZATIONPATHLEN,"%s/dmtcp_ptrace_unlocked.%d",dir,GETTID());
    unlink(file);
}

void ptrace_unlock_inferiors()
{
    char file[SYNCHRONIZATIONPATHLEN];
    int fd;
    snprintf(file, SYNCHRONIZATIONPATHLEN, "%s/dmtcp_ptrace_unlocked.%d",dir,GETTID());
    fd = creat(file,0644);
    if( fd < 0 ){
        mtcp_printf("init_lock: Error while creating lock file: %s\n",
                    strerror(errno));
        mtcp_abort();
    }
    close(fd);
}

void create_file(pid_t pid)
{
  char str[SYNCHRONIZATIONPATHLEN];
  int fd;

  memset(str, 0, SYNCHRONIZATIONPATHLEN);
  sprintf(str, "%s/%d", dir, pid);

  fd = open(str, O_CREAT|O_APPEND|O_WRONLY, 0644);
  if (fd == -1) {
    mtcp_printf("create_file: Error opening file %s\n: %s\n",
                str, strerror(errno));
    mtcp_abort();
  }
  if ( close(fd) != 0 ) {
    mtcp_printf("create_file: Error closing file\n: %s\n",
                strerror(errno));
    mtcp_abort();
  }
}

static void have_file(pid_t pid)
{
  char str[SYNCHRONIZATIONPATHLEN];
  int fd;

  memset(str, 0, SYNCHRONIZATIONPATHLEN);
  sprintf(str, "%s/%d", dir, pid);
  while(1) {
    fd = open(str, O_RDONLY);
    if (fd != -1) {
        if (close(fd) != 0) {
        mtcp_printf("have_file: Error closing file: %s\n",
                    strerror(errno));
        mtcp_abort();
        }
      if (unlink(str) == -1) {
        mtcp_printf("have_file: unlink failed: %s\n",
                    strerror(errno));
        mtcp_abort();
      }
      break;
    }
    usleep(100);
  }
}

void ptrace_wait4(pid_t pid)
{    char file[SYNCHRONIZATIONPATHLEN];
    struct stat buf;
    snprintf(file,SYNCHRONIZATIONPATHLEN,"%s/dmtcp_ptrace_unlocked.%d",dir,pid);

    DPRINTF(("%d: Start waiting for superior\n",GETTID()));
    while( stat(file,&buf) < 0 ){
      struct timespec ts;
      DPRINTF(("%d: Superior is not ready\n",GETTID()));
      ts.tv_sec = 0;
      ts.tv_nsec = 100000000;
      if( errno != ENOENT ){
        mtcp_printf("prtrace_wait4: Unexpected error in stat: %d\n",errno);
        mtcp_abort();
      }
      nanosleep(&ts,NULL);
    }
    DPRINTF(("%d: Superior unlocked us\n",GETTID()));
}

/*************************************************************************/
/* Utilities for ptrace code                                             */
/* IF ALL THESE FUNCTIONS MUST EXIST INSIDE mtcp.c AND NOT IN SEAPARATE  */
/* FILE, THEN WE MUST RENAME THEM ALL WITH SOME PREFIX LIKE pt_          */
/* (DIFFERENT NAMESPACE FROM REST OF FILE).  IF WE CAN MOVE THEM TO      */
/* A DIFFERENT FILE, THAT WOULD BE EVEN BETTER.   - Gene                 */
/*************************************************************************/
static void reset_ptrace_pairs_entry ( int i )
{
  ptrace_pairs[i].last_command = PTRACE_UNSPECIFIED_COMMAND;
  ptrace_pairs[i].singlestep_waited_on = FALSE;
  ptrace_pairs[i].free = TRUE;
  ptrace_pairs[i].inferior_st = 'u';
}

static void move_last_ptrace_pairs_entry_to_i ( int i )
{
  ptrace_pairs[i].superior = ptrace_pairs[ptrace_pairs_count-1].superior;
  ptrace_pairs[i].inferior = ptrace_pairs[ptrace_pairs_count-1].inferior;
  ptrace_pairs[i].last_command = ptrace_pairs[ptrace_pairs_count-1].last_command;
  ptrace_pairs[i].singlestep_waited_on = ptrace_pairs[ptrace_pairs_count-1].singlestep_waited_on;
  ptrace_pairs[i].free = ptrace_pairs[ptrace_pairs_count-1].free;
  ptrace_pairs[i].inferior_st = ptrace_pairs[ptrace_pairs_count-1].inferior_st;
}

/* This is called by DMTCP.  BUT IT MUST THEN HAVE A PREFIX LIKE mtcp_
 * IN FRONT OF IT.  WE DON'T WANT TO POLLUTE THE USER'S NAMESPACE.
 * WE'RE A GUEST IN HIS PROCESS.    - Gene
 */
void remove_from_ptrace_pairs ( pid_t superior, pid_t inferior )
{
  int i;
  for (i = 0; i < ptrace_pairs_count; i++) {
    if ((ptrace_pairs[i].superior == superior) && (ptrace_pairs[i].inferior == inferior)) {
      break;
    }
  }
  if (i == ptrace_pairs_count) return;
  if (i != (ptrace_pairs_count-1)) {
    pthread_mutex_lock(&ptrace_pairs_mutex);
    move_last_ptrace_pairs_entry_to_i(i);
    reset_ptrace_pairs_entry(ptrace_pairs_count-1);
    ptrace_pairs_count--;
    pthread_mutex_unlock(&ptrace_pairs_mutex);
  }
  else {
    pthread_mutex_lock(&ptrace_pairs_mutex);
    reset_ptrace_pairs_entry(i);
    ptrace_pairs_count--;
    pthread_mutex_unlock(&ptrace_pairs_mutex);
  }
}

static int is_in_ptrace_pairs ( pid_t superior, pid_t inferior )
{
  int i;
  for (i = 0; i < ptrace_pairs_count; i++) {
    if ((ptrace_pairs[i].superior == superior) && (ptrace_pairs[i].inferior == inferior)) return i;
  }
  return -1;
}

static void add_to_ptrace_pairs ( pid_t superior, pid_t inferior, int last_command, int singlestep_waited_on )
{
  struct ptrace_tid_pairs new_pair;

  new_pair.superior = superior;
  new_pair.inferior = inferior;
  new_pair.last_command = last_command;
  new_pair.singlestep_waited_on = singlestep_waited_on;
  new_pair.free = FALSE;
  new_pair.inferior_st = 'u';
  new_pair.eligible_for_deletion = TRUE;

  pthread_mutex_lock(&ptrace_pairs_mutex);
  ptrace_pairs[ptrace_pairs_count] = new_pair;
  ptrace_pairs_count++;
  pthread_mutex_unlock(&ptrace_pairs_mutex);
}

/* This is called by DMTCP.  BUT IT MUST THEN HAVE A PREFIX LIKE mtcp_
 * IN FRONT OF IT.  WE DON'T WANT TO POLLUTE THE USER'S NAMESPACE.
 * WE'RE A GUEST IN HIS PROCESS.    - Gene
 */
void handle_command ( pid_t superior, pid_t inferior, int last_command )
{
  int index = is_in_ptrace_pairs ( superior, inferior );
  if ( index >= 0 ) {
    ptrace_pairs[index].last_command = last_command;
    if ( last_command == PTRACE_SINGLESTEP_COMMAND ) ptrace_pairs[index].singlestep_waited_on = FALSE;
  }
  else {
    /* not in the ptrace pairs array; reason: inferior did an PTRACE_TRACEME and now the superior is issuing commands */
    add_to_ptrace_pairs( superior, inferior, last_command, FALSE );
  }
}

enum {
  SORT_BY_SUPERIOR = 0,
  SORT_BY_INFERIOR
};

static int find_slot (int seed) {
  int i;
  pid_t inferior;
  for (i = seed; i >= 0; i--) {
    inferior = ptrace_pairs[i].inferior;
    if (!is_checkpoint_thread(inferior))
      return i;   
  }
  return -1;
}

/* This function moves all checkpoint threads at the end of ptrace_pairs array.
 * It returns the index in ptrace_pairs array where ckpt threads are located. */
static int move_ckpt_threads_towards_end () {
  int i;
  struct ptrace_tid_pairs temp;
  pid_t superior;
  pid_t inferior;
  int limit = ptrace_pairs_count;
  int ckpt_threads = 0;
  
  int slot = find_slot(ptrace_pairs_count - 1);
  for (i = 0; i < limit; i++) {
    inferior = ptrace_pairs[i].inferior;
    if (is_checkpoint_thread(inferior)) {
      ckpt_threads++;
      if (slot > i) {
        temp = ptrace_pairs[i];
        ptrace_pairs[i] = ptrace_pairs[slot];
        ptrace_pairs[slot] = temp;
        limit = slot;
        slot = find_slot(slot - 1);
      }
    }
  }
  return ckpt_threads;
}

static pid_t get_pid_by_key (int key, int index) {
  if (key == SORT_BY_SUPERIOR)
    return ptrace_pairs[index].superior;
  else if (key == SORT_BY_INFERIOR)
    return ptrace_pairs[index].inferior;
  return -1;
}

static void sort_ptrace_pairs_by_key (int key, int begin, int end) {
  int i, j;
  pid_t upper_pid;
  pid_t inner_pid;
  pid_t temp_pid;
  struct ptrace_tid_pairs temp;
  for (i = begin; i < (end - 1); i++) {
    upper_pid = get_pid_by_key (key, i);
    for (j = i + 1; j < end; j++) {
      inner_pid = get_pid_by_key (key, j);
      if (upper_pid < inner_pid) {
        temp = ptrace_pairs[i];
        ptrace_pairs[i] = ptrace_pairs[j];
        ptrace_pairs[j] = temp; 
        temp_pid = upper_pid;
        upper_pid = inner_pid;
        inner_pid = temp_pid;
      }
    }  
  }
}

static void sort_ptrace_pairs_within_limit (int begin, int end) {
  sort_ptrace_pairs_by_key (SORT_BY_SUPERIOR, begin, end);
  int ref_superior;
  int superior;
  int inferior;
  int start = begin;
  int i;
  ref_superior = ptrace_pairs[0].superior;
  for (i = (start + 1); i < end; i++) {
    superior = ptrace_pairs[i].superior;
    if (superior != ref_superior) {
      sort_ptrace_pairs_by_key (SORT_BY_INFERIOR, start, i);
      ref_superior = superior;
      start = i;
    }
  }
  sort_ptrace_pairs_by_key (SORT_BY_INFERIOR, start, end);
}

static void sort_ptrace_pairs () {
  if (ptrace_pairs_count <= 1)
    return;
  int limit = ptrace_pairs_count - move_ckpt_threads_towards_end ();
  /* sort all non-checkpoint threads*/
  sort_ptrace_pairs_within_limit (0, limit);
  /* sort all checkpoint threads */
  sort_ptrace_pairs_within_limit (limit, ptrace_pairs_count);
}

static void print_ptrace_pairs ()
{
  int i;
  DPRINTF(("\n\n"));
  for ( i = 0; i < ptrace_pairs_count; i++ )
     DPRINTF(("tid = %d superior = %d inferior = %d \n",
              GETTID(), (int)ptrace_pairs[i].superior, (int)ptrace_pairs[i].inferior));
  DPRINTF(("tid = %d ptrace_pairs_count = %d \n", GETTID(), ptrace_pairs_count));
  DPRINTF(("\n\n"));
}

/* This is called by DMTCP.  BUT IT MUST THEN HAVE A PREFIX LIKE mtcp_
 * IN FRONT OF IT.  WE DON'T WANT TO POLLUTE THE USER'S NAMESPACE.
 * WE'RE A GUEST IN HIS PROCESS.    - Gene
 */


void write_info_to_file (int file, pid_t superior, pid_t inferior)
{
  int fd;
  struct flock lock;

  switch (file) {
    case 0: {
      fd = open(ptrace_shared_file, O_CREAT|O_APPEND|O_WRONLY|O_FSYNC, 0644);
      break;
    }
    case 1: {
      fd = open(ptrace_setoptions_file, O_CREAT|O_APPEND|O_WRONLY|O_FSYNC, 0644);
      break;
    }
    case 2: {
      fd = open(checkpoint_threads_file, O_CREAT|O_APPEND|O_WRONLY|O_FSYNC, 0644);
      break;
    }
    default: {
      mtcp_printf ("write_info_to_file: unknown option\n");
      return;
    }
  }

  if (fd == -1) {
    mtcp_printf("write_info_to_file: Error opening file\n: %s\n",
                strerror(errno));
    abort();
  }

  lock.l_type = F_WRLCK;
  lock.l_whence = SEEK_CUR;
  lock.l_start = 0;
  lock.l_len = 0;
  lock.l_pid = getpid();

  if (fcntl(fd, F_GETLK, &lock ) == -1) {
    mtcp_printf("write_info_to_file: Error acquiring lock: %s\n",
                strerror(errno));
    abort();
  }

  if (write(fd, &superior, sizeof(pid_t)) == -1) {
    mtcp_printf("write_info_to_file: Error writing to file: %s\n",
                strerror(errno));
    abort();
  }
  if (write(fd, &inferior, sizeof(pid_t)) == -1) {
    mtcp_printf("write_info_to_file: Error writing to file: %s\n",
                strerror(errno));
    abort();
  }

  lock.l_type = F_UNLCK;
  lock.l_whence = SEEK_CUR;
  lock.l_start = 0;
  lock.l_len = 0;

  if (fcntl(fd, F_SETLK, &lock) == -1) {
    mtcp_printf("write_info_to_file: Error releasing lock: %s\n",
                strerror(errno));
    abort();
  }
  if (close(fd) != 0) {
    mtcp_printf("write_info_to_file: Error closing file: %s\n",
                strerror(errno));
    abort();
  }
}

/* This is called by DMTCP.  BUT IT MUST THEN HAVE A PREFIX LIKE mtcp_
 * IN FRONT OF IT.  WE DON'T WANT TO POLLUTE THE USER'S NAMESPACE.
 * WE'RE A GUEST IN HIS PROCESS.    - Gene
 */
void writeptraceinfo (pid_t superior, pid_t inferior)
{
  int index = is_in_ptrace_pairs ( superior, inferior );
  if (index == -1 ) {
    write_info_to_file (0, superior, inferior);
    add_to_ptrace_pairs ( superior, inferior, PTRACE_UNSPECIFIED_COMMAND, FALSE );
  }
}

/***********************************************************************
 * This is called by DMTCP.  BUT IT MUST THEN HAVE A PREFIX LIKE mtcp_
 * IN FRONT OF IT.  WE DON'T WANT TO POLLUTE THE USER'S NAMESPACE.
 * WE'RE A GUEST IN THE USER'S PROCESS.    - Gene
 ***********************************************************************/
void set_singlestep_waited_on ( pid_t superior, pid_t inferior,
                                       int value )
{
  int index = is_in_ptrace_pairs ( superior, inferior );
  if (( index >= 0 ) && ( ptrace_pairs[index].last_command == PTRACE_SINGLESTEP_COMMAND ))
    ptrace_pairs[index].singlestep_waited_on = value;
  if (index >= 0)
        ptrace_pairs[index].last_command = PTRACE_UNSPECIFIED_COMMAND;
}

/* This is called by DMTCP.  BUT IT MUST THEN HAVE A PREFIX LIKE mtcp_
 * IN FRONT OF IT.  WE DON'T WANT TO POLLUTE THE USER'S NAMESPACE.
 * WE'RE A GUEST IN HIS PROCESS.    - Gene
 */
int get_is_waitpid_local ()
{
  return is_waitpid_local;
}

/* This is called by DMTCP.  BUT IT MUST THEN HAVE A PREFIX LIKE mtcp_
 * IN FRONT OF IT.  WE DON'T WANT TO POLLUTE THE USER'S NAMESPACE.
 * WE'RE A GUEST IN HIS PROCESS.    - Gene
 */
int get_is_ptrace_local ()
{
  return is_ptrace_local;
}

/* This is called by DMTCP.  BUT IT MUST THEN HAVE A PREFIX LIKE mtcp_
 * IN FRONT OF IT.  WE DON'T WANT TO POLLUTE THE USER'S NAMESPACE.
 * WE'RE A GUEST IN HIS PROCESS.    - Gene
 */
void unset_is_waitpid_local ()
{
  is_waitpid_local = 0;
}

/* This is called by DMTCP.  BUT IT MUST THEN HAVE A PREFIX LIKE mtcp_
 * IN FRONT OF IT.  WE DON'T WANT TO POLLUTE THE USER'S NAMESPACE.
 * WE'RE A GUEST IN HIS PROCESS.    - Gene
 */
void unset_is_ptrace_local ()
{
  is_ptrace_local = 0;
}

/* This is called by DMTCP.  BUT IT MUST THEN HAVE A PREFIX LIKE mtcp_
 * IN FRONT OF IT.  WE DON'T WANT TO POLLUTE THE USER'S NAMESPACE.
 * WE'RE A GUEST IN HIS PROCESS.    - Gene
 */
pid_t get_saved_pid ()
{
  return saved_pid;
}

/* This is called by DMTCP.  BUT IT MUST THEN HAVE A PREFIX LIKE mtcp_
 * IN FRONT OF IT.  WE DON'T WANT TO POLLUTE THE USER'S NAMESPACE.
 * WE'RE A GUEST IN HIS PROCESS.    - Gene
 */
int get_saved_status ()
{
  return saved_status;
}

/* This is called by DMTCP.  BUT IT MUST THEN HAVE A PREFIX LIKE mtcp_
 * IN FRONT OF IT.  WE DON'T WANT TO POLLUTE THE USER'S NAMESPACE.
 * WE'RE A GUEST IN HIS PROCESS.    - Gene
 */
int get_has_status_and_pid ()
{
  return has_status_and_pid;
}

/* This is called by DMTCP.  BUT IT MUST THEN HAVE A PREFIX LIKE mtcp_
 * IN FRONT OF IT.  WE DON'T WANT TO POLLUTE THE USER'S NAMESPACE.
 * WE'RE A GUEST IN HIS PROCESS.    - Gene
 */
void reset_pid_status ()
{
  saved_pid = -1;
  saved_status = -1;
  has_status_and_pid = 0;
}

static int is_alive (pid_t pid)
{
  char str[20];
  int fd;

  memset(str, 0, 20);
    sprintf(str, "/proc/%d/maps", pid);

  fd = open(str, O_RDONLY);
    if (fd != -1) {
      if ( close(fd) != 0 ) {
      mtcp_printf("is_alive: Error closing file: %s\n",
                  strerror(errno));
      mtcp_abort();
      }
    return 1;
  }
  return 0;
}

static int is_in_ckpt_threads (pid_t pid)
{
  int i;
  for (i = 0; i < ckpt_threads_count; i++) {
    if (ckpt_threads[i].pid == pid) return 1;
  }
  return 0;
}
static void print_ckpt_threads ()
{
  int i;
  for (i = 0; i < ckpt_threads_count; i++)
    DPRINTF(("moa = %d pid = %d tid = %d \n",
             GETTID(), ckpt_threads[i].pid, ckpt_threads[i].tid));
}

char procfs_state(int tid)
{
  char name[64];
  char sbuf[256], *S, *tmp;
  char state;
  int num_read, fd;

  sprintf(name,"/proc/%d/stat",tid);
  fd = open(name, O_RDONLY, 0);
  if( fd < 0 ){
    mtcp_printf("procfs_status: cannot open %s\n",name);
    return 0;
  }
  /* THIS CODE CAN'T WORK RELIABLY.  SUPPOSE read() RETURNS 0,
   * OR -1 WITH EAGAIN OR EINTR?  LOOK FOR EXAMPLES IN
   *  mtcp_restart_nolibc.c:readfile() OR ELSEWHERE.   - Gene
   */
  num_read = read(fd, sbuf, sizeof sbuf - 1);
  close(fd);
  if(num_read<=0) {
    return 0;
  }
  sbuf[num_read] = '\0';

  S = strchr(sbuf, '(') + 1;
  tmp = strrchr(S, ')');
  S = tmp + 2;                 // skip ") "

  /* YOU SEEM TO WANT S[0] HERE.  WHY sscanf?  ALSO WHY ARE WE USING
   * CAPS ("S") FOR VAR NAME?  - Gene
   */
  sscanf(S, "%c", &state);

  return state;
}

void process_ptrace_info (pid_t *delete_ptrace_leader,
        int *has_ptrace_file,
        pid_t *delete_setoptions_leader, int *has_setoptions_file,
        pid_t *delete_checkpoint_leader, int *has_checkpoint_file)
{
  int ptrace_fd = -1;
  siginfo_t infoop;
  int i;
  pid_t superior;
  pid_t inferior;
  int setoptions_fd = -1;
  struct ptrace_tid_pairs temp;
  int checkpoint_fd = -1;
  pid_t pid;
  pid_t tid;
  struct ckpt_thread ckpt_thread_temp;

  DPRINTF((">>>>>>>>>>>>>>>>>>>>>>>>>>>>process_ptrace_info: thread = %d\n", GETTID()));

// TODO: consider that only checkpoint thread now runs this code  
//  if (thread == motherofall) {
    // read the information from the ptrace file  
    ptrace_fd = open(ptrace_shared_file, O_RDONLY);
    if (ptrace_fd != -1) {
      *has_ptrace_file = 1;
      while (readall(ptrace_fd, &superior, sizeof(pid_t)) > 0) {
        readall(ptrace_fd, &inferior, sizeof(pid_t));
        if ( is_in_ptrace_pairs(superior, inferior) == -1 ) {
          add_to_ptrace_pairs(superior, inferior, PTRACE_UNSPECIFIED_COMMAND, FALSE);
        }
        if (*delete_ptrace_leader < superior)
          *delete_ptrace_leader = superior;
      }
        if ( close(ptrace_fd) != 0 ) {
        mtcp_printf("process_ptrace_info: Error closing file. Error: %s\n", strerror(errno));
        mtcp_abort();
        }

      /* delete all dead threads */
      for (i = 0; i < ptrace_pairs_count; i++) {
        if ((!is_alive(ptrace_pairs[i].superior) || !is_alive(ptrace_pairs[i].inferior))
                   &&
            (ptrace_pairs[i].eligible_for_deletion == TRUE)) {
          if ( i != (ptrace_pairs_count - 1)) {
            temp = ptrace_pairs[i];
            ptrace_pairs[i] = ptrace_pairs[ptrace_pairs_count - 1];
            ptrace_pairs[ptrace_pairs_count - 1] = temp;
            reset_ptrace_pairs_entry (ptrace_pairs_count - 1);
            i--;
            ptrace_pairs_count --;
          }
          else {
            reset_ptrace_pairs_entry (ptrace_pairs_count - 1);
            ptrace_pairs_count --;
            break;
          }
        }
      }

      /* none of the eligible for deletion entries can be deleted anymore */
      for (i = 0; i < ptrace_pairs_count; i++) {
        /* SHOULDN'T "=" BE REPLACED BY "==" IN IF CONDITION?  - Gene */
        if (ptrace_pairs[i].eligible_for_deletion = TRUE)
          ptrace_pairs[i].eligible_for_deletion = FALSE;
      }
    }
    else mtcp_printf("process_ptrace_info: NO ptrace file\n");

    // read the information from the setoptions file
    setoptions_fd = open(ptrace_setoptions_file, O_RDONLY);
    if (setoptions_fd != -1) {
      *has_setoptions_file = 1;
      while (readall(setoptions_fd, &superior, sizeof(pid_t)) > 0) {
        readall(setoptions_fd, &inferior, sizeof(pid_t));
        if (inferior == GETTID()) {
          setoptions_superior = superior;
          is_ptrace_setoptions = TRUE;
        }
        if (*delete_setoptions_leader < superior)
          *delete_setoptions_leader = superior;
      }
        if ( close(setoptions_fd) != 0 ) {
        mtcp_printf("process_ptrace_info: Error closing file: %s\n", strerror(errno));
        mtcp_abort();
        }
    }
    else mtcp_printf ("process_ptrace_info: NO setoptions file\n");

    // read the information from the checkpoint threads file  
    /* GDB specific code */
    checkpoint_fd = open(checkpoint_threads_file, O_RDONLY);
    if (checkpoint_fd != -1) {
      *has_checkpoint_file = 1;
      while (readall(checkpoint_fd, &pid, sizeof(pid_t)) >  0) {
        readall(checkpoint_fd, &tid, sizeof(pid_t));
        DPRINTF(("{%d} checkpoint threads: pid = %d tid = %d\n", GETTID(), pid, tid));
        if (is_alive(pid) && is_alive(tid)) {
           /* only the pid matters 
            * for the first alive tid & pid, then tid is the ckpt of pid 
            */
          if (!is_in_ckpt_threads(pid)) {
            ckpt_thread_temp.pid = pid;
            ckpt_thread_temp.tid = tid;
            ckpt_threads[ckpt_threads_count] = ckpt_thread_temp;
            ckpt_threads_count++;
          }
          if (*delete_checkpoint_leader < pid)
            *delete_checkpoint_leader = pid;
        }
      }
        if ( close(checkpoint_fd) != 0 ) {
        mtcp_printf("process_ptrace_info: Error closing file: %s\n", strerror(errno));
        mtcp_abort();
        }
    }
    else mtcp_printf("process_ptrace_info: NO checkpoint file\n");

    sort_ptrace_pairs ();

    print_ptrace_pairs ();

    print_ckpt_threads ();

    // We dont need sem_post anymore because this function is only called by checkpoint thread
    // TODO: remove semaphor-related stuff

    /* allow all other threads to proceed 
     * for all the threads excluding motherofall and checkpoint thread */
/*     
    for (loopthread = threads; loopthread != NULL && loopthread->next != NULL && loopthread->next->next != NULL;
                  loopthread = loopthread->next) { 
      sem_post(&ptrace_read_pairs_sem);

    }
  }else 
    // all threads with the exception of motherofall (checkpoint thread does NOT run this code) wait for motherofall to write
    //   the info from the ptrace file to memory (shared among all threads of a process) 
    sem_wait(&ptrace_read_pairs_sem); 
*/

}

static int is_checkpoint_thread (pid_t tid)
{
  int i;
  for (i = 0; i < ckpt_threads_count; i++) {
    if (ckpt_threads[i].tid == tid ) return ckpt_threads[i].pid;
  }
  return 0;
}

static int ptrace_detach_ckpthread(pid_t tgid, pid_t tid, pid_t supid)
{
  int status;
  char pstate;
  pid_t tpid;

  DPRINTF(("detach_ckpthread: tid=%d, tgid = %d >>>>>>>>>>>>>>>>>>\n", tid, tgid));

  pstate = procfs_state(tid);
  DPRINTF(("detach_ckpthread: CKPT Thread procfs_state(%d) = %c\n", tid, pstate));
  if( pstate == 0 ){
  // such process not exist 
    return -ENOENT;
  }else if (pstate == 'T') {
    // There can be posibility that GDB (or other) reads status of this
    // thread before us. So we will block. We don't want that.    // Read anyway but without hang
    DPRINTF(("detach_ckpthread: Checkpoint thread already stopped\n"));

    is_waitpid_local = 1;
    tpid = waitpid(tid, &status, WNOHANG);
    if (tpid == -1 && errno == ECHILD) {
      DPRINTF(("detach_ckpthread: Check cloned process\n"));
      // Try again with __WCLONE to check cloned processes.
      is_waitpid_local = 1;
      if ((tpid = waitpid(tid, &status, __WCLONE | WNOHANG)) == -1) {
        DPRINTF(("detach_ckpthread: ptrace_detach_checkpoint_threads: waitpid(..,__WCLONE): %s\n"
,
                    strerror(errno)));
      }
    }
    DPRINTF(("detach_ckpthread: tgid = %d, tpid=%d,stopped=%d is_sigstop=%d,signal=%d\n",
             tid, tpid, WIFSTOPPED(status),
             WSTOPSIG(status) == SIGSTOP, WSTOPSIG(status)));
  }else{
    /*
     * and if inferior is a checkpoint thread 
     */
    if (kill(tid, SIGSTOP) == -1) {
      mtcp_printf("detach_ckpthread: ptrace_detach_checkpoint_threads: kill: %s\n",
                  strerror(errno));
      return -EAGAIN;
    }
    is_waitpid_local = 1;
    tpid = waitpid(tid, &status, 0);
    DPRINTF(("detach_ckpthread: tpid1=%d,errno=%d,ECHILD=%d\n", tpid, errno, ECHILD));
    if ((tpid) == -1 && errno == ECHILD) {
      DPRINTF(("detach_ckpthread: Check cloned process\n"));
      /*
       * Try again with __WCLONE to check cloned processes.  
       */
      is_waitpid_local = 1;
      if ((tpid = waitpid(tid, &status, __WCLONE)) == -1) {
        mtcp_printf("detach_ckpthread: ptrace_detach_checkpoint_threads: waitpid(..,__WCLONE): %s\n",
                    strerror(errno));
        return -EAGAIN;
      }
    }
  }
  DPRINTF(("detach_ckpthread: tgid = %d, tpid=%d,stopped=%d is_sigstop=%d,"
           "signal=%d,err=%s\n",
           tid, tpid, WIFSTOPPED(status),WSTOPSIG(status) == SIGSTOP,
           WSTOPSIG(status), strerror(errno)));
  if (WIFSTOPPED(status)) {
    if (WSTOPSIG(status) == SIGSTOP)
      DPRINTF(("detach_ckpthread: checkpoint thread %d was stopped by the delivery of SIGSTOP\n",tid));
    else {                      // we should never get here 
      DPRINTF(("detach_ckpthread: checkpoint thread %d was stopped by the delivery of %d\n",
               tid,WSTOPSIG(status)));
    }
  } else                        // we should never end up here 
    DPRINTF(("detach_ckpthread: checkpoint thread %d was NOT stopped by a signal\n", tid));

  is_ptrace_local = 1;
  if (ptrace(PTRACE_DETACH, tid, 0, SIGCONT) == -1) {
    DPRINTF(("detach_ckpthread: ptrace_detach_checkpoint_threads: parent = %d child = %d\n",
             supid, tid));
    DPRINTF(("detach_ckpthread: ptrace_detach_checkpoint_threads: PTRACE_DETACH failed: %s\n",
             strerror(errno)));
    return -EAGAIN;
  }

  DPRINTF(("detach_ckpthread: tid=%d, tgid = %d <<<<<<<<<<<<<<<<<<<<<<<<<<\n", tid, tgid));
  only_once = 0; /* no need for semaphore here - the UT execute this code */
  return 0;
}

static int ptrace_control_ckpthread(pid_t tgid, pid_t tid)
{
  int status;
  char pstate;
  pid_t tpid;

  DPRINTF(("control_ckpthread: tid=%d, tgid = %d >>>>>>>>>>>>>>>>>>>>>>>\n", tid, tgid));

  pstate = procfs_state(tid);
  DPRINTF(("control_ckpthread: CKPT Thread procfs_state(%d) = %c\n", tid, pstate));

  if( pstate == 0 ){
    // process not exist
    return -ENOENT;
  }else if( pstate == 'T') {
    // There can be posibility that GDB (or other) reads status of this
    // thread before us. So we will block. We don't want that.
    // Read anyway but without hang
    DPRINTF(("control_ckpthread: Checkpoint thread stopped by controlled debugger\n"));

    if( mtcp_sys_kernel_tkill(tid,SIGCONT) )
      return -EAGAIN;

    DPRINTF(("control_ckpthread: Check cloned process\n"));
    // Try again with __WCLONE to check cloned processes.
    is_waitpid_local = 1;
    if ((tpid = waitpid(tid, &status, __WCLONE | WNOHANG)) == -1) {
      mtcp_printf("control_ckpthread: ptrace_detach_checkpoint_threads: waitpid(..,__WCLONE): %s\n",
                  strerror(errno));
      return -EAGAIN;
    }

    DPRINTF(("control_ckpthread: tgid = %d, tpid=%d,continued=%d,err=%s\n",
       tid, tpid, WIFCONTINUED(status), strerror(errno)));

    if( WIFCONTINUED(status) ) {
      DPRINTF(("control_ckpthread: checkpoint thread %d was stopped by the delivery of SIGSTOP\n",tid));
    }else{                        // we should never end up here 
      DPRINTF(("control_ckpthread: checkpoint thread %d was NOT stopped by a signal\n", tid));
    }
  }

  DPRINTF(("control_ckpthread: tid=%d, tgid = %d <<<<<<<<<<<<<<<<<<<<<<<<<<\n", tid, tgid));
  return 0;
}

void ptrace_save_threads_state ()
{
  int i;

  DPRINTF((">>>>>>>>> start ptrace_save_threads_state %d\n", GETTID()));

  for(i = 0; i < ptrace_pairs_count; i++) {
  /*
    if( is_checkpoint_thread(ptrace_pairs[i].inferior) ){
      mtcp_printf("ptrace_detach_user_threads: SKIP checkpoint thread %d\n",ptrace_pairs[i].inferior);
      continue;
    }
  */
    //if( ptrace_pairs[i].superior == GETTID()){
      char pstate;
      int tid = ptrace_pairs[i].inferior;
      pstate = procfs_state(tid);
      DPRINTF(("save state of thread %d = %c\n",tid,pstate));
      ptrace_pairs[i].inferior_st = pstate;
    //}     
  }
  DPRINTF((">>>>>>>>> done ptrace_save_threads_state %d\n", GETTID()));
}

#endif
