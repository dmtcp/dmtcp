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

#include <stdarg.h>
#include <stdlib.h>
#include <vector>
#include <list>
#include <string>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/version.h>
#include "uniquepid.h"
#include "dmtcpworker.h"
#include "dmtcpmessagetypes.h"
#include "protectedfds.h"
#include "constants.h"
#include "connectionmanager.h"
#include "syscallwrappers.h"
#include "sysvipc.h"
#include  "../jalib/jassert.h"
#include  "../jalib/jconvert.h"

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
}

extern "C" int fclose(FILE *fp)
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

/* epoll is currently not supported by DMTCP */
extern "C" int epoll_create(int size)
{
  JWARNING (false) .Text("epoll is currently not supported by DMTCP.");
  errno = EPERM;
  return -1;
}

extern "C" int socketpair ( int d, int type, int protocol, int sv[2] )
{
  WRAPPER_EXECUTION_LOCK_LOCK();

  JASSERT ( sv != NULL );
  int rv = _real_socketpair ( d,type,protocol,sv );
  JTRACE ( "socketpair()" ) ( sv[0] ) ( sv[1] );

  dmtcp::TcpConnection *a, *b;

  a = new dmtcp::TcpConnection ( d, type, protocol );
  a->onConnect();
  b = new dmtcp::TcpConnection ( *a, a->id() );

  dmtcp::KernelDeviceToConnection::instance().create ( sv[0] , a );
  dmtcp::KernelDeviceToConnection::instance().create ( sv[1] , b );

  WRAPPER_EXECUTION_LOCK_UNLOCK();

  return rv;
}

extern "C" int pipe ( int fds[2] )
{
  JTRACE ( "promoting pipe() to socketpair()" );
  //just promote pipes to socketpairs
  return socketpair ( AF_UNIX, SOCK_STREAM, 0, fds );
}

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
  static char tmpbuf[1024];

  if ( ptsname_r ( fd, tmpbuf, sizeof ( tmpbuf ) ) != 0 )
  {
    return NULL;
  }

  return tmpbuf;
}

extern "C" int ptsname_r ( int fd, char * buf, size_t buflen )
{
  WRAPPER_EXECUTION_LOCK_LOCK();

  int retVal = ptsname_r_work(fd, buf, buflen);

  WRAPPER_EXECUTION_LOCK_UNLOCK();

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

  if ( strncmp ( path, "/proc/", 6 ) == 0 )
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

extern "C" int open (const char *path, int flags, ... )
{
  va_list ap;
  //int flags;
  mode_t mode;
  int rc;
  char newpath [ 1024 ] = {0} ;
  int len,i;

  // Handling the variable number of arguments
  va_start( ap, flags );
  //flags = va_arg ( ap, int );
  mode = va_arg ( ap, mode_t );
  va_end ( ap );

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

  WRAPPER_EXECUTION_LOCK_LOCK();

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

  WRAPPER_EXECUTION_LOCK_UNLOCK();

  return fd;
}

extern "C" FILE *fopen (const char* path, const char* mode)
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

  WRAPPER_EXECUTION_LOCK_LOCK();

  char newpath [ 1024 ] = {0} ;
  int fd = -1;

  if ( strncmp(path, UNIQUE_PTS_PREFIX_STR, strlen(UNIQUE_PTS_PREFIX_STR)) == 0 ) {
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
  } else if ( fd >= 0 && strncmp(path, UNIQUE_PTS_PREFIX_STR, strlen(UNIQUE_PTS_PREFIX_STR)) == 0 ) {
    processDevPtsConnection(fd, path, newpath);
  }

  WRAPPER_EXECUTION_LOCK_UNLOCK();

  return file;
}

#ifdef ENABLE_MALLOC_WRAPPER
# ifdef ENABLE_DLOPEN
#  error "ENABLE_MALLOC_WRAPPER can't work with ENABLE_DLOPEN"
# endif
extern "C" void *calloc(size_t nmemb, size_t size)
{
  WRAPPER_EXECUTION_LOCK_LOCK();
  void *retVal = _real_calloc ( nmemb, size );
  WRAPPER_EXECUTION_LOCK_UNLOCK();
  return retVal;
}
extern "C" void *malloc(size_t size)
{
  WRAPPER_EXECUTION_LOCK_LOCK();
  void *retVal = _real_malloc ( size );
  WRAPPER_EXECUTION_LOCK_UNLOCK();
  return retVal;
}
extern "C" void free(void *ptr)
{
  WRAPPER_EXECUTION_LOCK_LOCK();
  _real_free ( ptr );
  WRAPPER_EXECUTION_LOCK_UNLOCK();
}
extern "C" void *realloc(void *ptr, size_t size)
{
  WRAPPER_EXECUTION_LOCK_LOCK();
  void *retVal = _real_realloc ( ptr, size );
  WRAPPER_EXECUTION_LOCK_UNLOCK();
  return retVal;
}
#endif


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
  WRAPPER_EXECUTION_LOCK_LOCK();
  int retVal = _real_vfprintf ( s, format, ap );
  WRAPPER_EXECUTION_LOCK_UNLOCK();
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

#ifndef DISABLE_SYS_V_IPC
extern "C"
int shmget(key_t key, size_t size, int shmflg)
{
  int ret;
  WRAPPER_EXECUTION_LOCK_LOCK();
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
  WRAPPER_EXECUTION_LOCK_UNLOCK();
  return ret;
}

extern "C"
void *shmat(int shmid, const void *shmaddr, int shmflg)
{
  WRAPPER_EXECUTION_LOCK_LOCK();
  int currentShmid = dmtcp::SysVIPC::instance().originalToCurrentShmid(shmid);
  JASSERT(currentShmid != -1);
  void *ret = _real_shmat(currentShmid, shmaddr, shmflg);
  if (ret != (void *) -1) {
    dmtcp::SysVIPC::instance().on_shmat(shmid, shmaddr, shmflg, ret);
    JTRACE ("Mapping Shared memory segment" ) (shmid) (shmflg) (ret);
  }
  WRAPPER_EXECUTION_LOCK_UNLOCK();
  return ret;
}

extern "C"
int shmdt(const void *shmaddr)
{
  WRAPPER_EXECUTION_LOCK_LOCK();
  int ret = _real_shmdt(shmaddr);
  if (ret != -1) {
    dmtcp::SysVIPC::instance().on_shmdt(shmaddr);
    JTRACE ("Unmapping Shared memory segment" ) (shmaddr);
  }
  WRAPPER_EXECUTION_LOCK_UNLOCK();
  return ret;
}

extern "C"
int shmctl(int shmid, int cmd, struct shmid_ds *buf)
{
  WRAPPER_EXECUTION_LOCK_LOCK();
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
  WRAPPER_EXECUTION_LOCK_UNLOCK();
  return ret;
}
#endif

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
