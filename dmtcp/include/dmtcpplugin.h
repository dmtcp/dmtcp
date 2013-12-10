/****************************************************************************
 * TODO: Replace this header with appropriate header showing MIT OR BSD     *
 *       License                                                            *
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 * This file, dmtcpplugin.h, is placed in the public domain.                *
 * The motivation for this is to allow anybody to freely use this file      *
 * without restriction to statically link this file with any software.      *
 * This allows that software to communicate with the DMTCP libraries.       *
 * -  Jason Ansel, Kapil Arya, and Gene Cooperman                           *
 *      jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu           *
 ****************************************************************************/

#ifndef DMTCPPLUGIN_H
#define DMTCPPLUGIN_H

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/ip.h>

#ifndef __USE_GNU
# define __USE_GNU
#endif

#include <dlfcn.h>  /* for NEXT_FNC() */

#ifndef EXTERNC
# ifdef __cplusplus
#  define EXTERNC extern "C"
# else
#  define EXTERNC
# endif
#endif

#define SYSCALL_ARG_RET_TYPE long int
#define POLL_TIMEOUT_TYPE int
#define EVENTFD_VAL_TYPE int

#define DELETED_FILE_SUFFIX " (deleted)"
#define LIB_PRIVATE __attribute__ ((visibility ("hidden")))

typedef enum eDmtcpEvent {
  //DMTCP_EVENT_WRAPPER_INIT, // Future Work :-).
  DMTCP_EVENT_INIT,
  DMTCP_EVENT_EXIT,

  DMTCP_EVENT_PRE_EXEC,
  DMTCP_EVENT_POST_EXEC,

  DMTCP_EVENT_ATFORK_PREPARE,
  DMTCP_EVENT_ATFORK_PARENT,
  DMTCP_EVENT_ATFORK_CHILD,

  DMTCP_EVENT_WAIT_FOR_SUSPEND_MSG,
  DMTCP_EVENT_THREADS_SUSPEND,
  DMTCP_EVENT_LEADER_ELECTION,
  DMTCP_EVENT_DRAIN,
  DMTCP_EVENT_WRITE_CKPT,

  DMTCP_EVENT_RESTART,
  DMTCP_EVENT_RESUME,
  DMTCP_EVENT_REGISTER_NAME_SERVICE_DATA,
  DMTCP_EVENT_SEND_QUERIES,
  DMTCP_EVENT_REFILL,
  DMTCP_EVENT_THREADS_RESUME,

  DMTCP_EVENT_PRE_SUSPEND_USER_THREAD,
  DMTCP_EVENT_RESUME_USER_THREAD,

  DMTCP_EVENT_THREAD_START,
  DMTCP_EVENT_THREAD_CREATED,

  DMTCP_EVENT_PTHREAD_START,
  DMTCP_EVENT_PTHREAD_EXIT,
  DMTCP_EVENT_PTHREAD_RETURN,

  nDmtcpEvents
} DmtcpEvent_t;

typedef union _DmtcpEventData_t {
  struct {
    int fd;
  } serializerInfo;

  struct {
    int isRestart;
  } resumeUserThreadInfo, refillInfo, resumeInfo, nameserviceInfo;
} DmtcpEventData_t;

typedef struct DmtcpUniqueProcessId {
  uint64_t  _hostid; //gethostid()
  uint64_t _time; //time()
  pid_t _pid; //getpid()
  uint32_t _generation; //generation()
} DmtcpUniqueProcessId;

EXTERNC int dmtcp_unique_pids_equal(DmtcpUniqueProcessId a,
                                    DmtcpUniqueProcessId b);
EXTERNC int dmtcp_plugin_disable_ckpt(void);
EXTERNC void dmtcp_plugin_enable_ckpt(void);
EXTERNC void dmtcp_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
  __attribute((weak));
EXTERNC int dmtcp_send_key_val_pair_to_coordinator(const char *id,
                                                   const void *key,
                                                   uint32_t key_len,
                                                   const void *val,
                                                   uint32_t val_len);
EXTERNC int dmtcp_send_query_to_coordinator(const char *id,
                                            const void *key, uint32_t key_len,
                                            void *val, uint32_t *val_len);
EXTERNC void dmtcp_get_local_ip_addr(struct in_addr *in);

EXTERNC const char* dmtcp_get_tmpdir(void);
EXTERNC void dmtcp_set_tmpdir(const char *);

EXTERNC const char* dmtcp_get_ckpt_dir(void);
EXTERNC void dmtcp_set_ckpt_dir(const char *);
EXTERNC void dmtcp_set_coord_ckpt_dir(const char* dir);
EXTERNC const char* dmtcp_get_ckpt_files_subdir(void);
EXTERNC int dmtcp_should_ckpt_open_files(void);

EXTERNC int dmtcp_get_ckpt_signal(void);
EXTERNC const char* dmtcp_get_uniquepid_str(void);
EXTERNC const char* dmtcp_get_computation_id_str(void);
EXTERNC uint64_t dmtcp_get_coordinator_timestamp(void);
EXTERNC uint32_t dmtcp_get_generation(void);
EXTERNC int dmtcp_is_running_state(void);
EXTERNC int dmtcp_is_initializing_wrappers(void);
EXTERNC int dmtcp_is_protected_fd(int fd);
EXTERNC int dmtcp_get_restart_env(char *key, char *value, int maxvaluelen);
EXTERNC int dmtcp_no_coordinator(void);
EXTERNC DmtcpUniqueProcessId dmtcp_get_uniquepid();
EXTERNC DmtcpUniqueProcessId dmtcp_get_coord_id();
EXTERNC const char* dmtcp_get_executable_path();

EXTERNC int dmtcp_get_ptrace_fd(void);
EXTERNC int dmtcp_get_readlog_fd(void);
EXTERNC void dmtcp_block_ckpt_signal(void);
EXTERNC void dmtcp_unblock_ckpt_signal(void);

EXTERNC void *dmtcp_get_libc_dlsym_addr(void);
EXTERNC void dmtcp_close_protected_fd(int fd);
EXTERNC int dmtcp_protected_environ_fd(void);

EXTERNC pid_t dmtcp_real_to_virtual_pid(pid_t realPid) __attribute((weak));
EXTERNC pid_t dmtcp_virtual_to_real_pid(pid_t virtualPid) __attribute((weak));

EXTERNC int dmtcp_is_bq_file(const char *path)
  __attribute((weak));
EXTERNC int dmtcp_bq_should_ckpt_file(const char *path, int *type)
  __attribute((weak));
EXTERNC int dmtcp_bq_restore_file(const char *path, const char *savedFilePath,
                                  int fcntlFlags, int type)
  __attribute((weak));

EXTERNC int dmtcp_must_ckpt_file(const char *path)
  __attribute((weak));
EXTERNC void dmtcp_get_new_file_path(const char *abspath, const char *cwd,
                                     char *newpath)
  __attribute((weak));


EXTERNC void dmtcp_prepare_wrappers(void) __attribute((weak));

#define dmtcp_process_event(e,d) \
    __REPLACE_dmtcp_process_event_WITH_dmtcp_event_hook()__

#define DMTCP_PLUGIN_DISABLE_CKPT DMTCP_DISABLE_CKPT
#define DMTCP_PLUGIN_ENABLE_CKPT  DMTCP_ENABLE_CKPT

#define DMTCP_DISABLE_CKPT() \
  bool __dmtcp_plugin_ckpt_disabled = dmtcp_plugin_disable_ckpt()

#define DMTCP_ENABLE_CKPT() \
  if (__dmtcp_plugin_ckpt_disabled) dmtcp_plugin_enable_ckpt()


#define NEXT_FNC(func)                                                      \
  ({                                                                        \
     static __typeof__(&func) _real_##func = (__typeof__(&func)) -1;        \
     if (_real_##func == (__typeof__(&func)) -1) {                          \
       __typeof__(&dlsym) dlsym_fnptr;                                      \
       if (dmtcp_prepare_wrappers) dmtcp_prepare_wrappers();                \
       dlsym_fnptr = (__typeof__(&dlsym)) dmtcp_get_libc_dlsym_addr();      \
       _real_##func = (__typeof__(&func)) (*dlsym_fnptr) (RTLD_NEXT, #func);\
     }                                                                      \
   _real_##func;})

#define DMTCP_NEXT_EVENT_HOOK(event, data)                                  \
  do {                                                                      \
    static __typeof__(&dmtcp_event_hook) fn                                 \
      = (__typeof__(&dmtcp_event_hook)) -1;                                 \
    if ((void*) fn == (void*) -1) {                                         \
      fn = NEXT_FNC(dmtcp_event_hook);                                      \
    }                                                                       \
    if (fn != NULL) {                                                       \
      (*fn) (event, data);                                                  \
    }                                                                       \
  } while (0)

// dmtcpaware.h and this part of dmtcpplugin.h are mutually exclusive
#ifndef DMTCPAWARE_H
//===================================================================
// DMTCP utilities

#ifndef DMTCP_AFTER_CHECKPOINT
  // Return value of dmtcpCheckpoint
# define DMTCP_AFTER_CHECKPOINT 1
  // Return value of dmtcpCheckpoint
# define DMTCP_AFTER_RESTART    2
#endif
#ifndef DMTCP_NOT_PRESENT
# define DMTCP_NOT_PRESENT 3
#endif

// C++ takes null arg, while C takes void arg.
#ifdef __cplusplus
# define VOID
#else
# define VOID void
#endif

//FIXME:
// If a plugin is not compiled with defined(__PIC__) and we can verify
// that we're using DMTCP (environment variables), and dmtcpIsEnabled
// or dmtcpCheckpoint expands to 0, then we should print a warning
// at run-time.

// These utility functions require compiling the target app with -fPIC

int __attribute__ ((weak)) dmtcpIsEnabled(VOID);
#define dmtcpIsEnabled() (dmtcpIsEnabled ? dmtcpIsEnabled() : 0)

int __attribute__ ((weak)) dmtcpCheckpoint(VOID);
#define dmtcpCheckpoint() \
  (dmtcpCheckpoint ? dmtcpCheckpoint() : DMTCP_NOT_PRESENT)

int __attribute__ ((weak)) dmtcpDelayCheckpointsLock(VOID);
#define dmtcpDelayCheckpointsLock() \
 (dmtcpDelayCheckpointsLock ? dmtcpDelayCheckpointsLock() : DMTCP_NOT_PRESENT)

int __attribute__ ((weak)) dmtcpDelayCheckpointsUnlock(VOID);
#define dmtcpDelayCheckpointsUnlock() \
  (dmtcpDelayCheckpointsUnlock ? dmtcpDelayCheckpointsUnlock() : \
   DMTCP_NOT_PRESENT)

#endif
#endif
