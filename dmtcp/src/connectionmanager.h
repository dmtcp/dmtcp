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
#ifndef CONNECTIONMANAGER_H
#define CONNECTIONMANAGER_H

#include <connection.h>
#include <list>
#include <map>
#include <string>
#include "jserialize.h"
#include "jfilesystem.h"


namespace dmtcp
{

  class KernelDeviceToConnection;
  class ConnectionToFds;
  class ConnectionState;
  
  class ConnectionList
  {
      friend class KernelDeviceToConnection;
    public:
      typedef std::map<ConnectionIdentifier, Connection*>::iterator iterator;
      iterator begin() { return _connections.begin(); }
      iterator end() { return _connections.end(); }
      static ConnectionList& Instance();
      void erase ( iterator i );
      ConnectionList();
      Connection& operator[] ( const ConnectionIdentifier& id );

      void serialize ( jalib::JBinarySerializer& o );

      //examine /proc/self/fd for unknown connections
      void scanForPreExisting();
    protected:
      void add ( Connection* c );
    private:
      typedef  std::map<ConnectionIdentifier, Connection*> ConnectionMapT;
      ConnectionMapT _connections;
  };


  class KernelDeviceToConnection
  {
    public:


      static KernelDeviceToConnection& Instance();
      Connection& retrieve ( int fd );
      void        create ( int fd, Connection* c );

      void erase(const ConnectionIdentifier&);

      std::string fdToDevice ( int fd , bool noOnDemandPts = false );

      void dbgSpamFds();

      //fix things up post-restart (all or KernelDevices have changed)
      KernelDeviceToConnection ( const ConnectionToFds& source );


      void serialize ( jalib::JBinarySerializer& o );

      KernelDeviceToConnection();

      void handlePreExistingFd ( int fd );
      
      //called when a device name changes
      void redirect( int fd, const ConnectionIdentifier& id );
    protected:


    private:
      typedef std::map< std::string , ConnectionIdentifier >::iterator iterator;
      std::map< std::string , ConnectionIdentifier > _table;
  };


  class ConnectionToFds
  {
    public:
      ConnectionToFds() { 
        _procname = jalib::Filesystem::GetProgramName(); 
        _hostname = jalib::Filesystem::GetCurrentHostname();
        _inhostname = jalib::Filesystem::GetCurrentHostname();
        _pid = UniquePid::ThisProcess();
				_ppid = UniquePid::ParentProcess();
      }
      ConnectionToFds ( KernelDeviceToConnection& source );
      std::vector<int>& operator[] ( const ConnectionIdentifier& c ) { return _table[c]; }
    
      typedef std::map< ConnectionIdentifier, std::vector<int> >::iterator iterator;
      iterator begin() { return _table.begin(); }
      iterator end() { return _table.end(); }
      typedef std::map< ConnectionIdentifier, std::vector<int> >::const_iterator const_iterator;
      const_iterator begin() const { return _table.begin(); }
      const_iterator end() const { return _table.end(); }
    
      size_t size() const { return _table.size(); }

      void serialize ( jalib::JBinarySerializer& o );
    
      std::string procname() const { return _procname; }
      std::string hostname() const { return _hostname; }
      std::string inhostname() const { return _inhostname; }
      const UniquePid &pid() const { return _pid; }
      const UniquePid &ppid() const { return _ppid; }

    private:
      std::map< ConnectionIdentifier, std::vector<int> > _table;
      std::string _procname;
      std::string _hostname;
      std::string _inhostname;
      UniquePid _pid,_ppid;
  };


  ///
  /// Another mapping from Connection to FD
  /// This time to temporarying holding FD's which must be slid around as each FD is put into use
  class SlidingFdTable
  {
    public:
      SlidingFdTable ( int startingFd = 500 )
        : _nextFd ( startingFd )
        , _startFd ( startingFd )
      {}

      int startFd() { return _startFd; }

      ///
      /// retrieve, and if needed assign an FD for id
      int getFdFor ( const ConnectionIdentifier& id );

      ///
      /// if the given FD is in use... reassign it to another FD
      void freeUpFd ( int fd );

      bool isInUse ( int fd ) const;

      static void changeFd ( int oldfd, int newfd );

      void closeAll();
    private:
      std::map< ConnectionIdentifier, int > _conToFd;
      std::map< int, ConnectionIdentifier > _fdToCon;
      int _nextFd;
      int _startFd;
  };

  ///
  /// Mapping from pts device to symlink file in /tmp
  ///
  class PtsToSymlink
  {
    public:
      static PtsToSymlink& Instance();
      typedef std::map<std::string, std::string>::iterator iterator;
      void replace ( std::string oldDevice, std::string newDevice );
      PtsToSymlink();

      //void serialize(jalib::JBinarySerializer& o);

      void add ( std::string device, std::string filename );
      std::string getFilename ( std::string device );
      bool isDuplicate(std::string);
  
    private:
      std::map<std::string, std::string> _table;
  };

}

#endif
