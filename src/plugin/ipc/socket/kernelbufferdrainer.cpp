/****************************************************************************
 *   Copyright (C) 2006-2010 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#include "kernelbufferdrainer.h"
#include "../jalib/jassert.h"
#include "../jalib/jbuffer.h"
#include "connectionlist.h"
#include "connectionmessage.h"
#include "socketwrappers.h"
#include "util.h"
#include <sys/ioctl.h>
#include <sys/socket.h>

#define SOCKET_DRAIN_MAGIC_COOKIE_STR "[dmtcp{v0<DRAIN!"

using namespace dmtcp;

const char theMagicDrainCookie[] = SOCKET_DRAIN_MAGIC_COOKIE_STR;

// Reader for SOCK_SEQPACKET that preserves message boundaries by reading
// exactly one frame per readOnce() using FIONREAD to determine size.
class JSeqpacketReader : public jalib::JReaderInterface
{
  public:
    JSeqpacketReader(jalib::JSocket sock)
      : jalib::JReaderInterface(sock), _hadError(false) {}

    bool readOnce() override
    {
      if (!_frame.empty()) {
        return true;
      }
      int fd = _sock.sockfd();
      // First peek the next message length without removing it from the queue
      struct msghdr peekMsg;
      memset(&peekMsg, 0, sizeof(peekMsg));
      // No iovecs; MSG_TRUNC returns full packet length for seqpacket
      ssize_t needed = ::recvmsg(fd, &peekMsg, MSG_PEEK | MSG_TRUNC);
      if (needed <= 0) {
        if (errno != EAGAIN && errno != EINTR) {
          _hadError = true;
        }
        return false;
      }
      _frame.resize((size_t)needed);
      struct iovec iov;
      iov.iov_base = _frame.data();
      iov.iov_len = _frame.size();
      struct msghdr readMsg;
      memset(&readMsg, 0, sizeof(readMsg));
      readMsg.msg_iov = &iov;
      readMsg.msg_iovlen = 1;
      ssize_t got = ::recvmsg(fd, &readMsg, 0);
      if (got <= 0) {
        if (errno != EAGAIN && errno != EINTR) {
          _hadError = true;
        }
        _frame.clear();
        return false;
      }
      _frame.resize((size_t)got);
      return true;
    }

    bool hadError() const override { return _hadError || !_sock.isValid(); }

    void reset() override { _frame.clear(); }

    bool ready() const override { return !_frame.empty(); }

    const char *buffer() const override { return _frame.empty() ? NULL : _frame.data(); }

    int bytesRead() const override { return (int)_frame.size(); }

  private:
    dmtcp::vector<char> _frame;
    bool _hadError;
};

void
scaleSendBuffers(int fd, double factor)
{
  int size;
  unsigned len = sizeof(size);

  JASSERT(getsockopt(fd, SOL_SOCKET, SO_SNDBUF, (void *)&size, &len) == 0);

  // getsockopt returns doubled size. So, if we pass the same value to
  // setsockopt, it would double the buffer size.
  int newSize = static_cast<int>(size * factor / 2);
  len = sizeof(newSize);
  JASSERT(_real_setsockopt(fd,
                           SOL_SOCKET,
                           SO_SNDBUF,
                           (void *)&newSize,
                           len) == 0);
}

static KernelBufferDrainer *theDrainer = NULL;
KernelBufferDrainer&
KernelBufferDrainer::instance()
{
  if (theDrainer == NULL) {
    theDrainer = new KernelBufferDrainer();
  }
  return *theDrainer;
}

void
KernelBufferDrainer::onConnect(const jalib::JSocket &sock,
                               const struct sockaddr *remoteAddr,
                               socklen_t remoteLen)
{
  JWARNING(false) (sock.sockfd())
  .Text("we don't yet support checkpointing non-accepted connections..."
        " restore will likely fail.. closing connection");
  jalib::JSocket(sock).close();
}

void
KernelBufferDrainer::onData(jalib::JReaderInterface *sock)
{
  int fd = sock->socket().sockfd();
  // Detect and cache socket type
  JASSERT(_isSeqpacket.find(fd) != _isSeqpacket.end()) (fd);

  if (_isSeqpacket[fd]) {
    // Each onData corresponds to one full frame from JSeqpacketReader
    dmtcp::vector<char> frame;
    frame.resize(sock->bytesRead());
    memcpy(frame.data(), sock->buffer(), sock->bytesRead());
    _drainedFrames[fd].push_back(frame);
    sock->reset();
  } else {
    vector<char> &buffer = _drainedData[fd];
    buffer.resize(buffer.size() + sock->bytesRead());
    int startIdx = buffer.size() - sock->bytesRead();
    memcpy(&buffer[startIdx], sock->buffer(), sock->bytesRead());
    sock->reset();
  }
}

void
KernelBufferDrainer::onDisconnect(jalib::JReaderInterface *sock)
{
  int fd;

  errno = 0;
  fd = sock->socket().sockfd();

  // check if this was on purpose
  if (fd < 0) {
    return;
  }
  JTRACE("found disconnected socket... marking it dead")
    (fd) (_reverseLookup[fd]) (JASSERT_ERRNO);
  _disconnectedSockets[_reverseLookup[fd]] = _drainedData[fd];

  // _drainedData is used to refill socket buffers. Remove the disconnected
  // socket from this list. Disconnected sockets are refilled when they are
  // recreated by _makeDeadSocket().
  _drainedData.erase(fd);
}

void
KernelBufferDrainer::onTimeoutInterval()
{
  int count = 0;

  for (size_t i = 0; i < _dataSockets.size(); ++i) {
    if (_dataSockets[i]->bytesRead() > 0) {
      onData(_dataSockets[i]);
    }
    int fd = _dataSockets[i]->socket().sockfd();
    if (_isSeqpacket[fd]) {
      dmtcp::vector< dmtcp::vector<char> > &frames = _drainedFrames[fd];
      if (!frames.empty()) {
        dmtcp::vector<char> &last = frames.back();
        if ((size_t)last.size() == sizeof(theMagicDrainCookie) &&
            memcmp(last.data(), theMagicDrainCookie,
                   sizeof(theMagicDrainCookie)) == 0) {
          // Remove cookie frame and mark drained
          frames.pop_back();
          JTRACE("seqpacket drain complete") (fd) (frames.size())
            ((_dataSockets.size()));
          _dataSockets[i]->socket() = -1; // poison socket
          continue;
        }
      }
      ++count;
    } else {
      vector<char> &buffer = _drainedData[fd];
      if (buffer.size() >= sizeof(theMagicDrainCookie)
          && memcmp(&buffer[buffer.size() - sizeof(theMagicDrainCookie)],
                    theMagicDrainCookie,
                    sizeof(theMagicDrainCookie)) == 0) {
        buffer.resize(buffer.size() - sizeof(theMagicDrainCookie));
        JTRACE("buffer drain complete") (fd)
          (buffer.size()) ((_dataSockets.size()));
        _dataSockets[i]->socket() = -1; // poison socket
      } else {
        ++count;
      }
    }
  }

  if (count == 0) {
    _listenSockets.clear();
  } else {
    const static int WARN_INTERVAL_TICKS =
      (int)(DRAINER_WARNING_FREQ / DRAINER_CHECK_FREQ + 0.5);
    const static float WARN_INTERVAL_SEC =
      WARN_INTERVAL_TICKS * DRAINER_CHECK_FREQ;
    if (_timeoutCount++ > WARN_INTERVAL_TICKS) {
      _timeoutCount = 0;
      for (size_t i = 0; i < _dataSockets.size(); ++i) {
        vector<char> &buffer = _drainedData[_dataSockets[i]->socket().sockfd()];
        JWARNING(false) (_dataSockets[i]->socket().sockfd())
          (buffer.size()) (WARN_INTERVAL_SEC)
        .Text("Still draining socket... "
              "perhaps remote host is not running under DMTCP?");
#ifdef CERN_CMS
        JNOTE("\n*** Closing this socket (to database?).  Please use dmtcp \n"
              "***  plugins to gracefully handle such sockets, and re-run.\n"
              "***  Trying a workaround for now, and hoping it doesn't fail.\n"
             );
        _real_close(_dataSockets[i]->socket().sockfd());

        // it does it by creating a socket pair and closing one side
        int sp[2] = { -1, -1 };
        JASSERT(_real_socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0)
          (JASSERT_ERRNO).Text("socketpair() failed");
        JASSERT(sp[0] >= 0 && sp[1] >= 0) (sp[0]) (sp[1])
        .Text("socketpair() failed");
        _real_close(sp[1]);
        JTRACE("created dead socket") (sp[0]);
        _real_dup2(sp[0], _dataSockets[i]->socket().sockfd());
#endif // ifdef CERN_CMS
      }
    }
  }
}

void
KernelBufferDrainer::beginDrainOf(int fd, const ConnectionIdentifier &id, int baseType)
{
  // JTRACE("will drain socket") (fd);
  _drainedData[fd]; // create buffer
  _drainedFrames[fd]; // create frames list (possibly unused)
  // this is the simple way:  jalib::JSocket(fd) << theMagicDrainCookie;
  // instead used delayed write in case kernel buffer is full:
  addWrite(new jalib::JChunkWriter(fd, theMagicDrainCookie,
                                   sizeof theMagicDrainCookie));

  // now setup a reader:
  if (_isSeqpacket[fd] = (baseType == SOCK_SEQPACKET)) {
    addDataSocket(new JSeqpacketReader(fd));
  } else {
    addDataSocket(new jalib::JChunkReader(fd, 512));
  }

  // insert it in reverse lookup
  _reverseLookup[fd] = id;
}

void
KernelBufferDrainer::refillAllSockets()
{
  JTRACE("refilling socket buffers") (_drainedData.size());

  // write all buffers out (stream sockets only)
  map<int, vector<char> >::iterator i;
  for (i = _drainedData.begin(); i != _drainedData.end(); ++i) {
    int fd = i->first;
    if (_isSeqpacket[fd]) {
      continue;
    }
    int size = i->second.size();
    JWARNING(size >= 0) (size).Text("a failed drain is in our table???");
    if (size < 0) {
      size = 0;
    }

    // Double the send buffer
    scaleSendBuffers(fd, 2);
    ConnMsg msg(ConnMsg::REFILL);
    msg.extraBytes = size;
    jalib::JSocket sock(fd);
    if (size > 0) {
      JTRACE("requesting repeat buffer...") (sock.sockfd()) (size);
    }
    sock << msg;
    if (size > 0) {
      sock.writeAll(&i->second[0], size);
    }
    i->second.clear();
  }

  // JTRACE("repeating our friends buffers...");

  // read all buffers in
  for (i = _drainedData.begin(); i != _drainedData.end(); ++i) {
    int fd = i->first;
    if (_isSeqpacket[fd]) {
      continue;
    }
    ConnMsg msg;
    msg.poison();
    jalib::JSocket sock(fd);
    sock >> msg;

    msg.assertValid(ConnMsg::REFILL);
    int size = msg.extraBytes;
    JTRACE("repeating buffer back to peer") (size);
    if (size > 0) {
      // echo it back...
      jalib::JBuffer tmp(size);
      sock.readAll(tmp, size);
      sock.writeAll(tmp, size);
    }

    // Reset the send buffer
    scaleSendBuffers(fd, 0.5);
  }

  // Now handle seqpacket sockets: send frames and echo back peer frames
  map<int, dmtcp::vector< dmtcp::vector<char> > >::iterator f;
  for (f = _drainedFrames.begin(); f != _drainedFrames.end(); ++f) {
    int fd = f->first;
    if (!_isSeqpacket[fd]) {
      continue;
    }
    // Send our frames
    scaleSendBuffers(fd, 2);
    jalib::JSocket sock(fd);
    ConnMsg msgOut(ConnMsg::REFILL);
    msgOut.extraBytes = (int)f->second.size();
    sock << msgOut;
    for (size_t k = 0; k < f->second.size(); ++k) {
      uint32_t len32 = (uint32_t)f->second[k].size();
      sock.writeAll((const char *)&len32, sizeof(len32));
      if (len32 > 0) {
        sock.writeAll(f->second[k].data(), len32);
      }
    }
    f->second.clear();
  }

  // Read peer frames and echo back
  for (f = _drainedFrames.begin(); f != _drainedFrames.end(); ++f) {
    int fd = f->first;
    if (!_isSeqpacket[fd]) {
      continue;
    }
    jalib::JSocket sock(fd);
    ConnMsg msgIn;
    msgIn.poison();
    sock >> msgIn;
    msgIn.assertValid(ConnMsg::REFILL);
    int count = msgIn.extraBytes;
    for (int c = 0; c < count; ++c) {
      uint32_t len32 = 0;
      sock.readAll((char *)&len32, sizeof(len32));
      if (len32 > 0) {
        jalib::JBuffer tmp(len32);
        sock.readAll(tmp, len32);
        sock.writeAll((const char *)&len32, sizeof(len32));
        sock.writeAll(tmp, len32);
      } else {
        sock.writeAll((const char *)&len32, sizeof(len32));
      }
    }
    scaleSendBuffers(fd, 0.5);
  }

  JTRACE("buffers refilled");

  // Free up the object
  delete theDrainer;
  theDrainer = NULL;
}

const vector<char>&
KernelBufferDrainer::getDrainedData(ConnectionIdentifier id)
{
  JASSERT(_disconnectedSockets.find(id) != _disconnectedSockets.end()) (id);
  return _disconnectedSockets[id];
}
