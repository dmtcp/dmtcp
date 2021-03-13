/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel                                 *
 *   jansel@csail.mit.edu                                                   *
 *                                                                          *
 *   This file is part of the JALIB module of DMTCP (DMTCP:dmtcp/jalib).    *
 *                                                                          *
 *  DMTCP:dmtcp/jalib is free software: you can redistribute it and/or      *
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

#define _DEFAULT_SOURCE


#include <algorithm>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <typeinfo>
#include <unistd.h>

#include "jalib.h"
#include "jassert.h"
#include "jsocket.h"

const jalib::JSockAddr jalib::JSockAddr::ANY(NULL);

jalib::JSockAddr::JSockAddr(const char *hostname /* == NULL*/,
                            int port /* == -1*/)
{
  // Initialization for any case
  memset((void *)&_addr, 0, sizeof(_addr));
  for (unsigned int i = 0; i < (max_count + 1); i++) {
    _addr[i].sin_family = AF_INET;
  }
  _count = 0;

  if (hostname == NULL) {
    _count = 1;
    _addr[0].sin_addr.s_addr = INADDR_ANY;
    if (port != -1) {
      _addr[0].sin_port = htons(port);
    }
    return;
  }

#if 0
  struct hostent ret, *result;
  char buf[1024];
  int h_errnop;

  int res =
    gethostbyname_r(hostname, &ret, buf, sizeof buf, &result, &h_errnop);

  // Fall back to gethostbyname on error
  if (res != 0) {
    JWARNING(false) (hostname) (hstrerror(h_errno))
    .Text("gethostbyname_r failed, calling gethostbyname");
    result = gethostbyname(hostname);
  }

  JWARNING(result != NULL) (hostname).Text("No such host");

  if (result != NULL) {
    JASSERT((int)sizeof(_addr.sin_addr.s_addr) <= result->h_length)
      (sizeof(_addr.sin_addr.s_addr)) (result->h_length);

    memcpy(&_addr.sin_addr.s_addr, result->h_addr, result->h_length);
    if (port != -1) {
      _addr.sin_port = htons(port);
    }
  } else { // else (hostname, port) not valid; poison the port number
    JASSERT(typeid(_addr.sin_port) == typeid(in_port_t));
    _addr.sin_port = (in_port_t)-2;
  }
#else // if 0
  struct addrinfo hints;
  struct addrinfo *res;
  memset(&hints, '\0', sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_ADDRCONFIG;

  /* FIXME: Ulrich Drepper states the following about getaddrinfo():
   *   The most important thing when using getaddrinfo is to make sure that all
   *   results are used in order. To stress the important words again: all and
   *   order. Too many (incorrect) programs only use the first result.
   * Source: http://www.akkadia.org/drepper/userapi-ipv6.html
   *
   * This would require some sort of redesign of JSockAddr and JSocket classes.
   */
  int e = getaddrinfo(hostname, NULL, &hints, &res);
  if (e == EAI_NONAME) {
    /*
     * If the AI_ADDRCONFIG flag is passed to getaddrinfo() with AF_INET or
     * AF_INET6 address families,
     * addresses in /etc/hosts are not resolved properly.
     * According to following bug reports:
     * https://bugs.launchpad.net/ubuntu/+source/glibc/+bug/583278
     * https://bugzilla.mozilla.org/show_bug.cgi?id=467497#c10
     *
     * As a temporary workaround, just call it without AI_ADDRCONFIG again
     */
    hints.ai_flags = 0;
    e = getaddrinfo(hostname, NULL, &hints, &res);
  }

  JWARNING(e == 0) (e) (gai_strerror(e)) (hostname).Text("No such host");
  if (e == 0) {
    JASSERT(sizeof(*_addr) >= res->ai_addrlen)(sizeof(*_addr))(res->ai_addrlen);

    // 1. count number of addresses returned
    struct addrinfo *r;
    for (r = res, _count = 0; r != NULL; r = r->ai_next, _count++) {}
    if (_count > max_count) {
      _count = max_count;
    }

    // 2. array for storing all necessary addresses
    int i;
    for (r = res, i = 0; r != NULL; r = r->ai_next, i++) {
      memcpy(_addr + i, r->ai_addr, r->ai_addrlen);
      if (port != -1) {
        _addr[i].sin_port = htons(port);
      }
    }
  } else { // else (hostname, port) not valid; poison the port number
    _addr[0].sin_port = (unsigned short)-2;
  }

  freeaddrinfo(res);
#endif // if 0
}

jalib::JSocket::JSocket()
{
  _sockfd = jalib::socket(AF_INET, SOCK_STREAM, 0);
}

bool
jalib::JSocket::connect(const JSockAddr &addr, int port)
{
  bool ret = false;

  // jalib::JSockAddr::JSockAddr used -2 to poison port (invalid host)
  if (addr._addr->sin_port == (unsigned short)-2) {
    return false;
  }
  for (unsigned int i = 0; i < addr._count; i++) {
    ret = JSocket::connect((sockaddr *)(addr._addr + i),
                           sizeof(addr._addr[0]),
                           port);
    if (ret || errno != ECONNREFUSED) {
      break;
    }
  }
  return ret;
}

bool
jalib::JSocket::connect(const struct  sockaddr *addr,
                        socklen_t addrlen,
                        int port)
{
  struct sockaddr_storage addrbuf;

  memset(&addrbuf, 0, sizeof(addrbuf));
  JASSERT(addrlen <= sizeof(addrbuf)) (addrlen) (sizeof(addrbuf));
  // if condition needed to stop gcc-7.x warning: -Wstringop-overflow=
  if (addrlen <= sizeof(addrbuf))
    memcpy ( &addrbuf, addr, addrlen );
  JWARNING(addrlen == sizeof(sockaddr_in)) (addrlen)
    (sizeof(sockaddr_in)).Text("may not be correct socket type");
  if (port != -1) {
    ((sockaddr_in *)&addrbuf)->sin_port = htons(port);
  }
  int count = 0;
  int ret;
  while (count++ < 10) {
    ret = jalib::connect(_sockfd, (sockaddr *)&addrbuf, addrlen);
    if (ret == 0 ||
        (ret == -1 && errno != ECONNREFUSED && errno != ETIMEDOUT)) {
      break;
    }
    if (ret == -1 && (errno == ECONNREFUSED || errno == ETIMEDOUT)) {
      struct timespec ts = { 0, 100 * 1000 * 1000 };
      nanosleep(&ts, NULL);
    }
  }
  return ret == 0;
}

bool
jalib::JSocket::bind(const JSockAddr &addr, int port)
{
  bool ret = false;

  for (unsigned int i = 0; i < addr._count; i++) {
    struct sockaddr_in addrbuf = addr._addr[i];
    addrbuf.sin_port = htons(port);
    int retval = bind((sockaddr *)&addrbuf, sizeof(addrbuf));
    ret = ret || retval;
  }
  return ret;
}

bool
jalib::JSocket::bind(const struct  sockaddr *addr, socklen_t addrlen)
{
  return jalib::bind(_sockfd, addr, addrlen) == 0;
}

bool
jalib::JSocket::listen(int backlog /* = 32*/)
{
  return jalib::listen(_sockfd, backlog) == 0;
}

jalib::JSocket
jalib::JSocket::accept(struct sockaddr_storage *remoteAddr,
                       socklen_t *remoteLen)
{
  if (remoteAddr == NULL || remoteLen == NULL) {
    return JSocket(jalib::accept(_sockfd, NULL, NULL));
  } else {
    return JSocket(jalib::accept(_sockfd, (sockaddr *)remoteAddr, remoteLen));
  }
}

void
jalib::JSocket::enablePortReuse()
{
  int one = 1;

#ifdef SO_REUSEADDR

  // This option will hopefully reduce address already in use errors. More
  // details here:
  // http://stackoverflow.com/a/14388707/1136967
  if (jalib::setsockopt(_sockfd, SOL_SOCKET, SO_REUSEADDR, &one,
                        sizeof(one)) < 0) {
    JWARNING(false)(JASSERT_ERRNO).Text("setsockopt(SO_REUSEADDR) failed");
  }
#endif // ifdef SO_REUSEADDR
#ifdef SO_REUSEPORT

  /* Setting SO_REUSEPORT can be dangerous, multiple processes can bind
   * to the same address. See this for more explanation:
   *   http://stackoverflow.com/a/14388707/1136967
   */

  // if (jalib::setsockopt(_sockfd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one))
  // < 0){
  // JWARNING(false)(JASSERT_ERRNO).Text("setsockopt(SO_REUSEPORT) failed");
  // }
#endif // ifdef SO_REUSEPORT
}

bool
jalib::JSocket::close()
{
  if (!isValid()) {
    return false;
  }
  int ret = jalib::close(_sockfd);
  _sockfd = -1;
  return ret == 0;
}

ssize_t
jalib::JSocket::read(char *buf, size_t len)
{
  return ::read(_sockfd, buf, len);
}

ssize_t
jalib::JSocket::write(const char *buf, size_t len)
{
  return ::write(_sockfd, buf, len);
}

ssize_t
jalib::JSocket::readAll(char *buf, size_t len)
{
  return jalib::readAll(_sockfd, buf, len);
}

ssize_t
jalib::JSocket::writeAll(const char *buf, size_t len)
{
  return jalib::writeAll(_sockfd, buf, len);
}

bool
jalib::JSocket::isValid() const
{
  return _sockfd >= 0;
}

/*!
    \fn jalib::JChunkReader::readAvailable()
 */
bool
jalib::JChunkReader::readOnce()
{
  if (!ready()) {
    ssize_t cnt = _sock.read(_buffer + _read, _length - _read);
    if (cnt <= 0 && errno != EAGAIN && errno != EINTR) {
      _hadError = true;
    }
    if (cnt > 0) {
      _read += cnt;
    }
  }
  return _read > 0;
}

/*!
    \fn jalib::JChunkWriter::isDone()
 */
bool
jalib::JChunkWriter::isDone()
{
  return _length <= _sent;
}

/*!
    \fn jalib::JChunkWriter::writeOnce()
 */
bool
jalib::JChunkWriter::writeOnce()
{
  if (!isDone()) {
    ssize_t cnt = _sock.write(_buffer + _sent, _length - _sent);
    if (cnt <= 0 && errno != EAGAIN && errno != EINTR) {
      _hadError = true;
    }
    if (cnt > 0) {
      _sent += cnt;
    }
  }
  return isDone();
}

/*!
    \fn jalib::JChunkWriter::hadError()
 */
bool
jalib::JChunkWriter::hadError()
{
  return _hadError || !_sock.isValid();
}

/*!
    \fn jalib::JChunkReader::reset()
 */
void
jalib::JChunkReader::reset()
{
  memset(_buffer, 0, _length);
  _read = 0;
}

/*!
    \fn jalib::JChunkReader::readAll()
 */
void
jalib::JChunkReader::readAll()
{
  while (!ready()) {
    readOnce();
  }
}

/*!
    \fn jalib::JChunkReader::JChunkReader(JSocket sock, int chunkSize);
 */
jalib::JChunkReader::JChunkReader(JSocket sock, int chunkSize)
  : JReaderInterface(sock)
  , _buffer((char *)jalib::JAllocDispatcher::malloc(chunkSize))
  , _length(chunkSize)
  , _read(0)
  , _hadError(false)
{
  memset(_buffer, 0, _length);
}

/*!
    \fn jalib::JChunkReader::JChunkReader(const JChunkReader& that)
 */
jalib::JChunkReader::JChunkReader(const JChunkReader &that)
  : JReaderInterface(that)
  , _buffer((char *)jalib::JAllocDispatcher::malloc(that._length))
  , _length(that._length)
  , _read(that._read)
  , _hadError(that._hadError)
{
  memcpy(_buffer, that._buffer, _length);
}

/*!
    \fn jalib::JChunkReader::~JChunkReader()
 */
jalib::JChunkReader::~JChunkReader()
{
  jalib::JAllocDispatcher::free(_buffer);

  _buffer = 0;
}

/*!
    \fn jalib::JChunkReader::operator=(const JChunkReader& that)
 */
jalib::JChunkReader&
jalib::JChunkReader::operator=(const JChunkReader &that)
{
  if (this == &that) {
    return *this;
  }
  jalib::JAllocDispatcher::free(_buffer);
  _buffer = 0;
  new (this)JChunkReader(that);
  return *this;
}

/*!
    \fn jalib::JSocket::changeFd(int newFd)
 */
void
jalib::JSocket::changeFd(int newFd)
{
  if (_sockfd == newFd) {
    return;
  }
  JASSERT(newFd == jalib::dup2(_sockfd, newFd))
    (_sockfd) (newFd).Text("dup2 failed");
  close();
  _sockfd = newFd;
}

void
jalib::JMultiSocketProgram::addDataSocket(JReaderInterface *sock)
{
  _dataSockets.push_back(sock);
}

void
jalib::JMultiSocketProgram::addListenSocket(const JSocket &sock)
{
  _listenSockets.push_back(sock);
}

void
jalib::JMultiSocketProgram::addWrite(JWriterInterface *write)
{
  _writes.push_back(write);
}

void
jalib::JMultiSocketProgram::setTimeoutInterval(double dblTimeout)
{
  int tSec = (int)dblTimeout;
  int tMs = (int)(1000000.0 * (dblTimeout - tSec));

  timeoutInterval.tv_sec = tSec;
  timeoutInterval.tv_usec = tMs;
  timeoutEnabled = dblTimeout > 0 && timerisset(&timeoutInterval);

  JASSERT(gettimeofday(&stoptime, NULL) == 0);
  timeradd(&timeoutInterval, &stoptime, &stoptime);
}

void
jalib::JMultiSocketProgram::monitorSockets(double dblTimeout)
{
  struct timeval tmptime = { 0, 0 };
  struct timeval timeoutBuf;
  struct timeval *timeout;

  setTimeoutInterval(dblTimeout);

  timeoutBuf = timeoutInterval;
  timeout = timeoutEnabled ? &timeoutBuf : NULL;

  dmtcp::set<int> closedFds;
  dmtcp::vector<struct pollfd>fds;
  size_t i;
  for (;;) {
    closedFds.clear();
    fds.clear();

    if (timeout == NULL && timeoutEnabled) {
      timeoutBuf = timeoutInterval;
      timeout = &timeoutBuf;
    } else if (timeout != NULL && !timeoutEnabled) {
      timeout = NULL;
    }

    struct pollfd socketFd = { 0 };

    // collect listen fds in rfds, clean up dead sockets
    for (i = 0; i < _listenSockets.size(); ++i) {
      if (_listenSockets[i].isValid()) {
        socketFd.fd = _listenSockets[i].sockfd();
        socketFd.events = POLLIN;
        fds.push_back(socketFd);
      } else {
        _listenSockets[i].close();

        // socket is dead... remove it
        JTRACE("listen socket failure") (i);

        // swap with last
        _listenSockets[i] = _listenSockets[_listenSockets.size() - 1];
        _listenSockets.pop_back();
        i--;
      }
    }

    // collect data fds in rfds, clean up dead sockets
    for (i = 0; i < _dataSockets.size(); ++i) {
      if (!_dataSockets[i]->hadError()) {
        socketFd.fd = _dataSockets[i]->socket().sockfd();
        socketFd.events = POLLIN;
        fds.push_back(socketFd);
      } else {
        JReaderInterface *dsock = _dataSockets[i];
        closedFds.insert(dsock->socket().sockfd());

        // socket is dead... remove it
        JTRACE("disconnect") (i) (_dataSockets[i]->socket().sockfd());

        _dataSockets[i] = 0;

        // swap with last
        _dataSockets[i] = _dataSockets[_dataSockets.size() - 1];
        _dataSockets.pop_back();
        i--;
        onDisconnect(dsock);
        dsock->socket().close();
        delete dsock;
      }
    }

    // collect all writes in wfds, cleanup finished/dead
    for (i = 0; i < _writes.size(); ++i) {
      if (!_writes[i]->hadError() && !_writes[i]->isDone()
          && closedFds.find(_writes[i]->socket().sockfd()) == closedFds.end()) {
        socketFd.fd = _writes[i]->socket().sockfd();
        socketFd.events = POLLOUT;
        fds.push_back(socketFd);
      } else {
        // socket is or write done... pop it
        delete _writes[i];
        _writes[i] = 0;

        // swap with last
        _writes[i] = _writes[_writes.size() - 1];
        _writes.pop_back();
        i--;
      }
    }

    if (fds.size() == 0) {
      JTRACE("no sockets left");
      return;
    }

    // NOTE:  The top level routine of dmtcp_coordinator calls monitorSockets(),
    // which then waits on activity from clients connecting to the coordinator.
    // Depending on the result of this selection, this rountine,
    // monitorSockets() will in turn call onData(), onConnect(),
    // onTimeoutInterval(), etc.  Those routines do their work, and on
    // completion, they then return to monitorSockets() to wait for more
    // work to do.  dmtcp_coordinator.cpp also describes some of this logic.
    // this will block till we have some work to do
    uint64_t millis = timeout ? ((timeout->tv_sec * (uint64_t)1000) +
                                 (timeout->tv_usec / 1000))
      : -1;
    int retval = poll((struct pollfd *)&fds[0], fds.size(), millis);

    if (retval == -1) {
      JWARNING(retval != -1)
        (fds.size()) (retval) (JASSERT_ERRNO).Text("poll failed");
      return;
    } else if (retval > 0) {
      // write all data
      dmtcp::vector<struct pollfd>::iterator it;
      for (i = 0; i < _writes.size(); ++i) {
        int fd = _writes[i]->socket().sockfd();
        for (it = fds.begin(); it != fds.end(); it++) {
          if (it->fd == fd && it->revents & POLLOUT) {
            break;
          }
        }
        if (fd >= 0 && it != fds.end()) {
          JTRACE("writing data")(_writes[i]->socket().sockfd());
          _writes[i]->writeOnce();
        }
      }


      // read all new data
      for (i = 0; i < _dataSockets.size(); ++i) {
        int fd = _dataSockets[i]->socket().sockfd();
        for (it = fds.begin(); it != fds.end(); it++) {
          if (it->fd == fd && it->revents & POLLIN) {
            break;
          }
        }
        if (fd >= 0 && it != fds.end()) {
          JTRACE("receiving data")(i)(_dataSockets[i]->socket().sockfd());
          if (_dataSockets[i]->readOnce()) {
            onData(_dataSockets[i]);
            _dataSockets[i]->reset();
          }
        }
      }


      // accept all new connections
      for (i = 0; i < _listenSockets.size(); ++i) {
        int fd = _listenSockets[i].sockfd();
        for (it = fds.begin(); it != fds.end(); it++) {
          if (it->fd == fd && it->revents & POLLIN) {
            break;
          }
        }
        if (fd >= 0 && it != fds.end()) {
          struct sockaddr_storage addr;
          socklen_t addrlen = sizeof(addr);
          JSocket sk = _listenSockets[i].accept(&addr, &addrlen);
          JTRACE("accepting new connection") (i) (sk.sockfd())
            (_listenSockets[i].sockfd()) (JASSERT_ERRNO);
          if (sk.isValid()) {
            onConnect(sk, (sockaddr *)&addr, addrlen);
          } else if (errno != EAGAIN && errno != EINTR) {
            _listenSockets[i].close();
          }
        }
      }
    }

    if (timeoutEnabled) {
      JASSERT(gettimeofday(&tmptime, NULL) == 0);
      if (timercmp(&tmptime, &stoptime, <)) {
        timersub(&stoptime, &tmptime, &timeoutBuf);
      } else {
        timeoutBuf = timeoutInterval;
        stoptime = tmptime;
        timeradd(&timeoutInterval, &stoptime, &stoptime);
        onTimeoutInterval();
      }
    }
  }
}

/*!
    \fn jalib::JChunkWriter::JChunkWriter(JSocket sock, const char* buf, int len)
 */
jalib::JChunkWriter::JChunkWriter(JSocket sock, const char *buf, int len)
  : JWriterInterface(sock)
  , _buffer((char *)jalib::JAllocDispatcher::malloc(len))
  , _length(len)
  , _sent(0)
  , _hadError(false)
{
  memcpy(_buffer, buf, len);
}

/*!
    \fn jalib::JChunkWriter::JChunkWriter(const JChunkWriter& that)
 */
jalib::JChunkWriter::JChunkWriter(const JChunkWriter &that)
  : JWriterInterface(that)
  , _buffer((char *)jalib::JAllocDispatcher::malloc(that._length))
  , _length(that._length)
  , _sent(that._sent)
  , _hadError(false)
{
  memcpy(_buffer, that._buffer, that._length);
}

/*!
    \fn jalib::JChunkWriter::~JChunkWriter()
 */
jalib::JChunkWriter::~JChunkWriter()
{
  jalib::JAllocDispatcher::free(_buffer);

  _buffer = 0;
}

/*!
    \fn jalib::JChunkWriter::method_4()
 */
jalib::JChunkWriter&
jalib::JChunkWriter::operator=(const JChunkWriter &that)
{
  if (this == &that) {
    return *this;
  }
  jalib::JAllocDispatcher::free(_buffer);
  _buffer = 0;
  new (this)JChunkWriter(that);
  return *this;
}
