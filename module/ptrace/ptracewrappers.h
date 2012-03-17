/*****************************************************************************
 *   Copyright (C) 2008-2012 by Ana-Maria Visan, Kapil Arya, and             *
 *                                                            Gene Cooperman *
 *   amvisan@cs.neu.edu, kapil@cs.neu.edu, and gene@ccs.neu.edu              *
 *                                                                           *
 *   This file is part of the dmtcp/src module of DMTCP (DMTCP:dmtcp/src).   *
 *                                                                           *
 *  DMTCP:dmtcp/src is free software: you can redistribute it and/or         *
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

#ifndef PTRACEWRAPPERS_H
#define PTRACEWRAPPERS_H

#include <unistd.h>
#include <signal.h>
#include <sched.h>
#include <asm/ldt.h>
#include "ptrace.h"
#include "mtcp_ptrace.h"

#ifdef __cplusplus
static inline bool operator==(const struct ptrace_info& a, const struct ptrace_info& b) {
  return b.superior == a.superior && b.inferior == a.inferior;
}

static inline bool operator!= (const struct ptrace_info& a, const struct ptrace_info& b) {
  return b.superior != a.superior || b.inferior != a.inferior;
}
#endif

static const struct cmd_info EMPTY_CMD_INFO = {0, 0, 0, 0, 0, 0, 0};

EXTERNC void ptrace_info_list_insert (pid_t superior, pid_t inferior,
                                      int last_command, int singlestep_waited_on,
                                      char inferior_st, int file_option);

EXTERNC void ptrace_info_list_insert (pid_t superior, pid_t inferior,
                                      int last_command, int singlestep_waited_on,
                                      char inferior_st, int file_option);

EXTERNC char procfs_state(int tid);

EXTERNC struct ptrace_info *get_next_ptrace_info(int index);

EXTERNC void ptrace_info_list_command(struct cmd_info cmd);

EXTERNC int ptrace_info_list_size();

EXTERNC void ptrace_info_list_update_info(pid_t superior, pid_t inferior,
                                          int singlestep_waited_on);

EXTERNC void ptrace_info_list_set_attach_state(pid_t superior, pid_t inferior,
                                               int attach_state);

EXTERNC long _real_ptrace(enum __ptrace_request request, pid_t pid, void *addr,
                          void *data);
EXTERNC pid_t _real_waitpid(pid_t pid, int *stat_loc, int options);

EXTERNC int _real_clone(int (*fn) (void *arg), void *child_stack, int flags,
                        void *arg, int *parent_tidptr, struct user_desc *newtls,
                        int *child_tidptr);

EXTERNC void ptrace_init_data_structures();
#endif
