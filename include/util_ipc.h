/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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
#ifndef __DMTCP_UTIL_H__
#define __DMTCP_UTIL_H__

#include <sys/socket.h>
#include <sys/un.h>

namespace dmtcp
{
namespace Util
{
static inline int
sendFd(int restoreFd,
       int32_t fd,
       void *data,
       size_t len,
       struct sockaddr_un &addr,
       socklen_t addrLen)
{
  struct iovec iov;
  struct msghdr hdr;
  struct cmsghdr *cmsg;
  char cms[CMSG_SPACE(sizeof(int32_t))];

  iov.iov_base = data;
  iov.iov_len = len;

  memset(&hdr, 0, sizeof hdr);
  hdr.msg_name = &addr;
  hdr.msg_namelen = addrLen;
  hdr.msg_iov = &iov;
  hdr.msg_iovlen = 1;
  hdr.msg_control = (caddr_t)cms;
  hdr.msg_controllen = CMSG_LEN(sizeof(int32_t));

  cmsg = CMSG_FIRSTHDR(&hdr);
  cmsg->cmsg_len = CMSG_LEN(sizeof(fd));
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;

  memcpy(CMSG_DATA(cmsg), &fd, sizeof(fd));

  return sendmsg(restoreFd, &hdr, 0);
}

static inline int32_t
receiveFd(int restoreFd, void *data, size_t len)
{
  int32_t fd;
  struct iovec iov;
  struct msghdr hdr;
  struct cmsghdr *cmsg;
  char cms[CMSG_SPACE(sizeof(int32_t))];

  iov.iov_base = data;
  iov.iov_len = len;

  memset(&hdr, 0, sizeof hdr);
  hdr.msg_name = 0;
  hdr.msg_namelen = 0;
  hdr.msg_iov = &iov;
  hdr.msg_iovlen = 1;

  hdr.msg_control = (caddr_t)cms;
  hdr.msg_controllen = sizeof cms;

  if (recvmsg(restoreFd, &hdr, 0) == -1) {
    return -1;
  }

  cmsg = CMSG_FIRSTHDR(&hdr);
  if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
    return -1;
  }
  memcpy(&fd, CMSG_DATA(cmsg), sizeof(fd));

  return fd;
}
}
}
#endif // #ifndef __DMTCP_UTIL_H__
