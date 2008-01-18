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
#include "kernelbufferdrainer.h"

#include "jassert.h"
#include "constants.h"
#include "sockettable.h"
#include "jbuffer.h"
#include "connectionmanager.h"

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
  JWARNING ( false ) ( sock.sockfd() ).Text ( "we dont yet support checkpointing non-accepted connections... restore will likely fail.. closing connection" );
  jalib::JSocket ( sock ).close();
}

void dmtcp::KernelBufferDrainer::onData ( jalib::JReaderInterface* sock )
{
  std::vector<char>& buffer = _drainedData[sock->socket().sockfd() ];
  buffer.resize ( buffer.size() + sock->bytesRead() );
  int startIdx = buffer.size() - sock->bytesRead();
  memcpy ( &buffer[startIdx],sock->buffer(),sock->bytesRead() );
//     JTRACE("got buffer chunk")(sock->bytesRead());
  sock->reset();
}
void dmtcp::KernelBufferDrainer::onDisconnect ( jalib::JReaderInterface* sock )
{
  int fd = sock->socket().sockfd();
  //check if this was on purpose
  if ( fd < 0 ) return;
  JTRACE ( "found disconnected socket... marking it dead" ) ( fd ) ( JASSERT_ERRNO );
  KernelDeviceToConnection::Instance().retrieve ( fd ).asTcp().onError();
  _drainedData.erase ( fd );
}
void dmtcp::KernelBufferDrainer::onTimeoutInterval()
{
  int count = 0;
  for ( size_t i = 0; i < _dataSockets.size();++i )
  {
    if ( _dataSockets[i]->bytesRead() > 0 ) onData ( _dataSockets[i] );
    std::vector<char>& buffer = _drainedData[_dataSockets[i]->socket().sockfd() ];
    if ( memcmp ( &buffer[buffer.size() - sizeof ( theMagicDrainCookie ) ]
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
  }
}

// void dmtcp::KernelBufferDrainer::drainAllSockets()
// {
//     scaleSendBuffers(2);
/*
    SocketTable& table = SocketTable::Instance();
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

void dmtcp::KernelBufferDrainer::beginDrainOf ( int fd )
{
//     JTRACE("will drain socket")(fd);
  _drainedData[fd]; // create buffer
// this is the simple way:  jalib::JSocket(fd) << theMagicDrainCookie;
  //instead used delayed write incase kernel buffer is full:
  addWrite ( new jalib::JChunkWriter ( fd, theMagicDrainCookie, sizeof theMagicDrainCookie ) );
  //now setup a reader:
  addDataSocket ( new jalib::JChunkReader ( fd,512 ) );
}


void dmtcp::KernelBufferDrainer::refillAllSockets()
{
  scaleSendBuffers ( 2 );

  JTRACE ( "refilling socket buffers" ) ( _drainedData.size() );

  //write all buffers out
  for ( std::map<int , std::vector<char> >::iterator i = _drainedData.begin()
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
    if ( size>0 ) JTRACE ( "requesting repeat buffer..." ) ( sock.sockfd() ) ( size );
    sock << msg;
    if ( size>0 ) sock.writeAll ( &i->second[0],size );
    i->second.clear();
  }

//     JTRACE("repeating our friends buffers...");

  //read all buffers in
  for ( std::map<int , std::vector<char> >::iterator i = _drainedData.begin()
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
