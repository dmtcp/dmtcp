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

#include <netinet/ip.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>

#ifndef __USE_GNU
# define __USE_GNU_NOT_SET
# define __USE_GNU
#endif // ifndef __USE_GNU
#include <dlfcn.h>  /* for NEXT_FNC() */
#ifdef __USE_GNU_NOT_SET
# undef __USE_GNU_NOT_SET
# undef __USE_GNU
#endif // ifdef __USE_GNU_NOT_SET

#ifndef DMTCP_PACKAGE_VERSION
# include "dmtcp/version.h"
#endif

#ifdef __cplusplus
# define EXTERNC extern "C"
#else // ifdef __cplusplus
# define EXTERNC
#endif // ifdef __cplusplus

/* Define to the version of this package. */
#define DMTCP_PLUGIN_API_VERSION "3"

#ifdef __cplusplus
extern "C" {
#endif // ifdef __cplusplus

#define LIB_PRIVATE              __attribute__((visibility("hidden")))

typedef enum eDmtcpEvent {
  DMTCP_EVENT_INIT,
  DMTCP_EVENT_EXIT,

  DMTCP_EVENT_PRE_EXEC,
  DMTCP_EVENT_POST_EXEC,

  DMTCP_EVENT_ATFORK_PREPARE,
  DMTCP_EVENT_ATFORK_PARENT,
  DMTCP_EVENT_ATFORK_CHILD,
  DMTCP_EVENT_ATFORK_FAILED,

  DMTCP_EVENT_VFORK_PREPARE,
  DMTCP_EVENT_VFORK_PARENT,
  DMTCP_EVENT_VFORK_CHILD,
  DMTCP_EVENT_VFORK_FAILED,

  DMTCP_EVENT_PTHREAD_START,
  DMTCP_EVENT_PTHREAD_EXIT,
  DMTCP_EVENT_PTHREAD_RETURN,

  DMTCP_EVENT_PRESUSPEND,
  DMTCP_EVENT_PRECHECKPOINT,
  DMTCP_EVENT_RESUME,
  DMTCP_EVENT_RESTART,

  DMTCP_EVENT_OPEN_FD,
  DMTCP_EVENT_REOPEN_FD,
  DMTCP_EVENT_CLOSE_FD,
  DMTCP_EVENT_DUP_FD,

  DMTCP_EVENT_VIRTUAL_TO_REAL_PATH,
  DMTCP_EVENT_REAL_TO_VIRTUAL_PATH,

  nDmtcpEvents
} DmtcpEvent_t;

typedef union _DmtcpEventData_t {
  struct {
    int serializationFd;
    char *filename;
    size_t maxArgs;
    const char **argv;
    size_t maxEnv;
    const char **envp;
  } preExec;

  struct {
    int serializationFd;
  } postExec;

  struct {
    int isRestart;
  } resumeUserThreadInfo, nameserviceInfo;

  struct {
    int fd;
    const char *path;
    int flags;
    mode_t mode;
  } openFd;

  struct {
    int fd;
    const char *path;
    int flags;
  } reopenFd;

  struct {
    int fd;
  } closeFd;

  struct {
    int oldFd;
    int newFd;
  } dupFd;

  struct {
    char *path;
  } realToVirtualPath, virtualToRealPath;
} DmtcpEventData_t;

typedef void (*HookFunctionPtr_t)(DmtcpEvent_t, DmtcpEventData_t *);

typedef struct {
  const char *pluginApiVersion;
  const char *dmtcpVersion;

  const char *pluginName;
  const char *authorName;
  const char *authorEmail;
  const char *description;

  void (*event_hook)(const DmtcpEvent_t event, DmtcpEventData_t *data);
} DmtcpPluginDescriptor_t;

// Used by dmtcp_get_restart_env()
typedef enum eDmtcpGetRestartEnvErr {
  RESTART_ENV_SUCCESS = 0,
  RESTART_ENV_NOTFOUND = -1,
  RESTART_ENV_TOOLONG = -2,
  RESTART_ENV_DMTCP_BUF_TOO_SMALL = -3,
  RESTART_ENV_INTERNAL_ERROR = -4,
  RESTART_ENV_NULL_PTR = -5,
} DmtcpGetRestartEnvErr_t;

typedef enum eDmtcpMutexType
{
  DMTCP_MUTEX_NORMAL,
  DMTCP_MUTEX_RECURSIVE,
  DMTCP_MUTEX_LLL
} DmtcpMutexType;

typedef struct
{
  DmtcpMutexType type;
  int32_t owner;
  uint32_t count;
} DmtcpMutex;

#define DMTCP_MUTEX_INITIALIZER {DMTCP_MUTEX_NORMAL, 0, 0}
#define DMTCP_MUTEX_INITIALIZER_RECURSIVE {DMTCP_MUTEX_RECURSIVE, 0, 0}

typedef struct
{
  int32_t writer;
  uint32_t nReaders;
  uint32_t nWritersQueued;
  uint32_t nReadersQueued;
  uint32_t writersFutex;
  uint32_t readersFutex;

  DmtcpMutex xLock;
} DmtcpRWLock;

void DmtcpMutexInit(DmtcpMutex *mutex, DmtcpMutexType type);
int DmtcpMutexLock(DmtcpMutex *mutex);
int DmtcpMutexTryLock(DmtcpMutex *mutex);
int DmtcpMutexUnlock(DmtcpMutex *mutex);

void DmtcpRWLockInit(DmtcpRWLock *rwlock);
int DmtcpRWLockRdLock(DmtcpRWLock *rwlock);
int DmtcpRWLockTryRdLock(DmtcpRWLock *rwlock);
int DmtcpRWLockWrLock(DmtcpRWLock *rwlock);
int DmtcpRWLockUnlock(DmtcpRWLock *rwlock);


#define   RESTART_ENV_MAXSIZE               12288*10

#define DMTCP_DECL_PLUGIN(descr)                      \
  EXTERNC void dmtcp_initialize_plugin()              \
  {                                                   \
    dmtcp_register_plugin(descr);                     \
    void (*fn)() = NEXT_FNC(dmtcp_initialize_plugin); \
    if (fn != NULL) {                                 \
      (*fn)();                                        \
    }                                                 \
  }

typedef struct DmtcpUniqueProcessId {
  uint64_t _hostid;  // gethostid()
  uint64_t _time; // time()
  pid_t _pid; // getpid()
  uint32_t _computation_generation; // computationGeneration()
} DmtcpUniqueProcessId;

int dmtcp_unique_pids_equal(DmtcpUniqueProcessId a, DmtcpUniqueProcessId b);

// FIXME:
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
int dmtcp_is_enabled(void) __attribute((weak));
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
int dmtcp_checkpoint(void) __attribute__((weak));
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
int dmtcp_disable_ckpt(void) __attribute__((weak));
#define dmtcp_disable_ckpt() \
  (dmtcp_disable_ckpt ? dmtcp_disable_ckpt() : DMTCP_NOT_PRESENT)

/**
 * Re-allow checkpoints, opposite of dmtcp_disable_ckpt().
 * + Returns 1 on success, <=0 on error
 * See: test/plugin/applic-delayed-ckpt directory for an exammple:
 */
int dmtcp_enable_ckpt(void) __attribute__((weak));
#define dmtcp_enable_ckpt() \
  (dmtcp_enable_ckpt ? dmtcp_enable_ckpt() : DMTCP_NOT_PRESENT)

/**
 * Example:  dmtcp_get_libc_addr("fork") returns the address of "fork"
 *           in libc at runtime (when loaded in memory).
 * NOTE:  dmtcp_get_libc_addr(fnc) skips any DMTCP wrapper functions around
 *        fnc, and directly calls its definition in libc.  In contrast, the
 *        _real_XX() functions found in DMTCP_ROOT/src are pointers to the
 *        next definition of fnc in library search order.  (This may be in the
 *        next libdmctp_YY.so library in search order, or in libc.so istelf.)
 * EXAMPLE USAGE 1:
 *   static typeof(fork) *libc_fork_ptr = NULL;
 *   if (libc_fork_ptr == NULL) {libc_fork_ptr = dmtcp_get_libc_addr("fork");}
 *   if (libc_fork_ptr != DMTCP_NOT_PRESENT) {
 *     // Skip DMTCP interposition on fork; child doesn't coonect to coord.
 *     (*libc_fork_ptr)();
 *   }
 * EXAMPLE USAGE 2:
 *   static typeof(execvp) *libc_execvp_ptr = NULL;
 *   if (libc_execvp_ptr == NULL) {
 *     libc_execvp_ptr = dmtcp_get_libc_addr("execvp");
 *   }
 *   if (libc_execvp_ptr != DMTCP_NOT_PRESENT) {
 *     // Don't preload DMTCP libraries when you exec to a new program.
 *     char *ld_preload = getenv("LD_PRELOAD");
 *     ld_preload[0] = '\0';
 *     (*libc_execvp_ptr)(...);
 *   }
 */
void * dmtcp_get_libc_addr(const char* libc_fnc) __attribute__((weak));
#define dmtcp_get_libc_addr(libc_fnc) \
  (dmtcp_get_libc_addr? dmtcp_get_libc_addr(libc_fnc) : \
                        (void *)DMTCP_NOT_PRESENT)

/* FIXME:  Usage: ?? */
void dmtcp_initialize_plugin(void) __attribute((weak));

/*
 * Global barriers are required when a plugin needs inter-node synchronization,
 * such as using the coordinator name-service database. Currently, only the
 * socket, RM, and InfiniBand plugins need global barriers. All other plugins
 * handle node-local resources such as files, pids, etc., and are fine with
 * using local barriers.
 * A simple thumb rule is to always insert a global-barrier between registering
 * and querying the coordinator name-service database.
 */
void dmtcp_global_barrier(const char *barrier) __attribute((weak));
void dmtcp_local_barrier(const char *barrier) __attribute((weak));

// See: test/plugin/example-db dir for an example:
int dmtcp_send_key_val_pair_to_coordinator(const char *id,
                                           const void *key,
                                           uint32_t key_len,
                                           const void *val,
                                           uint32_t val_len);
int dmtcp_send_query_to_coordinator(const char *id,
                                    const void *key,
                                    uint32_t key_len,
                                    void *val,
                                    uint32_t *val_len);

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
int dmtcp_get_unique_id_from_coordinator(const char *id,
                                         const void *key,
                                         uint32_t key_len,
                                         void *val,
                                         uint32_t offset,
                                         uint32_t val_len);

/*
 * This function can be used to query all mappings in the given nameservice
 * database, specified by the first argument, id. If a zero-length buffer
 * is passed to the function, i.e., if the len argument is 0, the function
 * will allocate a buffer of required length; it's the caller's responsibility
 * to free the buffer after use. The key-value mappings in the buffer are
 * serialized in the following format:
 *
 *    <int key_length, key, int value_length, value>
 *    <int key_length, key, int value_length, value>
 *    ...
 *
 * The function returns 0 on success and sets the len argument to the size
 * of the buffer. On failure, -1 is returned and errno is set to indicate
 * what went wrong. If the length of the supplied buffer is less than the
 * required size, -1 is returned and errno is set to ERANGE.
 */
int dmtcp_send_query_all_to_coordinator(const char *id, void **buf, int *len);

void dmtcp_get_local_ip_addr(struct in_addr *in) __attribute((weak));

const char *dmtcp_get_tmpdir(void);

const char *dmtcp_get_ckpt_dir(void) __attribute((weak));
#define dmtcp_get_ckpt_dir() \
  (dmtcp_get_ckpt_dir ? dmtcp_get_ckpt_dir() : "")

int dmtcp_set_ckpt_dir(const char *) __attribute((weak));
#define dmtcp_set_ckpt_dir(d) \
  (dmtcp_set_ckpt_dir ? dmtcp_set_ckpt_dir(d) : DMTCP_NOT_PRESENT)

const char *dmtcp_get_coord_ckpt_dir(void) __attribute__((weak));
#define dmtcp_get_coord_ckpt_dir() \
  (dmtcp_get_coord_ckpt_dir ? dmtcp_get_coord_ckpt_dir() : "")

int dmtcp_set_coord_ckpt_dir(const char *dir) __attribute__((weak));
#define dmtcp_set_coord_ckpt_dir(d) \
  (dmtcp_set_coord_ckpt_dir ? dmtcp_set_coord_ckpt_dir(d) : DMTCP_NOT_PRESENT)

const char *dmtcp_get_ckpt_filename(void) __attribute__((weak));
const char *dmtcp_get_ckpt_files_subdir(void);
int dmtcp_should_ckpt_open_files(void);
int dmtcp_allow_overwrite_with_ckpted_files(void);

int dmtcp_get_ckpt_signal(void);
const char *dmtcp_get_uniquepid_str(void) __attribute__((weak));

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
const char *dmtcp_get_computation_id_str(void);
uint64_t dmtcp_get_coordinator_timestamp(void);

// Generation is 0 before first checkpoint, and then successively incremented.
uint32_t dmtcp_get_generation(void) __attribute__((weak));
int checkpoint_is_pending(void) __attribute__((weak));

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
int dmtcp_get_coordinator_status(int *numPeers, int *isRunning)
__attribute__((weak));
#define dmtcp_get_coordinator_status(p, r)                           \
  (dmtcp_get_coordinator_status ? dmtcp_get_coordinator_status(p, r) \
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
int dmtcp_get_local_status(int *numCheckpoints, int *numRestarts)
__attribute__((weak));
#define dmtcp_get_local_status(c, r) \
  (dmtcp_get_local_status ? dmtcp_get_local_status(c, r) : DMTCP_NOT_PRESENT)

// Is DMTCP in the running state?
// (e.g., not in pre-ckpt, post-ckpt, post-restart event)?
int dmtcp_is_running_state(void);

// Primarily for use by the modify-env plugin.
DmtcpGetRestartEnvErr_t dmtcp_get_restart_env(const char *name,
                                              char *value,
                                              size_t maxvaluelen);

// Get pathname of target executable under DMTCP control.
const char *dmtcp_get_executable_path();

// True if dmtcp_launch called with --no-coordinator
int dmtcp_no_coordinator(void);

/* If your plugin invokes wrapper functions before DMTCP is initialized,
 *   then call this prior to your first wrapper function call.
 */
void dmtcp_initialize(void) __attribute((weak));

// FOR EXPERTS ONLY:
int dmtcp_is_protected_fd(int fd);
DmtcpUniqueProcessId dmtcp_get_uniquepid();
DmtcpUniqueProcessId dmtcp_get_coord_id();
DmtcpUniqueProcessId dmtcp_get_computation_id();

// FOR EXPERTS ONLY:
int dmtcp_get_ptrace_fd(void);
int dmtcp_get_readlog_fd(void);
void dmtcp_block_ckpt_signal(void);
void dmtcp_unblock_ckpt_signal(void);

// FOR EXPERTS ONLY:
void dmtcp_close_protected_fd(int fd);
int dmtcp_protected_environ_fd(void);

/* FOR EXPERTS ONLY:
 *   The DMTCP internal pid plugin ensures that the application sees only
 *  a virtual pid, which can be translated to the current real pid
 *  assigned to the kernel on a restart.  The pid plugin places wrappers
 *  around all system calls referring to a pid.  If your application
 *  discovers a pid without going through a system call (e.g., through
 *  the proc filesystem), use this to virtualize the pid.
 */
pid_t dmtcp_real_to_virtual_pid(pid_t realPid) __attribute((weak));
pid_t dmtcp_virtual_to_real_pid(pid_t virtualPid) __attribute((weak));

// bq_file -> "batch queue file"; used only by batch-queue plugin
int dmtcp_is_bq_file(const char *path) __attribute((weak));
int dmtcp_bq_should_ckpt_file(const char *path, int *type) __attribute((weak));
int dmtcp_bq_restore_file(const char *path,
                          const char *savedFilePath,
                          int fcntlFlags,
                          int type) __attribute((weak));

/*  These next two functions are defined in contrib/ckptfile/ckptfile.cpp
 *  But they are currently used only in src/plugin/ipc/file/fileconnection.cpp
 *    and in a trivial fashion.  These are intended for future extensions.
 */
int dmtcp_must_ckpt_file(const char *path) __attribute((weak));
void dmtcp_get_new_file_path(const char *abspath,
                             const char *cwd,
                             char *newpath) __attribute((weak));
int dmtcp_must_overwrite_file(const char *path) __attribute((weak));

void dmtcp_initialize(void) __attribute((weak));

void dmtcp_register_plugin(DmtcpPluginDescriptor_t) __attribute((weak));

// These are part of the internal implementation of DMTCP plugins
int dmtcp_plugin_disable_ckpt(void);
#define DMTCP_PLUGIN_DISABLE_CKPT() \
  int __dmtcp_plugin_ckpt_disabled = dmtcp_plugin_disable_ckpt()

void dmtcp_plugin_enable_ckpt(void);
#define DMTCP_PLUGIN_ENABLE_CKPT() \
  if (__dmtcp_plugin_ckpt_disabled) dmtcp_plugin_enable_ckpt()


void *dmtcp_dlsym(void *handle, const char *symbol) __attribute((weak));
void *dmtcp_dlvsym(void *handle, char *symbol, const char *version);
void *dmtcp_dlsym_lib(const char *libname, const char *symbol);

/*
 * Returns the offset of the given function within the given shared library
 * or LIB_FNC_OFFSET_FAILED if the function does not exist in the library
 */
#define LIB_FNC_OFFSET_FAILED ((uint64_t)-1)
uint64_t dmtcp_dlsym_lib_fnc_offset(const char *libname, const char *symbol);

#define NEXT_FNC(func)                                                       \
  ({                                                                         \
    static __typeof__(&func)_real_ ## func = (__typeof__(&func)) - 1;        \
    if (_real_ ## func == (__typeof__(&func)) - 1) {                         \
      if (dmtcp_initialize) {                                                \
        dmtcp_initialize();                                                  \
      }                                                                      \
      _real_ ## func = (__typeof__(&func))dmtcp_dlsym(RTLD_NEXT, # func); \
    }                                                                        \
    _real_ ## func;                                                          \
  })

/*
 * It uses dmtcp_dlvsym to get the function with the specified version in the
 * next library in the library-search order.
 */
# define NEXT_FNC_V(func, ver)                                                 \
  ({                                                                           \
    static __typeof__(&func) _real_##func = (__typeof__(&func)) -1;            \
    if (_real_##func == (__typeof__(&func)) -1) {                              \
      if (dmtcp_initialize) {                                                  \
        dmtcp_initialize();                                                    \
      }                                                                        \
      _real_##func = (__typeof__(&func)) dmtcp_dlvsym(RTLD_NEXT, #func, ver);  \
    }                                                                          \
    _real_##func;                                                              \
  })

/*
 * It uses dmtcp_dlsym to get the default function (in case of symbol
 * versioning) in the library with the given name.
 *
 * One possible usecase could be for bypassing the plugin layers and directly
 * jumping to a symbol in libc.
 */
# define NEXT_FNC_LIB(lib, func)                                               \
  ({                                                                           \
    static __typeof__(&func) _real_##func = (__typeof__(&func)) -1;            \
    if (_real_##func == (__typeof__(&func)) -1) {                              \
      if (dmtcp_initialize) {                                                  \
        dmtcp_initialize();                                                    \
      }                                                                        \
      _real_##func = (__typeof__(&func)) dmtcp_dlsym_lib(lib,  #func);         \
    }                                                                          \
    _real_##func;                                                              \
  })

// ===================================================================
// DMTCP utilities

#ifndef DMTCP_AFTER_CHECKPOINT

// Return value of dmtcp_checkpoint
# define DMTCP_AFTER_CHECKPOINT 1

// Return value of dmtcp_checkpoint
# define DMTCP_AFTER_RESTART    2
#endif // ifndef DMTCP_AFTER_CHECKPOINT
#ifndef DMTCP_NOT_PRESENT
# define DMTCP_NOT_PRESENT      3
#endif // ifndef DMTCP_NOT_PRESENT
#ifndef DMTCP_IS_PRESENT
# define DMTCP_IS_PRESENT       4
#endif // ifndef DMTCP_IS_PRESENT

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

#ifdef __cplusplus
} // extern "C" {
#endif // ifdef __cplusplus

#endif // ifndef DMTCP_H
