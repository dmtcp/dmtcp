/****************************************************************************
 *   Copyright (C) 2006-2010 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#include "dmtcpworker.h"
#include "constants.h"
#include  "../jalib/jconvert.h"
#include  "../jalib/jalloc.h"
#include "dmtcpmessagetypes.h"
#include "dmtcpmodule.h"
#include <stdlib.h>
#include "mtcpinterface.h"
#include <unistd.h>
#include "sockettable.h"
#include  "../jalib/jsocket.h"
#include <map>
#include "kernelbufferdrainer.h"
#include  "../jalib/jfilesystem.h"
#include "syscallwrappers.h"
#include "protectedfds.h"
#include "connectionidentifier.h"
#include "connectionmanager.h"
#include "connectionstate.h"
#include "dmtcp_coordinator.h"
#include "util.h"
#include "sysvipc.h"
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/personality.h>
#include <netdb.h>
#include <malloc.h>
#ifdef RECORD_REPLAY
#include "synchronizationlogging.h"
#include "log.h"
#endif

#ifdef RECORD_REPLAY
static inline void memfence() {  asm volatile ("mfence" ::: "memory"); }
#endif

using namespace dmtcp;

static pthread_mutex_t theCkptCanStart = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
static pthread_mutex_t destroyDmtcpWorker = PTHREAD_MUTEX_INITIALIZER;

/*
 * WrapperProtectionLock is used to make the checkpoint safe by making sure
 *   that no user-thread is executing any DMTCP wrapper code when it receives
 *   the checkpoint signal.
 * Working:
 *   On entering the wrapper in DMTCP, the user-thread acquires the read lock,
 *     and releases it before leaving the wrapper.
 *   When the Checkpoint-thread wants to send the SUSPEND signal to user
 *     threads, it must acquire the write lock. It is blocked until all the
 *     existing read-locks by user threads have been released. NOTE that this
 *     is a WRITER-PREFERRED lock.
 *
 * There is a corner case too -- the newly created thread that has not been
 *   initialized yet; we need to take some extra efforts for that.
 * Here are the steps to handle the newly created uninitialized thread:
 *   A counter for the number of newly created uninitialized threads is kept.
 *     The counter is made thread safe by using a mutex.
 *   The calling thread (parent) increments the counter before calling clone.
 *   The newly created child thread decrements the counter at the end of
 *     initialization in MTCP/DMTCP.
 *   After acquiring the Write lock, the checkpoint thread waits until the
 *     number of uninitialized threads is zero. At that point, no thread is
 *     executing in the clone wrapper and it is safe to do a checkpoint.
 *
 * XXX: Currently this security is provided only for the clone wrapper; this
 * should be extended to other calls as well.           -- KAPIL
 */
static pthread_rwlock_t theWrapperExecutionLock = PTHREAD_RWLOCK_WRITER_NONRECURSIVE_INITIALIZER_NP;
static pthread_mutex_t unInitializedThreadCountLock = PTHREAD_MUTEX_INITIALIZER;
static int unInitializedThreadCount = 0;
static dmtcp::UniquePid compGroup;
int dmtcp_wrappers_initializing = 0;

// static dmtcp::KernelBufferDrainer* theDrainer = NULL;
static dmtcp::ConnectionState* theCheckpointState = NULL;

#ifdef EXTERNAL_SOCKET_HANDLING
static dmtcp::vector <dmtcp::TcpConnectionInfo> theTcpConnections;
dmtcp::vector <dmtcp::ConnectionIdentifier> externalTcpConnections;
static bool _waitingForExternalSocketsToClose = false;
#endif

bool dmtcp::DmtcpWorker::_exitInProgress = false;
size_t dmtcp::DmtcpWorker::_argvSize = 0;
size_t dmtcp::DmtcpWorker::_envSize = 0;

void processDmtcpCommands(dmtcp::string programName,
                          dmtcp::vector<dmtcp::string>& args);
static void processSshCommand(dmtcp::string programName,
                              dmtcp::vector<dmtcp::string>& args);

// To allow linking without mtcpinterface;  Weak symbol undefined, is set to 0
void __attribute__ ((weak)) dmtcp::initializeMtcpEngine();
void __attribute__ ((weak)) dmtcp::killCkpthread();

const unsigned int dmtcp::DmtcpWorker::ld_preload_c_len;
char dmtcp::DmtcpWorker::ld_preload_c[dmtcp::DmtcpWorker::ld_preload_c_len];

bool _checkpointThreadInitialized = false;
void restoreUserLDPRELOAD()
{
  // We have now successfully used LD_PRELOAD to execute prior to main()
  // Next, hide our value of LD_PRELOAD, in a global variable.
  // At checkpoint and restart time, we will no longer need our LD_PRELOAD.
  // We will need it in only one place:
  //  when the user application makes an exec call:
  //   If anybody calls our execwrapper, we will reset LD_PRELOAD then.
  //   If they directly call _real_execve to get libc symbol, they will
  //   not be part of DMTCP computation.
  // This has the advantage that our value of LD_PRELOAD will always come
  //   before any paths set by user application.
  // Also, bash likes to keep its own envp, but we will interact with bash only
  //   within the exec wrapper.
  // NOTE:  If the user called exec("ssh ..."), we currently catch this in
  //   DmtcpWorker() due to LD_PRELOAD, unset LD_PRELOAD, and edit this into
  //   exec("dmtcp_checkpoint --ssh-slave ... ssh ..."), and re-execute.
  //   This way, we will unset LD_PRELOAD here and now, instead of at that time.
  char * preload =  getenv("LD_PRELOAD");
  char * preload_rest = strstr(preload, "dmtcphijack.so:");
  if (preload_rest) {
    preload_rest = strstr(preload_rest, ":");
    *preload_rest = '\0'; // Now preload is just our preload string
    preload_rest++;
  }
  JTRACE("LD_PRELOAD")(preload);
  JASSERT(strlen(preload) < dmtcp::DmtcpWorker::ld_preload_c_len)
	 (preload) (dmtcp::DmtcpWorker::ld_preload_c_len)
	 .Text("preload string is longer than ld_preload_c_len");
  strcpy(dmtcp::DmtcpWorker::ld_preload_c, preload);  // Don't malloc
  if (preload_rest) {
    setenv("LD_PRELOAD", preload_rest, 1);
  } else {
    _dmtcp_unsetenv("LD_PRELOAD");
  }
}

// FIXME:  We need a better way to get MTCP_DEFAULT_SIGNAL
//         See:  pidwrappers.cpp:get_sigckpt()
#include "../../mtcp/mtcp.h" //for MTCP_DEFAULT_SIGNAL

// This should be visible to library only.  DmtcpWorker will call
//   this to initialize tmp (ckpt signal) at startup time.  This avoids
//   any later calls to getenv(), at which time the user app may have
//   a wrapper around getenv, modified environ, or other tricks.
//   (Matlab needs this or else it segfaults on restart, and bash plays
//   similar tricks with maintaining its own environment.)
// Used in mtcpinterface.cpp and signalwrappers.cpp.
// FIXME: DO we still want it to be library visible only?
//__attribute__ ((visibility ("hidden")))
int dmtcp::DmtcpWorker::determineMtcpSignal()
{
  // this mimics the MTCP logic for determining signal number found in
  // mtcp_init()
  int sig = MTCP_DEFAULT_SIGNAL;
  char* endp = NULL;
  static const char* tmp = getenv("MTCP_SIGCKPT");
  if(tmp != NULL){
      sig = strtol(tmp, &endp, 0);
      if((errno != 0) || (tmp == endp))
        sig = MTCP_DEFAULT_SIGNAL;
      if(sig < 1 || sig > 31)
        sig = MTCP_DEFAULT_SIGNAL;
  }
  return sig;
}

#ifdef USE_MALLOC_HOOKS
void *(*old_malloc_hook)(size_t, const void *);
void (*old_free_hook)(void *, const void *);
static void *_dmtcp_malloc_hook(size_t size, const void *caller);
static void _dmtcp_free_hook(void *ptr, const void *caller);

#define _ALLOC_HOOKS_UNDEF() do {      \
  __malloc_hook  = old_malloc_hook;    \
  __free_hook    = old_free_hook;      \
  } while (0)

#define _ALLOC_HOOKS_DEF() do {         \
  __malloc_hook  = _dmtcp_malloc_hook;  \
  __free_hook    = _dmtcp_free_hook;    \
  } while (0)

#ifdef JALIB_ALLOCATOR
static void *_dmtcp_malloc_hook(size_t size, const void *caller)
{
  void *result = JALLOC_HELPER_MALLOC (size);
  return result;
}

static void _dmtcp_free_hook(void *ptr, const void *caller)
{
  JALLOC_HELPER_FREE(ptr);
}
#else
// May not work with malloc() wrappers
static void *_dmtcp_malloc_hook(size_t size, const void *caller)
{
  _ALLOC_HOOKS_UNDEF();
  void *result = malloc (size);
  _ALLOC_HOOKS_DEF();
  return result;
}

static void _dmtcp_free_hook(void *ptr, const void *caller)
{
  _ALLOC_HOOKS_UNDEF();
  free(ptr);
  _ALLOC_HOOKS_DEF();
}
#endif
#endif


extern "C" LIB_PRIVATE void prepareDmtcpWrappers()
{
  JALLOC_HELPER_DISABLE_LOCKS();
#ifdef USE_MALLOC_HOOKS
  _ALLOC_HOOKS_DEF();
#endif

  dmtcp_wrappers_initializing = 1;
  initialize_wrappers();
  //dmtcp_process_event(DMTCP_EVENT_INIT_WRAPPERS, NULL);
  dmtcp_wrappers_initializing = 0;

#ifdef USE_MALLOC_HOOKS
  _ALLOC_HOOKS_UNDEF();
#endif
  JALLOC_HELPER_ENABLE_LOCKS();
}

static void calculateArgvAndEnvSize(size_t& argvSize, size_t& envSize)
{
  dmtcp::vector<dmtcp::string> args = jalib::Filesystem::GetProgramArgs();
  argvSize = 0;
  for (size_t i = 0; i < args.size(); i++) {
    argvSize += args[i].length() + 1;
  }
  envSize = 0;
  if (environ != NULL) {
    char *ptr = environ[0];
    while (*ptr != '\0' && args[0].compare(ptr) != 0) {
      envSize += strlen(ptr) + 1;
      ptr += strlen(ptr) + 1;
    }
  }
  envSize += args[0].length();
}

static dmtcp::string getLogFilePath()
{
#ifdef DEBUG
  dmtcp::ostringstream o;
  o << "/proc/self/fd/" << PROTECTED_JASSERTLOG_FD;
  return jalib::Filesystem::ResolveSymlink(o.str());
#else
  return "";
#endif
}

static void writeCurrentLogFileNameToPrevLogFile(dmtcp::string& path)
{
#ifdef DEBUG
  dmtcp::ostringstream o;
  o << "========================================\n"
    << "This process exec()'d into a new program\n"
    << "Program Name: " << jalib::Filesystem::GetProgramName() << "\n"
    << "New JAssertLog Path: " << getLogFilePath() << "\n"
    << "========================================\n";

  int fd = open(path.c_str(), O_WRONLY | O_APPEND, 0);
  if (fd != -1) {
    Util::writeAll(fd, o.str().c_str(), o.str().length());
  }
  _real_close(fd);
#endif
}

static void prepareLogAndProcessdDataFromSerialFile()
{
  const char* serialFile = getenv( ENV_VAR_SERIALFILE_INITIAL );
  if ( serialFile != NULL ) {
    // This process was under ckpt-control and exec()'d into a new program.
    // Find out path of previous log file so that later, we can write the name
    // of the new log file into that one.
    dmtcp::string prevLogFilePath = getLogFilePath();

    jalib::JBinarySerializeReader rd ( serialFile );
    UniquePid::serialize ( rd );
    Util::initializeLogFile("", prevLogFilePath);

    writeCurrentLogFileNameToPrevLogFile(prevLogFilePath);

    JTRACE ( "loading initial socket table from file..." ) ( serialFile );
    KernelDeviceToConnection::instance().serialize ( rd );

#ifdef PID_VIRTUALIZATION
    VirtualPidTable::instance().serialize ( rd );
    VirtualPidTable::instance().postExec();
    SysVIPC::instance().serialize ( rd );
#endif
    _dmtcp_unsetenv(ENV_VAR_SERIALFILE_INITIAL);
  } else {
    // Brand new process (was never under ckpt-control),
    // Initialize the log file
    Util::initializeLogFile();

#ifdef PID_VIRTUALIZATION
    if ( getenv( ENV_VAR_ROOT_PROCESS ) != NULL ) {
      JTRACE("Root of processes tree");
      VirtualPidTable::instance().setRootOfProcessTree();
      _dmtcp_unsetenv(ENV_VAR_ROOT_PROCESS);
    }
#endif

    JTRACE("Checking for pre-existing sockets");
    ConnectionList::instance().scanForPreExisting();
  }

  JTRACE ("Initial socket table:");
  KernelDeviceToConnection::instance().dbgSpamFds();
}

static void processRlimit()
{
#ifdef __i386__
  // Match work begun in dmtcpPrepareForExec()
# if 0
  if (getenv("DMTCP_ADDR_COMPAT_LAYOUT")) {
    _dmtcp_unsetenv("DMTCP_ADDR_COMPAT_LAYOUT");
    // DMTCP had set ADDR_COMPAT_LAYOUT.  Now unset it.
    personality( (unsigned long)personality(0xffffffff) ^ ADDR_COMPAT_LAYOUT );
    JTRACE( "unsetting ADDR_COMPAT_LAYOUT" );
  }
# else
  { char * rlim_cur_char = getenv("DMTCP_RLIMIT_STACK");
    if ( rlim_cur_char != NULL ) {
      struct rlimit rlim;
      getrlimit(RLIMIT_STACK, &rlim);
      rlim.rlim_cur = atol(rlim_cur_char);
      JTRACE ( "rlim_cur for RLIMIT_STACK being restored." ) ( rlim.rlim_cur );
      setrlimit(RLIMIT_STACK, &rlim);
      _dmtcp_unsetenv("DMTCP_RLIMIT_STACK");
    }
  }
# endif
#endif
}

#ifdef RECORD_REPLAY
static void recordReplayInit()
{
  // As of rev. 816, this line caused DMTCP with standard ./configure
  //   (no command line flags) to segfault.
  // To see bug, do:  gdb --args bin/dmtcp_checkpoint ls
  // NOTe: This comment may not be true anymore.
  _dmtcp_setup_trampolines();

  /* This is called only on exec(). We reset the global clone counter for this
     process, assign the first thread (this one) clone_id 1, and increment the
     counter. */
  JTRACE ( "resetting global clone counter." );
  global_clone_counter = GLOBAL_CLONE_COUNTER_INIT;
  my_clone_id = global_clone_counter;
  global_clone_counter++;

  my_log = new dmtcp::SynchronizationLog();
  clone_id_to_tid_table[my_clone_id] = pthread_self();
  clone_id_to_log_table[my_clone_id] = my_log;

  /* Other initialization for sync log/replay specific to this process. */
  initializeLogNames();
  if (getenv(ENV_VAR_LOG_REPLAY) == NULL) {
    /* If it is NULL, this is the very first exec. We unset => set to 0
       (meaning no logging, no replay) */
    // FIXME: setenv is known to cause issues when interacting with bash.
    setenv(ENV_VAR_LOG_REPLAY, "0", 1);
  }
  sync_logging_branch = atoi(getenv(ENV_VAR_LOG_REPLAY));
  /* Synchronize this constructor, if this is not the very first exec. */
  log_entry_t my_entry = create_exec_barrier_entry();
  if (SYNC_IS_REPLAY) {
    memfence();
    waitForExecBarrier();
    getNextLogEntry();
  } else if (SYNC_IS_RECORD) {
    addNextLogEntry(my_entry);
  }
}
#endif

//called before user main()
//workerhijack.cpp initializes a static variable theInstance to DmtcpWorker obj
dmtcp::DmtcpWorker::DmtcpWorker ( bool enableCheckpointing )
{
  if ( !enableCheckpointing ) return;
  else {
    WorkerState::setCurrentState( WorkerState::UNKNOWN);
    prepareDmtcpWrappers();
    prepareLogAndProcessdDataFromSerialFile();
  }

  JTRACE ( "dmtcphijack.so:  Running " )
    ( jalib::Filesystem::GetProgramName() ) ( getenv ( "LD_PRELOAD" ) );

  if (getenv(ENV_VAR_UTILITY_DIR) == NULL) {
    JNOTE ( "\n **** Not checkpointing this process,"
            " due to missing environment var ****" )
          ( getenv(ENV_VAR_UTILITY_DIR) )
          ( jalib::Filesystem::GetProgramName() );
    return;
  }

  processRlimit();

  //This is called for side effect only.  Force this function to call
  // getenv("MTCP_SIGCKPT") now and cache it to avoid getenv calls later.
  determineMtcpSignal();

  // Also cache programName and arguments
  dmtcp::string programName = jalib::Filesystem::GetProgramName();
  dmtcp::vector<dmtcp::string> args = jalib::Filesystem::GetProgramArgs();

  if (programName == "dmtcp_coordinator" || programName == "dmtcp_command" ||
      programName == "dmtcp_checkpoint"  || programName == "dmtcp_restart" ||
      programName == "mtcp_restart" ) {
    processDmtcpCommands(programName, args);
  } else if ( programName == "ssh" ) {
    processSshCommand(programName, args);
  }
  calculateArgvAndEnvSize(_argvSize, _envSize);

  WorkerState::setCurrentState ( WorkerState::RUNNING );

  connectToCoordinatorWithHandshake();

#ifdef RECORD_REPLAY
  recordReplayInit();
#endif
  // define "Weak Symbols for each library module in dmtcphijack.so
  process_dmtcp_event(DMTCP_EVENT_INIT, NULL);

  /* Acquire the lock here, so that the checkpoint-thread won't be able to
   * process CHECKPOINT request until we are done with initializeMtcpEngine()
   */
  if (initializeMtcpEngine) { // if strong symbol defined elsewhere
    WRAPPER_EXECUTION_DISABLE_CKPT();
    initializeMtcpEngine();
    WRAPPER_EXECUTION_ENABLE_CKPT();
  } else { // else trying to call weak symbol, which is undefined
    JASSERT(false).Text("initializeMtcpEngine should not be called");
  }

  /* Now wait for Checkpoint Thread to finish initialization
   * NOTE: This should be the last thing in this constructor
   */
  while (!_checkpointThreadInitialized) {
    struct timespec sleepTime = {0, 10*1000*1000};
    nanosleep(&sleepTime, NULL);
  }
}

void dmtcp::DmtcpWorker::cleanupWorker()
{
  pthread_rwlock_t newLock = PTHREAD_RWLOCK_WRITER_NONRECURSIVE_INITIALIZER_NP;
  theWrapperExecutionLock = newLock;

  pthread_mutex_t newCountLock = PTHREAD_MUTEX_INITIALIZER;
  unInitializedThreadCountLock = newCountLock;

  pthread_mutex_t newDestroyDmtcpWorker = PTHREAD_MUTEX_INITIALIZER;
  destroyDmtcpWorker = newDestroyDmtcpWorker;

  unInitializedThreadCount = 0;
  WorkerState::setCurrentState( WorkerState::UNKNOWN);
  JTRACE ( "disconnecting from dmtcp coordinator" );
  _coordinatorSocket.close();
}

void dmtcp::DmtcpWorker::interruptCkpthread()
{
  if (pthread_mutex_trylock(&destroyDmtcpWorker) == EBUSY) {
    if (killCkpthread) // if strong symbol defined elsewhere
      killCkpthread();
    else // else trying to call weak symbol, which is undefined
      JASSERT(false).Text("killCkpthread should not be called");
    JASSERT(_real_pthread_mutex_lock(&destroyDmtcpWorker) == 0) (JASSERT_ERRNO);
  }
}

//called after user main()
dmtcp::DmtcpWorker::~DmtcpWorker()
{
  if( exitInProgress() ){
    /*
     * Exit race fixed. If the user threads calls exit(), ~DmtcpWorker() is
     * called.  Now if the ckpt-thread is trying to use DmtcpWorker object
     * while it is being destroyed, there is a problem.
     *
     * The fix here is to raise the flag exitInProgress in the exit() system
     * call wrapper. Later in ~DmtcpWorker() we check if the flag has been
     * raised or not.  If the exitInProgress flag has been raised, it closes
     * the coordinator socket and tries to acquire destroyDmtcpWorker mutex.
     *
     * The ckpt-thread tries to acquire the destroyDmtcpWorker mutex before
     * writing/reading any message to/from coordinator socket while the user
     * threads are running (i.e. messages like DMT_SUSPEND, DMT_SUSPENDED
     * etc.)_. If it fails to acquire the lock, it verifies that the
     * exitInProgress has been raised and performs pthread_exit().
     *
     * As obvious, once the user threads have been suspended the ckpt-thread
     *  releases the destroyDmtcpWorker() mutex and continues normal execution.
     */
    process_dmtcp_event(DMTCP_EVENT_PRE_EXIT, NULL);
    JTRACE ( "exit() in progress, disconnecting from dmtcp coordinator" );
    _coordinatorSocket.close();
    interruptCkpthread();
  }
  cleanupWorker();
}

void processDmtcpCommands(dmtcp::string programName,
                          dmtcp::vector<dmtcp::string>& args)
{
  JASSERT (programName == "dmtcp_coordinator" ||
           programName == "dmtcp_checkpoint"  ||
           programName == "dmtcp_restart"     ||
           programName == "dmtcp_command"     ||
           programName == "mtcp_restart");

  //make sure coordinator connection is closed
  _real_close ( PROTECTED_COORD_FD );

  /*
   * When running gdb or any shell which does a waitpid() on the child
   * processes, executing dmtcp_command from within gdb session / shell results
   * in process getting hung up because:
   *   gdb shell dmtcp_command -c => hangs because gdb forks off a new process
   *   and it does a waitpid  (in which we block signals) ...
   */
  if (programName == "dmtcp_command") {
    pid_t cpid = _real_fork();
    JASSERT (cpid != -1);
    if (cpid != 0) {
      _real_exit(0);
    }
  }

  //now repack args
  char** argv = new char*[args.size() + 1];
  memset ( argv, 0, sizeof ( char* ) * ( args.size() + 1 ) );

  for ( size_t i=0; i< args.size(); ++i ) {
    argv[i] = ( char* ) args[i].c_str();
  }

  JNOTE ( "re-running without checkpointing" ) ( programName );

  //now re-call the command
  restoreUserLDPRELOAD();
  _real_execvp ( jalib::Filesystem::GetProgramPath().c_str(), argv );

  //should be unreachable
  JASSERT ( false ) (jalib::Filesystem::GetProgramPath()) ( argv[0] )
    ( JASSERT_ERRNO ) .Text ( "exec() failed" );
}

static void processSshCommand(dmtcp::string programName,
                              dmtcp::vector<dmtcp::string>& args)
{
  JASSERT ( jalib::Filesystem::GetProgramName() == "ssh" );
  //make sure coordinator connection is closed
  _real_close ( PROTECTED_COORD_FD );

  JASSERT ( args.size() >= 3 ) ( args.size() )
    .Text ( "ssh must have at least 3 args to be wrapped (ie: ssh host cmd)" );

  //find command part
  size_t commandStart = 2;
  for ( size_t i = 1; i < args.size(); ++i )
  {
    if ( args[i][0] != '-' )
    {
      commandStart = i + 1;
      break;
    }
  }
  JASSERT ( commandStart < args.size() && args[commandStart][0] != '-' )
    ( commandStart ) ( args.size() ) ( args[commandStart] )
    .Text ( "failed to parse ssh command line" );

  //find the start of the command
  dmtcp::string& cmd = args[commandStart];


  const char * coordinatorAddr      = getenv ( ENV_VAR_NAME_HOST );
  const char * coordinatorPortStr   = getenv ( ENV_VAR_NAME_PORT );
  const char * sigckpt              = getenv ( ENV_VAR_SIGCKPT );
  const char * compression          = getenv ( ENV_VAR_COMPRESSION );
#ifdef HBICT_DELTACOMP
  const char * deltacompression     = getenv ( ENV_VAR_DELTACOMPRESSION );
#endif
  const char * ckptOpenFiles        = getenv ( ENV_VAR_CKPT_OPEN_FILES );
  const char * ckptDir              = getenv ( ENV_VAR_CHECKPOINT_DIR );
  const char * tmpDir               = getenv ( ENV_VAR_TMPDIR );
  if (getenv(ENV_VAR_QUIET)) {
    jassert_quiet                   = *getenv ( ENV_VAR_QUIET ) - '0';
  } else {
    jassert_quiet = 0;
  }

  //modify the command

  //dmtcp::string prefix = "env ";

  dmtcp::string prefix = DMTCP_CHECKPOINT_CMD " --ssh-slave ";


  if ( coordinatorAddr != NULL )
    prefix += dmtcp::string() + "--host " + coordinatorAddr    + " ";
  if ( coordinatorPortStr != NULL )
    prefix += dmtcp::string() + "--port " + coordinatorPortStr + " ";
  if ( sigckpt != NULL )
    prefix += dmtcp::string() + "--mtcp-checkpoint-signal "    + sigckpt + " ";
  if ( ckptDir != NULL )
    prefix += dmtcp::string() + "--ckptdir " + ckptDir         + " ";
  if ( tmpDir != NULL )
    prefix += dmtcp::string() + "--tmpdir " + tmpDir           + " ";
  if ( ckptOpenFiles != NULL )
    prefix += dmtcp::string() + "--checkpoint-open-files"      + " ";

  if ( compression != NULL ) {
    if ( strcmp ( compression, "0" ) == 0 )
      prefix += "--no-gzip ";
    else
      prefix += "--gzip ";
  }

#ifdef HBICT_DELTACOMP
  if (deltacompression != NULL) {
    if (strcmp(deltacompression, "0") == 0)
      prefix += "--no-hbict ";
    else
      prefix += "--hbict ";
  }
#endif

  // process command
  size_t semipos, pos;
  size_t actpos = dmtcp::string::npos;
  for(semipos = 0; (pos = cmd.find(';',semipos+1)) != dmtcp::string::npos;
      semipos = pos, actpos = pos);

  if( actpos > 0 && actpos != dmtcp::string::npos ){
    cmd = cmd.substr(0,actpos+1) + prefix + cmd.substr(actpos+1);
  } else {
    cmd = prefix + cmd;
  }

  //now repack args
  dmtcp::string newCommand = "";
  char** argv = new char*[args.size() +2];
  memset ( argv,0,sizeof ( char* ) * ( args.size() +2 ) );

  for ( size_t i=0; i< args.size(); ++i )
  {
    argv[i] = ( char* ) args[i].c_str();
    newCommand += args[i] + ' ';
  }

  JNOTE ( "re-running SSH with checkpointing" ) ( newCommand );

  restoreUserLDPRELOAD();
  //now re-call ssh
  _real_execvp ( argv[0], argv );

  //should be unreachable
  JASSERT ( false ) ( cmd ) ( JASSERT_ERRNO ).Text ( "exec() failed" );
}


const dmtcp::UniquePid& dmtcp::DmtcpWorker::coordinatorId() const
{
  return _coordinatorId;
}


void dmtcp::DmtcpWorker::waitForCoordinatorMsg(dmtcp::string msgStr,
                                               DmtcpMessageType type )
{
  if ( type == DMT_DO_SUSPEND ) {
    if ( pthread_mutex_trylock(&destroyDmtcpWorker) != 0 ) {
      JTRACE ( "User thread is performing exit()."
               " ckpt thread exit()ing as well" );
      pthread_exit(NULL);
    }
    if ( exitInProgress() ) {
      JASSERT(_real_pthread_mutex_unlock(&destroyDmtcpWorker)==0)(JASSERT_ERRNO);
      pthread_exit(NULL);
    }
  }

  dmtcp::DmtcpMessage msg;

  msg.type = DMT_OK;
  msg.state = WorkerState::currentState();
  _coordinatorSocket << msg;

  JTRACE ( "waiting for " + msgStr + " message" );

  do {
    msg.poison();
    _coordinatorSocket >> msg;

    if ( type == DMT_DO_SUSPEND && exitInProgress() ) {
      JASSERT(_real_pthread_mutex_unlock(&destroyDmtcpWorker)==0)(JASSERT_ERRNO);
      pthread_exit(NULL);
    }

    msg.assertValid();

    if ( msg.type == DMT_KILL_PEER ) {
      JTRACE ( "Received KILL Message from coordinator, exiting" );
      _exit ( 0 );
    }

    // The ckpt thread can receive multiple DMT_RESTORE_WAITING or
    // DMT_FORCE_RESTART messages while waiting for a DMT_DO_REFILL message, we
    // need to ignore them and wait for the DMT_DO_REFILL message to arrive.
    if ( type != DMT_DO_REFILL && type != DMT_DO_REGISTER_NAME_SERVICE_DATA &&
         type != DMT_DO_SEND_QUERIES ) {
      break;
    }

  } while((type == DMT_DO_REFILL
           || type == DMT_DO_REGISTER_NAME_SERVICE_DATA
           || type == DMT_DO_SEND_QUERIES)
          && (msg.type == DMT_RESTORE_WAITING ||
              msg.type == DMT_FORCE_RESTART));

  JASSERT ( msg.type == type ) ( msg.type );

  // Coordinator sends some computation information along with the SUSPEND
  // message. Extracting that.
  if ( type == DMT_DO_SUSPEND ) {
    JTRACE ( "Computation information" ) ( msg.compGroup ) ( msg.params[0] );
    JASSERT ( theCheckpointState != NULL );
    theCheckpointState->numPeers(msg.params[0]);
    theCheckpointState->compGroup(msg.compGroup);
    compGroup = msg.compGroup;
  }
}

void dmtcp::DmtcpWorker::waitForStage1Suspend()
{
  JTRACE ( "running" );

  WorkerState::setCurrentState ( WorkerState::RUNNING );

  /*
   * Its only use is to inform the user thread (waiting in DmtcpWorker
   * constructor) that the checkpoint thread has finished initialization. This
   * is to serialize DmtcpWorker-Constructor(), mtcp_init(), checkpoint-thread
   * initialization and user main(). As obvious, this is only effective when
   * the process is being initialized.
   */
  if (!_checkpointThreadInitialized) {
    /*
     * We should not call this function any higher in the logic because it
     * calls setenv() and if it is running under bash, then it getenv() will
     * not work between the call to setenv() and bash main().
     */
    restoreUserLDPRELOAD();
    _checkpointThreadInitialized = true;
  }

  // Create signature file which could then be used by an outside process to
  // check if the process restarted successfully.
  if ( 0 && compGroup != UniquePid() ) {
    dmtcp::string signatureFile = UniquePid::getTmpDir() + "/"
                                + compGroup.toString() + "-"
#ifdef PID_VIRTUALIZATION
                                + jalib::XToString ( _real_getppid() );
#else
                                + jalib::XToString ( getppid() );
#endif
    JTRACE("creating signature file") (signatureFile)(_real_getpid());
    int fd = _real_open ( signatureFile.c_str(), O_CREAT|O_WRONLY, 0600 );
    JASSERT ( fd != -1 ) ( fd ) ( signatureFile )
      .Text ( "Unable to create signature file" );
    dmtcp::string pidstr = jalib::XToString(_real_getpid());
    ssize_t ret = Util::writeAll(fd, pidstr.c_str(), pidstr.length()+1);
    JASSERT( (ssize_t)pidstr.length() + 1 == ret ) ( pidstr.length()+1 );
    _real_close(fd);
  }

  if ( theCheckpointState != NULL ) {
    delete theCheckpointState;
    theCheckpointState = NULL;
  }

  theCheckpointState = new ConnectionState();

#ifdef EXTERNAL_SOCKET_HANDLING
  JASSERT ( _waitingForExternalSocketsToClose == true ||
             externalTcpConnections.empty() == true );

  while ( externalTcpConnections.empty() == false ) {
    JTRACE("Waiting for externalSockets toClose")
          (_waitingForExternalSocketsToClose);
    sleep ( 1 );
  }
  if ( _waitingForExternalSocketsToClose == true ) {
    DmtcpMessage msg ( DMT_EXTERNAL_SOCKETS_CLOSED );
    _coordinatorSocket << msg;
    _waitingForExternalSocketsToClose = false;
    JTRACE("externalSocketsClosed") (_waitingForExternalSocketsToClose);
  }
#endif

  waitForCoordinatorMsg ( "SUSPEND", DMT_DO_SUSPEND );

  JTRACE ( "got SUSPEND message, waiting for dmtcp_lock():"
	   " to get synchronized with _runCoordinatorCmd if we use DMTCP API" );
  _dmtcp_lock();
  // TODO: may be it is better to move unlock to more appropriate place.
  // For example after suspending all threads
  _dmtcp_unlock();


  JTRACE ( "got SUSPEND message, waiting for lock(&theCkptCanStart)" );
  JASSERT(_real_pthread_mutex_lock(&theCkptCanStart)==0)(JASSERT_ERRNO);

  JTRACE ( "got SUSPEND message,"
           " waiting for other threads to exit DMTCP-Wrappers" );
  JASSERT(_real_pthread_rwlock_wrlock(&theWrapperExecutionLock) == 0)(JASSERT_ERRNO);
  JTRACE ( "got SUSPEND message,"
           " waiting for newly created threads to finish initialization" )
         (unInitializedThreadCount);
  waitForThreadsToFinishInitialization();

  JTRACE ( "Starting checkpoint, suspending..." );
}

#ifdef EXTERNAL_SOCKET_HANDLING
bool dmtcp::DmtcpWorker::waitForStage2Checkpoint()
#else
void dmtcp::DmtcpWorker::waitForStage2Checkpoint()
#endif
{
  WorkerState::setCurrentState ( WorkerState::SUSPENDED );
  JTRACE ( "suspended" );

  if ( exitInProgress() ) {
    JASSERT(_real_pthread_mutex_unlock(&destroyDmtcpWorker)==0)(JASSERT_ERRNO);
    pthread_exit(NULL);
  }

  JASSERT(_coordinatorSocket.isValid());

  JASSERT(_real_pthread_mutex_unlock(&destroyDmtcpWorker)==0)(JASSERT_ERRNO);
  JASSERT(_real_pthread_rwlock_unlock(&theWrapperExecutionLock) == 0)(JASSERT_ERRNO);
  JASSERT(_real_pthread_mutex_unlock(&theCkptCanStart)==0)(JASSERT_ERRNO);

  process_dmtcp_event(DMTCP_EVENT_POST_SUSPEND, NULL);

  theCheckpointState->preLockSaveOptions();

  waitForCoordinatorMsg ( "FD_LEADER_ELECTION", DMT_DO_FD_LEADER_ELECTION );

  JTRACE ( "locking..." );
  JASSERT ( theCheckpointState != NULL );
  theCheckpointState->preCheckpointFdLeaderElection();
  JTRACE ( "locked" );

#ifdef PID_VIRTUALIZATION
  /*
   * Save first 2 * sizeof(pid_t) bytes of each shared memory area and fill it
   * with all zeros.
   */
  SysVIPC::instance().prepareForLeaderElection();
#endif

  WorkerState::setCurrentState ( WorkerState::FD_LEADER_ELECTION );

  process_dmtcp_event(DMTCP_EVENT_POST_LEADER_ELECTION, NULL);

#ifdef EXTERNAL_SOCKET_HANDLING
  if ( waitForStage2bCheckpoint() == false ) {
    return false;
  }
#else
  waitForCoordinatorMsg ( "DRAIN", DMT_DO_DRAIN );
#endif

  JTRACE ( "draining..." );
  theCheckpointState->preCheckpointDrain();
  JTRACE ( "drained" );

#ifdef PID_VIRTUALIZATION
  /*
   * write pid at offset 0. Also write pid at offset sizeof(pid_t) if this
   * process is the creator of this memory area. After the leader election
   * barrier, the leader of the shared-memory object is the creator of the
   * object. If the creator process is missing, then the leader process is the
   * process whose pid is stored at offset 0
   */
  SysVIPC::instance().leaderElection();
#endif

  WorkerState::setCurrentState ( WorkerState::DRAINED );

  process_dmtcp_event(DMTCP_EVENT_POST_DRAIN, NULL);

  waitForCoordinatorMsg ( "CHECKPOINT", DMT_DO_CHECKPOINT );
  JTRACE ( "got checkpoint message" );

#if HANDSHAKE_ON_CHECKPOINT == 1
  //handshake is done after one barrier after drain
  JTRACE ( "beginning handshakes" );
  theCheckpointState->preCheckpointHandshakes(coordinatorId());
  JTRACE ( "handshaking done" );
#endif

//   JTRACE("writing *.dmtcp file");
//   theCheckpointState->outputDmtcpConnectionTable();

#ifdef PID_VIRTUALIZATION
  dmtcp::VirtualPidTable::instance().preCheckpoint();
  SysVIPC::instance().preCheckpoint();
#endif

  process_dmtcp_event(DMTCP_EVENT_PRE_CHECKPOINT, NULL);

#ifdef EXTERNAL_SOCKET_HANDLING
  return true;
#endif
}

#ifdef EXTERNAL_SOCKET_HANDLING
bool dmtcp::DmtcpWorker::waitForStage2bCheckpoint()
{
  waitForCoordinatorMsg ( "PEER_LOOKUP", DMT_DO_PEER_LOOKUP );
  JTRACE ( "Looking up Socket Peers..." );
  theTcpConnections.clear();
  theCheckpointState->preCheckpointPeerLookup(theTcpConnections);
  sendPeerLookupRequest(theTcpConnections);
  JTRACE ( "Done Socket Peer Lookup" );


  WorkerState::setCurrentState ( WorkerState::PEER_LOOKUP_COMPLETE );

  {
    dmtcp::DmtcpMessage msg;

    msg.type = DMT_OK;
    msg.state = WorkerState::currentState();
    _coordinatorSocket << msg;

    JTRACE ( "waiting for DRAIN/RESUME message" );

    do {
      msg.poison();
      _coordinatorSocket >> msg;
      msg.assertValid();

      if ( msg.type == DMT_KILL_PEER ) {
        JTRACE ( "Received KILL Message from coordinator, exiting" );
        _exit ( 0 );
      }
      JTRACE ( "received message" ) (msg.type );
      if ( msg.type != DMT_UNKNOWN_PEER )
        break;

      JTRACE ("received DMT_UNKNOWN_PEER message") (msg.conId);

      TcpConnection* con =
        (TcpConnection*) &( ConnectionList::instance() [msg.conId] );
      con->markExternal();
      externalTcpConnections.push_back(msg.conId);
      _waitingForExternalSocketsToClose = true;

    } while ( msg.type == DMT_UNKNOWN_PEER );

    JASSERT ( msg.type == DMT_DO_DRAIN || msg.type == DMT_DO_RESUME )
            ( msg.type );

    ConnectionList& connections = ConnectionList::instance();

    // Tcp Accept and Connect connection with PeerType UNKNOWN should be marked as INTERNAL
    for ( ConnectionList::iterator i = connections.begin()
        ; i!= connections.end()
        ; ++i )
    {
      Connection* con =  i->second;
      if ( con->conType() == Connection::TCP ) {
        TcpConnection* tcpCon = (TcpConnection *) con;
        if ( (tcpCon->tcpType() == TcpConnection::TCP_ACCEPT ||
             tcpCon->tcpType() == TcpConnection::TCP_CONNECT) &&
             tcpCon->peerType() == TcpConnection::PEER_UNKNOWN )
          tcpCon->markInternal();
      }
    }
    if ( msg.type == DMT_DO_RESUME ) {
      JTRACE ( "Peer Lookup not complete, skipping checkpointing \n\n\n\n\n");
      return false;
    }

    JASSERT (msg.type == DMT_DO_DRAIN);
  }
}

void dmtcp::DmtcpWorker::sendPeerLookupRequest (dmtcp::vector<TcpConnectionInfo>& conInfoTable )
{
  for (int i = 0; i < conInfoTable.size(); ++i) {
    DmtcpMessage msg;
    msg.type = DMT_PEER_LOOKUP;
    msg.localAddr    = conInfoTable[i].localAddr();
    msg.remoteAddr   = conInfoTable[i].remoteAddr();
    msg.localAddrlen = conInfoTable[i].addrlen();
    msg.conId        = conInfoTable[i].conId();

    _coordinatorSocket << msg;
  }
}

bool dmtcp::DmtcpWorker::waitingForExternalSocketsToClose() {
  return _waitingForExternalSocketsToClose;
}
#endif

void dmtcp::DmtcpWorker::writeCheckpointPrefix ( int fd )
{
  const int len = strlen(DMTCP_FILE_HEADER);
  JASSERT(write(fd, DMTCP_FILE_HEADER, len)==len);

  theCheckpointState->outputDmtcpConnectionTable(fd, argvSize(), envSize());
}

void dmtcp::DmtcpWorker::sendCkptFilenameToCoordinator()
{
  // Tell coordinator to record our filename in the restart script
  dmtcp::string ckptFilename = dmtcp::UniquePid::checkpointFilename();
  dmtcp::string hostname = jalib::Filesystem::GetCurrentHostname();
  JTRACE ( "recording filenames" ) ( ckptFilename ) ( hostname );
  dmtcp::DmtcpMessage msg;
  msg.type = DMT_CKPT_FILENAME;
  msg.extraBytes = ckptFilename.length() +1 + hostname.length() +1;
  _coordinatorSocket << msg;
  _coordinatorSocket.writeAll ( ckptFilename.c_str(), ckptFilename.length() +1 );
  _coordinatorSocket.writeAll ( hostname.c_str(),     hostname.length() +1 );
}


// At restart, the HOST/PORT used by dmtcp_coordinator could be different then
// those at checkpoint time. This could cause the child processes created after
// restart to fail to connect to the coordinator.
void dmtcp::DmtcpWorker::updateCoordinatorHostAndPortEnv()
{
  struct sockaddr addr;
  socklen_t addrLen = sizeof addr;
  JASSERT (0 == getpeername(_coordinatorSocket.sockfd(), &addr, &addrLen))
    (JASSERT_ERRNO);

  /* If the current coordinator is running on a HOST/PORT other than the
   * pre-checkpoint HOST/PORT, we need to update the environment variables
   * pointing to the coordinator HOST/PORT. This is needed if the new
   * coordinator has been moved around.
   */

  const char * origCoordAddr = getenv ( ENV_VAR_NAME_HOST );
  const char * origCoordPortStr = getenv ( ENV_VAR_NAME_PORT );
  if (origCoordAddr == NULL) origCoordAddr = DEFAULT_HOST;
  int origCoordPort = origCoordPortStr==NULL ? DEFAULT_PORT : jalib::StringToInt ( origCoordPortStr );

  jalib::JSockAddr originalCoordinatorAddr(origCoordAddr, origCoordPort);
  if (addrLen != originalCoordinatorAddr.addrlen() ||
      memcmp(originalCoordinatorAddr.addr(), &addr, addrLen) != 0) {

    JASSERT (addr.sa_family == AF_INET) (addr.sa_family)
      .Text ("Coordinator socket always uses IPV4 sockets");

    char currHost[1024];
    char currPort[16];

    int res = getnameinfo(&addr, addrLen, currHost, sizeof currHost,
                          currPort, sizeof currPort, NI_NUMERICSERV);
    JASSERT (res == 0) (currHost) (currPort) (gai_strerror(res))
      .Text ("getnameinfo(... currHost, ..., currPort,...) failed");

    JTRACE ("Coordinator running at a different location")
      (origCoordAddr) (origCoordPort) (currHost) (currPort);

    JASSERT (0 == setenv (ENV_VAR_NAME_HOST, currHost, 1)) (JASSERT_ERRNO);
    JASSERT( 0 == setenv (ENV_VAR_NAME_PORT, currPort, 1)) (JASSERT_ERRNO);
  }
}

void dmtcp::DmtcpWorker::postRestart()
{
  JTRACE("begin postRestart()");

  WorkerState::setCurrentState(WorkerState::RESTARTING);
  recvCoordinatorHandshake();

  JASSERT ( theCheckpointState != NULL );
  theCheckpointState->postRestart();

#ifdef PID_VIRTUALIZATION
  if ( jalib::Filesystem::GetProgramName() == "screen" )
    send_sigwinch = 1;
  // With hardstatus (bottom status line), screen process has diff. size window
  // Must send SIGWINCH to adjust it.
  // MTCP will send SIGWINCH to process on restart.  This will force 'screen'
  // to execute ioctl wrapper.  The wrapper will report a changed winsize,
  // so that 'screen' must re-initialize the screen (scrolling regions, etc.).
  // The wrapper will also send a second SIGWINCH.  Then 'screen' will
  // call ioctl and get the correct window size and resize again.
  // We can't just send two SIGWINCH's now, since window size has not
  // changed yet, and 'screen' will assume that there's nothing to do.

  dmtcp::VirtualPidTable::instance().postRestart();
  SysVIPC::instance().postRestart();
#endif

  process_dmtcp_event(DMTCP_EVENT_POST_RESTART, NULL);
}

void dmtcp::DmtcpWorker::waitForStage3Refill( bool isRestart )
{
  JTRACE ( "checkpointed" );

  WorkerState::setCurrentState ( WorkerState::CHECKPOINTED );

#ifdef COORD_NAMESERVICE
  waitForCoordinatorMsg("REGISTER_NAME_SERVICE_DATA",
                          DMT_DO_REGISTER_NAME_SERVICE_DATA );
  process_dmtcp_event(DMTCP_EVENT_REGISTER_NAME_SERVICE_DATA, NULL);
  JTRACE("Key Value Pairs registered with the coordinator");
  WorkerState::setCurrentState(WorkerState::NAME_SERVICE_DATA_REGISTERED);

  waitForCoordinatorMsg("SEND_QUERIES", DMT_DO_SEND_QUERIES);
  process_dmtcp_event(DMTCP_EVENT_SEND_QUERIES, NULL);
  JTRACE("Queries sent to the coordinator");
  WorkerState::setCurrentState(WorkerState::DONE_QUERYING);
#endif

  waitForCoordinatorMsg ( "REFILL", DMT_DO_REFILL );

  JASSERT ( theCheckpointState != NULL );
  theCheckpointState->postCheckpoint(isRestart);
  delete theCheckpointState;
  theCheckpointState = NULL;

#ifdef PID_VIRTUALIZATION
  SysVIPC::instance().postCheckpoint();
#endif
  if (!isRestart) {
    process_dmtcp_event(DMTCP_EVENT_POST_CHECKPOINT, NULL);
  }
}

void dmtcp::DmtcpWorker::waitForStage4Resume()
{
  JTRACE ( "refilled" );
  WorkerState::setCurrentState ( WorkerState::REFILLED );
  waitForCoordinatorMsg ( "RESUME", DMT_DO_RESUME );
  JTRACE ( "got resume message" );

#ifdef PID_VIRTUALIZATION
  SysVIPC::instance().preResume();
#endif
}

void dmtcp::DmtcpWorker::restoreVirtualPidTable()
{
#ifdef PID_VIRTUALIZATION
  dmtcp::VirtualPidTable::instance().readPidMapsFromFile();
  dmtcp::VirtualPidTable::instance().restoreProcessGroupInfo();
#endif
}

void dmtcp::DmtcpWorker::delayCheckpointsLock(){
  JASSERT(_real_pthread_mutex_lock(&theCkptCanStart)==0)(JASSERT_ERRNO);
}

void dmtcp::DmtcpWorker::delayCheckpointsUnlock(){
  JASSERT(_real_pthread_mutex_unlock(&theCkptCanStart)==0)(JASSERT_ERRNO);
}

LIB_PRIVATE __thread int thread_performing_dlopen_dlsym = 0;
// XXX: Handle deadlock error code
// NOTE: Don't do any fancy stuff in this wrapper which can cause the process to go into DEADLOCK
bool dmtcp::DmtcpWorker::wrapperExecutionLockLock()
{
#ifdef PTRACE
  return false;
#endif
  int saved_errno = errno;
  bool lockAcquired = false;
  if ( dmtcp::WorkerState::currentState() == dmtcp::WorkerState::RUNNING &&
       thread_performing_dlopen_dlsym == 0 ) {
    int retVal = _real_pthread_rwlock_rdlock(&theWrapperExecutionLock);
    if ( retVal != 0 && retVal != EDEADLK ) {
      fprintf(stderr, "ERROR %s: Failed to acquire lock", __PRETTY_FUNCTION__ );
      _exit(1);
    }
    // retVal should always be 0 (success) here.
    lockAcquired = retVal == 0 ? true : false;
  }
  errno = saved_errno;
  return lockAcquired;
}

// NOTE: Don't do any fancy stuff in this wrapper which can cause the process to go into DEADLOCK
void dmtcp::DmtcpWorker::wrapperExecutionLockUnlock()
{
  int saved_errno = errno;
  if ( dmtcp::WorkerState::currentState() != dmtcp::WorkerState::RUNNING ) {
    printf ( "ERROR: DmtcpWorker::wrapperExecutionLockUnlock: This process is not in \n"
             "RUNNING state and yet this thread managed to acquire the wrapperExecutionLock.\n"
             "This should not be happening, something is wrong." );
    _exit(1);
  }
  if ( _real_pthread_rwlock_unlock(&theWrapperExecutionLock) != 0) {
    fprintf(stderr, "ERROR %s: Failed to release lock", __PRETTY_FUNCTION__ );
    _exit(1);
    }
  errno = saved_errno;
}

// GNU g++ uses __thread.  But the C++0x standard says to use thread_local.
//   If your compiler fails here, you can: change "__thread" to "thread_local";
//   or delete "__thread" (but if user code calls these routines from multiple
//   threads, it will not be thread-safe).
//   In GCC 4.3 and later, g++ supports -std=c++0x and -std=g++0x.
static __thread bool dmtcp_module_ckpt_lock;
extern "C"
void dmtcp_module_disable_ckpt() {
  WRAPPER_EXECUTION_DISABLE_CKPT();
  dmtcp_module_ckpt_lock = __wrapperExecutionLockAcquired;
}
extern "C"
void dmtcp_module_enable_ckpt() {
  bool __wrapperExecutionLockAcquired = dmtcp_module_ckpt_lock;
  WRAPPER_EXECUTION_ENABLE_CKPT();
}


void dmtcp::DmtcpWorker::waitForThreadsToFinishInitialization()
{
  while (unInitializedThreadCount != 0) {
    struct timespec sleepTime = {0, 10*1000*1000};
    JTRACE("sleeping")(sleepTime.tv_nsec);
    nanosleep(&sleepTime, NULL);
  }
}

void dmtcp::DmtcpWorker::incrementUninitializedThreadCount()
{
  int saved_errno = errno;
  if ( dmtcp::WorkerState::currentState() == dmtcp::WorkerState::RUNNING ) {
    JASSERT(_real_pthread_mutex_lock(&unInitializedThreadCountLock) == 0)
      (JASSERT_ERRNO);
    unInitializedThreadCount++;
    //JTRACE(":") (unInitializedThreadCount);
    JASSERT(_real_pthread_mutex_unlock(&unInitializedThreadCountLock) == 0)
      (JASSERT_ERRNO);
  }
  errno = saved_errno;
}

void dmtcp::DmtcpWorker::decrementUninitializedThreadCount()
{
  int saved_errno = errno;
  if ( dmtcp::WorkerState::currentState() == dmtcp::WorkerState::RUNNING ) {
    JASSERT(_real_pthread_mutex_lock(&unInitializedThreadCountLock) == 0)
      (JASSERT_ERRNO);
    JASSERT(unInitializedThreadCount > 0) (unInitializedThreadCount);
    unInitializedThreadCount--;
    //JTRACE(":") (unInitializedThreadCount);
    JASSERT(_real_pthread_mutex_unlock(&unInitializedThreadCountLock) == 0)
      (JASSERT_ERRNO);
  }
  errno = saved_errno;
}
