#include "socketdiscovery.h"

#include "jfilesystem.h"
#include "plugin/ssh/ssh.h"
#include "protectedfds.h"
#include "syscallwrappers.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <linux/inet_diag.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/sock_diag.h>
#include <linux/unix_diag.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <unistd.h>

namespace dmtcp {
namespace {

bool
getIntSocketOption(int fd, int option, int *value)
{
  socklen_t length = sizeof(*value);
  return _real_getsockopt(fd, SOL_SOCKET, option, value, &length) == 0 &&
         length == sizeof(*value);
}

int
openDiagnosticSocket()
{
  return _real_socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC,
                      NETLINK_SOCK_DIAG);
}

void
closeDiagnosticSocket(int fd)
{
  _real_close(fd);
}

bool
receiveDiagnosticMessage(int fd, vector<char> *message)
{
  ssize_t bytes = recv(fd, nullptr, 0, MSG_PEEK | MSG_TRUNC);
  if (bytes <= 0) {
    return false;
  }

  message->resize(bytes);
  return recv(fd, message->data(), message->size(), 0) == bytes;
}

bool
queryInetDiagnostics(const DiscoveredSocket& discovered,
                     InspectedSocket *inspected)
{
  if (discovered.inode > std::numeric_limits<uint32_t>::max()) {
    return false;
  }

  int fd = openDiagnosticSocket();
  if (fd == -1) {
    return false;
  }

  struct {
    nlmsghdr header;
    inet_diag_req_v2 request;
  } message = {};
  message.header.nlmsg_len = NLMSG_LENGTH(sizeof(message.request));
  message.header.nlmsg_type = SOCK_DIAG_BY_FAMILY;
  message.header.nlmsg_flags = NLM_F_REQUEST;
  message.header.nlmsg_seq = 1;
  message.request.sdiag_family = discovered.domain;
  message.request.sdiag_protocol = IPPROTO_TCP;
  message.request.idiag_states = UINT32_MAX;
  message.request.id.idiag_cookie[0] = INET_DIAG_NOCOOKIE;
  message.request.id.idiag_cookie[1] = INET_DIAG_NOCOOKIE;

  if (discovered.domain == AF_INET) {
    const sockaddr_in *local =
      reinterpret_cast<const sockaddr_in *>(&inspected->local);
    message.request.id.idiag_sport = local->sin_port;
    message.request.id.idiag_src[0] = local->sin_addr.s_addr;
    if (inspected->hasPeer) {
      const sockaddr_in *peer =
        reinterpret_cast<const sockaddr_in *>(&inspected->peer);
      message.request.id.idiag_dport = peer->sin_port;
      message.request.id.idiag_dst[0] = peer->sin_addr.s_addr;
    }
  } else {
    const sockaddr_in6 *local =
      reinterpret_cast<const sockaddr_in6 *>(&inspected->local);
    message.request.id.idiag_sport = local->sin6_port;
    memcpy(message.request.id.idiag_src, &local->sin6_addr,
           sizeof(local->sin6_addr));
    message.request.id.idiag_if = local->sin6_scope_id;
    if (inspected->hasPeer) {
      const sockaddr_in6 *peer =
        reinterpret_cast<const sockaddr_in6 *>(&inspected->peer);
      message.request.id.idiag_dport = peer->sin6_port;
      memcpy(message.request.id.idiag_dst, &peer->sin6_addr,
             sizeof(peer->sin6_addr));
    }
  }

  sockaddr_nl kernel = {};
  kernel.nl_family = AF_NETLINK;
  bool found = false;
  bool valid = sendto(fd, &message, message.header.nlmsg_len, 0,
                      reinterpret_cast<sockaddr *>(&kernel),
                      sizeof(kernel)) ==
               static_cast<ssize_t>(message.header.nlmsg_len);

  vector<char> response;
  if (valid && receiveDiagnosticMessage(fd, &response)) {
    int remaining = response.size();
    for (nlmsghdr *header =
           reinterpret_cast<nlmsghdr *>(response.data());
         NLMSG_OK(header, remaining);
         header = NLMSG_NEXT(header, remaining)) {
      if (header->nlmsg_type == NLMSG_DONE) {
        break;
      }
      if (header->nlmsg_type == NLMSG_ERROR ||
          header->nlmsg_type == NLMSG_OVERRUN ||
          header->nlmsg_len < NLMSG_LENGTH(sizeof(inet_diag_msg))) {
        break;
      }

      const inet_diag_msg *diag =
        static_cast<const inet_diag_msg *>(NLMSG_DATA(header));
      if (diag->idiag_inode == discovered.inode) {
        inspected->kernelState = diag->idiag_state;
        if (inspected->acceptConn != 0) {
          inspected->listenBacklog = diag->idiag_wqueue;
        }
        inspected->hasDiagnostics = true;
        found = true;
        break;
      }
    }
  }

  closeDiagnosticSocket(fd);
  return found;
}

bool
queryUnixDiagnostics(const DiscoveredSocket& discovered,
                     InspectedSocket *inspected)
{
  if (discovered.inode > std::numeric_limits<uint32_t>::max()) {
    return false;
  }

  int fd = openDiagnosticSocket();
  if (fd == -1) {
    return false;
  }

  struct {
    nlmsghdr header;
    unix_diag_req request;
  } message = {};
  message.header.nlmsg_len = NLMSG_LENGTH(sizeof(message.request));
  message.header.nlmsg_type = SOCK_DIAG_BY_FAMILY;
  message.header.nlmsg_flags = NLM_F_REQUEST;
  message.header.nlmsg_seq = 1;
  message.request.sdiag_family = AF_UNIX;
  message.request.udiag_states = UINT32_MAX;
  message.request.udiag_ino = discovered.inode;
  message.request.udiag_show = UDIAG_SHOW_PEER | UDIAG_SHOW_RQLEN;
  message.request.udiag_cookie[0] = INET_DIAG_NOCOOKIE;
  message.request.udiag_cookie[1] = INET_DIAG_NOCOOKIE;

  sockaddr_nl kernel = {};
  kernel.nl_family = AF_NETLINK;
  bool found = false;
  bool valid = sendto(fd, &message, message.header.nlmsg_len, 0,
                      reinterpret_cast<sockaddr *>(&kernel),
                      sizeof(kernel)) ==
               static_cast<ssize_t>(message.header.nlmsg_len);

  vector<char> response;
  if (valid && receiveDiagnosticMessage(fd, &response)) {
    int remaining = response.size();
    for (nlmsghdr *header =
           reinterpret_cast<nlmsghdr *>(response.data());
         NLMSG_OK(header, remaining);
         header = NLMSG_NEXT(header, remaining)) {
      if (header->nlmsg_type == NLMSG_DONE) {
        break;
      }
      if (header->nlmsg_type == NLMSG_ERROR ||
          header->nlmsg_type == NLMSG_OVERRUN ||
          header->nlmsg_len < NLMSG_LENGTH(sizeof(unix_diag_msg))) {
        break;
      }

      const unix_diag_msg *diag =
        static_cast<const unix_diag_msg *>(NLMSG_DATA(header));
      if (diag->udiag_ino != discovered.inode) {
        continue;
      }

      inspected->kernelState = diag->udiag_state;
      int attributeBytes = NLMSG_PAYLOAD(header, sizeof(*diag));
      for (rtattr *attribute =
             reinterpret_cast<rtattr *>(const_cast<unix_diag_msg *>(diag) + 1);
           RTA_OK(attribute, attributeBytes);
           attribute = RTA_NEXT(attribute, attributeBytes)) {
        if (attribute->rta_type == UNIX_DIAG_PEER &&
            RTA_PAYLOAD(attribute) >= sizeof(uint32_t)) {
          inspected->peerInode =
            *static_cast<uint32_t *>(RTA_DATA(attribute));
        } else if (attribute->rta_type == UNIX_DIAG_RQLEN &&
                   RTA_PAYLOAD(attribute) >= sizeof(unix_diag_rqlen)) {
          const unix_diag_rqlen *queue =
            static_cast<const unix_diag_rqlen *>(RTA_DATA(attribute));
          if (inspected->acceptConn != 0) {
            inspected->listenBacklog = queue->udiag_wqueue;
          }
        }
      }
      inspected->hasDiagnostics = true;
      found = true;
      break;
    }
  }

  closeDiagnosticSocket(fd);
  return found;
}

} // namespace

int
socketConnectWaitMs()
{
  constexpr int defaultWaitMs = 100;
  const char *value = getenv("DMTCP_SOCKET_CONNECT_WAIT_MS");
  if (value == nullptr) {
    return defaultWaitMs;
  }

  std::string_view text(value);
  int waitMs = 0;
  auto result =
    std::from_chars(text.data(), text.data() + text.size(), waitMs);
  return result.ec == std::errc() &&
         result.ptr == text.data() + text.size() &&
         waitMs >= 0 ? waitMs : defaultWaitMs;
}

vector<DiscoveredSocket>
enumerateSockets()
{
  vector<DiscoveredSocket> sockets;
  map<uint64_t, size_t> socketByInode;

  for (int fd : jalib::Filesystem::ListOpenFds()) {
    if (DMTCP_IS_PROTECTED_FD(fd) ||
        (dmtcp_ssh_owns_fd && dmtcp_ssh_owns_fd(fd))) {
      continue;
    }

    struct stat statBuf = {};
    if (fstat(fd, &statBuf) == -1 || !S_ISSOCK(statBuf.st_mode)) {
      continue;
    }

    int fdFlags = _real_fcntl(fd, F_GETFD);
    if (fdFlags == -1) {
      continue;
    }

    uint64_t inode = statBuf.st_ino;
    auto existing = socketByInode.find(inode);
    if (existing != socketByInode.end()) {
      DiscoveredSocket& socket = sockets[existing->second];
      socket.fds.push_back(fd);
      socket.fdFlags[fd] = fdFlags;
      continue;
    }

    DiscoveredSocket socket;
    getIntSocketOption(fd, SO_DOMAIN, &socket.domain);
    getIntSocketOption(fd, SO_TYPE, &socket.type);
    getIntSocketOption(fd, SO_PROTOCOL, &socket.protocol);

    socket.inode = inode;
    socket.fds.push_back(fd);
    socket.fdFlags[fd] = fdFlags;
    socketByInode[inode] = sockets.size();
    sockets.push_back(std::move(socket));
  }

  for (DiscoveredSocket& socket : sockets) {
    std::sort(socket.fds.begin(), socket.fds.end());
  }
  return sockets;
}

bool
inspectSocket(const DiscoveredSocket& discovered,
              InspectedSocket *inspected)
{
  if (discovered.fds.empty() || inspected == nullptr) {
    return false;
  }

  *inspected = {};
  int fd = discovered.fds.front();
  inspected->statusFlags = _real_fcntl(fd, F_GETFL);
  if (inspected->statusFlags == -1) {
    return false;
  }

  errno = 0;
  inspected->realOwner = _real_fcntl(fd, F_GETOWN);
  if (inspected->realOwner == -1 && errno != 0) {
    return false;
  }
  if (dmtcp_pid_real_to_virtual != nullptr) {
    if (inspected->realOwner < 0) {
      inspected->realOwner =
        -dmtcp_pid_real_to_virtual(-inspected->realOwner);
    } else {
      inspected->realOwner =
        dmtcp_pid_real_to_virtual(inspected->realOwner);
    }
  }

  inspected->signal = _real_fcntl(fd, F_GETSIG);
  if (inspected->signal == -1) {
    return false;
  }
  inspected->hasDescriptorState = true;

  if (!getIntSocketOption(fd, SO_ACCEPTCONN, &inspected->acceptConn)) {
    return false;
  }

  inspected->localLen = sizeof(inspected->local);
  if (getsockname(fd,
                  reinterpret_cast<sockaddr *>(&inspected->local),
                  &inspected->localLen) == -1) {
    return false;
  }

  inspected->peerLen = sizeof(inspected->peer);
  if (getpeername(fd,
                  reinterpret_cast<sockaddr *>(&inspected->peer),
                  &inspected->peerLen) == 0) {
    inspected->hasPeer = true;
  } else if (errno == ENOTCONN) {
    inspected->peerLen = 0;
  } else {
    return false;
  }

  if ((discovered.domain == AF_INET ||
       discovered.domain == AF_INET6) &&
      discovered.protocol == IPPROTO_TCP) {
    queryInetDiagnostics(discovered, inspected);
  }
  if (discovered.domain == AF_UNIX &&
      (discovered.type == SOCK_STREAM ||
       discovered.type == SOCK_SEQPACKET)) {
    queryUnixDiagnostics(discovered, inspected);
  }
  return true;
}

} // namespace dmtcp
