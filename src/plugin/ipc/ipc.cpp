/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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
#include <sys/syscall.h>
#include <sys/errno.h>
#include <linux/version.h>
#include "dmtcp.h"
#include "jassert.h"
#include "ipc.h"
#include "config.h"

#include "file/fileconnlist.h"
#include "event/eventconnlist.h"
#include "socket/socketconnlist.h"
#include "ssh/ssh.h"

using namespace dmtcp;

void dmtcp_SSH_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data);
void dmtcp_FileConnList_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data);
void dmtcp_SocketConnList_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data);
void dmtcp_EventConnList_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data);

void dmtcp_FileConn_ProcessFdEvent(int event, int arg1, int arg2);
void dmtcp_SocketConn_ProcessFdEvent(int event, int arg1, int arg2);
void dmtcp_EventConn_ProcessFdEvent(int event, int arg1, int arg2);
static void ipc_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  dmtcp_SSH_EventHook(event, data);
  dmtcp_FileConnList_EventHook(event, data);
  dmtcp_SocketConnList_EventHook(event, data);
  dmtcp_EventConnList_EventHook(event, data);
}

static DmtcpBarrier fileBarriers[] = {
  {DMTCP_LOCAL_BARRIER_PRE_CKPT, FileConnList::saveOptions, "PRE_CKPT"},
  {DMTCP_LOCAL_BARRIER_PRE_CKPT, FileConnList::leaderElection, "LEADER_ELECTION"},
  {DMTCP_LOCAL_BARRIER_PRE_CKPT, FileConnList::drainFd, "DRAIN"},
  {DMTCP_LOCAL_BARRIER_PRE_CKPT, FileConnList::ckpt, "WRITE_CKPT"},

  {DMTCP_LOCAL_BARRIER_RESUME,   FileConnList::resumeRefill, "RESUME_REFILL"},
  {DMTCP_LOCAL_BARRIER_RESUME,   FileConnList::resumeResume, "RESUME_RESUME"},

  {DMTCP_LOCAL_BARRIER_RESTART,  FileConnList::restart, "RESTART_POST_RESTART"},
  {DMTCP_LOCAL_BARRIER_RESTART,  FileConnList::restartRegisterNSData, "RESTART_NS_REGISTER_DATA"},
  {DMTCP_LOCAL_BARRIER_RESTART,  FileConnList::restartSendQueries, "RESTART_NS_SEND_QUERIES"},
  {DMTCP_LOCAL_BARRIER_RESTART,  FileConnList::restartRefill, "RESTART_REFILL"},
  {DMTCP_LOCAL_BARRIER_RESTART,  FileConnList::restartResume, "RESTART_RESUME"}
};

static DmtcpBarrier socketBarriers[] = {
  {DMTCP_LOCAL_BARRIER_PRE_CKPT, SocketConnList::saveOptions, "PRE_CKPT"},
  {DMTCP_LOCAL_BARRIER_PRE_CKPT, SocketConnList::leaderElection, "LEADER_ELECTION"},
  {DMTCP_LOCAL_BARRIER_PRE_CKPT, SocketConnList::drainFd, "DRAIN"},
  {DMTCP_LOCAL_BARRIER_PRE_CKPT, SocketConnList::ckpt, "WRITE_CKPT"},

  {DMTCP_LOCAL_BARRIER_RESUME,   SocketConnList::resumeRefill, "RESUME_REFILL"},
  {DMTCP_LOCAL_BARRIER_RESUME,   SocketConnList::resumeResume, "RESUME_RESUME"},

  {DMTCP_LOCAL_BARRIER_RESTART,  SocketConnList::restart, "RESTART_POST_RESTART"},
  {DMTCP_LOCAL_BARRIER_RESTART,  SocketConnList::restartRegisterNSData, "RESTART_NS_REGISTER_DATA"},
  {DMTCP_GLOBAL_BARRIER_RESTART,  SocketConnList::restartSendQueries, "RESTART_NS_SEND_QUERIES"},
  {DMTCP_LOCAL_BARRIER_RESTART,  SocketConnList::restartRefill, "RESTART_REFILL"},
  {DMTCP_LOCAL_BARRIER_RESTART,  SocketConnList::restartResume, "RESTART_RESUME"}
};

static DmtcpBarrier eventBarriers[] = {
  {DMTCP_LOCAL_BARRIER_PRE_CKPT, EventConnList::saveOptions, "PRE_CKPT"},
  {DMTCP_LOCAL_BARRIER_PRE_CKPT, EventConnList::leaderElection, "LEADER_ELECTION"},
  {DMTCP_LOCAL_BARRIER_PRE_CKPT, EventConnList::drainFd, "DRAIN"},
  {DMTCP_LOCAL_BARRIER_PRE_CKPT, EventConnList::ckpt, "WRITE_CKPT"},

  {DMTCP_LOCAL_BARRIER_RESUME,   EventConnList::resumeRefill, "RESUME_REFILL"},
  {DMTCP_LOCAL_BARRIER_RESUME,   EventConnList::resumeResume, "RESUME_RESUME"},

  {DMTCP_LOCAL_BARRIER_RESTART,  EventConnList::restart, "RESTART_POST_RESTART"},
  {DMTCP_LOCAL_BARRIER_RESTART,  EventConnList::restartRegisterNSData, "RESTART_NS_REGISTER_DATA"},
  {DMTCP_LOCAL_BARRIER_RESTART,  EventConnList::restartSendQueries, "RESTART_NS_SEND_QUERIES"},
  {DMTCP_LOCAL_BARRIER_RESTART,  EventConnList::restartRefill, "RESTART_REFILL"},
  {DMTCP_LOCAL_BARRIER_RESTART,  EventConnList::restartResume, "RESTART_RESUME"}
};

static DmtcpBarrier sshBarriers[] = {
  {DMTCP_LOCAL_BARRIER_PRE_CKPT, dmtcp_ssh_drain, "DRAIN"},
  {DMTCP_LOCAL_BARRIER_RESUME,   dmtcp_ssh_resume, "RESUME"},
  {DMTCP_LOCAL_BARRIER_RESTART,  dmtcp_ssh_restart, "RESTART"}
};

DmtcpPluginDescriptor_t sshPlugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "ssh",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "SSH plugin",
  DMTCP_DECL_BARRIERS(sshBarriers),
  dmtcp_SSH_EventHook
};

DmtcpPluginDescriptor_t filePlugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "file",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "File plugin",
  DMTCP_DECL_BARRIERS(fileBarriers),
  dmtcp_FileConnList_EventHook
};

DmtcpPluginDescriptor_t socketPlugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "socket",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Socket plugin",
  DMTCP_DECL_BARRIERS(socketBarriers),
  dmtcp_SocketConnList_EventHook
};

DmtcpPluginDescriptor_t eventPlugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "event",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Event plugin",
  DMTCP_DECL_BARRIERS(eventBarriers),
  dmtcp_EventConnList_EventHook
};

DmtcpPluginDescriptor_t ipcPlugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "ipc",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "IPC virtualization plugin",
  DMTCP_NO_PLUGIN_BARRIERS,
  ipc_event_hook
};

EXTERNC void dmtcp_initialize_plugin()
{
  dmtcp_register_plugin(sshPlugin);
  dmtcp_register_plugin(filePlugin);
  dmtcp_register_plugin(socketPlugin);
  dmtcp_register_plugin(eventPlugin);

  void (*fn)() = NEXT_FNC(dmtcp_initialize_plugin);
  if (fn != NULL) {
    (*fn)();
  }
}

/*
 *
 */
extern "C" void process_fd_event(int event, int arg1, int arg2 = -1)
{
  dmtcp_FileConn_ProcessFdEvent(event, arg1, arg2);
  dmtcp_SocketConn_ProcessFdEvent(event, arg1, arg2);
  dmtcp_EventConn_ProcessFdEvent(event, arg1, arg2);
}

extern "C" int close(int fd)
{
  if (dmtcp_is_protected_fd(fd)) {
    JTRACE("blocked attempt to close protected fd") (fd);
    errno = EBADF;
    return -1;
  }

  DMTCP_PLUGIN_DISABLE_CKPT();
  int rv = _real_close(fd);
  if (rv == 0 && dmtcp_is_running_state()) {
    process_fd_event(SYS_close, fd);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return rv;
}

extern "C" int fclose(FILE *fp)
{
  int fd = fileno(fp);
  if (dmtcp_is_protected_fd(fd)) {
    JTRACE("blocked attempt to fclose protected fd") (fd);
    errno = EBADF;
    return -1;
  }

  DMTCP_PLUGIN_DISABLE_CKPT();
  int rv = _real_fclose(fp);
  if (rv == 0 && dmtcp_is_running_state()) {
    process_fd_event(SYS_close, fd);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();

  return rv;
}

extern "C" int closedir(DIR *dir)
{
  int fd = dirfd(dir);
  if (dmtcp_is_protected_fd(fd)) {
    JTRACE("blocked attempt to closedir protected fd") (fd);
    errno = EBADF;
    return -1;
  }

  DMTCP_PLUGIN_DISABLE_CKPT();
  int rv = _real_closedir(dir);
  if (rv == 0 && dmtcp_is_running_state()) {
    process_fd_event(SYS_close, fd);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();

  return rv;
}

extern "C" int dup(int oldfd)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int newfd = _real_dup(oldfd);
  if (newfd != -1 && dmtcp_is_running_state()) {
    process_fd_event(SYS_dup, oldfd, newfd);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return newfd;
}

extern "C" int dup2(int oldfd, int newfd)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int res = _real_dup2(oldfd, newfd);
  if (res != -1 && newfd != oldfd && dmtcp_is_running_state()) {
    process_fd_event(SYS_dup, oldfd, newfd);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return newfd;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)) && __GLIBC_PREREQ(2,9)
// dup3 appeared in Linux 2.6.27
extern "C" int dup3(int oldfd, int newfd, int flags)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int res = _real_dup3(oldfd, newfd, flags);
  if (res != -1 && newfd != oldfd && dmtcp_is_running_state()) {
    process_fd_event(SYS_dup, oldfd, newfd);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return newfd;
}
#endif

