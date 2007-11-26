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
#include "connectionmanager.h"

#include "jfilesystem.h"
#include "jconvert.h"
#include "jassert.h"
#include "protectedfds.h"
#include "syscallwrappers.h"


#include <sys/types.h>
#include <unistd.h>
#include <errno.h>


static std::string _procFDPath(int fd) { 
    return "/proc/" + jalib::XToString(getpid()) + "/fd/" + jalib::XToString(fd); 
}

static bool _isBadFd(int fd)
{
    std::string device = jalib::Filesystem::ResolveSymlink( _procFDPath(fd) );
    return  (device == "");
}

dmtcp::ConnectionList& dmtcp::ConnectionList::Instance()
{
    static ConnectionList inst; return inst;
}

dmtcp::KernelDeviceToConnection& dmtcp::KernelDeviceToConnection::Instance()
{
    static KernelDeviceToConnection inst; return inst;
}

dmtcp::ConnectionList::ConnectionList(){}

dmtcp::KernelDeviceToConnection::KernelDeviceToConnection(){}


dmtcp::ConnectionToFds::ConnectionToFds( KernelDeviceToConnection& source )
{
    std::vector<int> fds = jalib::Filesystem::ListOpenFds();
    for(size_t i=0; i<fds.size(); ++i)
    {
        if(_isBadFd(fds[i])) continue;
        if(ProtectedFDs::isProtected( fds[i] )) continue;
        Connection* con = &source.retrieve(fds[i]);
        _table[con->id()].push_back(fds[i]);
    }
}


dmtcp::Connection& dmtcp::KernelDeviceToConnection::retrieve(int fd)
{
    std::string device = fdToDevice( fd );
    JASSERT(device.length() > 0)(fd).Text("invalid fd");
    iterator i = _table.find(device);
    JASSERT(i != _table.end())(fd)(device)(_table.size()).Text("failed to find connection for fd");
    return ConnectionList::Instance()[i->second];
}
void dmtcp::KernelDeviceToConnection::create(int fd, Connection* c)
{
    ConnectionList::Instance().add(c);
    
    std::string device = fdToDevice( fd );
    
    JTRACE("device created")(fd)(device)(c->id());
    
    JASSERT(device.length() > 0)(fd).Text("invalid fd");
    iterator i = _table.find(device);
    JASSERT(i == _table.end())(fd)(device).Text("connection already exists");
    _table[device] = c->id();
}



std::string dmtcp::KernelDeviceToConnection::fdToDevice(int fd)
{
    //gather evidence
    std::string device = jalib::Filesystem::ResolveSymlink( _procFDPath(fd) );
    bool isBadFd =  (device == "");
    
    if(isBadFd)
    {
        JTRACE("bad fd (we expect one of these lines)")(fd);
        JASSERT(device == "")(fd)(_procFDPath(fd))(device)(JASSERT_ERRNO)
                .Text("expected badFd not to have a proc entry...");
        
        return "";
    }
        
    bool isFile  = (device[0] == '/');
    
    
    if(isFile)
    {
        std::string deviceName = "file["+jalib::XToString(fd)+"]:" + device;
        iterator i = _table.find(deviceName);
        if(i == _table.end())
        {
            JTRACE("creating file connection [on-demand]")(deviceName);
            Connection * c = new FileConnection(device);
            ConnectionList::Instance().add(c);
            _table[deviceName] = c->id();
            return deviceName;
        }
        else
        {
            return deviceName; 
        }
    }
    

    return device;

}


void dmtcp::ConnectionList::erase( iterator i )
{
    Connection * con = i->second;
    JTRACE("deleting stale connection...")(con->id());
    _connections.erase( i );
//     KernelDeviceToConnection::Instance().erase( con );
    delete con;
}
// void dmtcp::KernelDeviceToConnection::erase( Connection* con )
// {
//     for(iterator i = _table.begin(); i!=_table.end(); ++i)
//     {
//         if(i->second == con)
//         {
//             _table.erase(i);
//             return;
//         }
//     }
//     JWARNING(false)(con->id()).Text("failed to find connection in table to erase it");
// }


void dmtcp::KernelDeviceToConnection::dbgSpamFds()
{
    JASSERT_STDERR << "Listing FDs...\n";
    std::vector<int> fds = jalib::Filesystem::ListOpenFds();
    for(size_t i=0; i<fds.size(); ++i)
    {
        if(_isBadFd(fds[i])) continue;
//         if(ProtectedFDs::isProtected( fds[i] )) continue;
        std::string device = fdToDevice( fds[i] );
        bool exists = (_table.find(device) != _table.end());
        JASSERT_STDERR << fds[i] 
                << " -> "  << device
                << " inTable=" << exists << "\n";
    }
}


//fix things up post-restart (all or KernelDevices have changed)
dmtcp::KernelDeviceToConnection::KernelDeviceToConnection( const ConnectionToFds& source )
{
    JTRACE("reconstructing table...");
    for(ConnectionToFds::const_iterator i = source.begin()
       ; i!=source.end()
       ; ++i)
    {
        ConnectionIdentifier con = i->first;
        const std::vector<int>& fds = i->second;
        std::string device = fdToDevice( fds[0] );
        _table[device] = con;
        
        
#ifdef DEBUG
        //double check to make sure all fds have same device
        for(size_t i=1; i<fds.size(); ++i)
        {
             JASSERT(device == fdToDevice( fds[i] ))
                     (device)(fdToDevice( fds[i] ))(fds[i])(fds[0]);
        }
#endif 
        
    }
#ifdef DEBUG
    JTRACE("new fd table...");
    dbgSpamFds();
#endif
    
}

void dmtcp::ConnectionList::serialize(jalib::JBinarySerializer& o)
{
    JSERIALIZE_ASSERT_POINT("dmtcp::ConnectionList:");
    
    size_t numCons = _connections.size();
    o & numCons;
    
    if(o.isWriter())
    {
        for(iterator i=_connections.begin(); i!=_connections.end(); ++i)
        {
            
            ConnectionIdentifier key = i->first;
            Connection& con = *i->second;
            int type = con.conType();
            
            JSERIALIZE_ASSERT_POINT("[StartConnection]");
            o & key & type;
            con.serialize(o);
            JSERIALIZE_ASSERT_POINT("[EndConnection]");
        }
    }
    else
    {
        while(numCons-- > 0)
        {
            
            ConnectionIdentifier key;
            int type = -1;
            Connection* con = NULL;
            
            JSERIALIZE_ASSERT_POINT("[StartConnection]");
            o & key & type;
            
            switch(type)
            {
                case Connection::TCP:
                    con = new TcpConnection(-1,-1,-1);
                    break;
                case Connection::FILE:
                    con = new FileConnection("?");
                    break;
    //             case Connection::PIPE:
    //                 con = new PipeConnection();
    //                 break;
                case Connection::PTS:
                    con = new PtsConnection();
                    break;
                default:
                    JASSERT(false)(key)(o.filename()).Text("unknown connection type");
            }
            if(con != NULL)
            {
                con->serialize(o);
                _connections[key] = con;
            }
            
            JSERIALIZE_ASSERT_POINT("[EndConnection]");
        }
        
    }
    
    JSERIALIZE_ASSERT_POINT("EndConnectionList");
}

void dmtcp::ConnectionToFds::serialize(jalib::JBinarySerializer& o)
{
    JSERIALIZE_ASSERT_POINT("dmtcp-serialized-connection-table!v0.07");
    ConnectionList::Instance().serialize( o );
    JSERIALIZE_ASSERT_POINT("dmtcp::ConnectionToFds:");
    
    size_t numCons = _table.size();
    o & numCons;
    
    if(o.isWriter())
    {
        for(iterator i=_table.begin(); i!=_table.end(); ++i)
        {
            JSERIALIZE_ASSERT_POINT("CFdEntry:");
            ConnectionIdentifier key = i->first;
            std::vector<int>& val = i->second;
            o & key & val;
            JASSERT(val.size()>0)(o.filename()).Text("writing bad file format");
        }
    }
    else
    {
        while(numCons-- > 0)
        {
            JSERIALIZE_ASSERT_POINT("CFdEntry:");
            ConnectionIdentifier key;
            std::vector<int> val;
            o & key & val;
            JASSERT(val.size()>0)(o.filename()).Text("invalid file format");
            _table[key]=val;
        }
        
    }
    
    
    JSERIALIZE_ASSERT_POINT("EOF");
}

void dmtcp::KernelDeviceToConnection::serialize(jalib::JBinarySerializer& o)
{
    JSERIALIZE_ASSERT_POINT("dmtcp-serialized-exec-lifeboat!v0.07");
    ConnectionList::Instance().serialize( o );
    JSERIALIZE_ASSERT_POINT("dmtcp::KernelDeviceToConnection:");
    
    size_t numCons = _table.size();
    o & numCons;
    
    if(o.isWriter())
    {
        for(iterator i=_table.begin(); i!=_table.end(); ++i)
        {
            JSERIALIZE_ASSERT_POINT("KDEntry:");
            std::string key = i->first;
            ConnectionIdentifier val = i->second;
            o & key & val;
        }
    }
    else
    {
        while(numCons-- > 0)
        {
            JSERIALIZE_ASSERT_POINT("KDEntry:");
            std::string key = "?";
            ConnectionIdentifier val;
            o & key & val;
            _table[key] = val;
        }
        
    }
    
    JSERIALIZE_ASSERT_POINT("EOF");
}


dmtcp::Connection& dmtcp::ConnectionList::operator[] (const ConnectionIdentifier& id)
{
    JASSERT(_connections.find(id) != _connections.end())(id)
            .Text("Unknown connection");
    return *_connections[id];
}

void dmtcp::ConnectionList::add(Connection* c)
{
    JWARNING(_connections.find(c->id()) == _connections.end())(c->id())
            .Text("duplicate connection");
    _connections[c->id()] = c; 
}


int dmtcp::SlidingFdTable::getFdFor( const ConnectionIdentifier& con )
{
    //is our work already done?
    if( _conToFd.find(con) != _conToFd.end() )
        return _conToFd[con];
    
    //find a free fd
    int newfd;
    while(isInUse( newfd = _nextFd++ )){}
    
    //ad it
    _conToFd[con] = newfd;
    _fdToCon[newfd]  = con;
    
    JTRACE("allocated fd for connection")(newfd)(con);
    
    return newfd;
}
void dmtcp::SlidingFdTable::freeUpFd( int fd )
{
    //is that FD in use?
    if(_fdToCon.find(fd) == _fdToCon.end())
        return;
    
    ConnectionIdentifier con = _fdToCon[fd];
    
    if(con == ConnectionIdentifier::Null())
        return;
    
    //find a free fd
    int newfd;
    while(isInUse( newfd = _nextFd++ )){}
    
    JTRACE("sliding fd for connection")(fd)(newfd)(con);
    
    //do the change
    changeFd( fd, newfd );
    
    //update table
    _conToFd[con] = newfd;
    _fdToCon[newfd]  = con;
    _fdToCon[fd] = ConnectionIdentifier::Null();
}
bool dmtcp::SlidingFdTable::isInUse( int fd ) const
{
    if(_fdToCon.find(fd) != _fdToCon.end())
        return true;
    //double check with the filesystem
    std::string device = jalib::Filesystem::ResolveSymlink( _procFDPath(fd) );
    return device != "";
}
void dmtcp::SlidingFdTable::changeFd( int oldfd, int newfd )
{
    if(oldfd == newfd) return;
    JASSERT(_real_dup2(oldfd,newfd) == newfd)(oldfd)(newfd).Text("dup2() failed");
    JASSERT(_real_close(oldfd))(oldfd).Text("close() failed");
}
void dmtcp::SlidingFdTable::closeAll()
{
    for(std::map< ConnectionIdentifier, int >::iterator i=_conToFd.begin()
        ; i!=_conToFd.end()
        ; ++i)
    {
        JWARNING(_real_close(i->second)==0)(i->second);
    }
    _conToFd.clear();
}

