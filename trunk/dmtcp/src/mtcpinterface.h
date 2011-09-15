/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#ifndef DMTCPMTCPINTERFACE_H
#define DMTCPMTCPINTERFACE_H

#include <sys/types.h>
#include <pthread.h>
#include "constants.h"

namespace dmtcp
{
  void __attribute__ ((weak)) initializeMtcpEngine();
  void killCkpthread();

  void shutdownMtcpEngineOnFork();

  //these next two are defined in dmtcpawareapi.cpp
  void userHookTrampoline_preCkpt();
  void userHookTrampoline_postCkpt(bool isRestart);
}

extern "C"
{
  typedef void (*mtcp_set_callbacks_t)
    (void (*sleep_between_ckpt)(int sec),
     void (*pre_ckpt)(char ** ckptFilename),
     void (*post_ckpt)(int isRestarting,
                       char* mtcpRestoreArgvStartAddr),
     int  (*should_ckpt_fd ) ( int fd ),
     void (*write_ckpt_prefix ) ( int fd ),
     void (*restore_virtual_pid_table) (),
     void (*pre_suspend_user_thread)(),
     void (*pre_resume_user_thread)(int is_ckpt, int is_restart),
     void (*send_stop_signal)(pid_t tid, pid_t original_tid,
                              int *retry_signalling, int *retval),
     void (*ckpt_thread_start)());

  typedef int  (*mtcp_init_dmtcp_info_t)(int pid_virtualization_enabled,
                                         int stderr_fd,
                                         int jassertlog_fd,
                                         int restore_working_directory,
                                         void *clone_fnptr,
                                         void *sigaction_fnptr,
                                         void *malloc_fnptr,
                                         void *free_fnptr);

  typedef int  (*mtcp_init_t) (char const *checkpointFilename,
                               int interval,
                               int clonenabledefault);
  typedef int  (*mtcp_ok_t)(void);
  typedef void (*mtcp_kill_ckpthread_t)(void);
  typedef void (*mtcp_fill_in_pthread_id_t)(pid_t tid, pthread_t pthread_id);
  typedef int  (*mtcp_clone_t)(int (*)(void*), void*, int, void*, int*,
                               struct user_desc*, int*);
  typedef void (*mtcp_process_pthread_join_t)(pthread_t);

  typedef struct MtcpFuncPtrs {
    mtcp_set_callbacks_t        set_callbacks;
    mtcp_init_dmtcp_info_t      init_dmtcp_info;
    mtcp_init_t                 init;
    mtcp_ok_t                   ok;
    mtcp_clone_t                clone;
    mtcp_kill_ckpthread_t       kill_ckpthread;
    mtcp_fill_in_pthread_id_t   fill_in_pthread_id;
    mtcp_process_pthread_join_t process_pthread_join;
  } MtcpFuncPtrs_t;
}
LIB_PRIVATE void* get_mtcp_symbol ( const char* name );

#endif
