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


#define _GNU_SOURCE
#define _XOPEN_SOURCE 500
// These next two are defined in features.h based on the user macros above.
// #define GNU_SRC
// #define __USE_UNIX98

#include <pthread.h>
#include "syscallwrappers.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "constants.h"
#include "sockettable.h"
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <ctype.h>
#include <assert.h>
#ifdef RECORD_REPLAY
#include <sys/mman.h>
#include <dirent.h>
#include <time.h>
#define open _libc_open
#include <fcntl.h>
#undef open
#endif
typedef int ( *funcptr_t ) ();
typedef pid_t ( *funcptr_pid_t ) ();
typedef funcptr ( *signal_funcptr_t ) ();

static unsigned int libcFuncOffsetArray[numLibcWrappers];
#ifdef RECORD_REPLAY
static long libpthreadFuncOffsetArray[numLibpthreadWrappers];

#endif

static pthread_mutex_t theMutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

/*
static print_mutex(pthread_mutex_t *m,char *func)
{
        int i = 0;
        printf("theMutex(%s) internals: ",func);
        for(i=0;i<sizeof(pthread_mutex_t);i++){
                printf("%02x ",*((char*)m + i) );
        }
        printf("\n");
}
*/

#ifdef RECORD_REPLAY
// Need these prototypes for _dmtcp_lock/unlock().
int _real_pthread_mutex_lock(pthread_mutex_t *mutex);
int _real_pthread_mutex_unlock(pthread_mutex_t *mutex);
void _dmtcp_lock() { _real_pthread_mutex_lock ( &theMutex ); }
void _dmtcp_unlock() { _real_pthread_mutex_unlock ( &theMutex ); }
#else
void _dmtcp_lock() { pthread_mutex_lock ( &theMutex ); }

void _dmtcp_unlock() { pthread_mutex_unlock ( &theMutex ); }
#endif

/*
  if( ret == EPERM ){
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE_NP);
        pthread_mutex_init(&theMutex,&attr);
  }
}
*/

void _dmtcp_remutex_on_fork() {
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);
  pthread_mutex_init ( &theMutex, &attr);
  pthread_mutexattr_destroy(&attr);
}

// Any single glibc function could have been re-defined.
// For example, isalnum is wrapped by /bin/dash.  So, find a function
//   closest to the average, and it's probably in glibc.
static char * get_libc_base_func ()
{
  static char * base_addr = NULL;
  void *base_func_addresses[100];
  unsigned long int average, minDist;
  int minIndex;
  int count, index;
  if (base_addr != NULL)
    return base_addr;

  count = 0;
# define _COUNT(name) count++;
  FOREACH_GLIBC_BASE_FUNC(_COUNT)
  assert(count <= 100);
  index = 0;
# define _GET_ADDR(name) base_func_addresses[index++] = &name;
  FOREACH_GLIBC_BASE_FUNC(_GET_ADDR)

# define SHIFT_RIGHT(x) (long int)((unsigned long int)(x) >> 10)
  // Of all the addresses in base_func_addresses, we choose the one
  //  closest to the average as the one that really is in glibc.
  for (average = 0, index = 0; index < count; index++)
    average += SHIFT_RIGHT(base_func_addresses[index]);
  average /= count;

  minIndex = 0;
  /* Shift right to guard against overflow */
  minDist = labs(average - SHIFT_RIGHT(base_func_addresses[0]));
  for (index = 0; index < count; index++) {
    unsigned int tmp = labs(average - SHIFT_RIGHT(base_func_addresses[index]));
    if (tmp < minDist) {
      minIndex = index;
      minDist = tmp;
    }
  }

  base_addr = base_func_addresses[minIndex] - libcFuncOffsetArray[minIndex];
  /* FIXME:  Should verify that base_addr is in libc seg of /proc/self/maps */
  assert(base_addr + libcFuncOffsetArray[minIndex]
         == base_func_addresses[minIndex]);
  return base_addr;
}

static void computeLibcOffsetArray ()
{
  char* libcFuncOffsetStr = getenv(ENV_VAR_LIBC_FUNC_OFFSETS);
  char *start;
  char *next;
  int count;

  assert(libcFuncOffsetStr != NULL);

  start = libcFuncOffsetStr;
  for (count = 0; count < numLibcWrappers; count++) {
    libcFuncOffsetArray[count] = strtoul(start, &next, 16);
    if (*next != ';') break;
    start = next + 1;
  }
  if (*start != '\0') count++;

  assert(count == numLibcWrappers);
}

static funcptr_t get_libc_symbol_from_array ( LibcWrapperOffset idx )
{
  static int libcOffsetArrayComputed = 0;
  static char *libc_base_func_addr = NULL;
  if (libcOffsetArrayComputed == 0) {
    computeLibcOffsetArray();
    // Important:  call computeLibcOffsetArray() before get_libc_base_func()
    libc_base_func_addr = get_libc_base_func();
    libcOffsetArrayComputed = 1;
  }
  if (libcFuncOffsetArray[idx] == -1) {
    return NULL;
  }
  return (funcptr_t)(libc_base_func_addr + libcFuncOffsetArray[idx]);
}

static funcptr_t get_libc_symbol ( const char* name )
{
  static void* handle = NULL;
  if ( handle==NULL && ( handle=dlopen ( LIBC_FILENAME,RTLD_NOW ) ) == NULL )
  {
    fprintf ( stderr, "dmtcp: get_libc_symbol: ERROR in dlopen: %s \n",
              dlerror() );
    abort();
  }

#ifdef PTRACE
  void* tmp = _real_dlsym ( handle, name );
#else
  void* tmp = dlsym ( handle, name );
#endif

  if ( tmp == NULL )
  {
    fprintf ( stderr, "dmtcp: get_libc_symbol: ERROR in dlsym: %s \n",
              dlerror() );
    abort();
  }
  return ( funcptr_t ) tmp;
}

static funcptr_t get_libpthread_symbol ( const char* name )
{
  static void* handle = NULL;
  if ( handle==NULL &&
       ( handle=dlopen ( LIBPTHREAD_FILENAME, RTLD_NOW ) ) == NULL ) {
    fprintf ( stderr, "dmtcp: get_libpthread_symbol: ERROR in dlopen: %s \n",
              dlerror() );
    abort();
  }

  void* tmp = dlsym ( handle, name );
  if ( tmp == NULL ) {
    fprintf ( stderr, "dmtcp: get_libpthread_symbol: ERROR in dlsym: %s \n",
              dlerror() );
    abort();
  }
  return ( funcptr_t ) tmp;
}

#ifdef RECORD_REPLAY
static void computeLibpthreadOffsetArray ()
{
  char *libpthreadFuncOffsetStr = getenv(ENV_VAR_LIBPTHREAD_FUNC_OFFSETS);
  char *start;
  char *next;
  int count;
  
  assert(libpthreadFuncOffsetStr != NULL);

  start = libpthreadFuncOffsetStr;
  for (count = 0; count < numLibpthreadWrappers; count++) {
    libpthreadFuncOffsetArray[count] = strtol(start, &next, 10);
    if (*next != ';') break;
    start = next + 1;
  }
  if (*start != '\0') count++;
  
  assert(count == numLibpthreadWrappers);
}

static funcptr_t get_libpthread_symbol_from_array (LibPthreadWrapperOffset off)
{
  static int libpthreadOffsetArrayComputed = 0;
  static char *libpthread_base_function_addr = NULL;
  if (libpthreadOffsetArrayComputed == 0) {
    computeLibpthreadOffsetArray();
    libpthread_base_function_addr = (char *)&LIBPTHREAD_BASE_FUNC;
    libpthreadOffsetArrayComputed = 1;
  }
  return (funcptr_t)((char*)libpthread_base_function_addr
		     + libpthreadFuncOffsetArray[off]);
}
#endif //RECORD_REPLAY

//////////////////////////
//// FIRST DEFINE REAL VERSIONS OF NEEDED FUNCTIONS

#ifdef ENABLE_DLOPEN
static int use_dlsym = 1;
#else
static int use_dlsym = 0;
#endif

#define REAL_FUNC_PASSTHROUGH(name)  REAL_FUNC_PASSTHROUGH_TYPED(int, name)

#define REAL_FUNC_PASSTHROUGH_TYPED(type,name) static type (*fn) () = NULL; \
    if (fn==NULL) { \
      if (use_dlsym) \
        fn = (void*)get_libc_symbol(#name); \
      else \
        fn = (void*)get_libc_symbol_from_array ( ENUM(name) ); \
      if (fn == NULL) { \
        fprintf(stderr, "*** DMTCP: Error: glibc symbol lookup failed for %s.\n" \
                        "           The symbol wasn't found in current glibc.\n" \
                        "    Aborting.\n", #name); \
      } \
    } \
    return (*fn)

#define REAL_FUNC_PASSTHROUGH_VOID(name) static funcptr_t fn = NULL; \
    if (fn==NULL) { \
      if (use_dlsym) \
        fn = get_libc_symbol(#name); \
      else \
        fn = get_libc_symbol_from_array ( ENUM(name) ); \
      if (fn == NULL) { \
        fprintf(stderr, "*** DMTCP: Error: glibc symbol lookup failed for %s.\n" \
                        "           The symbol wasn't found in current glibc.\n" \
                        "    Aborting.\n", #name); \
      } \
    } \
    (*fn)

#define LIBPTHREAD_REAL_FUNC_PASSTHROUGH_TYPED(type,name) \
    static type (*fn) () = NULL; \
    if (fn==NULL) fn = (void *)get_libpthread_symbol(#name); \
    return (*fn)

#ifdef RECORD_REPLAY
#undef LIBPTHREAD_REAL_FUNC_PASSTHROUGH_TYPED
#define LIBPTHREAD_REAL_FUNC_PASSTHROUGH_TYPED(type,name) \
    static type (*fn) () = NULL; \
    if (fn==NULL) { \
      if (use_dlsym) \
        fn = (void*)get_libpthread_symbol(#name); \
      else \
        fn = (void*)get_libpthread_symbol_from_array ( ENUM(name) ); \
    } \
    return (*fn)

#define LIBPTHREAD_REAL_FUNC_PASSTHROUGH_VOID(name) \
    static funcptr_t fn = NULL; \
    if (fn==NULL) { \
      if (use_dlsym) \
        fn = (void*)get_libpthread_symbol(#name); \
      else \
        fn = (void*)get_libpthread_symbol_from_array ( ENUM(name) ); \
    } \
    (*fn)
#else // else not RECORD_REPLAY
#define LIBPTHREAD_REAL_FUNC_PASSTHROUGH_TYPED(type,name) \
    static type (*fn) () = NULL; \
    if (fn==NULL) fn = (void *)get_libpthread_symbol(#name); \
    return (*fn)
#endif // end RECORD_REPLAY

/// call the libc version of this function via dlopen/dlsym
int _real_socket ( int domain, int type, int protocol )
{
  REAL_FUNC_PASSTHROUGH ( socket ) ( domain,type,protocol );
}

/// call the libc version of this function via dlopen/dlsym
int _real_connect ( int sockfd, const struct sockaddr *serv_addr,
                    socklen_t addrlen )
{
  REAL_FUNC_PASSTHROUGH ( connect ) ( sockfd,serv_addr,addrlen );
}

/// call the libc version of this function via dlopen/dlsym
int _real_bind ( int sockfd, const struct sockaddr *my_addr,
                 socklen_t addrlen )
{
  REAL_FUNC_PASSTHROUGH ( bind ) ( sockfd,my_addr,addrlen );
}

/// call the libc version of this function via dlopen/dlsym
int _real_listen ( int sockfd, int backlog )
{
  REAL_FUNC_PASSTHROUGH ( listen ) ( sockfd,backlog );
}

/// call the libc version of this function via dlopen/dlsym
int _real_accept ( int sockfd, struct sockaddr *addr, socklen_t *addrlen )
{
  REAL_FUNC_PASSTHROUGH ( accept ) ( sockfd,addr,addrlen );
}

//#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)) && __GLIBC_PREREQ(2,10)
/// call the libc version of this function via dlopen/dlsym
int _real_accept4 ( int sockfd, struct sockaddr *addr, socklen_t *addrlen,
                    int flags )
{
  REAL_FUNC_PASSTHROUGH ( accept4 ) ( sockfd,addr,addrlen,flags );
}
//#endif

#ifdef RECORD_REPLAY
int _real_getsockname( int sockfd, struct sockaddr *addr, socklen_t *addrlen )
{
  REAL_FUNC_PASSTHROUGH ( getsockname ) ( sockfd,addr,addrlen );
}

int _real_getpeername( int sockfd, struct sockaddr *addr, socklen_t *addrlen )
{
  REAL_FUNC_PASSTHROUGH ( getpeername ) ( sockfd,addr,addrlen );
}
#endif

/// call the libc version of this function via dlopen/dlsym
int _real_setsockopt ( int s, int level, int optname, const void *optval,
                       socklen_t optlen )
{
  REAL_FUNC_PASSTHROUGH ( setsockopt ) ( s,level,optname,optval,optlen );
}

int _real_fexecve ( int fd, char *const argv[], char *const envp[] )
{
  REAL_FUNC_PASSTHROUGH ( fexecve ) ( fd,argv,envp );
}

int _real_execve ( const char *filename, char *const argv[],
                   char *const envp[] )
{
  REAL_FUNC_PASSTHROUGH ( execve ) ( filename,argv,envp );
}

int _real_execv ( const char *path, char *const argv[] )
{
  REAL_FUNC_PASSTHROUGH ( execv ) ( path,argv );
}

int _real_execvp ( const char *file, char *const argv[] )
{
  REAL_FUNC_PASSTHROUGH ( execvp ) ( file,argv );
}

int _real_system ( const char *cmd )
{
  REAL_FUNC_PASSTHROUGH ( system ) ( cmd );
}

pid_t _real_fork( void )
{
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, fork ) ();
}

int _real_close ( int fd )
{
  REAL_FUNC_PASSTHROUGH ( close ) ( fd );
}

int _real_fclose ( FILE *fp )
{
  REAL_FUNC_PASSTHROUGH ( fclose ) ( fp );
}

void _real_exit ( int status )
{
  REAL_FUNC_PASSTHROUGH_VOID ( exit ) ( status );
}

int _real_getpt ( void )
{
  REAL_FUNC_PASSTHROUGH ( getpt ) ( );
}

int _real_ptsname_r ( int fd, char * buf, size_t buflen )
{
  REAL_FUNC_PASSTHROUGH ( ptsname_r ) ( fd, buf, buflen );
}

int _real_socketpair ( int d, int type, int protocol, int sv[2] )
{
  REAL_FUNC_PASSTHROUGH ( socketpair ) ( d,type,protocol,sv );
}

void _real_openlog ( const char *ident, int option, int facility )
{
  REAL_FUNC_PASSTHROUGH_VOID ( openlog ) ( ident,option,facility );
}

void _real_closelog ( void )
{
  REAL_FUNC_PASSTHROUGH_VOID ( closelog ) ();
}

//set the handler
sighandler_t _real_signal(int signum, sighandler_t handler){
  REAL_FUNC_PASSTHROUGH_TYPED ( sighandler_t, signal ) (signum, handler);
}
int _real_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact){
  REAL_FUNC_PASSTHROUGH ( sigaction ) ( signum, act, oldact );
}
//int _real_rt_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact){
//  REAL_FUNC_PASSTHROUGH ( rt_sigaction ) ( signum, act, oldact );
//}
int _real_sigvec(int signum, const struct sigvec *vec, struct sigvec *ovec){
  REAL_FUNC_PASSTHROUGH ( sigvec ) ( signum, vec, ovec );
}

//set the mask
int _real_sigblock(int mask){
  REAL_FUNC_PASSTHROUGH ( sigblock ) ( mask );
}
int _real_sigsetmask(int mask){
  REAL_FUNC_PASSTHROUGH ( sigsetmask ) ( mask );
}
int _real_siggetmask(void){
  REAL_FUNC_PASSTHROUGH ( siggetmask )( );
}
int _real_sigprocmask(int how, const sigset_t *a, sigset_t *b){
  REAL_FUNC_PASSTHROUGH ( sigprocmask ) ( how, a, b);
}
//int _real_rt_sigprocmask(int how, const sigset_t *a, sigset_t *b){
//  REAL_FUNC_PASSTHROUGH ( rt_sigprocmask ) ( how, a, b);
//}
int _real_pthread_sigmask(int how, const sigset_t *a, sigset_t *b){
  LIBPTHREAD_REAL_FUNC_PASSTHROUGH_TYPED ( int, pthread_sigmask ) ( how, a, b);
}

int _real_sigwait(const sigset_t *set, int *sig) {
  REAL_FUNC_PASSTHROUGH ( sigwait ) ( set, sig);
}
int _real_sigwaitinfo(const sigset_t *set, siginfo_t *info) {
  REAL_FUNC_PASSTHROUGH ( sigwaitinfo ) ( set, info);
}
int _real_sigtimedwait(const sigset_t *set, siginfo_t *info,
                       const struct timespec *timeout) {
  REAL_FUNC_PASSTHROUGH ( sigtimedwait ) ( set, info, timeout);
}

/* In dmtcphijack.so code always use this function instead of unsetenv.
 * Bash has its own implementation of getenv/setenv/unsetenv and keeps its own
 * environment equivalent to its shell variables. If DMTCP uses the bash
 * unsetenv, bash will unset its internal environment variable but won't remove
 * the process environment variable and yet on the next getenv, bash will
 * return the process environment variable.
 * This is arguably a bug in bash-3.2.
 */
int _dmtcp_unsetenv( const char *name ) {
  unsetenv (name);
  REAL_FUNC_PASSTHROUGH ( unsetenv ) ( name );
}

#ifdef PID_VIRTUALIZATION
pid_t _real_getpid(void){
  return (pid_t) _real_syscall(SYS_getpid);
//  REAL_FUNC_PASSTHROUGH_PID_T ( getpid ) ( );
}

pid_t _real_getppid(void){
  return (pid_t) _real_syscall(SYS_getppid);
}

int _real_tcsetpgrp(int fd, pid_t pgrp){
  REAL_FUNC_PASSTHROUGH ( tcsetpgrp ) ( fd, pgrp );
}

int _real_tcgetpgrp(int fd) {
  REAL_FUNC_PASSTHROUGH ( tcgetpgrp ) ( fd );
}

pid_t _real_getpgrp(void) {
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, getpgrp ) ( );
}

pid_t _real_setpgrp(void) {
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, setpgrp ) ( );
}

pid_t _real_getpgid(pid_t pid) {
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, getpgid ) ( pid );
}

int   _real_setpgid(pid_t pid, pid_t pgid) {
  REAL_FUNC_PASSTHROUGH ( setpgid ) ( pid, pgid );
}

pid_t _real_getsid(pid_t pid) {
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, getsid ) ( pid );
}

pid_t _real_setsid(void) {
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, setsid ) ( );
}

int   _real_kill(pid_t pid, int sig) {
  REAL_FUNC_PASSTHROUGH ( kill ) ( pid, sig );
}

pid_t _real_wait(__WAIT_STATUS stat_loc) {
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, wait ) ( stat_loc );
}

pid_t _real_waitpid(pid_t pid, int *stat_loc, int options) {
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, waitpid ) ( pid, stat_loc, options );
}

int   _real_waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options) {
  REAL_FUNC_PASSTHROUGH ( waitid ) ( idtype, id, infop, options );
}

pid_t _real_wait3(__WAIT_STATUS status, int options, struct rusage *rusage) {
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, wait3 ) ( status, options, rusage );
}

pid_t _real_wait4(pid_t pid, __WAIT_STATUS status, int options, struct rusage *rusage) {
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, wait4 ) ( pid, status, options, rusage );
}

int _real_ioctl(int d, unsigned long int request, ...) {
  void * arg;
  va_list ap;

  // Most calls to ioctl take 'void *', 'int' or no extra argument
  // A few specialized ones take more args, but we don't need to handle those.
  va_start(ap, request);
  arg = va_arg(ap, void *);
  va_end(ap);

  // /usr/include/unistd.h says syscall returns long int (contrary to man page)
  REAL_FUNC_PASSTHROUGH_TYPED ( int, ioctl ) ( d, request, arg );
}

int _real_setgid(gid_t gid) {
  REAL_FUNC_PASSTHROUGH( setgid ) (gid);
}

int _real_setuid(uid_t uid) {
  REAL_FUNC_PASSTHROUGH( setuid ) (uid);
}

#endif

// gettid / tkill / tgkill are not defined in libc.
// So, this is needed even if there is no PID_VIRTUALIZATION
pid_t _real_gettid(void){
  return (pid_t) _real_syscall(SYS_gettid);
//  REAL_FUNC_PASSTHROUGH ( pid_t, syscall(SYS_gettid) );
}

int   _real_tkill(int tid, int sig) {
  return (int) _real_syscall(SYS_tkill, tid, sig);
  //REAL_FUNC_PASSTHROUGH ( syscall(SYS_tkill) ) ( tid, sig );
}

int   _real_tgkill(int tgid, int tid, int sig) {
  return (int) _real_syscall(SYS_tgkill, tgid, tid, sig);
  //REAL_FUNC_PASSTHROUGH ( tgkill ) ( tgid, tid, sig );
}

int _real_open( const char *pathname, int flags, mode_t mode ) {
  REAL_FUNC_PASSTHROUGH ( open ) ( pathname, flags, mode );
}

int _real_open64( const char *pathname, int flags, mode_t mode ) {
  REAL_FUNC_PASSTHROUGH ( open ) ( pathname, flags, mode );
}

FILE * _real_fopen( const char *path, const char *mode ) {
  REAL_FUNC_PASSTHROUGH_TYPED ( FILE *, fopen ) ( path, mode );
}

FILE * _real_fopen64( const char *path, const char *mode ) {
  REAL_FUNC_PASSTHROUGH_TYPED ( FILE *, fopen64 ) ( path, mode );
}

#ifdef RECORD_REPLAY
int _real_closedir(DIR *dirp) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, closedir ) ( dirp );
}

DIR * _real_opendir(const char *name) {
  REAL_FUNC_PASSTHROUGH_TYPED ( DIR *, opendir ) ( name );
}

int _real_mkdir(const char *pathname, mode_t mode) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, mkdir ) ( pathname, mode );
}

int _real_mkstemp(char *temp) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, mkstemp ) ( temp );
}

FILE * _real_fdopen(int fd, const char *mode) {
  REAL_FUNC_PASSTHROUGH_TYPED ( FILE *, fdopen ) ( fd, mode );
}

char * _real_fgets(char *s, int size, FILE *stream) {
  REAL_FUNC_PASSTHROUGH_TYPED ( char *, fgets ) ( s, size, stream );
}

int _real_fflush(FILE *stream) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, fflush ) ( stream );
}

int _real_fdatasync(int fd) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, fdatasync ) ( fd );
}

int _real_fsync(int fd) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, fsync ) ( fd );
}

int _real_getc(FILE *stream) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, getc ) ( stream );
}

int _real_gettimeofday(struct timeval *tv, struct timezone *tz) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, gettimeofday ) ( tv, tz );
}

int _real_fgetc(FILE *stream) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, fgetc ) ( stream );
}

int _real_ungetc(int c, FILE *stream) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, ungetc ) ( c, stream );
}

ssize_t _real_getline(char **lineptr, size_t *n, FILE *stream) {
  REAL_FUNC_PASSTHROUGH_TYPED ( ssize_t, getline ) ( lineptr, n, stream );
}

int _real_link(const char *oldpath, const char *newpath) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, link ) ( oldpath, newpath );
}

int _real_rename(const char *oldpath, const char *newpath) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, rename ) ( oldpath, newpath );
}

void _real_rewind(FILE *stream) {
  REAL_FUNC_PASSTHROUGH_VOID ( rewind ) ( stream );
}

int _real_rmdir(const char *pathname) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, rmdir ) ( pathname );
}

long _real_ftell(FILE *stream) {
  REAL_FUNC_PASSTHROUGH_TYPED ( long, ftell ) ( stream );
}

int _real_fputs(const char *s, FILE *stream) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, fputs ) ( s, stream );
}

int _real_putc(int c, FILE *stream) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, putc ) ( c, stream );
}

size_t _real_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
  REAL_FUNC_PASSTHROUGH_TYPED ( size_t, fwrite) ( ptr, size, nmemb, stream );
}

int _real_fcntl(int fd, int cmd, ...) {
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
  if (arg_3_l == -1 && arg_3_f == NULL) {
    REAL_FUNC_PASSTHROUGH_TYPED ( int, fcntl ) ( fd, cmd );
  } else if (arg_3_l == -1) {
    REAL_FUNC_PASSTHROUGH_TYPED ( int, fcntl ) ( fd, cmd, arg_3_f);
  } else {
    REAL_FUNC_PASSTHROUGH_TYPED ( int, fcntl ) ( fd, cmd, arg_3_l);
  }
}
#endif


int _real_pthread_join(pthread_t thread, void **value_ptr)
{
  LIBPTHREAD_REAL_FUNC_PASSTHROUGH_TYPED ( int, pthread_join )
    ( thread, value_ptr );
}

/* See comments for syscall wrapper */
long int _real_syscall(long int sys_num, ... ) {
  int i;
  void * arg[7];
  va_list ap;

  va_start(ap, sys_num);
  for (i = 0; i < 7; i++)
    arg[i] = va_arg(ap, void *);
  va_end(ap);

  // /usr/include/unistd.h says syscall returns long int (contrary to man page)
  REAL_FUNC_PASSTHROUGH_TYPED ( long int, syscall ) ( sys_num, arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6] );
}

int _real_xstat(int vers, const char *path, struct stat *buf) {
  REAL_FUNC_PASSTHROUGH ( __xstat ) ( vers, path, buf );
}

int _real_xstat64(int vers, const char *path, struct stat64 *buf) {
  REAL_FUNC_PASSTHROUGH ( __xstat64 ) ( vers, path, buf );
}

#ifdef RECORD_REPLAY
int _real_fxstat(int vers, int fd, struct stat *buf) {
  REAL_FUNC_PASSTHROUGH ( __fxstat ) ( vers, fd, buf );
}

int _real_fxstat64(int vers, int fd, struct stat64 *buf) {
  REAL_FUNC_PASSTHROUGH ( __fxstat64 ) ( vers, fd, buf );
}
#endif

int _real_lxstat(int vers, const char *path, struct stat *buf) {
  REAL_FUNC_PASSTHROUGH ( __lxstat ) ( vers, path, buf );
}

int _real_lxstat64(int vers, const char *path, struct stat64 *buf) {
  REAL_FUNC_PASSTHROUGH ( __lxstat64 ) ( vers, path, buf );
}

ssize_t _real_readlink(const char *path, char *buf, size_t bufsiz) {
  REAL_FUNC_PASSTHROUGH_TYPED ( ssize_t, readlink ) ( path, buf, bufsiz );
}

int _real_clone ( int ( *function ) (void *), void *child_stack, int flags, void *arg, int *parent_tidptr, struct user_desc *newtls, int *child_tidptr )
{
  REAL_FUNC_PASSTHROUGH ( __clone ) ( function, child_stack, flags, arg, parent_tidptr, newtls, child_tidptr );
}

int _real_shmget (key_t key, size_t size, int shmflg) {
  REAL_FUNC_PASSTHROUGH ( shmget ) (key, size, shmflg);
}

void* _real_shmat (int shmid, const void *shmaddr, int shmflg) {
  REAL_FUNC_PASSTHROUGH_TYPED ( void*, shmat ) (shmid, shmaddr, shmflg);
}

int _real_shmdt (const void *shmaddr) {
  REAL_FUNC_PASSTHROUGH ( shmdt ) (shmaddr);
}

int _real_shmctl (int shmid, int cmd, struct shmid_ds *buf) {
  REAL_FUNC_PASSTHROUGH ( shmctl ) (shmid, cmd, buf);
}

#ifdef ENABLE_MALLOC_WRAPPER
# ifdef ENABLE_DLOPEN
#  error "ENABLE_MALLOC_WRAPPER can't work with ENABLE_DLOPEN"
# endif

void * _real_calloc(size_t nmemb, size_t size) {
  REAL_FUNC_PASSTHROUGH_TYPED(void*, calloc) (nmemb, size);
}

void * _real_malloc(size_t size) {
  REAL_FUNC_PASSTHROUGH_TYPED (void*, malloc) (size);
}

void * _real_realloc(void *ptr, size_t size) {
  REAL_FUNC_PASSTHROUGH_TYPED (void*, realloc) (ptr, size);
}

void * _real_libc_memalign(size_t boundary, size_t size) {
  REAL_FUNC_PASSTHROUGH_TYPED (void*, __libc_memalign) (boundary, size);
}

void _real_free(void *ptr) {
  REAL_FUNC_PASSTHROUGH_VOID (free) (ptr);
}

#ifdef RECORD_REPLAY
void *_real_mmap(void *addr, size_t length, int prot, int flags,
    int fd, off_t offset) {
  REAL_FUNC_PASSTHROUGH_TYPED (void*, mmap) (addr,length,prot,flags,fd,offset);
}

void *_real_mmap64(void *addr, size_t length, int prot, int flags,
    int fd, off64_t offset) {
  REAL_FUNC_PASSTHROUGH_TYPED (void*,mmap64) (addr,length,prot,flags,fd,offset);
}

void *_real_mremap(void *old_address, size_t old_size, size_t new_size,
    int flags, void *new_address) {
  REAL_FUNC_PASSTHROUGH_TYPED (void*, mremap)
    (old_address, old_size, new_size, flags, new_address);
}

int _real_munmap(void *addr, size_t length) {
  REAL_FUNC_PASSTHROUGH_TYPED (int, munmap) (addr, length);
}

/* A way to call mmap to ensure it is not logged/replayed. This is primarily
   used by jalloc.cpp to skip log/replay for its mmap calls. */
void *_mmap_no_sync(void *addr, size_t length, int prot, int flags,
    int fd, off_t offset)
{
  SET_MMAP_NO_SYNC(); // Needed to bypass trampoline
  void *retval = _real_mmap(addr, length, prot, flags, fd, offset);
  UNSET_MMAP_NO_SYNC();
  return retval;
}

/* This is currently just calls _real_munmap, since we don't have an munmap
   trampoline to worry about. */
int _munmap_no_sync(void *addr, size_t length)
{
  return _real_munmap(addr, length);
}

#endif
// int _real_vfprintf ( FILE *s, const char *format, va_list ap ) {
//   REAL_FUNC_PASSTHROUGH ( vfprintf ) ( s, format, ap );
// }
#endif

#ifdef PTRACE
// gdb calls dlsym on td_thr_get_info.  Need wrapper for tid virtualization.
// The fnc td_thr_get_info is in libthread_db, and not in libc.
static funcptr_t get_libthread_db_symbol ( const char* name )
{
  void* tmp = NULL;
  static void* handle = NULL;
  if ( handle==NULL && ( handle=dlopen ( LIBTHREAD_DB, RTLD_NOW ) ) == NULL )
  {
    fprintf ( stderr, "dmtcp: get_libthread_db_symbol: ERROR in dlopen: %s \n",
              dlerror() );
    abort();
  }
  tmp = _real_dlsym ( handle, name );
  if ( tmp == NULL )
  {
    fprintf ( stderr, "dmtcp: get_libthread_db_symbol: ERROR in dlsym: %s \n",
              dlerror() );
    abort();
  }
  return ( funcptr_t ) tmp;
}
// Adding the macro for calls to get_libthread_db_symbol
#define REAL_FUNC_PASSTHROUGH_TD_THR(type,name) static type (*fn) () = NULL; \
    if (fn==NULL) fn = (void *)get_libthread_db_symbol(#name); \
    return (*fn)

void *_real_dlsym ( void *handle, const char *symbol ) {
  /* In the future dlsym_offset should be global variable defined in 
     dmtcpworker.cpp. For some unclear reason doing that causes a link
     error DmtcpWorker::coordinatorId defined twice in dmtcp_coordinator.cpp 
     and  dmtcpworker.cpp. Commenting out the extra definition in 
     dmtcp_coordinator.cpp sometimes caused an seg fault on 
       dmtcp_checkpoint gdb a.out 
     when user types run in gdb.
  */
  static int dlsym_offset = 0;
  if (dlsym_offset == 0 && getenv(ENV_VAR_DLSYM_OFFSET))
  {
    dlsym_offset = ( int ) strtol ( getenv(ENV_VAR_DLSYM_OFFSET), NULL, 10 );
    /*  Couldn't unset the environment. If we try to unset it dmtcp_checkpoint
        fails to start.
    */
    //unsetenv ( ENV_VAR_DLSYM_OFFSET );
  }
  //printf ( "_real_dlsym : Inside the _real_dlsym wrapper symbol = %s \n",symbol); 
  if ( dlsym_offset == 0)
    return dlsym ( handle, symbol );
  else
  {
    typedef void* ( *fncptr ) (void *handle, const char *symbol);
    fncptr dlsym_addr = (fncptr)((char *)&LIBDL_BASE_FUNC + dlsym_offset);
    return (*dlsym_addr) ( handle, symbol );
  }
}

/* gdb calls dlsym on td_thr_get_info.  We need to wrap td_thr_get_info for
   tid virtualization. */
td_err_e _real_td_thr_get_info ( const td_thrhandle_t *th_p, td_thrinfo_t *ti_p) {
  REAL_FUNC_PASSTHROUGH_TD_THR ( td_err_e, td_thr_get_info ) ( th_p, ti_p );
}

long _real_ptrace(enum __ptrace_request request, pid_t pid, void *addr, void *data) {
  REAL_FUNC_PASSTHROUGH_TYPED ( long, ptrace ) ( request, pid, addr, data );
}
#endif

#ifdef RECORD_REPLAY
int _real_pthread_mutex_lock(pthread_mutex_t *mutex) {
  LIBPTHREAD_REAL_FUNC_PASSTHROUGH_TYPED ( int,pthread_mutex_lock ) ( mutex );
}

int _real_pthread_mutex_trylock(pthread_mutex_t *mutex) {
  LIBPTHREAD_REAL_FUNC_PASSTHROUGH_TYPED ( int,pthread_mutex_trylock ) ( mutex );
}

int _real_pthread_mutex_unlock(pthread_mutex_t *mutex) {
  LIBPTHREAD_REAL_FUNC_PASSTHROUGH_TYPED ( int,pthread_mutex_unlock ) ( mutex );
}

int _real_pthread_rwlock_unlock(pthread_rwlock_t *rwlock) {
  LIBPTHREAD_REAL_FUNC_PASSTHROUGH_TYPED ( int,pthread_rwlock_unlock ) ( rwlock );
}

int _real_pthread_rwlock_rdlock(pthread_rwlock_t *rwlock) {
  LIBPTHREAD_REAL_FUNC_PASSTHROUGH_TYPED ( int,pthread_rwlock_rdlock ) ( rwlock );
}

int _real_pthread_rwlock_wrlock(pthread_rwlock_t *rwlock) {
  LIBPTHREAD_REAL_FUNC_PASSTHROUGH_TYPED ( int,pthread_rwlock_wrlock ) ( rwlock );
}

int _real_pthread_cond_signal(pthread_cond_t *cond) {
  LIBPTHREAD_REAL_FUNC_PASSTHROUGH_TYPED ( int,pthread_cond_signal ) ( cond );
}

int _real_pthread_cond_broadcast(pthread_cond_t *cond) {
  LIBPTHREAD_REAL_FUNC_PASSTHROUGH_TYPED ( int,pthread_cond_broadcast ) ( cond );
}

int _real_pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
  LIBPTHREAD_REAL_FUNC_PASSTHROUGH_TYPED ( int,pthread_cond_wait ) ( cond,mutex );
}

int _real_pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
    const struct timespec *abstime) {
  LIBPTHREAD_REAL_FUNC_PASSTHROUGH_TYPED ( int,pthread_cond_timedwait ) ( cond,mutex,abstime );
}

int _real_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
    void *(*start_routine)(void*), void *arg) {
  LIBPTHREAD_REAL_FUNC_PASSTHROUGH_TYPED ( int,pthread_create )
    (thread,attr,start_routine,arg);
}

void _real_pthread_exit(void *value_ptr) {
  LIBPTHREAD_REAL_FUNC_PASSTHROUGH_VOID ( pthread_exit ) ( value_ptr );
}

int _real_pthread_detach(pthread_t thread)
{
  LIBPTHREAD_REAL_FUNC_PASSTHROUGH_TYPED ( int,pthread_detach ) ( thread );
}

int _real_pthread_kill(pthread_t thread, int sig)
{
  LIBPTHREAD_REAL_FUNC_PASSTHROUGH_TYPED ( int,pthread_kill )
    ( thread, sig );
}

int _real_access(const char *pathname, int mode)
{
  REAL_FUNC_PASSTHROUGH_TYPED ( int,access ) ( pathname,mode );
}

int _real_select(int nfds, fd_set *readfds, fd_set *writefds, 
    fd_set *exceptfds, struct timeval *timeout) {
  REAL_FUNC_PASSTHROUGH ( select ) ( nfds,readfds,writefds,exceptfds,timeout );
}

int _real_read(int fd, void *buf, size_t count) {
  REAL_FUNC_PASSTHROUGH ( read ) ( fd,buf,count );
}

struct dirent *_real_readdir(DIR *dirp) {
  REAL_FUNC_PASSTHROUGH_TYPED ( struct dirent *, readdir ) ( dirp );
}

int _real_readdir_r(DIR *dirp, struct dirent *entry, struct dirent **result ) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, readdir_r ) ( dirp, entry, result );
}

ssize_t _real_write(int fd, const void *buf, size_t count) {
  REAL_FUNC_PASSTHROUGH_TYPED ( ssize_t,write ) ( fd,buf,count );
}

int _real_rand(void) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int,rand ) ( );
}

void _real_srand(unsigned int seed) {
  REAL_FUNC_PASSTHROUGH_VOID ( srand ) ( seed );
}

time_t _real_time(time_t *tloc) {
  REAL_FUNC_PASSTHROUGH_TYPED ( time_t,time ) ( tloc );
}

int _real_dup(int oldfd) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int,dup ) ( oldfd );
}

off_t _real_lseek(int fd, off_t offset, int whence) {
  REAL_FUNC_PASSTHROUGH_TYPED ( off_t,lseek) ( fd,offset,whence );
}

int _real_unlink(const char *pathname) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int,unlink ) ( pathname );
}

ssize_t _real_pread(int fd, void *buf, size_t count, off_t offset) {
  REAL_FUNC_PASSTHROUGH_TYPED ( ssize_t,pread ) ( fd,buf,count,offset );
}

ssize_t _real_pwrite(int fd, const void *buf, size_t count, off_t offset) {
  REAL_FUNC_PASSTHROUGH_TYPED ( ssize_t,pwrite ) ( fd,buf,count,offset );
}


sighandler_t _real_sigset(int sig, sighandler_t disp) {
  REAL_FUNC_PASSTHROUGH_TYPED ( sighandler_t,sigset) ( sig, disp );
}
#endif // RECORD_REPLAY
