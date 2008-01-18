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
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include <iostream>
#include "jassert.h"

void doAccept ( int& acceptSock, int listenSock );
void doConnect ( int& connectSock, struct hostent* host, int port,const char* hostname );

int main ( int argc, char ** argv )
{
  JASSERT ( argc == 4 ) ( argc ).Text ( "usage: player listen-port connect-host connect-port" );

  int listenSock = socket ( AF_INET, SOCK_STREAM, 0 );
  int connectSock = socket ( AF_INET, SOCK_STREAM, 0 );
  int acceptSock = -1;
  JASSERT ( listenSock>0 && connectSock>0 ) ( listenSock ) ( connectSock );

  int listenPort = atoi ( argv[1] );
  hostent *connectHost = gethostbyname ( argv[2] );
  int connectPort = atoi ( argv[3] );
  JASSERT ( listenPort > 0 ) ( listenPort ) ( argv[1] ).Text ( "expected port" );
  JASSERT ( connectHost!=NULL ) ( argv[2] ).Text ( "unkown host" );
  JASSERT ( connectPort > 0 ) ( connectPort ) ( argv[3] ).Text ( "expected port" );

  //bind listen socket
  {
    sockaddr_in listenAddy;
    memset ( &listenAddy,0,sizeof ( listenAddy ) );
    listenAddy.sin_family = AF_INET;
    listenAddy.sin_addr.s_addr = INADDR_ANY;
    listenAddy.sin_port = htons ( listenPort );

    JASSERT ( bind ( listenSock, ( sockaddr * ) &listenAddy, sizeof ( listenAddy ) ) >=0 )
    ( listenPort ).Text ( "failed to bind socket" );

    JASSERT ( listen ( listenSock,5 ) >=0 );
  }

  bool isStarterNode = false;

  {
    std::cout << "starter node? [y/n] ";
    std::string c;
    std::cin >> c;
    if ( c[0]=='y' || c[0]=='Y' ) isStarterNode = true;
  }

  if ( !isStarterNode )
  {
    std::cout << "accepting..."  << std::endl;
    doAccept ( acceptSock, listenSock );
    std::cout << "connecting..."  << std::endl;
    doConnect ( connectSock, connectHost, connectPort, argv[2] );
  }
  else
  {
    std::cout << "connecting..."  << std::endl;
    doConnect ( connectSock, connectHost, connectPort, argv[2] );
    std::cout << "accepting..."  << std::endl;
    doAccept ( acceptSock, listenSock );
  }
//     /*
//     {
//         size_t tmp = 0;
//         socklen_t oplen = sizeof(tmp);
//         JASSERT(setsockopt(connectSock,SOL_SOCKET, SO_RCVBUF, &tmp, sizeof(tmp)) == 0);
//         JASSERT(setsockopt(connectSock,SOL_SOCKET, SO_SNDBUF, &tmp, sizeof(tmp)) == 0);
//         JASSERT(setsockopt(acceptSock, SOL_SOCKET, SO_RCVBUF, &tmp, sizeof(tmp)) == 0);
//         JASSERT(setsockopt(acceptSock, SOL_SOCKET, SO_SNDBUF, &tmp, sizeof(tmp)) == 0);
//
//         JASSERT(getsockopt(connectSock,SOL_SOCKET, SO_RCVBUF, &tmp, &oplen) == 0);
//         JNOTE("sockopt")(tmp);
//         JASSERT(getsockopt(connectSock,SOL_SOCKET, SO_SNDBUF, &tmp, &oplen) == 0);
//         JNOTE("sockopt")(tmp);
//         JASSERT(getsockopt(acceptSock, SOL_SOCKET, SO_RCVBUF, &tmp, &oplen) == 0);
//         JNOTE("sockopt")(tmp);
//         JASSERT(getsockopt(acceptSock, SOL_SOCKET, SO_SNDBUF, &tmp, &oplen) == 0);
//         JNOTE("sockopt")(tmp);
//
//     }*/
  std::cout << "ready" << std::endl;

  for ( ;; )
  {
    std::cout << "throw [a-z] or catch [C]?";
    std::cout.flush();
    std::string c = " ";
    std::cin >> c;
    if ( c[0] >= 'a' && c[0] <= 'z' )
    {
      std::cout << "throwing a '" << c[0] << "' to next player..." << std::endl;
      JASSERT ( write ( connectSock,&c[0],1 ) ==1 ) ( c );
      fsync ( connectSock );
      std::cout << "throw complete." << std::endl;
    }
//         else if(c[0] == 'S')
//         {
//             std::cout << "shrinking buffers..." << std::endl;
//             size_t tmp = 0;
//             socklen_t oplen = sizeof(tmp);
//             JASSERT(setsockopt(connectSock,SOL_SOCKET, SO_RCVBUF, &tmp, sizeof(tmp)) == 0);
//             JASSERT(setsockopt(connectSock,SOL_SOCKET, SO_SNDBUF, &tmp, sizeof(tmp)) == 0);
//             JASSERT(setsockopt(acceptSock, SOL_SOCKET, SO_RCVBUF, &tmp, sizeof(tmp)) == 0);
//             JASSERT(setsockopt(acceptSock, SOL_SOCKET, SO_SNDBUF, &tmp, sizeof(tmp)) == 0);
//             std::cout << "shrunk" << std::endl;
//
//         }
    else
    {
      std::cout << "catching from previos player..." << std::endl;
      JASSERT ( read ( acceptSock,&c[0],1 ) ==1 );
      std::cout << "caught a '" << c[0] << "'." << std::endl;
    }
  }

  return 0;
}

void doAccept ( int& acceptSock, int listenSock )
{
  JASSERT ( ( acceptSock = accept ( listenSock,NULL,NULL ) ) >0 ).Text ( "accept() failed" );
}
void doConnect ( int& connectSock, hostent* host, int port,const char* hostname )
{
  sockaddr_in addr;
  memset ( &addr,0,sizeof ( addr ) );
  addr.sin_family = AF_INET;
  memcpy ( &addr.sin_addr.s_addr, host->h_addr, host->h_length );
  addr.sin_port = htons ( port );
  JASSERT ( connect ( connectSock, ( sockaddr* ) &addr,sizeof ( addr ) ) >=0 ) ( hostname ) ( port ).Text ( "connect failed" );
}

