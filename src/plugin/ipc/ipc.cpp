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

#include "ipc.h"
#include <linux/version.h>
#include <sys/errno.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "jassert.h"
#include "config.h"
#include "dmtcp.h"

#include "event/eventconnlist.h"
#include "file/fileconnlist.h"
#include "file/filewrappers.h"
#include "file/ptyconnlist.h"
#include "socket/socketconnlist.h"
#include "ssh/ssh.h"

using namespace dmtcp;

void ipc_initialize_plugin_socket();
void ipc_initialize_plugin_file();
void ipc_initialize_plugin_pty();
void ipc_initialize_plugin_event();
void ipc_initialize_plugin_ssh();

EXTERNC void
dmtcp_initialize_plugin()
{
  /* A note on the ordering of plugins:
   * 1. The file, pty, and socket plugins are independent of each other. Thus the
   *    relative ordering doesn't matter.
   * 2. The event plugin must be restored _after_ all other plugins that deal
   *    with file descriptors have been restored. This is because an epoll call
   *    would require the to-be-monitored fds to be valid at the time of calling
   *    epoll_ctl. (See github issue #405)
   * 3. The SSH plugin should be restored _after_ the socket plugin because it
   *    relies on the out-of-band socket to be restored in order to determine
   *    the current network address of the remote ssh-child.
   */

  ipc_initialize_plugin_ssh();
  ipc_initialize_plugin_event();
  ipc_initialize_plugin_file();
  ipc_initialize_plugin_pty();
  ipc_initialize_plugin_socket();

  void (*fn)() = NEXT_FNC(dmtcp_initialize_plugin);
  if (fn != NULL) {
    (*fn)();
  }
}
