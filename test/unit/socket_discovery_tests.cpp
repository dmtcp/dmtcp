#include "plugin/socket/socketdiscovery.h"
#include "protectedfds.h"
#include "unit_test.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

uint64_t
socketInode(int fd)
{
  struct stat statBuf = {};
  ASSERT_EQ(fstat(fd, &statBuf), 0);
  return statBuf.st_ino;
}

const dmtcp::DiscoveredSocket *
findSocket(const dmtcp::vector<dmtcp::DiscoveredSocket>& sockets,
           uint64_t inode)
{
  auto socket = std::find_if(
    sockets.begin(), sockets.end(),
    [inode](const dmtcp::DiscoveredSocket& item) {
      return item.inode == inode;
    });
  return socket == sockets.end() ? nullptr : &*socket;
}

void
enumeratesAndInspectsSockets()
{
  int unbound = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_TRUE(unbound >= 0);

  int listener = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_TRUE(listener >= 0);
  sockaddr_in listenAddr = {};
  listenAddr.sin_family = AF_INET;
  listenAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_EQ(bind(listener,
                 reinterpret_cast<sockaddr *>(&listenAddr),
                 sizeof(listenAddr)), 0);
  ASSERT_EQ(listen(listener, 7), 0);

  int pair[2] = { -1, -1 };
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
  int alias = dup(pair[0]);
  ASSERT_TRUE(alias >= 0);
  ASSERT_EQ(fcntl(alias, F_SETFD, FD_CLOEXEC), 0);

  int protectedSocket = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_TRUE(protectedSocket >= 0);
  uint64_t protectedInode = socketInode(protectedSocket);
  ASSERT_EQ(dup2(protectedSocket, PROTECTED_SOCKET_FDREWIRER_FD),
            PROTECTED_SOCKET_FDREWIRER_FD);
  close(protectedSocket);

  dmtcp::vector<dmtcp::DiscoveredSocket> sockets =
    dmtcp::enumerateSockets();

  const dmtcp::DiscoveredSocket *unboundSocket =
    findSocket(sockets, socketInode(unbound));
  ASSERT_TRUE(unboundSocket != nullptr);
  ASSERT_EQ(unboundSocket->domain, AF_INET);
  ASSERT_EQ(unboundSocket->type, SOCK_STREAM);
  ASSERT_EQ(unboundSocket->protocol, IPPROTO_TCP);
  dmtcp::InspectedSocket inspected = {};
  ASSERT_TRUE(dmtcp::inspectSocket(*unboundSocket, &inspected));

  const dmtcp::DiscoveredSocket *pairSocket =
    findSocket(sockets, socketInode(pair[0]));
  ASSERT_TRUE(pairSocket != nullptr);
  ASSERT_EQ(pairSocket->fds.size(), static_cast<size_t>(2));
  ASSERT_EQ(pairSocket->fdFlags.at(pair[0]), 0);
  ASSERT_EQ(pairSocket->fdFlags.at(alias), FD_CLOEXEC);

  ASSERT_TRUE(findSocket(sockets, protectedInode) == nullptr);

  const dmtcp::DiscoveredSocket *listenerSocket =
    findSocket(sockets, socketInode(listener));
  ASSERT_TRUE(listenerSocket != nullptr);
  ASSERT_TRUE(dmtcp::inspectSocket(*listenerSocket, &inspected));
  ASSERT_EQ(inspected.acceptConn, 1);
  ASSERT_TRUE(inspected.localLen >= sizeof(sockaddr_in));
  ASSERT_TRUE(!inspected.hasPeer);

  ASSERT_TRUE(dmtcp::inspectSocket(*pairSocket, &inspected));
  ASSERT_TRUE(inspected.hasPeer);

  close(PROTECTED_SOCKET_FDREWIRER_FD);
  close(alias);
  close(pair[0]);
  close(pair[1]);
  close(listener);
  close(unbound);
}

void
queriesSocketDiagnostics()
{
  int listener4 = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_TRUE(listener4 >= 0);
  sockaddr_in addr4 = {};
  addr4.sin_family = AF_INET;
  addr4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_EQ(bind(listener4,
                 reinterpret_cast<sockaddr *>(&addr4),
                 sizeof(addr4)), 0);
  ASSERT_EQ(listen(listener4, 7), 0);
  socklen_t addr4Len = sizeof(addr4);
  ASSERT_EQ(getsockname(listener4,
                        reinterpret_cast<sockaddr *>(&addr4),
                        &addr4Len), 0);
  int client4 = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_TRUE(client4 >= 0);
  ASSERT_EQ(connect(client4,
                    reinterpret_cast<sockaddr *>(&addr4),
                    sizeof(addr4)), 0);
  int accepted4 = accept(listener4, nullptr, nullptr);
  ASSERT_TRUE(accepted4 >= 0);

  int bound4 = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_TRUE(bound4 >= 0);
  sockaddr_in boundAddr = {};
  boundAddr.sin_family = AF_INET;
  boundAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_EQ(bind(bound4,
                 reinterpret_cast<sockaddr *>(&boundAddr),
                 sizeof(boundAddr)), 0);

  int listener6 = socket(AF_INET6, SOCK_STREAM, 0);
  ASSERT_TRUE(listener6 >= 0);
  sockaddr_in6 addr6 = {};
  addr6.sin6_family = AF_INET6;
  addr6.sin6_addr = in6addr_loopback;
  ASSERT_EQ(bind(listener6,
                 reinterpret_cast<sockaddr *>(&addr6),
                 sizeof(addr6)), 0);
  ASSERT_EQ(listen(listener6, 9), 0);

  int streamPair[2] = { -1, -1 };
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, streamPair), 0);
  int seqPair[2] = { -1, -1 };
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, seqPair), 0);

  int unixListener = socket(AF_UNIX, SOCK_STREAM, 0);
  ASSERT_TRUE(unixListener >= 0);
  sockaddr_un unixAddr = {};
  unixAddr.sun_family = AF_UNIX;
  char name[sizeof(unixAddr.sun_path) - 1] = {};
  int nameLen = snprintf(name, sizeof(name),
                         "dmtcp-socket-discovery-%ld",
                         static_cast<long>(getpid()));
  ASSERT_TRUE(nameLen > 0);
  ASSERT_TRUE(static_cast<size_t>(nameLen) < sizeof(name));
  memcpy(unixAddr.sun_path + 1, name, nameLen);
  socklen_t unixAddrLen =
    offsetof(sockaddr_un, sun_path) + 1 + nameLen;
  ASSERT_EQ(bind(unixListener,
                 reinterpret_cast<sockaddr *>(&unixAddr),
                 unixAddrLen), 0);
  ASSERT_EQ(listen(unixListener, 5), 0);

  dmtcp::vector<dmtcp::DiscoveredSocket> sockets =
    dmtcp::enumerateSockets();
  dmtcp::InspectedSocket inspected = {};

  const dmtcp::DiscoveredSocket *socket4 =
    findSocket(sockets, socketInode(listener4));
  ASSERT_TRUE(socket4 != nullptr);
  ASSERT_TRUE(dmtcp::inspectSocket(*socket4, &inspected));
  ASSERT_TRUE(inspected.hasDiagnostics);
  ASSERT_EQ(inspected.kernelState, TCP_LISTEN);
  ASSERT_EQ(inspected.listenBacklog, static_cast<uint32_t>(7));

  const dmtcp::DiscoveredSocket *connected =
    findSocket(sockets, socketInode(client4));
  ASSERT_TRUE(connected != nullptr);
  ASSERT_TRUE(dmtcp::inspectSocket(*connected, &inspected));
  ASSERT_TRUE(inspected.hasPeer);
  ASSERT_TRUE(inspected.hasDiagnostics);
  ASSERT_EQ(inspected.kernelState, TCP_ESTABLISHED);

  const dmtcp::DiscoveredSocket *bound =
    findSocket(sockets, socketInode(bound4));
  ASSERT_TRUE(bound != nullptr);
  ASSERT_TRUE(dmtcp::inspectSocket(*bound, &inspected));
  ASSERT_TRUE(!inspected.hasPeer);
  ASSERT_TRUE(inspected.localLen >= sizeof(sockaddr_in));

  const dmtcp::DiscoveredSocket *socket6 =
    findSocket(sockets, socketInode(listener6));
  ASSERT_TRUE(socket6 != nullptr);
  ASSERT_TRUE(dmtcp::inspectSocket(*socket6, &inspected));
  ASSERT_TRUE(inspected.hasDiagnostics);
  ASSERT_EQ(inspected.kernelState, TCP_LISTEN);
  ASSERT_EQ(inspected.listenBacklog, static_cast<uint32_t>(9));

  const dmtcp::DiscoveredSocket *stream =
    findSocket(sockets, socketInode(streamPair[0]));
  ASSERT_TRUE(stream != nullptr);
  ASSERT_TRUE(dmtcp::inspectSocket(*stream, &inspected));
  ASSERT_TRUE(inspected.hasDiagnostics);
  ASSERT_EQ(inspected.peerInode, socketInode(streamPair[1]));

  const dmtcp::DiscoveredSocket *seq =
    findSocket(sockets, socketInode(seqPair[0]));
  ASSERT_TRUE(seq != nullptr);
  ASSERT_TRUE(dmtcp::inspectSocket(*seq, &inspected));
  ASSERT_TRUE(inspected.hasDiagnostics);
  ASSERT_EQ(inspected.peerInode, socketInode(seqPair[1]));

  const dmtcp::DiscoveredSocket *unixSocket =
    findSocket(sockets, socketInode(unixListener));
  ASSERT_TRUE(unixSocket != nullptr);
  ASSERT_TRUE(dmtcp::inspectSocket(*unixSocket, &inspected));
  ASSERT_TRUE(inspected.hasDiagnostics);
  ASSERT_EQ(inspected.listenBacklog, static_cast<uint32_t>(5));

  close(unixListener);
  close(seqPair[0]);
  close(seqPair[1]);
  close(streamPair[0]);
  close(streamPair[1]);
  close(bound4);
  close(accepted4);
  close(client4);
  close(listener6);
  close(listener4);
}

void
parsesConnectWait()
{
  unsetenv("DMTCP_SOCKET_CONNECT_WAIT_MS");
  ASSERT_EQ(dmtcp::socketConnectWaitMs(), 100);

  setenv("DMTCP_SOCKET_CONNECT_WAIT_MS", "25", 1);
  ASSERT_EQ(dmtcp::socketConnectWaitMs(), 25);

  for (const char *invalid : { "", "-1", "10ms", "invalid" }) {
    setenv("DMTCP_SOCKET_CONNECT_WAIT_MS", invalid, 1);
    ASSERT_EQ(dmtcp::socketConnectWaitMs(), 100);
  }
  unsetenv("DMTCP_SOCKET_CONNECT_WAIT_MS");
}

} // namespace

extern const dmtcp_test::TestCase socketDiscoveryTests[] = {
  {"enumerate and inspect sockets", enumeratesAndInspectsSockets},
  {"query socket diagnostics", queriesSocketDiagnostics},
  {"parse socket connect wait", parsesConnectWait},
};

extern const size_t socketDiscoveryTestCount =
  sizeof(socketDiscoveryTests) / sizeof(socketDiscoveryTests[0]);
