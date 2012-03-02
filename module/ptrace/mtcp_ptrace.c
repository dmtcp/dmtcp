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
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sched.h>
#include <sys/user.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#define MTCP_PRINTF(...) \
  do { \
    printf("[%d:%d] %s:%d %s\n\t", getpid(), gettid(), \
                                   __FILE__, __LINE__, __FUNCTION__); \
    printf(__VA_ARGS__); \
  } while (0)

#ifdef DEBUG
# define DPRINTF(...) MTCP_PRINTF(__VA_ARGS__)
#else
# define DPRINTF(...)
#endif

#define mtcp_abort abort
#include "mtcp_ptrace.h"
#include "ptrace.h"
#include "dmtcpmodule.h"


#define GETTID() (int)syscall(SYS_gettid)
#define TGKILL(pid, tid, sig) (int)syscall(SYS_tgkill, pid, tid, sig)

const unsigned char DMTCP_SYS_sigreturn =  0x77;
const unsigned char DMTCP_SYS_rt_sigreturn = 0xad;

static const unsigned char linux_syscall[] = { 0xcd, 0x80 };

__thread struct ptrace_waitpid_info __ptrace_waitpid;

__thread pid_t setoptions_superior = -1;
__thread int is_ptrace_setoptions = FALSE;

char new_ptrace_shared_file[PATH_MAX];
char ptrace_shared_file[PATH_MAX];
char ptrace_setoptions_file[PATH_MAX];
char checkpoint_threads_file[PATH_MAX];
char ckpt_leader_file[PATH_MAX];

int motherofall_done_reading = 0;
pthread_mutex_t motherofall_done_reading_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t motherofall_done_reading_cv = PTHREAD_COND_INITIALIZER;
int jalib_ckpt_unlock_ready = 0;
pthread_mutex_t jalib_ckpt_unlock_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t jalib_ckpt_unlock_cv = PTHREAD_COND_INITIALIZER;
int proceed_to_checkpoint = 0;
pthread_mutex_t proceed_to_checkpoint_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t proceed_to_checkpoint_cv = PTHREAD_COND_INITIALIZER;
int nthreads = 0;
pthread_mutex_t nthreads_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t nthreads_cv = PTHREAD_COND_INITIALIZER;

void mtcp_ptrace_info_list_update_info(int singlestep_waited_on);

pid_t ckpt_leader = 0;
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

  fd = open("/proc/self/stat", O_RDONLY, 0);
  if (fd == -1) return -1;

  num_read = read(fd, sbuf, sizeof sbuf - 1);
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
int mtcp_is_ptracing() {
  struct stat buf;
  return (ptrace_info_list_size() > 0 ||
          stat(ptrace_shared_file, &buf) == 0);
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

void mtcp_init_thread_local()
{
  __ptrace_waitpid.is_waitpid_local = 0;    // no crash on pre-access
  __ptrace_waitpid.is_ptrace_local = 0;     // no crash on pre-access
  __ptrace_waitpid.saved_pid = -1;          // no crash on pre-access
  __ptrace_waitpid.saved_status = -1;       // no crash on pre-access
  __ptrace_waitpid.has_status_and_pid = 0;  // crash

  setoptions_superior = -1;
  is_ptrace_setoptions = FALSE;
}

void mtcp_init_ptrace()
{
  DPRINTF("begin init_thread_local\n");
  mtcp_init_thread_local();

  strcpy(ptrace_shared_file, ptrace_get_tmpdir());
  strcpy(ptrace_setoptions_file, ptrace_get_tmpdir());
  strcpy(checkpoint_threads_file, ptrace_get_tmpdir());
  strcpy(new_ptrace_shared_file, ptrace_get_tmpdir());
  strcpy(ckpt_leader_file, ptrace_get_tmpdir());

  strcat(ptrace_shared_file, "/ptrace_shared");
  strcat(ptrace_setoptions_file, "/ptrace_setoptions");
  strcat(checkpoint_threads_file, "/ptrace_ckpthreads");
  strcat(new_ptrace_shared_file, "/new_ptrace_shared");
  strcat(ckpt_leader_file, "/ckpt_leader_file");
}

void mtcp_ptrace_process_ckpt_thread_creation()
{
  mtcp_ptrace_info_list_insert(getpid(), GETTID(),
                               PTRACE_UNSPECIFIED_COMMAND, FALSE,
                               'u', PTRACE_CHECKPOINT_THREADS_FILE_OPTION);
}

void mtcp_ptrace_process_thread_creation(pid_t clone_id)
{
  /* Record new pairs to files: newly traced threads to ptrace_shared_file;
   * also, write the checkpoint thread to checkpoint_threads_file. */
  if (is_ptrace_setoptions == TRUE) {
    mtcp_ptrace_info_list_insert(setoptions_superior, clone_id,
                                 PTRACE_UNSPECIFIED_COMMAND,
                                 FALSE, 'u', PTRACE_SHARED_FILE_OPTION);
  }
  else read_ptrace_setoptions_file(TRUE, clone_id);
}

void mtcp_ptrace_process_pre_suspend_ckpt_thread()
{
  if (mtcp_is_ptracing()) {
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
    int ckpt_ptraced_by = 0;
    int index = 0;
    struct ptrace_info *pt_info;
    while ((pt_info = get_next_ptrace_info(index++)) != NULL) {
      if (pt_info->inferior == GETTID()) {
        ckpt_ptraced_by = pt_info->superior;
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

void mtcp_ptrace_process_pre_suspend_user_thread()
{
  if (mtcp_is_ptracing()) {

    /* Wait for JALIB_CKPT_UNLOCK to have been called. */
    pthread_mutex_lock(&jalib_ckpt_unlock_lock);
    if (!jalib_ckpt_unlock_ready) pthread_cond_wait(&jalib_ckpt_unlock_cv,
                                                    &jalib_ckpt_unlock_lock);
    pthread_mutex_unlock(&jalib_ckpt_unlock_lock);

    /* Motherofall reads in new_ptrace_shared_file and
     * checkpoint_threads_file. */
    if (getpid() == GETTID()) {
      read_new_ptrace_shared_file();
      read_checkpoint_threads_file();
      mtcp_ptrace_info_list_remove_pairs_with_dead_tids();
      mtcp_ptrace_info_list_sort();
      pthread_mutex_lock(&motherofall_done_reading_lock);
      motherofall_done_reading = 1;
      pthread_cond_broadcast(&motherofall_done_reading_cv);
      pthread_mutex_unlock(&motherofall_done_reading_lock);
    }

    /* Wait for motherofall to have read in the ptrace related files. */
    pthread_mutex_lock(&motherofall_done_reading_lock);
    if (!motherofall_done_reading)
      pthread_cond_wait(&motherofall_done_reading_cv,
                        &motherofall_done_reading_lock);
    pthread_mutex_unlock(&motherofall_done_reading_lock);
  }

  /* Safe to detach - we have all the information in memory. */
  ptrace_unlock_inferiors();
  ptrace_detach_checkpoint_threads();
  ptrace_detach_user_threads();

  if (mtcp_is_ptracing()) {
    pthread_mutex_lock(&nthreads_lock);
    nthreads--;
    if (nthreads == 0) pthread_cond_broadcast(&nthreads_cv);
    pthread_mutex_unlock(&nthreads_lock);

    pthread_mutex_lock(&proceed_to_checkpoint_lock);
    if (!proceed_to_checkpoint) pthread_cond_wait(&proceed_to_checkpoint_cv,
                                                  &proceed_to_checkpoint_lock);
    pthread_mutex_unlock(&proceed_to_checkpoint_lock);
  }
}

void mtcp_ptrace_write_to_new_ptrace_shared_file(int fd, int superior,
  int inferior, char inferior_st) {
  if (write(fd, &superior, sizeof(pid_t)) == -1) {
    MTCP_PRINTF("Error writing to file: %s\n", strerror(errno));
    mtcp_abort();
  }
  if (write(fd, &inferior, sizeof(pid_t)) == -1) {
    MTCP_PRINTF("Error writing to file: %s\n", strerror(errno));
    mtcp_abort();
  }
  if (write(fd, &inferior_st, sizeof(char)) == -1) {
    MTCP_PRINTF("Error writing to file: %s\n", strerror(errno));
    mtcp_abort();
  }
}

char mtcp_ptrace_get_tid_from_new_ptrace_shared_file(pid_t tid) {
  int fd = open(new_ptrace_shared_file, O_RDONLY);
  if (fd == -1) {
    MTCP_PRINTF("Error opening the file: %s\n", strerror(errno));
    mtcp_abort();
  }

  pid_t superior, inferior;
  char inferior_st = 'u';
  while (read_no_error(fd, &superior, sizeof(pid_t)) > 0) {
    read_no_error(fd, &inferior, sizeof(pid_t));
    read_no_error(fd, &inferior_st, sizeof(char));
    if (tid == inferior) break;
  }
  if (close(fd) != 0) {
    MTCP_PRINTF("error closing file, %s.\n", strerror(errno));
    mtcp_abort();
  }
  return inferior_st;
}

void mtcp_ptrace_info_list_write_to_new_ptrace_shared_file(int fd) {
  struct ptrace_info *pt_info;
  pid_t superior, inferior;
  char inf_st;
  int index = 0;

  while ((pt_info = get_next_ptrace_info(index++)) != NULL) {
    superior = pt_info->superior;
    inferior = pt_info->inferior;
    inf_st = pt_info->inferior_st;
    mtcp_ptrace_write_to_new_ptrace_shared_file(fd, superior, inferior, inf_st);
  }
}

void mtcp_ptrace_send_stop_signal(pid_t tid, int *retry_signalling, int *retval)
{
  DPRINTF("Beginning of mtcp_ptrace_send_stop_signal [tid: %d]\n", tid);
  *retry_signalling = 0;

  if (!mtcp_is_ptracing()) {
    DPRINTF("MTCP NOT PTRACING");
    *retry_signalling = 1;
    *retval = -1;
    return;
  }

  char inferior_st = 'u';
  if (ckpt_leader) {
    int new_ptrace_fd = open(new_ptrace_shared_file,
                             O_CREAT|O_APPEND|O_WRONLY|O_FSYNC, 0644);
    if (new_ptrace_fd == -1) {
      MTCP_PRINTF("Error writing to file: %s\n", strerror(errno));
      mtcp_abort();
    }
    int ptrace_fd = open(ptrace_shared_file, O_RDONLY);
    if (ptrace_fd != -1) {
      pid_t superior, inferior;
      char inf_st;
      while (read_no_error(ptrace_fd, &superior, sizeof(pid_t)) > 0) {
        read_no_error(ptrace_fd, &inferior, sizeof(pid_t));
        inf_st = procfs_state(inferior);
        if (!inf_st) continue;
        if (inferior == tid) inferior_st = inf_st;
        mtcp_ptrace_write_to_new_ptrace_shared_file(new_ptrace_fd, superior,
                                                    inferior, inf_st);
      }
      if (close(ptrace_fd) != 0) {
        MTCP_PRINTF("error while closing file, %s\n", strerror(errno));
        mtcp_abort();
      }
    }
    /* Update the pairs that are already in memory. */
    mtcp_ptrace_info_list_save_threads_state();
    if (inferior_st == 'u') inferior_st = retrieve_inferior_state(tid);
    mtcp_ptrace_info_list_write_to_new_ptrace_shared_file(new_ptrace_fd);
    if (close(new_ptrace_fd) != 0) {
      MTCP_PRINTF("error while closing file: %s\n", strerror(errno));
      mtcp_abort();
    }
  } else {
    struct stat buf;
    while (stat(new_ptrace_shared_file, &buf)) {
      struct timespec ts;
      ts.tv_sec = 0;
      ts.tv_nsec = 100000000;
      if (errno != ENOENT) {
        MTCP_PRINTF("Unexpected error in stat: %s\n", strerror(errno));
        mtcp_abort();
      }
      nanosleep(&ts,NULL);
    }
    DPRINTF("done waiting for new_ptrace_shared_file [tid=%d]\n", tid);
    /* Get the status of tid from new_ptrace_shared_file. */
    inferior_st = mtcp_ptrace_get_tid_from_new_ptrace_shared_file(tid);
  }

  DPRINTF("inferior_st=%c [tid=%d]\n", inferior_st, tid);
  if (inferior_st == 'N') {
    /* If the state is unknown, send a stop signal to inferior. */
    if (TGKILL(getpid(), tid, dmtcp_get_ckpt_signal()) < 0) {
      if (errno != ESRCH) {
        MTCP_PRINTF ("error signalling thread %d: %s\n",
                     tid, strerror(errno));
      }
      *retval = -1;
      return;
    }
  } else {
    DPRINTF("%c %d\n", inferior_st, tid);
    /* If the state is not stopped, then send a stop signal to
     * the inferior. */
    if (inferior_st != 'T') {
      if (TGKILL(getpid(), tid, dmtcp_get_ckpt_signal()) < 0) {
        if (errno != ESRCH) {
          MTCP_PRINTF ("error signalling thread %d: %s\n",
                       tid, strerror(errno));
        }
        *retval = -1;
        return;
      }
    }
    if (inferior_st != 'u') { /* We're not the superior. */
      DPRINTF("inferior_st=%c creating detach.PID [tid=%d]\n",
              inferior_st, tid);
      superior_can_detach_from_inferior(tid);
    }
  }
}

void mtcp_ptrace_thread_died_before_checkpoint()
{
  if (mtcp_is_ptracing()) {
    pthread_mutex_lock(&nthreads_lock);
    nthreads--;
    if (nthreads == 0) pthread_cond_broadcast(&nthreads_cv);
    pthread_mutex_unlock(&nthreads_lock);
  }
}

void mtcp_ptrace_process_post_suspend_ckpt_thread()
{
  if (mtcp_is_ptracing()) {
    jalib_ckpt_unlock();
    /* Allow user threads to process new_ptrace_shared_file. */
    pthread_mutex_lock(&jalib_ckpt_unlock_lock);
    jalib_ckpt_unlock_ready = 1;
    pthread_cond_broadcast(&jalib_ckpt_unlock_cv);
    pthread_mutex_unlock(&jalib_ckpt_unlock_lock);

    /* Wait for all the threads to have been detached. */
    pthread_mutex_lock(&nthreads_lock);
    if (nthreads) pthread_cond_wait(&nthreads_cv, &nthreads_lock);
    pthread_mutex_unlock(&nthreads_lock);

    /* It is now safe to proceed to checkpoint. */
    pthread_mutex_lock(&proceed_to_checkpoint_lock);
    proceed_to_checkpoint = 1;
    pthread_cond_broadcast(&proceed_to_checkpoint_cv);
    pthread_mutex_unlock(&proceed_to_checkpoint_lock);
  }
}

void mtcp_ptrace_process_post_restart_resume_ckpt_thread()
{
  struct ptrace_info *pt_info;
  int index = 0;

  while ((pt_info = get_next_ptrace_info(index++)) != NULL) {
    if (GETTID() == pt_info->inferior) {
      ckpt_thread_is_ready(GETTID());
      return;
    }
  }
}

void mtcp_ptrace_process_post_ckpt_resume_user_thread()
{
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
  motherofall_done_reading = 0;
}

void mtcp_ptrace_process_post_restart_resume_user_thread()
{
  ptrace_attach_threads(1);
}

void mtcp_ptrace_process_resume_user_thread(int is_ckpt, int is_restart)
{
  if (is_ckpt || is_restart) {
    ptrace_attach_threads(is_restart);
  }
  if (!is_restart) {
    /* All user threads will try to delete these files. It doesn't matter if
     * unlink fails. The sole purpose of these unlinks is to prevent these
     * files to become too big. Big files imply an increased processing time
     * of the ptrace pairs. */
    unlink(ptrace_shared_file);
    unlink(ptrace_setoptions_file);
    unlink(checkpoint_threads_file);
    unlink(new_ptrace_shared_file);
    unlink(ckpt_leader_file);
  }
  ptrace_lock_inferiors();
}

void mtcp_ptrace_process_pre_resume_user_thread()
{
  ptrace_lock_inferiors();
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
  if (!mtcp_is_ptracing()) return;

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
  struct ptrace_info *pt_info;
  int index = 0;
  struct cmd_info cmd;

  DPRINTF("%d started.\n", GETTID());

  /* Make sure the inferior threads exist & are inside ptrace_attach_threads. */
  while ((pt_info = get_next_ptrace_info(index++)) != NULL) {
    if (pt_info->superior == GETTID() && !pt_info->inferior_is_ckpthread)
      is_inferior_in_ptrace_attach_threads(pt_info->inferior);
  }

  index = 0;

  while ((pt_info = get_next_ptrace_info(index++)) != NULL) {
    superior = pt_info->superior;
    inferior = pt_info->inferior;
    last_command = pt_info->last_command;
    singlestep_waited_on = pt_info->singlestep_waited_on;
    inferior_st = pt_info->inferior_st;
    inferior_is_ckpthread = pt_info->inferior_is_ckpthread;

    if (superior == GETTID()) {
      if (inferior_is_ckpthread) {
        is_ckpt_thread_ready(inferior);
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

        // Mark thread as attached.
        pt_info->attach_state = 1;

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
      superior_has_attached(inferior);
      // Mark thread as attached.
      pt_info->attach_state = 1;

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
          DPRINTF("%d is dead because it was terminated by signal\n", inferior);
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
              // FIXME: I had to change the eax to rax and esp to rsp as the
              // compiler was complaining about the fields missing from regs. I
              // do not understand why it would compile fine when it was under
              // mtcp.
              if (regs.rax != DMTCP_SYS_rt_sigreturn) addr = regs.rsp;
              else {
                addr = regs.rsp + 8;
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
              if (mtcp_ptrace(PTRACE_POKEDATA, inferior, (void *)addr,
			      (void *)eflags)
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
      inferior_is_in_ptrace_attach_threads(inferior);
      wait_for_superior_to_attach(inferior);
    }
  }
  DPRINTF("%d done.\n", GETTID());
}

/* Detach only the checkpoint threads.
 * The checkpoint threads need to be detached before user threads, so that they
 * can process the checkpoint message from the coordinator. */
void ptrace_detach_checkpoint_threads ()
{
  if (!mtcp_is_ptracing()) return;

  int ret;
  struct ptrace_info *pt_info;
  int index = 0;

  while ((pt_info = get_next_ptrace_info(index++)) != NULL) {
    if ((pt_info->superior == GETTID()) && pt_info->inferior_is_ckpthread) {
      if ((ret = ptrace_detach_ckpthread(pt_info->inferior,
                                         pt_info->superior)) != 0) {
        if (ret == -ENOENT)
          MTCP_PRINTF("process does not exist %d\n", pt_info->inferior);
        mtcp_abort();
      }
    }
  }
  DPRINTF("done for %d\n", GETTID());
}

/* This function detaches the user threads. */
void ptrace_detach_user_threads ()
{
  if (!mtcp_is_ptracing()) return;

  int status = 0;
  int index = 0;
  int tpid;
  char pstate;
  struct ptrace_info *pt_info;
  int all_threads_detached = 0;

  while (!all_threads_detached) {
    // Reset index
    index = 0;
    /* all_threads_detached starts out as 1. If we skip detaching a thread
     * because it wasn't ready at the time, the flag is set to 0 again, meaning
     * we repeat this outer loop. */
    all_threads_detached = 1;
    while ((pt_info = get_next_ptrace_info(index++)) != NULL) {
      if (pt_info->inferior_is_ckpthread || !pt_info->attach_state) {
        continue;
      }

      if (pt_info->superior == GETTID()) {
        /* All UTs must receive a MTCP_DEFAULT_SIGNAL from their CT.
         * Was the status of this thread already read by the debugger? */
        pstate = procfs_state(pt_info->inferior);
        if (pstate == 0) { /* The thread does not exist. */
          MTCP_PRINTF("process not exist %d\n", pt_info->inferior);
          mtcp_abort();
        } else if (pstate == 'T') {
          /* This is a stopped thread. It is possible for gdb or another thread
           * to read the status of this thread before us. Consequently we will
           * block. Thus we need to read without hanging. */
          tpid = mtcp_waitpid(pt_info->inferior, &status, WNOHANG);
          if (tpid == -1 && errno == ECHILD) {
            if ((tpid = mtcp_waitpid(pt_info->inferior, &status,
                    __WCLONE | WNOHANG)) == -1) {
              MTCP_PRINTF("waitpid(..,__WCLONE) %s\n", strerror(errno));
            }
          }
        } else {
          /* The thread is not in a stopped state. The thread will be stopped by
           * the CT of the process it belongs to, by the delivery of
           * MTCP_DEFAULT_SIGNAL. It is safe to call blocking waitpid. */
          tpid = mtcp_waitpid(pt_info->inferior, &status, 0);
          if (tpid == -1 && errno == ECHILD) {
            if ((tpid = mtcp_waitpid(pt_info->inferior, &status,
                    __WCLONE)) == -1) {
              MTCP_PRINTF("waitpid(..,__WCLONE) %s\n", strerror(errno));
            }
          }
          if (WIFSTOPPED(status)) {
            if (WSTOPSIG(status) == dmtcp_get_ckpt_signal())
              DPRINTF("UT %d stopped by the delivery of MTCP_DEFAULT_SIGNAL\n",
                  pt_info->inferior);
            else /* We should never get here. */
              DPRINTF("UT %d was stopped by the delivery of %d\n",
                  pt_info->inferior, WSTOPSIG(status));
          } else  /* We should never end up here. */
            DPRINTF("UT %d was NOT stopped by a signal\n", pt_info->inferior);
        }

        if (pt_info->last_command == PTRACE_SINGLESTEP_COMMAND &&
            pt_info->singlestep_waited_on == FALSE) {
          __ptrace_waitpid.has_status_and_pid = 1;
          __ptrace_waitpid.saved_status = status;
          mtcp_ptrace_info_list_update_info(TRUE);
        }

        if (wait_until_superior_can_detach_from_inferior(pt_info->inferior)) {
          // Inferior has created the file -- we can detach now.
          if (mtcp_ptrace(PTRACE_DETACH, pt_info->inferior, 0,
                (void*) (unsigned long) dmtcp_get_ckpt_signal()) == -1) {
            MTCP_PRINTF("parent = %d child = %d PTRACE_DETACH failed, error = %d\n",
                (int)pt_info->superior, (int)pt_info->inferior, errno);
          }
          // Mark thread as detached now.
          pt_info->attach_state = 0;
        } else {
          /* Reset this flag here meaning at least one thread wasn't detached
           * (the file hadn't been created, meaning the thread wasn't ready to
           * be detached). */
          all_threads_detached = 0;
        }
      }
    }
  }
  DPRINTF("%d done.\n", GETTID());
}

static void form_file(char *file, char *root, pid_t pid)
{
  char tmp[20];
  sprintf(tmp, "%d", pid);
  strcpy(file, ptrace_get_tmpdir());
  strcat(file, "/");
  strcat(file, root);
  strcat(file, ".");
  strcat(file, tmp);
}

void ptrace_lock_inferiors()
{
    if (!mtcp_is_ptracing()) return;
    char file[RECORDPATHLEN];
    form_file(file, "dmtcp_ptrace_unlocked", GETTID());
    unlink(file);
}

void ptrace_unlock_inferiors()
{
    if (!mtcp_is_ptracing()) return;

    char file[RECORDPATHLEN];
    form_file(file, "dmtcp_ptrace_unlocked", GETTID());
    int fd = creat(file, 0644);
    if (fd < 0) {
        MTCP_PRINTF("Error creating lock file: %s\n", strerror(errno));
        mtcp_abort();
    }
    close(fd);
}

void create_file(char *action, pid_t pid)
{
  char file[RECORDPATHLEN];
  form_file(file, action, pid);
  int fd = open(file, O_CREAT|O_APPEND|O_WRONLY, 0644);
  if (fd == -1) {
    MTCP_PRINTF("Error opening file %s\n: %s\n", file, strerror(errno));
    mtcp_abort();
  }
  if (close(fd) != 0) {
    MTCP_PRINTF("Error closing file\n: %s\n", strerror(errno));
    mtcp_abort();
  }
}

/* Files are used for IPC. */
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

void have_file(char *action, pid_t pid)
{
  char file[RECORDPATHLEN];
  form_file(file, action, pid);
  wait_for_file(file);
  if (unlink(file) == -1) {
    MTCP_PRINTF("unlink failed: %s\n", strerror(errno));
    mtcp_abort();
  }
}

/* Next two functions make sure that the inferior thread is stopped before
 * detaching. Otherwise, we can't detach. */
void superior_can_detach_from_inferior(pid_t inferior)
{
  create_file("detach", inferior);
}

/*
 * This function returns 0 if the file for the given inferior does NOT exist,
 * else returns 1.
 */
int wait_until_superior_can_detach_from_inferior(pid_t inferior)
{
  char file[RECORDPATHLEN];
  struct stat buf;
  form_file(file, "detach", inferior);
  if (stat(file, &buf) < 0) {
    // File does not exist.
    return 0;
  }
  if (unlink(file) == -1) {
    MTCP_PRINTF("unlink failed: %s\n", strerror(errno));
    mtcp_abort();
  }
  return 1;
}

/* Next two functions make sure that the inferior waits inside
 * DMTCP for the superior to re-attach. */
void superior_has_attached(pid_t inferior)
{
  create_file("has_attached", inferior);
}

void wait_for_superior_to_attach(pid_t inferior)
{
  have_file("has_attached", inferior);
}

/* Next two functions make sure that the inferior threads exist and are in
 * ptrace_attach_threads. */
void is_inferior_in_ptrace_attach_threads(pid_t inferior)
{
  have_file("ready_to_attach", inferior);
}

void inferior_is_in_ptrace_attach_threads(pid_t inferior)
{
  create_file("ready_to_attach", inferior);
}

/* Next two functions make sure that the ckpt thread is ready to be attached. */
void is_ckpt_thread_ready(pid_t inferior)
{
  have_file("ckpt_th_is_ready", inferior);
}

void ckpt_thread_is_ready(pid_t inferior)
{
  create_file("ckpt_th_is_ready", inferior);
}

void ptrace_wait4(pid_t pid)
{
  char file[RECORDPATHLEN];
  form_file(file, "dmtcp_ptrace_unlocked", pid);
  wait_for_file(file);
}

struct ptrace_waitpid_info mtcp_get_ptrace_waitpid_info ()
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

  if (islower(state[0])) {
    return toupper(state[0]);
  }
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
      mtcp_ptrace_info_list_insert(superior, inferior,
                                   PTRACE_UNSPECIFIED_COMMAND, FALSE, 'u',
                                   PTRACE_NO_FILE_OPTION);
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
  struct cmd_info cmd;
  init_empty_cmd_info(&cmd);
  cmd.option = PTRACE_INFO_LIST_UPDATE_IS_INFERIOR_CKPTHREAD;
  cmd.superior = pid;
  cmd.inferior = tid;
  ptrace_info_list_command(cmd);
}

void mtcp_ptrace_info_list_sort() {
  struct cmd_info cmd;
  init_empty_cmd_info(&cmd);
  cmd.option = PTRACE_INFO_LIST_SORT;
  ptrace_info_list_command(cmd);
}

void mtcp_ptrace_info_list_remove_pairs_with_dead_tids() {
  struct cmd_info cmd;
  init_empty_cmd_info(&cmd);
  cmd.option = PTRACE_INFO_LIST_REMOVE_PAIRS_WITH_DEAD_TIDS;
  ptrace_info_list_command(cmd);
}

void mtcp_ptrace_info_list_save_threads_state() {
  struct cmd_info cmd;
  init_empty_cmd_info(&cmd);
  cmd.option = PTRACE_INFO_LIST_SAVE_THREADS_STATE;
  ptrace_info_list_command(cmd);
}

void mtcp_ptrace_info_list_print() {
  struct cmd_info cmd;
  init_empty_cmd_info(&cmd);
  cmd.option = PTRACE_INFO_LIST_PRINT;
  ptrace_info_list_command(cmd);
}

void mtcp_ptrace_info_list_insert(pid_t superior, pid_t inferior,
  int last_command, int singlestep_waited_on, char inf_st, int file_option) {
  struct cmd_info cmd;
  init_empty_cmd_info(&cmd);
  cmd.option = PTRACE_INFO_LIST_INSERT;
  cmd.superior = superior;
  cmd.inferior = inferior;
  cmd.last_command = last_command;
  cmd.singlestep_waited_on = singlestep_waited_on;
  cmd.inferior_st = inf_st;
  cmd.file_option = file_option;
  ptrace_info_list_command(cmd);
}

void mtcp_ptrace_info_list_update_info(int singlestep_waited_on) {
  struct cmd_info cmd;
  init_empty_cmd_info(&cmd);
  cmd.option = PTRACE_INFO_LIST_UPDATE_INFO;
  cmd.singlestep_waited_on = singlestep_waited_on;
  ptrace_info_list_command(cmd);
}

char retrieve_inferior_state(pid_t tid) {
  int index = 0;
  struct ptrace_info *pt_info;
  while ((pt_info = get_next_ptrace_info(index++)) != NULL) {
    if (pt_info->inferior == tid) return procfs_state(pt_info->inferior);
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

  int index = 0;
  struct ptrace_info *pt_info;
  while ((pt_info = get_next_ptrace_info(index++)) != NULL) {
    if (pt_info->inferior == tid) return 0;
  }
  return 1;
}
