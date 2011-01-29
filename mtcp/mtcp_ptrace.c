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

#ifndef _GNU_SOURCE
# define _GNU_SOURCE /* Needed for syscall declaration */
#endif
#define _XOPEN_SOURCE 500 /* _XOPEN_SOURCE >= 500 needed for getsid */
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

/*  A cleaner way to write this code is to define in this file:
 *  static pid_t dmtcp_waitpid(pid_t pid, int *status, int options) {
 *    int rc;
 *    is_waitpid_local = 1;
 *    rc = waitpid(pid, status, options);
 *    is_waitpid_local = 0;
 *    return rc;
 *  }
 *  Then, in pidwrappers.cpp:
 *  extern "C"
 *  static mtcp_is_waitpid_local_ptr = dmtcp_get_mtcp_symbol("is_waitpid_local");
 *    and then use   *mtcp_is_waitpid_local  directly in the waitpid wrapper.
 *
 *  Then remove get_is_waitpid_local and unset_is_waitpid_local _everywhere_,
 *   including removing it from mtcpinterface.cpp and from this file.
 *  Also then remove all other occurences of is_waitpid_local in this file.
 *  One can also do the same for is_ptrace_local.  All of this will
 *  simplify the current code a lot.
 *
 *  If you agree, please do it.  Otherwise, let's discuss it.  Thanks, - Gene
 */
/* All of these variables are for the benefit of pidwrappers.cpp.  Please
 * Consider making them fields of a struct, and using dmtcp_get_mtcp_symbol()
 * on a single pointer to the struct to be added here, instead of
 * repeatedly calling dmtcp_get_mtcp_symbol() on a huge number of functions
 * that access these variables.  - Gene.
 */
static __thread int is_waitpid_local = 0; /* true if waitpid called by DMTCP */
static __thread int is_ptrace_local = 0; /* true if ptrace called by DMTCP */
static __thread pid_t saved_pid = -1;
static __thread int saved_status = -1;
static __thread int has_status_and_pid = 0;

__thread pid_t setoptions_superior = -1;
__thread int is_ptrace_setoptions = FALSE;

sem_t __sem;
int init__sem = 0;

char dir[MAXPATHLEN];
char new_ptrace_shared_file[MAXPATHLEN];
char ptrace_shared_file[MAXPATHLEN];
char ptrace_setoptions_file[MAXPATHLEN];
char checkpoint_threads_file[MAXPATHLEN];
char ckpt_leader_file[MAXPATHLEN];

int empty_ptrace_info(struct ptrace_info pt_info) {
  return pt_info.superior && pt_info.inferior;
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

void ptrace_set_controlling_term(pid_t superior, pid_t inferior)
{
  if (getsid(inferior) == getsid(superior)) {
    char tty_name[80];
    if (mtcp_get_controlling_term(tty_name, 80) == -1) {
      mtcp_printf("ptrace_set_controlling_term: unable to find ctrling term\n");
      mtcp_abort();
    }

    int fd = open(tty_name, O_RDONLY);
    if (fd < 0) {
      mtcp_printf("ptrace_set_controlling_term: error %s opening ctrlling term "                  " %s\n", strerror(errno), tty_name);
      mtcp_abort();
    }

    if (tcsetpgrp(fd, inferior) == -1) {
      mtcp_printf("ptrace_set_controlling_term: tcsetpgrp failed, tty:%s %s\n",
                  tty_name, strerror(errno));
      mtcp_abort();
    }
    close(fd);
  }
}

/* In this function the superiors attach to their inferiors, at resume time
 * or after restart. */
void ptrace_attach_threads(int isRestart)
{
  if (!callback_get_next_ptrace_info) return;

  pid_t superior;
  pid_t inferior;
  int last_command;
  int singlestep_waited_on;
  char inferior_st;
  int inferior_is_ckpthread;
  struct user_regs_struct regs;
  long peekdata;
  long low, upp;
  int status;
  unsigned long addr;
  unsigned long int eflags;
  int i;
  struct ptrace_info pt_info;

  DPRINTF(("ptrace_attach_threads: %d started.\n", GETTID()));

  int index = 0;
  while (empty_ptrace_info(
           pt_info = (*callback_get_next_ptrace_info)(index++))) {
    superior = pt_info.superior;
    inferior = pt_info.inferior;
    last_command = pt_info.last_command;
    singlestep_waited_on = pt_info.singlestep_waited_on;
    inferior_st = pt_info.inferior_st;
    inferior_is_ckpthread = pt_info.inferior_is_ckpthread;

    if (superior == GETTID()) {
      DPRINTF (("pthread_attach_threads: GETTID = %d superior = %d "
                "inferior = %d\n", GETTID(), superior, inferior));

      /* We must make sure the inferior process was created. */
      sem_wait( &__sem);
      if (only_once == 0) {
        have_file (superior);
        only_once = 1;
      }
      sem_post( &__sem);

      /* If the inferior is the checkpoint thread, attach. */
      if (inferior_is_ckpthread) {
        have_file (inferior);
        DPRINTF(("ptrace_attach_threads: %d attaching to ckptthread: %d\n",
                superior, inferior));
        is_ptrace_local = 1;
        if (ptrace(PTRACE_ATTACH, inferior, 0, 0) == -1) {
          perror("ptrace_attach_threads: PTRACE_ATTACH for ckpthread failed.");
          mtcp_abort();
        }
        is_waitpid_local = 1;
        if (waitpid(inferior, &status, __WCLONE) == -1) {
          perror("ptrace_attach_threads: waitpid for ckpt failed\n");
          mtcp_abort();
        }
        if (WIFEXITED(status)) {
          mtcp_printf("ptrace_attach_threads: ckpthread is dead because %d\n",
                      WEXITSTATUS(status));
        } else if(WIFSIGNALED(status)) {
          mtcp_printf("ptrace_attach_threads: ckpthread is dead because of "
                      "signal %d\n",WTERMSIG(status));
        }
        DPRINTF(("ptrace_attach_threads: preCheckpoint state = %c\n",
                inferior_st));
        if (inferior_st != 'T') {
          is_ptrace_local = 1;
          if (ptrace(PTRACE_CONT, inferior, 0, 0) < 0) {
            perror("ptrace_attach_threads: PTRACE_CONT failed");
            mtcp_abort();
          }
        }
        continue;
      }

      /* Attach to the user threads. */
      is_ptrace_local = 1;
      if (ptrace(PTRACE_ATTACH, inferior, 0, 0) == -1) {
        mtcp_printf("ptrace_attach_threads: %d failed to attach to %d\n",
                    superior, inferior);
        perror("ptrace_attach_threads: PTRACE_ATTACH failed");
        mtcp_abort();
      }
      create_file (inferior);

      /* After attach, the superior needs to singlestep the inferior out of
       * stopthisthread, aka the signal handler. */
      while(1) {
        is_waitpid_local = 1;
        if (waitpid(inferior, &status, 0) == -1) {
          is_waitpid_local = 1;
          if (waitpid(inferior, &status, __WCLONE) == -1) {
            mtcp_printf("ptrace_attach_threads: %d failed waitpid on %d\n",
                        superior, inferior);
            perror("ptrace_attach_threads: waitpid failed\n");
            mtcp_abort();
          }
        }
        if (WIFEXITED(status)) {
          DPRINTF(("ptrace_attach_threads: %d is dead because %d\n",
                  inferior, WEXITSTATUS(status)));
        } else if(WIFSIGNALED(status)) {
          DPRINTF(("ptrace_attach_threads: %d is dead because of signal %d\n",
                  WTERMSIG(status)));
        }

        if (ptrace(PTRACE_GETREGS, inferior, 0, &regs) < 0) {
          mtcp_printf("ptrace_attach_threads: %d failed while calling "
                      "PTRACE_GETREGS for %d\n", superior, inferior);
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
        /* For 64 bit architectures. */
        if (low == 0xf && upp == 0x05 && regs.rax == 0xf) {
          if (isRestart) { /* Restart time. */
            if (last_command == PTRACE_SINGLESTEP_COMMAND) {
              if (regs.eax == DMTCP_SYS_sigreturn) addr = regs.esp;
              else {
                /* TODO: test, gdb very unclear. */
                addr = regs.esp + 8;
                addr = ptrace(PTRACE_PEEKDATA, inferior, addr, 0);
                addr += 20;
              }
              addr += EFLAGS_OFFSET;
              errno = 0;
              if ((eflags =
                   ptrace(PTRACE_PEEKDATA, inferior, (void *)addr, 0)) < 0) {
                if (errno != 0) {
                  mtcp_printf("ptrace_attach_threads: %d failed while calling "
                              "PTRACE_PEEKDATA for %d\n", superior, inferior);
                  perror("ptrace_attach_threads: PTRACE_PEEKDATA failed");
                  mtcp_abort ();
                }
              }
              eflags |= 0x0100;
              if (ptrace(PTRACE_POKEDATA, inferior, addr, eflags) < 0) {
                mtcp_printf("ptrace_attach_threads: %d failed while calling "
                            "PTRACE_POKEDATA for %d\n", superior, inferior);
                perror("ptrace_attach_threads: PTRACE_POKEDATA failed");
                mtcp_abort();
              }
            }
            else if (inferior_st != 'T' ) {
              /* TODO: remove in future as GROUP restore becames stable
               *                                                    - Artem */
              is_ptrace_local = 1;
              if (ptrace(PTRACE_CONT, inferior, 0, 0) < 0) {
                mtcp_printf("ptrace_attach_threads: %d failed while calling "
                            "PTRACE_CONT on %d\n", superior, inferior);
                perror("ptrace_attach_threads: PTRACE_CONT failed");
                mtcp_abort();
              }
            }
          } else { /* Resume time. */
            if (inferior_st != 'T') {
              ptrace_set_controlling_term(superior, inferior);
              is_ptrace_local = 1;
              if (ptrace(PTRACE_CONT, inferior, 0, 0) < 0) {
                mtcp_printf("ptrace_attach_threads: %d failed while calling "
                            "PTRACE_CONT on %d\n", superior, inferior);
                perror("ptrace_attach_threads: PTRACE_CONT failed");
                mtcp_abort();
              }
            }
          }

          /* In case we have checkpointed at a breakpoint, we don't want to
           * hit the same breakpoint twice. Thus this code. */
          if (inferior_st == 'T') {
            is_ptrace_local = 1;
            if (ptrace(PTRACE_SINGLESTEP, inferior, 0, 0) < 0) {
              mtcp_printf("ptrace_attach_threads: %d failed while calling "
                          "PTRACE_SINGLESTEP on %d\n", superior, inferior);
              perror("ptrace_attach_threads: PTRACE_SINGLESTEP failed");
              mtcp_abort();
            }
            is_waitpid_local = 1;
            if (waitpid(inferior, &status, 0 ) == -1) {
              is_waitpid_local = 1;
              if (waitpid(inferior, &status, __WCLONE) == -1) {
                mtcp_printf("ptrace_attach_threads: %d failed waitpid on %d\n",
                            superior, inferior);
                perror("ptrace_attach_threads: waitpid failed\n");
                mtcp_abort();
              }
            }
          }
          break;
        } //if (low == 0xf && upp == 0x05 && regs.rax == 0xf)
#else /* For 32 bit architectures.*/
        if (((low == 0xcd) && (upp == 0x80)) &&
            ((regs.eax == DMTCP_SYS_sigreturn) ||
             (regs.eax == DMTCP_SYS_rt_sigreturn))) {
          if (isRestart) { /* Restart time. */
            if (last_command == PTRACE_SINGLESTEP_COMMAND) {
              if (regs.eax == DMTCP_SYS_sigreturn) addr = regs.esp;
              else {
                /* TODO: test, gdb very unclear. */
                addr = regs.esp + 8;
                addr = ptrace(PTRACE_PEEKDATA, inferior, addr, 0);
                addr += 20;
              }
              addr += EFLAGS_OFFSET;
              errno = 0;
              if ((eflags =
                     ptrace(PTRACE_PEEKDATA, inferior, (void *)addr, 0)) < 0) {
                if (errno != 0) {
                  mtcp_printf("ptrace_attach_threads: %d failed while calling "
                              "PTRACE_PEEKDATA for %d\n", superior, inferior);
                  perror ("ptrace_attach_threads: PTRACE_PEEKDATA failed");
                  mtcp_abort ();
                }
              }
              eflags |= 0x0100;
              if (ptrace(PTRACE_POKEDATA, inferior, (void *)addr, eflags) < 0) {
                mtcp_printf("ptrace_attach_threads: %d failed while calling "
                            "PTRACE_POKEDATA for %d\n", superior, inferior);
                perror("ptrace_attach_threads: PTRACE_POKEDATA failed");
                mtcp_abort();
              }
            } else if (inferior_st != 'T') {
              ptrace_set_controlling_term(superior, inferior);
              is_ptrace_local = 1;
              if (ptrace(PTRACE_CONT, inferior, 0, 0) < 0) {
                mtcp_printf("ptrace_attach_threads: %d failed while calling "
                            "PTRACE_CONT on %d\n", superior, inferior);
                perror("ptrace_attach_threads: PTRACE_CONT failed");
                mtcp_abort();
              }
            }
          } else { /* Resume time. */
            if (inferior_st != 'T') {
              ptrace_set_controlling_term(superior, inferior);
              is_ptrace_local = 1;
              if (ptrace(PTRACE_CONT, inferior, 0, 0) < 0) {
                mtcp_printf("ptrace_attach_threads: %d failed while calling "
                            "PTRACE_CONT on %d\n", superior, inferior);
                perror("ptrace_attach_threads: PTRACE_CONT failed");
                mtcp_abort();
              }
            }
          }

          /* In case we have checkpointed at a breakpoint, we don't want to
           * hit the same breakpoint twice. Thus this code. */
          if (inferior_st == 'T') {
            is_ptrace_local = 1;
            if (ptrace(PTRACE_SINGLESTEP, inferior, 0, 0) < 0) {
              mtcp_printf("ptrace_attach_threads: %d failed while calling "
                          "PTRACE_SINGLESTEP on %d\n", superior, inferior);
              perror("ptrace_attach_threads: PTRACE_SINGLESTEP failed");
              mtcp_abort();
            }
            is_waitpid_local = 1;
            if (waitpid(inferior, &status, 0 ) == -1) {
              is_waitpid_local = 1;
              if (waitpid(inferior, &status, __WCLONE ) == -1) {
                mtcp_printf("ptrace_attach_threads: %d failed waitpid on %d\n",
                            superior, inferior);
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
          mtcp_printf("ptrace_attach_threads: %d failed while calling "
                      "PTRACE_SINGLESTEP on %d\n", superior, inferior);
          perror("ptrace_attach_threads: PTRACE_SINGLESTEP failed");
          mtcp_abort();
        }
      } //while(1)
    }
    else if (inferior == GETTID()) {
      create_file (superior);
      have_file (inferior);
    }
  }
  DPRINTF(("ptrace_attach_threads: %d done.\n", GETTID()));
}

/* This function detaches only the checkpoint threads.
 * The checkpoint threads need to be unattached so that they can process the
 * checkpoint message from the coordinator. */
void ptrace_detach_checkpoint_threads ()
{
  if (!callback_get_next_ptrace_info) return;

  int ret;
  struct ptrace_info pt_info;
  int index = 0;

  while (empty_ptrace_info(
           pt_info = (*callback_get_next_ptrace_info)(index++))) {
    if ((pt_info.superior == GETTID()) && pt_info.inferior_is_ckpthread) {
      DPRINTF(("ptrace_detach_checkpoint_threads: inferior = %d, "
               "superior = %d\n", pt_info.inferior, pt_info.superior));
      if ((ret = ptrace_detach_ckpthread(pt_info.inferior,
                                         pt_info.superior)) != 0) {
        if (ret == -ENOENT) {
          DPRINTF(("%s: process does not exist %d\n", __FUNCTION__,
                  pt_info.inferior));
        }
        mtcp_abort();
      }
    }
  }
  DPRINTF(("ptrace_detach_checkpoint_threads: done for %d\n", GETTID()));
}

/* This function detaches the user threads. */
void ptrace_detach_user_threads ()
{
  if (!callback_get_next_ptrace_info) return;

  int status = 0;
  struct ptrace_info pt_info;
  int index = 0;
  int tpid;
  char pstate;

  while (empty_ptrace_info(
           pt_info = (*callback_get_next_ptrace_info)(index++))) {
    if (pt_info.inferior_is_ckpthread) {
      DPRINTF(("ptrace_detach_user_threads: skip checkpoint thread %d\n",
              pt_info.inferior));
      continue;
    }

    if (pt_info.superior == GETTID()) {
      /* All UTs(user threads) must receive a MTCP_DEFAULT_SIGNAL from their
       * CT (checkpoint threads).
      DPRINTF(("start waiting on %d\n", pt_info.inferior));
      /* Was the status of this thread already read by the debugger? */
      pstate = procfs_state(pt_info.inferior);
      DPRINTF(("procfs_state(%d) = %c\n", pt_info.inferior, pstate));
      if (pstate == 0) {
        /* The thread does not exist. */
        mtcp_printf("%s: process not exist %d\n", __FUNCTION__,
                    pt_info.inferior);
        mtcp_abort();
      } else if (pstate == 'T') {
        /* This is a stopped process/thread.
         * It might happen that gdb or another process reads the status of this
         * thread before us. Consequently we will block. Thus we need to read
         * without hanging. */
        DPRINTF(("Thread %d is already stopped.\n", pt_info.inferior));
        is_waitpid_local = 1;
        tpid = waitpid (pt_info.inferior, &status, WNOHANG);
        if (tpid == -1 && errno == ECHILD) {
          DPRINTF(("Check cloned process\n"));
          // Try again with __WCLONE to check cloned processes.
          is_waitpid_local = 1;
          if ((tpid = waitpid(pt_info.inferior, &status,
                              __WCLONE | WNOHANG)) == -1) {
            DPRINTF(("ptrace_detach_user_threads: waitpid(..,__WCLONE), %s\n",
                    strerror(errno)));
          }
        }
        DPRINTF(("tgid = %d, tpid=%d,stopped=%d is_sigstop=%d,signal=%d\n",
                pt_info.inferior, tpid, WIFSTOPPED(status),
                WSTOPSIG(status) == SIGSTOP, WSTOPSIG(status)));
      } else {
        /* The thread is not in a stopped state.
         * The thread will be stopped by the CT of the process it belongs to,
         * by the delivery of MTCP_DEFAULT_SIGNAL.
         * It is safe to call blocking waitpid. */
        DPRINTF(("Thread %d is not stopped yet.\n", pt_info.inferior));
        is_waitpid_local = 1;
        tpid = waitpid(pt_info.inferior, &status, 0);
        if (tpid == -1 && errno == ECHILD) {
          DPRINTF(("Check cloned process\n"));
          is_waitpid_local = 1;
          if ((tpid = waitpid(pt_info.inferior, &status, __WCLONE)) == -1) {
            mtcp_printf("ptrace_detach_user_threads: waitpid(..,__WCLONE) %s\n",
                        strerror(errno));
          }
        }
        DPRINTF(("tgid = %d, tpid=%d,stopped=%d is_sigstop=%d,signal=%d\n",
                pt_info.inferior, tpid, WIFSTOPPED(status),
                WSTOPSIG(status) == SIGSTOP, WSTOPSIG(status)));
        if (WIFSTOPPED(status)) {
          if (WSTOPSIG(status) == MTCP_DEFAULT_SIGNAL)
            DPRINTF(("UT %d stopped by the delivery of MTCP_DEFAULT_SIGNAL\n",
                    pt_info.inferior));
          else /* We should never get here. */
            DPRINTF(("UT %d was stopped by the delivery of %d\n",
                    pt_info.inferior, WSTOPSIG(status)));
        } else  /* We should never end up here. */ 
          DPRINTF(("UT %d was NOT stopped by a signal\n", pt_info.inferior));
      }

      if (pt_info.last_command == PTRACE_SINGLESTEP_COMMAND &&
          pt_info.singlestep_waited_on == FALSE) {
        //is_waitpid_local = 1;
        has_status_and_pid = 1;
        saved_status = status;
        DPRINTF(("ptrace_detach_user_threads: AFTER WAITPID %d\n", status));
        pt_info.singlestep_waited_on = TRUE;
        pt_info.last_command = PTRACE_UNSPECIFIED_COMMAND;
      }

      DPRINTF(("GETTID = %d detaching superior = %d from inferior = %d\n",
               GETTID(), (int)pt_info.superior, (int)pt_info.inferior));
      have_file (pt_info.inferior);
      is_ptrace_local = 1;
      if (ptrace(PTRACE_DETACH, pt_info.inferior, 0,
                 MTCP_DEFAULT_SIGNAL) == -1) {
        DPRINTF(("ptrace_detach_user_threads: parent = %d child = %d\n",
                (int)pt_info.superior, (int)pt_info.inferior));
        DPRINTF(("ptrace_detach_user_threads: PTRACE_DETACH failed, error=%d",
                errno));
      }
    }
  }
  DPRINTF(("ptrace_detach_user_threads: %d done.\n", GETTID()));
}

void ptrace_lock_inferiors()
{
    char file[RECORDPATHLEN];
    snprintf(file,RECORDPATHLEN,"%s/dmtcp_ptrace_unlocked.%d",dir,GETTID());
    unlink(file);
}

void ptrace_unlock_inferiors()
{
    char file[RECORDPATHLEN];
    int fd;
    snprintf(file, RECORDPATHLEN, "%s/dmtcp_ptrace_unlocked.%d",dir,GETTID());
    fd = creat(file, 0644);
    if (fd < 0) {
        mtcp_printf("ptrace_unlock_inferiors: Error creating lock file: %s\n",
                    strerror(errno));
        mtcp_abort();
    }
    close(fd);
}

void create_file(pid_t pid)
{
  char str[RECORDPATHLEN];
  int fd;

  memset(str, 0, RECORDPATHLEN);
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

void have_file(pid_t pid)
{
  char str[RECORDPATHLEN];
  int fd;

  memset(str, 0, RECORDPATHLEN);
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
{   char file[RECORDPATHLEN];
    struct stat buf;
    snprintf(file,RECORDPATHLEN,"%s/dmtcp_ptrace_unlocked.%d",dir,pid);

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
/***********************************************************************
 * This is called by DMTCP.  BUT IT MUST THEN HAVE A PREFIX LIKE mtcp_
 * IN FRONT OF IT.  WE DON'T WANT TO POLLUTE THE USER'S NAMESPACE.
 * WE'RE A GUEST IN THE USER'S PROCESS.    - Gene
 ***********************************************************************/
void set_singlestep_waited_on ( pid_t superior, pid_t inferior, int value )
{
  mtcp_ptrace_info_list_update_info(superior, inferior, value);
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
      mtcp_printf("is_alive: Error closing file: %s\n", strerror(errno));
      mtcp_abort();
    } 
    return 1;
  }
  return 0;
}

/* TODO: give me a facelift. */
char procfs_state(int tid)
{
  char name[64];
  char sbuf[256], *S, *tmp;
  char state;
  int num_read, fd;

  sprintf(name,"/proc/%d/stat",tid);
  fd = open(name, O_RDONLY, 0);
  if( fd < 0 ){
    DPRINTF(("procfs_state: cannot open %s\n",name));
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

pid_t is_ckpt_in_ptrace_shared_file (pid_t ckpt) {
  int ptrace_fd = open(ptrace_shared_file, O_RDONLY);
  if (ptrace_fd == -1) return 0;

  pid_t superior, inferior;
  pid_t ckpt_ptraced_by = 0;
  while (readall(ptrace_fd, &superior, sizeof(pid_t)) > 0) {
    readall(ptrace_fd, &inferior, sizeof(pid_t));
    if (inferior == ckpt) {
      ckpt_ptraced_by = superior;
      break;
    }
  }
  if (close(ptrace_fd) != 0) {
    mtcp_printf("is_ckpt_in_ptrace_shared_file: error closing file, %s.\n",
                strerror(errno));
    mtcp_abort();
  }
  return ckpt_ptraced_by;
}

void read_ptrace_setoptions_file () {
  int fd = open(ptrace_setoptions_file, O_RDONLY);
  if (fd == -1) {
    DPRINTF(("read_ptrace_setoptions_file: NO setoptions file\n"));
    return;
  }

  pid_t superior, inferior;
  while (readall(fd, &superior, sizeof(pid_t)) > 0) {
    readall(fd, &inferior, sizeof(pid_t));
    if (inferior == GETTID()) {
      setoptions_superior = superior;
      is_ptrace_setoptions = TRUE;
    }
  }
  if (close(fd) != 0) {
    mtcp_printf("read_ptrace_setoptions_file: error while closing file, %s.\n",
                strerror(errno));
    mtcp_abort();
  }
}

void read_new_ptrace_shared_file () {
  int fd = open(new_ptrace_shared_file, O_RDONLY);
  if (fd == -1) {
    DPRINTF(("read_new_ptrace_shared_file: no file.\n"));
    return;
  }

  pid_t superior, inferior;
  char inferior_st;
  while (readall(fd, &superior, sizeof(pid_t)) > 0) {
    readall(fd, &inferior, sizeof(pid_t));
    readall(fd, &inferior_st, sizeof(char));
    mtcp_ptrace_info_list_insert(superior, inferior,
      PTRACE_UNSPECIFIED_COMMAND, FALSE, inferior_st, PTRACE_NO_FILE_OPTION);
  }
  if (close(fd) != 0) {
    mtcp_printf("read_new_ptrace_shared_file: error closing file, %s.\n",
                strerror(errno));
    mtcp_abort();
  }
}

void read_checkpoint_threads_file () {
  int fd = open(checkpoint_threads_file, O_RDONLY);
  if (fd == -1) {
    DPRINTF(("read_checkpoint_threads_file: no file.\n"));
    return;
  }

  pid_t pid, tid;
  while (readall(fd, &pid, sizeof(pid_t)) >  0) {
    readall(fd, &tid, sizeof(pid_t));
    if (is_alive(pid) && is_alive(tid))
      mtcp_ptrace_info_list_update_is_inferior_ckpthread(pid, tid);
  }
  if (close(fd) != 0) {
      mtcp_printf("read_checkpoint_threads_file: error closing file, %s.\n",
                  strerror(errno));
      mtcp_abort();
  }
}

/* In this function a given superior detaches from a given inferior, which is
 * a checkpoint thread. */
int ptrace_detach_ckpthread (pid_t inferior, pid_t superior)
{
  int status;
  char pstate;
  pid_t tpid;

  DPRINTF(("ptrace_detach_ckpthread: inferior = %d\n", inferior));
  pstate = procfs_state(inferior);
  DPRINTF(("ptrace_detach_ckpthread: ckpt_thread procfs_state(%d) = %c\n",
          inferior, pstate));

  if (pstate == 0) {
    /* This process does not exist. */
    return -ENOENT;
  } else if (pstate == 'T') {
    /* This is a stopped process/thread.
     * It might happen that gdb or another process reads the status of this
     * thread before us. Consequently we will block. Thus we need to read
     * without hanging. */
    DPRINTF(("ptrace_detach_ckpthread: ckpt_thread already stopped.\n"));

    is_waitpid_local = 1;
    tpid = waitpid(inferior, &status, WNOHANG);
    if (tpid == -1 && errno == ECHILD) {
      DPRINTF(("ptrace_detach_ckpthread: check for cloned process.\n"));
      is_waitpid_local = 1;
      if ((tpid = waitpid(inferior, &status, __WCLONE | WNOHANG)) == -1) {
        DPRINTF(("ptrace_detach_ckpthread: waitpid(..,__WCLONE): %s\n",
                strerror(errno)));
      }
    }
    DPRINTF(("ptrace_detach_ckpthread: tpid=%d, stopped=%d, "
             "is_sigstop=%d, signal=%d\n", tpid, WIFSTOPPED(status),
            WSTOPSIG(status) == SIGSTOP, WSTOPSIG(status)));
  } else {
    if (kill(inferior, SIGSTOP) == -1) {
      mtcp_printf("ptrace_detach_checkpoint_threads: sending SIGSTOP to %d, "
                  "error = %s\n", inferior, strerror(errno));
      return -EAGAIN;
    }
    is_waitpid_local = 1;
    tpid = waitpid(inferior, &status, 0);
    DPRINTF(("ptrace_detach_ckpthread: tpid = %d, errno = %d, ECHILD = %d\n",
            tpid, errno, ECHILD));
    if (tpid == -1 && errno == ECHILD) {
      DPRINTF(("ptrace_detach_ckpthread: check for cloned process.\n"));
      is_waitpid_local = 1;
      if ((tpid = waitpid(inferior, &status, __WCLONE)) == -1) {
        mtcp_printf("ptrace_detach_ckpthread: waitpid(..,__WCLONE): %s\n",
                    strerror(errno));
        return -EAGAIN;
      }
    }
  }
  DPRINTF(("ptrace_detach_ckpthread: tpid=%d, stopped=%d, "
           "is_sigstop=%d, signal=%d,err=%s\n",
           tpid, WIFSTOPPED(status), WSTOPSIG(status) == SIGSTOP,
           WSTOPSIG(status), strerror(errno)));
  if (WIFSTOPPED(status)) {
    if (WSTOPSIG(status) == SIGSTOP)
      DPRINTF(("ptrace_detach_ckpthread: ckpthread %d stopped by SIGSTOP\n",
              inferior));
    else  /* We should never get here. */ 
      DPRINTF(("ptrace_detach_ckpthread: ckpthread %d stopped by %d\n",
               inferior, WSTOPSIG(status)));
  } else /* We should never get here. */
    DPRINTF(("ptrace_detach_ckpthread: ckpthread %d NOT stopped by a signal\n",
              inferior));

  is_ptrace_local = 1;
  if (ptrace(PTRACE_DETACH, inferior, 0, SIGCONT) == -1) {
    DPRINTF(("ptrace_detach_ckpthread: parent = %d child = %d failed: %s\n",
             superior, inferior, strerror(errno)));
    return -EAGAIN;
  }
  DPRINTF(("ptrace_detach_ckpthread: inferior =%d\n", inferior));

  only_once = 0; /* No need for semaphore here - the UT execute this code. */
  return 0;
}

void init_empty_cmd_info(struct cmd_info *cmd) {
  cmd->option = 0;
  cmd->superior = 0;
  cmd->inferior = 0;
  cmd->last_command = 0;
  cmd->singlestep_waited_on = 0;
  cmd->file_option = 0;
}

void mtcp_ptrace_info_list_update_is_inferior_ckpthread(pid_t pid, pid_t tid) {
  if (!callback_ptrace_info_list_command) return;

  struct cmd_info cmd;
  init_empty_cmd_info(&cmd);
  cmd.option = PTRACE_INFO_LIST_UPDATE_IS_INFERIOR_CKPTHREAD;
  cmd.superior = pid;
  cmd.inferior = tid;
  (*callback_ptrace_info_list_command)(cmd);
}

void mtcp_ptrace_info_list_sort() {
  if (!callback_ptrace_info_list_command) return;

  struct cmd_info cmd;
  init_empty_cmd_info(&cmd);
  cmd.option = PTRACE_INFO_LIST_SORT;
  (*callback_ptrace_info_list_command)(cmd);
}

void mtcp_ptrace_info_list_remove_pairs_with_dead_tids() {
  if (!callback_ptrace_info_list_command) return;

  struct cmd_info cmd;
  init_empty_cmd_info(&cmd);
  cmd.option = PTRACE_INFO_LIST_REMOVE_PAIRS_WITH_DEAD_TIDS;
  (*callback_ptrace_info_list_command)(cmd);
}

void mtcp_ptrace_info_list_save_threads_state() {
  if (!callback_ptrace_info_list_command) return;

  struct cmd_info cmd;
  init_empty_cmd_info(&cmd);
  cmd.option = PTRACE_INFO_LIST_SAVE_THREADS_STATE;
  (*callback_ptrace_info_list_command)(cmd);
}

void mtcp_ptrace_info_list_print() {
  if (!callback_ptrace_info_list_command) return;

  struct cmd_info cmd;
  init_empty_cmd_info(&cmd);
  cmd.option = PTRACE_INFO_LIST_PRINT;
  (*callback_ptrace_info_list_command)(cmd);
}

void mtcp_ptrace_info_list_insert(pid_t superior, pid_t inferior,
  int last_command, int singlestep_waited_on, char inf_st, int file_option) {
  if (!callback_ptrace_info_list_command) return;

  struct cmd_info cmd;
  init_empty_cmd_info(&cmd);
  cmd.option = PTRACE_INFO_LIST_INSERT;
  cmd.superior = superior;
  cmd.inferior = inferior;
  cmd.last_command = last_command;
  cmd.singlestep_waited_on = singlestep_waited_on;
  cmd.inferior_st = inf_st;
  cmd.file_option = file_option;
  (*callback_ptrace_info_list_command)(cmd);
}

void mtcp_ptrace_info_list_update_info(pid_t superior, pid_t inferior,
  int singlestep_waited_on) {
  if (!callback_ptrace_info_list_command) return;

  struct cmd_info cmd;
  init_empty_cmd_info(&cmd);
  cmd.option = PTRACE_INFO_LIST_UPDATE_INFO;
  cmd.superior = superior;
  cmd.inferior = inferior;
  cmd.singlestep_waited_on = singlestep_waited_on;
  (*callback_ptrace_info_list_command)(cmd);
}

char retrieve_inferior_state(pid_t tid) {
  if (!callback_ptrace_info_list_command) return;

  int index = 0;
  struct ptrace_info pt_info;
  while (empty_ptrace_info(
           pt_info = (*callback_get_next_ptrace_info)(index++))) {
    if (pt_info.inferior == tid) return procfs_state(pt_info.inferior);
  }
  return 'u';
}

#endif
