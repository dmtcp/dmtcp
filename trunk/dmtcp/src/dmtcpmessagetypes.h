/***************************************************************************
 *   Copyright (C) 2006 by Jason Ansel                                     *
 *   jansel@ccs.neu.edu                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#ifndef DMTCPMESSAGETYPES_H
#define DMTCPMESSAGETYPES_H

#include "uniquepid.h"
#include "jassert.h"
#include "constants.h"
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "connectionidentifier.h"

namespace dmtcp
{

  enum DmtcpMessageType
  {
    DMT_NULL,
    DMT_HELLO_PEER,   //on connect established peer-peer               1
    DMT_HELLO_COORDINATOR, //on connect established worker-coordinator  2
    DMT_HELLO_WORKER, //on connect established coordinator-coordinator

    DMT_DO_SUSPEND, // when coordinator wants slave to suspend             4
    DMT_DO_RESUME,// when coordinator wants slave to resume (after checkpoint)
    DMT_DO_LOCK_FDS, // when coordinator wants lock fds
    DMT_DO_DRAIN, // when coordinator wants slave to flush
    DMT_DO_CHECKPOINT,// when coordinator wants slave to checkpoint
    DMT_DO_REFILL,// when coordinator wants slave to refill buffers

    DMT_RESTORE_RECONNECTED, //sent to peer on reconnect
    DMT_RESTORE_WAITING, //announce the existance of a restoring server on network
//        DMT_RESTORE_SEARCHING, //slave waiting wanting to know where to connect to

    DMT_PEER_ECHO,      //used to get a peer to echo back a buffer at you param[0] is len
                        //this is used to refill buffers after checkpointing
    DMT_OK,             //slave telling coordinator it is done (response to DMT_DO_*)
                        //this means slave reached barrier
    DMT_CKPT_FILENAME,  //a slave sending it's checkpoint filename to coordinator
    DMT_FORCE_RESTART,  //force a restart even if not all sockets are reconnected
    DMT_KILL_PEER,      // send kill message to peer
    DMT_USER_CMD,       // simulate typing params[0] into coordinator
    DMT_USER_CMD_RESULT // return code of user command
  };

  class WorkerState
  {
    public:
      enum eWorkerState
      {
        UNKNOWN,
        RUNNING,
        SUSPENDED,
        LOCKED,
        DRAINED,
        RESTARTING,
        CHECKPOINTED,
        REFILLED,
        _MAX
      };
      WorkerState ( eWorkerState s = UNKNOWN ) : _state ( s ) {}

      static void setCurrentState ( const dmtcp::WorkerState& theValue );
      static dmtcp::WorkerState currentState();

      eWorkerState value() const;

      bool operator== ( const WorkerState& v ) const{return _state == v.value();}
      bool operator!= ( const WorkerState& v ) const{return _state != v.value();}

      const char* toString() const;
    private:
      eWorkerState _state;
  };

  struct UniquePidConId
  {
    UniquePid id;
    int       conId;
    UniquePidConId() : conId ( -1 ) {}
    bool operator== ( const UniquePidConId& that ) const
    {
      return id==that.id && conId==that.conId;
    }
  };

#define DMTCPMESSAGE_NUM_PARAMS 2

  struct DmtcpMessage
  {
    char _magicBits[16];
    int  _msgSize;
    DmtcpMessageType type;
    ConnectionIdentifier from;
//         UniquePidConId to;

    UniquePid   coordinator;
    WorkerState state;


    ConnectionIdentifier    restorePid;
    struct sockaddr_storage restoreAddr;
    socklen_t               restoreAddrlen;
    int                     restorePort;

    //message type specific parameters
    int params[DMTCPMESSAGE_NUM_PARAMS];

    //extraBytes are used for passing checkpoint filename to coordinator it must be zero in all messages except for in DMT_CKPT_FILENAME
    int extraBytes;

    static void setDefaultCoordinator ( const UniquePid& id );
    DmtcpMessage ( DmtcpMessageType t = DMT_NULL );
    void assertValid() const;
    void poison();
  };


  std::ostream& operator << ( std::ostream& o, const WorkerState& s );



}//namespace dmtcp



#endif



