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

enum {
  FALSE = 0,
  TRUE
};

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

/* This needs to match the structure declaration in mtcp/mtcp_ptrace.h. */
struct ptrace_info {
  pid_t superior;
  pid_t inferior;
  char inferior_st;
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

static const struct ptrace_info EMPTY_PTRACE_INFO = {0, 0, 0, 0, 0, 0};

struct cmd_info {
  int option;
  pid_t superior;
  pid_t inferior;
  int last_command;
  int singlestep_waited_on;
  char inferior_st;
  int file_option;
};

static const struct cmd_info EMPTY_CMD_INFO = {0, 0, 0, 0, 0, 0, 0};

extern dmtcp::list<struct ptrace_info> ptrace_info_list;

typedef pid_t ( *get_saved_pid_t) ( );
__attribute__ ((visibility ("hidden"))) extern get_saved_pid_t
  get_saved_pid_ptr;

typedef int ( *get_saved_status_t) ( );
__attribute__ ((visibility ("hidden"))) extern get_saved_status_t
  get_saved_status_ptr;

typedef int ( *get_has_status_and_pid_t) ( );
__attribute__ ((visibility ("hidden"))) extern get_has_status_and_pid_t
  get_has_status_and_pid_ptr;

typedef void ( *reset_pid_status_t) ( );
__attribute__ ((visibility ("hidden"))) extern reset_pid_status_t
  reset_pid_status_ptr;

typedef void ( *set_singlestep_waited_on_t) ( pid_t superior,
  pid_t inferior, int value );
__attribute__ ((visibility ("hidden"))) extern set_singlestep_waited_on_t
  set_singlestep_waited_on_ptr;

typedef int ( *get_is_waitpid_local_t ) ();
__attribute__ ((visibility ("hidden"))) extern get_is_waitpid_local_t
  get_is_waitpid_local_ptr;

typedef int ( *get_is_ptrace_local_t ) ();
__attribute__ ((visibility ("hidden"))) extern get_is_ptrace_local_t
  get_is_ptrace_local_ptr;

typedef void ( *unset_is_waitpid_local_t ) ();
__attribute__ ((visibility ("hidden"))) extern unset_is_waitpid_local_t
  unset_is_waitpid_local_ptr;

typedef void ( *unset_is_ptrace_local_t ) ();
__attribute__ ((visibility ("hidden"))) extern unset_is_ptrace_local_t
  unset_is_ptrace_local_ptr;

typedef void ( *t_mtcp_init_thread_local ) ();
__attribute__ ((visibility ("hidden"))) extern t_mtcp_init_thread_local
  mtcp_init_thread_local; 

__attribute__ ((visibility ("hidden"))) extern sigset_t signals_set;

void ptrace_info_list_insert (pid_t superior, pid_t inferior, int last_command,
                              int singlestep_waited_on, char inferior_st,
                              int file_option);

char procfs_state(int tid);

extern "C" struct ptrace_info get_next_ptrace_info(int index);

extern "C" void ptrace_info_list_command(struct cmd_info cmd);

#define MTCP_DEFAULT_SIGNAL SIGUSR2
#endif
#endif
