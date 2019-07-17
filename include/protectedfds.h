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

#include <stdint.h>
#include <stdlib.h>

#define DEFAULT_PROTECTED_FD_BASE   820

int protectedFdBase();

// FIXME:  This should be unsigned short, and we should use "%lud" to print it.
//         Then, "protectedFdBase() + XXX" fits inside unsigned long.
inline int protectedFdBase()
{
  static int base = DEFAULT_PROTECTED_FD_BASE;
  const char *buf = getenv("DMTCP_PROTECTED_FD_BASE");

  if (buf) {
    base = atoi(buf);
  }
  return base;
}

enum ProtectedFds {
  FD_START = 0,
  COORD_FD,
  VIRT_COORD_FD,
  RESTORE_IP4_SOCK_FD,
  RESTORE_IP6_SOCK_FD,
  RESTORE_UDS_SOCK_FD,
  COORD_ALT_FD,
  STDERR_FD,
  JASSERTLOG_FD,
  LIFEBOAT_FD,
  CKPT_DIR_FD,
  SHM_FD,
  PIDMAP_FD,
  PTRACE_FD,
  FILE_FDREWIRER_FD,
  SOCKET_FDREWIRER_FD,
  EVENT_FDREWIRER_FD,
  READLOG_FD,
  ENVIRON_FD,
  NS_FD,
  DEBUG_SOCKET_FD,
  FD_END
};

#define PROTECTED_FD_START                protectedFdBase() + FD_START
#define PROTECTED_COORD_FD                protectedFdBase() + COORD_FD
#define PROTECTED_VIRT_COORD_FD           protectedFdBase() + VIRT_COORD_FD
#define PROTECTED_RESTORE_IP4_SOCK_FD     protectedFdBase() + RESTORE_IP4_SOCK_FD
#define PROTECTED_RESTORE_IP6_SOCK_FD     protectedFdBase() + RESTORE_IP6_SOCK_FD
#define PROTECTED_RESTORE_UDS_SOCK_FD     protectedFdBase() + RESTORE_UDS_SOCK_FD
#define PROTECTED_COORD_ALT_FD            protectedFdBase() + COORD_ALT_FD
#define PROTECTED_STDERR_FD               protectedFdBase() + STDERR_FD
#define PROTECTED_JASSERTLOG_FD           protectedFdBase() + JASSERTLOG_FD
#define PROTECTED_LIFEBOAT_FD             protectedFdBase() + LIFEBOAT_FD
#define PROTECTED_CKPT_DIR_FD             protectedFdBase() + CKPT_DIR_FD
#define PROTECTED_SHM_FD                  protectedFdBase() + SHM_FD
#define PROTECTED_PIDMAP_FD               protectedFdBase() + PIDMAP_FD
#define PROTECTED_PTRACE_FD               protectedFdBase() + PTRACE_FD
#define PROTECTED_FILE_FDREWIRER_FD       protectedFdBase() + FILE_FDREWIRER_FD
#define PROTECTED_SOCKET_FDREWIRER_FD     protectedFdBase() + SOCKET_FDREWIRER_FD
#define PROTECTED_EVENT_FDREWIRER_FD      protectedFdBase() + EVENT_FDREWIRER_FD
#define PROTECTED_READLOG_FD              protectedFdBase() + READLOG_FD
#define PROTECTED_ENVIRON_FD              protectedFdBase() + ENVIRON_FD
#define PROTECTED_NS_FD                   protectedFdBase() + NS_FD
#define PROTECTED_DEBUG_SOCKET_FD         protectedFdBase() + DEBUG_SOCKET_FD
#define PROTECTED_FD_END                  protectedFdBase() + FD_END

#define DMTCP_IS_PROTECTED_FD(fd) \
  ((fd) > PROTECTED_FD_START && (fd) < PROTECTED_FD_END)

#endif
