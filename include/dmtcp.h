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

#define DMTCP_PLUGIN_API_VERSION "3"

typedef enum eDmtcpEvent {
  DMTCP_EVENT_INIT,
  DMTCP_EVENT_EXIT,

  DMTCP_EVENT_PRE_EXEC,
  DMTCP_EVENT_POST_EXEC,

  DMTCP_EVENT_ATFORK_PREPARE,
  DMTCP_EVENT_ATFORK_PARENT,
  DMTCP_EVENT_ATFORK_CHILD,

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
  } resumeUserThreadInfo, nameserviceInfo;

  struct {
    const char* barrierId;
  } barrierInfo;
} DmtcpEventData_t;

typedef enum {
  DMTCP_GLOBAL_BARRIER_PRE_CKPT,
  DMTCP_LOCAL_BARRIER_PRE_CKPT,
  DMTCP_PRIVATE_BARRIER_PRE_CKPT,

  DMTCP_GLOBAL_BARRIER_RESUME,
  DMTCP_LOCAL_BARRIER_RESUME,
  DMTCP_PRIVATE_BARRIER_RESUME,

  DMTCP_GLOBAL_BARRIER_RESTART,
  DMTCP_LOCAL_BARRIER_RESTART,
  DMTCP_PRIVATE_BARRIER_RESTART
} DmtcpBarrierType;

typedef struct
{
  DmtcpBarrierType type;
  void (*callback)();
  const char *id;
} DmtcpBarrier;

typedef void (*HookFunctionPtr_t)(DmtcpEvent_t, DmtcpEventData_t *);

typedef struct
{
  const char* pluginApiVersion;
  const char* dmtcpVersion;

  const char* pluginName;
  const char* authorName;
  const char* authorEmail;
  const char* description;

  size_t numBarriers;
  DmtcpBarrier *barriers;

  void (*event_hook)(const DmtcpEvent_t event, DmtcpEventData_t *data);
} DmtcpPluginDescriptor_t;

#define DMTCP_NO_PLUGIN_BARRIERS 0, NULL

#define DMTCP_DECL_BARRIERS(barriers)                                          \
  sizeof(barriers) / sizeof (DmtcpBarrier),                                    \
  barriers

#define DMTCP_DECL_PLUGIN(descr)                                               \
  EXTERNC void dmtcp_initialize_plugin()                                       \
  {                                                                            \
    dmtcp_register_plugin(descr);                                              \
    void (*fn)() = NEXT_FNC(dmtcp_initialize_plugin);                          \
    if (fn != NULL) {                                                          \
      (*fn)();                                                                 \
    }                                                                          \
  }

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

EXTERNC void dmtcp_initialize_plugin(void) __attribute((weak));

// See: test/plugin/example-db dir for an example:
EXTERNC int dmtcp_send_key_val_pairs_to_coordinator(const char *id,
                                                    size_t keyLen,
                                                    size_t valLen,
                                                    size_t count,
                                                    const void *data);


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
//EXTERNC void dmtcp_set_tmpdir(const char *);

EXTERNC const char* dmtcp_get_ckpt_dir(void) __attribute ((weak));
#define dmtcp_get_ckpt_dir() \
 (dmtcp_get_ckpt_dir ? dmtcp_get_ckpt_dir() : NULL)
EXTERNC int dmtcp_set_ckpt_dir(const char *) __attribute ((weak));
#define dmtcp_set_ckpt_dir(d) \
 (dmtcp_set_ckpt_dir ? dmtcp_set_ckpt_dir(d) : DMTCP_NOT_PRESENT)
EXTERNC const char* dmtcp_get_coord_ckpt_dir(void) __attribute__ ((weak));
#define dmtcp_get_coord_ckpt_dir() \
 (dmtcp_get_coord_ckpt_dir ? dmtcp_get_coord_ckpt_dir() : NULL)
EXTERNC int dmtcp_set_coord_ckpt_dir(const char* dir) __attribute__ ((weak));
#define dmtcp_set_coord_ckpt_dir(d) \
 (dmtcp_set_coord_ckpt_dir ? dmtcp_set_coord_ckpt_dir(d) : DMTCP_NOT_PRESENT)
EXTERNC const char* dmtcp_get_ckpt_filename(void) __attribute__((weak));
EXTERNC const char* dmtcp_get_ckpt_files_subdir(void);
EXTERNC int dmtcp_should_ckpt_open_files(void);

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


EXTERNC void dmtcp_prepare_wrappers(void) __attribute((weak));

EXTERNC void dmtcp_register_plugin(DmtcpPluginDescriptor_t);

// These are part of the internal implementation of DMTCP plugins
EXTERNC int dmtcp_plugin_disable_ckpt(void);
#define DMTCP_PLUGIN_DISABLE_CKPT() \
  int __dmtcp_plugin_ckpt_disabled = dmtcp_plugin_disable_ckpt()

EXTERNC void dmtcp_plugin_enable_ckpt(void);
#define DMTCP_PLUGIN_ENABLE_CKPT() \
  if (__dmtcp_plugin_ckpt_disabled) dmtcp_plugin_enable_ckpt()


#define NEXT_FNC(func)                                                      \
  ({                                                                        \
     static __typeof__(&func) _real_##func = (__typeof__(&func)) -1;        \
     if (_real_##func == (__typeof__(&func)) -1) {                          \
       if (dmtcp_prepare_wrappers) dmtcp_prepare_wrappers();                \
       __typeof__(&dlsym) dlsym_fnptr;                                      \
       dlsym_fnptr = (__typeof__(&dlsym)) dmtcp_get_libc_dlsym_addr();      \
       _real_##func = (__typeof__(&func)) (*dlsym_fnptr) (RTLD_NEXT, #func);\
     }                                                                      \
   _real_##func;})

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

/// Pointer to a "void foo();" function
typedef void (*dmtcp_fnptr_t)(void);

#endif
