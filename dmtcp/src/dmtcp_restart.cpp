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
#include "protectedfds.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>


static void runMtcpRestore ( const char* path, int offset );

using namespace dmtcp;

namespace
{

  class RestoreTarget
  {
    public:
      RestoreTarget ( const dmtcp::string& path )
          : _path ( path )
      {

        JASSERT ( jalib::Filesystem::FileExists ( _path ) ) ( _path ).Text ( "checkpoint file missing" );
#ifdef PID_VIRTUALIZATION
        _offset = _conToFd.loadFromFile(_path, _virtualPidTable);
#else
        _offset = _conToFd.loadFromFile(_path);
#endif
        JTRACE ( "restore target" ) ( _path ) ( _conToFd.size() ) (_offset);
      }

      void dupAllSockets ( SlidingFdTable& slidingFd )
      {
        int lastfd = -1;
        dmtcp::vector<int> fdlist;
        for ( ConnectionToFds::const_iterator i = _conToFd.begin(); i!=_conToFd.end(); ++i )
        {
          Connection& con = ConnectionList::Instance() [i->first];
          if ( con.conType() == Connection::INVALID ){
            JWARNING(false)(i->first).Text("Can't restore invalid Connection");
            continue;
          }

          const dmtcp::vector<int>& fds = i->second;
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
              const dmtcp::vector<int>& fds = i->second;
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
              const dmtcp::vector<int>& fds = i->second;
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
        runMtcpRestore ( _path.c_str(), _offset );
      }

      const UniquePid& pid() const { return _conToFd.pid(); }
      const dmtcp::string& procname() const { return _conToFd.procname(); }

#ifdef PID_VIRTUALIZATION
      VirtualPidTable& getVirtualPidTable() { return _virtualPidTable; }
#endif

      dmtcp::string     _path;
      int _offset;
      ConnectionToFds _conToFd;
#ifdef PID_VIRTUALIZATION
      VirtualPidTable _virtualPidTable;
#endif
  };

#ifdef PID_VIRTUALIZATION

  class OriginalPidTable {

    public:
      OriginalPidTable(){}
      void insert ( dmtcp::vector< pid_t > newVector )
      {
        for ( int i = 0; i < newVector.size(); ++i )
        {
          _vector.push_back ( newVector[i] );
        }
      }

      bool isConflictingChildPid ( pid_t pid )
      {
        //iterator i = _vector.find ( pid );
        //if ( i == _vector.end() )
        //  return false;
        for ( int i = 0; i < _vector.size(); ++i )
          if ( _vector[i] == pid )
            return true;

        return false;
      }


    private:
      typedef dmtcp::vector< pid_t >::iterator iterator;
      dmtcp::vector< pid_t > _vector;
  };

#endif

}//namespace

static const char* theUsage =
  "USAGE:\n dmtcp_restart [OPTIONS] <ckpt1.dmtcp> [ckpt2.dmtcp...]\n\n"
  "OPTIONS:\n"
  "  --host, -h, (environment variable DMTCP_HOST):\n"
  "      Hostname where dmtcp_coordinator is run (default: localhost)\n"
  "  --port, -p, (environment variable DMTCP_PORT):\n"
  "      Port where dmtcp_coordinator is run (default: 7779)\n"
  "  --tmpdir, -t, (environment variable DMTCP_TMPDIR):\n"
  "      Directory to store temporary files (default: env var TMDPIR or /tmp)\n"
  "  --join, -j:\n"
  "      Join an existing coordinator, do not create one automatically\n"
  "  --new, -n:\n"
  "      Create a new coordinator, raise error if one already exists\n"
  "  --no-check:\n"
  "      Skip check for valid coordinator and never start one automatically\n"
  "  --quiet, -q:\n"
  "      Skip copyright notice\n\n"
  "See http://dmtcp.sf.net/ for more information.\n"
;

//shift args
#define shift argc--,argv++

dmtcp::vector<RestoreTarget> targets;

#ifdef PID_VIRTUALIZATION
OriginalPidTable originalPidTable;
void CreateProcess(RestoreTarget& targ, DmtcpWorker& worker, SlidingFdTable& slidingFd, pid_t ppid, jalib::JBinarySerializeWriterRaw& wr );
static jalib::JBinarySerializeWriterRaw& createPidMapFile();
static void insertIntoPidMapFile(jalib::JBinarySerializer& o, pid_t originalPid, pid_t currentPid);
static pid_t forkChild();
#endif

int main ( int argc, char** argv )
{
  bool quiet = false;
  bool autoStartCoordinator=true;
  int allowedModes = dmtcp::DmtcpWorker::COORD_ANY;

  if (getenv("TMPDIR"))
    setenv(ENV_VAR_TMPDIR, getenv("TMPDIR"), 0);
  else
    setenv(ENV_VAR_TMPDIR, "/tmp", 0);

  //process args
  shift;
  while(true){
    dmtcp::string s = argc>0 ? argv[0] : "--help";
    if(s=="--help" || s=="-h" && argc==1){
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
    }else if(argc>1 && (s == "-t" || s == "--tmpdir")){
      setenv(ENV_VAR_TMPDIR, argv[1], 1);
      shift; shift;
    }else if(s == "-q" || s == "--quiet"){
      quiet = true;
      shift;
    }else if(argc>1 && s=="--"){
      shift;
      break;
    }else{
      break;
    }
  }
  JASSERT(0 == access(getenv(ENV_VAR_TMPDIR), R_OK|W_OK))
    (getenv(ENV_VAR_TMPDIR)).Text("ERROR: Missing read- or write-access to tmp dir: %s");

  if (! quiet)
    printf("DMTCP/MTCP  Copyright (C) 2006-2008  Jason Ansel, Michael Rieker,\n"
           "                                       Kapil Arya, and Gene Cooperman\n"
           "This program comes with ABSOLUTELY NO WARRANTY.\n"
           "This is free software, and you are welcome to redistribute it\n"
           "under certain conditions; see COPYING file for details.\n"
           "(Use flag \"-q\" to hide this message.)\n\n");

  if(autoStartCoordinator) dmtcp::DmtcpWorker::startCoordinatorIfNeeded(allowedModes);

  //make sure JASSERT initializes now, rather than during restart
  JASSERT_INIT();

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

#ifndef PID_VIRTUALIZATION
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
#else
  int i = (int)targets.size();

  jalib::JBinarySerializeWriterRaw& wr = createPidMapFile();

  wr & i;

  for (int j = 0; j < targets.size(); ++j) 
  {
    VirtualPidTable& virtualPidTable = targets[j].getVirtualPidTable();
    originalPidTable.insert ( virtualPidTable.getPidVector() );
  }


  int pgrp_index=-1;
  JTRACE ( "Creating ROOT Processes" );
  for ( int j = 0 ; j < targets.size(); ++j ) 
  {
    //RestoreTarget& targ1 = targets[i];
    VirtualPidTable& virtualPidTable = targets[j].getVirtualPidTable();
    if ( virtualPidTable.isRootOfProcessTree() == true ) 
    {
      if (pgrp_index == -1)
      {
        pgrp_index = j;
        continue;
      }
      pid_t cid = fork();
      if ( cid == 0 ) 
      {
        JTRACE ( "Root of process tree, Creating Child Processes" ) ( _real_getpid() ) ( _real_getppid() );

//       //setpgid(0, 0);
//       JTRACE( "this" ) (ttyname(0));
//         sleep(30);
//       int err = tcsetpgrp(0, 0);
//       JTRACE ("this is the error*****************************") (errno)(strerror(errno));
       CreateProcess( targets[j], worker, slidingFd, _real_getppid(), wr);
        JASSERT (false) . Text( "Unreachable" );
      }
      JASSERT ( cid > 0 );
    }
  }
  CreateProcess( targets[pgrp_index], worker, slidingFd, _real_getppid(), wr );
}

void CreateProcess(RestoreTarget& targ, DmtcpWorker& worker, SlidingFdTable& slidingFd, pid_t ppid, jalib::JBinarySerializeWriterRaw& wr)
{
  //change UniquePid
  UniquePid::resetOnFork(targ.pid());

  VirtualPidTable& virtualPidTable = targ.getVirtualPidTable();
  virtualPidTable.updateMapping( targ.pid().pid(), _real_getpid() );

  for ( VirtualPidTable::iterator i = virtualPidTable.begin(); i != virtualPidTable.end(); ++i )
  {
    bool found = false;
    pid_t childOriginalPid = i->first;
    UniquePid& childUniquePid = i->second;

    for ( int j = 0; j < targets.size(); ++j )
    {
      if ( childUniquePid == targets[j].pid() )
      {
        JTRACE ( "Forking Child Process" ) ( targ.pid() ) ( childUniquePid ); 
        pid_t cid = forkChild();
        if ( cid == 0 )
        {
          CreateProcess ( targets[j], worker, slidingFd, targ.pid().pid(), wr );
          JASSERT ( false ) . Text ( "Unreachable" );
        }
        JASSERT ( cid > 0 );
        virtualPidTable.updateMapping ( childOriginalPid, cid );
        found = true;
      }
    }
    if ( !found ){
      virtualPidTable.erase( childOriginalPid );
    }
  }

  JTRACE("Child Processes forked, restoring process")(targets.size())(targ.pid())(getpid());

  insertIntoPidMapFile ( wr, targ.pid().pid(), _real_getpid() );

  //Reconnect to dmtcp_coordinator
  WorkerState::setCurrentState ( WorkerState::RESTARTING );
  worker.connectToCoordinator(false);
  worker.sendCoordinatorHandshake(targ.procname());
 
  dmtcp::string serialFile = dmtcp::UniquePid::pidTableFilename();
  JTRACE ( "PidTableFile: ") ( serialFile ) ( dmtcp::UniquePid::ThisProcess() );
  jalib::JBinarySerializeWriter tblwr ( serialFile );
  virtualPidTable.serialize ( tblwr );
  tblwr.~JBinarySerializeWriter();

  int stmpfd =  open( serialFile.c_str(), O_RDONLY);
  JASSERT ( stmpfd >= 0 ) ( serialFile ) ( errno );

  JASSERT ( dup2 ( stmpfd, PROTECTED_PIDTBL_FD) == PROTECTED_PIDTBL_FD ) ( serialFile ) ( stmpfd );

  close (stmpfd);
 
 //restart targets[i]
  targ.dupAllSockets ( slidingFd );
  targ.mtcpRestart();

  JASSERT ( false ).Text ( "unreachable" );
}


static pid_t forkChild()
{
  while ( 1 ) {

    pid_t childPid = fork();
    
    if ( childPid == 0 ) { /* child process */
      if ( originalPidTable.isConflictingChildPid ( getpid() ) )
        _exit(1);
      else
        return 0;
    } 
    else { /* Parent Process */
      if ( originalPidTable.isConflictingChildPid ( childPid ) ) {
        JTRACE( "PID Conflict, creating new child" ) (childPid);
        waitpid ( childPid, NULL, 0 );
      }
      else 
        return childPid;
    }
  }

  return -1;
}


static jalib::JBinarySerializeWriterRaw& createPidMapFile()
{
  dmtcp::ostringstream os;

  os << getenv(ENV_VAR_TMPDIR) << "/dmtcpPidMap."
     << dmtcp::UniquePid::ThisProcess();

  int fd = open(os.str().c_str(), O_CREAT|O_WRONLY|O_TRUNC|O_APPEND, 0600); 
  JASSERT(fd>=0) ( os.str() ) (strerror(errno))
    .Text("Failed to create file to store node wide PID Maps");

  JASSERT ( dup2 ( fd, PROTECTED_PIDMAP_FD ) == PROTECTED_PIDMAP_FD ) ( os.str() );

  static jalib::JBinarySerializeWriterRaw wr ( os.str(), PROTECTED_PIDMAP_FD );

  close (fd);

  return wr;
}

static void insertIntoPidMapFile(jalib::JBinarySerializer& o, pid_t originalPid, pid_t currentPid)
{
  struct flock fl;
  int fd;

  fl.l_type   = F_WRLCK;  /* F_RDLCK, F_WRLCK, F_UNLCK    */
  fl.l_whence = SEEK_SET; /* SEEK_SET, SEEK_CUR, SEEK_END */
  fl.l_start  = 0;        /* Offset from l_whence         */
  fl.l_len    = 0;        /* length, 0 = to EOF           */
  fl.l_pid    = getpid(); /* our PID                      */

  int result = -1;
  errno = 0;
  while (result == -1 || errno == EINTR )
    result = fcntl(PROTECTED_PIDMAP_FD, F_SETLKW, &fl);  /* F_GETLK, F_SETLK, F_SETLKW */

  JASSERT ( result != -1 ) (strerror(errno)) (errno) . Text ( "Unable to lock the PID MAP file" );

  JTRACE ( "Serializing PID MAP:" ) ( originalPid ) ( currentPid );
  /* Write the mapping to the file*/
  JSERIALIZE_ASSERT_POINT ( "PidMap:[" );
  o & originalPid & currentPid;
  JSERIALIZE_ASSERT_POINT ( "]" );

  fl.l_type   = F_UNLCK;  /* tell it to unlock the region */
  result = fcntl(PROTECTED_PIDMAP_FD, F_SETLK, &fl); /* set the region to unlocked */

  JASSERT (result != -1 || errno == ENOLCK) .Text ( "Unlock Failed" ) ;
}

#endif

static void runMtcpRestore ( const char* path, int offset )
{
  static dmtcp::string mtcprestart = jalib::Filesystem::FindHelperUtility ( "mtcp_restart" );

#ifdef USE_MTCP_FD_CALLING
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
  
  JTRACE ( "launching mtcp_restart -fd" )(fd)(path);
#else
  char buf[64];
  sprintf(buf,"%d", offset);

  char* newArgs[] = {
    ( char* ) mtcprestart.c_str(),
    ( char* ) "-offset",
    buf,
    (char*) path,
    NULL
  };
  
  JTRACE ( "launching mtcp_restart -offset" )(path)(offset);

#endif

  execvp ( newArgs[0], newArgs );
  JASSERT ( false ) ( newArgs[0] ) ( newArgs[1] ) ( JASSERT_ERRNO ).Text ( "exec() failed" );
}
