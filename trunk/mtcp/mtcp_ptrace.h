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

#include "mtcp_internal.h" 
#include <sys/ptrace.h>
#include <semaphore.h>

#ifdef PTRACE

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

/* Must match the enum from dmtcp/src/ptracewrapper.h. */
/* These are values for singlestep_waited_on field of struct ptrace_info.
 * We only read singlestep_waited_on if last_command is
 * PTRACE_SINGLESTEP_COMMAND. */ 
enum {
  FALSE = 0,
  TRUE
};

#define EFLAGS_OFFSET (64)
#define RECORDPATHLEN (MAXPATHLEN + 128)

/* On restart, superior must wait for inferior to be created before attaching.
 * On resume, inferior already exists. Thus this check is not important on
 * resume.  */
extern sem_t __does_inferior_exist_sem;
extern int __init_does_inferior_exist_sem;
extern int __check_once_does_inferior_exist;

extern char dmtcp_tmp_dir[MAXPATHLEN];

/* Superior, inferior tids and the state of inferior are stored in this file.
 * This extra file is needed because we can't copy to memory the information
 * from ptrace_shared_file in the checkpoint thread. However we need to
 * record the state of inferiors in the checkpoint thread. */
extern char new_ptrace_shared_file[MAXPATHLEN];

/* Superior and inferior tids from ptrace wrapper are stored to this file. */
extern char ptrace_shared_file[MAXPATHLEN];

/* Superior, inferior tids are stored to this file, if PTRACE_SETOPTIONS is set.
 * See below. */
extern char ptrace_setoptions_file[MAXPATHLEN];

/* Pid and checkpoint thread tid are stored to this file. For each process we
 * need to know the ckpt thread, especially for the traced processes. */
extern char checkpoint_threads_file[MAXPATHLEN];

/* File used for synchronization purposes. The checkpoint thread which creates
 * this file gets to write new_ptrace_shared_file. */
extern char ckpt_leader_file[MAXPATHLEN];

/* The following two variables are used in case the superior calls ptrace with
 * PTRACE_SETOPTIONS. In this case, all threads forked off by the already
 * traced inferior, will be traced without calling ptrace. Thus we need to
 * record in a separate file the newly forked off threads as being traced. */
extern __thread pid_t setoptions_superior;
extern __thread int is_ptrace_setoptions;

extern void init_thread_local(void);

extern int empty_ptrace_info(struct ptrace_info pt_info);

extern void create_file(pid_t pid);

extern void have_file(pid_t pid);

extern pid_t is_ckpt_in_ptrace_shared_file (pid_t ckpt);

extern char procfs_state(int tid);

extern int ptracing();

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

#endif 
#endif
