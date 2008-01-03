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
#ifndef DMTCPCONNECTION_H
#define DMTCPCONNECTION_H

#include "connectionidentifier.h"
#include <vector>
#include <sys/types.h>
#include <sys/socket.h>
#include <map>
#include "jbuffer.h"
#include "jserialize.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


namespace dmtcp {

class KernelBufferDrainer;
class ConnectionRewirer;
class TcpConnection;
class KernelDeviceToConnection;
    
class Connection {
public:
    enum ConnectionType
    {
        INVALID = 0x0000,
        TCP =     0x1000,
        PIPE =    0x2000,
        PTS =     0x4000,
        FILE =    0x8000,
                
        TYPEMASK = TCP | PIPE | PTS | FILE
    };
    
    virtual ~Connection(){}
    
    int conType() const { return _type & TYPEMASK; }
    
    const ConnectionIdentifier& id() const { return _id; }
    
    virtual void preCheckpoint(const std::vector<int>& fds, KernelBufferDrainer&) = 0;
    virtual void postCheckpoint(const std::vector<int>& fds) = 0;
    virtual void restore(const std::vector<int>&, ConnectionRewirer&) = 0;
    
    virtual void doLocking(const std::vector<int>& fds){};
    virtual void saveOptions(const std::vector<int>& fds);
    virtual void restoreOptions(const std::vector<int>& fds);
    
    //convert with type checking
    virtual TcpConnection& asTcp();
   
    
    void serialize(jalib::JBinarySerializer& o);
protected:
    virtual void serializeSubClass(jalib::JBinarySerializer& o) = 0;
protected:
    //only child classes can construct us...
    Connection(int t);
protected:
    ConnectionIdentifier _id;
    int                  _type;
    int                  _fcntlFlags;
    int                  _fcntlOwner;
    int                  _fcntlSignal;
};

class TcpConnection : public Connection
{
public:
    enum TcpType
    {
        TCP_INVALID = TCP,
        TCP_ERROR,
        TCP_CREATED,
        TCP_BIND,
        TCP_LISTEN,
        TCP_ACCEPT,
        TCP_CONNECT,
	TCP_PREEXISTING
    };
    
    int tcpType() const { return _type; }
    
    //basic commands for updating state as a from wrappers
    /*onSocket*/ TcpConnection(int domain, int type, int protocol);
    void onBind(const struct sockaddr* addr, socklen_t len);
    void onListen(int backlog);
    void onConnect(); // connect side does not know remote host
    /*onAccept*/ TcpConnection(const TcpConnection& parent, const ConnectionIdentifier& remote);
    void onError();
    void addSetsockopt(int level, int option, const char* value, int len);
    
    void markPreExisting() { _type = TCP_PREEXISTING; }
    
    //basic checkpointing commands
    virtual void preCheckpoint(const std::vector<int>& fds
                            , KernelBufferDrainer& drain);
    virtual void postCheckpoint(const std::vector<int>& fds);
    virtual void restore(const std::vector<int>&, ConnectionRewirer&);
    
    virtual void doLocking(const std::vector<int>& fds);
    
    virtual void restoreOptions(const std::vector<int>& fds);
    
private:
    virtual void serializeSubClass(jalib::JBinarySerializer& o);
    TcpConnection& asTcp();
private:
    int                     _sockDomain;
    int                     _sockType;
    int                     _sockProtocol;
    int                     _listenBacklog;
    socklen_t               _bindAddrlen;
    struct sockaddr_storage _bindAddr;
    ConnectionIdentifier    _acceptRemoteId;
    std::map< int, std::map< int, jalib::JBuffer > > _sockOptions; // _options[level][option] = value
};

// class PipeConnection : public Connection
// {
// public:
//     virtual void preCheckpoint(const std::vector<int>& fds
//                             , KernelBufferDrainer& drain);
//     virtual void postCheckpoint(const std::vector<int>& fds);
//     virtual void restore(const std::vector<int>&, ConnectionRewirer&);
//     
//     virtual void serializeSubClass(jalib::JBinarySerializer& o);
//     
// };

class PtsConnection : public Connection
{
public:
	enum PtsType
    {
        INVALID   = 0x0000,
        Pt_Master = 0x1000,
        Pt_Slave  = 0x2000,
                
        TYPEMASK = Pt_Master | Pt_Slave
    };
	
    PtsConnection(const std::string& device, const std::string& filename, PtsType type)
		: Connection(PTS)
		, _device(device)
		, _symlinkFilename(filename)
		, _type(type)
	{
		if ( filename.compare("?") == 0)
		{
			_type = INVALID;
		}
	   JTRACE("creating PtsConnection*****************************************")(id())(_device)(_symlinkFilename)(_type);
	}
    
    PtsConnection()
		: Connection(PTS)
		, _device("?")
		, _symlinkFilename("?")
		, _type(INVALID)
	{
	   JTRACE("creating PtsConnection*****************************************")(id())(_device)(_symlinkFilename)(_type);	
	}

	PtsType type() { return PtsType(_type & TYPEMASK); }
    virtual void preCheckpoint(const std::vector<int>& fds
                            , KernelBufferDrainer& drain);
    virtual void postCheckpoint(const std::vector<int>& fds);
    virtual void restore(const std::vector<int>&, ConnectionRewirer&);
    virtual void restoreOptions(const std::vector<int>& fds);
 
    virtual void serializeSubClass(jalib::JBinarySerializer& o);
private:
	PtsType 	_type;
	std::string _symlinkFilename;
	std::string _device;
			
};

class FileConnection : public Connection
{
public:
    inline FileConnection(const std::string& path, off_t offset)
		: Connection( FILE ), _path(path), _offset(offset) 
	{
	   JTRACE("creating FileConnection")(path)(offset);
	}
    
    virtual void preCheckpoint(const std::vector<int>& fds
                            , KernelBufferDrainer& drain);
    virtual void postCheckpoint(const std::vector<int>& fds);
    virtual void restore(const std::vector<int>&, ConnectionRewirer&);

    virtual void serializeSubClass(jalib::JBinarySerializer& o);
private:
    std::string _path;
    off_t       _offset;
    struct stat _stat;
};





}

#endif
