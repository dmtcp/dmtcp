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

#ifdef PID_VIRTUALIZATION

  class OriginalPidTable {
    public:
      OriginalPidTable(){}

      void insertFromVirtualPidTable ( dmtcp::VirtualPidTable vt )
      {
        _insert(vt.getPidVector());
        _insert(vt.getTidVector());
      }

      void _insert ( dmtcp::vector< pid_t > newVector )
      {
        for ( int i = 0; i < newVector.size(); ++i ) {
          if (!isConflictingChildPid (newVector[i]) /* && newVector[i] != getpid()*/) {
            _vector.push_back ( newVector[i] );
            JTRACE("New Pid Pushed to PidVector") (newVector[i]);
          }
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

      size_t numPids () { return _vector.size(); }

    private:
      typedef dmtcp::vector< pid_t >::iterator iterator;
      dmtcp::vector< pid_t > _vector;
  };

  OriginalPidTable originalPidTable;
  
#endif



  class RestoreTarget
  {
    public:
      RestoreTarget ( const dmtcp::string& path )
          : _path ( path )
      {

        JASSERT ( jalib::Filesystem::FileExists ( _path ) ) ( _path ).Text ( "checkpoint file missing" );
#ifdef PID_VIRTUALIZATION
        _offset = _conToFd.loadFromFile(_path, _virtualPidTable);
        _virtualPidTable.erase(getpid());
        _roots.clear();
        _childs.clear();
        _smap.clear();
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
      typedef map<pid_t,bool> sidMapping;
      typedef sidMapping::iterator s_iterator;
      typedef vector<RestoreTarget *>::iterator t_iterator;
      
      VirtualPidTable& getVirtualPidTable() { return _virtualPidTable; }
      void addChild(RestoreTarget *t){ _childs.push_back(t); }

      bool isSessionLeader(){
        JTRACE("")(_virtualPidTable.sid()) (pid().pid());
        if( _virtualPidTable.sid() == pid().pid() )
          return true;
        else
          return false;
      }

      bool isInitChild(){
        JTRACE("")(_virtualPidTable.ppid());
        if( _virtualPidTable.ppid() == 1 )
          return true;
        else
          return false;
      }
      
      
      int addRoot(RestoreTarget *t,pid_t sid){ 
        if( isSessionLeader() && _virtualPidTable.sid() == sid ){
          _roots.push_back(t);
          return 1;
        }else{
          t_iterator it = _childs.begin();
          for(; it != _childs.end(); it++){
            if( (*it)->addRoot(t,sid) )
              return 1;
          }
        }
        return 0;
      }
    
      // Traverce this process subtree and setup 
      // information about sessions and its leaders of all childs
      sidMapping &setupSessions() {
        pid_t sid = _virtualPidTable.sid();
        if( !_childs.size() ) {
          _smap[sid] = isSessionLeader();
          return _smap;
        }
        // We have at least one child
        t_iterator it = _childs.begin();
        _smap = (*it)->setupSessions();
        for(it++; it != _childs.end();it++) {
          sidMapping tmp = (*it)->setupSessions();
          s_iterator it1 = tmp.begin();
          for(;it1 != tmp.end(); it1++) {
            s_iterator it2 = _smap.find(it1->first);
            if( it2 != _smap.end() ) {
              // mapping already exist
              if( it2->second != it1->second ) {
                // Session was created after child creation so child from 
                // one thread cannot be slave of child from other thread
                printf("One child contain session leader and other session slave slave!\n");
                exit(0);
              }
            } else {
              // add new mapping
              _smap[it1->first] = it1->second;
            }
          }
        }

        s_iterator sit = _smap.find(sid);
        if( sit != _smap.end() ) {
          if( sit->second && !isSessionLeader() ) {
            // child is leader and parent is slave - impossible
            printf("child is leader and parent is slave - impossible\n");
            exit(0);
          }
        }
        _smap[sid] = isSessionLeader();
        return _smap;
      }
 
      void printMapping(){
          t_iterator it = _childs.begin();
          for(; it != _childs.end(); it++){
              (*it)->printMapping();
          }
          JTRACE("")(pid());
          s_iterator sit = _smap.begin();
          for(; sit != _smap.end(); sit++){
              printf("(%d,%d) ",sit->first,sit->second);
          }
          printf("\n");
      }
      
      sidMapping &getSmap(){ return _smap; }
      
      pid_t checkDependence(RestoreTarget *t){
        sidMapping smap = t->getSmap();
        s_iterator ext = smap.begin();
        // Run through sessions --> has leader mapping
        for(; ext != smap.end(); ext++){
          if( ext->second == false ){
            // Session pointed by ext has no leader in target t process tree
            s_iterator intern = _smap.find(ext->first);
            if( intern != _smap.end() && intern->second == true ){
              // internal target has session leader in its tree
              // TODO: can process trees be connected through several sessions?
              return ext->first;
            }
          }
        }
        return -1;
      }

      void CreateProcess(DmtcpWorker& worker, SlidingFdTable& slidingFd, jalib::JBinarySerializeWriterRaw& wr)
      {
        //change UniquePid
        UniquePid::resetOnFork(pid());
        VirtualPidTable &vt = _virtualPidTable;
        
        JTRACE("")(_real_getpid())(_real_getppid())(_real_getsid(0));

        vt.updateMapping(pid().pid(),_real_getpid());
        pid_t psid = vt.sid();
        
        if( !isSessionLeader() ){
          // If process is not session leader - restore all childs and restore it
          t_iterator it = _childs.begin();
          for(; it != _childs.end(); it++){
            JTRACE ( "Forking Child Process" ) ( (*it)->pid() ); 
            pid_t cid = forkChild();
            
            if ( cid == 0 )
            {
              (*it)->CreateProcess (worker, slidingFd, wr );
              JASSERT ( false ) . Text ( "Unreachable" );
            }
            JASSERT ( cid > 0 );
            VirtualPidTable::iterator vit = vt.begin();
            for(; vit != vt.end(); vit++){
              if( (*it)->pid() == vit->second ){
                vt.updateMapping ( vit->first, cid );
                break;
              }
            }

          }
        }else{
          // Process is session leader
          
          // there may be not setsid-ed childs
          t_iterator it = _childs.begin();
          for(it; it != _childs.end(); it++){
              s_iterator sit = (*it)->getSmap().find(psid);
              JTRACE("Restore processes that was created before their parent called setsid()");
              if( sit == (*it)->getSmap().end() ){
                JTRACE ( "Forking Child Process" ) ( (*it)->pid() ); 
                pid_t cid = forkChild();
                if ( cid == 0 )
                {
                  (*it)->CreateProcess (worker, slidingFd, wr );
                  JASSERT ( false ) . Text ( "Unreachable" );
                }
                JASSERT ( cid > 0 );
                VirtualPidTable::iterator vit = _virtualPidTable.begin();
                for(; vit != _virtualPidTable.end(); vit++){
                  if( (*it)->pid() == vit->second ){
                    _virtualPidTable.updateMapping ( vit->first, cid );
                  }
                }
              }
          }
          
          pid_t nsid = setsid();
          JTRACE("change SID")(nsid);
          
          it = _childs.begin();
          for(it; it != _childs.end(); it++) {
            JTRACE("Restore processes that was created after their parent called setsid()");
            s_iterator sit = (*it)->getSmap().find(psid);
            if( sit != (*it)->getSmap().end() ) {
              JTRACE ( "Forking Child Process" ) ( (*it)->pid() );
              pid_t cid = forkChild();
              if ( cid == 0 ){
                (*it)->CreateProcess (worker, slidingFd, wr );
                JASSERT ( false ) . Text ( "Unreachable" );
              }
              JASSERT ( cid> 0 );
              VirtualPidTable::iterator vit = _virtualPidTable.begin();
              for(; vit != _virtualPidTable.end(); vit++) {
                if( (*it)->pid() == vit->second ) {
                  _virtualPidTable.updateMapping ( vit->first, cid );
                }
              }
            }
          }          

          it = _roots.begin();
          for(it; it != _roots.end(); it++) {
            JTRACE ( "Forking Dependent Root Process" ) ( (*it)->pid() );
            pid_t cid;
            if( (cid = fork()) ){
                waitpid(cid,NULL,0);
            }else{
              if( fork() )
                exit(0);
              (*it)->CreateProcess(worker, slidingFd, wr);
              JASSERT (false) . Text( "Unreachable" );
            }
          }
        }          

        JTRACE("Child & dependent root Processes forked, restoring process")(pid())(getpid());

        dmtcp::VirtualPidTable::InsertIntoPidMapFile ( wr, pid().pid(), _real_getpid() );

        //Reconnect to dmtcp_coordinator
        WorkerState::setCurrentState ( WorkerState::RESTARTING );
        worker.connectToCoordinator(false);
        worker.sendCoordinatorHandshake(procname());
       
        dmtcp::string serialFile = dmtcp::UniquePid::pidTableFilename();
        JTRACE ( "PidTableFile: ") ( serialFile ) ( dmtcp::UniquePid::ThisProcess() );
        jalib::JBinarySerializeWriter tblwr ( serialFile );
        _virtualPidTable.serialize ( tblwr );
        tblwr.~JBinarySerializeWriter();

        int stmpfd =  open( serialFile.c_str(), O_RDONLY);
        JASSERT ( stmpfd >= 0 ) ( serialFile ) ( errno );

        JASSERT ( dup2 ( stmpfd, PROTECTED_PIDTBL_FD) == PROTECTED_PIDTBL_FD ) ( serialFile ) ( stmpfd );

        close (stmpfd);
       
       //restart targets[i]
        dupAllSockets ( slidingFd );
        mtcpRestart();

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

      
#endif

      dmtcp::string     _path;
      int _offset;
      ConnectionToFds _conToFd;
#ifdef PID_VIRTUALIZATION
      VirtualPidTable _virtualPidTable;
      // Links to childs of this process
      vector<RestoreTarget *> _childs; 
      // Links to roots, which depends on this target
      // i.e. have SID of this target in its tree.
      vector<RestoreTarget *> _roots;
      sidMapping _smap;
#endif
  };


}//namespace

// gcc-4.3.4 -Wformat=2 issues false positives for warnings unless the format
// string has at least one format specifier with corresponding format argument.
// Ubuntu 9.01 uses -Wformat=2 by default.
static const char* theUsage =
  "%sUSAGE:\n dmtcp_restart [OPTIONS] <ckpt1.dmtcp> [ckpt2.dmtcp...]\n\n"
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
  "  --quiet, -q, (or set environment variable DMTCP_QUIET = 0, 1, or 2):\n"
  "      Skip banner and NOTE messages; if given twice, also skip WARNINGs\n\n"
  "See http://dmtcp.sf.net/ for more information.\n"
;

//shift args
#define shift argc--,argv++

dmtcp::vector<RestoreTarget> targets;

#ifdef PID_VIRTUALIZATION
static jalib::JBinarySerializeWriterRaw& createPidMapFile();
typedef struct {
  RestoreTarget *t;
  bool indep;
} RootTarget;
dmtcp::vector<RootTarget> roots;
void BuildProcessTree();
void SetupSessions();

#endif

int main ( int argc, char** argv )
{
  bool autoStartCoordinator=true;
  bool isRestart = true;
  int allowedModes = dmtcp::DmtcpWorker::COORD_ANY;

  if (getenv(ENV_VAR_TMPDIR))
    {}
  else if (getenv("TMPDIR"))
    setenv(ENV_VAR_TMPDIR, getenv("TMPDIR"), 0);
  else
    setenv(ENV_VAR_TMPDIR, "/tmp", 0);

  if (! getenv(ENV_VAR_QUIET))
    setenv(ENV_VAR_QUIET, "0", 0);

  //process args
  shift;
  while(true){
    dmtcp::string s = argc>0 ? argv[0] : "--help";
    if(s=="--help" || s=="-h" && argc==1){
      fprintf(stderr, theUsage, "");
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
      *getenv(ENV_VAR_QUIET) = *getenv(ENV_VAR_QUIET) + 1;
      // Just in case a non-standard version of setenv is being used:
      setenv(ENV_VAR_QUIET, getenv(ENV_VAR_QUIET), 1);
      shift;
    }else if(argc>1 && s=="--"){
      shift;
      break;
    }else{
      break;
    }
  }
  JASSERT(0 == access(getenv(ENV_VAR_TMPDIR), X_OK|W_OK))
    (getenv(ENV_VAR_TMPDIR))
    .Text("ERROR: Missing execute- or write-access to tmp dir: %s");
  jassert_quiet = *getenv(ENV_VAR_QUIET) - '0';

  if (jassert_quiet == 0)
    printf("DMTCP/MTCP  Copyright (C) 2006-2008  Jason Ansel, Michael Rieker,\n"
           "                                       Kapil Arya, and Gene Cooperman\n"
           "This program comes with ABSOLUTELY NO WARRANTY.\n"
           "This is free software, and you are welcome to redistribute it\n"
           "under certain conditions; see COPYING file for details.\n"
           "(Use flag \"-q\" to hide this message.)\n\n");

  if(autoStartCoordinator) dmtcp::DmtcpWorker::startCoordinatorIfNeeded(allowedModes, isRestart);

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
  size_t i = targets.size();

  // Create roots vector, assign childs to their parents
  // Delete not existing childs.
  BuildProcessTree();

  // Create session meta-information in each node of the process tree
  // Node contains info about all sessions which exists at lower levels.
  // Also node is aware about session leader existense at lower levels
  SetupSessions();
  
  /* Create the file to hold the pid/tid maps*/
  jalib::JBinarySerializeWriterRaw& wr = createPidMapFile();

  size_t numMaps = originalPidTable.numPids();
  dmtcp::VirtualPidTable::serializeEntryCount ( wr, numMaps );

  int pgrp_index=-1;
  JTRACE ( "Creating ROOT Processes" )(roots.size());
  for ( int j = 0 ; j < roots.size(); ++j ) 
  {
    if( roots[j].indep == false ){
      // we will restore this process from one of 
      // independent roots
      continue;
    }
    if (pgrp_index == -1 && !roots[j].t->isInitChild() ){
      pgrp_index = j;
      continue;
    }
    
    pid_t cid = fork();
    if ( cid == 0 ){
      JTRACE ( "Root of process tree" ) ( _real_getpid() ) ( _real_getppid() );
      if( roots[j].t->isInitChild() ){
        JTRACE ( "Create init-child process" ) ( _real_getpid() ) ( _real_getppid() );
        if( fork() )
          _exit(0);
      }
      roots[j].t->CreateProcess(worker, slidingFd, wr);
      JASSERT (false) . Text( "Unreachable" );
    }
    JASSERT ( cid > 0 );
    if( roots[j].t->isInitChild() ){
      waitpid(cid,NULL,0);
    }
  }
  if( pgrp_index < 0 )
    _exit(0);
  JTRACE("Restore first Root Target")(roots[pgrp_index].t->pid());
  roots[pgrp_index].t->CreateProcess(worker, slidingFd, wr );
}

void BuildProcessTree()
{
  for (int j = 0; j < targets.size(); ++j) 
  {
    VirtualPidTable& virtualPidTable = targets[j].getVirtualPidTable();
    originalPidTable.insertFromVirtualPidTable ( virtualPidTable );
    if( virtualPidTable.isRootOfProcessTree() == true ){
      RootTarget rt;
      rt.t = &targets[j];
      rt.indep = true;
      roots.push_back(rt);
    }

    // Add all childs
    VirtualPidTable::iterator it;
    for(it = virtualPidTable.begin(); it != virtualPidTable.end(); it++ ){
      // find target
      bool found = false;
      pid_t childOriginalPid = it->first;
      UniquePid& childUniquePid = it->second;
      
      for ( int i = 0; i < targets.size(); i++ )
      {
        if ( childUniquePid == targets[i].pid() )
        {
          found = 1;
          JTRACE ( "Add child to current target" ) ( targets[j].pid() ) ( childUniquePid );
          targets[j].addChild(&targets[i]);
        }
      }
      if ( !found ){
        virtualPidTable.erase( childOriginalPid );
      }
    }      
  }
}


void SetupSessions()
{
  for(int j=0; j < roots.size(); j++){
    roots[j].t->setupSessions();
  }
  
  for(int i = 0; i < roots.size(); i++){
    for(int j = 0; j < roots.size(); j++){
      if( i == j )
        continue;
      pid_t sid;
      if( (sid = (roots[i].t)->checkDependence(roots[j].t)) >= 0 ){
        // it2 depends on it1
        JTRACE("Root target j depends on Root target i")(i)(roots[i].t->pid())(j)(roots[j].t->pid());
        (roots[i].t)->addRoot(roots[j].t,sid);
        roots[j].indep = false;
      }
    }
  }
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
