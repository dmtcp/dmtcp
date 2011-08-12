/****************************************************************************
 *   Copyright (C) 2006-2010 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *   This file is part of the dmtcp/src module of DMTCP (DMTCP:dmtcp/src).  *
 *                                                                          *
 *  DMTCP:dmtcp/src is free software: you can redistribute it and/or        *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP:dmtcp/src is distributed in the hope that it will be useful,      *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#ifndef PTRACEWRAPPER_H
#define PTRACEWRAPPER_H

#ifdef PTRACE
#include <unistd.h>
#include <signal.h>

/* Must match the enum from mtcp/mtcp_ptrace.h. */
enum {
  PTRACE_UNSPECIFIED_COMMAND = 0,
  PTRACE_SINGLESTEP_COMMAND,
  PTRACE_CONTINUE_COMMAND
};

/* Must match the enum from mtcp/mtcp_ptrace.h. */
enum {
  PTRACE_NO_FILE_OPTION = 0,
  PTRACE_SHARED_FILE_OPTION,
  PTRACE_SETOPTIONS_FILE_OPTION,
  PTRACE_CHECKPOINT_THREADS_FILE_OPTION,
  PTRACE_NEW_SHARED_FILE_OPTION
};

/* Must match the enum from mtcp/mtcp_ptrace.h. */
enum {
  PTRACE_INFO_LIST_UPDATE_IS_INFERIOR_CKPTHREAD = 1,
  PTRACE_INFO_LIST_SORT,
  PTRACE_INFO_LIST_REMOVE_PAIRS_WITH_DEAD_TIDS,
  PTRACE_INFO_LIST_SAVE_THREADS_STATE,
  PTRACE_INFO_LIST_PRINT,
  PTRACE_INFO_LIST_INSERT,
  PTRACE_INFO_LIST_UPDATE_INFO
};

/* Must match the enum from mtcp/mtcp_ptrace.h. */
typedef enum {
  PTRACE_INFERIOR_NOT_FOUND = 0,
  PTRACE_INFERIOR_UNKNOWN_STATE = 'U',    // Code refers to 'N' as well ?
  PTRACE_INFERIOR_RUNNING = 'R',
  PTRACE_INFERIOR_STOPPED = 'T'
} PtraceInferiorState;

/* Must match the enum from mtcp/mtcp_ptrace.h. */
enum {
  FALSE = 0,
  TRUE
};

#define MTCP_DEFAULT_SIGNAL SIGUSR2

/* Must match the structure declaration in mtcp/mtcp.h, without the operator
 * overloading feature. */
struct ptrace_info {
  pid_t superior;
  pid_t inferior;
  PtraceInferiorState inferior_st;
  int inferior_is_ckpthread;
  int last_command;
  int singlestep_waited_on;

  bool operator==(const struct ptrace_info& a) {
    return this->superior == a.superior && this->inferior == a.inferior;
  }

  bool operator!= (const struct ptrace_info& a) {
    return this->superior != a.superior || this->inferior != a.inferior;
  }
};

/* Must match the structure declaration in mtcp/mtcp.h. */
struct cmd_info {
  int option;
  pid_t superior;
  pid_t inferior;
  int last_command;
  int singlestep_waited_on;
  PtraceInferiorState inferior_st;
  int file_option;
};

/* Must match the structure declaration in mtcp/mtcp.h. */
/* Default values: 0, 0, -1, -1, 0. */
struct ptrace_waitpid_info {
  int is_waitpid_local; /* 1 = waitpid called by DMTCP */
  int is_ptrace_local;  /* 1 = ptrace called by DMTCP */
  pid_t saved_pid;
  int saved_status;
  int has_status_and_pid;
};

static const struct ptrace_info EMPTY_PTRACE_INFO
  = {0, 0, PTRACE_INFERIOR_UNKNOWN_STATE, 0, 0, 0};

static const struct cmd_info EMPTY_CMD_INFO
  = {0, 0, 0, 0, 0, PTRACE_INFERIOR_UNKNOWN_STATE, 0};

extern dmtcp::list<struct ptrace_info> ptrace_info_list;

typedef struct ptrace_waitpid_info ( *t_mtcp_get_ptrace_waitpid_info ) ();
__attribute__ ((visibility ("hidden"))) extern t_mtcp_get_ptrace_waitpid_info
  mtcp_get_ptrace_waitpid_info;

typedef void ( *t_mtcp_init_thread_local ) ();
__attribute__ ((visibility ("hidden"))) extern t_mtcp_init_thread_local
  mtcp_init_thread_local;

typedef int ( *t_mtcp_ptracing) ();
__attribute__ ((visibility ("hidden"))) extern t_mtcp_ptracing
  mtcp_ptracing;

__attribute__ ((visibility ("hidden"))) extern sigset_t signals_set;

void ptrace_info_list_insert (pid_t superior, pid_t inferior, int last_command,
  int singlestep_waited_on, PtraceInferiorState inferior_st, int file_option);

PtraceInferiorState procfs_state(int tid);

extern "C" struct ptrace_info get_next_ptrace_info(int index);

extern "C" void ptrace_info_list_command(struct cmd_info cmd);

extern "C" int ptrace_info_list_size();

extern "C" void ptrace_info_list_update_info(pid_t superior, pid_t inferior,
  int singlestep_waited_on);

#endif
#endif
