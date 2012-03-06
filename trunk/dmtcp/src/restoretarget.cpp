/****************************************************************************
 *   Copyright (C) 2006-2010 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <vector>

#include  "../jalib/jassert.h"
#include  "../jalib/jfilesystem.h"
#include "restoretarget.h"
#include "constants.h"
#include "connectionmanager.h"
#include "ckptserializer.h"
#include "protectedfds.h"
#include "util.h"
#include "syscallwrappers.h"

using namespace dmtcp;

void runMtcpRestore(const char* path, int offset, size_t argvSize,
                    size_t envSize);

RestoreTarget::RestoreTarget (const dmtcp::string& path)
  : _path (path)
{
  JASSERT (jalib::Filesystem::FileExists (_path)) (_path)
    .Text ("checkpoint file missing");

  _offset = CkptSerializer::loadFromFile(_path, &_conToFd, &_processInfo);

  _roots.clear();
  _children.clear();
  _smap.clear();
  _used = 0;

  JTRACE ("restore target") (_path) (_processInfo.numPeers())
    (_processInfo.compGroup()) (_conToFd.size()) (_offset);
}

void RestoreTarget::dupAllSockets (SlidingFdTable& slidingFd)
{
  int lastfd = -1;
  dmtcp::vector<int> fdlist;

  ConnectionToFds::const_iterator i;
  for (i = _conToFd.begin(); i!=_conToFd.end(); ++i) {
    Connection& con = ConnectionList::instance() [i->first];
    if (con.conType() == Connection::INVALID) {
      JWARNING(false)(i->first).Text("Can't restore invalid Connection");
      continue;
    }

    const dmtcp::vector<int>& fds = i->second;
    for (size_t x=0; x<fds.size(); ++x) {
      int fd = fds[x];
      fdlist.push_back (fd);
      slidingFd.freeUpFd (fd);
      int oldFd = slidingFd.getFdFor (i->first);
      JTRACE ("restoring fd") (i->first) (oldFd) (fd);
      //let connection do custom dup2 handling
      con.restartDup2(oldFd, fd);

      if (fd > lastfd) {
        lastfd = fd;
      }
    }
  }

  size_t j;
  for (int i = 0 ; i < slidingFd.startFd() ; i++) {
    for (j = 0 ; j < fdlist.size() ; j++) {
      if (fdlist.at (j) == i) {
        break;
      }
    }
    if (j == fdlist.size()) {
      close ( i );
    }
  }

  slidingFd.closeAll();
}

int RestoreTarget::find_stdin(SlidingFdTable& slidingFd)
{
  ConnectionToFds::const_iterator i;
  for (i = _conToFd.begin(); i!=_conToFd.end(); ++i) {
    const dmtcp::vector<int>& fds = i->second;
    for (size_t x=0; x<fds.size(); ++x) {
      if (fds[x] == STDIN_FILENO) {
        JTRACE("Found stdin: fds[x] <---> slidingFd.getFdfor ()")
          (x) (fds[x]) (slidingFd.getFdFor (i->first));
        return slidingFd.getFdFor (i->first);
      }
    }
  }
  return -1;
}

void RestoreTarget::mtcpRestart()
{
  runMtcpRestore(_path.c_str(), _offset,
                 _processInfo.argvSize(), _processInfo.envSize());
}


bool RestoreTarget::isSessionLeader()
{
  JTRACE("") (_processInfo.sid()) (upid().pid());
  return _processInfo.sid() == upid().pid();
}

bool RestoreTarget::isGroupLeader()
{
  JTRACE("") (_processInfo.sid()) (upid().pid());
  return _processInfo.gid() == upid().pid();
}

bool RestoreTarget::isForegroundProcess()
{
  JTRACE("") (_processInfo.sid()) (upid().pid());
  return _processInfo.fgid() == _processInfo.gid();
}

bool RestoreTarget::isInitChild()
{
  JTRACE("")(_processInfo.ppid());
  return _processInfo.ppid() == 1;
}

int RestoreTarget::addRoot(RestoreTarget *t, pid_t sid)
{
  if (isSessionLeader() && _processInfo.sid() == sid) {
    _roots.push_back(t);
    return 1;
  } else {
    t_iterator it = _children.begin();
    for (; it != _children.end(); it++) {
      if ((*it)->addRoot(t, sid)) {
        return 1;
      }
    }
  }
  return 0;
}

// Traverse this process subtree and set up information about sessions
//   and their leaders for all children.
RestoreTarget::sidMapping &RestoreTarget::setupSessions()
{
  pid_t sid = _processInfo.sid();
  if (!_children.size()) {
    _smap[sid] = isSessionLeader();
    return _smap;
  }
  // We have at least one child
  t_iterator it = _children.begin();
  _smap = (*it)->setupSessions();
  for (it++; it != _children.end();it++) {
    sidMapping tmp = (*it)->setupSessions();
    s_iterator it1 = tmp.begin();
    for (;it1 != tmp.end(); it1++) {
      s_iterator it2 = _smap.find(it1->first);
      if (it2 != _smap.end()) {
        // mapping already exist
        if (it2->second != it1->second) {
          // Session was created after child creation.  So child from one
          // thread cannot be member of session of child from other thread.
          JASSERT(false). Text("One child contains session leader"
                               " and other contains session member!\n");
          exit(0);
        }
      } else {
        // add new mapping
        _smap[it1->first] = it1->second;
      }
    }
  }

  s_iterator sit = _smap.find(sid);
  if (sit != _smap.end()) {
    // child is leader and parent is slave - impossible
    JASSERT(!sit->second || isSessionLeader())
      .Text("child is leader and parent is slave - impossible\n");
  }
  _smap[sid] = isSessionLeader();
  return _smap;
}

void RestoreTarget::printMapping()
{
  t_iterator it = _children.begin();
  for (; it != _children.end(); it++) {
    (*it)->printMapping();
  }
  JTRACE("")(upid());
  s_iterator sit = _smap.begin();
  for (; sit != _smap.end(); sit++) {
    JTRACE("") (sit->first) (sit->second);
  }
}

pid_t RestoreTarget::checkDependence(RestoreTarget *t)
{
  sidMapping smap = t->getSmap();
  s_iterator ext = smap.begin();
  // Run through sessions --> has leader mapping
  for (; ext != smap.end(); ext++) {
    if (ext->second == false) {
      // Session pointed by ext has no leader in target t process tree
      s_iterator intern = _smap.find(ext->first);
      if (intern != _smap.end() && intern->second == true) {
        // internal target has session leader in its tree
        // TODO: can process trees be connected through several sessions?
        return ext->first;
      }
    }
  }
  return -1;
}

void RestoreTarget::bringToForeground(SlidingFdTable& slidingFd)
{
  char controllingTerm[L_ctermid];
  pid_t pid;

  int sin = find_stdin(slidingFd);

  if (isSessionLeader()) {
    // XXX: Where is the controlling terminal being set?
    char *ptr =  ttyname(sin);
    int fd = open(ptr,O_RDWR);
    if (ctermid(controllingTerm)) {
      int tfd = open(ptr,O_RDONLY);
      if (tfd >= 0) {
        JTRACE("Setting current controlling terminal") (controllingTerm);
        close(tfd);
      } else if (ptr == NULL) {
        JTRACE("Cannot restore controlling terminal") (ttyname(sin));
      } else {
        JWARNING(false) (ttyname(sin))
          .Text("Cannot restore controlling terminal");
      }
    }
    if (fd >= 0) close(fd);
  }

  pid_t gid = getpgid(0);
  pid_t fgid = tcgetpgrp(sin);

  if (!isForegroundProcess())
    return;
  if (!isGroupLeader()) {
    return;
  }

  if (gid != fgid) {
    if (!(pid = fork())) { // fork subversive process
      // This process moves itself to current foreground Group
      // and then changes foreground Group to what we need
      // so it works as a spy, saboteur or wrecker :)
      // -- Artem
      JTRACE("Change current GID to foreground GID.");

      if (setpgid(0, fgid)) {
        if (fgid == -1) {
          JTRACE("CANNOT Change current GID to foreground GID")
            (getpid()) (fgid) (_processInfo.fgid()) (gid) (JASSERT_ERRNO);
        } else {
          JWARNING(false)
            (getpid()) (fgid) (_processInfo.fgid()) (gid) (JASSERT_ERRNO)
            .Text("CANNOT Change current GID to foreground GID");
        }
        fflush(stdout);
        exit(0);
      }

      JASSERT (tcsetpgrp(sin, gid) == 0)
        (JASSERT_ERRNO) (getpid()) (fgid) (gid) (_processInfo.fgid())
        .Text("CANNOT Move parent GID to foreground");

      JTRACE("Finish foregrounding.")(getpid())(getpgid(0))(tcgetpgrp(0));
      exit(0);
    } else {
      int status;
      wait(&status);
    }
  }
}

void RestoreTarget::restoreGroup(SlidingFdTable& slidingFd)
{
  if (isGroupLeader()) {
    // create new Group where this process becomes a leader
    JTRACE("Create new Group.");
    setpgid(0, 0);
    bringToForeground(slidingFd);
  }
}

void RestoreTarget::CreateProcess(DmtcpCoordinatorAPI& coordinatorAPI,
                   SlidingFdTable& slidingFd)
{
  //change UniquePid
  UniquePid::resetOnFork(upid());
  //UniquePid::ThisProcess(true) = _conToFd.upid();

  Util::initializeLogFile(procname());
  JTRACE("Creating process during restart") (upid()) (procname());

  ProcessInfo &pInfo = _processInfo;

  JTRACE("")(getpid())(getppid())(getsid(0));

  pid_t psid = pInfo.sid();

  if (!isSessionLeader()) {

    // Restore Group information
    restoreGroup(slidingFd);

    // If process is not session leader, restore it and all children.
    t_iterator it = _children.begin();
    for (; it != _children.end(); it++) {
      JTRACE ("Forking Child Process") ((*it)->upid());
      pid_t cid = fork();

      if (cid == 0) {
        (*it)->CreateProcess (coordinatorAPI, slidingFd);
        JASSERT (false) . Text ("Unreachable");
      }
      JASSERT (cid > 0);
    }
  } else {
    // Process is session leader.
    // There may be not setsid-ed children.
    for (t_iterator it = _children.begin(); it != _children.end(); it++) {
      s_iterator sit = (*it)->getSmap().find(psid);
      JTRACE("Restore processes that were created before their parent called setsid()");
      if (sit == (*it)->getSmap().end()) {
        JTRACE ("Forking Child Process") ((*it)->upid());
        pid_t cid = fork();
        if (cid == 0) {
          (*it)->CreateProcess (coordinatorAPI, slidingFd);
          JASSERT (false) . Text ("Unreachable");
        }
        JASSERT (cid > 0);
      }
    }

    pid_t nsid = setsid();
    JTRACE("change SID")(nsid);

    // Restore Group information
    restoreGroup(slidingFd);

    for (t_iterator it = _children.begin(); it != _children.end(); it++) {
      JTRACE("Restore processes that was created after their parent called setsid()");
      s_iterator sit = (*it)->getSmap().find(psid);
      if (sit != (*it)->getSmap().end()) {
        JTRACE ("Forking Child Process") ((*it)->upid());
        pid_t cid = fork();
        if (cid == 0) {
          (*it)->CreateProcess (coordinatorAPI, slidingFd);
          JASSERT (false) . Text ("Unreachable");
        }
        JASSERT (cid> 0);
      }
    }

    for (t_iterator it = _roots.begin() ; it != _roots.end(); it++) {
      JTRACE ("Forking Dependent Root Process") ((*it)->upid());
      pid_t cid;
      if ((cid = fork())) {
        waitpid(cid, NULL, 0);
      } else {
        if (fork())
          exit(0);
        (*it)->CreateProcess(coordinatorAPI, slidingFd);
        JASSERT (false) . Text("Unreachable");
      }
    }
  }

  bool isTheGroupLeader = isGroupLeader(); // Calls JTRACE;avoid recursion
  JTRACE("Child and dependent root processes forked, restoring process")
    (upid())(getpid())(isGroupLeader());

  //Reconnect to dmtcp_coordinator
  WorkerState::setCurrentState (WorkerState::RESTARTING);

  int tmpCoordFd = dup(PROTECTED_COORD_FD);
  JASSERT(tmpCoordFd != -1);
  coordinatorAPI.connectToCoordinator();
  coordinatorAPI.sendCoordinatorHandshake(procname(), _processInfo.compGroup());
  coordinatorAPI.recvCoordinatorHandshake();
  close(tmpCoordFd);

  //restart targets[i]
  dupAllSockets (slidingFd);

  mtcpRestart();

  JASSERT (false).Text ("unreachable");
}
