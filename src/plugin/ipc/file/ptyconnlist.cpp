/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, and gene@ccs.neu.edu          *
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

#include <sys/syscall.h>

#include "shareddata.h"
#include "util.h"

#include "ptyconnection.h"
#include "ptyconnlist.h"
#include "ptywrappers.h"

using namespace dmtcp;

static uint32_t virtPtyId = 0;

static PtyConnList *ptyConnList = NULL;
static PtyConnList *vfork_ptyConnList = NULL;

void
pty_virtual_to_real_filepath(DmtcpEventData_t *data);
void
pty_real_to_virtual_filepath(DmtcpEventData_t *data);

void
dmtcp_PtyConnList_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  PtyConnList::instance().eventHook(event, data);

  switch (event) {
  case DMTCP_EVENT_CLOSE_FD:
    PtyConnList::instance().processClose(data->closeFd.fd);
    break;

  case DMTCP_EVENT_DUP_FD:
    PtyConnList::instance().processDup(data->dupFd.oldFd, data->dupFd.newFd);
    break;

  case DMTCP_EVENT_VFORK_PREPARE:
    vfork_ptyConnList = (PtyConnList*) PtyConnList::instance().clone();
    break;

  case DMTCP_EVENT_VFORK_PARENT:
  case DMTCP_EVENT_VFORK_FAILED:
    delete ptyConnList;
    ptyConnList = vfork_ptyConnList;
    break;

  case DMTCP_EVENT_PRESUSPEND:
    break;

  case DMTCP_EVENT_PRECHECKPOINT:
    PtyConnList::drainFd();
    break;

  case DMTCP_EVENT_RESUME:
    PtyConnList::resumeRefill();
    break;

  case DMTCP_EVENT_RESTART:
    PtyConnList::restart();
    dmtcp_local_barrier("Pty::RESTART_POST_RESTART");
    PtyConnList::restartRefill();
    break;

  case DMTCP_EVENT_VIRTUAL_TO_REAL_PATH:
    pty_virtual_to_real_filepath(data);
  break;

  case DMTCP_EVENT_REAL_TO_VIRTUAL_PATH:
    pty_virtual_to_real_filepath(data);
  break;

  default:  // other events are not registered
    break;
  }
}


DmtcpPluginDescriptor_t ptyPlugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "pty",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "PTY plugin",
  dmtcp_PtyConnList_EventHook
};

void
ipc_initialize_plugin_pty()
{
  dmtcp_register_plugin(ptyPlugin);
}

void
pty_virtual_to_real_filepath(DmtcpEventData_t *data)
{
  if (!Util::strStartsWith(data->virtualToRealPath.path, VIRT_PTS_PREFIX_STR)) {
    return;
  }

  char newPath[PATH_MAX] = {0};
  SharedData::getRealPtyName(data->virtualToRealPath.path,
                             newPath,
                             sizeof(newPath));

  strncpy(data->virtualToRealPath.path, newPath, PATH_MAX);
}

void
pty_real_to_virtual_filepath(DmtcpEventData_t *data)
{
  if (!Util::strStartsWith(data->realToVirtualPath.path, VIRT_PTS_PREFIX_STR)) {
    return;
  }

  char newPath[PATH_MAX] = {0};
  SharedData::getRealPtyName(data->realToVirtualPath.path,
                             newPath,
                             sizeof(newPath));

  strncpy(data->realToVirtualPath.path, newPath, PATH_MAX);

  // TODO:
  // We probably received this terminal fd over unix-domain socket using
  // recvmsg(). This was observed with tmux. When we run `tmux attach`, the
  // tmux client sends its controlling terminal to the daemon process via
  // sendmsg() over a unix domain socket. Ideally, we would create wrappers
  // for recvmsg() and sendmsg() for handling such cases. In the meanwhile,
  // we would create a PtyConection of type PTY_EXTERNAL and will fail on
  // checkpoint if we still have it open.
  // PtyConnection *c =
  //   new PtyConnection(fd, tmpbuf, O_RDWR, -1, PtyConnection::PTY_EXTERNAL);
}

PtyConnList&
PtyConnList::instance()
{
  if (ptyConnList == NULL) {
    ptyConnList = new PtyConnList();
  }
  return *ptyConnList;
}

void
PtyConnList::drain()
{
  virtPtyId = SharedData::getVirtualPtyId();
  for (iterator i = begin(); i != end(); ++i) {
    PtyConnection *con = (PtyConnection *)i->second;
    con->drain();
  }
}

void
PtyConnList::postRestart()
{
  SharedData::setVirtualPtyId(virtPtyId);
  /* It is possible to have two different connection-ids for a pre-existing
   * CTTY in two or more different process trees. In this case, only one of the
   * several process trees would be able to acquire a lock on the underlying
   * fd.  The send-receive fd logic fails in this case due to different
   * connection-ids.  Therefore, we let every process do a postRestart to
   * reopen the CTTY.
   *
   * TODO: A better fix would be to have a unique connection-id for each
   * pre-existing CTTY that is then used by all process trees.  It can be
   * implemented by using the SharedData area.
   */
  for (iterator i = begin(); i != end(); ++i) {
    PtyConnection *pcon = (PtyConnection *)i->second;
    pcon->postRestart();
  }
}

void
PtyConnList::refill(bool isRestart)
{
  for (iterator i = begin(); i != end(); ++i) {
    PtyConnection *pcon = (PtyConnection *)i->second;
    pcon->refill(isRestart);
  }
}

// examine /proc/self/fd for unknown connections
void
PtyConnList::scanForPreExisting()
{
  // FIXME: Detect stdin/out/err fds to detect duplicates.
  vector<int>fds = jalib::Filesystem::ListOpenFds();
  string ctty = jalib::Filesystem::GetControllingTerm();
  string parentCtty = jalib::Filesystem::GetControllingTerm(getppid());

  for (size_t i = 0; i < fds.size(); ++i) {
    int fd = fds[i];
    if (!Util::isValidFd(fd)) {
      continue;
    }
    if (dmtcp_is_protected_fd(fd)) {
      continue;
    }

    string device = jalib::Filesystem::GetDeviceName(fd);

    JTRACE("scanning pre-existing device") (fd) (device);
    if (device == ctty || device == parentCtty) {
      // Search if this is duplicate connection
      iterator conit;
      uint32_t cttyType = (device == ctty) ? PtyConnection::PTY_CTTY
        : PtyConnection::PTY_PARENT_CTTY;
      for (conit = begin(); conit != end(); conit++) {
        Connection *c = conit->second;
        if (c->subType() == cttyType &&
            ((PtyConnection *)c)->ptsName() == device) {
          processDup(c->getFds()[0], fd);
          break;
        }
      }
      if (conit == end()) {
        // FIXME: Merge this code with the code in processFileConnection
        PtyConnection *con = new PtyConnection(fd, (const char *)device.c_str(),
                                               -1, -1, cttyType);

        // Check comments in PtyConnList::postRestart() for the explanation
        // about isPreExistingCTTY.
        con->markPreExistingCTTY();
        add(fd, (Connection *)con);
      }
    }
  }
}

void
PtyConnList::processPtyConnection(int fd,
                                  const char *path,
                                  int flags,
                                  mode_t mode)
{
  Connection *c = NULL;

  string device;

  if (path == NULL) {
    device = jalib::Filesystem::GetDeviceName(fd);
  } else {
    device = jalib::Filesystem::ResolveSymlink(path);
    if (device == "") {
      device = path;
    }
  }

  path = device.c_str();
  if (strcmp(path, "/dev/tty") == 0) {
    // Controlling terminal
    c = new PtyConnection(fd, path, flags, mode, PtyConnection::PTY_DEV_TTY);
  } else if (strcmp(path, "/dev/pty") == 0) {
    JASSERT(false).Text("Not Implemented");
  } else if (Util::strStartsWith(path, "/dev/pty")) {
    // BSD Master
    c = new PtyConnection(fd, path, flags, mode, PtyConnection::PTY_BSD_MASTER);
  } else if (Util::strStartsWith(path, "/dev/tty")) {
    // BSD Slave
    c = new PtyConnection(fd, path, flags, mode, PtyConnection::PTY_BSD_SLAVE);
  } else if (strcmp(path, "/dev/ptmx") == 0 ||
             strcmp(path, "/dev/pts/ptmx") == 0) {
    // POSIX Master PTY
    c = new PtyConnection(fd, path, flags, mode, PtyConnection::PTY_MASTER);
  } else if (Util::strStartsWith(path, "/dev/pts/")) {
    // POSIX Slave PTY
    c = new PtyConnection(fd, path, flags, mode, PtyConnection::PTY_SLAVE);
  } else {
    JASSERT(false) (path).Text("Unimplemented file type.");
  }

  PtyConnList::instance().add(fd, c);
}
