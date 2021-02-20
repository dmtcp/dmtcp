/****************************************************************************
 * TODO: Replace this header with appropriate header showing MIT OR BSD     *
 *       License                                                            *
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 * This file, dmtcp.h, is placed in the public domain.                *
 * The motivation for this is to allow anybody to freely use this file      *
 * without restriction to statically link this file with any software.      *
 * This allows that software to communicate with the DMTCP libraries.       *
 * -  Jason Ansel, Kapil Arya, and Gene Cooperman                           *
 *      jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu           *
 ****************************************************************************/

#ifndef DMTCP_H
#define DMTCP_H

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/ip.h>

#ifndef __USE_GNU
# define __USE_GNU_NOT_SET
# define __USE_GNU
#endif
#include <dlfcn.h>  /* for NEXT_FNC() */
#ifdef __USE_GNU_NOT_SET
# undef __USE_GNU_NOT_SET
# undef __USE_GNU
#endif

#ifndef EXTERNC
# ifdef __cplusplus
#  define EXTERNC extern "C"
# else
#  define EXTERNC
# endif
#endif

// C++ takes null arg, while C takes void arg.
#ifdef __cplusplus
# define DMTCP_VOID
#else
# define DMTCP_VOID void
#endif

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
  DMTCP_EVENT_PRE_CKPT_NAME_SERVICE_DATA_REGISTER,
  DMTCP_EVENT_PRE_CKPT_NAME_SERVICE_DATA_QUERY,
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
  uint32_t _computation_generation; //computationGeneration()
} DmtcpUniqueProcessId;

EXTERNC int dmtcp_unique_pids_equal(DmtcpUniqueProcessId a,
                                    DmtcpUniqueProcessId b);

//FIXME:
// If a plugin is not compiled with defined(__PIC__) and we can verify
// that we're using DMTCP (environment variables), and dmtcp_is_enabled
// or dmtcp_checkpoint expands to 0, then we should print a warning
// at run-time.

// These utility functions require compiling the target app with -fPIC

/**
 * Returns 1 if executing under dmtcp_launch, 0 otherwise
 * See: test/plugin/applic-initiated-ckpt and applic-delayed-ckpt
 *      directories for exammples:
 */
EXTERNC int dmtcp_is_enabled(DMTCP_VOID) __attribute ((weak));
#define dmtcp_is_enabled() (dmtcp_is_enabled ? dmtcp_is_enabled() : 0)

/**
 * Checkpoint the entire distributed computation
 *   (Does not necessarily block until checkpoint is complete.
 *    Use dmtcp_get_generation() to test if checkpoint is complete.)
 * NOTE:  This macro is blocking.  dmtcp_checkpoint() will not return
 *        until a checkpoint is taken.  This guarantees that the
 *        current _thread_ blocks until the current process has been
 *        checkpointed.  It guarantees nothing about other threads or
 *        other processes.
 * + returns DMTCP_AFTER_CHECKPOINT if the checkpoint succeeded.
 * + returns DMTCP_AFTER_RESTART    after a restart.
 * + returns <=0 on error.
 * See: test/plugin/applic-initiated-ckpt directory for an exammple:
 */
EXTERNC int dmtcp_checkpoint(DMTCP_VOID) __attribute__ ((weak));
#define dmtcp_checkpoint() \
  (dmtcp_checkpoint ? dmtcp_checkpoint() : DMTCP_NOT_PRESENT)

/**
 * Prevent a checkpoint from starting until dmtcp_enable_checkpoint() is
 * called.
 * + Has (recursive) lock semantics, only one thread may acquire it at time.
 * + Only prevents checkpoints locally, remote processes may be suspended.
 *   Thus, send or recv to another checkpointed process may create deadlock.
 * + Returns 1 on success, <=0 on error
 * See: test/plugin/applic-delayed-ckpt directory for an exammple:
 */
EXTERNC int dmtcp_disable_ckpt(DMTCP_VOID) __attribute__ ((weak));
#define dmtcp_disable_ckpt() \
 (dmtcp_disable_ckpt ? dmtcp_disable_ckpt() : DMTCP_NOT_PRESENT)

/**
 * Re-allow checkpoints, opposite of dmtcp_disable_ckpt().
 * + Returns 1 on success, <=0 on error
 * See: test/plugin/applic-delayed-ckpt directory for an exammple:
 */
EXTERNC int dmtcp_enable_ckpt(DMTCP_VOID) __attribute__ ((weak));
#define dmtcp_enable_ckpt() \
 (dmtcp_enable_ckpt ? dmtcp_enable_ckpt() : DMTCP_NOT_PRESENT)

// See: test/plugin/sleep1 dir and sibling directories for examples:
EXTERNC void dmtcp_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
  __attribute((weak));

// See: test/plugin/example-db dir for an example:
EXTERNC int dmtcp_send_key_val_pair_to_coordinator(const char *id,
                                                   const void *key,
                                                   uint32_t key_len,
                                                   const void *val,
                                                   uint32_t val_len);
EXTERNC int dmtcp_send_key_val_pair_to_coordinator_sync(const char *id,
                                                        const void *key,
                                                        uint32_t key_len,
                                                        const void *val,
                                                        uint32_t val_len);
EXTERNC int dmtcp_send_query_to_coordinator(const char *id,
                                            const void *key, uint32_t key_len,
                                            void *val, uint32_t *val_len);
/*
 * This API can be used to create a new NS database, generate a unique
 * id, populate the database with the unique id, and return the generated
 * unique id. This allows one to use the API first at launch time to get
 * unique ids, and then use the regular NS API later for querying and
 * publishing.
 *
 * To use this API, one needs to specify the following parameters:
 *  - a nameservice database name;
 *  - a key, which could be a host-id (if you wanted per-host unique ids,
 *    e.g., lids for IB), or a pid (if you wanted per process unique ids,
 *    e.g., QP numbers for IB);
 *  - key_len, which is the length of the key (in bytes);
 *  - val, which is the return value, (in other words, the unique id
 *    generated by the coordinator);
 *  - offset, which determines the difference between two ids
 *   (e.g., a difference of 10 between two lids); and
 *  - val_len, which is the max. length in bytes for the unique id
 *    (e.g., 2 bytes for a lid).
 */
EXTERNC int dmtcp_get_unique_id_from_coordinator(const char *id,    // DB name
                                                 const void *key,   // Key: can be hostid, pid, etc.
                                                 uint32_t key_len,  // Length of the key
                                                 void *val,         // Result
                                                 uint32_t offset,   // unique id offset
                                                 uint32_t val_len); // Expected value length

EXTERNC void dmtcp_get_local_ip_addr(struct in_addr *in);

EXTERNC const char* dmtcp_get_tmpdir(void);
//EXTERNC void dmtcp_set_tmpdir(const char *);

EXTERNC const char* dmtcp_get_ckpt_dir(void) __attribute ((weak));
#define dmtcp_get_ckpt_dir() \
 (dmtcp_get_ckpt_dir ? dmtcp_get_ckpt_dir() : NULL)
EXTERNC int dmtcp_set_ckpt_dir(const char *) __attribute ((weak));
#define dmtcp_set_ckpt_dir(d) \
 (dmtcp_set_ckpt_dir ? dmtcp_set_ckpt_dir(d) : DMTCP_NOT_PRESENT)

// One peer can request a change to a global ckpt dir while another requests
// a checkpoint. If a peer can send a global ckpt dir request before being
// suspended, then the coordinator will recognize this request, and pass on
// to all peers the new global ckpt dir. Upon restart, all peers will remember
// the current ckpt dir (whether the original local one or a change to a
// global one).
EXTERNC int dmtcp_set_global_ckpt_dir(const char *) __attribute__ ((weak));
#define dmtcp_set_global_ckpt_dir(d) \
 (dmtcp_set_global_ckpt_dir ? dmtcp_set_global_ckpt_dir(d) : DMTCP_NOT_PRESENT)
EXTERNC const char* dmtcp_get_coord_ckpt_dir(void) __attribute__ ((weak));
#define dmtcp_get_coord_ckpt_dir() \
 (dmtcp_get_coord_ckpt_dir ? dmtcp_get_coord_ckpt_dir() : NULL)
EXTERNC int dmtcp_set_coord_ckpt_dir(const char* dir) __attribute__ ((weak));
#define dmtcp_set_coord_ckpt_dir(d) \
 (dmtcp_set_coord_ckpt_dir ? dmtcp_set_coord_ckpt_dir(d) : DMTCP_NOT_PRESENT)
EXTERNC const char* dmtcp_get_ckpt_filename(void) __attribute__((weak));
EXTERNC const char* dmtcp_get_ckpt_files_subdir(void);
EXTERNC int dmtcp_should_ckpt_open_files(void);
EXTERNC int dmtcp_allow_overwrite_with_ckpted_files(void);

EXTERNC int dmtcp_get_ckpt_signal(void);
EXTERNC const char* dmtcp_get_uniquepid_str(void) __attribute__((weak));

/*
 * ComputationID
 *   ComputationID of a computation is the unique-pid of the first process of
 *   the computation. Even if that process dies, the rest of the computation
 *   retains the same computation ID.
 *
 *   With --enable-unique-checkpoint-filenames, the ComputationID also includes
 *   the checkpoint generation number (starting from 1 for the first
 *   checkpoint).  This number is the same for the entire computation at a
 *   given point in time.  Dmtcp coordinator increments this number prior
 *   to sending the SUSPEND message, and it is sent to the workers as a part
 *   of the SUSPEND message.
 */
EXTERNC const char* dmtcp_get_computation_id_str(void);
EXTERNC uint64_t dmtcp_get_coordinator_timestamp(void);
// Generation is 0 before first checkpoint, and then successively incremented.
EXTERNC uint32_t dmtcp_get_generation(void) __attribute__((weak));
EXTERNC int checkpoint_is_pending(void) __attribute__((weak));

/**
 * Gets the coordinator-specific status of DMTCP.
 * - Returns DMTCP_IS_PRESENT if running under DMTCP and DMTCP_NOT_PRESENT
 *   otherwise.
 * - Side effects: modifies the arguments
 *
 * Args:
 *   numPeers: Number of processes connected to dmtcp_coordinator
 *   isRunning: 1 if all processes connected to dmtcp_coordinator are in a
 *              running state
 */
EXTERNC int dmtcp_get_coordinator_status(int *numPeers, int *isRunning)
  __attribute__((weak));
#define dmtcp_get_coordinator_status(p,r) \
  (dmtcp_get_coordinator_status ? dmtcp_get_coordinator_status(p,r) \
                             : DMTCP_NOT_PRESENT)

/**
 * Queries local state of this process, not global state seen by DMTCP coord.
 * - Returns DMTCP_IS_PRESENT if running under DMTCP and DMTCP_NOT_PRESENT
 *   otherwise.
 * - Side effects: modifies the arguments
 *
 * Args:
 *   numCheckpoints: The number of times this process has been checkpointed
 *                   (excludes restarts)
 *   numRestarts: The number of times this process has been restarted
 */
EXTERNC int dmtcp_get_local_status(int *numCheckpoints, int *numRestarts)
  __attribute__((weak));
#define dmtcp_get_local_status(c,r) \
  (dmtcp_get_local_status ? dmtcp_get_local_status(c,r) : DMTCP_NOT_PRESENT)

// Is DMTCP in the running state?
//   (e.g., not in pre-ckpt, post-ckpt, post-restart event)?
EXTERNC int dmtcp_is_running_state(void);
// Primarily for use by the modify-env plugin.
EXTERNC int dmtcp_get_restart_env(const char *name,
                                  char *value, size_t maxvaluelen);
// Get pathname of target executable under DMTCP control.
EXTERNC const char* dmtcp_get_executable_path();
// True if dmtcp_launch called with --no-coordinator
EXTERNC int dmtcp_no_coordinator(void);
/* If your plugin invokes wrapper functions before DMTCP is initialized,
 *   then call this prior to your first wrapper function call.
 */
EXTERNC void dmtcp_prepare_wrappers(void) __attribute((weak));

// FOR EXPERTS ONLY:
EXTERNC int dmtcp_is_protected_fd(int fd);
EXTERNC DmtcpUniqueProcessId dmtcp_get_uniquepid();
EXTERNC DmtcpUniqueProcessId dmtcp_get_coord_id();
EXTERNC DmtcpUniqueProcessId dmtcp_get_computation_id();

// FOR EXPERTS ONLY:
EXTERNC int dmtcp_get_ptrace_fd(void);
EXTERNC int dmtcp_get_readlog_fd(void);
EXTERNC void dmtcp_block_ckpt_signal(void);
EXTERNC void dmtcp_unblock_ckpt_signal(void);

// FOR EXPERTS ONLY:
EXTERNC void *dmtcp_get_libc_dlsym_addr(void);
EXTERNC void dmtcp_close_protected_fd(int fd);
EXTERNC int dmtcp_protected_environ_fd(void);

/* FOR EXPERTS ONLY:
 *   The DMTCP internal pid plugin ensures that the application sees only
 *  a virtual pid, which can be translated to the current real pid
 *  assigned to the kernel on a restart.  The pid plugin places wrappers
 *  around all system calls referring to a pid.  If your application
 *  discovers a pid without going through a system call (e.g., through
 *  the proc filesystem), use this to virtualize the pid.
 */
EXTERNC pid_t dmtcp_real_to_virtual_pid(pid_t realPid) __attribute((weak));
EXTERNC pid_t dmtcp_virtual_to_real_pid(pid_t virtualPid) __attribute((weak));

EXTERNC void dmtcp_update_max_required_fd(int fd) __attribute((weak));

// bq_file -> "batch queue file"; used only by batch-queue plugin
EXTERNC int dmtcp_is_bq_file(const char *path)
  __attribute((weak));
EXTERNC int dmtcp_bq_should_ckpt_file(const char *path, int *type)
  __attribute((weak));
EXTERNC int dmtcp_bq_restore_file(const char *path, const char *savedFilePath,
                                  int fcntlFlags, int type)
  __attribute((weak));

/*  These next two functions are defined in contrib/ckptfile/ckptfile.cpp
 *  But they are currently used only in src/plugin/ipc/file/fileconnection.cpp
 *    and in a trivial fashion.  These are intended for future extensions.
 */
EXTERNC int dmtcp_must_ckpt_file(const char *path) __attribute((weak));
EXTERNC void dmtcp_get_new_file_path(const char *abspath, const char *cwd,
                                     char *newpath)
  __attribute((weak));
EXTERNC int dmtcp_must_overwrite_file(const char *path) __attribute((weak));

#define dmtcp_process_event(e,d) \
    __REPLACE_dmtcp_process_event_WITH_dmtcp_event_hook()__

// These are part of the internal implementation of DMTCP plugins
EXTERNC int dmtcp_plugin_disable_ckpt(void);
#define DMTCP_PLUGIN_DISABLE_CKPT() \
  int __dmtcp_plugin_ckpt_disabled = dmtcp_plugin_disable_ckpt()

EXTERNC void dmtcp_plugin_enable_ckpt(void);
#define DMTCP_PLUGIN_ENABLE_CKPT() \
  if (__dmtcp_plugin_ckpt_disabled) dmtcp_plugin_enable_ckpt()

EXTERNC void dmtcp_initialize() __attribute ((weak));

#define NEXT_FNC(func)                                                      \
  ({                                                                        \
     static __typeof__(&func) _real_##func = (__typeof__(&func)) -1;        \
     if (_real_##func == (__typeof__(&func)) -1) {                          \
       if (dmtcp_initialize) dmtcp_initialize();                            \
       __typeof__(&dlsym) dlsym_fnptr;                                      \
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

#if defined(__clang__) && __clang_major__ < 3 || \
    __clang_major__ == 3 && __clang_minor__ <= 4
/***************************************************************************
 * This workaround is required for what is arguably a bug in clang-3.4.1
 *   under Ubuntu 13.10.
 * We don't see a problem with clang-3.4.2 under Ubuntu 14.04.  So, eventually
 *   we can deprecate this patch, when most distros use a later clang.
 * clang-3.4 declares fn and dmtcp_event_hook as weak symbols ("V")
 *   when these variables are declared inside the function dmtcp_event_hook().
 *   This workaround declares them outside of dmtcp_event_hook().
 *   If the bug in clang gets fixed, we should expand this macro inline
 *     for the non-clang case.
 ***************************************************************************/
# define DECLARE_TYPEOF_FNC(fnc_type,fnc) \
static __typeof__(&fnc_type) fnc          \
      = (__typeof__(&fnc_type)) -1

// For clang, declare these at top level, instead of inside a function.
DECLARE_TYPEOF_FNC(dmtcp_event_hook,fn);
DECLARE_TYPEOF_FNC(dmtcp_event_hook,_real_dmtcp_event_hook);
# undef DECLARE_TYPEOF_FNC
// This removes the declarations from NEXT_FNC2() and DMTCP_NEXT_EVENT_HOOK()
// Those macros are invoked from inside dmtcp_next_event_hook(), which
//   is declare in dmtcp.h as a weak function.
//   clang-3.4.1 seems to declare these inner fnc pointer types as weak
//     because the outer function is weak.  Arguably, this is a bug.
# define DECLARE_TYPEOF_FNC(fnc_type,fnc)

# define NEXT_FNC2(func)                                                    \
  ({                                                                        \
     /* static __typeof__(&func) _real_##func = (__typeof__(&func)) -1; */  \
     DECLARE_TYPEOF_FNC(func,_real_##func);                                 \
     if (_real_##func == (__typeof__(&func)) -1) {                          \
       if (dmtcp_prepare_wrappers) dmtcp_prepare_wrappers();                \
       __typeof__(&dlsym) dlsym_fnptr;                                      \
       dlsym_fnptr = (__typeof__(&dlsym)) dmtcp_get_libc_dlsym_addr();      \
       _real_##func = (__typeof__(&func)) (*dlsym_fnptr) (RTLD_NEXT, #func);\
     }                                                                      \
   _real_##func;})

# undef DMTCP_NEXT_EVENT_HOOK
# define DMTCP_NEXT_EVENT_HOOK(event, data)                                 \
  do {                                                                      \
    /* static __typeof__(&dmtcp_event_hook) fn                              \
        = (__typeof__(&dmtcp_event_hook)) -1; */                            \
    DECLARE_TYPEOF_FNC(dmtcp_event_hook,fn);                                \
    if ((void*) fn == (void*) -1) {                                         \
      fn = NEXT_FNC2(dmtcp_event_hook);                                     \
    }                                                                       \
    if (fn != NULL) {                                                       \
      (*fn) (event, data);                                                  \
    }                                                                       \
  } while (0)

// End of patches for clang-3.4.1
#endif

//===================================================================
// DMTCP utilities

#ifndef DMTCP_AFTER_CHECKPOINT
  // Return value of dmtcp_checkpoint
# define DMTCP_AFTER_CHECKPOINT 1
  // Return value of dmtcp_checkpoint
# define DMTCP_AFTER_RESTART    2
#endif
#ifndef DMTCP_NOT_PRESENT
# define DMTCP_NOT_PRESENT 3
#endif
#ifndef DMTCP_IS_PRESENT
# define DMTCP_IS_PRESENT 4
#endif

#define dmtcp_get_ckpt_filename() \
  (dmtcp_get_ckpt_filename ? dmtcp_get_ckpt_filename() : NULL)

#define dmtcp_get_uniquepid_str() \
  (dmtcp_get_uniquepid_str ? dmtcp_get_uniquepid_str() : NULL)

/* dmtcp_launch, dmtcp_restart return a unique rc (default: 99)
 * TYPICAL USAGE:  exit(DMTCP_FAIL_RC)
 * Use this to distinguish DMTCP failing versus the target application failing.
 */
#define DMTCP_FAIL_RC                                         \
  (getenv("DMTCP_FAIL_RC") && atoi(getenv("DMTCP_FAIL_RC"))   \
     ? atoi(getenv("DMTCP_FAIL_RC"))                          \
     : 99)

/// Pointer to a "void foo();" function
typedef void (*dmtcp_fnptr_t)(void);

#endif
