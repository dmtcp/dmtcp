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

#ifndef DMTCPMESSAGETYPES_H
#define DMTCPMESSAGETYPES_H

#include "dmtcpalloc.h"
#include "uniquepid.h"
#include "../jalib/jassert.h"
#include "../jalib/jalloc.h"
#include "constants.h"

namespace dmtcp
{

  enum DmtcpMessageType
  {
    DMT_NULL,
    DMT_HELLO_COORDINATOR,   // on connect established worker-coordinator
    DMT_HELLO_WORKER,        // on connect established coordinator-worker
    DMT_UPDATE_PROCESS_INFO_AFTER_FORK,

    DMT_GET_VIRTUAL_PID,
    DMT_GET_VIRTUAL_PID_RESULT,

    DMT_USER_CMD,            // on connect established dmtcp_command -> coordinator
    DMT_USER_CMD_RESULT,     // on reply coordinator -> dmtcp_command

    DMT_RESTART_PROCESS,     // on connect established dmtcp_restart -> coordinator
    DMT_RESTART_PROCESS_REPLY,  // on reply coordinator -> dmtcp_restart

    DMT_DO_SUSPEND,          // when coordinator wants slave to suspend        8
    DMT_DO_RESUME,           // when coordinator wants slave to resume (after checkpoint)
    DMT_DO_FD_LEADER_ELECTION, // when coordinator wants slaves to do leader election

    DMT_DO_DRAIN,            // when coordinator wants slave to flush
    DMT_DO_CHECKPOINT,       // when coordinator wants slave to checkpoint

#ifdef COORD_NAMESERVICE
    DMT_DO_REGISTER_NAME_SERVICE_DATA,
    DMT_DO_SEND_QUERIES,
#endif

    DMT_DO_REFILL,           // when coordinator wants slave to refill buffers

//#ifdef COORD_NAMESERVICE
    DMT_REGISTER_NAME_SERVICE_DATA,
    DMT_NAME_SERVICE_QUERY,
    DMT_NAME_SERVICE_QUERY_RESPONSE,
//#endif

    DMT_OK,                  // slave telling coordinator it is done (response
                             //   to DMT_DO_*)  this means slave reached barrier
    DMT_CKPT_FILENAME,       // a slave sending it's checkpoint filename to coordinator
    DMT_FORCE_RESTART,       // force a restart even if not all sockets are reconnected

    DMT_KILL_PEER,           // send kill message to peer
    DMT_REJECT               // coordinator discards incoming connection
                             //because it is not from current computation group


  };

  dmtcp::ostream& operator << ( dmtcp::ostream& o, const DmtcpMessageType& s );

  class WorkerState
  {
    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif
      enum eWorkerState
      {
        UNKNOWN,
        PRE_FORK,
        PRE_EXEC,
        RUNNING,
        SUSPENDED,
        FD_LEADER_ELECTION,
        DRAINED,
        RESTARTING,
        CHECKPOINTED,
#ifdef COORD_NAMESERVICE
        NAME_SERVICE_DATA_REGISTERED,
        DONE_QUERYING,
#endif
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
#define DMTCPMESSAGE_SAME_CKPT_INTERVAL (-1) /* default value */

  struct DmtcpMessage
  {
    char _magicBits[16];
    int  _msgSize;
    DmtcpMessageType type;
    UniquePid from;

    DmtcpUniqueProcessId   coordinator;
    WorkerState state;
    UniquePid   compGroup;
    pid_t       virtualPid;

//#ifdef COORD_NAMESERVICE
    size_t                  keyLen;
    size_t                  valLen;
//#endif

    int theCheckpointInterval;
    char coordCmd;
    time_t coordTimeStamp;
    int numPeers;
    int isRunning;
    int coordErrorCode;

    //extraBytes are used for passing checkpoint filename to coordinator it
    //must be zero in all messages except for in DMT_CKPT_FILENAME
    size_t extraBytes;

    static void setDefaultCoordinator ( const DmtcpUniqueProcessId& id );
    static void setDefaultCoordinator ( const UniquePid& id );
    DmtcpMessage ( DmtcpMessageType t = DMT_NULL );
    void assertValid() const;
    bool isValid() const;
    void poison();
  };


  dmtcp::ostream& operator << ( dmtcp::ostream& o, const WorkerState& s );



}//namespace dmtcp



#endif



