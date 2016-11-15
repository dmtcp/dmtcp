/*****************************************************************************
 *   Copyright (C) 2008-2013 Ana-Maria Visan, Kapil Arya, and Gene Cooperman *
 *   amvisan@cs.neu.edu, kapil@cs.neu.edu, and gene@ccs.neu.edu              *
 *                                                                           *
 *  This file is part of the PTRACE plugin of DMTCP (DMTCP:plugin/ptrace).   *
 *                                                                           *
 *  DMTCP:mtcp is free software: you can redistribute it and/or              *
 *  modify it under the terms of the GNU Lesser General Public License as    *
 *  published by the Free Software Foundation, either version 3 of the       *
 *  License, or (at your option) any later version.                          *
 *                                                                           *
 *  DMTCP:plugin/ptrace is distributed in the hope that it will be useful,   *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *  GNU Lesser General Public License for more details.                      *
 *                                                                           *
 *  You should have received a copy of the GNU Lesser General Public         *
 *  License along with DMTCP:dmtcp/src.  If not, see                         *
 *  <http://www.gnu.org/licenses/>.                                          *
 *****************************************************************************/

#ifndef PTRACE_H
#define PTRACE_H

#include "dmtcp.h"

#ifndef EXTERNC
# ifdef __cplusplus
#  define EXTERNC extern "C"
# else // ifdef __cplusplus
#  define EXTERNC
# endif // ifdef __cplusplus
#endif // ifndef EXTERNC

#define LIB_PRIVATE  __attribute__((visibility("hidden")))

#define _real_wait4  NEXT_FNC(wait4)
#define _real_open   NEXT_FNC(open)
#define _real_lseek  NEXT_FNC(lseek)
#define _real_unlink NEXT_FNC(unlink)
#define _real_dup2   NEXT_FNC(dup2)
#define _real_mmap   NEXT_FNC(mmap)

#define _real_ptrace(request, pid, addr, data) \
  NEXT_FNC(ptrace)((enum __ptrace_request)request, pid, addr, data)

#define GETTID()              (int)syscall(SYS_gettid)
#define TGKILL(pid, tid, sig) (int)syscall(SYS_tgkill, pid, tid, sig)

typedef enum PtraceProcState {
  PTRACE_PROC_INVALID = -1,
  PTRACE_PROC_UNDEFINED = 'u',
  PTRACE_PROC_STOPPED = 'T',
  PTRACE_PROC_TRACING_STOP = 'P',
  PTRACE_PROC_RUNNING = 'R',
  PTRACE_PROC_SLEEPING = 'S'
} PtraceProcState;

pid_t ptrace_ckpt_thread_tid();
void ptrace_process_pre_suspend_user_thread();
void ptrace_process_thread_creation(pid_t clone_id);
void ptrace_process_resume_user_thread(int isRestart);
#endif // ifndef PTRACE_H
