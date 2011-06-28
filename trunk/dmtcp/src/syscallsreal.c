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

#include <malloc.h>
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
typedef funcptr_t ( *signal_funcptr_t ) ();

static pthread_mutex_t theMutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

// FIXME: Are these primitives (_dmtcp_lock, _dmtcp_unlock) required anymore?
void _dmtcp_lock() { _real_pthread_mutex_lock ( &theMutex ); }
void _dmtcp_unlock() { _real_pthread_mutex_unlock ( &theMutex ); }

void _dmtcp_remutex_on_fork() {
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);
  pthread_mutex_init ( &theMutex, &attr);
  pthread_mutexattr_destroy(&attr);
}
/*
 * DMTCP puts wrappers around several libc (also libpthread, libdl etc.)
 * functions in order to work. In these wrappers, DMTCP has to do some work
 * before and after the real function is called in libc.
 *
 * In order to call the real function in libc, DMTCP calculates the address of
 * the function in libc and calls that address directly. There are several
 * techniques of calculating the address of the libc function. In this
 * document, we briefly discuss the techniques that DMTCP has used in past and
 * how it evolved into the current design.
 *
 * History:
 * 1. dlopen/dlsym: Once upon a time :-), DMTCP used to dlopen "libc.so" and
 *    then call dlsym() on the libc handle to find the addresses of the libc
 *    functions wrapped by DMTCP.
 *
 * This work great for a while until we needed wrappers for malloc/calloc/free
 * etc. The reason was the fact that dlopen/dlsym/dlerror internally call
 * calloc to allocate a small buffer. As can be seen, dlopen calls calloc which
 * goes to the DMTCP wrapper for calloc, which in turn needs to call dlopn() to
 * find the address of libc calloc and so this goes into an infinite recursion.
 *
 * 2a. Libc-offsets - take 1: To counter the problems related to malloc/calloc
 *     wrappers, DMTCP was modified to not use dlopen/dlsym. Instead, a new
 *     mechanism was implemented.
 *
 *     While executing dmtcp_checkpoint, for each function wrapped by DMTCP, we
 *     calculated it's offset, in libc, from a known base-function (toupper, a
 *     function not wrapped by DMTCP) in libc, i.e. we do:
 *       open_offset = &open - &toupper;
 *     The offsets were passed along to dmtcphijack.so in an environment
 *     variable. To calculate the address of libc function now becomes very
 *     easy -- calculate the address of base-function, and add to it the offset
 *     of the required function i.e.
 *       open_libc_address = &toupper + open_offset;
 *
 *     The environment variable holding the offsets was made available to each
 *     and every new process created via fork/exec.
 *
 * This worked fine until we discovered that some applications put a wrapper
 * around toupper as well :(.
 *
 * 2b. Libc-offsets - take 2:2b. Libc-offsets - take 2: In the next iteration,
 *     we decided to use a heuristic based approach of using a pool of libc
 *     base-functions instead of just one. An average address of base-functions
 *     was calculated and that was used in offset calculations.
 *
 * This approach was fine until we decided to support process migration. If a
 * process is migrated to a different machine with a different version of libc,
 * the offsets that are stored in memory aren't correct anymore and so if the
 * migrated process creates a child process, the offsets won't work.
 *
 * 3. dlsym(RTLD_NEXT, symbol): This is the current approach. In the _real_XYZ
 *    function, we call dlsym(RTLD_NEXT, "XYZ") to calculate the address of
 *    function in the libraries that come after the current library in the
 *    search order (see man dlsym for more details).
 *
 * There are two problem with this scheme:
 * a) As with scheme 1 (dlopen/dlsym) -- if there are wrappers around
 *    calloc/free, it goes into an infinite recursion, and
 * b). Even if we don't have wrappers around calloc, there can be a problem if
 *     some application uses the malloc_hooks.
 *     (see http://www.gnu.org/s/hello/manual/libc/Hooks-for-Malloc.html).
 *     One notable example is libopen-pal.so (part of OpenMPI) which uses
 *     malloc_hooks and in the malloc hook, it called xstat() which landed in
 *     the DMTCP wrapper for xstat() and hence an infinite recursive loop.
 *
 * The work around to these problems is described in the following section.
 *
 * ***************************************************************************
 *
 * Current Workaround:
 *
 * In order to deal with the situation where we have malloc/calloc wrappers and
 * a potential application with malloc_hooks, we need to do the following:
 *
 * 0. Initialize all wrappers (calculate libc addr) before DMTCP does anything
 *    else i.e. do it at the beginning of the DmtcpWorker constructor.
 * 1. Define a variable dmtcp_wrappers_initializing, which is set to '1' while
 *    it is initializing and '0' after the * initialization has completed.
 * 2. Always have wrappers for malloc/calloc/free.
 * 3. In the wrappers for malloc/calloc/free, make sure that malloc hooks are
 *    never called. One way to do this is to disable malloc_hooks, but since
 *    they are not thread-safe, this is not a desired solution. Also note that
 *    malloc hooks have been deprecated in glibc 2.14 and will be removed in
 *    glibc 2.15.
 *
 *    Another way to avoid malloc hooks is to allocate memory using JALLOC to
 *    avoid calling libc:malloc. But we don't want to do this for all
 *    malloc/calloc calls, and so the call to JALLOC should be made only if
 *    dmtcp_wrappers_initializing is set to '1'.
 *
 *    There is a problem with the JALLOC approach too when using RECORD_REPLAY.
 *    RECORD_REPLAY puts wrappers around mmap() etc. and JALLOC uses mmap() to
 *    allocate memory :-( and as one can guess, it gets into a infinite
 *    recursion.
 * 4. The solution is to use static buffer when dlsym() calls calloc() during
 *    wrapper-initialization. It was noted that, calloc() is called only once
 *    with buf-size of 32, during dlsym() execution and thus it is safe to keep
 *    a small static buffer and pass on its address to the caller. The
 *    subsequent call to free() is ignored.
 */

extern void prepareDmtcpWrappers();
extern int dmtcp_wrappers_initializing;
static void *_real_func_addr[numLibcWrappers];
static int _wrappers_initialized = 0;
#define GET_FUNC_ADDR(name) \
  _real_func_addr[ENUM(name)] = _real_dlsym(RTLD_NEXT, #name);

LIB_PRIVATE
void initialize_wrappers()
{
  const char *warn_msg =
    "WARNING: dmtcp_wrappers_initializing is set to '0' in the call to\n"
    "         initialize_wrappers(). This may not be a good sign. \n"
    "         Please inform dmtcp-developers if you see this message.\n\n";

  if (dmtcp_wrappers_initializing == 0) {
    _real_write(STDERR_FILENO, warn_msg, strlen(warn_msg));
    sleep(1);
    abort();
  }

  if (!_wrappers_initialized) {
    FOREACH_DMTCP_WRAPPER(GET_FUNC_ADDR);
    _wrappers_initialized = 1;
  }
}

//////////////////////////
//// FIRST DEFINE REAL VERSIONS OF NEEDED FUNCTIONS

#define REAL_FUNC_PASSTHROUGH(name)  REAL_FUNC_PASSTHROUGH_TYPED(int, name)

#define REAL_FUNC_PASSTHROUGH_TYPED(type,name) \
  static type (*fn)() = NULL; \
  if (fn == NULL) { \
    if (_real_func_addr[ENUM(name)] == NULL) prepareDmtcpWrappers(); \
    fn = _real_func_addr[ENUM(name)]; \
    if (fn == NULL) { \
      fprintf(stderr, "*** DMTCP: Error: lookup failed for %s.\n" \
                      "           The symbol wasn't found in current library" \
                      " loading sequence.\n" \
                      "    Aborting.\n", #name); \
      abort(); \
    } \
  } \
  return (*fn)

#define REAL_FUNC_PASSTHROUGH_VOID(name) \
  static void (*fn)() = NULL; \
  if (fn == NULL) { \
    if (_real_func_addr[ENUM(name)] == NULL) prepareDmtcpWrappers(); \
    fn = _real_func_addr[ENUM(name)]; \
    if (fn == NULL) { \
      fprintf(stderr, "*** DMTCP: Error: lookup failed for %s.\n" \
                      "           The symbol wasn't found in current library" \
                      " loading sequence.\n" \
                      "    Aborting.\n", #name); \
      abort(); \
    } \
  } \
  (*fn)


LIB_PRIVATE
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
  void *res = NULL;
  // Avoid calling WRAPPER_EXECUTION_DISABLE_CKPT() in calloc() wrapper. See
  // comment in miscwrappers for more details.
  thread_performing_dlopen_dlsym = 1;
  if ( dlsym_offset == 0)
    res = dlsym ( handle, symbol );
  else
  {
    typedef void* ( *fncptr ) (void *handle, const char *symbol);
    fncptr dlsym_addr = (fncptr)((char *)&LIBDL_BASE_FUNC + dlsym_offset);
    res = (*dlsym_addr) ( handle, symbol );
  }
  thread_performing_dlopen_dlsym = 0;
  return res;
}

/* In dmtcphijack.so code always use this function instead of unsetenv.
 * Bash has its own implementation of getenv/setenv/unsetenv and keeps its own
 * environment equivalent to its shell variables. If DMTCP uses the bash
 * unsetenv, bash will unset its internal environment variable but won't remove
 * the process environment variable and yet on the next getenv, bash will
 * return the process environment variable.
 * This is arguably a bug in bash-3.2.
 */
LIB_PRIVATE
int _dmtcp_unsetenv( const char *name ) {
  unsetenv (name);
  // FIXME: Fix this by getting the symbol address from libc.
  // Another way to fix this would be to do a getenv() here and put a '\0' byte
  // at the start of the returned value.
  //REAL_FUNC_PASSTHROUGH ( unsetenv ) ( name );
  char *str = (char*) getenv(name);
  if (str != NULL) *str = '\0';
  return 1;
}

LIB_PRIVATE
void *_real_dlopen(const char *filename, int flag){
  REAL_FUNC_PASSTHROUGH_TYPED ( void*, dlopen ) ( filename, flag );
}

LIB_PRIVATE
int _real_dlclose(void *handle){
  REAL_FUNC_PASSTHROUGH_TYPED ( int, dlclose ) ( handle );
}

LIB_PRIVATE
int _real_pthread_mutex_lock(pthread_mutex_t *mutex) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int,pthread_mutex_lock ) ( mutex );
}

LIB_PRIVATE
int _real_pthread_mutex_trylock(pthread_mutex_t *mutex) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int,pthread_mutex_trylock ) ( mutex );
}

LIB_PRIVATE
int _real_pthread_mutex_unlock(pthread_mutex_t *mutex) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int,pthread_mutex_unlock ) ( mutex );
}

LIB_PRIVATE
int _real_pthread_rwlock_unlock(pthread_rwlock_t *rwlock) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int,pthread_rwlock_unlock ) ( rwlock );
}

LIB_PRIVATE
int _real_pthread_rwlock_rdlock(pthread_rwlock_t *rwlock) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int,pthread_rwlock_rdlock ) ( rwlock );
}

LIB_PRIVATE
int _real_pthread_rwlock_wrlock(pthread_rwlock_t *rwlock) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int,pthread_rwlock_wrlock ) ( rwlock );
}

LIB_PRIVATE
ssize_t _real_read(int fd, void *buf, size_t count) {
  REAL_FUNC_PASSTHROUGH ( read ) ( fd,buf,count );
}

LIB_PRIVATE
ssize_t _real_write(int fd, const void *buf, size_t count) {
  REAL_FUNC_PASSTHROUGH_TYPED ( ssize_t,write ) ( fd,buf,count );
}

LIB_PRIVATE
int _real_select(int nfds, fd_set *readfds, fd_set *writefds,
                 fd_set *exceptfds, struct timeval *timeout) {
  REAL_FUNC_PASSTHROUGH ( select ) ( nfds,readfds,writefds,exceptfds,timeout );
}

LIB_PRIVATE
int _real_socket ( int domain, int type, int protocol )
{
  REAL_FUNC_PASSTHROUGH ( socket ) ( domain,type,protocol );
}

LIB_PRIVATE
int _real_connect ( int sockfd, const struct sockaddr *serv_addr,
                    socklen_t addrlen )
{
  REAL_FUNC_PASSTHROUGH ( connect ) ( sockfd,serv_addr,addrlen );
}

LIB_PRIVATE
int _real_bind ( int sockfd, const struct sockaddr *my_addr,
                 socklen_t addrlen )
{
  REAL_FUNC_PASSTHROUGH ( bind ) ( sockfd,my_addr,addrlen );
}

LIB_PRIVATE
int _real_listen ( int sockfd, int backlog )
{
  REAL_FUNC_PASSTHROUGH ( listen ) ( sockfd,backlog );
}

LIB_PRIVATE
int _real_accept ( int sockfd, struct sockaddr *addr, socklen_t *addrlen )
{
  REAL_FUNC_PASSTHROUGH ( accept ) ( sockfd,addr,addrlen );
}

LIB_PRIVATE
int _real_accept4 ( int sockfd, struct sockaddr *addr, socklen_t *addrlen,
                    int flags )
{
  REAL_FUNC_PASSTHROUGH ( accept4 ) ( sockfd,addr,addrlen,flags );
}

LIB_PRIVATE
int _real_setsockopt ( int s, int level, int optname, const void *optval,
                       socklen_t optlen )
{
  REAL_FUNC_PASSTHROUGH ( setsockopt ) ( s,level,optname,optval,optlen );
}

LIB_PRIVATE
int _real_fexecve ( int fd, char *const argv[], char *const envp[] )
{
  REAL_FUNC_PASSTHROUGH ( fexecve ) ( fd,argv,envp );
}

LIB_PRIVATE
int _real_execve ( const char *filename, char *const argv[],
                   char *const envp[] )
{
  REAL_FUNC_PASSTHROUGH ( execve ) ( filename,argv,envp );
}

LIB_PRIVATE
int _real_execv ( const char *path, char *const argv[] )
{
  REAL_FUNC_PASSTHROUGH ( execv ) ( path,argv );
}

LIB_PRIVATE
int _real_execvp ( const char *file, char *const argv[] )
{
  REAL_FUNC_PASSTHROUGH ( execvp ) ( file,argv );
}
LIB_PRIVATE
int _real_execvpe(const char *file, char *const argv[], char *const envp[]) {
  REAL_FUNC_PASSTHROUGH ( execvpe ) ( file, argv, envp );
}

LIB_PRIVATE
int _real_system ( const char *cmd )
{
  REAL_FUNC_PASSTHROUGH ( system ) ( cmd );
}

LIB_PRIVATE
pid_t _real_fork( void )
{
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, fork ) ();
}

LIB_PRIVATE
int _real_close ( int fd )
{
  REAL_FUNC_PASSTHROUGH ( close ) ( fd );
}

LIB_PRIVATE
int _real_fclose ( FILE *fp )
{
  REAL_FUNC_PASSTHROUGH ( fclose ) ( fp );
}

LIB_PRIVATE
void _real_exit ( int status )
{
  REAL_FUNC_PASSTHROUGH_VOID ( exit ) ( status );
}

LIB_PRIVATE
int _real_getpt ( void )
{
  REAL_FUNC_PASSTHROUGH ( getpt ) ( );
}

LIB_PRIVATE
int _real_ptsname_r ( int fd, char * buf, size_t buflen )
{
  REAL_FUNC_PASSTHROUGH ( ptsname_r ) ( fd, buf, buflen );
}

LIB_PRIVATE
int _real_socketpair ( int d, int type, int protocol, int sv[2] )
{
  REAL_FUNC_PASSTHROUGH ( socketpair ) ( d,type,protocol,sv );
}

LIB_PRIVATE
void _real_openlog ( const char *ident, int option, int facility )
{
  REAL_FUNC_PASSTHROUGH_VOID ( openlog ) ( ident,option,facility );
}

LIB_PRIVATE
void _real_closelog ( void )
{
  REAL_FUNC_PASSTHROUGH_VOID ( closelog ) ();
}

//set the handler
LIB_PRIVATE
sighandler_t _real_signal(int signum, sighandler_t handler){
  REAL_FUNC_PASSTHROUGH_TYPED ( sighandler_t, signal ) (signum, handler);
}
LIB_PRIVATE
int _real_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact){
  REAL_FUNC_PASSTHROUGH ( sigaction ) ( signum, act, oldact );
}
LIB_PRIVATE
int _real_sigvec(int signum, const struct sigvec *vec, struct sigvec *ovec){
  REAL_FUNC_PASSTHROUGH ( sigvec ) ( signum, vec, ovec );
}

//set the mask
LIB_PRIVATE
int _real_sigblock(int mask){
  REAL_FUNC_PASSTHROUGH ( sigblock ) ( mask );
}
LIB_PRIVATE
int _real_sigsetmask(int mask){
  REAL_FUNC_PASSTHROUGH ( sigsetmask ) ( mask );
}
LIB_PRIVATE
int _real_siggetmask(void){
  REAL_FUNC_PASSTHROUGH ( siggetmask )( );
}
LIB_PRIVATE
int _real_sigprocmask(int how, const sigset_t *a, sigset_t *b){
  REAL_FUNC_PASSTHROUGH ( sigprocmask ) ( how, a, b);
}
LIB_PRIVATE
int _real_pthread_sigmask(int how, const sigset_t *a, sigset_t *b){
  REAL_FUNC_PASSTHROUGH_TYPED ( int, pthread_sigmask ) ( how, a, b);
}

LIB_PRIVATE
int _real_sigsuspend(const sigset_t *mask){
  REAL_FUNC_PASSTHROUGH ( sigsuspend ) ( mask );
}
LIB_PRIVATE
sighandler_t _real_sigset(int sig, sighandler_t disp)
{
  REAL_FUNC_PASSTHROUGH_TYPED ( sighandler_t, sigset ) ( sig, disp );
}
LIB_PRIVATE
int _real_sighold(int sig){
  REAL_FUNC_PASSTHROUGH ( sighold ) ( sig );
}
LIB_PRIVATE
int _real_sigignore(int sig){
  REAL_FUNC_PASSTHROUGH ( sigignore ) ( sig );
}
// See 'man sigpause':  signal.h defines two possible versions for sigpause.
LIB_PRIVATE
int _real__sigpause(int __sig_or_mask, int __is_sig){
  REAL_FUNC_PASSTHROUGH ( __sigpause ) ( __sig_or_mask, __is_sig );
}
LIB_PRIVATE
int _real_sigpause(int sig){
  REAL_FUNC_PASSTHROUGH ( sigpause ) ( sig );
}
LIB_PRIVATE
int _real_sigrelse(int sig){
  REAL_FUNC_PASSTHROUGH ( sigrelse ) ( sig );
}

LIB_PRIVATE
int _real_sigwait(const sigset_t *set, int *sig) {
  REAL_FUNC_PASSTHROUGH ( sigwait ) ( set, sig);
}
LIB_PRIVATE
int _real_sigwaitinfo(const sigset_t *set, siginfo_t *info) {
  REAL_FUNC_PASSTHROUGH ( sigwaitinfo ) ( set, info);
}
LIB_PRIVATE
int _real_sigtimedwait(const sigset_t *set, siginfo_t *info,
                       const struct timespec *timeout) {
  REAL_FUNC_PASSTHROUGH ( sigtimedwait ) ( set, info, timeout);
}

#ifdef PID_VIRTUALIZATION
LIB_PRIVATE
pid_t _real_getpid(void){
  // libc caches pid of the process and hence after restart, libc:getpid()
  // returns the pre-ckpt value.
  return (pid_t) _real_syscall(SYS_getpid);
}

LIB_PRIVATE
pid_t _real_getppid(void){
  // libc caches ppid of the process and hence after restart, libc:getppid()
  // returns the pre-ckpt value.
  return (pid_t) _real_syscall(SYS_getppid);
}

LIB_PRIVATE
int _real_tcsetpgrp(int fd, pid_t pgrp){
  REAL_FUNC_PASSTHROUGH ( tcsetpgrp ) ( fd, pgrp );
}

LIB_PRIVATE
int _real_tcgetpgrp(int fd) {
  REAL_FUNC_PASSTHROUGH ( tcgetpgrp ) ( fd );
}

LIB_PRIVATE
pid_t _real_getpgrp(void) {
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, getpgrp ) ( );
}

LIB_PRIVATE
pid_t _real_setpgrp(void) {
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, setpgrp ) ( );
}

LIB_PRIVATE
pid_t _real_getpgid(pid_t pid) {
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, getpgid ) ( pid );
}

LIB_PRIVATE
int   _real_setpgid(pid_t pid, pid_t pgid) {
  REAL_FUNC_PASSTHROUGH ( setpgid ) ( pid, pgid );
}

LIB_PRIVATE
pid_t _real_getsid(pid_t pid) {
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, getsid ) ( pid );
}

LIB_PRIVATE
pid_t _real_setsid(void) {
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, setsid ) ( );
}

LIB_PRIVATE
int   _real_kill(pid_t pid, int sig) {
  REAL_FUNC_PASSTHROUGH ( kill ) ( pid, sig );
}

LIB_PRIVATE
pid_t _real_wait(__WAIT_STATUS stat_loc) {
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, wait ) ( stat_loc );
}

LIB_PRIVATE
pid_t _real_waitpid(pid_t pid, int *stat_loc, int options) {
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, waitpid ) ( pid, stat_loc, options );
}

LIB_PRIVATE
int   _real_waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options) {
  REAL_FUNC_PASSTHROUGH ( waitid ) ( idtype, id, infop, options );
}

LIB_PRIVATE
pid_t _real_wait3(__WAIT_STATUS status, int options, struct rusage *rusage) {
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, wait3 ) ( status, options, rusage );
}

LIB_PRIVATE
pid_t _real_wait4(pid_t pid, __WAIT_STATUS status, int options, struct rusage *rusage) {
  REAL_FUNC_PASSTHROUGH_TYPED ( pid_t, wait4 ) ( pid, status, options, rusage );
}

LIB_PRIVATE
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

LIB_PRIVATE
int _real_setgid(gid_t gid) {
  REAL_FUNC_PASSTHROUGH( setgid ) (gid);
}

LIB_PRIVATE
int _real_setuid(uid_t uid) {
  REAL_FUNC_PASSTHROUGH( setuid ) (uid);
}

#endif

// gettid / tkill / tgkill are not defined in libc.
// So, this is needed even if there is no PID_VIRTUALIZATION
LIB_PRIVATE
pid_t _real_gettid(void){
  // No glibc wrapper for gettid, although even if it had one, we would have
  // the issues similar to getpid/getppid().
  return (pid_t) _real_syscall(SYS_gettid);
}

LIB_PRIVATE
int   _real_tkill(int tid, int sig) {
  // No glibc wrapper for tkill, although even if it had one, we would have
  // the issues similar to getpid/getppid().
  return (int) _real_syscall(SYS_tkill, tid, sig);
}

LIB_PRIVATE
int   _real_tgkill(int tgid, int tid, int sig) {
  // No glibc wrapper for tgkill, although even if it had one, we would have
  // the issues similar to getpid/getppid().
  return (int) _real_syscall(SYS_tgkill, tgid, tid, sig);
}

LIB_PRIVATE
int _real_open( const char *pathname, int flags, mode_t mode ) {
  REAL_FUNC_PASSTHROUGH ( open ) ( pathname, flags, mode );
}

LIB_PRIVATE
int _real_open64( const char *pathname, int flags, mode_t mode ) {
  REAL_FUNC_PASSTHROUGH ( open ) ( pathname, flags, mode );
}

LIB_PRIVATE
FILE * _real_fopen( const char *path, const char *mode ) {
  REAL_FUNC_PASSTHROUGH_TYPED ( FILE *, fopen ) ( path, mode );
}

LIB_PRIVATE
FILE * _real_fopen64( const char *path, const char *mode ) {
  REAL_FUNC_PASSTHROUGH_TYPED ( FILE *, fopen64 ) ( path, mode );
}

/* See comments for syscall wrapper */
LIB_PRIVATE
long int _real_syscall(long int sys_num, ... ) {
  int i;
  void * arg[7];
  va_list ap;

  va_start(ap, sys_num);
  for (i = 0; i < 7; i++)
    arg[i] = va_arg(ap, void *);
  va_end(ap);

  // /usr/include/unistd.h says syscall returns long int (contrary to man page)
  REAL_FUNC_PASSTHROUGH_TYPED ( long int, syscall ) ( sys_num, arg[0], arg[1],
                                                      arg[2], arg[3], arg[4],
                                                      arg[5], arg[6] );
}

LIB_PRIVATE
int _real_xstat(int vers, const char *path, struct stat *buf) {
  REAL_FUNC_PASSTHROUGH ( __xstat ) ( vers, path, buf );
}

LIB_PRIVATE
int _real_xstat64(int vers, const char *path, struct stat64 *buf) {
  REAL_FUNC_PASSTHROUGH ( __xstat64 ) ( vers, path, buf );
}

LIB_PRIVATE
int _real_lxstat(int vers, const char *path, struct stat *buf) {
  REAL_FUNC_PASSTHROUGH ( __lxstat ) ( vers, path, buf );
}

LIB_PRIVATE
int _real_lxstat64(int vers, const char *path, struct stat64 *buf) {
  REAL_FUNC_PASSTHROUGH ( __lxstat64 ) ( vers, path, buf );
}

LIB_PRIVATE
ssize_t _real_readlink(const char *path, char *buf, size_t bufsiz) {
  REAL_FUNC_PASSTHROUGH_TYPED ( ssize_t, readlink ) ( path, buf, bufsiz );
}

LIB_PRIVATE
int _real_clone ( int ( *function ) (void *), void *child_stack, int flags, void *arg, int *parent_tidptr, struct user_desc *newtls, int *child_tidptr )
{
  REAL_FUNC_PASSTHROUGH ( __clone ) ( function, child_stack, flags, arg,
                                      parent_tidptr, newtls, child_tidptr );
}

LIB_PRIVATE
int _real_pthread_join(pthread_t thread, void **value_ptr) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, pthread_join ) ( thread, value_ptr );
}


LIB_PRIVATE
int _real_shmget (key_t key, size_t size, int shmflg) {
  REAL_FUNC_PASSTHROUGH ( shmget ) (key, size, shmflg);
}

LIB_PRIVATE
void* _real_shmat (int shmid, const void *shmaddr, int shmflg) {
  REAL_FUNC_PASSTHROUGH_TYPED ( void*, shmat ) (shmid, shmaddr, shmflg);
}

LIB_PRIVATE
int _real_shmdt (const void *shmaddr) {
  REAL_FUNC_PASSTHROUGH ( shmdt ) (shmaddr);
}

LIB_PRIVATE
int _real_shmctl (int shmid, int cmd, struct shmid_ds *buf) {
  REAL_FUNC_PASSTHROUGH ( shmctl ) (shmid, cmd, buf);
}

LIB_PRIVATE
void * _real_calloc(size_t nmemb, size_t size) {
  REAL_FUNC_PASSTHROUGH_TYPED(void*, calloc) (nmemb, size);
}

LIB_PRIVATE
void * _real_malloc(size_t size) {
  REAL_FUNC_PASSTHROUGH_TYPED (void*, malloc) (size);
}

LIB_PRIVATE
void * _real_realloc(void *ptr, size_t size) {
  REAL_FUNC_PASSTHROUGH_TYPED (void*, realloc) (ptr, size);
}

LIB_PRIVATE
void * _real_libc_memalign(size_t boundary, size_t size) {
  REAL_FUNC_PASSTHROUGH_TYPED (void*, __libc_memalign) (boundary, size);
}

LIB_PRIVATE
void _real_free(void *ptr) {
  REAL_FUNC_PASSTHROUGH_VOID (free) (ptr);
}

LIB_PRIVATE
void *_real_mmap(void *addr, size_t length, int prot, int flags,
    int fd, off_t offset) {
  REAL_FUNC_PASSTHROUGH_TYPED (void*, mmap) (addr,length,prot,flags,fd,offset);
}

LIB_PRIVATE
void *_real_mmap64(void *addr, size_t length, int prot, int flags,
    int fd, off64_t offset) {
  REAL_FUNC_PASSTHROUGH_TYPED (void*,mmap64) (addr,length,prot,flags,fd,offset);
}

LIB_PRIVATE
void *_real_mremap(void *old_address, size_t old_size, size_t new_size,
    int flags, void *new_address) {
  REAL_FUNC_PASSTHROUGH_TYPED (void*, mremap)
    (old_address, old_size, new_size, flags, new_address);
}

LIB_PRIVATE
int _real_munmap(void *addr, size_t length) {
  REAL_FUNC_PASSTHROUGH_TYPED (int, munmap) (addr, length);
}

#ifdef PTRACE
LIB_PRIVATE
long _real_ptrace(enum __ptrace_request request, pid_t pid, void *addr,
                  void *data) {
  REAL_FUNC_PASSTHROUGH_TYPED ( long, ptrace ) ( request, pid, addr, data );
}
#endif

#ifdef RECORD_REPLAY
LIB_PRIVATE
int _real_getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, getsockname ) ( sockfd, addr, addrlen );
}

LIB_PRIVATE
int _real_getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, getpeername ) ( sockfd, addr, addrlen );
}

LIB_PRIVATE
int _real_fxstat(int vers, int fd, struct stat *buf) {
  REAL_FUNC_PASSTHROUGH ( __fxstat ) ( vers, fd, buf );
}

LIB_PRIVATE
int _real_fxstat64(int vers, int fd, struct stat64 *buf) {
  REAL_FUNC_PASSTHROUGH ( __fxstat64 ) ( vers, fd, buf );
}

LIB_PRIVATE
int _real_closedir(DIR *dirp) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, closedir ) ( dirp );
}

LIB_PRIVATE
DIR * _real_opendir(const char *name) {
  REAL_FUNC_PASSTHROUGH_TYPED ( DIR *, opendir ) ( name );
}

LIB_PRIVATE
int _real_mkdir(const char *pathname, mode_t mode) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, mkdir ) ( pathname, mode );
}

LIB_PRIVATE
int _real_mkstemp(char *temp) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, mkstemp ) ( temp );
}

LIB_PRIVATE
FILE * _real_fdopen(int fd, const char *mode) {
  REAL_FUNC_PASSTHROUGH_TYPED ( FILE *, fdopen ) ( fd, mode );
}

LIB_PRIVATE
char * _real_fgets(char *s, int size, FILE *stream) {
  REAL_FUNC_PASSTHROUGH_TYPED ( char *, fgets ) ( s, size, stream );
}

LIB_PRIVATE
int _real_fflush(FILE *stream) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, fflush ) ( stream );
}

LIB_PRIVATE
int _real_fdatasync(int fd) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, fdatasync ) ( fd );
}

LIB_PRIVATE
int _real_fsync(int fd) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, fsync ) ( fd );
}

LIB_PRIVATE
int _real_getc(FILE *stream) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, getc ) ( stream );
}

LIB_PRIVATE
int _real_gettimeofday(struct timeval *tv, struct timezone *tz) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, gettimeofday ) ( tv, tz );
}

LIB_PRIVATE
int _real_fgetc(FILE *stream) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, fgetc ) ( stream );
}

LIB_PRIVATE
int _real_ungetc(int c, FILE *stream) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, ungetc ) ( c, stream );
}

LIB_PRIVATE
ssize_t _real_getline(char **lineptr, size_t *n, FILE *stream) {
  REAL_FUNC_PASSTHROUGH_TYPED ( ssize_t, getline ) ( lineptr, n, stream );
}

LIB_PRIVATE
int _real_link(const char *oldpath, const char *newpath) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, link ) ( oldpath, newpath );
}

LIB_PRIVATE
int _real_rename(const char *oldpath, const char *newpath) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, rename ) ( oldpath, newpath );
}

LIB_PRIVATE
void _real_rewind(FILE *stream) {
  REAL_FUNC_PASSTHROUGH_VOID ( rewind ) ( stream );
}

LIB_PRIVATE
int _real_rmdir(const char *pathname) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, rmdir ) ( pathname );
}

LIB_PRIVATE
long _real_ftell(FILE *stream) {
  REAL_FUNC_PASSTHROUGH_TYPED ( long, ftell ) ( stream );
}

LIB_PRIVATE
int _real_fputs(const char *s, FILE *stream) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, fputs ) ( s, stream );
}

LIB_PRIVATE
int _real_putc(int c, FILE *stream) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, putc ) ( c, stream );
}

LIB_PRIVATE
size_t _real_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
  REAL_FUNC_PASSTHROUGH_TYPED ( size_t, fwrite) ( ptr, size, nmemb, stream );
}

LIB_PRIVATE
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

LIB_PRIVATE
int _real_pthread_cond_signal(pthread_cond_t *cond) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int,pthread_cond_signal ) ( cond );
}

LIB_PRIVATE
int _real_pthread_cond_broadcast(pthread_cond_t *cond) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int,pthread_cond_broadcast ) ( cond );
}

LIB_PRIVATE
int _real_pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int,pthread_cond_wait ) ( cond,mutex );
}

LIB_PRIVATE
int _real_pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
    const struct timespec *abstime) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int,pthread_cond_timedwait ) ( cond,mutex,abstime );
}

LIB_PRIVATE
int _real_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
    void *(*start_routine)(void*), void *arg) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int,pthread_create )
    (thread,attr,start_routine,arg);
}

LIB_PRIVATE
void _real_pthread_exit(void *value_ptr) {
  REAL_FUNC_PASSTHROUGH_VOID ( pthread_exit ) ( value_ptr );
}

LIB_PRIVATE
int _real_pthread_detach(pthread_t thread)
{
  REAL_FUNC_PASSTHROUGH_TYPED ( int,pthread_detach ) ( thread );
}

LIB_PRIVATE
int _real_pthread_kill(pthread_t thread, int sig)
{
  REAL_FUNC_PASSTHROUGH_TYPED ( int,pthread_kill )
    ( thread, sig );
}

LIB_PRIVATE
int _real_access(const char *pathname, int mode)
{
  REAL_FUNC_PASSTHROUGH_TYPED ( int,access ) ( pathname,mode );
}

LIB_PRIVATE
struct dirent *_real_readdir(DIR *dirp) {
  REAL_FUNC_PASSTHROUGH_TYPED ( struct dirent *, readdir ) ( dirp );
}

LIB_PRIVATE
int _real_readdir_r(DIR *dirp, struct dirent *entry, struct dirent **result ) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int, readdir_r ) ( dirp, entry, result );
}

LIB_PRIVATE
int _real_rand(void) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int,rand ) ( );
}

LIB_PRIVATE
void _real_srand(unsigned int seed) {
  REAL_FUNC_PASSTHROUGH_VOID ( srand ) ( seed );
}

LIB_PRIVATE
time_t _real_time(time_t *tloc) {
  REAL_FUNC_PASSTHROUGH_TYPED ( time_t,time ) ( tloc );
}

LIB_PRIVATE
int _real_dup(int oldfd) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int,dup ) ( oldfd );
}

LIB_PRIVATE
off_t _real_lseek(int fd, off_t offset, int whence) {
  REAL_FUNC_PASSTHROUGH_TYPED ( off_t,lseek) ( fd, offset, whence );
}

LIB_PRIVATE
int _real_unlink(const char *pathname) {
  REAL_FUNC_PASSTHROUGH_TYPED ( int,unlink ) ( pathname );
}

LIB_PRIVATE
ssize_t _real_pread(int fd, void *buf, size_t count, off_t offset) {
  REAL_FUNC_PASSTHROUGH_TYPED ( ssize_t,pread ) ( fd, buf, count, offset );
}

LIB_PRIVATE
ssize_t _real_pwrite(int fd, const void *buf, size_t count, off_t offset) {
  REAL_FUNC_PASSTHROUGH_TYPED ( ssize_t,pwrite ) ( fd, buf, count, offset );
}
#endif // RECORD_REPLAY
