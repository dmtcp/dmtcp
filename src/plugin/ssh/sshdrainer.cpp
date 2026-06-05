#include "sshdrainer.h"
#include "../jalib/jassert.h"
#include "util.h"
#include "util_assert.h"

#define SOCKET_DRAIN_MAGIC_COOKIE_STR "[dmtcp{v0<DRAIN!"

using namespace dmtcp;

const char theMagicDrainCookie[] = SOCKET_DRAIN_MAGIC_COOKIE_STR;

void
SSHDrainer::onConnect(const jalib::JSocket &sock, const struct sockaddr *
                      remoteAddr, socklen_t remoteLen)
{
  ASSERT(false, "SSHDrainer::onConnect is not implemented");
}

void
SSHDrainer::onData(jalib::JReaderInterface *sock)
{
  vector<char> &buffer = _drainedData[sock->socket().sockfd()];
  buffer.resize(buffer.size() + sock->bytesRead());
  int startIdx = buffer.size() - sock->bytesRead();
  memcpy(&buffer[startIdx], sock->buffer(), sock->bytesRead());

  // JTRACE("got buffer chunk") (sock->bytesRead());
  sock->reset();
}

void
SSHDrainer::onDisconnect(jalib::JReaderInterface *sock)
{
  int fd;

  errno = 0;
  fd = sock->socket().sockfd();

  // check if this was on purpose
  if (fd < 0) {
    return;
  }
  JNOTE("found disconnected socket... marking it dead")
    (fd) (strerror(errno));
  _drainedData.erase(fd);
  ASSERT(false, "SSHDrainer::onDisconnect is not implemented");
}

void
SSHDrainer::onTimeoutInterval()
{
  int count = 0;

  for (size_t i = 0; i < _dataSockets.size(); ++i) {
    if (_dataSockets[i]->bytesRead() > 0) {
      onData(_dataSockets[i]);
    }
    vector<char> &buffer = _drainedData[_dataSockets[i]->socket().sockfd()];
    if (buffer.size() >= sizeof(theMagicDrainCookie)
        && memcmp(&buffer[buffer.size() - sizeof(theMagicDrainCookie)],
                  theMagicDrainCookie,
                  sizeof(theMagicDrainCookie)) == 0) {
      buffer.resize(buffer.size() - sizeof(theMagicDrainCookie));
      JTRACE("buffer drain complete") (_dataSockets[i]->socket().sockfd())
        (buffer.size()) ((_dataSockets.size()));
      _dataSockets[i]->socket() = -1; // poison socket
    } else {
      ++count;
    }
  }

  if (count == 0) {
    _listenSockets.clear();
  } else {
    const static int WARN_INTERVAL_TICKS =
      (int)(SSH_DRAINER_WARNING_FREQ / SSH_DRAINER_CHECK_FREQ + 0.5);
    const static int WARN_INTERVAL_MS =
      (int)(WARN_INTERVAL_TICKS * SSH_DRAINER_CHECK_FREQ * 1000 + 0.5);
    if (_timeoutCount++ > WARN_INTERVAL_TICKS) {
      _timeoutCount = 0;
      for (size_t i = 0; i < _dataSockets.size(); ++i) {
        vector<char> &buffer = _drainedData[_dataSockets[i]->socket().sockfd()];
        WARN(false,
                "Still draining socket; perhaps remote host is not running "
                "under DMTCP: fd={} bytes={} interval_ms={}",
                _dataSockets[i]->socket().sockfd(), buffer.size(),
                WARN_INTERVAL_MS);
      }
    }
  }
}

void
SSHDrainer::beginDrainOf(int fd, int refillFd)
{
  if (refillFd == -1) {
    // Just write to the socket
    addWrite(new jalib::JChunkWriter(fd, theMagicDrainCookie,
                                     sizeof theMagicDrainCookie));
  } else {
    // Need to relay the read data to the refillFd.
    _drainedData[fd]; // create buffer
    _refillFd[fd] = refillFd;
    addDataSocket(new jalib::JChunkReader(fd, 512));
  }
}

void
SSHDrainer::refill()
{
  JTRACE("refilling socket buffers") (_drainedData.size());

  // write all buffers out
  map<int, vector<char> >::iterator i;
  for (i = _drainedData.begin(); i != _drainedData.end(); ++i) {
    int fd = i->first;
    int refillFd = _refillFd[fd];

    size_t size = i->second.size();
    if (size > 0) {
      Util::writeAll(refillFd, i->second.data(), size);
    }
    i->second.clear();
  }
}
