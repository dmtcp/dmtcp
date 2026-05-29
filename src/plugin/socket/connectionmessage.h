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

#pragma once
#ifndef __SOCKET_CONNECTIONMESSAGE_H__
#define __SOCKET_CONNECTIONMESSAGE_H__

#include <stdint.h>
#include "jalloc.h"
#include "connectionidentifier.h"
#include "dmtcpalloc.h"

# define HANDSHAKE_SIGNATURE_MSG "DMTCP_SOCK_HANDSHAKE_V0\n"

namespace dmtcp
{
class ConnMsg
{
  public:
# ifdef JALIB_ALLOCATOR
    static void *operator new(size_t nbytes, void *p) { return p; }

    static void *operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }

    static void operator delete(void *p) { JALLOC_HELPER_DELETE(p); }
# endif // ifdef JALIB_ALLOCATOR

    enum MsgType {
      INVALID = -1,
      HANDSHAKE = 0,
      DRAIN,
      REFILL
    };

    ConnMsg(enum MsgType t = INVALID)
    {
      strcpy(sign, HANDSHAKE_SIGNATURE_MSG);
      type = t;
      size = sizeof(ConnMsg);
      extraBytes = 0;
    }

    void poison()
    {
      sign[0] = '\0';
      type = INVALID;
    }

    void assertValid(enum MsgType t)
    {
      JASSERT(strcmp(sign, HANDSHAKE_SIGNATURE_MSG) == 0) (sign)
      .Text("read invalid message, signature mismatch. (External socket?)");
      JASSERT(size == sizeof(ConnMsg)) (size) (sizeof(ConnMsg))
      .Text("read invalid message, size mismatch.");
      JASSERT(type == t) ((int)t) ((int)type).Text("Wrong Msg Type.");
    }

    ConnectionIdentifier from;
    ConnectionIdentifier coordId;

    char sign[32];
    int32_t type;
    int32_t size;
    int32_t extraBytes;
    char padding[4];
};

ostream&operator<<(ostream &o, const ConnectionIdentifier &id);
}
#endif // ifndef __SOCKET_CONNECTIONMESSAGE_H__
