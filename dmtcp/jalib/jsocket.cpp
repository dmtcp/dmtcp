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

#define _BSD_SOURCE

#include "jsocket.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include "jassert.h"
#include <errno.h>
#include <algorithm>
#include <set>
#include <typeinfo>

#ifndef DMTCP
#  define DECORATE_FN(fn) ::fn
#else
#  include "syscallwrappers.h"
#  define DECORATE_FN(fn) ::_real_ ## fn
#endif

const jalib::JSockAddr jalib::JSockAddr::ANY ( NULL );

jalib::JSockAddr::JSockAddr ( const char* hostname /* == NULL*/,
                              int port /* == -1*/ )
{
  // Initialization for any case
  memset( (void*)&_addr, 0, sizeof( _addr ) );
  for(int i=0; i < (max_count + 1); i++){
    _addr[i].sin_family = AF_INET;
  }
  _count = 0;

  if ( hostname == NULL ) {
    _count = 1;
    _addr[0].sin_addr.s_addr = INADDR_ANY;
    if (port != -1)
      _addr[0].sin_port = htons (port);
    return;
  }

#if 0
  struct hostent ret, *result;
  char buf[1024];
  int h_errnop;
  
  int res = gethostbyname_r(hostname, &ret, buf, sizeof buf, &result, &h_errnop);

  // Fall back to gethostbyname on error
  if (res != 0) {
    JWARNING (false) (hostname) (hstrerror(h_errno))
      .Text("gethostbyname_r failed, calling gethostbyname");
    result = gethostbyname ( hostname );
  }

  JWARNING (result != NULL) (hostname) .Text("No such host");

  if (result != NULL) {
    JASSERT ( ( int ) sizeof ( _addr.sin_addr.s_addr ) <= result->h_length )
      (sizeof (_addr.sin_addr.s_addr)) (result->h_length);

    memcpy ( &_addr.sin_addr.s_addr, result->h_addr, result->h_length );
    if (port != -1) _addr.sin_port = htons (port);
  } else { // else (hostname, port) not valid; poison the port number
    JASSERT( typeid(_addr.sin_port) == typeid(in_port_t) );
    _addr.sin_port = (in_port_t)-2;
  }
#else
  struct addrinfo hints;
  struct addrinfo *res;
  bzero(&hints, sizeof hints);
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
  if( e == EAI_NONAME ){
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
    e = getaddrinfo( hostname, NULL, &hints, &res);
  }

  JWARNING (e == 0) (e) (gai_strerror(e)) (hostname) .Text ("No such host");
  if (e == 0) {
    JASSERT(sizeof(*_addr) >= res->ai_addrlen)(sizeof(*_addr))(res->ai_addrlen);

    // 1. count number of addresses returned
    struct addrinfo *r;
    for(r = res, _count = 0; r != NULL; r = r->ai_next, _count++);
    if( _count > max_count)
      _count = max_count;

    // 2. array for storing all nesessary addresses
    int i;
    for(r = res, i = 0; r != NULL; r = r->ai_next, i++) {
      memcpy(_addr + i, r->ai_addr, r->ai_addrlen);
      if (port != -1)
        _addr[i].sin_port = htons (port);
    }

  } else { // else (hostname, port) not valid; poison the port number
    _addr[0].sin_port = -2;
  }

  freeaddrinfo(res);
#endif
}

jalib::JSocket::JSocket()
{
  _sockfd = DECORATE_FN ( socket ) ( AF_INET, SOCK_STREAM, 0 );
}


bool jalib::JSocket::connect ( const JSockAddr& addr, int port )
{
  bool ret = false;
  // jalib::JSockAddr::JSockAddr used -2 to poison port (invalid host)
  if (addr._addr->sin_port == (unsigned short)-2)
    return false;
  for(int i=0; i< addr._count; i++){
    if( ret = JSocket::connect( (sockaddr*)(addr._addr + i),
                                sizeof(addr._addr[0]), port ) ){
      break;
    }else{
      if( errno != ECONNREFUSED)
        break;
    }
  }
  return ret;
}


bool jalib::JSocket::connect ( const  struct  sockaddr  *addr,
                               socklen_t addrlen, int port )
{
  struct sockaddr_storage addrbuf;
  memset ( &addrbuf,0,sizeof ( addrbuf ) );
  JASSERT ( addrlen <= sizeof ( addrbuf ) ) ( addrlen ) ( sizeof ( addrbuf ) );
  memcpy ( &addrbuf,addr,addrlen );
  JWARNING ( addrlen == sizeof ( sockaddr_in ) ) ( addrlen )
          ( sizeof ( sockaddr_in ) ).Text ( "may not be correct socket type" );
  ( (sockaddr_in*)&addrbuf )->sin_port = htons ( port );
  return DECORATE_FN(connect)( _sockfd, (sockaddr*)&addrbuf, addrlen ) == 0;
}

bool jalib::JSocket::bind ( const JSockAddr& addr, int port )
{
  bool ret = false;
  for( int i=0; i<addr._count; i++){
    struct sockaddr_in addrbuf = addr._addr[i];
    addrbuf.sin_port = htons ( port );
    int retval = bind( (sockaddr*)&addrbuf, sizeof(addrbuf) );
    ret = ret || retval;
  }
  return ret;
}

bool jalib::JSocket::bind ( const  struct  sockaddr  *addr,  socklen_t addrlen )
{
  return DECORATE_FN ( bind ) ( _sockfd, addr, addrlen ) == 0;
}

bool jalib::JSocket::listen ( int backlog/* = 32*/ )
{
  return DECORATE_FN ( listen ) ( _sockfd, backlog ) == 0;
}

jalib::JSocket jalib::JSocket::accept ( struct sockaddr_storage* remoteAddr,socklen_t* remoteLen )
{
  if ( remoteAddr == NULL || remoteLen == NULL )
    return JSocket ( DECORATE_FN ( accept ) ( _sockfd,NULL,NULL ) );
  else
    return JSocket ( DECORATE_FN ( accept ) ( _sockfd, ( sockaddr* ) remoteAddr, remoteLen ) );
}

void jalib::JSocket::enablePortReuse()
{
  int one = 1;
  //These options will hopefully reduce address already in use errors
#ifdef SO_REUSEADDR
  if (setsockopt(_sockfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0){
    JWARNING(false)(JASSERT_ERRNO).Text("setsockopt(SO_REUSEADDR) failed");
  }
#endif
#ifdef SO_REUSEPORT
  if (setsockopt(_sockfd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one)) < 0){
    JWARNING(false)(JASSERT_ERRNO).Text("setsockopt(SO_REUSEPORT) failed");
  }
#endif
}

bool jalib::JSocket::close()
{
  if ( !isValid() ) return false;
  int ret = DECORATE_FN(close) ( _sockfd );
  _sockfd = -1;
  return ret==0;
}

ssize_t jalib::JSocket::read ( char* buf, size_t len )
{
  return DECORATE_FN(read) ( _sockfd,buf,len );
}

ssize_t jalib::JSocket::write ( const char* buf, size_t len )
{
  return ::write ( _sockfd,buf,len );
}

ssize_t jalib::JSocket::readAll ( char* buf, size_t len )
{
  int origLen = len;
  while ( len > 0 )
  {
    fd_set rfds;
    struct timeval tv;
    int retval;

    int tmp_sockfd = _sockfd;
    if ( tmp_sockfd == -1 ) {
      return -1;
    }

    /* Watch stdin (fd 0) to see when it has input. */
    FD_ZERO ( &rfds );
    FD_SET ( tmp_sockfd, &rfds );

    tv.tv_sec = 120;
    tv.tv_usec = 0;

    retval = DECORATE_FN(select) ( tmp_sockfd+1, &rfds, NULL, NULL, &tv );
    /* Don't rely on the value of tv now! */


    if ( retval == -1 )
    {
      if ( errno == EBADF ) {
        JWARNING (false) .Text ( "Socket already closed" );
        return -1;
      } else if( errno != EINTR ){ 
        JWARNING ( retval >= 0 )
          ( tmp_sockfd ) ( JASSERT_ERRNO ).Text ( "select() failed" );
        return -1;
      }
    }
    else if ( retval )
    {
      errno = 0;
      ssize_t cnt = read ( buf, len );
      if ( cnt < 0 && errno != EAGAIN && errno != EINTR )
      {
        JWARNING(cnt>=0)(sockfd())(cnt)(len)(JASSERT_ERRNO).Text( "JSocket read failure" );
        return -1;
      }
      if (cnt == 0)
      {
        JWARNING(cnt!=0)(sockfd())(origLen)(len).Text( "JSocket needed to read origLen chars,\n still needs to read len chars, but EOF reached" );
        return -1;
      }
      if ( cnt > 0 )
      {
        buf += cnt;
        len -= cnt;
      }
    }
    else
    {
      JTRACE ( "still waiting for data" ) ( tmp_sockfd ) ( len );
    }
  }
  return origLen;
}

ssize_t jalib::JSocket::writeAll ( const char* buf, size_t len )
{
  int origLen = len;
  while ( len > 0 )
  {
    fd_set wfds;
    struct timeval tv;
    int retval;

    int tmp_sockfd = _sockfd;
    if ( tmp_sockfd == -1 ) {
      return -1;
    }

    /* Watch stdin (fd 0) to see when it has input. */
    FD_ZERO ( &wfds );
    FD_SET ( tmp_sockfd, &wfds );

    /* Wait up to five seconds. */
    tv.tv_sec = 30;
    tv.tv_usec = 0;

    retval = DECORATE_FN(select) ( tmp_sockfd+1, NULL, &wfds, NULL, &tv );
    /* Don't rely on the value of tv now! */


    if ( retval == -1 )
    {
      if ( errno == EBADF ) {
        JWARNING (false) .Text ( "Socket already closed" );
        return -1;
      } 
      JWARNING ( retval >= 0 ) ( tmp_sockfd ) ( JASSERT_ERRNO ).Text ( "select() failed" );
      return -1;
    }
    else if ( retval )
    {
      errno = 0;
      ssize_t cnt = write ( buf, len );
      if ( cnt <= 0 && errno != EAGAIN && errno != EINTR )
      {
        JWARNING ( cnt > 0 ) ( cnt ) ( len ) ( JASSERT_ERRNO ).Text ( "JSocket read failure" );
        return -1;
      }
      if ( cnt > 0 )
      {
        buf += cnt;
        len -= cnt;
      }
    }
    else
    {
      JTRACE ( "still waiting for data" ) ( tmp_sockfd ) ( len );
    }
  }
  return origLen;
}

bool jalib::JSocket::isValid() const
{
  return _sockfd >= 0;
}




/*!
    \fn jalib::JChunkReader::readAvailable()
 */
bool jalib::JChunkReader::readOnce()
{
  if ( !ready() )
  {
    ssize_t cnt = _sock.read ( _buffer + _read, _length - _read );
    if ( cnt <= 0 && errno != EAGAIN && errno != EINTR )
      _hadError = true;
    if ( cnt > 0 )
      _read += cnt;
  }
  return ready();
}

/*!
    \fn jalib::JChunkWriter::isDone()
 */
bool jalib::JChunkWriter::isDone()
{
  return _length <= _sent;
}


/*!
    \fn jalib::JChunkWriter::writeOnce()
 */
bool jalib::JChunkWriter::writeOnce()
{
  if ( !isDone() )
  {
    ssize_t cnt = _sock.write ( _buffer + _sent, _length - _sent );
    if ( cnt <= 0 && errno != EAGAIN && errno != EINTR )
      _hadError = true;
    if ( cnt > 0 )
      _sent += cnt;
  }
  return isDone();
}


/*!
    \fn jalib::JChunkWriter::hadError()
 */
bool jalib::JChunkWriter::hadError()
{
  return _hadError || !_sock.isValid();
}



/*!
    \fn jalib::JChunkReader::reset()
 */
void jalib::JChunkReader::reset()
{
  memset ( _buffer,0,_length );
  _read = 0;
}


/*!
    \fn jalib::JChunkReader::readAll()
 */
void jalib::JChunkReader::readAll()
{
  while ( !ready() ) readOnce();
}

/*!
    \fn jalib::JChunkReader::JChunkReader(JSocket sock, int chunkSize);
 */
jalib::JChunkReader::JChunkReader ( JSocket sock, int chunkSize )
    : JReaderInterface ( sock )
    , _buffer ( new char[chunkSize] )
    , _length ( chunkSize )
    , _read ( 0 )
    , _hadError ( false )
{
  memset ( _buffer,0,_length );
}


/*!
    \fn jalib::JChunkReader::JChunkReader(const JChunkReader& that)
 */
jalib::JChunkReader::JChunkReader ( const JChunkReader& that )
    : JReaderInterface ( that )
    , _buffer ( new char[that._length] )
    , _length ( that._length )
    , _read ( that._read )
    , _hadError ( that._hadError )
{
  memcpy ( _buffer,that._buffer,_length );
}


/*!
    \fn jalib::JChunkReader::~JChunkReader()
 */
jalib::JChunkReader::~JChunkReader()
{
  delete [] _buffer;
  _buffer = 0;
}


/*!
    \fn jalib::JChunkReader::operator=(const JChunkReader& that)
 */
jalib::JChunkReader& jalib::JChunkReader::operator= ( const JChunkReader& that )
{
  if ( this == &that ) return *this;;
  delete [] _buffer;
  _buffer = 0;
  new ( this ) JChunkReader ( that );
  return *this;
}




/*!
    \fn jalib::JSocket::changeFd(int newFd)
 */
void jalib::JSocket::changeFd ( int newFd )
{
  if ( _sockfd == newFd ) return;
  JASSERT ( newFd == DECORATE_FN(dup2) ( _sockfd, newFd ) )
      ( _sockfd ) ( newFd ).Text ( "dup2 failed" );
  close();
  _sockfd = newFd;
}


void jalib::JMultiSocketProgram::addDataSocket ( JReaderInterface* sock )
{
  _dataSockets.push_back ( sock );
}

void jalib::JMultiSocketProgram::addListenSocket ( const JSocket& sock )
{
  _listenSockets.push_back ( sock );
}


void jalib::JMultiSocketProgram::addWrite ( JWriterInterface* write )
{
  _writes.push_back ( write );
}

void jalib::JMultiSocketProgram::setTimeoutInterval ( double dblTimeout )
{
  int tSec = ( int ) dblTimeout;
  int tMs = ( int ) ( 1000000.0 * ( dblTimeout - tSec ) );
  timeoutInterval.tv_sec  = tSec;
  timeoutInterval.tv_usec = tMs;
  timeoutEnabled = dblTimeout > 0 && timerisset ( &timeoutInterval );

  JASSERT ( gettimeofday ( &stoptime,NULL ) ==0 );
  timeradd ( &timeoutInterval,&stoptime,&stoptime );
}

void jalib::JMultiSocketProgram::monitorSockets ( double dblTimeout )
{
  /*
  int tSec = ( int ) dblTimeout;
  int tMs = ( int ) ( 1000000.0 * ( dblTimeout - tSec ) );
  const struct timeval timeoutInterval = {tSec,tMs};
  bool timeoutEnabled = dblTimeout > 0 && timerisset ( &timeoutInterval );

  struct timeval stoptime={0,0};
  struct timeval timeoutBuf=timeoutInterval;
  struct timeval * timeout = timeoutEnabled ? &timeoutBuf : NULL;
  JASSERT ( gettimeofday ( &stoptime,NULL ) ==0 );
  timeradd ( &timeoutInterval,&stoptime,&stoptime );
  */
  struct timeval tmptime={0,0};
  struct timeval timeoutBuf;
  struct timeval * timeout;

  setTimeoutInterval ( dblTimeout );

  timeoutBuf = timeoutInterval;
  timeout = timeoutEnabled ? &timeoutBuf : NULL;

  IntSet closedFds;
  fd_set rfds;
  fd_set wfds;
  int maxFd;
  size_t i;
  for ( ;; )
  {
    closedFds.clear();
    maxFd = -1;
    FD_ZERO ( &rfds );
    FD_ZERO ( &wfds );

    if( timeout == NULL && timeoutEnabled){
      timeoutBuf=timeoutInterval;
      timeout = &timeoutBuf;
    }else if( timeout != NULL && !timeoutEnabled){
      timeout = NULL;
    }

    //collect listen fds in rfds, cleanup dead sockets
    for ( i=0; i<_listenSockets.size(); ++i )
    {
      if ( _listenSockets[i].isValid() )
      {
        FD_SET ( _listenSockets[i].sockfd(), &rfds );
        maxFd = std::max ( maxFd,_listenSockets[i].sockfd() );
      }
      else
      {
        _listenSockets[i].close();
        //socket is dead... remove it
        JTRACE ( "listen socket failure" ) ( i );
        //swap with last
        _listenSockets[i] = _listenSockets[_listenSockets.size()-1];
        _listenSockets.pop_back();
        i--;
      }
    }

    //collect data fds in rfds, cleanup dead sockets
    for ( i=0; i<_dataSockets.size(); ++i )
    {
      if ( !_dataSockets[i]->hadError() )
      {
        FD_SET ( _dataSockets[i]->socket().sockfd(), &rfds );
        maxFd = std::max ( maxFd,_dataSockets[i]->socket().sockfd() );
      }
      else
      {
        closedFds.insert(_dataSockets[i]->socket().sockfd());
        //socket is dead... remove it
        //JTRACE ( "disconnect" ) ( i ) ( _dataSockets[i]->socket().sockfd() );
        onDisconnect ( _dataSockets[i] );
        _dataSockets[i]->socket().close();

        delete _dataSockets[i];
        _dataSockets[i] = 0;
        //swap with last
        _dataSockets[i] = _dataSockets[_dataSockets.size()-1];
        _dataSockets.pop_back();
        i--;
      }
    }

    //collect all writes in wfds, cleanup finished/dead
    for ( i=0; i<_writes.size(); ++i )
    {
      if (  !_writes[i]->hadError()
         && !_writes[i]->isDone()
         && closedFds.find(_writes[i]->socket().sockfd())==closedFds.end() )
      {
        FD_SET ( _writes[i]->socket().sockfd(), &wfds );
        maxFd = std::max ( maxFd,_writes[i]->socket().sockfd() );
      }
      else
      {
        //socket is or write done... pop it
        delete _writes[i];
        _writes[i] = 0;
        //swap with last
        _writes[i] = _writes[_writes.size()-1];
        _writes.pop_back();
        i--;
      }
    }

    if ( maxFd == -1 )
    {
      //JTRACE ( "no sockets left" );
      return;
    }

    //this will block till we have some work to do
    int retval = DECORATE_FN(select) ( maxFd+1, &rfds, &wfds, NULL, timeout );

    if ( retval == -1 )
    {
      JWARNING ( retval != -1 )
        ( maxFd ) ( retval ) ( JASSERT_ERRNO ).Text ( "select failed" );
      return;
    }
    else if ( retval > 0 )
    {

      //write all data
      for ( i=0; i<_writes.size(); ++i )
      {
        int fd = _writes[i]->socket().sockfd();
        if ( fd >= 0 && FD_ISSET ( fd, &wfds ) )
        {
//                    JTRACE("writing data")(_writes[i]->socket().sockfd());
          _writes[i]->writeOnce();
        }
      }


      //read all new data
      for ( i=0; i<_dataSockets.size(); ++i )
      {
        int fd = _dataSockets[i]->socket().sockfd();
        if ( fd >= 0 && FD_ISSET ( fd, &rfds ) )
        {
//                   JTRACE("receiving data")(i)(_dataSockets[i].socket().sockfd());
          if ( _dataSockets[i]->readOnce() )
          {
            onData ( _dataSockets[i] );
            _dataSockets[i]->reset();
          }
        }
      }


      //accept all new connections
      for ( i=0; i<_listenSockets.size(); ++i )
      {
        int fd = _listenSockets[i].sockfd();
        if ( fd >= 0 && FD_ISSET ( fd, &rfds ) )
        {
          struct sockaddr_storage addr;
          socklen_t addrlen = sizeof ( addr );
          JSocket sk = _listenSockets[i].accept ( &addr,&addrlen );
          JTRACE ( "accepting new connection" ) ( i ) ( sk.sockfd() )
            ( _listenSockets[i].sockfd() ) ( JASSERT_ERRNO );
          if ( sk.isValid() )
          {
            onConnect ( sk, ( sockaddr* ) &addr,addrlen );
          }
          else if ( errno != EAGAIN && errno != EINTR )
          {
            _listenSockets[i].close();
          }
        }
      }



    }

    if ( timeoutEnabled )
    {
      JASSERT ( gettimeofday ( &tmptime,NULL ) ==0 );
      if ( timercmp ( &tmptime, &stoptime, < ) )
      {
        timersub ( &stoptime, &tmptime, &timeoutBuf );
      }
      else
      {
        timeoutBuf = timeoutInterval;
        stoptime = tmptime;
        timeradd ( &timeoutInterval,&stoptime,&stoptime );
//                 JTRACE("timeout interval")(timeoutSec);
        onTimeoutInterval();
      }
    }
  }
}





/*!
    \fn jalib::JChunkWriter::JChunkWriter(JSocket sock, const char* buf, int len)
 */
jalib::JChunkWriter::JChunkWriter ( JSocket sock, const char* buf, int len )
    :JWriterInterface ( sock )
    ,_buffer ( new char [len] )
    ,_length ( len )
    ,_sent ( 0 )
    ,_hadError ( false )
{
  memcpy ( _buffer,buf,len );
}


/*!
    \fn jalib::JChunkWriter::JChunkWriter(const JChunkWriter& that)
 */
jalib::JChunkWriter::JChunkWriter ( const JChunkWriter& that )
    :JWriterInterface ( that )
    ,_buffer ( new char [that._length] )
    ,_length ( that._length )
    ,_sent ( that._sent )
    ,_hadError ( false )
{
  memcpy ( _buffer,that._buffer,that._length );
}


/*!
    \fn jalib::JChunkWriter::~JChunkWriter()
 */
jalib::JChunkWriter::~JChunkWriter()
{
  delete [] _buffer;
  _buffer = 0;
}


/*!
    \fn jalib::JChunkWriter::method_4()
 */
jalib::JChunkWriter& jalib::JChunkWriter::operator= ( const JChunkWriter& that )
{
  if ( this == &that ) return *this;;
  delete [] _buffer;
  _buffer = 0;
  new ( this ) JChunkWriter ( that );
  return *this;
}


