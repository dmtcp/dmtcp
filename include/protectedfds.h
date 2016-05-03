/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#ifndef DMTCPPROTECTEDFDS_H
#define DMTCPPROTECTEDFDS_H

enum ProtectedFds {
  PROTECTED_FD_START = 820,
  PROTECTED_COORD_FD,
  PROTECTED_VIRT_COORD_FD,
  PROTECTED_RESTORE_IP4_SOCK_FD,
  PROTECTED_RESTORE_IP6_SOCK_FD,
  PROTECTED_RESTORE_UDS_SOCK_FD,
  PROTECTED_COORD_ALT_FD,
  PROTECTED_STDERR_FD,
  PROTECTED_JASSERTLOG_FD,
  PROTECTED_LIFEBOAT_FD,
  PROTECTED_CKPT_DIR_FD,
  PROTECTED_SHM_FD,
  PROTECTED_PIDMAP_FD,
  PROTECTED_PTRACE_FD,
  PROTECTED_FILE_FDREWIRER_FD,
  PROTECTED_SOCKET_FDREWIRER_FD,
  PROTECTED_EVENT_FDREWIRER_FD,
  PROTECTED_READLOG_FD,
  PROTECTED_ENVIRON_FD,
  PROTECTED_NS_FD,
  PROTECTED_DEBUG_SOCKET_FD,
  PROTECTED_FD_END
};

#define DMTCP_IS_PROTECTED_FD(fd) \
  ((fd) > PROTECTED_FD_START && (fd) < PROTECTED_FD_END)
#endif // ifndef DMTCPPROTECTEDFDS_H
