#include <sys/socket.h>
#include <sys/un.h>

// This is a copy of the code from src/plugin/ipc/utils_ipc.cpp and SSH plugin
// This code also must be shared between ssh ans rm plugins.

int
slurm_sendFd(int restoreFd,
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
  cmsg->cmsg_len = CMSG_LEN(sizeof(int32_t));
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  memcpy(CMSG_DATA(cmsg), &fd, sizeof(fd));

  return sendmsg(restoreFd, &hdr, 0);
}

int32_t
slurm_receiveFd(int restoreFd, void *data, size_t len)
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
