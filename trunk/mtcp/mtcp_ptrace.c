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

const unsigned char DMTCP_SYS_sigreturn =  0x77;
const unsigned char DMTCP_SYS_rt_sigreturn = 0xad;

static const unsigned char linux_syscall[] = { 0xcd, 0x80 };

__thread struct ptrace_waitpid_info __ptrace_waitpid;

__thread pid_t setoptions_superior = -1;
__thread int is_ptrace_setoptions = FALSE;

sem_t __does_inferior_exist_sem;
int __init_does_inferior_exist_sem = 0;
int __check_once_does_inferior_exist = 0;

char dmtcp_tmp_dir[PATH_MAX];
char new_ptrace_shared_file[PATH_MAX];
char ptrace_shared_file[PATH_MAX];
char ptrace_setoptions_file[PATH_MAX];
char checkpoint_threads_file[PATH_MAX];
char ckpt_leader_file[PATH_MAX];

void mtcp_ptrace_info_list_update_info(int singlestep_waited_on);


// Return Value
// 0 : succeeded
// -1: failed
int mtcp_get_controlling_term(char* ttyname, size_t len)
{
  char sbuf[1024];
  char *tmp;
  char *s;
  char state;
  int ppid, pgrp, session, tty, tpgid;

  int fd, num_read;

  if (len < strlen("/dev/pts/123456780"))
    return -1;
  ttyname[0] = '\0';

  fd = mtcp_sys_open("/proc/self/stat", O_RDONLY, 0);
  if (fd == -1) return -1;

  num_read = mtcp_sys_read(fd, sbuf, sizeof sbuf - 1);
  close(fd);
  if(num_read<=0) return -1;
  sbuf[num_read] = '\0';

  s = strchr(sbuf, '(') + 1;
  tmp = strrchr(s, ')');
  s = tmp + 2;                 // skip ") "

  sscanf(s,
      "%c "
      "%d %d %d %d %d ",
      &state,
      &ppid, &pgrp, &session, &tty, &tpgid
      );

  int maj =  ((unsigned)(tty)>>8u) & 0xfffu;
  int min =  ((unsigned)(tty)&0xffu) | (((unsigned)(tty)&0xfff00000u)>>12u);

  /* /dev/pts/ * has major numbers in the range 136 - 143 */
  if ( maj >= 136 && maj <= 143)
    sprintf(ttyname, "/dev/pts/%d", min+(maj-136)*256);

  return 0;
}

/* We're ptracing when the size of ptrace_info_list is greater than zero or
 * we have a file with the ptrace_info pairs. */
int ptracing() {
  if (!callback_ptrace_info_list_size) return 0;
  struct stat buf;
  return ((*callback_ptrace_info_list_size)() > 0) ||
         (stat(ptrace_shared_file, &buf) == 0);
}

static pid_t mtcp_waitpid(pid_t pid, int *status, int options) {
  int rc;
  __ptrace_waitpid.is_waitpid_local = 1;
  rc = waitpid(pid, status, options);
  __ptrace_waitpid.is_waitpid_local = 0;
  if (__ptrace_waitpid.has_status_and_pid) {
    __ptrace_waitpid.saved_pid = -1;
    __ptrace_waitpid.saved_status = -1;
    __ptrace_waitpid.has_status_and_pid = 0;
  }
  return rc;
}

static long mtcp_ptrace(enum __ptrace_request request, pid_t pid, void *addr,
                         void *data) {
  long rc;
  __ptrace_waitpid.is_ptrace_local = 1;
  rc = ptrace(request, pid, addr, data);
  __ptrace_waitpid.is_ptrace_local = 0;
  return rc;
}

int empty_ptrace_info(struct ptrace_info pt_info) {
  return pt_info.superior && pt_info.inferior;
}

void init_thread_local()
{
  __ptrace_waitpid.is_waitpid_local = 0;    // no crash on pre-access
  __ptrace_waitpid.is_ptrace_local = 0;     // no crash on pre-access
  __ptrace_waitpid.saved_pid = -1;          // no crash on pre-access
  __ptrace_waitpid.saved_status = -1;       // no crash on pre-access
  __ptrace_waitpid.has_status_and_pid = 0;  // crash

  setoptions_superior = -1;
  is_ptrace_setoptions = FALSE;
}

ssize_t read_no_error(int fd, void *buf, size_t count)
{
  int rc;
  do {
    rc = read(fd, buf, count);
  } while (rc == -1 && (errno == EAGAIN  || errno == EINTR));
  if (rc == -1) { /* if not harmless error */
    MTCP_PRINTF("Internal error\n");
    mtcp_abort();
  }
  return rc; /* else rc >= 0; success */
}

void ptrace_set_controlling_term(pid_t superior, pid_t inferior)
{
  return;
  if (getsid(inferior) == getsid(superior)) {
    char tty_name[80];
    if (mtcp_get_controlling_term(tty_name, 80) == -1) {
      MTCP_PRINTF("unable to find controlling term\n");
      mtcp_abort();
    }

    int fd = open(tty_name, O_RDONLY);
    if (fd < 0) {
      MTCP_PRINTF("error %s opening controlling term %s\n",
                  strerror(errno), tty_name);
      mtcp_abort();
    }

    if (tcsetpgrp(fd, inferior) == -1) {
      MTCP_PRINTF("tcsetpgrp failed, tty:%s %s\n", tty_name, strerror(errno));
      mtcp_abort();
    }
    close(fd);
  }
}

void ptrace_attach_threads(int isRestart)
{
  if (!ptracing()) return;
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

  DPRINTF("%d started.\n", GETTID());

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
      /* We must make sure the inferior process was created and is inside
       * ptrace_attach_threads. */
      sem_wait(&__does_inferior_exist_sem);
      if (__check_once_does_inferior_exist == 0) {
        have_file (superior); /* Inferior is inside ptrace_attach_threads. */
        __check_once_does_inferior_exist = 1;
      }
      sem_post(&__does_inferior_exist_sem);

      /* If the inferior is the checkpoint thread, attach. */
      if (inferior_is_ckpthread) {
        have_file (inferior);
        if (mtcp_ptrace(PTRACE_ATTACH, inferior, 0, 0) == -1) {
          perror("ptrace_attach_threads: PTRACE_ATTACH for ckpthread failed.");
          mtcp_abort();
        }
        if (mtcp_waitpid(inferior, &status, __WCLONE) == -1) {
          perror("ptrace_attach_threads: waitpid for ckpt failed\n");
          mtcp_abort();
        }
        if (WIFEXITED(status)) {
          MTCP_PRINTF("ckpthread is dead because %d\n", WEXITSTATUS(status));
        } else if(WIFSIGNALED(status)) {
          MTCP_PRINTF("ckpthread is dead because of signal %d\n",
                      WTERMSIG(status));
        }
        if (inferior_st != 'T') {
          if (mtcp_ptrace(PTRACE_CONT, inferior, 0, 0) < 0) {
            perror("ptrace_attach_threads: PTRACE_CONT failed");
            mtcp_abort();
          }
        }
        continue;
      }

      /* Attach to the user threads. */
      if (mtcp_ptrace(PTRACE_ATTACH, inferior, 0, 0) == -1) {
        MTCP_PRINTF("%d failed to attach to %d\n", superior, inferior);
        perror("ptrace_attach_threads: PTRACE_ATTACH failed");
        mtcp_abort();
      }
      create_file (inferior);

      /* After attach, the superior needs to singlestep the inferior out of
       * stopthisthread, aka the signal handler. */
      while(1) {
        if (mtcp_waitpid(inferior, &status, 0) == -1) {
          if (mtcp_waitpid(inferior, &status, __WCLONE) == -1) {
            MTCP_PRINTF("%d failed waitpid on %d\n", superior, inferior);
            perror("ptrace_attach_threads: waitpid failed\n");
            mtcp_abort();
          }
        }
        if (WIFEXITED(status)) {
          DPRINTF("%d is dead because %d\n", inferior, WEXITSTATUS(status));
        } else if(WIFSIGNALED(status)) {
          DPRINTF("%d is dead because of signal %d\n", WTERMSIG(status));
        }

        if (mtcp_ptrace(PTRACE_GETREGS, inferior, 0, &regs) < 0) {
          MTCP_PRINTF("%d failed while calling PTRACE_GETREGS for %d\n",
                      superior, inferior);
          perror("ptrace_attach_threads: PTRACE_GETREGS failed");
          mtcp_abort();
        }
#ifdef __x86_64__
        peekdata = mtcp_ptrace(PTRACE_PEEKDATA, inferior, (void*) regs.rip, 0);
#else
        peekdata = mtcp_ptrace(PTRACE_PEEKDATA, inferior, (void*) regs.eip, 0);
#endif
        low = peekdata & 0xff;
        peekdata >>=8;
        upp = peekdata & 0xff;
#ifdef __x86_64__
        /* For 64 bit architectures. */
        if (low == 0xf && upp == 0x05 && regs.rax == 0xf) {
          if (isRestart) { /* Restart time. */
            if (last_command == PTRACE_SINGLESTEP_COMMAND) {
              if (regs.eax != DMTCP_SYS_rt_sigreturn) addr = regs.esp;
              else {
                addr = regs.esp + 8;
                addr = mtcp_ptrace(PTRACE_PEEKDATA, inferior, (void*) addr, 0);
                addr += 20;
              }
              addr += EFLAGS_OFFSET;
              errno = 0;
              if ((eflags = mtcp_ptrace(PTRACE_PEEKDATA, inferior,
                                        (void *)addr, 0)) < 0) {
                if (errno != 0) {
                  MTCP_PRINTF("%d failed while calling "
                              "PTRACE_PEEKDATA for %d\n", superior, inferior);
                  perror("ptrace_attach_threads: PTRACE_PEEKDATA failed");
                  mtcp_abort ();
                }
              }
              eflags |= 0x0100;
              if (mtcp_ptrace(PTRACE_POKEDATA, inferior, (void *)addr,
                              (void*) eflags) < 0) {
                MTCP_PRINTF("%d failed while calling PTRACE_POKEDATA for %d\n",
                            superior, inferior);
                perror("ptrace_attach_threads: PTRACE_POKEDATA failed");
                mtcp_abort();
              }
            }
            else if (inferior_st != 'T' ) {
              /* TODO: remove in future as GROUP restore becames stable
               *                                                    - Artem */
              if (mtcp_ptrace(PTRACE_CONT, inferior, 0, 0) < 0) {
                MTCP_PRINTF("%d failed while calling PTRACE_CONT on %d\n",
                            superior, inferior);
                perror("ptrace_attach_threads: PTRACE_CONT failed");
                mtcp_abort();
              }
            }
          } else { /* Resume time. */
            if (inferior_st != 'T') {
              ptrace_set_controlling_term(superior, inferior);
              if (mtcp_ptrace(PTRACE_CONT, inferior, 0, 0) < 0) {
                MTCP_PRINTF("%d failed while calling PTRACE_CONT on %d\n",
                            superior, inferior);
                perror("ptrace_attach_threads: PTRACE_CONT failed");
                mtcp_abort();
              }
            }
          }

          /* In case we have checkpointed at a breakpoint, we don't want to
           * hit the same breakpoint twice. Thus this code. */
          if (inferior_st == 'T') {
            if (mtcp_ptrace(PTRACE_SINGLESTEP, inferior, 0, 0) < 0) {
              MTCP_PRINTF("%d failed while calling PTRACE_SINGLESTEP on %d\n",
                          superior, inferior);
              perror("ptrace_attach_threads: PTRACE_SINGLESTEP failed");
              mtcp_abort();
            }
            if (mtcp_waitpid(inferior, &status, 0 ) == -1) {
              if (mtcp_waitpid(inferior, &status, __WCLONE) == -1) {
                MTCP_PRINTF("%d failed waitpid on %d\n", superior, inferior);
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
              if (regs.eax != DMTCP_SYS_rt_sigreturn) addr = regs.esp;
              else {
                addr = regs.esp + 8;
                addr = mtcp_ptrace(PTRACE_PEEKDATA, inferior, (void*) addr, 0);
                addr += 20;
              }
              addr += EFLAGS_OFFSET;
              errno = 0;
              if ((eflags = mtcp_ptrace(PTRACE_PEEKDATA, inferior,
                                        (void *)addr, 0)) < 0) {
                if (errno != 0) {
                  MTCP_PRINTF("%d failed while calling "
                              "PTRACE_PEEKDATA for %d\n", superior, inferior);
                  perror ("ptrace_attach_threads: PTRACE_PEEKDATA failed");
                  mtcp_abort ();
                }
              }
              eflags |= 0x0100;
              if (mtcp_ptrace(PTRACE_POKEDATA, inferior, (void *)addr, eflags)
			      < 0) {
                MTCP_PRINTF("%d failed while calling PTRACE_POKEDATA for %d\n",
                            superior, inferior);
                perror("ptrace_attach_threads: PTRACE_POKEDATA failed");
                mtcp_abort();
              }
            } else if (inferior_st != 'T') {
              ptrace_set_controlling_term(superior, inferior);
              if (mtcp_ptrace(PTRACE_CONT, inferior, 0, 0) < 0) {
                MTCP_PRINTF("%d failed while calling PTRACE_CONT on %d\n",
                            superior, inferior);
                perror("ptrace_attach_threads: PTRACE_CONT failed");
                mtcp_abort();
              }
            }
          } else { /* Resume time. */
            if (inferior_st != 'T') {
              ptrace_set_controlling_term(superior, inferior);
              if (mtcp_ptrace(PTRACE_CONT, inferior, 0, 0) < 0) {
                MTCP_PRINTF("%d failed while calling PTRACE_CONT on %d\n",
                            superior, inferior);
                perror("ptrace_attach_threads: PTRACE_CONT failed");
                mtcp_abort();
              }
            }
          }

          /* In case we have checkpointed at a breakpoint, we don't want to
           * hit the same breakpoint twice. Thus this code. */
          if (inferior_st == 'T') {
            if (mtcp_ptrace(PTRACE_SINGLESTEP, inferior, 0, 0) < 0) {
              MTCP_PRINTF("%d failed while calling PTRACE_SINGLESTEP on %d\n",
                          superior, inferior);
              perror("ptrace_attach_threads: PTRACE_SINGLESTEP failed");
              mtcp_abort();
            }
            if (mtcp_waitpid(inferior, &status, 0) == -1) {
              if (mtcp_waitpid(inferior, &status, __WCLONE) == -1) {
                MTCP_PRINTF("%d failed waitpid on %d\n", superior, inferior);
                perror("ptrace_attach_threads: waitpid failed\n");
                mtcp_abort();
              }
            }
          }
          break;
        }
        #endif
        if (mtcp_ptrace(PTRACE_SINGLESTEP, inferior, 0, 0) < 0) {
          MTCP_PRINTF("%d failed while calling PTRACE_SINGLESTEP on %d\n",
                      superior, inferior);
          perror("ptrace_attach_threads: PTRACE_SINGLESTEP failed");
          mtcp_abort();
        }
      } //while(1)
    }
    else if (inferior == GETTID()) {
      /* Inferior is in ptrace_attach_threads. */
      create_file (superior);
      /* Wait for the superior to attach. */
      have_file (inferior);
    }
  }
  DPRINTF("%d done.\n", GETTID());
}

/* Detach only the checkpoint threads.
 * The checkpoint threads need to be detached before user threads, so that they
 * can process the checkpoint message from the coordinator. */
void ptrace_detach_checkpoint_threads ()
{
  if (!ptracing()) return;
  if (!callback_get_next_ptrace_info) return;

  int ret;
  struct ptrace_info pt_info;
  int index = 0;

  while (empty_ptrace_info(
           pt_info = (*callback_get_next_ptrace_info)(index++))) {
    if ((pt_info.superior == GETTID()) && pt_info.inferior_is_ckpthread) {
      if ((ret = ptrace_detach_ckpthread(pt_info.inferior,
                                         pt_info.superior)) != 0) {
        if (ret == -ENOENT)
          MTCP_PRINTF("process does not exist %d\n", pt_info.inferior);
        mtcp_abort();
      }
    }
  }
  DPRINTF("done for %d\n", GETTID());
}

/* This function detaches the user threads. */
void ptrace_detach_user_threads ()
{
  if (!ptracing()) return;
  if (!callback_get_next_ptrace_info) return;

  int status = 0;
  int index = 0;
  int tpid;
  char pstate;
  struct ptrace_info pt_info;

  while (empty_ptrace_info(
           pt_info = (*callback_get_next_ptrace_info)(index++))) {
    if (pt_info.inferior_is_ckpthread) continue;

    if (pt_info.superior == GETTID()) {
      /* All UTs must receive a MTCP_DEFAULT_SIGNAL from their CT.
       * Was the status of this thread already read by the debugger? */
      pstate = procfs_state(pt_info.inferior);
      if (pstate == 0) { /* The thread does not exist. */
        MTCP_PRINTF("process not exist %d\n", pt_info.inferior);
        mtcp_abort();
      } else if (pstate == 'T') {
        /* This is a stopped thread. It is possible for gdb or another thread
         * to read the status of this thread before us. Consequently we will
         * block. Thus we need to read without hanging. */
        tpid = mtcp_waitpid(pt_info.inferior, &status, WNOHANG);
        if (tpid == -1 && errno == ECHILD) {
          if ((tpid = mtcp_waitpid(pt_info.inferior, &status,
                                    __WCLONE | WNOHANG)) == -1) {
            MTCP_PRINTF("waitpid(..,__WCLONE) %s\n", strerror(errno));
          }
        }
      } else {
        /* The thread is not in a stopped state. The thread will be stopped by
         * the CT of the process it belongs to, by the delivery of
         * MTCP_DEFAULT_SIGNAL. It is safe to call blocking waitpid. */
        tpid = mtcp_waitpid(pt_info.inferior, &status, 0);
        if (tpid == -1 && errno == ECHILD) {
          if ((tpid = mtcp_waitpid(pt_info.inferior, &status,
                                    __WCLONE)) == -1) {
            MTCP_PRINTF("waitpid(..,__WCLONE) %s\n", strerror(errno));
          }
        }
        if (WIFSTOPPED(status)) {
          if (WSTOPSIG(status) == MTCP_DEFAULT_SIGNAL)
            DPRINTF("UT %d stopped by the delivery of MTCP_DEFAULT_SIGNAL\n",
                    pt_info.inferior);
          else /* We should never get here. */
            DPRINTF("UT %d was stopped by the delivery of %d\n",
                    pt_info.inferior, WSTOPSIG(status));
        } else  /* We should never end up here. */
          DPRINTF("UT %d was NOT stopped by a signal\n", pt_info.inferior);
      }

      if (pt_info.last_command == PTRACE_SINGLESTEP_COMMAND &&
          pt_info.singlestep_waited_on == FALSE) {
        __ptrace_waitpid.has_status_and_pid = 1;
        __ptrace_waitpid.saved_status = status;
        mtcp_ptrace_info_list_update_info(TRUE);
      }

      have_file (pt_info.inferior);
      if (mtcp_ptrace(PTRACE_DETACH, pt_info.inferior, 0,
                 (void*) MTCP_DEFAULT_SIGNAL) == -1) {
        MTCP_PRINTF("parent = %d child = %d PTRACE_DETACH failed, error = %d\n",
                    (int)pt_info.superior, (int)pt_info.inferior, errno);
      }
    }
  }
  DPRINTF("%d done.\n", GETTID());
}

void ptrace_lock_inferiors()
{
    if (!ptracing()) return;

    char file[RECORDPATHLEN];
    snprintf(file, RECORDPATHLEN, "%s/dmtcp_ptrace_unlocked.%d",
             dmtcp_tmp_dir, GETTID());
    unlink(file);
}

void ptrace_unlock_inferiors()
{
    if (!ptracing()) return;

    char file[RECORDPATHLEN];
    int fd;
    snprintf(file, RECORDPATHLEN, "%s/dmtcp_ptrace_unlocked.%d", dmtcp_tmp_dir,
             GETTID());
    fd = creat(file, 0644);
    if (fd < 0) {
        MTCP_PRINTF("Error creating lock file: %s\n", strerror(errno));
        mtcp_abort();
    }
    close(fd);
}

void create_file(pid_t pid)
{
  char str[RECORDPATHLEN];
  memset(str, 0, RECORDPATHLEN);
  sprintf(str, "%s/%d", dmtcp_tmp_dir, pid);

  int fd = open(str, O_CREAT|O_APPEND|O_WRONLY, 0644);
  if (fd == -1) {
    MTCP_PRINTF("Error opening file %s\n: %s\n", str, strerror(errno));
    mtcp_abort();
  }
  if (close(fd) != 0) {
    MTCP_PRINTF("Error closing file\n: %s\n", strerror(errno));
    mtcp_abort();
  }
}

void wait_for_file(char *file)
{
  struct stat buf;
  while (stat(file, &buf)) {
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 100000000;
    if (errno != ENOENT) {
      MTCP_PRINTF("Unexpected error in stat: %d\n", errno);
      mtcp_abort();
    }
    nanosleep(&ts,NULL);
  }
}

void have_file(pid_t pid)
{
  char file[RECORDPATHLEN];
  snprintf(file, RECORDPATHLEN, "%s/%d", dmtcp_tmp_dir, pid);

  wait_for_file(file);
  if (unlink(file) == -1) {
    MTCP_PRINTF("unlink failed: %s\n", strerror(errno));
    mtcp_abort();
  }
}

void ptrace_wait4(pid_t pid)
{
  char file[RECORDPATHLEN];
  snprintf(file, RECORDPATHLEN, "%s/dmtcp_ptrace_unlocked.%d",
           dmtcp_tmp_dir, pid);
  wait_for_file(file);
}

struct ptrace_waitpid_info get_ptrace_waitpid_info ()
{
  return __ptrace_waitpid;
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
      MTCP_PRINTF("Error closing file: %s\n", strerror(errno));
      mtcp_abort();
    }
    return 1;
  }
  return 0;
}

char procfs_state(int tid) {
  char name[64];
  sprintf(name, "/proc/%d/stat", tid);
  int fd = open(name, O_RDONLY, 0);
  if (fd < 0) {
    DPRINTF("cannot open %s\n",name);
    return 0;
  }

  /* The format of /proc/pid/stat is: tid (name_of_executable) state.
   * We need to retrieve the state of tid. 255 is enough in this case. */
  char sbuf[256];
  size_t num_read = 0;
  ssize_t rc;
  /* Read at most 255 characters or until the end of the file. */
  while (num_read != 255) {
    rc = read(fd, sbuf + num_read, 255 - num_read);
    if (rc < 0) break;
    else if (rc == 0) break;
    num_read += rc;
  }
  if (close(fd) != 0) {
    MTCP_PRINTF("error closing file, %s\n.", strerror(errno));
    mtcp_abort();
  }
  if (num_read <= 0) return 0;

  sbuf[num_read] = '\0';
  char *state, *tmp;
  state = strchr(sbuf, '(') + 1;
  if (!state) return 'u';
  tmp = strrchr(state, ')');
  if (!tmp || (tmp + 2 - sbuf) > 255) return 'u';
  state = tmp + 2;

  return state[0];
}

pid_t is_ckpt_in_ptrace_shared_file (pid_t ckpt) {
  int ptrace_fd = open(ptrace_shared_file, O_RDONLY);
  if (ptrace_fd == -1) return 0;

  pid_t superior, inferior;
  pid_t ckpt_ptraced_by = 0;
  while (read_no_error(ptrace_fd, &superior, sizeof(pid_t)) > 0) {
    read_no_error(ptrace_fd, &inferior, sizeof(pid_t));
    if (inferior == ckpt) {
      ckpt_ptraced_by = superior;
      break;
    }
  }
  if (close(ptrace_fd) != 0) {
    MTCP_PRINTF("error closing file, %s.\n", strerror(errno));
    mtcp_abort();
  }
  return ckpt_ptraced_by;
}

/* rc is the tid returned by __clone. We record to file only when
 * record_to_file is true. In this case, the tid of the superior is
 * determined from ptrace_setoptions_file. */
void read_ptrace_setoptions_file (int record_to_file, int rc) {
  int fd = open(ptrace_setoptions_file, O_RDONLY);
  if (fd == -1) {
    DPRINTF("NO setoptions file\n");
    return;
  }

  pid_t superior, inferior;
  while (read_no_error(fd, &superior, sizeof(pid_t)) > 0) {
    read_no_error(fd, &inferior, sizeof(pid_t));
    if (inferior == GETTID()) {
      setoptions_superior = superior;
      is_ptrace_setoptions = TRUE;
      if (record_to_file) {
        mtcp_ptrace_info_list_insert(setoptions_superior, rc,
                                     PTRACE_UNSPECIFIED_COMMAND, FALSE, 'u',
                                     PTRACE_SHARED_FILE_OPTION);
      }
    }
  }
  if (close(fd) != 0) {
    MTCP_PRINTF("error while closing file, %s.\n", strerror(errno));
    mtcp_abort();
  }
}

void read_new_ptrace_shared_file () {
  int fd = open(new_ptrace_shared_file, O_RDONLY);
  if (fd == -1) {
    DPRINTF("no file.\n");
    return;
  }

  pid_t superior, inferior;
  char inferior_st;
  while (read_no_error(fd, &superior, sizeof(pid_t)) > 0) {
    read_no_error(fd, &inferior, sizeof(pid_t));
    read_no_error(fd, &inferior_st, sizeof(char));
    mtcp_ptrace_info_list_insert(superior, inferior,
      PTRACE_UNSPECIFIED_COMMAND, FALSE, inferior_st, PTRACE_NO_FILE_OPTION);
  }
  if (close(fd) != 0) {
    MTCP_PRINTF("error closing file, %s.\n", strerror(errno));
    mtcp_abort();
  }
}

void read_checkpoint_threads_file () {
  int fd = open(checkpoint_threads_file, O_RDONLY);
  if (fd == -1) {
    DPRINTF("no file.\n");
    return;
  }

  pid_t pid, tid;
  while (read_no_error(fd, &pid, sizeof(pid_t)) >  0) {
    read_no_error(fd, &tid, sizeof(pid_t));
    if (is_alive(pid) && is_alive(tid))
      mtcp_ptrace_info_list_update_is_inferior_ckpthread(pid, tid);
  }
  if (close(fd) != 0) {
      MTCP_PRINTF("error closing file, %s.\n", strerror(errno));
      mtcp_abort();
  }
}

/* Superior detaches from an inferior, which is a ckpt thread. */
int ptrace_detach_ckpthread (pid_t inferior, pid_t superior)
{
  int status;
  pid_t tpid;

  char pstate = procfs_state(inferior);
  if (pstate == 0) { /* This process does not exist. */
    return -ENOENT;
  } else if (pstate == 'T') {
    /* This is a stopped thread. It might happen that gdb or another thread
     * reads the status of this thread before us. Consequently we will block.
     * Thus we need to read without hanging. */
    tpid = mtcp_waitpid(inferior, &status, WNOHANG);
    if (tpid == -1 && errno == ECHILD) {
      if ((tpid = mtcp_waitpid(inferior, &status, __WCLONE | WNOHANG)) == -1) {
        MTCP_PRINTF("waitpid(..,__WCLONE): %s\n", strerror(errno));
      }
    }
  } else {
    if (kill(inferior, SIGSTOP) == -1) {
      MTCP_PRINTF("sending SIGSTOP to %d, error = %s\n",
                  inferior, strerror(errno));
      return -EAGAIN;
    }
    tpid = mtcp_waitpid(inferior, &status, 0);
    if (tpid == -1 && errno == ECHILD) {
      if ((tpid = mtcp_waitpid(inferior, &status, __WCLONE)) == -1) {
        MTCP_PRINTF("waitpid(..,__WCLONE), errno %s\n", strerror(errno));
        return -EAGAIN;
      }
    }
  }
  if (WIFSTOPPED(status)) {
    if (WSTOPSIG(status) == SIGSTOP)
      DPRINTF("ckpthread %d stopped by SIGSTOP\n", inferior);
    else  /* We should never get here. */
      DPRINTF("ckpthread %d stopped by %d\n", inferior, WSTOPSIG(status));
  } else /* We should never get here. */
    DPRINTF("ckpthread %d NOT stopped by a signal\n", inferior);

  if (mtcp_ptrace(PTRACE_DETACH, inferior, 0, (void*) SIGCONT) == -1) {
    MTCP_PRINTF("parent = %d child = %d failed: %s\n",
                superior, inferior, strerror(errno));
    return -EAGAIN;
  }
  /* No need for semaphore here - the UT execute this code. */
  __check_once_does_inferior_exist = 0;

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

void mtcp_ptrace_info_list_update_info(int singlestep_waited_on) {
  if (!callback_ptrace_info_list_command) return;

  struct cmd_info cmd;
  init_empty_cmd_info(&cmd);
  cmd.option = PTRACE_INFO_LIST_UPDATE_INFO;
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

/* The checkpoint leader can only be the checkpoint thread of the superior. If
 * we are to select as checkpoint leader an inferior, then we would have a race
 * condition: the superior would wait for new_ptrace_shared_file to be written,
 * then detach from the inferior, which then writes new_ptrace_shared_file.
 * The detach has to happen after new_ptrace_shared_file is written.  Return 0,
 * if the checkpoint thread can't be a possible ckpt_leader.  1, otherwise. */
int possible_ckpt_leader(pid_t tid) {
  int ptrace_fd = open(ptrace_shared_file, O_RDONLY);
  if (ptrace_fd != -1) {
    pid_t superior, inferior;
    while (read_no_error(ptrace_fd, &superior, sizeof(pid_t)) > 0) {
      read_no_error(ptrace_fd, &inferior, sizeof(pid_t));
      if (inferior == tid)  {
        if (close(ptrace_fd) != 0) {
          MTCP_PRINTF("error closing the file. %s\n", strerror(errno));
          mtcp_abort();
        }
        return 0;
      }
    }
    if (close(ptrace_fd) != 0) {
      MTCP_PRINTF("error closing the file. %s\n", strerror(errno));
      mtcp_abort();
    }
  }

  if (!callback_get_next_ptrace_info) return 0;

  int index = 0;
  struct ptrace_info pt_info;
  while (empty_ptrace_info(
           pt_info = (*callback_get_next_ptrace_info)(index++))) {
    if (pt_info.inferior == tid) return 0;
  }
  return 1;
}
#endif
