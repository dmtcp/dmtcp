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
#ifndef DMTCPCONNECTIONIDENTIFIER_H
#define DMTCPCONNECTIONIDENTIFIER_H

#include "uniquepid.h"

// #include <vector>
// #include <map>

namespace dmtcp {

// class ConnectionIdentifiers;

class ConnectionIdentifier{
//     friend class ConnectionIdentifiers;
public:
    static ConnectionIdentifier Create();
    static ConnectionIdentifier Null(); 
    static ConnectionIdentifier Self(); 
    
    
    
    
    int conId() const;
    const UniquePid& pid() const;
//     void addFd(int fd);
//     void removeFd(int fd);
//     size_t fdCount() const;
//     void dup2AllFds(int sourceFd);
//     typedef std::vector<int>::iterator fditerator;
//     fditerator begin(){ return _fds.begin(); }
//     fditerator end(){ return _fds.end(); }
//     void updateAfterDup(int oldfd,int newfd);

    
    ConnectionIdentifier(const UniquePid& pid = UniquePid(), int id = -1);
    
private:
    UniquePid _pid;
    int _id;
};


bool operator< (const ConnectionIdentifier& a, const ConnectionIdentifier& b);
bool operator== (const ConnectionIdentifier& a, const ConnectionIdentifier& b);
inline bool operator!= (const ConnectionIdentifier& a, const ConnectionIdentifier& b)
{ return ! (a == b); }

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
//     std::map< int, ConnectionIdentifier* > _table;
// };

}

namespace std{ 
    inline std::ostream& operator<< (std::ostream& o, const dmtcp::ConnectionIdentifier& i){
    o << i.pid() << '(' << i.conId() << ')';
    return o;
}}

#endif
