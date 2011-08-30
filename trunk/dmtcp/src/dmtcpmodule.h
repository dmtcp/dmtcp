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

#ifndef DMTCPMODULE_H
#define DMTCPMODULE_H

#include <sys/types.h>

#ifndef EXTERNC
# ifdef __cplusplus
#  define EXTERNC extern "C"
# else
#  define EXTERNC
# endif
#endif

typedef enum eDmtcpEvent {
  //DMTCP_EVENT_WRAPPER_INIT, // Future Work :-).
  DMTCP_EVENT_INIT,
  DMTCP_EVENT_PRE_EXIT,
  DMTCP_EVENT_RESET_ON_FORK,
  DMTCP_EVENT_POST_SUSPEND,
  DMTCP_EVENT_POST_LEADER_ELECTION,
  DMTCP_EVENT_POST_DRAIN,
  DMTCP_EVENT_PRE_CHECKPOINT,
  DMTCP_EVENT_POST_CHECKPOINT,
  DMTCP_EVENT_REGISTER_NAME_SERVICE_DATA,
  DMTCP_EVENT_SEND_QUERIES,
  DMTCP_EVENT_POST_CHECKPOINT_RESUME,
  DMTCP_EVENT_POST_RESTART,
  DMTCP_EVENT_POST_RESTART_RESUME,
  DMTCP_EVENT_THREAD_START,
  DMTCP_EVENT_THREAD_EXIT,
  nDmtcpEvents
} DmtcpEvent_t;

EXTERNC void dmtcp_module_disable_ckpt(void);
EXTERNC void dmtcp_module_enable_ckpt(void);
EXTERNC void dmtcp_process_event(DmtcpEvent_t event, void* data);
EXTERNC int dmtcp_send_key_val_pair_to_coordinator(const void *key,
                                                   size_t key_len,
                                                   const void *val,
                                                   size_t val_len);
EXTERNC int dmtcp_send_query_to_coordinator(const void *key, size_t key_len,
                                            void *val, size_t *val_len);

EXTERNC int  dmtcp_get_ckpt_signal();
EXTERNC const char* dmtcp_get_tmpdir();
EXTERNC const char* dmtcp_get_uniquepid_str();
EXTERNC int  dmtcp_is_running_state();
EXTERNC int  dmtcp_is_protected_fd(int fd);

EXTERNC int dmtcp_get_readlog_fd();
EXTERNC void *dmtcp_get_real_dlsym_addr();

#define DMTCP_CALL_NEXT_PROCESS_DMTCP_EVENT(event, data)                \
  do {                                                                  \
    typedef void (*fnptr_t) (DmtcpEvent_t, void*);                      \
    static fnptr_t fn = NULL;                                           \
    static bool fn_initialized = false;                                 \
    if (!fn_initialized) {                                              \
      fn = (fnptr_t) _real_dlsym(RTLD_NEXT, "process_dmtcp_event");     \
      fn_initialized = true;                                            \
    } else if (fn != NULL) {                                            \
      (*fn) (event, data);                                              \
    }                                                                   \
  } while (0)

#endif
