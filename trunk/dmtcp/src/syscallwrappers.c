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

//////////////////////////////////////
//////// Now we define our wrappers

#define PASSTHROUGH_DMTCP_HELPER(func, ...) {\
    int ret = _real_ ## func (__VA_ARGS__); \
    PASSTHROUGH_DMTCP_HELPER2(func,__VA_ARGS__); }
    
#define PASSTHROUGH_DMTCP_HELPER2(func, ...) {\
    _dmtcp_lock();\
    if(ret < 0) ret = dmtcp_on_error(ret, sockfd, #func); \
    else ret = dmtcp_on_ ## func (ret, __VA_ARGS__);\
    _dmtcp_unlock();\
    return ret;}

int socket(int domain, int type, int protocol)
{
    static int sockfd = -1;
    PASSTHROUGH_DMTCP_HELPER(socket, domain, type, protocol);
}

int connect(int sockfd,  const  struct sockaddr *serv_addr, socklen_t addrlen)
{
    int ret = _real_connect(sockfd,serv_addr,addrlen);
    
    //no blocking connect... need to hang around until it is writable
    if(ret < 0 && errno == EINPROGRESS)
    {
        fd_set wfds;
        struct timeval tv;
        int retval;
   
        FD_ZERO(&wfds);
        FD_SET(sockfd, &wfds);
    
        tv.tv_sec = 15;
        tv.tv_usec = 0;
    
        retval = select(sockfd+1, NULL, &wfds, NULL, &tv);
        /* Don't rely on the value of tv now! */
    
        if (retval == -1)
            perror("select()");
        else if (FD_ISSET(sockfd, &wfds))
        {
            int val = -1;
            socklen_t sz = sizeof(val);
            getsockopt(sockfd,SOL_SOCKET,SO_ERROR,&val,&sz);
            if(val==0) ret = 0;
        }
        else
            printf("No data within five seconds.\n");
    }
    
    PASSTHROUGH_DMTCP_HELPER2(connect,sockfd,serv_addr,addrlen);
}

int bind(int sockfd,  const struct  sockaddr  *my_addr,  socklen_t addrlen)
{
    PASSTHROUGH_DMTCP_HELPER(bind, sockfd, my_addr, addrlen);
}

int listen(int sockfd, int backlog)
{
    PASSTHROUGH_DMTCP_HELPER(listen, sockfd, backlog );
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    if(addr == NULL || addrlen == NULL)
    {
        struct sockaddr_storage tmp_addr;
        socklen_t tmp_len = 0;
        memset(&tmp_addr,0,sizeof(tmp_addr));
        PASSTHROUGH_DMTCP_HELPER(accept, sockfd, ((struct sockaddr *)&tmp_addr) , (&tmp_len));
    }
    else
        PASSTHROUGH_DMTCP_HELPER(accept, sockfd, addr, addrlen);
}

int setsockopt(int sockfd, int  level,  int  optname,  const  void  *optval,
       socklen_t optlen) 
{
    PASSTHROUGH_DMTCP_HELPER(setsockopt,sockfd,level,optname,optval,optlen);
}
