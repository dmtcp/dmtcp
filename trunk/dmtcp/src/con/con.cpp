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

// THESE INCLUDES ARE IN RANDOM ORDER.  LET'S CLEAN IT UP AFTER RELEASE. - Gene
#include "constants.h"
#include "syscallwrappers.h"
#include "connection.h"
#include  "../jalib/jassert.h"
#include  "../jalib/jfilesystem.h"
#include  "../jalib/jconvert.h"
#include "kernelbufferdrainer.h"
#include "syscallwrappers.h"
#include "connectionrewirer.h"
#include "connectionmanager.h"
#include "connectionstate.h"
#include "dmtcpmessagetypes.h"
#include "dmtcpworker.h"
#include "dmtcpplugin.h"
#include "util.h"
#include "coordinatorapi.h"
#include "resource_manager.h"
#include  "../jalib/jsocket.h"
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <termios.h>
#include <iostream>
#include <ios>
#include <fstream>
#include <linux/limits.h>
#include <arpa/inet.h>

// static dmtcp::KernelBufferDrainer* theDrainer = NULL;
static dmtcp::ConnectionState* theCheckpointState = NULL;

dmtcp::string dmtcp_Connection_VirtualToRealPtsName(const char *ptsname)
{
  JASSERT(dmtcp::Util::strStartsWith(ptsname, UNIQUE_PTS_PREFIX_STR));
  return dmtcp::UniquePtsNameToPtmxConId::instance().
    retrieveCurrentPtsDeviceName(ptsname);
}

void dmtcp_Connection_ProcessEvent(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
    case DMTCP_EVENT_INIT:
      dmtcp::ConnectionList::instance().scanForPreExisting();
      JTRACE("Initial socket table:");
      dmtcp::KernelDeviceToConnection::instance().dbgSpamFds();
      break;

    case DMTCP_EVENT_WAIT_FOR_SUSPEND_MSG:
      if (theCheckpointState != NULL) {
        delete theCheckpointState;
        theCheckpointState = NULL;
      }

      theCheckpointState = new dmtcp::ConnectionState();
      break;

    case DMTCP_EVENT_PRE_EXEC:
      {
        jalib::JBinarySerializeWriterRaw wr("", data->serializerInfo.fd);
        dmtcp::KernelDeviceToConnection::instance().serialize(wr);
      }
      break;

    case DMTCP_EVENT_POST_EXEC:
      {
        jalib::JBinarySerializeReaderRaw rd("", data->serializerInfo.fd);
        dmtcp::KernelDeviceToConnection::instance().serialize(rd);
        JTRACE("Initial socket table:");
        dmtcp::KernelDeviceToConnection::instance().dbgSpamFds();
      }
      break;

    case DMTCP_EVENT_POST_RESTART:
      //dmtcp::SysVIPC::instance().postRestart();
      JASSERT(theCheckpointState != NULL);
      theCheckpointState->postRestart();

      break;

    case DMTCP_EVENT_PRE_FORK:
      dmtcp::KernelDeviceToConnection::instance().prepareForFork();
      break;

    case DMTCP_EVENT_SUSPENDED:
      theCheckpointState->preLockSaveOptions();
      break;

    case DMTCP_EVENT_LEADER_ELECTION:
      JTRACE("locking...");
      JASSERT(theCheckpointState != NULL);
      theCheckpointState->preCheckpointFdLeaderElection();
      JTRACE("locked");
      break;

    case DMTCP_EVENT_DRAIN:
      JTRACE("draining...");
      theCheckpointState->preCheckpointDrain();
      JTRACE("drained");
      break;

    case DMTCP_EVENT_PRE_CKPT:
#if HANDSHAKE_ON_CHECKPOINT == 1
      //handshake is done after one barrier after drain
      JTRACE("beginning handshakes");
      theCheckpointState->preCheckpointHandshakes(
                            dmtcp::CoordinatorAPI::instance().coordinatorId());
      JTRACE("handshaking done");
#endif
      break;

    case DMTCP_EVENT_REFILL:
      JASSERT(theCheckpointState != NULL);
      theCheckpointState->postCheckpoint(data->refillInfo.isRestart);
      delete theCheckpointState;
      theCheckpointState = NULL;
      break;

    case DMTCP_EVENT_WRITE_CKPT_PREFIX:
      {
        jalib::JBinarySerializeWriterRaw wr("", data->serializerInfo.fd);
        theCheckpointState->outputDmtcpConnectionTable(wr);
      }
      break;

    default:
      break;
  }
}

