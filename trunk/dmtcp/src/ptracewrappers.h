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
#include "ptrace.h"
#include "mtcp_ptrace.h"

//enum {
//  PTRACE_INFO_LIST_UPDATE_IS_INFERIOR_CKPTHREAD = 1,
//  PTRACE_INFO_LIST_SORT,
//  PTRACE_INFO_LIST_REMOVE_PAIRS_WITH_DEAD_TIDS,
//  PTRACE_INFO_LIST_SAVE_THREADS_STATE,
//  PTRACE_INFO_LIST_PRINT,
//  PTRACE_INFO_LIST_INSERT,
//  PTRACE_INFO_LIST_UPDATE_INFO
//};

#define MTCP_DEFAULT_SIGNAL SIGUSR2

/* Must match the structure declaration in mtcp/mtcp.h, without the operator
 * overloading feature. */
//struct ptrace_info {
//  pid_t superior;
//  pid_t inferior;
//  char inferior_st;
//  int inferior_is_ckpthread;
//  int last_command;
//  int singlestep_waited_on;
//
//};

static inline bool operator==(const struct ptrace_info& a, const struct ptrace_info& b) {
  return b.superior == a.superior && b.inferior == a.inferior;
}

static inline bool operator!= (const struct ptrace_info& a, const struct ptrace_info& b) {
  return b.superior != a.superior || b.inferior != a.inferior;
}

static const struct ptrace_info EMPTY_PTRACE_INFO = {0, 0, 0, 0, 0, 0};

static const struct cmd_info EMPTY_CMD_INFO = {0, 0, 0, 0, 0, 0, 0};

//extern dmtcp::list<struct ptrace_info> ptrace_info_list;

#if 0
__attribute__ ((visibility ("hidden"))) extern t_mtcp_get_ptrace_waitpid_info
  mtcp_get_ptrace_waitpid_info;

__attribute__ ((visibility ("hidden"))) extern t_mtcp_init_thread_local
  mtcp_init_thread_local;

__attribute__ ((visibility ("hidden"))) extern t_mtcp_ptracing
  mtcp_ptracing;

__attribute__ ((visibility ("hidden"))) extern sigset_t signals_set;
#endif

void ptrace_info_list_insert (pid_t superior, pid_t inferior, int last_command,
  int singlestep_waited_on, char inferior_st, int file_option);

char procfs_state(int tid);

extern "C" struct ptrace_info get_next_ptrace_info(int index);

extern "C" void ptrace_info_list_command(struct cmd_info cmd);

extern "C" int ptrace_info_list_size();

extern "C" void ptrace_info_list_update_info(pid_t superior, pid_t inferior,
  int singlestep_waited_on);

#endif
#endif
