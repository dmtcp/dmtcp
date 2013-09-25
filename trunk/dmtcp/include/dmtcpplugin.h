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

#if __GLIBC_PREREQ(2,5)
# define READLINK_RET_TYPE ssize_t
#else
# define READLINK_RET_TYPE int
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
  long  _hostid; //gethostid()
  pid_t _pid; //getpid()
  time_t _time; //time()
  int _generation; //generation()
} DmtcpUniqueProcessId;

EXTERNC int dmtcp_unique_pids_equal(DmtcpUniqueProcessId a,
                                    DmtcpUniqueProcessId b);
EXTERNC int dmtcp_plugin_disable_ckpt(void);
EXTERNC void dmtcp_plugin_enable_ckpt(void);
EXTERNC void dmtcp_process_event(DmtcpEvent_t event, DmtcpEventData_t *data);
EXTERNC int dmtcp_send_key_val_pair_to_coordinator(const void *key,
                                                   size_t key_len,
                                                   const void *val,
                                                   size_t val_len);
EXTERNC int dmtcp_send_query_to_coordinator(const void *key, size_t key_len,
                                            void *val, size_t *val_len);
EXTERNC int dmtcp_get_host_ipv4(struct in_addr *in);

EXTERNC const char* dmtcp_get_tmpdir();
EXTERNC void dmtcp_set_tmpdir(const char *);

EXTERNC const char* dmtcp_get_ckpt_dir();
EXTERNC void dmtcp_set_ckpt_dir(const char *);
EXTERNC const char* dmtcp_get_ckpt_files_subdir();
EXTERNC int dmtcp_should_ckpt_open_files();

EXTERNC int  dmtcp_get_ckpt_signal();
EXTERNC const char* dmtcp_get_uniquepid_str();
EXTERNC const char* dmtcp_get_computation_id_str();
EXTERNC time_t dmtcp_get_coordinator_timestamp();
EXTERNC int  dmtcp_get_generation();
EXTERNC int  dmtcp_is_running_state();
EXTERNC int  dmtcp_is_initializing_wrappers();
EXTERNC int  dmtcp_is_protected_fd(int fd);
EXTERNC int dmtcp_no_coordinator();
EXTERNC DmtcpUniqueProcessId dmtcp_get_uniquepid();
EXTERNC DmtcpUniqueProcessId dmtcp_get_coord_id();
EXTERNC const char* dmtcp_get_executable_path();

EXTERNC int dmtcp_get_ptrace_fd();
EXTERNC int dmtcp_get_readlog_fd();
EXTERNC void dmtcp_block_ckpt_signal();
EXTERNC void dmtcp_unblock_ckpt_signal();

EXTERNC void *dmtcp_get_libc_dlsym_addr();
EXTERNC void dmtcp_close_protected_fd(int fd);

EXTERNC pid_t dmtcp_real_to_virtual_pid(pid_t realPid) __attribute((weak));
EXTERNC pid_t dmtcp_virtual_to_real_pid(pid_t virtualPid) __attribute((weak));

EXTERNC int dmtcp_is_bq_file(const char *path)
  __attribute((weak));
EXTERNC int dmtcp_bq_should_ckpt_file(const char *path, int *type)
  __attribute((weak));
EXTERNC int dmtcp_bq_restore_file(const char *path, const char *savedFilePath,
                                  int fcntlFlags, int type)
  __attribute((weak));

#define DMTCP_PLUGIN_DISABLE_CKPT DMTCP_DISABLE_CKPT
#define DMTCP_PLUGIN_ENABLE_CKPT  DMTCP_ENABLE_CKPT

#define DMTCP_DISABLE_CKPT() \
  bool __dmtcp_plugin_ckpt_disabled = dmtcp_plugin_disable_ckpt()

#define DMTCP_ENABLE_CKPT() \
  if (__dmtcp_plugin_ckpt_disabled) dmtcp_plugin_enable_ckpt()


#define NEXT_FNC(func)                                                      \
  ({                                                                        \
     static __typeof__(&func) _real_##func = (__typeof__(&func)) -1;        \
     if ((void*) _real_##func == (void*) -1) {                              \
       __typeof__(&dlsym) dlsym_fnptr;                                      \
       dlsym_fnptr = (__typeof__(&dlsym)) dmtcp_get_libc_dlsym_addr();      \
       _real_##func = (__typeof__(&func)) (*dlsym_fnptr) (RTLD_NEXT, #func);\
     }                                                                      \
   _real_##func;})

#define NEXT_DMTCP_PROCESS_EVENT DMTCP_CALL_NEXT_PROCESS_DMTCP_EVENT

#define DMTCP_CALL_NEXT_PROCESS_DMTCP_EVENT(event, data)                    \
  do {                                                                      \
    static __typeof__(&dmtcp_process_event) fn                              \
      = (__typeof__(&dmtcp_process_event)) -1;                              \
    if ((void*) fn == (void*) -1) {                                         \
      fn = NEXT_FNC(dmtcp_process_event);                                   \
    }                                                                       \
    if (fn != NULL) {                                                       \
      (*fn) (event, data);                                                  \
    }                                                                       \
  } while (0)

#endif
