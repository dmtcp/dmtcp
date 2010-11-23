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

#include <stdarg.h>
#include <stdlib.h>
#include <vector>
#include <list>
#include <string>
#include <fcntl.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/version.h>
#include <limits.h>
#include "uniquepid.h"
#include "dmtcpworker.h"
#include "dmtcpmessagetypes.h"
#include "protectedfds.h"
#include "constants.h"
#include "connectionmanager.h"
#include "syscallwrappers.h"
#include "sysvipc.h"
#include "util.h"
#include  "../jalib/jassert.h"
#include  "../jalib/jconvert.h"
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
#include "synchronizationlogging.h"
#include <sys/mman.h>
#include <sys/syscall.h>
#include <malloc.h>
#include <execinfo.h>
// TODO: hack to be able to compile this (fcntl wrapper).
#define open _libc_open
#define open64 _libc_open64
#include <fcntl.h>
#undef open
#undef open64

// Limit at which we promote malloc() -> mmap() (in bytes):
#define SYNCHRONIZATION_MALLOC_LIMIT 1024
static int initHook = 0;
#ifdef x86_64
struct alloc_header {
  unsigned int is_mmap : 1;
  unsigned int chunk_size : 63;
  int signature;
};
#else
struct alloc_header {
  unsigned int is_mmap : 1;
  unsigned int chunk_size : 31;
  int signature;
};
#endif
#define ALLOC_HEADER_SIZE sizeof(struct alloc_header)
#define REAL_ADDR(addr) ((char *)(addr) - ALLOC_HEADER_SIZE)
#define REAL_SIZE(size) ((size) + ALLOC_HEADER_SIZE)
#define USER_ADDR(addr) ((char *)(addr) + ALLOC_HEADER_SIZE)
#define USER_SIZE(size) ((size) - ALLOC_HEADER_SIZE)

static void my_init_hooks (void);
static void *my_malloc_hook (size_t, const void *);
static void my_free_hook (void*, const void *);
static void *(*old_malloc_hook) (size_t, const void *);
static void  (*old_free_hook) (void*, const void *);
//static void *_wrapped_malloc(size_t size);
//static void _wrapped_free(void *ptr);
/* Override initializing hook from the C library. */
//void (*__malloc_initialize_hook) (void) = my_init_hooks;

static pthread_mutex_t getline_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t fgets_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t getc_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t hook_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t allocation_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t open_lock = PTHREAD_MUTEX_INITIALIZER;

static inline void memfence() {  asm volatile ("mfence" ::: "memory"); }
#endif

extern "C" void exit ( int status )
{
  dmtcp::DmtcpWorker::setExitInProgress();
  _real_exit ( status );
  for (;;); // Without this, gcc emits warning:  `noreturn' fnc does return
}

#ifdef EXTERNAL_SOCKET_HANDLING
extern dmtcp::vector <dmtcp::ConnectionIdentifier> externalTcpConnections;
static void processClose(dmtcp::ConnectionIdentifier conId)
{
  if ( dmtcp::DmtcpWorker::waitingForExternalSocketsToClose() == true ) {
    dmtcp::vector <dmtcp::ConnectionIdentifier>::iterator i = externalTcpConnections.begin();
    for ( i = externalTcpConnections.begin(); i != externalTcpConnections.end(); ++i ) {
      if ( conId == *i ) {
        externalTcpConnections.erase(i);
        break;
      }
    }
    if ( externalTcpConnections.empty() == true ) {
    }
    sleep(4);
  }
}
#endif

extern "C" int close ( int fd )
{
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr) || dmtcp::ProtectedFDs::isProtected(fd)) {
    _real_pthread_mutex_lock(&fd_change_mutex);
    int retval = _real_close(fd);
    _real_pthread_mutex_unlock(&fd_change_mutex);
    return retval;
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    _real_pthread_mutex_lock(&fd_change_mutex);
    int retval = _real_close(fd);
    _real_pthread_mutex_unlock(&fd_change_mutex);
    return retval;
  }
  int retval = 0;
  log_entry_t my_entry = create_close_entry(my_clone_id, close_event, fd);
  log_entry_t my_return_entry = create_close_entry(my_clone_id, close_event_return, fd);

  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &close_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &close_turn_check);
    retval = GET_COMMON(currentLogEntry, retval);
    if (retval == -1) {
      errno = GET_COMMON(currentLogEntry, my_errno);
    }
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _real_close(fd);
    SET_COMMON(my_return_entry, retval);
    if (retval == -1) {
      SET_COMMON2(my_return_entry, my_errno, errno);
    }
    addNextLogEntry(my_return_entry);
  }
  return retval;
#else
  if ( dmtcp::ProtectedFDs::isProtected ( fd ) )
  {
    JTRACE ( "blocked attempt to close protected fd" ) ( fd );
    errno = EBADF;
    return -1;
  }

#ifdef EXTERNAL_SOCKET_HANDLING
  dmtcp::ConnectionIdentifier conId;
  if ( dmtcp::WorkerState::currentState() == dmtcp::WorkerState::RUNNING &&
       dmtcp::DmtcpWorker::waitingForExternalSocketsToClose() == true &&
       dup2(fd,fd) != -1 ) {
    conId = dmtcp::KernelDeviceToConnection::instance().retrieve(fd).id();
  }
#endif

  int rv = _real_close ( fd );

#ifdef EXTERNAL_SOCKET_HANDLING
  if (rv == 0) {
    processClose(conId);
  }
#endif

  return rv;
#endif //SYNCHRONIZATION_LOG_AND_REPLAY
}

#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
static int _almost_real_fclose(FILE *fp)
{
  int fd = fileno(fp);
  if ( dmtcp::ProtectedFDs::isProtected ( fd ) )
  {
    JTRACE ( "blocked attempt to fclose protected fd" ) ( fd );
    errno = EBADF;
    return -1;
  }

#ifdef EXTERNAL_SOCKET_HANDLING
  dmtcp::ConnectionIdentifier conId;

  if ( dmtcp::WorkerState::currentState() == dmtcp::WorkerState::RUNNING &&
       dmtcp::DmtcpWorker::waitingForExternalSocketsToClose() == true &&
       dup2(fd,fd) != -1 ) {
    conId = dmtcp::KernelDeviceToConnection::instance().retrieve(fd).id();
  }
#endif

  int rv = _real_fclose(fp);

#ifdef EXTERNAL_SOCKET_HANDLING
  if (rv == 0 ) {
    processClose(conId);
  }
#endif
  return rv;
}
#endif

extern "C" int fclose(FILE *fp)
{
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
  WRAPPER_HEADER(int, fclose, _almost_real_fclose, fp);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &fclose_turn_check);
    getNextLogEntry();
    free(fp);
    waitForTurn(my_return_entry, &fclose_turn_check);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    WRAPPER_LOG(_almost_real_fclose, fp);
  }
  return retval;
#else
  int fd = fileno(fp);
  if ( dmtcp::ProtectedFDs::isProtected ( fd ) )
  {
    JTRACE ( "blocked attempt to fclose protected fd" ) ( fd );
    errno = EBADF;
    return -1;
  }

#ifdef EXTERNAL_SOCKET_HANDLING
  dmtcp::ConnectionIdentifier conId;

  if ( dmtcp::WorkerState::currentState() == dmtcp::WorkerState::RUNNING &&
       dmtcp::DmtcpWorker::waitingForExternalSocketsToClose() == true &&
       dup2(fd,fd) != -1 ) {
    conId = dmtcp::KernelDeviceToConnection::instance().retrieve(fd).id();
  }
#endif

  int rv = _real_fclose(fp);

#ifdef EXTERNAL_SOCKET_HANDLING
  if (rv == 0 ) {
    processClose(conId);
  }
#endif
  return rv;
#endif
}

/* epoll is currently not supported by DMTCP */
extern "C" int epoll_create(int size)
{
  JWARNING (false) .Text("epoll is currently not supported by DMTCP.");
  errno = EPERM;
  return -1;
}

extern "C" int socketpair ( int d, int type, int protocol, int sv[2] )
{
  WRAPPER_EXECUTION_DISABLE_CKPT();

  JASSERT ( sv != NULL );
  int rv = _real_socketpair ( d,type,protocol,sv );
  JTRACE ( "socketpair()" ) ( sv[0] ) ( sv[1] );

  dmtcp::TcpConnection *a, *b;

  a = new dmtcp::TcpConnection ( d, type, protocol );
  a->onConnect();
  b = new dmtcp::TcpConnection ( *a, a->id() );

  dmtcp::KernelDeviceToConnection::instance().create ( sv[0] , a );
  dmtcp::KernelDeviceToConnection::instance().create ( sv[1] , b );

  WRAPPER_EXECUTION_ENABLE_CKPT();

  return rv;
}

extern "C" int pipe ( int fds[2] )
{
  JTRACE ( "promoting pipe() to socketpair()" );
  //just promote pipes to socketpairs
  return socketpair ( AF_UNIX, SOCK_STREAM, 0, fds );
}

# if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
// pipe2 appeared in Linux 2.6.27
extern "C" int pipe2 ( int fds[2], int flags )
{
  JTRACE ( "promoting pipe2() to socketpair()" );
  //just promote pipes to socketpairs
  int newFlags = 0;
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23)
  if (flags & O_NONBLOCK != 0) newFlags |= SOCK_NONBLOCK;
  if (flags & O_CLOEXEC != 0)  newFlags |= SOCK_CLOEXEC;
#endif
  return socketpair ( AF_UNIX, SOCK_STREAM | newFlags, 0, fds );
}
#endif


static int ptsname_r_work ( int fd, char * buf, size_t buflen )
{
  JTRACE ( "Calling ptsname_r" );

  dmtcp::Connection* c = &dmtcp::KernelDeviceToConnection::instance().retrieve ( fd );
  dmtcp::PtyConnection* ptyCon = (dmtcp::PtyConnection*) c;

  dmtcp::string uniquePtsName = ptyCon->uniquePtsName();

  JTRACE("ptsname_r") (uniquePtsName);

  if ( uniquePtsName.length() >= buflen )
  {
    JWARNING ( false ) ( uniquePtsName ) ( uniquePtsName.length() ) ( buflen )
      .Text ( "fake ptsname() too long for user buffer" );
    errno = ERANGE;
    return -1;
  }

  strcpy ( buf, uniquePtsName.c_str() );

  return 0;
}

extern "C" char *ptsname ( int fd )
{
  /* No need to acquire Wrapper Protection lock since it will be done in ptsname_r */
  JTRACE ( "ptsname() promoted to ptsname_r()" );
  static char tmpbuf[PATH_MAX];

  if ( ptsname_r ( fd, tmpbuf, sizeof ( tmpbuf ) ) != 0 )
  {
    return NULL;
  }

  return tmpbuf;
}

extern "C" int ptsname_r ( int fd, char * buf, size_t buflen )
{
  WRAPPER_EXECUTION_DISABLE_CKPT();

  int retVal = ptsname_r_work(fd, buf, buflen);

  WRAPPER_EXECUTION_ENABLE_CKPT();

  return retVal;
}

#ifdef PID_VIRTUALIZATION
#include <virtualpidtable.h>

static void updateProcPath ( const char *path, char *newpath )
{
  char temp [ 10 ];
  int index, tempIndex;

  if (  path == "" || path == NULL )
  {
    strcpy(newpath, "");
    return;
  }

  if ( dmtcp::Util::strStartsWith ( path, "/proc/" ) )
  {
    index = 6;
    tempIndex = 0;
    while ( path [ index ] != '/' )
    {
      if ( path [ index ] >= '0' && path [ index ] <= '9' )
        temp [ tempIndex++ ] = path [ index++ ];
      else
      {
        strcpy ( newpath, path );
        return;
      }
    }
    temp [ tempIndex ] = '\0';
    pid_t originalPid = atoi ( temp );
    pid_t currentPid = dmtcp::VirtualPidTable::instance().originalToCurrentPid( originalPid );
    if (currentPid == -1)
      currentPid = originalPid;

    sprintf ( newpath, "/proc/%d%s", currentPid, &path [ index ] );
  }
  else strcpy ( newpath, path );
  return;
}
#else
void updateProcPath ( const char *path, char *newpath )
{
  if (  path == "" || path == NULL ) {
    strcpy( newpath, "" );
    return;
  }
  strcpy ( newpath, path );
  return;
}
#endif

// The current implementation simply increments the last count and returns it.
// Although highly unlikely, this can cause a problem if the counter resets to
// zero. In that case we should have some more sophisticated code which checks
// to see if the value pointed by counter is in use or not.
static int getNextFreeSlavePtyNum()
{
  static int counter = -1;
  counter++;
  JASSERT(counter != -1) .Text ("See the comment above");
  return counter;
}

#define DMTCP_PTS_PREFIX_STR  "dmtcp_"
#define UNIQUE_PTS_PREFIX_STR "/dev/pts/dmtcp_"
//DMTCP_PTS_PREFIX_STR

static int _nextPtmxId()
{
  static int id = 0;
  return id++;
}

// XXX: The current implementation for handling Pseudo-Terminal Master-Slave pairs
// works only if the process involved in it are restarted from the same
// dmtcp_restart command.                               -- KAPIL

static void processDevPtmxConnection (int fd)
{
  char ptsName[21];

  JASSERT(_real_ptsname_r(fd, ptsName, 21) == 0) (JASSERT_ERRNO);

  dmtcp::string ptsNameStr = ptsName;
  dmtcp::string uniquePtsNameStr;

  // glibc allows only 20 char long ptsname
  // Check if there is enough room to insert the string "dmtcp_" before the
  //   terminal number, if not then we ASSERT here.
  JASSERT((strlen(ptsName) + strlen("dmtcp_")) <= 20)
    .Text("string /dev/pts/<n> too long, can not be virtualized."
          "Once possible workarong here is to replace the string"
          "\"dmtcp_\" with something short like \"d_\" or even "
          "\"d\" and recompile DMTCP");

  // Generate new Unique ptsName
  uniquePtsNameStr = UNIQUE_PTS_PREFIX_STR;
  uniquePtsNameStr += jalib::XToString(getNextFreeSlavePtyNum());

  dmtcp::string deviceName = "ptmx[" + ptsNameStr + "]:" + "/dev/ptmx";

//   dmtcp::string deviceName = "ptmx[" + dmtcp::UniquePid::ThisProcess().toString()
//                            + ":" + jalib::XToString ( _nextPtmxId() )
//                            + "]:" + device;

  JTRACE ( "creating ptmx connection" ) ( deviceName ) ( ptsNameStr ) ( uniquePtsNameStr );

  int type = dmtcp::PtyConnection::PTY_MASTER;
  dmtcp::Connection * c = new dmtcp::PtyConnection ( ptsNameStr, uniquePtsNameStr, type );

  dmtcp::KernelDeviceToConnection::instance().createPtyDevice ( fd, deviceName, c );

  dmtcp::UniquePtsNameToPtmxConId::instance().add ( uniquePtsNameStr, c->id() );
}

static void processDevPtsConnection (int fd, const char* uniquePtsName, const char* ptsName)
{
  dmtcp::string ptsNameStr = ptsName;
  dmtcp::string uniquePtsNameStr = uniquePtsName;

  dmtcp::string deviceName = "pts:" + ptsNameStr;

  JTRACE ( "creating pts connection" ) ( deviceName ) ( ptsNameStr ) ( uniquePtsNameStr );

  int type = dmtcp::PtyConnection::PTY_SLAVE;
  dmtcp::Connection * c = new dmtcp::PtyConnection ( ptsNameStr, uniquePtsNameStr, type );

  dmtcp::KernelDeviceToConnection::instance().createPtyDevice ( fd, deviceName, c );
}

extern "C" int getpt()
{
  int fd = _real_getpt();
  if ( fd >= 0 ) {
    processDevPtmxConnection(fd);
  }
  return fd;
}

#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
/* Used by open() wrapper to do other tracking of open apart from
   synchronization stuff. */
static int real_open_helper(const char *path, int flags, mode_t mode)
{
  char newpath [ 1024 ] = {0} ;
  /* If DMTCP has not yet initialized, it might be that JASSERT_INIT() is
   * calling this function to open jassert log files. Therefore we shouldn't be
   * playing with locks etc.
   *
   * FIXME: The following check is not required anymore. JASSERT_INIT calls
   *        libc:open directly.
   */
  if ( dmtcp::WorkerState::currentState() == dmtcp::WorkerState::UNKNOWN ) {
    return _real_open ( path, flags, mode );
  }

  WRAPPER_EXECUTION_DISABLE_CKPT();

  if ( strncmp(path, UNIQUE_PTS_PREFIX_STR, strlen(UNIQUE_PTS_PREFIX_STR)) == 0 ) {
    dmtcp::string currPtsDevName = dmtcp::UniquePtsNameToPtmxConId::instance().retrieveCurrentPtsDeviceName(path);
    strcpy(newpath, currPtsDevName.c_str());
  } else {
    updateProcPath ( path, newpath );
  }

  int fd = _real_open( newpath, flags, mode );

  if ( fd >= 0 && strcmp(path, "/dev/ptmx") == 0 ) {
    processDevPtmxConnection(fd);
  } else if ( fd >= 0 && strncmp(path, UNIQUE_PTS_PREFIX_STR, strlen(UNIQUE_PTS_PREFIX_STR)) == 0 ) {
    processDevPtsConnection(fd, path, newpath);
  }

  WRAPPER_EXECUTION_ENABLE_CKPT();

  return fd;
}
#endif

extern "C" int open (const char *path, int flags, ... )
{
  va_list ap;
  mode_t mode;
  int rc;
  char newpath [ PATH_MAX ] = {0} ;

  // Handling the variable number of arguments
  va_start( ap, flags );
  mode = va_arg ( ap, mode_t );
  va_end ( ap );
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    return real_open_helper(path, flags, mode);
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    return real_open_helper(path, flags, mode);
  }
  int retval = 0;
  log_entry_t my_entry = create_open_entry(my_clone_id, open_event,
      (unsigned long int)path, flags, mode);
  log_entry_t my_return_entry = create_open_entry(my_clone_id, open_event_return,
  (unsigned long int)path, flags, mode);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &open_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &open_turn_check);
    retval = GET_COMMON(currentLogEntry, retval);
    if (retval == -1) {
      if ( retval != GET_COMMON(currentLogEntry, retval) ) {
        JTRACE ( "tyler" )
          (retval) (GET_COMMON(currentLogEntry, retval)) (errno) (GET_COMMON(currentLogEntry, my_errno));
        kill(getpid(), SIGSEGV);
      }
      errno = GET_COMMON(currentLogEntry, my_errno);
      //JASSERT ( errno == GET_COMMON(currentLogEntry, my_errno) );
    }
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    _real_pthread_mutex_lock(&open_lock);
    addNextLogEntry(my_entry);
    retval = real_open_helper(path, flags, mode);
    SET_COMMON(my_return_entry, retval);
    if (retval == -1) {
      SET_COMMON2(my_return_entry, my_errno, errno);
    }
    addNextLogEntry(my_return_entry);
    _real_pthread_mutex_unlock(&open_lock);
  }
  return retval;
#else
  /* If DMTCP has not yet initialized, it might be that JASSERT_INIT() is
   * calling this function to open jassert log files. Therefore we shouldn't be
   * playing with locks etc.
   *
   * FIXME: The following check is not required anymore. JASSERT_INIT calls
   *        libc:open directly.
   */
  if ( dmtcp::WorkerState::currentState() == dmtcp::WorkerState::UNKNOWN ) {
    return _real_open ( path, flags, mode );
  }

  WRAPPER_EXECUTION_DISABLE_CKPT();

  if ( dmtcp::Util::strStartsWith(path, UNIQUE_PTS_PREFIX_STR) ) {
    dmtcp::string currPtsDevName = dmtcp::UniquePtsNameToPtmxConId::instance().retrieveCurrentPtsDeviceName(path);
    strcpy(newpath, currPtsDevName.c_str());
  } else {
    updateProcPath ( path, newpath );
  }

  int fd = _real_open( newpath, flags, mode );

  if ( fd >= 0 && strcmp(path, "/dev/ptmx") == 0 ) {
    processDevPtmxConnection(fd);
  } else if ( fd >= 0 && dmtcp::Util::strStartsWith(path, UNIQUE_PTS_PREFIX_STR) ) {
    processDevPtsConnection(fd, path, newpath);
  }

  WRAPPER_EXECUTION_ENABLE_CKPT();

  return fd;
#endif
}

#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
extern "C" FILE *fdopen(int fd, const char *mode)
{
  WRAPPER_HEADER(FILE *, fdopen, _real_fdopen, fd, mode);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &fdopen_turn_check);
    getNextLogEntry();
    void *p = NULL;
    size_t size = 0;
    while (1) {
      // Must wait until we're pointing at the malloc (to get the size)
      if (GET_COMMON(currentLogEntry,event) == malloc_event &&
          GET_COMMON(currentLogEntry,clone_id) == my_clone_id) {
        size = GET_FIELD(currentLogEntry, malloc, size);
        p = malloc(size);
        break;
      }
    }
    waitForTurn(my_return_entry, &fdopen_turn_check);
    // Copy the FILE struct we stored in the log to the area we just malloced.
    // This is to keep the addresses of streams consistent with record.
    // For an argument of why this is ok to do, please see the comment in the
    // fopen() wrapper.
    FILE f = GET_FIELD(currentLogEntry, fdopen, fdopen_retval);
    memcpy(p, (void *)&f, sizeof(f));
    retval = (FILE *)p;
    if (retval == NULL) {
      errno = GET_COMMON(currentLogEntry, my_errno);
    }
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _real_fdopen(fd, mode);
    SET_FIELD2(my_return_entry, fdopen, fdopen_retval, *retval);
    if (retval == NULL) {
      SET_COMMON2(my_return_entry, my_errno, errno);
    }
    addNextLogEntry(my_return_entry);
  }
  return retval;
}

extern "C" int getc(FILE *stream)
{
  BASIC_SYNC_WRAPPER(int, getc, _real_getc, stream);
}

extern "C" ssize_t getline(char **lineptr, size_t *n, FILE *stream)
{
  WRAPPER_HEADER(ssize_t, getline, _real_getline, lineptr, n, stream);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &getline_turn_check);
    if (GET_FIELD(currentLogEntry, getline, is_realloc)) {
      void *p = NULL;
      size_t size = 0;
      while (1) {
        // Must wait until we're pointing at the realloc (to get the size)
        if (GET_COMMON(currentLogEntry,event) == realloc_event &&
            GET_COMMON(currentLogEntry,clone_id) == my_clone_id) {
          size = GET_FIELD(currentLogEntry, realloc, size);
          void *ptr = (void *)GET_FIELD(currentLogEntry, realloc, ptr);
          p = realloc(ptr, size);
          break;
        }
      }
    }
    if (GET_FIELD(currentLogEntry, getline, lineptr) == NULL) {
      void *p = NULL;
      size_t size = 0;
      while (1) {
        // Must wait until we're pointing at the malloc (to get the size)
        if (GET_COMMON(currentLogEntry,event) == malloc_event &&
            GET_COMMON(currentLogEntry,clone_id) == my_clone_id) {
          size = GET_FIELD(currentLogEntry, malloc, size);
          p = malloc(size);
          break;
        }
      }
    }
    getNextLogEntry();
    waitForTurn(my_return_entry, &getline_turn_check);
    if (__builtin_expect(read_data_fd == -1, 0)) {
      read_data_fd = open(SYNCHRONIZATION_READ_DATA_LOG_PATH, O_RDONLY);
    }
    JASSERT ( read_data_fd != -1 );
    lseek(read_data_fd, GET_FIELD(currentLogEntry,getline,data_offset), SEEK_SET);
    if (GET_FIELD(currentLogEntry, getline, retval) != -1) {
      readAll(read_data_fd, *lineptr, GET_FIELD(currentLogEntry, getline, retval));
      retval = GET_FIELD(currentLogEntry, getline, retval);
    } else {
      retval = -1;
    }
    *n = GET_FIELD(currentLogEntry, getline, n);
    // Set the errno to what was logged (e.g. EINTR).
    if (GET_COMMON(currentLogEntry, my_errno) != 0) {
      errno = GET_COMMON(currentLogEntry, my_errno);
    }
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    bool is_realloc = false;
    size_t old_n = *n;
    retval = _real_getline(lineptr, n, stream);
    if (old_n != *n) is_realloc = true;
    SET_FIELD2(my_return_entry, getline, lineptr, *lineptr);
    SET_FIELD2(my_return_entry, getline, n, *n);
    SET_FIELD(my_return_entry, getline, retval);
    SET_FIELD(my_return_entry, getline, is_realloc);
    _real_pthread_mutex_lock(&getline_mutex);
    if (retval == -1) {
      SET_COMMON2(my_return_entry, my_errno, errno);
    } else {
      SET_FIELD2(my_return_entry, getline, data_offset, read_log_pos);
      logReadData(*lineptr, *n);
    }
    _real_pthread_mutex_unlock(&getline_mutex);
    addNextLogEntry(my_return_entry);
    // Be sure to not cover up the error with any intermediate calls
    // (like logReadData)
    if (retval == -1) errno = GET_COMMON(my_return_entry, my_errno);
  }
  return retval;
}

/* Here we borrow the data file used to store data returned from read() calls
   to store/replay the data for fgets() calls. */
extern "C" char *fgets(char *s, int size, FILE *stream)
{
  WRAPPER_HEADER(char *, fgets, _real_fgets, s, size, stream);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &fgets_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &fgets_turn_check);
    if (__builtin_expect(read_data_fd == -1, 0)) {
      read_data_fd = _real_open(SYNCHRONIZATION_READ_DATA_LOG_PATH, O_RDONLY, 0);
    }
    JASSERT ( read_data_fd != -1 );
    lseek(read_data_fd, GET_FIELD(currentLogEntry,fgets,data_offset), SEEK_SET);
    if (GET_FIELD(currentLogEntry, fgets, retval) != NULL) {
      readAll(read_data_fd, s, size);
      retval = s;
    } else {
      retval = NULL;
    }
    // Set the errno to what was logged (e.g. EINTR).
    if (GET_COMMON(currentLogEntry, my_errno) != 0) {
      errno = GET_COMMON(currentLogEntry, my_errno);
    }
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _real_fgets(s, size, stream);
    SET_FIELD(my_return_entry, fgets, retval);
    _real_pthread_mutex_lock(&fgets_mutex);
    if (retval == NULL) {
      SET_COMMON2(my_return_entry, my_errno, errno);
    } else {
      SET_FIELD2(my_return_entry, fgets, data_offset, read_log_pos);
      logReadData(s, size);
    }
    _real_pthread_mutex_unlock(&fgets_mutex);
    addNextLogEntry(my_return_entry);
    // Be sure to not cover up the error with any intermediate calls
    // (like logReadData)
    if (retval == NULL) errno = GET_COMMON(my_return_entry, my_errno);
  }
  return retval;
}

/* TODO: I think fprintf() is an inline function, so we can't wrap it directly.
   fprintf() internally calls this function, which happens to be exported.
   So we wrap this and have it call vfprintf(). We should think carefully about
   this and determine whether it is an acceptable solution or not. */
extern "C" int __fprintf_chk (FILE *stream, int flag, const char *format, ...)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    va_list arg;
    va_start (arg, format);
    int retval = vfprintf(stream, format, arg);
    va_end (arg);
    return retval;
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    va_list arg;
    va_start (arg, format);
    int retval = vfprintf(stream, format, arg);
    va_end (arg);
    return retval;
  }
  int retval;
  log_entry_t my_entry = create_fprintf_entry(my_clone_id,
      fprintf_event, stream, format);
  log_entry_t my_return_entry = create_fprintf_entry(my_clone_id,
      fprintf_event_return, stream, format);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &fprintf_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &fprintf_turn_check);
    retval = GET_COMMON(currentLogEntry, retval);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    addNextLogEntry(my_entry);
    va_list arg;
    va_start (arg, format);
    retval = vfprintf(stream, format, arg);
    va_end (arg);
    SET_COMMON(my_return_entry, retval);
    addNextLogEntry(my_return_entry);
  }
  return retval;
}

extern "C" int fputs(const char *s, FILE *stream)
{
  BASIC_SYNC_WRAPPER(int, fputs, _real_fputs, s, stream);
}

extern "C" int _IO_putc(int c, FILE *stream)
{
  BASIC_SYNC_WRAPPER(int, putc, _real_putc, c, stream);
}

extern "C" size_t fwrite(const void *ptr, size_t size, size_t nmemb,
    FILE *stream)
{
  BASIC_SYNC_WRAPPER(size_t, fwrite, _real_fwrite, ptr, size, nmemb, stream);
}

extern "C" void rewind(FILE *stream)
{
  BASIC_SYNC_WRAPPER_VOID(rewind, _real_rewind, stream);
}

extern "C" long ftell(FILE *stream)
{
  BASIC_SYNC_WRAPPER(long, ftell, _real_ftell, stream);
}
#endif // SYNCHRONIZATION_LOG_AND_REPLAY

#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
static FILE *_almost_real_fopen(const char *path, const char *mode)
{
  /* If DMTCP has not yet initialized, it might be that JASSERT_INIT() is
   * calling this function to open jassert log files. Therefore we shouldn't be
   * playing with locks etc.
   *
   * FIXME: The following check is not required anymore. JASSERT_INIT calls
   *        libc:open directly.
   */
  if ( dmtcp::WorkerState::currentState() == dmtcp::WorkerState::UNKNOWN ) {
    return _real_fopen ( path, mode );
  }

  WRAPPER_EXECUTION_DISABLE_CKPT();

  char newpath [ PATH_MAX ] = {0} ;
  int fd = -1;

  if ( dmtcp::Util::strStartsWith(path, UNIQUE_PTS_PREFIX_STR) ) {
    dmtcp::string currPtsDevName = dmtcp::UniquePtsNameToPtmxConId::instance().retrieveCurrentPtsDeviceName(path);
    strcpy(newpath, currPtsDevName.c_str());
  } else {
    updateProcPath ( path, newpath );
  }

  FILE *file = _real_fopen ( newpath, mode );

  if (file != NULL) {
    fd = fileno(file);
  }

  if ( fd >= 0 && strcmp(path, "/dev/ptmx") == 0 ) {
    processDevPtmxConnection(fd);
  } else if ( fd >= 0 && dmtcp::Util::strStartsWith(path, UNIQUE_PTS_PREFIX_STR) ) {
    processDevPtsConnection(fd, path, newpath);
  }

  WRAPPER_EXECUTION_ENABLE_CKPT();

  return file;
}
#endif

extern "C" FILE *fopen (const char* path, const char* mode)
{
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
  WRAPPER_HEADER(FILE *, fopen, _almost_real_fopen, path, mode);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &fopen_turn_check);
    getNextLogEntry();
    void *p = NULL;
    size_t size = 0;
    while (1) {
      // Must wait until we're pointing at the malloc (to get the size)
      if (GET_COMMON(currentLogEntry,event) == malloc_event &&
          GET_COMMON(currentLogEntry,clone_id) == my_clone_id) {
        size = GET_FIELD(currentLogEntry, malloc, size);
        p = malloc(size);
        break;
      }
    }
    waitForTurn(my_return_entry, &fopen_turn_check);
    /* An astute observer might note that the size we malloc()ed above is NOT
       the same as sizeof(FILE). Thus, the decision to use the above malloced
       area for the actual FILE structure needs some explanation.

       Internally, fopen() will malloc space for the new file stream. That
       malloc is replicated by the malloc above. However, the internal
       structure used in fopen() is slightly different -- it contains
       additional information, and the actual FILE object is just a field in
       that internal structure.

       Why, then, is it ok to simply copy the FILE struct to the beginning of
       that malloced area?

       Further investigation into the implementation of fopen() reveals that it
       returns the pointer to the actual FILE object within the internal
       structure. Furthermore, fclose() simple calls free() on that address.

       It follows, then, that the FILE object must be placed at the beginning
       of the internal structure, and we can simply replicate that here. */
    FILE f = GET_FIELD(currentLogEntry, fopen, fopen_retval);
    memcpy(p, (void *)&f, sizeof(f));
    retval = (FILE *)p;
    if (retval == NULL) {
      errno = GET_COMMON(currentLogEntry, my_errno);
    }
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _almost_real_fopen(path, mode);
    SET_FIELD2(my_return_entry, fopen, fopen_retval, *retval);
    if (retval == NULL) {
      SET_COMMON2(my_return_entry, my_errno, errno);
    }
    addNextLogEntry(my_return_entry);
  }
  return retval;
#else
  /* If DMTCP has not yet initialized, it might be that JASSERT_INIT() is
   * calling this function to open jassert log files. Therefore we shouldn't be
   * playing with locks etc.
   *
   * FIXME: The following check is not required anymore. JASSERT_INIT calls
   *        libc:open directly.
   */
  if ( dmtcp::WorkerState::currentState() == dmtcp::WorkerState::UNKNOWN ) {
    return _real_fopen ( path, mode );
  }

  WRAPPER_EXECUTION_DISABLE_CKPT();

  char newpath [ PATH_MAX ] = {0} ;
  int fd = -1;

  if ( dmtcp::Util::strStartsWith(path, UNIQUE_PTS_PREFIX_STR) ) {
    dmtcp::string currPtsDevName = dmtcp::UniquePtsNameToPtmxConId::instance().retrieveCurrentPtsDeviceName(path);
    strcpy(newpath, currPtsDevName.c_str());
  } else {
    updateProcPath ( path, newpath );
  }

  FILE *file = _real_fopen ( newpath, mode );

  if (file != NULL) {
    fd = fileno(file);
  }

  if ( fd >= 0 && strcmp(path, "/dev/ptmx") == 0 ) {
    processDevPtmxConnection(fd);
  } else if ( fd >= 0 && dmtcp::Util::strStartsWith(path, UNIQUE_PTS_PREFIX_STR) ) {
    processDevPtsConnection(fd, path, newpath);
  }

  WRAPPER_EXECUTION_ENABLE_CKPT();

  return file;
#endif
}

#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
#define CALL_CORRECT_FCNTL() \
  if (arg_3_l == -1 && arg_3_f == NULL) { \
    retval =  _real_fcntl(fd, cmd); \
  } else if (arg_3_l == -1) { \
    retval = _real_fcntl(fd, cmd, arg_3_f); \
  } else { \
    retval = _real_fcntl(fd, cmd, arg_3_l); \
  }

extern "C" int fcntl(int fd, int cmd, ...)
{
  va_list ap;
  // Handling the variable number of arguments
  long arg_3_l = -1;
  struct flock *arg_3_f = NULL;
  va_start( ap, cmd );
  switch (cmd) {
  case F_DUPFD:
  //case F_DUP_FD_CLOEXEC:
  case F_SETFD:
  case F_SETFL:
  case F_SETOWN:
  case F_SETSIG:
  case F_SETLEASE:
  case F_NOTIFY:
    arg_3_l = va_arg ( ap, long );
    va_end ( ap );
    break;
  case F_GETFD:
  case F_GETFL:
  case F_GETOWN:
  case F_GETSIG:
  case F_GETLEASE:
    va_end ( ap );
    break;
  case F_SETLK:
  case F_SETLKW:
  case F_GETLK:
    arg_3_f = va_arg ( ap, struct flock *);
    va_end ( ap );
    break;
  default:
    break;
  }
  int retval = 0;
  WRAPPER_EXECUTION_DISABLE_CKPT();
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    CALL_CORRECT_FCNTL();
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retval;
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    CALL_CORRECT_FCNTL();
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retval;
  }
  log_entry_t my_entry = create_fcntl_entry(my_clone_id, fcntl_event,
      fd, cmd, arg_3_l, (unsigned long int)arg_3_f);
  log_entry_t my_return_entry = create_fcntl_entry(my_clone_id, fcntl_event_return,
      fd, cmd, arg_3_l, (unsigned long int)arg_3_f);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &fcntl_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &fcntl_turn_check);
    // Don't call _real_ function. Lie to the user.
    // Set the errno to what was logged (e.g. EAGAIN).
    if (GET_COMMON(currentLogEntry, my_errno) != 0) {
      errno = GET_COMMON(currentLogEntry, my_errno);
    }
    retval = GET_COMMON(currentLogEntry, retval);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    CALL_CORRECT_FCNTL(); //sets retval
    SET_COMMON(my_return_entry, retval);
    if (retval == -1)
      SET_COMMON2(my_return_entry, my_errno, errno);
    addNextLogEntry(my_return_entry);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}
#endif

static void updateStatPath(const char *path, char *newpath)
{
  if ( dmtcp::WorkerState::currentState() == dmtcp::WorkerState::UNKNOWN ) {
    strncpy(newpath, path, PATH_MAX);
  } else if ( dmtcp::Util::strStartsWith(path, UNIQUE_PTS_PREFIX_STR) ) {
    dmtcp::string currPtsDevName = dmtcp::UniquePtsNameToPtmxConId::instance().retrieveCurrentPtsDeviceName(path);
    strcpy(newpath, currPtsDevName.c_str());
  } else {
    updateProcPath ( path, newpath );
  }
}

extern "C" 
int __xstat(int vers, const char *path, struct stat *buf)
{
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    return _real_xstat(vers, path, buf);
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    return _real_xstat(vers, path, buf);
  }
  int retval = 0;
  // my_entry will have garbage for 'buf'
  log_entry_t my_entry = create_xstat_entry(my_clone_id, xstat_event, vers,
      (unsigned long int)path, *buf);
  log_entry_t my_return_entry = create_xstat_entry(my_clone_id,
      xstat_event_return, vers, (unsigned long int)path, *buf);

  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &xstat_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &xstat_turn_check);
    //retval = _real_xstat(vers, path, buf);
    retval = GET_COMMON(currentLogEntry, retval);
    *buf = GET_FIELD(currentLogEntry, xstat, buf);
    if (retval != 0) {
      errno = GET_COMMON(currentLogEntry, my_errno);
    }
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _real_xstat(vers, path, buf);
    SET_COMMON(my_return_entry, retval);
    SET_FIELD2(my_return_entry, xstat, buf, *buf);
    if (retval != 0) {
      SET_COMMON2(my_return_entry, my_errno, errno);
    }
    addNextLogEntry(my_return_entry);
  }
  return retval;
#else
  char newpath [ PATH_MAX ] = {0} ;
  WRAPPER_EXECUTION_DISABLE_CKPT();
  updateStatPath(path, newpath);
  int rc = _real_xstat( vers, newpath, buf );
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return rc;
#endif
}

extern "C" 
int __xstat64(int vers, const char *path, struct stat64 *buf)
{
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    return _real_xstat64(vers, path, buf);
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    return _real_xstat64(vers, path, buf);
  }
  int retval = 0;
  log_entry_t my_entry = create_xstat64_entry(my_clone_id, xstat64_event, vers,
      (unsigned long int)path, *buf);
  log_entry_t my_return_entry = create_xstat64_entry(my_clone_id,
      xstat64_event_return, vers, (unsigned long int)path, *buf);

  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &xstat64_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &xstat64_turn_check);
    //retval = _real_xstat64(vers, path, buf);
    retval = GET_COMMON(currentLogEntry, retval);
    *buf = GET_FIELD(currentLogEntry, xstat64, buf);
    if (retval != 0) {
      errno = GET_COMMON(currentLogEntry, my_errno);
    }
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _real_xstat64(vers, path, buf);
    SET_COMMON(my_return_entry, retval);
    SET_FIELD2(my_return_entry, xstat64, buf, *buf);
    if (retval != 0) {
      SET_COMMON2(my_return_entry, my_errno, errno);
    }
    addNextLogEntry(my_return_entry);
  }
  return retval;
#else
  char newpath [ PATH_MAX ] = {0} ;
  WRAPPER_EXECUTION_DISABLE_CKPT();
  updateStatPath(path, newpath);
  int rc = _real_xstat64( vers, newpath, buf );
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return rc;
#endif
}

#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
extern "C" 
int __fxstat(int vers, int fd, struct stat *buf)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    return _real_fxstat(vers, fd, buf);
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    return _real_fxstat(vers, fd, buf);
  }
  int retval = 0;
  log_entry_t my_entry = create_fxstat_entry(my_clone_id, fxstat_event, vers,
      fd, *buf);
  log_entry_t my_return_entry = create_fxstat_entry(my_clone_id,
      fxstat_event_return, vers, fd, *buf);

  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &fxstat_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &fxstat_turn_check);
    //retval = _real_fxstat(vers, fd, buf);
    retval = GET_COMMON(currentLogEntry, retval);
    *buf = GET_FIELD(currentLogEntry, fxstat, buf);
    if (retval != 0) {
      errno = GET_COMMON(currentLogEntry, my_errno);
    }
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _real_fxstat(vers, fd, buf);
    SET_COMMON(my_return_entry, retval);
    SET_FIELD2(my_return_entry, fxstat, buf, *buf);
    if (retval != 0) {
      SET_COMMON2(my_return_entry, my_errno, errno);
    }
    addNextLogEntry(my_return_entry);
  }
  return retval;
}

extern "C" 
int __fxstat64(int vers, int fd, struct stat64 *buf)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    return _real_fxstat64(vers, fd, buf);
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    return _real_fxstat64(vers, fd, buf);
  }
  int retval = 0;
  log_entry_t my_entry = create_fxstat64_entry(my_clone_id, fxstat64_event, vers,
      fd, *buf);
  log_entry_t my_return_entry = create_fxstat64_entry(my_clone_id,
      fxstat64_event_return, vers, fd, *buf);

  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &fxstat64_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &fxstat64_turn_check);
    //retval = _real_fxstat64(vers, fd, buf);
    retval = GET_COMMON(currentLogEntry, retval);
    *buf = GET_FIELD(currentLogEntry, fxstat64, buf);
    if (retval != 0) {
      errno = GET_COMMON(currentLogEntry, my_errno);
    }
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _real_fxstat64(vers, fd, buf);
    SET_COMMON(my_return_entry, retval);
    SET_FIELD2(my_return_entry, fxstat64, buf, *buf);
    if (retval != 0) {
      SET_COMMON2(my_return_entry, my_errno, errno);
    }
    addNextLogEntry(my_return_entry);
  }
  return retval;
}
#endif // SYNCHRONIZATION_LOG_AND_REPLAY

extern "C" 
int __lxstat(int vers, const char *path, struct stat *buf)
{
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    return _real_lxstat(vers, path, buf);
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    return _real_lxstat(vers, path, buf);
  }
  int retval = 0;
  log_entry_t my_entry = create_lxstat_entry(my_clone_id, lxstat_event, vers,
      (unsigned long int)path, *buf);
  log_entry_t my_return_entry = create_lxstat_entry(my_clone_id,
      lxstat_event_return, vers,
      (unsigned long int)path, *buf);

  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &lxstat_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &lxstat_turn_check);
    retval = _real_lxstat(vers, path, buf);
    retval = GET_COMMON(currentLogEntry, retval);
    *buf = GET_FIELD(currentLogEntry, lxstat, buf);
    if (retval != 0) {
      errno = GET_COMMON(currentLogEntry, my_errno);
    }
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _real_lxstat(vers, path, buf);
    SET_COMMON(my_return_entry, retval);
    SET_FIELD2(my_return_entry, lxstat, buf, *buf);
    if (retval != 0) {
      SET_COMMON2(my_return_entry, my_errno, errno);
    }
    addNextLogEntry(my_return_entry);
  }
  return retval;
#else
  char newpath [ PATH_MAX ] = {0} ;
  WRAPPER_EXECUTION_DISABLE_CKPT();
  updateStatPath(path, newpath);
  int rc = _real_lxstat( vers, newpath, buf );
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return rc;
#endif
}

extern "C" 
int __lxstat64(int vers, const char *path, struct stat64 *buf)
{
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    return _real_lxstat64(vers, path, buf);
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    return _real_lxstat64(vers, path, buf);
  }
  int retval = 0;
  log_entry_t my_entry = create_lxstat64_entry(my_clone_id, lxstat64_event, vers,
      (unsigned long int)path, *buf);
  log_entry_t my_return_entry = create_lxstat64_entry(my_clone_id,
      lxstat64_event_return, vers,
      (unsigned long int)path, *buf);

  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &lxstat64_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &lxstat64_turn_check);
    //retval = _real_lxstat64(vers, path, buf);
    retval = GET_COMMON(currentLogEntry, retval);
    *buf = GET_FIELD(currentLogEntry, lxstat64, buf);
    if (retval != 0) {
      errno = GET_COMMON(currentLogEntry, my_errno);
    }
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    addNextLogEntry(my_entry);
    retval = _real_lxstat64(vers, path, buf);
    SET_COMMON(my_return_entry, retval);
    SET_FIELD2(my_return_entry, lxstat64, buf, *buf);
    if (retval != 0) {
      SET_COMMON2(my_return_entry, my_errno, errno);
    }
    addNextLogEntry(my_return_entry);
  }
  return retval;
#else
  char newpath [ PATH_MAX ] = {0} ;
  WRAPPER_EXECUTION_DISABLE_CKPT();
  updateStatPath(path, newpath);
  int rc = _real_lxstat64( vers, newpath, buf );
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return rc;
#endif
}

//       int fstat(int fd, struct stat *buf);

#ifdef ENABLE_MALLOC_WRAPPER
# ifdef ENABLE_DLOPEN
#  error "ENABLE_MALLOC_WRAPPER can't work with ENABLE_DLOPEN"
# endif

#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
/* 
We define several distinct layers of operation for the *alloc() family
wrappers.

Since large malloc()s are internally promoted to mmap() calls by libc, we want
to do our own promotion to mmap() so that we can control the addresses on
replay. The promotion threshold is SYNCHRONIZATION_MALLOC_LIMIT.

Since we do our own promotion, we need to remember which pointers we called
mmap() for, and which we called _real_malloc(). For example, user code calls
malloc(2000). This is above the limit, and so instead of calling
_real_malloc(2000), we mmap(). But, all the user knows is that they called
malloc(). This means that the user will eventually pass the pointer returned by
our mmap() to his own free() call. So, internally at that point we will need to
call munmap() instead of free().

We insert a header (struct alloc_header) at the beginning of each memory area
we allocate for the user that contains the size, and also if it was mmapped.

WRAPPER LEVEL
-------------
These are called directly from user's code, and as such only worry about user
addresses.

INTERNAL_ level
---------------
These are called from wrapper code. Externally they always deal in USER terms.
They consume and return USER addresses but know how to access and manipulate
the internal header ("REAL" addresses), and also make the choice of malloc()
vs. mmap().
*/

static void insertAllocHeader(struct alloc_header *header, void *dest)
{
  header->signature = 123456789;
  memcpy(dest, header, ALLOC_HEADER_SIZE);
}

static void getAllocHeader(struct alloc_header *header, void *p)
{
  memcpy(header, REAL_ADDR(p), ALLOC_HEADER_SIZE);
}

/* NOTE: Always consumes/returns USER addresses. Given a size and destination,
   will allocate the space and insert our own header at the beginning. Non-null
   destination forces allocation at that address. */
static void *internal_malloc(size_t size, void *dest)
{
  JASSERT ( size != 0 ).Text("0 should be not be passed to internal_malloc()");
  void *retval;
  struct alloc_header header;
  void *mmap_addr = (dest == NULL) ? NULL : REAL_ADDR(dest);
  int mmap_flags = (dest == NULL) ? (MAP_PRIVATE | MAP_ANONYMOUS)
                                  : (MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED);
  if (size >= SYNCHRONIZATION_MALLOC_LIMIT) {
    header.is_mmap = 1;
    retval = mmap ( mmap_addr, REAL_SIZE(size), 
        PROT_READ | PROT_WRITE, mmap_flags, -1, 0 );
    JASSERT ( retval != (void *)-1 ) ( retval ) ( errno );
  } else {
    header.is_mmap = 0;
    retval = _real_malloc ( REAL_SIZE(size) );
  }
  header.chunk_size = size;
  // Insert our header in the beginning:
  insertAllocHeader(&header, retval);
  return USER_ADDR(retval);
}

static void *internal_libc_memalign(size_t boundary, size_t size, void *dest)
{
  void *retval;
  struct alloc_header header;
  void *mmap_addr = (dest == NULL) ? NULL : REAL_ADDR(dest);
  int mmap_flags = (dest == NULL) ? (MAP_PRIVATE | MAP_ANONYMOUS)
                                  : (MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED);
  if (size >= SYNCHRONIZATION_MALLOC_LIMIT) {
    header.is_mmap = 1;
    retval = mmap ( mmap_addr, REAL_SIZE(size), 
        PROT_READ | PROT_WRITE, mmap_flags, -1, 0 );
    JASSERT ( retval != (void *)-1 ) ( retval ) ( errno );
  } else {
    header.is_mmap = 0;
    retval = _real_libc_memalign ( boundary, REAL_SIZE(size) );
  }
  header.chunk_size = size;
  // Insert our header in the beginning:
  insertAllocHeader(&header, retval);
  return USER_ADDR(retval);
}

/* NOTE: Always consumes/returns USER addresses. Given a size and destination,
   will allocate the space and insert our own header at the beginning. Non-null
   destination forces allocation at that address. */
static void *internal_calloc(size_t nmemb, size_t size, void *dest)
{
  // internal_malloc() returns USER address.
  void *retval = internal_malloc(nmemb*size, dest);
  memset(retval, 0, nmemb*size);
  return retval;
}

/* NOTE: Always consumes USER addresses. Frees the memory at the given address,
   calling munmap() or _real_free() as needed. */
static void internal_free(void *ptr)
{
  struct alloc_header header;
  getAllocHeader(&header, ptr);
  if (header.signature != 123456789) {
    JASSERT ( false ).Text("This should be handled by free() wrapper.");
    _real_free(ptr);
    return;
  }
  if (header.is_mmap) {
    int retval = munmap(REAL_ADDR(ptr), REAL_SIZE(header.chunk_size));
    JASSERT ( retval != -1 );
  } else {
    _real_free ( REAL_ADDR(ptr) );
  }
}

/* NOTE: Always consumes/returns USER addresses. Given a pointer, size and
   destination, will reallocate the space (copying old data in process) and
   insert our own header at the beginning. Non-null destination forces
   allocation at that address. */
static void *internal_realloc(void *ptr, size_t size, void *dest)
{
  struct alloc_header header;
  getAllocHeader(&header, ptr);
  void *retval = internal_malloc(size, dest);
  if (size < header.chunk_size) {
    memcpy(retval, ptr, size);
  } else {
    memcpy(retval, ptr, header.chunk_size);
  }
  internal_free(ptr);
  return retval;
}

static void my_init_hooks(void)
{
  /* Save old hook functions (from libc) and set them to our own hooks. */
  old_malloc_hook = __malloc_hook;
  old_free_hook = __free_hook;
  __malloc_hook = my_malloc_hook;
  __free_hook = my_free_hook;
}

static void *my_malloc_hook (size_t size, const void *caller)
{
  _real_pthread_mutex_lock(&hook_lock);
  void *result;
  /* Restore all old hooks */
  __malloc_hook = old_malloc_hook;
  __free_hook = old_free_hook;
  result = _real_malloc (size);
  /* Save underlying hooks */
  old_malloc_hook = __malloc_hook;
  old_free_hook = __free_hook;
  /*static int tyler_pid = _real_getpid();
  void *buffer[10];
  int nptrs;
  // NB: In order to use backtrace, you must disable tylerShouldLog.
  // AND remove the locks around !tylerShouldLog real_malloc in malloc wrapper
  nptrs = backtrace (buffer, 10);
  backtrace_symbols_fd ( buffer, nptrs, 1);
  printf ("<%d> malloc (%u) returns %p\n", tyler_pid, (unsigned int) size, result);*/
  /* Restore our own hooks */
  __malloc_hook = my_malloc_hook;
  __free_hook = my_free_hook;
  _real_pthread_mutex_unlock(&hook_lock);
  return result;
}

static void my_free_hook (void *ptr, const void *caller)
{
  _real_pthread_mutex_lock(&hook_lock);
  /* Restore all old hooks */
  __malloc_hook = old_malloc_hook;
  __free_hook = old_free_hook;
  _real_free (ptr);
  /* Save underlying hooks */
  old_malloc_hook = __malloc_hook;
  old_free_hook = __free_hook;
  /* printf might call free, so protect it too. */
  /*static int tyler_pid = _real_getpid();
  printf ("<%d> freed pointer %p\n", tyler_pid, ptr);*/
  /* Restore our own hooks */
  __malloc_hook = my_malloc_hook;
  __free_hook = my_free_hook;
  _real_pthread_mutex_unlock(&hook_lock);
}
#endif // SYNCHRONIZATION_LOG_AND_REPLAY

extern "C" void *calloc(size_t nmemb, size_t size)
{
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
  WRAPPER_EXECUTION_DISABLE_CKPT();
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr) && !tylerShouldLog) {
    _real_pthread_mutex_lock(&allocation_lock);
    void *retVal = _real_calloc ( nmemb, size );
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retVal;
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    // Don't log gdb's read calls (e.g. user commands)
    _real_pthread_mutex_lock(&allocation_lock);
    void *retVal = _real_calloc ( nmemb, size );
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retVal;
  }
  if (nmemb == 0 || size == 0) {
    _real_pthread_mutex_lock(&allocation_lock);
    void *retVal = _real_calloc ( nmemb, size );
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retVal;
  }
  void *retval;
  log_entry_t my_entry = create_calloc_entry(my_clone_id, calloc_event, nmemb, size);
  log_entry_t my_return_entry = create_calloc_entry(my_clone_id, calloc_event_return, nmemb, size);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &calloc_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &calloc_turn_check);
    _real_pthread_mutex_lock(&allocation_lock);
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY_DEBUG
    retval = internal_calloc(nmemb, size, (void *)currentLogEntry.return_ptr);
    JASSERT ( (unsigned long int)retval == currentLogEntry.return_ptr ) ( retval )
            ( (void*)currentLogEntry.return_ptr );
#else
    retval = internal_calloc(nmemb, size, 
               (void *)currentLogEntry.log_event_t.log_event_calloc.return_ptr);
    JASSERT ( (unsigned long int)retval ==
              currentLogEntry.log_event_t.log_event_calloc.return_ptr ) 
              ( retval )
              ( (void*)currentLogEntry.log_event_t.log_event_calloc.return_ptr );
#endif
    _real_pthread_mutex_unlock(&allocation_lock);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    _real_pthread_mutex_lock(&allocation_lock);
    addNextLogEntry(my_entry);
    retval = internal_calloc(nmemb, size, NULL);
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY_DEBUG
    my_return_entry.return_ptr = (unsigned long int)retval;
#else
    my_return_entry.log_event_t.log_event_calloc.return_ptr =
      (unsigned long int)retval;
#endif
    addNextLogEntry(my_return_entry);
    _real_pthread_mutex_unlock(&allocation_lock);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
#else
  WRAPPER_EXECUTION_DISABLE_CKPT();
  void *retVal = _real_calloc ( nmemb, size );
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retVal;
#endif
}
extern "C" void *malloc(size_t size)
{
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
  WRAPPER_EXECUTION_DISABLE_CKPT();
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr) && !tylerShouldLog) {
    _real_pthread_mutex_lock(&allocation_lock);
    void *retVal = _real_malloc ( size );
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retVal;
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    // Don't log gdb's read calls (e.g. user commands)
    _real_pthread_mutex_lock(&allocation_lock);
    void *retVal = _real_malloc ( size );
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retVal;
  }
  /*if ((logEntryIndex >= 900000) && !initHook) {
    my_init_hooks();
    initHook = 1;
  }*/
  if (size == 0) {
    _real_pthread_mutex_lock(&allocation_lock);
    void *retVal = _real_malloc ( size );
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retVal;
  }
  void *retval;
  log_entry_t my_entry = create_malloc_entry(my_clone_id, malloc_event, size);
  log_entry_t my_return_entry = create_malloc_entry(my_clone_id, malloc_event_return, size);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &malloc_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &malloc_turn_check);
    // Force allocation at same location as record:
    _real_pthread_mutex_lock(&allocation_lock);
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY_DEBUG
    retval = internal_malloc(size, (void *)currentLogEntry.return_ptr);
    if ((unsigned long int)retval != currentLogEntry.return_ptr) {
      JTRACE ( "tyler" ) ( retval ) 
             ( (void*) currentLogEntry.return_ptr )
	     ( log_entry_index );
#else
    retval = internal_malloc(size, 
               (void *)currentLogEntry.log_event_t.log_event_malloc.return_ptr);
    if ((unsigned long int)retval !=
        currentLogEntry.log_event_t.log_event_malloc.return_ptr) {
      JTRACE ( "tyler" ) ( retval ) 
             ( (void*) currentLogEntry.log_event_t.log_event_malloc.return_ptr )
	     ( log_entry_index );
#endif
      kill(getpid(), SIGSEGV);
    }
    _real_pthread_mutex_unlock(&allocation_lock);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    _real_pthread_mutex_lock(&allocation_lock);
    addNextLogEntry(my_entry);
    retval = internal_malloc(size, NULL);
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY_DEBUG
    my_return_entry.return_ptr = (unsigned long int)retval;
#else
    my_return_entry.log_event_t.log_event_malloc.return_ptr =
      (unsigned long int)retval;
#endif
    addNextLogEntry(my_return_entry);
    _real_pthread_mutex_unlock(&allocation_lock);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
#else
  WRAPPER_EXECUTION_DISABLE_CKPT();
  void *retVal = _real_malloc ( size );
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retVal;
#endif
}

#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
extern "C" void *__libc_memalign(size_t boundary, size_t size)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr) && !tylerShouldLog) {
    _real_pthread_mutex_lock(&allocation_lock);
    void *retVal = _real_libc_memalign ( boundary, size );
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retVal;
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    // Don't log gdb's read calls (e.g. user commands)
    _real_pthread_mutex_lock(&allocation_lock);
    void *retVal = _real_libc_memalign ( boundary, size );
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retVal;
  }
  if (size == 0) {
    _real_pthread_mutex_lock(&allocation_lock);
    void *retVal = _real_libc_memalign ( boundary, size );
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retVal;
  }
  void *retval;
  while (my_clone_id == 0) sleep(0.1);  
  log_entry_t my_entry = create_libc_memalign_entry(my_clone_id, libc_memalign_event, boundary, size);
  log_entry_t my_return_entry = create_libc_memalign_entry(my_clone_id, libc_memalign_event_return, boundary, size);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &libc_memalign_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &libc_memalign_turn_check);
    // Force allocation at same location as record:
    _real_pthread_mutex_lock(&allocation_lock);
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY_DEBUG
    retval = internal_libc_memalign(boundary, size, (void *)currentLogEntry.return_ptr);
    if ((unsigned long int)retval != currentLogEntry.return_ptr) {
      JTRACE ( "tyler" ) ( retval ) 
             ( (void*) currentLogEntry.return_ptr )
	     ( log_entry_index );
#else
    retval = internal_libc_memalign(boundary, size,
               (void *)currentLogEntry.log_event_t.log_event_libc_memalign.return_ptr);
    if ((unsigned long int)retval !=
        currentLogEntry.log_event_t.log_event_libc_memalign.return_ptr) {
      JTRACE ( "tyler" ) ( retval ) 
             ( (void*) currentLogEntry.log_event_t.log_event_libc_memalign.return_ptr )
	     ( log_entry_index );
#endif
      kill(getpid(), SIGSEGV);
    }
    _real_pthread_mutex_unlock(&allocation_lock);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    _real_pthread_mutex_lock(&allocation_lock);
    addNextLogEntry(my_entry);
    retval = internal_libc_memalign(boundary, size, NULL);
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY_DEBUG
    my_return_entry.return_ptr = (unsigned long int)retval;
#else
    my_return_entry.log_event_t.log_event_libc_memalign.return_ptr =
      (unsigned long int)retval;
#endif
    addNextLogEntry(my_return_entry);
    _real_pthread_mutex_unlock(&allocation_lock);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
}

/* TODO: fix me */
extern "C" void *valloc(size_t size) 
{
  return __libc_memalign(sysconf(_SC_PAGESIZE), size);
}

#endif

extern "C" void free(void *ptr)
{
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
  WRAPPER_EXECUTION_DISABLE_CKPT();
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr) && !tylerShouldLog) {
    _real_pthread_mutex_lock(&allocation_lock);
    _real_free ( ptr );
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
  } else if (jalib::Filesystem::GetProgramName() == "gdb") {
    // Don't log gdb's read calls (e.g. user commands)
    _real_pthread_mutex_lock(&allocation_lock);
    _real_free ( ptr );
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
  } else if (ptr == NULL) {
    // free(NULL) is allowed, but has no effect.
    // We can safely skip record/replay without compromising determinism.
    _real_pthread_mutex_lock(&allocation_lock);
    _real_free ( ptr );
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
  } else {
    struct alloc_header header;
    getAllocHeader(&header, ptr);
    if (header.signature != 123456789) {
      // Don't record or replay this.
      _real_free(ptr);
      return;
    }

    log_entry_t my_entry = create_free_entry(my_clone_id, free_event, (unsigned long int)ptr);
    log_entry_t my_return_entry = create_free_entry(my_clone_id, free_event_return, (unsigned long int)ptr);
    if (SYNC_IS_REPLAY) {
      waitForTurn(my_entry, &free_turn_check);
      getNextLogEntry();
      waitForTurn(my_return_entry, &free_turn_check);
      _real_pthread_mutex_lock(&allocation_lock);
      internal_free(ptr);
      _real_pthread_mutex_unlock(&allocation_lock);
      getNextLogEntry();
    } else if (SYNC_IS_LOG) {
      // Not restart; we should be logging.
      _real_pthread_mutex_lock(&allocation_lock);
      addNextLogEntry(my_entry);
      internal_free(ptr);
      addNextLogEntry(my_return_entry);
      _real_pthread_mutex_unlock(&allocation_lock);
    }
    WRAPPER_EXECUTION_ENABLE_CKPT();
  }
#else
  WRAPPER_EXECUTION_DISABLE_CKPT();
  _real_free ( ptr );
  WRAPPER_EXECUTION_ENABLE_CKPT();
#endif
}

extern "C" void *realloc(void *ptr, size_t size)
{
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
  WRAPPER_EXECUTION_DISABLE_CKPT();
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr) && !tylerShouldLog) {
    _real_pthread_mutex_lock(&allocation_lock);
    void *retVal = _real_realloc ( ptr, size );
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retVal;
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    // Don't log gdb's calls (e.g. user commands)
    _real_pthread_mutex_lock(&allocation_lock);
    void *retVal = _real_realloc ( ptr, size );
    _real_pthread_mutex_unlock(&allocation_lock);
    WRAPPER_EXECUTION_ENABLE_CKPT();
    return retVal;
  }
  if (ptr == NULL) {
    // See man page for details.
    return malloc(size);
  }
  void *retval;
  log_entry_t my_entry = create_realloc_entry(my_clone_id, realloc_event, (unsigned long int)ptr, size);
  log_entry_t my_return_entry = create_realloc_entry(my_clone_id, realloc_event_return, (unsigned long int)ptr, size);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &realloc_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &realloc_turn_check);
    _real_pthread_mutex_lock(&allocation_lock);
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY_DEBUG
    retval = internal_realloc(ptr, size, (void *)currentLogEntry.return_ptr);
    JASSERT ( (unsigned long int)retval == currentLogEntry.return_ptr ) 
              ( (unsigned long int)retval )
              ( currentLogEntry.return_ptr );
#else
    retval = internal_realloc(ptr, size,
               (void *)currentLogEntry.log_event_t.log_event_realloc.return_ptr);
    JASSERT ( (unsigned long int)retval ==
              currentLogEntry.log_event_t.log_event_realloc.return_ptr ) 
              ( (unsigned long int)retval )
              ( currentLogEntry.log_event_t.log_event_realloc.return_ptr );
#endif
    _real_pthread_mutex_unlock(&allocation_lock);
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    // Not restart; we should be logging.
    _real_pthread_mutex_lock(&allocation_lock);
    addNextLogEntry(my_entry);
    retval = internal_realloc(ptr, size, NULL);
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY_DEBUG
    my_return_entry.return_ptr = (unsigned long int)retval;
#else
    my_return_entry.log_event_t.log_event_realloc.return_ptr =
      (unsigned long int)retval;
#endif
    addNextLogEntry(my_return_entry);
    _real_pthread_mutex_unlock(&allocation_lock);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retval;
#else
  WRAPPER_EXECUTION_DISABLE_CKPT();
  void *retVal = _real_realloc ( ptr, size );
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retVal;
#endif
}
#endif // ENABLE_MALLOC_WRAPPER

/*
extern "C" int
printf (const char *format, ...)
{
  va_list arg;
  int done;

  va_start (arg, format);
  done = vfprintf (stdout, format, arg);
  va_end (arg);

  return done;
}

extern "C" int
fprintf (FILE *stream, const char *format, ...)
{
  va_list arg;
  int done;

  va_start (arg, format);
  done = vfprintf (stream, format, arg);
  va_end (arg);

  return done;
}

extern "C" int
vprintf (const char *format, __gnuc_va_list arg)
{
  return vfprintf (stdout, format, arg);
}

extern "C" int
vfprintf (FILE *s, const char *format, va_list ap)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  int retVal = _real_vfprintf ( s, format, ap );
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return retVal;
}


extern "C" int
sprintf (char *s, const char *format, ...)
{
  va_list arg;
  int done;

  va_start (arg, format);
  done = vsprintf (s, format, arg);
  va_end (arg);

  return done;
}


extern "C" int
snprintf (char *s, size_t maxlen, const char *format, ...)
{
  va_list arg;
  int done;

  va_start (arg, format);
  done = vsnprintf (s, maxlen, format, arg);
  va_end (arg);

  return done;
}


extern "C" int vsprintf(char *str, const char *format, va_list ap);
extern "C" int vsnprintf(char *str, size_t size, const char *format, va_list ap);
*/

extern "C"
int shmget(key_t key, size_t size, int shmflg)
{
  int ret;
  WRAPPER_EXECUTION_DISABLE_CKPT();
  while (true) {
    ret = _real_shmget(key, size, shmflg);
    if (ret != -1 && 
        dmtcp::SysVIPC::instance().isConflictingShmid(ret) == false) {
      dmtcp::SysVIPC::instance().on_shmget(key, size, shmflg, ret);
      break;
    }
    JASSERT(_real_shmctl(ret, IPC_RMID, NULL) != -1);
  };
  JTRACE ("Creating new Shared memory segment" ) (key) (size) (shmflg) (ret);
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return ret;
}

extern "C"
void *shmat(int shmid, const void *shmaddr, int shmflg)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  int currentShmid = dmtcp::SysVIPC::instance().originalToCurrentShmid(shmid);
  JASSERT(currentShmid != -1);
  void *ret = _real_shmat(currentShmid, shmaddr, shmflg);
  if (ret != (void *) -1) {
    dmtcp::SysVIPC::instance().on_shmat(shmid, shmaddr, shmflg, ret);
    JTRACE ("Mapping Shared memory segment" ) (shmid) (shmflg) (ret);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return ret;
}

extern "C"
int shmdt(const void *shmaddr)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  int ret = _real_shmdt(shmaddr);
  if (ret != -1) {
    dmtcp::SysVIPC::instance().on_shmdt(shmaddr);
    JTRACE ("Unmapping Shared memory segment" ) (shmaddr);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return ret;
}

extern "C"
int shmctl(int shmid, int cmd, struct shmid_ds *buf)
{
  WRAPPER_EXECUTION_DISABLE_CKPT();
  int currentShmid = dmtcp::SysVIPC::instance().originalToCurrentShmid(shmid);
  JASSERT(currentShmid != -1);
  int ret = _real_shmctl(currentShmid, cmd, buf);
  // Change the creater-pid of the shm object to the original so that if
  // calling thread wants to use it, pid-virtualization layer can take care of
  // the original to current conversion.
  // TODO: Need to update uid/gid fields to support uid/gid virtualization.
  if (buf != NULL) {
    buf->shm_cpid = dmtcp::VirtualPidTable::instance().currentToOriginalPid(buf->shm_cpid);
  }
  WRAPPER_EXECUTION_ENABLE_CKPT();
  return ret;
}

extern "C" int __clone ( int ( *fn ) ( void *arg ), void *child_stack, int flags, void *arg, int *parent_tidptr, struct user_desc *newtls, int *child_tidptr );
pid_t gettid();
int tkill(int tid, int sig);
int tgkill(int tgid, int tid, int sig);

#define SYSCALL_VA_START()                                              \
  va_list ap;                                                           \
  va_start(ap, sys_num)

#define SYSCALL_VA_END()                                                \
  va_end(ap)

#define SYSCALL_GET_ARG(type,arg) type arg = va_arg(ap, type)

#define SYSCALL_GET_ARGS_2(type1,arg1,type2,arg2)                       \
  SYSCALL_GET_ARG(type1,arg1);                                          \
  SYSCALL_GET_ARG(type2,arg2)

#define SYSCALL_GET_ARGS_3(type1,arg1,type2,arg2,type3,arg3)            \
  SYSCALL_GET_ARGS_2(type1,arg1,type2,arg2);                            \
  SYSCALL_GET_ARG(type3,arg3)

#define SYSCALL_GET_ARGS_4(type1,arg1,type2,arg2,type3,arg3,type4,arg4) \
  SYSCALL_GET_ARGS_3(type1,arg1,type2,arg2,type3,arg3);                 \
  SYSCALL_GET_ARG(type4,arg4)

#define SYSCALL_GET_ARGS_5(type1,arg1,type2,arg2,type3,arg3,type4,arg4, \
                           type5,arg5)                                  \
  SYSCALL_GET_ARGS_4(type1,arg1,type2,arg2,type3,arg3,type4,arg4);      \
  SYSCALL_GET_ARG(type5,arg5)

#define SYSCALL_GET_ARGS_6(type1,arg1,type2,arg2,type3,arg3,type4,arg4, \
                           type5,arg5,type6,arg6)                        \
  SYSCALL_GET_ARGS_5(type1,arg1,type2,arg2,type3,arg3,type4,arg4,       \
                     type5,arg5);                                       \
  SYSCALL_GET_ARG(type6,arg6)

#define SYSCALL_GET_ARGS_7(type1,arg1,type2,arg2,type3,arg3,type4,arg4, \
                           type5,arg5,type6,arg6,type7,arg7)             \
  SYSCALL_GET_ARGS_6(type1,arg1,type2,arg2,type3,arg3,type4,arg4,       \
                     type5,arg5,type6,arg6);                             \
  SYSCALL_GET_ARG(type7,arg7)

/* Comments by Gene:
 * Here, syscall is the wrapper, and the call to syscall would be _real_syscall
 * We would add a special case for SYS_gettid, while all others default as below
 * It depends on the idea that arguments are stored in registers, whose
 *  natural size is:  sizeof(void*)
 * So, we pass six arguments to syscall, and it will ignore any extra arguments
 * I believe that all Linux system calls have no more than 7 args.
 * clone() is an example of one with 7 arguments.
 * If we discover system calls for which the 7 args strategy doesn't work,
 *  we can special case them.
 *
 * XXX: DO NOT USE JTRACE/JNOTE/JASSERT in this function; even better, do not
 *      use any STL here.  (--Kapil)
 */
extern "C" long int syscall(long int sys_num, ... )
{
  long int ret;
  va_list ap;

  va_start(ap, sys_num);

  switch ( sys_num ) {
    case SYS_gettid:
    {
      ret = gettid();
      break;
    }
    case SYS_tkill:
    {
      SYSCALL_GET_ARGS_2(int, tid, int, sig);
      ret = tkill(tid, sig);
      break;
    }
    case SYS_tgkill:
    {
      SYSCALL_GET_ARGS_3(int, tgid, int, tid, int, sig);
      ret = tgkill(tgid, tid, sig);
      break;
    }

    case SYS_clone:
    {
      typedef int (*fnc) (void*);
      SYSCALL_GET_ARGS_7(fnc, fn, void*, child_stack, int, flags, void*, arg,
                         pid_t*, pid, struct user_desc*, tls, pid_t*, ctid);
      ret = __clone(fn, child_stack, flags, arg, pid, tls, ctid);
      break;
    }

    case SYS_execve:
    {
      SYSCALL_GET_ARGS_3(const char*,filename,char* const *,argv,char* const *,envp);
      ret = execve(filename,argv,envp);
      break;
    }

    case SYS_fork:
    {
      ret = fork();
      break;
    }
    case SYS_exit:
    {
      SYSCALL_GET_ARG(int,status);
      exit(status);
      break;
    }
    case SYS_open:
    {
      SYSCALL_GET_ARGS_3(const char*,pathname,int,flags,mode_t,mode);
      ret = open(pathname, flags, mode);
      break;
    }
    case SYS_close:
    {
      SYSCALL_GET_ARG(int,fd);
      ret = close(fd);
      break;
    }

    case SYS_rt_sigaction:
    {
      SYSCALL_GET_ARGS_3(int,signum,const struct sigaction*,act,struct sigaction*,oldact);
      ret = sigaction(signum, act, oldact);
      break;
    }
    case SYS_rt_sigprocmask:
    {
      SYSCALL_GET_ARGS_3(int,how,const sigset_t*,set,sigset_t*,oldset);
      ret = sigprocmask(how, set, oldset);
      break;
    }
    case SYS_rt_sigtimedwait:
    {
      SYSCALL_GET_ARGS_3(const sigset_t*,set,siginfo_t*,info,
                        const struct timespec*, timeout);
      ret = sigtimedwait(set, info, timeout);
      break;
    }

#ifdef __i386__
    case SYS_sigaction:
    {
      SYSCALL_GET_ARGS_3(int,signum,const struct sigaction*,act,struct sigaction*,oldact);
      ret = sigaction(signum, act, oldact);
      break;
    }
    case SYS_signal:
    {
      typedef void (*sighandler_t)(int);
      SYSCALL_GET_ARGS_2(int,signum,sighandler_t,handler);
      // Cast needed:  signal returns sighandler_t
      ret = (long int)signal(signum, handler);
      break;
    }
    case SYS_sigprocmask:
    {
      SYSCALL_GET_ARGS_3(int,how,const sigset_t*,set,sigset_t*,oldset);
      ret = sigprocmask(how, set, oldset);
      break;
    }
#endif

#ifdef __x86_64__
// These SYS_xxx are only defined for 64-bit Linux
    case SYS_socket:
    {
      SYSCALL_GET_ARGS_3(int,domain,int,type,int,protocol);
      ret = socket(domain,type,protocol);
      break;
    }
    case SYS_connect:
    {
      SYSCALL_GET_ARGS_3(int,sockfd,const struct sockaddr*,addr,socklen_t,addrlen);
      ret = connect(sockfd, addr, addrlen);
      break;
    }
    case SYS_bind:
    {
      SYSCALL_GET_ARGS_3(int,sockfd,const struct sockaddr*,addr,socklen_t,addrlen);
      ret = bind(sockfd,addr,addrlen);
      break;
    }
    case SYS_listen:
    {
      SYSCALL_GET_ARGS_2(int,sockfd,int,backlog);
      ret = listen(sockfd,backlog);
      break;
    }
    case SYS_accept:
    {
      SYSCALL_GET_ARGS_3(int,sockfd,struct sockaddr*,addr,socklen_t*,addrlen);
      ret = accept(sockfd, addr, addrlen);
      break;
    }
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)
# if __GLIBC_PREREQ(2,10)
    case SYS_accept4:
    {
      SYSCALL_GET_ARGS_4(int,sockfd,struct sockaddr*,addr,socklen_t*,addrlen,int,flags);
      ret = accept4(sockfd, addr, addrlen, flags);
      break;
    }
# endif
#endif
    case SYS_setsockopt:
    {
      SYSCALL_GET_ARGS_5(int,s,int,level,int,optname,const void*,optval,socklen_t,optlen);
      ret = setsockopt(s, level, optname, optval, optlen);
      break;
    }

    case SYS_socketpair:
    {
      SYSCALL_GET_ARGS_4(int,d,int,type,int,protocol,int*,sv);
      ret = socketpair(d,type,protocol,sv);
      break;
    }
#endif

    case SYS_pipe:
    {
      SYSCALL_GET_ARG(int*,fds);
      ret = pipe(fds);
      break;
    }
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
# if __GLIBC_PREREQ(2,9)
    case SYS_pipe2:
    {
      SYSCALL_GET_ARGS_2(int*,fds,int,flags);
      ret = pipe2(fds, flags);
      break;
    }
# endif
#endif

#ifdef PID_VIRTUALIZATION
    case SYS_getpid:
    {
      ret = getpid();
      break;
    }
    case SYS_getppid:
    {
      ret = getppid();
      break;
    }

    case SYS_getpgrp:
    {
      ret = getpgrp();
      break;
    }

    case SYS_getpgid:
    {
      SYSCALL_GET_ARG(pid_t,pid);
      ret = getpgid(pid);
      break;
    }
    case SYS_setpgid:
    {
      SYSCALL_GET_ARGS_2(pid_t,pid,pid_t,pgid);
      ret = setpgid(pid, pgid);
      break;
    }

    case SYS_getsid:
    {
      SYSCALL_GET_ARG(pid_t,pid);
      ret = getsid(pid);
      break;
    }
    case SYS_setsid:
    {
      ret = setsid();
      break;
    }

    case SYS_kill:
    {
      SYSCALL_GET_ARGS_2(pid_t,pid,int,sig);
      ret = kill(pid, sig);
      break;
    }

    case SYS_waitid:
    {
      //SYSCALL_GET_ARGS_4(idtype_t,idtype,id_t,id,siginfo_t*,infop,int,options);
      SYSCALL_GET_ARGS_4(int,idtype,id_t,id,siginfo_t*,infop,int,options);
      ret = waitid((idtype_t)idtype, id, infop, options);
      break;
    }
    case SYS_wait4:
    {
      SYSCALL_GET_ARGS_4(pid_t,pid,__WAIT_STATUS,status,int,options,
                         struct rusage*,rusage);
      ret = wait4(pid, status, options, rusage);
      break;
    }
#ifdef __i386__
    case SYS_waitpid:
    {
      SYSCALL_GET_ARGS_3(pid_t,pid,int*,status,int,options);
      ret = waitpid(pid, status, options);
      break;
    }
#endif

    case SYS_setgid:
    {
      SYSCALL_GET_ARG(gid_t,gid);
      ret = setgid(gid);
      break;
    }
    case SYS_setuid:
    {
      SYSCALL_GET_ARG(uid_t,uid);
      ret = setuid(uid);
      break;
    }
#endif /* PID_VIRTUALIZATION */

#ifndef DISABLE_SYS_V_IPC
# ifdef __x86_64__
// These SYS_xxx are only defined for 64-bit Linux
    case SYS_shmget:
    {
      SYSCALL_GET_ARGS_3(key_t,key,size_t,size,int,shmflg);
      ret = shmget(key, size, shmflg);
      break;
    }
    case SYS_shmat:
    {
      SYSCALL_GET_ARGS_3(int,shmid,const void*,shmaddr,int,shmflg);
      ret = (unsigned long) shmat(shmid, shmaddr, shmflg);
      break;
    }
    case SYS_shmdt:
    {
      SYSCALL_GET_ARG(const void*,shmaddr);
      ret = shmdt(shmaddr);
      break;
    }
    case SYS_shmctl:
    {
      SYSCALL_GET_ARGS_3(int,shmid,int,cmd,struct shmid_ds*,buf);
      ret = shmctl(shmid, cmd, buf);
      break;
    }
# endif
#endif

    default:
    {
      SYSCALL_GET_ARGS_7(void*, arg1, void*, arg2, void*, arg3, void*, arg4,
                         void*, arg5, void*, arg6, void*, arg7);
      ret = _real_syscall(sys_num, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
      break;
    }
  }
  va_end(ap);
  return ret;
}
