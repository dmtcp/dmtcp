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

void dmtcp_SSH_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data);
void dmtcp_FileConnList_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data);
void dmtcp_SocketConnList_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data);
void dmtcp_EventConnList_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data);
void dmtcp_SysVIPC_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data);
void dmtcp_Timer_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data);

void dmtcp_FileConn_ProcessFdEvent(int event, int arg1, int arg2);
void dmtcp_SocketConn_ProcessFdEvent(int event, int arg1, int arg2);
void dmtcp_EventConn_ProcessFdEvent(int event, int arg1, int arg2);

extern "C"
void dmtcp_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  dmtcp_SSH_EventHook(event, data);
  dmtcp_FileConnList_EventHook(event, data);
  dmtcp_SocketConnList_EventHook(event, data);
  dmtcp_EventConnList_EventHook(event, data);
  dmtcp_SysVIPC_EventHook(event, data);
  dmtcp_Timer_EventHook(event, data);

  DMTCP_NEXT_EVENT_HOOK(event, data);
  return;
}

LIB_PRIVATE void process_fd_event(int event, int arg1, int arg2 = -1)
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

