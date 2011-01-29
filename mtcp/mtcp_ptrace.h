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

// These constants must agree with the constants in mtcp/mtcp.c
enum {
  PTRACE_UNSPECIFIED_COMMAND = 0,
  PTRACE_SINGLESTEP_COMMAND,
  PTRACE_CONTINUE_COMMAND
};

enum {
  PTRACE_NO_FILE_OPTION = 0,
  PTRACE_SHARED_FILE_OPTION,
  PTRACE_SETOPTIONS_FILE_OPTION,
  PTRACE_CHECKPOINT_THREADS_FILE_OPTION,
  PTRACE_NEW_SHARED_FILE_OPTION
};

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

extern sem_t __sem;
extern int init__sem;

#define RECORDPATHLEN (MAXPATHLEN + 128)
// This defines an array in a .h file.
//  .h files should only declare types; _NOT_ allocate storage.    - Gene
extern char dir[MAXPATHLEN];
extern char new_ptrace_shared_file[MAXPATHLEN];
extern char ptrace_shared_file[MAXPATHLEN];
extern char ptrace_setoptions_file[MAXPATHLEN];
extern char checkpoint_threads_file[MAXPATHLEN];
extern char ckpt_leader_file[MAXPATHLEN];

// values for last_command of struct ptrace_info
// These constants must agree with the constants in dmtcp/src/mtcpinterface.cpp
#define PTRACE_UNSPECIFIED_COMMAND 0
#define PTRACE_SINGLESTEP_COMMAND 1
#define PTRACE_CONTINUE_COMMAND 2  

// values for singlestep_waited_on; this values matters only if last_command == PTRACE_SINGLESTEP_COMMAND
enum {
  FALSE = 0,
  TRUE
};

/*******************************************
 * continue with non-ptrace declarations   *
 *******************************************/

/* The following two variables are used in case the superior calls ptrace with
 * PTRACE_SETOPTIONS. In this case, all threads forked off by the already
 * traced inferior, will be traced without calling ptrace. Thus we need to
 * record in a separate file the newly forked off threads as being traced. */
extern __thread pid_t setoptions_superior;
extern __thread int is_ptrace_setoptions;

extern int empty_ptrace_info(struct ptrace_info pt_info);
extern void init_thread_local(void);
extern void create_file(pid_t pid);
extern void have_file(pid_t pid);
extern pid_t is_ckpt_in_ptrace_shared_file (pid_t ckpt);
extern void process_ptrace_info (pid_t *delete_ptrace_leader,
        int *has_ptrace_file,
        pid_t *delete_setoptions_leader, int *has_setoptions_file,
        pid_t *delete_checkpoint_leader, int *has_checkpoint_file);
extern char procfs_state(int tid);
extern void ptrace_attach_threads(int isRestart);
extern void ptrace_detach_checkpoint_threads (void);
extern int ptrace_detach_ckpthread(pid_t tid, pid_t supid);
extern void ptrace_detach_user_threads (void);
extern void ptrace_lock_inferiors(void);
extern void ptrace_unlock_inferiors(void);
extern void ptrace_wait4(pid_t pid);
extern ssize_t readall(int fd, void *buf, size_t count);
extern __attribute__ ((visibility ("hidden"))) struct ptrace_info
  (*callback_get_next_ptrace_info)(int index);
extern __attribute__ ((visibility ("hidden"))) void
  (*callback_ptrace_info_list_command)(struct cmd_info cmd);

extern void mtcp_ptrace_info_list_update_is_inferior_ckpthread(pid_t pid,
  pid_t tid);

extern void mtcp_ptrace_info_list_sort();

extern void mtcp_ptrace_info_list_remove_pairs_with_dead_tids();

extern void mtcp_ptrace_info_list_save_threads_state();

extern void mtcp_ptrace_info_list_print();

extern void mtcp_ptrace_info_list_insert(pid_t superior, pid_t inferior,
  int last_command, int singlestep_waited_on, char inf_st, int file_option);

extern void mtcp_ptrace_info_list_update_info(pid_t superior, pid_t inferior,
  int singlestep_waited_on);

extern void read_new_ptrace_shared_file ();

extern void read_checkpoint_threads_file();
#endif 
#endif
