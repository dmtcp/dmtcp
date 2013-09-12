
#include "util_ipc.h"

extern "C" LIB_PRIVATE
int sendFd(int restoreFd, int fd, void *data, size_t len,
            struct sockaddr_un& addr, socklen_t addrLen)
{
  struct iovec iov;
  struct msghdr hdr;
  struct cmsghdr *cmsg;
  char cms[CMSG_SPACE(sizeof(int))];

  iov.iov_base = data;
  iov.iov_len = len;

  memset(&hdr, 0, sizeof hdr);
  hdr.msg_name = &addr;
  hdr.msg_namelen = addrLen;
  hdr.msg_iov = &iov;
  hdr.msg_iovlen = 1;
  hdr.msg_control = (caddr_t)cms;
  hdr.msg_controllen = CMSG_LEN(sizeof(int));

  cmsg = CMSG_FIRSTHDR(&hdr);
  cmsg->cmsg_len = CMSG_LEN(sizeof(int));
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  *(int*)CMSG_DATA(cmsg) = fd;

  return sendmsg(restoreFd, &hdr, 0);
}

extern "C" LIB_PRIVATE
int receiveFd(int restoreFd, void *data, size_t len)
{
  int fd;
  struct iovec iov;
  struct msghdr hdr;
  struct cmsghdr *cmsg;
  char cms[CMSG_SPACE(sizeof(int))];

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
  if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type  != SCM_RIGHTS) {
    return -1;
  }
  fd = *(int *) CMSG_DATA(cmsg);

  return fd;
}
