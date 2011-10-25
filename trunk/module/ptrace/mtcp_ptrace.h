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

#ifndef _PTRACE_H
#define _PTRACE_H
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>

#include <sys/ptrace.h>
#include <semaphore.h>

#ifdef __cplusplus
extern "C" {
#endif
/* Must match the structure declaration in dmtcp/src/ptracewapper.h. */
struct ptrace_info {
  pid_t superior;
  pid_t inferior;
  char inferior_st;
  int inferior_is_ckpthread;
  int last_command;
  int singlestep_waited_on;
};

/* Must match the structure declaration in dmtcp/src/ptracewapper.h. */
struct cmd_info {
  int option;
  pid_t superior;
  pid_t inferior;
  int last_command;
  int singlestep_waited_on;
  char inferior_st;
  int file_option;
};

/* Must match the structure declaration in dmtcp/src/ptracewapper.h. */
/* Default values: 0, 0, -1, -1, 0. */
struct ptrace_waitpid_info {
  int is_waitpid_local; /* 1 = waitpid called by DMTCP */
  int is_ptrace_local;  /* 1 = ptrace called by DMTCP */
  pid_t saved_pid;
  int saved_status;
  int has_status_and_pid;
};

/* Must match the enum from dmtcp/src/ptracewrapper.h. */
enum {
  PTRACE_UNSPECIFIED_COMMAND = 0,
  PTRACE_SINGLESTEP_COMMAND,
  PTRACE_CONTINUE_COMMAND
};

/* Must match the enum from dmtcp/src/ptracewrapper.h. */
enum {
  PTRACE_NO_FILE_OPTION = 0,
  PTRACE_SHARED_FILE_OPTION,
  PTRACE_SETOPTIONS_FILE_OPTION,
  PTRACE_CHECKPOINT_THREADS_FILE_OPTION,
  PTRACE_NEW_SHARED_FILE_OPTION
};

/* Must match the enum from dmtcp/src/ptracewrapper.h. */
enum {
  PTRACE_INFO_LIST_UPDATE_IS_INFERIOR_CKPTHREAD = 1,
  PTRACE_INFO_LIST_SORT,
  PTRACE_INFO_LIST_REMOVE_PAIRS_WITH_DEAD_TIDS,
  PTRACE_INFO_LIST_SAVE_THREADS_STATE,
  PTRACE_INFO_LIST_PRINT,
  PTRACE_INFO_LIST_INSERT,
  PTRACE_INFO_LIST_UPDATE_INFO
};


#define EFLAGS_OFFSET (64)
#define RECORDPATHLEN (PATH_MAX + 128)

/* On restart, superior must wait for inferior to be created before attaching.
 * On resume, inferior already exists. Thus this check is not important on
 * resume.  */
extern sem_t __does_inferior_exist_sem;
extern int __init_does_inferior_exist_sem;
extern int __check_once_does_inferior_exist;

extern char dmtcp_tmp_dir[PATH_MAX];

/* Superior, inferior tids and the state of inferior are stored in this file.
 * This extra file is needed because we can't copy to memory the information
 * from ptrace_shared_file in the checkpoint thread. However we need to
 * record the state of inferiors in the checkpoint thread. */
extern char new_ptrace_shared_file[PATH_MAX];

/* Superior and inferior tids from ptrace wrapper are stored to this file. */
extern char ptrace_shared_file[PATH_MAX];

/* Superior, inferior tids are stored to this file, if PTRACE_SETOPTIONS is set.
 * See below. */
extern char ptrace_setoptions_file[PATH_MAX];

/* Pid and checkpoint thread tid are stored to this file. For each process we
 * need to know the ckpt thread, especially for the traced processes. */
extern char checkpoint_threads_file[PATH_MAX];

/* File used for synchronization purposes. The checkpoint thread which creates
 * this file gets to write new_ptrace_shared_file. */
extern char ckpt_leader_file[PATH_MAX];

/* The following two variables are used in case the superior calls ptrace with
 * PTRACE_SETOPTIONS. In this case, all threads forked off by the already
 * traced inferior, will be traced without calling ptrace. Thus we need to
 * record in a separate file the newly forked off threads as being traced. */
extern __thread pid_t setoptions_superior;
extern __thread int is_ptrace_setoptions;

extern int proceed_to_checkpoint;
extern pthread_mutex_t proceed_to_checkpoint_lock;
extern int has_new_ptrace_shared_file;
extern int jalib_ckpt_unlock_ready;
extern pthread_mutex_t jalib_ckpt_unlock_lock;
extern int nthreads;
extern pthread_mutex_t nthreads_lock;
extern int motherofall_done_reading;

extern void mtcp_init_thread_local(void);
void mtcp_ptrace_process_ckpt_thread_creation();
void mtcp_ptrace_process_thread_creation(pid_t clone_id);
void mtcp_ptrace_process_pre_suspend_ckpt_thread();
void mtcp_ptrace_process_pre_suspend_user_thread();
void mtcp_ptrace_send_stop_signal(pid_t tid, int *retry_signalling, int *retval);
void mtcp_ptrace_process_post_suspend_ckpt_thread();
void mtcp_ptrace_process_post_ckpt_resume_ckpt_thread();
void mtcp_ptrace_process_post_restart_resume_ckpt_thread();
void mtcp_ptrace_process_post_ckpt_resume_user_thread();
void mtcp_ptrace_process_post_restart_resume_user_thread();
void mtcp_ptrace_process_pre_resume_user_thread();

struct ptrace_waitpid_info mtcp_get_ptrace_waitpid_info ();
void mtcp_init_ptrace();

void mtcp_ptrace_process_resume_user_thread(int is_ckpt, int is_restart);

extern int empty_ptrace_info(struct ptrace_info pt_info);

extern void create_file(pid_t pid);

extern void have_file(pid_t pid);

extern pid_t is_ckpt_in_ptrace_shared_file (pid_t ckpt);

extern char procfs_state(int tid);

extern int possible_ckpt_leader(pid_t tid);

extern int mtcp_is_ptracing();

extern void ptrace_attach_threads(int isRestart);

extern void ptrace_detach_checkpoint_threads (void);

extern int ptrace_detach_ckpthread(pid_t tid, pid_t supid);

extern void ptrace_detach_user_threads (void);

extern void ptrace_lock_inferiors(void);

extern void ptrace_unlock_inferiors(void);

extern void ptrace_wait4(pid_t pid);

extern ssize_t read_no_error(int fd, void *buf, size_t count);

extern void read_new_ptrace_shared_file ();

extern void read_checkpoint_threads_file();

/* Callbacks to DMTCP, since the ptrace pairs are being stored in a dmtcp::list
 * data structure. */
extern __attribute__ ((visibility ("hidden"))) struct ptrace_info
  (*callback_get_next_ptrace_info)(int index);

extern __attribute__ ((visibility ("hidden"))) void
  (*callback_ptrace_info_list_command)(struct cmd_info cmd);

extern __attribute__ ((visibility ("hidden"))) void
  (*callback_jalib_ckpt_unlock)();

extern __attribute__ ((visibility ("hidden"))) int
  (*callback_ptrace_info_list_size)();

/* The interface between MTCP and DMTCP. */
extern void mtcp_ptrace_info_list_update_is_inferior_ckpthread(pid_t pid,
  pid_t tid);

extern void mtcp_ptrace_info_list_sort();

extern void mtcp_ptrace_info_list_remove_pairs_with_dead_tids();

extern void mtcp_ptrace_info_list_save_threads_state();

extern void mtcp_ptrace_info_list_print();

extern void mtcp_ptrace_info_list_insert(pid_t superior, pid_t inferior,
  int last_command, int singlestep_waited_on, char inf_st, int file_option);

void read_ptrace_setoptions_file (int record_to_file, int rc);
char retrieve_inferior_state(pid_t tid);
#ifdef __cplusplus
}
#endif

#endif
