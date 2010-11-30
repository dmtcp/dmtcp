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
#include <execinfo.h>
// TODO: hack to be able to compile this (fcntl wrapper).
#define open _libc_open
#define open64 _libc_open64
#include <fcntl.h>
#undef open
#undef open64

#endif

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
    _real_pthread_mutex_lock(&read_data_mutex);
    addNextLogEntry(my_entry);
    retval = real_open_helper(path, flags, mode);
    SET_COMMON(my_return_entry, retval);
    if (retval == -1) {
      SET_COMMON2(my_return_entry, my_errno, errno);
    }
    addNextLogEntry(my_return_entry);
    _real_pthread_mutex_unlock(&read_data_mutex);
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
    _real_pthread_mutex_lock(&read_data_mutex);
    if (retval == -1) {
      SET_COMMON2(my_return_entry, my_errno, errno);
    } else {
      SET_FIELD2(my_return_entry, getline, data_offset, read_log_pos);
      logReadData(*lineptr, *n);
    }
    _real_pthread_mutex_unlock(&read_data_mutex);
    addNextLogEntry(my_return_entry);
    // Be sure to not cover up the error with any intermediate calls
    // (like logReadData)
    if (retval == -1) errno = GET_COMMON(my_return_entry, my_errno);
  }
  return retval;
}

/* The list of strings: each string is a format, like for example %d or %lf.
 * This function deals with the following possible formats:
 * with or without whitespace delimited, eg: "%d%d" or "%d   %d".  */
static void parse_format (const char *format, dmtcp::list<dmtcp::string> *formats)
{
  int start = 0;
  int i;
  /* An argument format is delimited by expecting_start and expecting_end.
   * When expecting_start is true, that means we are about to begin a new
   * argument format. When expecting_end is true, we are expecting the
   * end of the argument format. expecting_start and expecting_end have
   * always opposite values. */
  bool expecting_start = true;
  bool expecting_end = false;
  char tmp[128];

  for ( i = 0; i < strlen(format); i++) {
    if (format[i] == '%') {
      if (expecting_end) {
        memset(tmp, 0, 128);
        memcpy(tmp, &format[start], i - start);
        formats->push_back(dmtcp::string(tmp));
        start = i;
      } else {
        start = i;
        expecting_end = true;
        expecting_start = false;
      }
      continue;
    }
    /* For formats like "%.2lf". */
    if (isdigit(format[i]) || format[i] == '.') continue;
    if (format[i] == ' ' || format[i] == '\t') {
      if (expecting_end) {
        expecting_end = false;
        expecting_start = true;
        memset(tmp, 0, 128);
        memcpy(tmp, &format[start], i - start);
        formats->push_back(dmtcp::string(tmp));
      }
      continue;
    }
  }
  /* This is for the last argument format in the list */
  if (!expecting_start && expecting_end) {
    memset(tmp, 0, 128);
    memcpy(tmp, &format[start], i - start);
    formats->push_back(dmtcp::string(tmp));
  }
}

/* TODO: not all formats are mapped. 
 * This function parses the given argument list and logs the values of the 
 * arguments in the list to read_data_fd. Returns the number of bytes written. */
static int parse_va_list_and_log (va_list arg, const char *format)
{
  dmtcp::list<dmtcp::string> formats;
  parse_format (format, &formats);

  dmtcp::list<dmtcp::string>::iterator it;
  int bytes = 0;
  //int bytes_to_write;
  //char tmp[1024] = { '\0' };

  /* The list arg is made up of pointers to variables because the list arg
   * resulted as a call to fscanf. Thus we need to extract the address for
   * each argument and cast it to the corresponding type. */
  for (it = formats.begin(); it != formats.end(); it++) {
    /* Get next argument in the list. */
    long int *val = va_arg(arg, long int *);
    //memset (tmp, 0, 1024);
    if (it->find("lf") != dmtcp::string::npos) {
      //bytes_to_write = sprintf (tmp, it->c_str(), *(double *)val);
      logReadData ((double *)val, sizeof(double));
      bytes += sizeof(double);
    }
    else if (it->find("d") != dmtcp::string::npos) {
      //bytes_to_write = sprintf (tmp, it->c_str(), *(int *)val);
      logReadData ((int *)val, sizeof(int));
      bytes += sizeof(int);
    }
    else {
      JASSERT (false).Text("format not added.");
    }
    //logReadData (tmp, bytes_to_write);
    //bytes += bytes_to_write;
  }
  return bytes;
}

/* Parses the format string and reads into the given va_list of arguments. 
  */
static void read_data_from_log_into_va_list (va_list arg, const char *format)
{
  dmtcp::list<dmtcp::string>::iterator it;
  dmtcp::list<dmtcp::string> formats;
  char tmp[1024] = { '\0' };

  parse_format (format, &formats);
  readAll(read_data_fd, tmp, GET_FIELD(currentLogEntry, fscanf, bytes));
  /* The list arg is made up of pointers to variables because the list arg
   * resulted as a call to fscanf. Thus we need to extract the address for
   * each argument and cast it to the corresponding type. */
  for (it = formats.begin(); it != formats.end(); it++) {
    /* Get next argument in the list. */
    long int *val = va_arg(arg, long int *);
    if (it->find("lf") != dmtcp::string::npos) {
      read(read_data_fd, (void *)val, sizeof(double));
    }
    else if (it->find("d") != dmtcp::string::npos) {
      read(read_data_fd, (void *)val, sizeof(int));
    }
    else {
      JASSERT (false).Text("format not added.");
    }
  }
}

/* fscanf seems to be #define'ed into this. */
extern "C" int __isoc99_fscanf (FILE *stream, const char *format, ...)
{
  void *return_addr = GET_RETURN_ADDRESS();
  if (!shouldSynchronize(return_addr)) {
    va_list arg;
    va_start (arg, format);
    int retval = vfscanf(stream, format, arg);
    va_end (arg);
    return retval;
  }
  if (jalib::Filesystem::GetProgramName() == "gdb") {
    va_list arg;
    va_start (arg, format);
    int retval = vfscanf(stream, format, arg);
    va_end (arg);
    return retval;
  }
  int retval;
  log_entry_t my_entry = create_fscanf_entry(my_clone_id,
      fscanf_event, stream, format);
  log_entry_t my_return_entry = create_fscanf_entry(my_clone_id,
      fscanf_event_return, stream, format);
  if (SYNC_IS_REPLAY) {
    waitForTurn(my_entry, &fscanf_turn_check);
    getNextLogEntry();
    waitForTurn(my_return_entry, &fscanf_turn_check);
    if (__builtin_expect(read_data_fd == -1, 0)) {
      read_data_fd = open(SYNCHRONIZATION_READ_DATA_LOG_PATH, O_RDONLY);
    }
    JASSERT ( read_data_fd != -1 );
    lseek(read_data_fd, GET_FIELD(currentLogEntry,fscanf,data_offset), SEEK_SET);
    if ((GET_FIELD(currentLogEntry, fscanf, retval) != EOF) ||
        (GET_COMMON(currentLogEntry, my_errno) == 0)) {
      va_list arg;
      va_start (arg, format);
      read_data_from_log_into_va_list (arg, format);
      va_end(arg);
      retval = GET_FIELD(currentLogEntry, fscanf, retval);
    } else {
      retval = EOF;
    }
    // Set the errno to what was logged (e.g. EINTR).
    if (GET_COMMON(currentLogEntry, my_errno) != 0) {
      errno = GET_COMMON(currentLogEntry, my_errno);
    }
    getNextLogEntry();
  } else if (SYNC_IS_LOG) {
    addNextLogEntry(my_entry);
    va_list arg;
    va_start (arg, format);
    retval = vfscanf(stream, format, arg);
    va_end (arg);
    SET_FIELD(my_return_entry, fscanf, retval);
    if (retval == EOF) {
      SET_COMMON2(my_return_entry, my_errno, errno);
    } else {
      SET_FIELD2(my_return_entry, fscanf, data_offset, read_log_pos);
      va_start (arg, format);
      int bytes = parse_va_list_and_log(arg, format);
      va_end (arg);
      SET_FIELD(my_return_entry, fscanf, bytes);
    }
    addNextLogEntry(my_return_entry);
    if (retval == EOF) errno = GET_COMMON(my_return_entry, my_errno);
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
    _real_pthread_mutex_lock(&read_data_mutex);
    if (retval == NULL) {
      SET_COMMON2(my_return_entry, my_errno, errno);
    } else {
      SET_FIELD2(my_return_entry, fgets, data_offset, read_log_pos);
      logReadData(s, size);
    }
    _real_pthread_mutex_unlock(&read_data_mutex);
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

extern "C" int getc(FILE *stream)
{
  BASIC_SYNC_WRAPPER(int, getc, _real_getc, stream);
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
