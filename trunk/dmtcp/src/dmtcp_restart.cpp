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

#include <unistd.h>
#include <stdlib.h>
#include <string>
#include <stdio.h>
#include  "../jalib/jassert.h"
#include  "../jalib/jfilesystem.h"
#include "connectionmanager.h"
#include "dmtcpworker.h"
#include "dmtcpmessagetypes.h"
#include "connectionstate.h"
#include "mtcpinterface.h"
#include "syscallwrappers.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


static void runMtcpRestore ( const char* path );

using namespace dmtcp;

namespace
{

  class RestoreTarget
  {
    public:
      RestoreTarget ( const std::string& path )
          : _path ( path )
      {

        JASSERT ( jalib::Filesystem::FileExists ( _path ) ) ( _path ).Text ( "checkpoint file missing" );
        _conToFd.loadFromFile(_path);
        JTRACE ( "restore target" ) ( _path ) ( _conToFd.size() );
      }


      void dupAllSockets ( SlidingFdTable& slidingFd )
      {
        int lastfd = -1;
        std::vector<int> fdlist;
        for ( ConnectionToFds::const_iterator i = _conToFd.begin(); i!=_conToFd.end(); ++i )
        {
          Connection& con = ConnectionList::Instance() [i->first];
          if ( con.conType() == Connection::INVALID ){
            JWARNING(false)(i->first).Text("Can't restore invalid Connection");
            continue;
          }

          const std::vector<int>& fds = i->second;
          for ( size_t x=0; x<fds.size(); ++x )
          {
            int fd = fds[x];
            fdlist.push_back ( fd );
            slidingFd.freeUpFd ( fd );
            int oldFd = slidingFd.getFdFor ( i->first );
            JTRACE ( "restoring fd" ) ( i->first ) ( oldFd ) ( fd );
            //let connection do custom dup2 handling
            con.restartDup2( oldFd, fd );

            if ( fd > lastfd )
            {
              lastfd = fd;
            }
          }
        }

        size_t j;
        for ( int i = 0 ; i < slidingFd.startFd() ; i++ )
        {
          for ( j = 0 ; j < fdlist.size() ; j++ )
          {
            if ( fdlist.at ( j ) == i )
              break;
          }
          if ( j == fdlist.size() )
          {
            _real_close ( i );
          }
        }

        slidingFd.closeAll();
      }
      /*      else if(ConnectionList::Instance()[i->first].conType() == Connection::PTS)
            {
              const std::vector<int>& fds = i->second;
              for(size_t x=0; x<fds.size(); ++x)
              {
                int fd = fds[x];
                slidingFd.freeUpFd( fd );
                int oldFd = slidingFd.getFdFor( i->first );
                JTRACE("restoring fd")(i->first)(oldFd)(fd);
		errno = 0;
                JWARNING(_real_dup2(oldFd, fd) == fd)(oldFd)(fd)(JASSERT_ERRNO);
                //_real_dup2(oldFd, fd);
              }
            }
            else if(ConnectionList::Instance()[i->first].conType() == Connection::FILE)
            {
              const std::vector<int>& fds = i->second;
              for(size_t x=0; x<fds.size(); ++x)
              {
                int fd = fds[x];
                slidingFd.freeUpFd( fd );
                int oldFd = slidingFd.getFdFor( i->first );
                JTRACE("restoring fd")(i->first)(oldFd)(fd);
		errno = 0;
                JWARNING(_real_dup2(oldFd, fd) == fd)(oldFd)(fd)(JASSERT_ERRNO);
                //_real_dup2(oldFd, fd);
              }
            }
              }
       */

      void mtcpRestart()
      {
        DmtcpWorker::maskStdErr();
        runMtcpRestore ( _path.c_str() );
      }

      const UniquePid& pid() const { return _conToFd.pid(); }
      const std::string& procname() const { return _conToFd.procname(); }

      std::string     _path;
      ConnectionToFds _conToFd;
  };


}//namespace

static const char* theUsage =
  "USAGE:\n dmtcp_restart [OPTIONS] <ckpt1.dmtcp> [ckpt2.dmtcp...]\n\n"
  "OPTIONS:\n"
  "  --host, -h, (environment variable DMTCP_HOST):\n"
  "      Hostname where dmtcp_coordinator is run (default: localhost)\n"
  "  --port, -p, (environment variable DMTCP_PORT):\n"
  "      Port where dmtcp_coordinator is run (default: 7779)\n"
  "  --join, -j:\n"
  "      Join an existing coordinator, do not create one automatically\n"
  "  --new, -n:\n"
  "      Create a new coordinator, raise error if one already exists\n"
  "  --no-check:\n"
  "      Skip check for valid coordinator and never start one automatically\n\n"
  "See http://dmtcp.sf.net/ for more information.\n"
;

//shift args
#define shift argc--,argv++

int main ( int argc, char** argv )
{
  bool autoStartCoordinator=true;
  int allowedModes = dmtcp::DmtcpWorker::COORD_ANY;

  //process args
  shift;
  while(true){
    std::string s = argc>0 ? argv[0] : "--help";
    if(s=="--help"){
      fprintf(stderr, theUsage);
      return 1;
    }else if(s == "--no-check"){
      autoStartCoordinator = false;
      shift;
    }else if(s == "-j" || s == "--join"){
      allowedModes = dmtcp::DmtcpWorker::COORD_JOIN;
      shift;
    }else if(s == "-n" || s == "--new"){
      allowedModes = dmtcp::DmtcpWorker::COORD_NEW;
      shift;
    }else if(argc>1 && (s == "-h" || s == "--host")){
      setenv(ENV_VAR_NAME_ADDR, argv[1], 1);
      shift; shift;
    }else if(argc>1 && (s == "-p" || s == "--port")){
      setenv(ENV_VAR_NAME_PORT, argv[1], 1);
      shift; shift;
    }else if(argc>1 && s=="--"){
      shift;
      break;
    }else{
      break;
    }
  }

  if(autoStartCoordinator) dmtcp::DmtcpWorker::startCoordinatorIfNeeded(allowedModes);

  //make sure JASSERT initializes now, rather than during restart
  JASSERT_INIT();

  std::vector<RestoreTarget> targets;

  for(; argc>0; shift){
    targets.push_back ( RestoreTarget ( argv[0] ) );
  }

  JASSERT(targets.size()>0);

  SlidingFdTable slidingFd;
  ConnectionToFds conToFd;

  ConnectionList& connections = ConnectionList::Instance();
  for ( ConnectionList::iterator i = connections.begin()
                                     ; i!= connections.end()
          ; ++i )
  {
    conToFd[i->first].push_back ( slidingFd.getFdFor ( i->first ) );
    JTRACE ( "will restore" ) ( i->first ) ( conToFd[i->first].back() );
  }

  DmtcpWorker worker ( false );
  ConnectionState ckptCoord ( conToFd );

  worker.restoreSockets ( ckptCoord );

  int i = (int)targets.size();

  //fork into targs.size() processes
  while(--i > 0){
    int cid = fork();
    if(cid==0) break;
    else JASSERT(cid>0);
  }
  RestoreTarget& targ = targets[i];

  JTRACE("forked, restoring process")(i)(targets.size())(targ.pid())(getpid());

  //change UniquePid
  UniquePid::resetOnFork(targ.pid());

  //Reconnect to dmtcp_coordinator
  WorkerState::setCurrentState ( WorkerState::RESTARTING );
  worker.connectToCoordinator(false);
  worker.sendCoordinatorHandshake(targ.procname());

  //restart targets[i]
  targets[i].dupAllSockets ( slidingFd );
  targets[i].mtcpRestart();

  JASSERT ( false ).Text ( "unreachable" );
  return -1;
}

static void runMtcpRestore ( const char* path )
{
  static std::string mtcprestart = jalib::Filesystem::FindHelperUtility ( "mtcp_restart" );

  int fd = ConnectionToFds::openMtcpCheckpointFile(path);
  char buf[64];
  sprintf(buf,"%d", fd);
  char buf2[64];
  // gzip_child_pid set by openMtcpCheckpointFile() above.
  sprintf(buf2,"%d", dmtcp::ConnectionToFds::gzip_child_pid);

  char* newArgs[] = {
    ( char* ) mtcprestart.c_str(),
    ( char* ) "-fd",
    buf,
    ( char* ) "-gzip_child_pid",
    buf2,
    NULL
  };
  if (dmtcp::ConnectionToFds::gzip_child_pid == -1) // If no gzip compression
    newArgs[3] = NULL;

  JTRACE ( "launching mtcp_restart" ) ( newArgs[2] );

  execvp ( newArgs[0], newArgs );
  JASSERT ( false ) ( newArgs[0] ) ( newArgs[1] ) ( JASSERT_ERRNO ).Text ( "exec() failed" );
}
