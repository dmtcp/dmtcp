/*****************************************************************************
 *   Copyright (C) 2006-2010 by Michael Rieker, Jason Ansel, Kapil Arya, and *
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

#ifndef _MTCP_H
#define _MTCP_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MTCP_DEFAULT_SIGNAL SIGUSR2

void mtcp_init_dmtcp_info (int pid_virtualization_enabled,
                           int stderr_fd,
                           int jassertlog_fd,
                           int restore_working_directory,
                           void *clone_fnptr,
                           void *sigaction_fnptr,
                           void *malloc_fnptr,
                           void *free_fnptr);
void mtcp_init (char const *checkpointfilename,
                int interval,
                int clonenabledefault);
int mtcp_wrapper_clone (int (*fn) (void *arg), void *child_stack, int flags, void *arg);
int mtcp_ok (void);
int mtcp_no (void);

__attribute__ ((visibility ("hidden"))) void * mtcp_safemmap (void *start, size_t length, int prot, int flags, int fd, off_t offset);

#ifdef PTRACE

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
#endif

void mtcp_set_callbacks(void (*sleep_between_ckpt)(int sec),
                        void (*pre_ckpt)(),
                        void (*post_ckpt)(int is_restarting,
                                          char* mtcp_restore_argv_start_addr),
                        int  (*ckpt_fd)(int fd),
                        void (*write_ckpt_prefix)(int fd),
                        void (*write_tid_maps)()
#ifdef PTRACE
                      , struct ptrace_info (*get_next_ptrace_info)(int index),
                        void (*ptrace_info_list_command)(struct cmd_info cmd),
                        void (*jalib_ckpt_unlock)(),
                        int (*ptrace_info_list_size)()
#endif
);

#ifdef __cplusplus
}
#endif

#endif
