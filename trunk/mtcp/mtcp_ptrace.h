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

#define EFLAGS_OFFSET (64)

extern sem_t ptrace_read_pairs_sem;
extern int init_ptrace_read_pairs_sem;

extern sem_t __sem;
extern int init__sem;

#define RECORDPATHLEN (MAXPATHLEN + 128)
// This defines an array in a .h file.
//  .h files should only declare types; _NOT_ allocate storage.    - Gene
char dir[MAXPATHLEN];
extern char ptrace_shared_file[MAXPATHLEN];
extern char ptrace_setoptions_file[MAXPATHLEN];
extern char checkpoint_threads_file[MAXPATHLEN];

// values for last_command of struct ptrace_tid_pairs
// These constants must agree with the constants in dmtcp/src/mtcpinterface.cpp
#define PTRACE_UNSPECIFIED_COMMAND 0
#define PTRACE_SINGLESTEP_COMMAND 1
#define PTRACE_CONTINUE_COMMAND 2  

// values for singlestep_waited_on; this values matters only if last_command == PTRACE_SINGLESTEP_COMMAND
#define TRUE 1
#define FALSE 0

/*******************************************
 * continue with non-ptrace declarations   *
 *******************************************/

extern __thread pid_t setoptions_superior;
extern __thread int is_ptrace_setoptions;

extern int has_ptrace_file;
extern pid_t delete_ptrace_leader;
extern int has_setoptions_file;
extern pid_t delete_setoptions_leader;
extern int has_checkpoint_file;
extern pid_t delete_checkpoint_leader;

/***************************************************************************/
/* THIS CODE MUST BE CHANGED TO CHECK TO SEE IF THE USER CREATES EVEN MORE */
/* THREADS.                                                                */
/***************************************************************************/
struct ptrace_tid_pairs {
  pid_t superior;
  pid_t inferior;
  char inferior_st;
  int last_command;
  int singlestep_waited_on;
  int free; //TODO: to be used at a later date
  int eligible_for_deletion;
};

#define MAX_PTRACE_PAIRS_COUNT 1000
extern void init_thread_local(void);
extern struct ptrace_tid_pairs ptrace_pairs[MAX_PTRACE_PAIRS_COUNT];
extern int ptrace_pairs_count;
extern int init_ptrace_pairs;

extern void check_size_for_ptrace_file (const char *file);
extern void write_info_to_file (int file, pid_t superior, pid_t inferior);
extern void writeptraceinfo (pid_t superior, pid_t inferior);
extern void create_file(pid_t pid);
extern void process_ptrace_info (pid_t *delete_ptrace_leader,
        int *has_ptrace_file,
        pid_t *delete_setoptions_leader, int *has_setoptions_file,
        pid_t *delete_checkpoint_leader, int *has_checkpoint_file);
extern char procfs_state(int tid);
extern void ptrace_save_threads_state (void);
extern void delete_file (int file, int delete_leader, int has_file);
extern void ptrace_remove_notexisted(void);
extern void ptrace_attach_threads(int isRestart);
extern void ptrace_detach_checkpoint_threads (void);
extern void ptrace_detach_user_threads (void);
extern void ptrace_lock_inferiors(void);
extern void ptrace_unlock_inferiors(void);
extern void ptrace_wait4(pid_t pid);
extern ssize_t readall(int fd, void *buf, size_t count);
#endif 
#endif
