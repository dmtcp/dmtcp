/***************************************************************************
 *   Copyright (C) 2006 by Jason Ansel                                     *
 *   jansel@ccs.neu.edu                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#ifndef JALIBJSOCKET_H
#define JALIBJSOCKET_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <vector>
#include "jassert.h"
#include <errno.h>
#include <sys/time.h>
#include <time.h>

namespace jalib
{

  class JSocket;

  class JSockAddr
  {
      friend class JSocket;
    public:
      JSockAddr ( const char* hostname = NULL );
      static const JSockAddr ANY;
      const struct sockaddr_in* addr() const{return &_addr;}
      socklen_t                 addrlen() const{return sizeof ( sockaddr_in );}
    private:
      struct sockaddr_in _addr;
  };


  class JSocket
  {
    public:
      ///
      /// Create new socket
  protected: JSocket(); public:
      //so we dont leak FDs
      inline static JSocket Create() { return JSocket(); }
      ///
      /// Use existing socket
      JSocket ( int fd ) : _sockfd ( fd ) {}

      bool connect ( const JSockAddr& addr, int port );
      bool connect ( const  struct  sockaddr  *addr,  socklen_t addrlen, int port );
      bool bind ( const JSockAddr& addr, int port );
      bool bind ( const  struct  sockaddr  *addr,  socklen_t addrlen );
      bool listen ( int backlog = 32 );
      JSocket accept ( struct sockaddr_storage* remoteAddr = NULL,socklen_t* remoteLen = NULL );
      bool close();
      ssize_t read ( char* buf, size_t len );
      ssize_t write ( const char* buf, size_t len );
      ssize_t readAll ( char* buf, size_t len );
      ssize_t writeAll ( const char* buf, size_t len );
      bool isValid() const;

      void enablePortReuse();

      template <typename T>
      JSocket& operator << ( const T& t ) { writeAll ( ( const char* ) &t, sizeof ( T ) ); return *this; }
      template <typename T>
      JSocket& operator >> ( T& t ) { readAll ( ( char* ) &t, sizeof ( T ) ); return *this; }

      int sockfd() const { return _sockfd; }
      operator int () { return _sockfd; }
      void changeFd ( int newFd );
    protected:
      int _sockfd;
  };

  class JClientSocket : public JSocket
  {
    public:
      JClientSocket ( const JSockAddr& addr, int port )
      {
        if ( !connect ( addr, port ) )
          close();
      }
  };

  class JServerSocket : public JSocket
  {
    public:
      JServerSocket ( const JSockAddr& addr, int port, int backlog = 32 )
      {
        enablePortReuse();
        if ( !bind ( addr, port ) || !listen ( backlog ) )
          close();
      }
  };

  class JReaderInterface
  {
    public:
      JReaderInterface ( JSocket& sock ) :_sock ( sock ) {}
      virtual ~JReaderInterface() {}
      virtual bool readOnce() = 0;
      virtual bool hadError() const = 0;
      virtual void reset() = 0;
      virtual bool ready() const = 0;
      virtual const char* buffer() const = 0;
      virtual int bytesRead() const = 0;

      const JSocket& socket() const{ return _sock; }
      JSocket& socket() { return _sock; }
    protected:
      JSocket _sock;
  };

  class JChunkReader : public JReaderInterface
  {
    public:
      JChunkReader ( JSocket sock, int chunkSize );
      JChunkReader ( const JChunkReader& that );
      ~JChunkReader();
      JChunkReader& operator= ( const JChunkReader& that );
      bool readOnce();
      void readAll();
      void reset();
      bool ready() const { return _length == _read; }
      const char* buffer() const{ return _buffer; }
      bool hadError() const { return _hadError || !_sock.isValid(); }
      int bytesRead() const {return _read;}
    protected:
      char* _buffer;
      int _length;
      int _read;
      bool _hadError;
  };

  class JWriterInterface
  {
    public:
      JWriterInterface ( JSocket& sock ) :_sock ( sock ) {}
      virtual ~JWriterInterface() {}
      virtual bool writeOnce() = 0;
      virtual bool isDone() = 0;
      virtual bool hadError() = 0;
      const JSocket& socket() const{ return _sock; }
      JSocket& socket() { return _sock; }
    protected:
      JSocket _sock;
  };

  class JChunkWriter : public JWriterInterface
  {
    public:

      JChunkWriter ( JSocket sock, const char* buf, int len );
      JChunkWriter ( const JChunkWriter& that );
      ~JChunkWriter();
      jalib::JChunkWriter& operator= ( const JChunkWriter& that );

      bool isDone();
      bool writeOnce();
      bool hadError();

    private:
      char* _buffer;
      int _length;
      int _sent;
      bool _hadError;
  };


  class JMultiSocketProgram
  {
    public:
      virtual ~JMultiSocketProgram() {}
      void addDataSocket ( JReaderInterface* sock );
      void addListenSocket ( const JSocket& sock );
      void monitorSockets ( double timeoutSec = -1 );
      virtual void onData ( JReaderInterface* sock ) = 0;
      virtual void onConnect ( const JSocket& sock, const struct sockaddr* remoteAddr,socklen_t remoteLen ) = 0;
      virtual void onDisconnect ( JReaderInterface* sock ) {};
      virtual void onTimeoutInterval() {};
      void addWrite ( JWriterInterface* write );
    protected:
      std::vector<JReaderInterface*> _dataSockets;
      std::vector<JSocket> _listenSockets;
      std::vector<JWriterInterface*> _writes;
  };

} //namespace jalib


#endif
