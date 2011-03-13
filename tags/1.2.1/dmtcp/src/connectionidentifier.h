/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#ifndef DMTCPCONNECTIONIDENTIFIER_H
#define DMTCPCONNECTIONIDENTIFIER_H

#include "dmtcpalloc.h"
#include "uniquepid.h"
#include "../jalib/jalloc.h"
 
// #include <vector>
// #include <map>

namespace dmtcp
{

// class ConnectionIdentifiers;

  class ConnectionIdentifier
  {
//     friend class ConnectionIdentifiers;
    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif
      static ConnectionIdentifier Create();
      static ConnectionIdentifier Null();
      static ConnectionIdentifier Self();

      static void serialize ( jalib::JBinarySerializer& o );

      int conId() const;
      const UniquePid& pid() const;
//     void addFd(int fd);
//     void removeFd(int fd);
//     size_t fdCount() const;
//     void dup2AllFds(int sourceFd);
//     typedef dmtcp::vector<int>::iterator fditerator;
//     fditerator begin(){ return _fds.begin(); }
//     fditerator end(){ return _fds.end(); }
//     void updateAfterDup(int oldfd,int newfd);


      ConnectionIdentifier ( const UniquePid& pid = UniquePid(), int id = -1 );

      bool isNull() const { return _id < 0; }
    private:
      UniquePid _pid;
      int _id;
  };


  bool operator< ( const ConnectionIdentifier& a, const ConnectionIdentifier& b );
  bool operator== ( const ConnectionIdentifier& a, const ConnectionIdentifier& b );
  inline bool operator!= ( const ConnectionIdentifier& a, const ConnectionIdentifier& b )
  { return ! ( a == b ); }

// class ConnectionIdentifiers{
// public:
//     static ConnectionIdentifiers& Incoming();
//     static ConnectionIdentifiers& Outgoing();
//     ConnectionIdentifier& lookup( int id );
//     ConnectionIdentifier& create();
//     void removeFd( int fd );
//     void updateAfterDup(int oldfd,int newfd);
// protected:
//     ConnectionIdentifiers();
// private:
//     dmtcp::map< int, ConnectionIdentifier* > _table;
// };

}

namespace std
{
  inline dmtcp::ostream& operator<< ( dmtcp::ostream& o, const dmtcp::ConnectionIdentifier& i )
  {
    o << i.pid() << '(' << i.conId() << ')';
    return o;
  }
}

#endif
