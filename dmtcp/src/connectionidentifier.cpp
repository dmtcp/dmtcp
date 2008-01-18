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
#include "connectionidentifier.h"
#include "constants.h"
#include "syscallwrappers.h"
#include "jassert.h"

static int _nextConnectionId()
{
  static int id = CONNECTION_ID_START;
  return id++;
}

dmtcp::ConnectionIdentifier::ConnectionIdentifier ( const UniquePid& pid, int id )
    : _pid ( pid ) , _id ( id )
{}

dmtcp::ConnectionIdentifier dmtcp::ConnectionIdentifier::Create()
{
  return ConnectionIdentifier ( UniquePid::ThisProcess(),_nextConnectionId() );
}
dmtcp::ConnectionIdentifier dmtcp::ConnectionIdentifier::Null()
{
  static dmtcp::ConnectionIdentifier n;
  return n;
}
dmtcp::ConnectionIdentifier dmtcp::ConnectionIdentifier::Self()
{
  return ConnectionIdentifier ( UniquePid::ThisProcess(),-1 );
}

int dmtcp::ConnectionIdentifier::conId() const { return _id; }

const dmtcp::UniquePid& dmtcp::ConnectionIdentifier::pid() const { return _pid; }


bool dmtcp::operator< ( const ConnectionIdentifier& a, const ConnectionIdentifier& b )
{
  if ( a.pid() != b.pid() ) return a.pid() < b.pid();
  return a.conId() < b.conId();
}

bool dmtcp::operator== ( const ConnectionIdentifier& a, const ConnectionIdentifier& b )
{
  return  a.pid() == b.pid()
          && a.conId()  == b.conId();
}

// void dmtcp::ConnectionIdentifier::addFd(int fd) { _fds.push_back(fd); }
//
// void dmtcp::ConnectionIdentifier::removeFd(int fd)
// {
//     for(size_t i=0; i<_fds.size(); ++i)
//     {
//         if(_fds[i] == fd)
//         {
//             JTRACE("removing fd")(fd)(i);
//             _fds[i] = _fds.back();
//             _fds.pop_back();
//         }
//     }
// }
//
// size_t dmtcp::ConnectionIdentifier::fdCount() const { return _fds.size(); }
//
// // void dmtcp::ConnectionIdentifier::dup2AllFds(int sourceFd)
// // {
// //     for(size_t i=0; i<_fds.size(); ++i)
// //     {
// //         JTRACE("duping...")(sourceFd)(_fds[i]);
// //         JASSERT(_fds[i] == _real_dup2(sourceFd,_fds[i]))(_fds[i])(sourceFd)
// //                 .Text("dup2() failed");
// //     }
// // }
//
//
// dmtcp::ConnectionIdentifiers::ConnectionIdentifiers() {}
//
// dmtcp::ConnectionIdentifiers& dmtcp::ConnectionIdentifiers::Incoming()
// {
//     static dmtcp::ConnectionIdentifiers instance;
//     return instance;
// }
//
// dmtcp::ConnectionIdentifiers& dmtcp::ConnectionIdentifiers::Outgoing()
// {
//     static dmtcp::ConnectionIdentifiers instance;
//     return instance;
// }
//
// dmtcp::ConnectionIdentifier& dmtcp::ConnectionIdentifiers::lookup( int id )
// {
//     std::map< int, ConnectionIdentifier* >::iterator i = _table.find(id);
//     JASSERT(i != _table.end())(id).Text("ConnectionIdentifer does not exist");
//     return *i->second;
// }
//
// dmtcp::ConnectionIdentifier& dmtcp::ConnectionIdentifiers::create()
// {
//     ConnectionIdentifier* item = new ConnectionIdentifier();
//     _table[item->id()] = item;
//     return * item;
// }
//
// void dmtcp::ConnectionIdentifiers::removeFd( int fd )
// {
//     std::map< int, ConnectionIdentifier* >::iterator i;
//     for(i=_table.begin(); i!=_table.end(); ++i)
//     {
//         i->second->removeFd( fd );
//         //todo: fix this delete code so it doesn't crash
// //         if(i->second->fdCount() == 0)
// //         {
// //             //delete item
// //             delete i->second;
// //             i->second = 0;
// //             _table.erase(i);
// //             //our iterator is now invalid... lets start over
// //             i = _table.begin();
// //         }
//     }
// }
//
// void dmtcp::ConnectionIdentifiers::updateAfterDup(int oldfd,int newfd)
// {
//     std::map< int, ConnectionIdentifier* >::iterator i;
//     for(i=_table.begin(); i!=_table.end(); ++i)
//     {
//         i->second->updateAfterDup(oldfd,newfd);
//     }
// }
//
// void dmtcp::ConnectionIdentifier::updateAfterDup(int oldfd,int newfd)
// {
//     for(size_t i=0; i<_fds.size(); ++i)
//     {
//         if(_fds[i] == oldfd)
//         {
//             JTRACE("updating after dup")(oldfd)(newfd)(_id);
//             _fds.push_back(newfd);
//         }
//     }
// }
