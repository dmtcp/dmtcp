/***************************************************************************
 *   Copyright (C) 2006 by Jason Ansel                                     *
 *   jansel@ccs.neu.edu                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include "syscallwrappers.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "constants.h"
#include "sockettable.h"
#include <pthread.h>
#include <sys/select.h>

/* According to earlier standards */
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>


typedef int (*funcptr)();

static pthread_mutex_t theMutex = PTHREAD_MUTEX_INITIALIZER;


static funcptr get_libc_symbol(const char* name)
{
    static void* handle = NULL;
    if(handle==NULL && (handle=dlopen(LIBC_FILENAME,RTLD_NOW)) == NULL)
    {
        fprintf(stderr,"dmtcp: get_libc_symbol: ERROR in dlopen: %s \n",dlerror());
        abort();
    }
    
    void* tmp = dlsym(handle, name);
    if(tmp==NULL)
    {
        fprintf(stderr,"dmtcp: get_libc_symbol: ERROR in dlsym: %s \n",dlerror());
        abort();
    }
    return (funcptr)tmp;
}

//////////////////////////
//// FIRST DEFINE REAL VERSIONS OF NEEDED FUNCTIONS

#define REAL_FUNC_PASSTHROUGH(name) static funcptr fn = NULL;\
    if(fn==NULL) fn = get_libc_symbol(#name); \
    return (*fn)

#define REAL_FUNC_PASSTHROUGH_VOID(name) static funcptr fn = NULL;\
    if(fn==NULL) fn = get_libc_symbol(#name); \
    (*fn)

/// call the libc version of this function via dlopen/dlsym
int _real_socket(int domain, int type, int protocol)
{
    REAL_FUNC_PASSTHROUGH(socket)(domain,type,protocol);
}

/// call the libc version of this function via dlopen/dlsym
int _real_connect(int sockfd,  const  struct sockaddr *serv_addr, socklen_t addrlen)
{
    REAL_FUNC_PASSTHROUGH(connect)(sockfd,serv_addr,addrlen);
}

/// call the libc version of this function via dlopen/dlsym
int _real_bind(int sockfd,  const struct  sockaddr  *my_addr,  socklen_t addrlen)
{
    REAL_FUNC_PASSTHROUGH(bind)(sockfd,my_addr,addrlen);
}

/// call the libc version of this function via dlopen/dlsym
int _real_listen(int sockfd, int backlog)
{
    REAL_FUNC_PASSTHROUGH(listen)(sockfd,backlog);
}

/// call the libc version of this function via dlopen/dlsym
int _real_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    REAL_FUNC_PASSTHROUGH(accept)(sockfd,addr,addrlen);
}

/// call the libc version of this function via dlopen/dlsym
int _real_setsockopt(int s, int  level,  int  optname,  const  void  *optval,
       socklen_t optlen)
{
    REAL_FUNC_PASSTHROUGH(setsockopt)(s,level,optname,optval,optlen);
}

int _real_fexecve(int fd, char *const argv[], char *const envp[])
{
    REAL_FUNC_PASSTHROUGH(fexecve)(fd,argv,envp);
}

int _real_execve(const char *filename, char *const argv[],
                char *const envp[])
{
    REAL_FUNC_PASSTHROUGH(execve)(filename,argv,envp);
}

int _real_execv(const char *path, char *const argv[])
{
    REAL_FUNC_PASSTHROUGH(execv)(path,argv);
}

int _real_execvp(const char *file, char *const argv[])
{
    REAL_FUNC_PASSTHROUGH(execvp)(file,argv);
}

int _real_system(const char *cmd)
{
    REAL_FUNC_PASSTHROUGH(system)(cmd);
}

pid_t _real_fork()
{
    REAL_FUNC_PASSTHROUGH(fork)();
}

int _real_close(int fd)
{
    REAL_FUNC_PASSTHROUGH(close)(fd);
}

int _real_dup(int oldfd)
{
    REAL_FUNC_PASSTHROUGH(dup)(oldfd);
}
int _real_dup2(int oldfd, int newfd)
{
    REAL_FUNC_PASSTHROUGH(dup2)(oldfd,newfd);
}

char *_real_ptsname(int fd)
{
    REAL_FUNC_PASSTHROUGH(ptsname)(fd);
}

int _real_ptsname_r(int fd, char * buf, size_t buflen)
{
    REAL_FUNC_PASSTHROUGH(ptsname_r)(fd, buf, buflen);
}

int _real_socketpair(int d, int type, int protocol, int sv[2])
{
    REAL_FUNC_PASSTHROUGH(socketpair)(d,type,protocol,sv);
}

void _real_openlog(const char *ident, int option, int facility)
{
    REAL_FUNC_PASSTHROUGH_VOID(openlog)(ident,option,facility);
}

void _real_closelog(void)
{
    REAL_FUNC_PASSTHROUGH_VOID(closelog)();
}

void _dmtcp_lock(){pthread_mutex_lock(&theMutex);}
void _dmtcp_unlock(){pthread_mutex_unlock(&theMutex);}
void _dmtcp_remutex_on_fork(){pthread_mutex_init(&theMutex, NULL);}

