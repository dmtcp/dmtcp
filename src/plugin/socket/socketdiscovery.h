#ifndef SOCKETDISCOVERY_H
#define SOCKETDISCOVERY_H

#include "dmtcpalloc.h"

#include <cstdint>
#include <sys/socket.h>

namespace dmtcp {

struct DiscoveredSocket {
  uint64_t inode = 0;
  vector<int> fds;
  map<int, int> fdFlags;
  int domain = -1;
  int type = -1;
  int protocol = -1;
};

struct InspectedSocket {
  int acceptConn = 0;
  int statusFlags = -1;
  int realOwner = 0;
  int signal = 0;
  sockaddr_storage local = {};
  socklen_t localLen = 0;
  sockaddr_storage peer = {};
  socklen_t peerLen = 0;
  bool hasPeer = false;
  int kernelState = -1;
  uint32_t listenBacklog = 0;
  uint64_t peerInode = 0;
  bool hasDiagnostics = false;
  bool hasDescriptorState = false;
};

vector<DiscoveredSocket> enumerateSockets();
bool inspectSocket(const DiscoveredSocket& discovered,
                   InspectedSocket *inspected);
int socketConnectWaitMs();

} // namespace dmtcp

#endif // SOCKETDISCOVERY_H
