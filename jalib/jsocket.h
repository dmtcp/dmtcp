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

#ifndef JALIBJSOCKET_H
#define JALIBJSOCKET_H

#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <vector>

#include "dmtcpalloc.h"
#include "jalloc.h"
#include "jassert.h"

namespace jalib
{
class JSocket;

class JSockAddr
{
  friend class JSocket;

  public:
#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR
    JSockAddr(const char *hostname = NULL, int port = -1);
    ~JSockAddr() {}

    static const JSockAddr ANY;

    const struct sockaddr_in *addr(unsigned int index = 0) const
    {
      if (index >= _count) {
        return &_addr[max_count];
      } else {
        return _addr + index;
      }
    }

    unsigned int addrcnt() const { return _count; }

    socklen_t addrlen() const { return sizeof(sockaddr_in); }

  private:
    const static unsigned int max_count = 32;

    // Allocate max_count + 1 to be able to return
    // zeroed socket structure if wrong addres index requested
    struct sockaddr_in _addr[max_count + 1];
    unsigned int _count;
};


class JSocket
{
  public:
#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR

    ///
    /// Create new socket

  protected:
    JSocket();

  public:
    // so we don't leak FDs
    inline static JSocket Create() { return JSocket(); }

    ///
    /// Use existing socket
    JSocket(int fd) : _sockfd(fd) {}

    bool connect(const JSockAddr &addr, int port);
    bool connect(const struct  sockaddr *addr, socklen_t addrlen,
                 int port = -1);
    bool bind(const JSockAddr &addr, int port);
    bool bind(const struct  sockaddr *addr, socklen_t addrlen);
    bool listen(int backlog = 32);
    JSocket accept(struct sockaddr_storage *remoteAddr = NULL,
                   socklen_t *remoteLen = NULL);
    bool close();
    ssize_t read(char *buf, size_t len);
    ssize_t write(const char *buf, size_t len);
    ssize_t readAll(char *buf, size_t len);
    ssize_t writeAll(const char *buf, size_t len);
    bool isValid() const;

    void enablePortReuse();

    template<typename T>
    JSocket&operator<<(const T &t)
    {
      writeAll((const char *)&t, sizeof(T));
      return *this;
    }

    template<typename T>
    JSocket&operator>>(T &t) { readAll((char *)&t, sizeof(T)); return *this; }

    int sockfd() const { return _sockfd; }

    // If socket originally bound to port 0, we need this to find actual port
    int port() const
    {
      struct sockaddr_in addr;
      socklen_t addrlen = sizeof(addr);

      if (-1 == getsockname(_sockfd,
                            (struct sockaddr *)&addr, &addrlen)) {
        return -1;
      } else {
        return (int)ntohs(addr.sin_port);
      }
    }

    operator int() { return _sockfd; }
    void changeFd(int newFd);

  protected:
    int _sockfd;
};

class JClientSocket : public JSocket
{
  public:
    JClientSocket(const struct sockaddr *addr, socklen_t addrlen, int port = -1)
    {
      if (!connect(addr, addrlen, port)) {
        close();
      }
    }

    JClientSocket(const JSockAddr &addr, int port)
    {
      if (!connect(addr, port)) {
        close();
      }
    }
};

class JServerSocket : public JSocket
{
  public:
    JServerSocket(int sockfd)
      : JSocket(sockfd)
    {
      enablePortReuse();
    }

    JServerSocket(const JSockAddr &addr, int port, int backlog = 32)
    {
      enablePortReuse();
      if (!bind(addr, port) || !listen(backlog)) {
        close();
      }
    }
};

class JReaderInterface
{
  public:
#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR
    JReaderInterface(JSocket &sock) : _sock(sock) {}

    virtual ~JReaderInterface() {}

    virtual bool readOnce() = 0;
    virtual bool hadError() const = 0;
    virtual void reset() = 0;
    virtual bool ready() const = 0;
    virtual const char *buffer() const = 0;
    virtual int bytesRead() const = 0;

    const JSocket &socket() const { return _sock; }

    JSocket &socket() { return _sock; }

  protected:
    JSocket _sock;
};

class JChunkReader : public JReaderInterface
{
  public:
#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR
    JChunkReader(JSocket sock, int chunkSize);
    JChunkReader(const JChunkReader &that);
    ~JChunkReader();
    JChunkReader&operator=(const JChunkReader &that);
    bool readOnce();
    void readAll();
    void reset();
    bool ready() const { return _length == _read; }

    const char *buffer() const { return _buffer; }

    bool hadError() const { return _hadError || !_sock.isValid(); }

    int bytesRead() const { return _read; }

  protected:
    char *_buffer;
    int _length;
    int _read;
    bool _hadError;
};

class JWriterInterface
{
  public:
#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR
    JWriterInterface(JSocket &sock) : _sock(sock) {}

    virtual ~JWriterInterface() {}

    virtual bool writeOnce() = 0;
    virtual bool isDone() = 0;
    virtual bool hadError() = 0;
    const JSocket &socket() const { return _sock; }

    JSocket &socket() { return _sock; }

  protected:
    JSocket _sock;
};

class JChunkWriter : public JWriterInterface
{
  public:
#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR
    JChunkWriter(JSocket sock, const char *buf, int len);
    JChunkWriter(const JChunkWriter &that);
    ~JChunkWriter();
    jalib::JChunkWriter&operator=(const JChunkWriter &that);

    bool isDone();
    bool writeOnce();
    bool hadError();

  private:
    char *_buffer;
    int _length;
    int _sent;
    bool _hadError;
};


class JMultiSocketProgram
{
  public:
#ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
#endif // ifdef JALIB_ALLOCATOR
    virtual ~JMultiSocketProgram() {}

    void addDataSocket(JReaderInterface *sock);
    void addListenSocket(const JSocket &sock);
    void monitorSockets(double timeoutSec = -1);
    virtual void onData(JReaderInterface *sock) = 0;
    virtual void onConnect(const JSocket &sock,
                           const struct sockaddr *remoteAddr,
                           socklen_t remoteLen) = 0;
    virtual void onDisconnect(JReaderInterface *sock) {}

    void setTimeoutInterval(double dblTimeout);
    virtual void onTimeoutInterval() {}

    void addWrite(JWriterInterface *write);

  protected:
    dmtcp::vector<JReaderInterface *>_dataSockets;
    dmtcp::vector<JSocket>_listenSockets;
    dmtcp::vector<JWriterInterface *>_writes;

  private:
    bool timeoutEnabled;
    struct timeval timeoutInterval;
    struct timeval stoptime;
};
} // namespace jalib
#endif // ifndef JALIBJSOCKET_H
