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

#include  "../jalib/jassert.h"
#include "constants.h"
#include "sockettable.h"
#include  "../jalib/jbuffer.h"
#include "connectionmanager.h"
#include "syscallwrappers.h"

namespace
{
  const char theMagicDrainCookie[] = SOCKET_DRAIN_MAGIC_COOKIE_STR;

  void scaleSendBuffers ( double factor )
  {
    //todo resize buffers to avoid blocking
  }

}


void dmtcp::KernelBufferDrainer::onConnect ( const jalib::JSocket& sock, const struct sockaddr* remoteAddr,socklen_t remoteLen )
{
  JWARNING ( false ) ( sock.sockfd() ).Text ( "we don't yet support checkpointing non-accepted connections... restore will likely fail.. closing connection" );
  jalib::JSocket ( sock ).close();
}

void dmtcp::KernelBufferDrainer::onData ( jalib::JReaderInterface* sock )
{
  dmtcp::vector<char>& buffer = _drainedData[sock->socket().sockfd() ];
  buffer.resize ( buffer.size() + sock->bytesRead() );
  int startIdx = buffer.size() - sock->bytesRead();
  memcpy ( &buffer[startIdx],sock->buffer(),sock->bytesRead() );
//     JTRACE("got buffer chunk")(sock->bytesRead());
  sock->reset();
}
void dmtcp::KernelBufferDrainer::onDisconnect ( jalib::JReaderInterface* sock )
{
  int fd;
  errno = 0;
  fd = sock->socket().sockfd();
  //check if this was on purpose
  if ( fd < 0 ) return;
  JTRACE ( "found disconnected socket... marking it dead" )
      (fd)(_reverseLookup[fd])( JASSERT_ERRNO );
  _disconnectedSockets.push_back(_reverseLookup[fd]);
  _drainedData.erase ( fd );
}
void dmtcp::KernelBufferDrainer::onTimeoutInterval()
{
  int count = 0;
  for ( size_t i = 0; i < _dataSockets.size();++i )
  {
    if ( _dataSockets[i]->bytesRead() > 0 ) onData ( _dataSockets[i] );
    dmtcp::vector<char>& buffer = _drainedData[_dataSockets[i]->socket().sockfd() ];
    if ( buffer.size() >= sizeof ( theMagicDrainCookie )
	 && memcmp ( &buffer[buffer.size() - sizeof ( theMagicDrainCookie ) ]
                     , theMagicDrainCookie
                     , sizeof ( theMagicDrainCookie ) ) == 0 )
    {
      buffer.resize ( buffer.size() - sizeof ( theMagicDrainCookie ) );
      JTRACE ( "buffer drain complete" ) ( _dataSockets[i]->socket().sockfd() ) ( buffer.size() ) ( ( _dataSockets.size() ) );
      _dataSockets[i]->socket() = -1; //poison socket
    }
    else
      ++count;
  }

  if ( count == 0 )
  {
    _listenSockets.clear();
  }else{
    const static int WARN_INTERVAL_TICKS = (int)(DRAINER_WARNING_FREQ/DRAINER_CHECK_FREQ + 0.5);
    const static float WARN_INTERVAL_SEC = WARN_INTERVAL_TICKS*DRAINER_CHECK_FREQ;
    if(_timeoutCount++ > WARN_INTERVAL_TICKS){
      _timeoutCount=0;
      for ( size_t i = 0; i < _dataSockets.size();++i ){
        dmtcp::vector<char>& buffer = _drainedData[_dataSockets[i]->socket().sockfd() ];
        JWARNING(false)(_dataSockets[i]->socket().sockfd())(buffer.size())(WARN_INTERVAL_SEC)
                 .Text("Still draining socket... perhaps remote host is not running under DMTCP?");
#ifdef CERN_CMS
        JNOTE("\n*** Closing this socket (to database?? ).  Please use dmtcpaware to\n"
              "***  gracefully handle database connections, and re-run.\n"
              "***  Trying a workaround for now, and hoping it doesn't fail.\n");
        _real_close(_dataSockets[i]->socket().sockfd());
	//it does it by creating a socket pair and closing one side
	int sp[2] = {-1,-1};
	JASSERT ( _real_socketpair ( AF_UNIX, SOCK_STREAM, 0, sp ) == 0 ) ( JASSERT_ERRNO )
		.Text ( "socketpair() failed" );
	JASSERT ( sp[0]>=0 && sp[1]>=0 ) ( sp[0] ) ( sp[1] )
		.Text ( "socketpair() failed" );
	_real_close ( sp[1] );
	JTRACE ( "created dead socket" ) ( sp[0] );
	_real_dup2(sp[0], _dataSockets[i]->socket().sockfd());
#endif

      }
    }
  }
}

// void dmtcp::KernelBufferDrainer::drainAllSockets()
// {
//     scaleSendBuffers(2);
/*
    SocketTable& table = SocketTable::instance();
    for(  SocketTable::iterator i = table.begin()
        ; i != table.end()
        ; ++i)
    {
        switch(i->state())
        {
            case SocketEntry::T_LISTEN:
//                 addListenSocket( i->sockfd() );
                break;
            case SocketEntry::T_CONNECT:
            case SocketEntry::T_ACCEPT:
                if(i->isStillAlive())
                {
                    JTRACE("will drain socket")(i->sockfd())(i->remoteId().id);
                    _drainedData[i->sockfd()]; // create buffer
                    jalib::JSocket(i->sockfd()) << theMagicDrainCookie;
                    addDataSocket( new jalib::JChunkReader(i->sockfd(),512));
                }
                else
                {
                    JTRACE("FOUND DEAD SOCKET")(i->sockfd());
                    i->setState(SocketEntry::T_ERROR);
                }
                break;
        }
    }
    monitorSockets( DRAINER_CHECK_FREQ );
  */
//     scaleSendBuffers(0.5);
// }

void dmtcp::KernelBufferDrainer::beginDrainOf ( int fd, const ConnectionIdentifier& id )
{
//     JTRACE("will drain socket")(fd);
  _drainedData[fd]; // create buffer
// this is the simple way:  jalib::JSocket(fd) << theMagicDrainCookie;
  //instead used delayed write in case kernel buffer is full:
  addWrite ( new jalib::JChunkWriter ( fd, theMagicDrainCookie, sizeof theMagicDrainCookie ) );
  //now setup a reader:
  addDataSocket ( new jalib::JChunkReader ( fd,512 ) );

  //insert it in reverse lookup
  _reverseLookup[fd]=id;
}


void dmtcp::KernelBufferDrainer::refillAllSockets()
{
  scaleSendBuffers ( 2 );

  JTRACE ( "refilling socket buffers" ) ( _drainedData.size() );

  //write all buffers out
  for ( dmtcp::map<int , dmtcp::vector<char> >::iterator i = _drainedData.begin()
          ;i != _drainedData.end()
          ;++i )
  {
    int size = i->second.size();
    JWARNING ( size>=0 ) ( size ).Text ( "a failed drain is in our table???" );
    if ( size<0 ) size=0;
    DmtcpMessage msg;
    msg.type = DMT_PEER_ECHO;
    msg.params[0] = size;
    jalib::JSocket sock ( i->first );
    if ( size>0 ) {
      JTRACE ( "requesting repeat buffer..." ) ( sock.sockfd() ) ( size );
    }
    sock << msg;
    if ( size>0 ) sock.writeAll ( &i->second[0],size );
    i->second.clear();
  }

//     JTRACE("repeating our friends buffers...");

  //read all buffers in
  for ( dmtcp::map<int , dmtcp::vector<char> >::iterator i = _drainedData.begin()
          ;i != _drainedData.end()
          ;++i )
  {
    DmtcpMessage msg;
    msg.poison();
    jalib::JSocket sock ( i->first );
    sock >> msg;

    msg.assertValid();
    JASSERT ( msg.type == DMT_PEER_ECHO ) ( msg.type );
    int size = msg.params[0];
    JTRACE ( "repeating buffer back to peer" ) ( size );
    if ( size>0 )
    {
      //echo it back...
      jalib::JBuffer tmp ( size );
      sock.readAll ( tmp,size );
      sock.writeAll ( tmp,size );
    }
  }

  JTRACE ( "buffers refilled" );


  scaleSendBuffers ( 0.5 );
}
