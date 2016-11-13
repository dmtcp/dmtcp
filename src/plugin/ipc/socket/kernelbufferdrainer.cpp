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

#define SOCKET_DRAIN_MAGIC_COOKIE_STR "[dmtcp{v0<DRAIN!"

using namespace dmtcp;

const char theMagicDrainCookie[] = SOCKET_DRAIN_MAGIC_COOKIE_STR;

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
  vector<char> &buffer = _drainedData[sock->socket().sockfd()];
  buffer.resize(buffer.size() + sock->bytesRead());
  int startIdx = buffer.size() - sock->bytesRead();
  memcpy(&buffer[startIdx], sock->buffer(), sock->bytesRead());

  // JTRACE("got buffer chunk") (sock->bytesRead());
  sock->reset();
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
KernelBufferDrainer::beginDrainOf(int fd, const ConnectionIdentifier &id)
{
  // JTRACE("will drain socket") (fd);
  _drainedData[fd]; // create buffer
  // this is the simple way:  jalib::JSocket(fd) << theMagicDrainCookie;
  // instead used delayed write in case kernel buffer is full:
  addWrite(new jalib::JChunkWriter(fd, theMagicDrainCookie,
                                   sizeof theMagicDrainCookie));

  // now setup a reader:
  addDataSocket(new jalib::JChunkReader(fd, 512));

  // insert it in reverse lookup
  _reverseLookup[fd] = id;
}

void
KernelBufferDrainer::refillAllSockets()
{
  JTRACE("refilling socket buffers") (_drainedData.size());

  // write all buffers out
  map<int, vector<char> >::iterator i;
  for (i = _drainedData.begin(); i != _drainedData.end(); ++i) {
    int size = i->second.size();
    JWARNING(size >= 0) (size).Text("a failed drain is in our table???");
    if (size < 0) {
      size = 0;
    }

    // Double the send buffer
    scaleSendBuffers(i->first, 2);
    ConnMsg msg(ConnMsg::REFILL);
    msg.extraBytes = size;
    jalib::JSocket sock(i->first);
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
    ConnMsg msg;
    msg.poison();
    jalib::JSocket sock(i->first);
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
    scaleSendBuffers(i->first, 0.5);
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
