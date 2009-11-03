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
#include <errno.h>

typedef int ( *funcptr ) ();
typedef pid_t ( *funcptr_pid_t ) ();
typedef funcptr ( *signal_funcptr ) ();

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

void _dmtcp_lock() { pthread_mutex_lock ( &theMutex ); }

void _dmtcp_unlock() { pthread_mutex_unlock ( &theMutex ); }
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

static funcptr get_libc_symbol ( const char* name )
{
  static void* handle = NULL;
  if ( handle==NULL && ( handle=dlopen ( LIBC_FILENAME,RTLD_NOW ) ) == NULL )
  {
    fprintf ( stderr, "dmtcp: get_libc_symbol: ERROR in dlopen: %s \n",
              dlerror() );
    abort();
  }

  void* tmp = dlsym ( handle, name );
  if ( tmp == NULL )
  {
    fprintf ( stderr, "dmtcp: get_libc_symbol: ERROR in dlsym: %s \n",
              dlerror() );
    abort();
  }
  return ( funcptr ) tmp;
}

//////////////////////////
//// FIRST DEFINE REAL VERSIONS OF NEEDED FUNCTIONS

#define REAL_FUNC_PASSTHROUGH(name) static funcptr fn = NULL; \
    if (fn==NULL) fn = get_libc_symbol(#name); \
    return (*fn)

#define REAL_FUNC_PASSTHROUGH_TYPED(type,name) static type (*fn) () = NULL; \
    if (fn==NULL) fn = (void *)get_libc_symbol(#name); \
    return (*fn)

#define REAL_FUNC_PASSTHROUGH_PID_T(name) static funcptr_pid_t fn = NULL; \
    if (fn==NULL) fn = (funcptr_pid_t)get_libc_symbol(#name); \
    return (*fn)

#define REAL_FUNC_PASSTHROUGH_VOID(name) static funcptr fn = NULL; \
    if (fn==NULL) fn = get_libc_symbol(#name); \
    (*fn)

/// call the libc version of this function via dlopen/dlsym
int _real_socket ( int domain, int type, int protocol )
{
  REAL_FUNC_PASSTHROUGH ( socket ) ( domain,type,protocol );
}

/// call the libc version of this function via dlopen/dlsym
int _real_connect ( int sockfd,  const  struct sockaddr *serv_addr, socklen_t addrlen )
{
  REAL_FUNC_PASSTHROUGH ( connect ) ( sockfd,serv_addr,addrlen );
}

/// call the libc version of this function via dlopen/dlsym
int _real_bind ( int sockfd,  const struct  sockaddr  *my_addr,  socklen_t addrlen )
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

/// call the libc version of this function via dlopen/dlsym
int _real_setsockopt ( int s, int  level,  int  optname,  const  void  *optval,
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
  REAL_FUNC_PASSTHROUGH_PID_T ( fork ) ();
}

int _real_close ( int fd )
{
  REAL_FUNC_PASSTHROUGH ( close ) ( fd );
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
    static signal_funcptr fn = NULL;
    if(fn==NULL) fn = (signal_funcptr)get_libc_symbol("signal");
    return (sighandler_t)(*fn)(signum, handler);
}
int _real_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact){
  REAL_FUNC_PASSTHROUGH ( sigaction ) ( signum, act, oldact );
}
int _real_rt_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact){
  REAL_FUNC_PASSTHROUGH ( rt_sigaction ) ( signum, act, oldact );
}
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
int _real_rt_sigprocmask(int how, const sigset_t *a, sigset_t *b){
  REAL_FUNC_PASSTHROUGH ( rt_sigprocmask ) ( how, a, b);
}
int _real_pthread_sigmask(int how, const sigset_t *a, sigset_t *b){
  //**** TODO Link with the "real" pthread_sigmask ******
  REAL_FUNC_PASSTHROUGH ( sigprocmask ) ( how, a, b);
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

pid_t _real_gettid(void){
  return (pid_t) _real_syscall(SYS_gettid);
//  REAL_FUNC_PASSTHROUGH_PID_T ( getpid ) ( );
}

pid_t _real_getppid(void){
  return (pid_t) _real_syscall(SYS_getppid);
  //REAL_FUNC_PASSTHROUGH_PID_T ( getppid ) ( );
}

int _real_tcsetpgrp(int fd, pid_t pgrp){
  REAL_FUNC_PASSTHROUGH ( tcsetpgrp ) ( fd, pgrp );
}

int _real_tcgetpgrp(int fd) {
  REAL_FUNC_PASSTHROUGH ( tcgetpgrp ) ( fd );
}

pid_t _real_getpgrp(void) {
  REAL_FUNC_PASSTHROUGH_PID_T ( getpgrp ) ( );
}

pid_t _real_setpgrp(void) {
  REAL_FUNC_PASSTHROUGH_PID_T ( setpgrp ) ( );
}

pid_t _real_getpgid(pid_t pid) {
  REAL_FUNC_PASSTHROUGH_PID_T ( getpgid ) ( pid );
}

int   _real_setpgid(pid_t pid, pid_t pgid) {
  REAL_FUNC_PASSTHROUGH ( setpgid ) ( pid, pgid );
}

pid_t _real_getsid(pid_t pid) {
  REAL_FUNC_PASSTHROUGH_PID_T ( getsid ) ( pid );
}

pid_t _real_setsid(void) {
  REAL_FUNC_PASSTHROUGH_PID_T ( setsid ) ( );
}

int   _real_kill(pid_t pid, int sig) {
  REAL_FUNC_PASSTHROUGH ( kill ) ( pid, sig );
}

int   _real_tkill(int tid, int sig) {
  return (int) _real_syscall(SYS_tkill, tid, sig);
  //REAL_FUNC_PASSTHROUGH ( tkill ) ( tid, sig );
}

int   _real_tgkill(int tgid, int tid, int sig) {
  return (int) _real_syscall(SYS_tgkill, tgid, tid, sig);
  //REAL_FUNC_PASSTHROUGH ( tgkill ) ( tgid, tid, sig );
}

pid_t _real_wait(__WAIT_STATUS stat_loc) {
  REAL_FUNC_PASSTHROUGH_PID_T ( wait ) ( stat_loc );
}

pid_t _real_waitpid(pid_t pid, int *stat_loc, int options) {
  REAL_FUNC_PASSTHROUGH_PID_T ( waitpid ) ( pid, stat_loc, options );
}

int   _real_waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options) {
  REAL_FUNC_PASSTHROUGH ( waitid ) ( idtype, id, infop, options );
}

pid_t _real_wait3(__WAIT_STATUS status, int options, struct rusage *rusage) {
  REAL_FUNC_PASSTHROUGH_PID_T ( wait3 ) ( status, options, rusage );
}

pid_t _real_wait4(pid_t pid, __WAIT_STATUS status, int options, struct rusage *rusage) {
  REAL_FUNC_PASSTHROUGH_PID_T ( wait4 ) ( pid, status, options, rusage );
}

int _real_open ( const char *pathname, int flags, mode_t mode ) {
  REAL_FUNC_PASSTHROUGH ( open ) ( pathname, flags, mode );
}

FILE * _real_fopen( const char *path, const char *mode ) {
  REAL_FUNC_PASSTHROUGH_TYPED ( FILE *, fopen ) ( path, mode );
}

#endif

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

int _real_clone ( int ( *function ) (void *), void *child_stack, int flags, void *arg, int *parent_tidptr, struct user_desc *newtls, int *child_tidptr )
{ 
  REAL_FUNC_PASSTHROUGH ( __clone ) ( function, child_stack, flags, arg, parent_tidptr, newtls, child_tidptr );
}

