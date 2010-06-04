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

#ifndef DMTCPDMTCPWORKER_H
#define DMTCPDMTCPWORKER_H

#include "dmtcpalloc.h"
#include  "../jalib/jsocket.h"
#include "../jalib/jalloc.h"
#include "uniquepid.h"
#include "constants.h"
#include "dmtcpmessagetypes.h"

#define WRAPPER_EXECUTION_LOCK_LOCK() \
  /*JTRACE("Acquiring wrapperExecutionLock");*/ \
  bool __wrapperExecutionLockAcquired = dmtcp::DmtcpWorker::wrapperExecutionLockLock(); \
  if ( __wrapperExecutionLockAcquired ) { \
    /*JTRACE("Acquired wrapperExecutionLock");*/ \
  }

#define WRAPPER_EXECUTION_LOCK_UNLOCK() \
  if ( __wrapperExecutionLockAcquired ) { \
    /*JTRACE("Releasing wrapperExecutionLock");*/ \
    dmtcp::DmtcpWorker::wrapperExecutionLockUnlock(); \
  }

namespace dmtcp
{

  class ConnectionState;

#ifdef EXTERNAL_SOCKET_HANDLING
  class TcpConnectionInfo {
    public:
      TcpConnectionInfo (const ConnectionIdentifier& id, 
                       socklen_t& len, 
                       struct sockaddr_storage& remote, 
                       struct sockaddr_storage& local) {
        _conId      = id;
        _addrlen    = len;
        memcpy ( &_remoteAddr, &remote, len );
        memcpy ( &_localAddr, &local, len );
      }

    ConnectionIdentifier&  conId() { return _conId; }
    socklen_t addrlen() { return _addrlen; }
    struct sockaddr_storage remoteAddr() { return _remoteAddr; }
    struct sockaddr_storage localAddr() { return _localAddr; }

    ConnectionIdentifier    _conId;
    socklen_t               _addrlen;
    struct sockaddr_storage _remoteAddr;
    struct sockaddr_storage _localAddr;
  };
#endif

  class DmtcpWorker
  {
    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif
      static DmtcpWorker& Instance();
      static const int ld_preload_c_len = 256;
      static char ld_preload_c[ld_preload_c_len];
      const dmtcp::UniquePid& coordinatorId() const;

      void waitForCoordinatorMsg(dmtcp::string signalStr,
                                 DmtcpMessageType type);
      void sendCkptFilenameToCoordinator();
      void waitForStage1Suspend();
#ifdef EXTERNAL_SOCKET_HANDLING
      bool waitForStage2Checkpoint();
      bool waitForStage2bCheckpoint();
      void sendPeerLookupRequest(dmtcp::vector<TcpConnectionInfo>& conInfoTable );
      static bool waitingForExternalSocketsToClose();
#else
      void waitForStage2Checkpoint();
#endif
      void waitForStage3Refill();
      void waitForStage4Resume();
      void restoreVirtualPidTable();
      void restoreSockets(ConnectionState& coordinator,
                          UniquePid compGroup,
                          int numPeers,
                          int &coordTstamp);
      void postRestart();



      static void resetOnFork();
      void CleanupWorker();

      DmtcpWorker ( bool shouldEnableCheckpointing );
      ~DmtcpWorker();

      void connectAndSendUserCommand(char c, int* result = NULL);

      void useAlternateCoordinatorFd();

      static void maskStdErr();
      static void unmaskStdErr();
      static bool isStdErrMasked() { return _stdErrMasked; }

      static void delayCheckpointsLock();
      static void delayCheckpointsUnlock();

      static bool wrapperExecutionLockLock();
      static void wrapperExecutionLockUnlock();

      static void waitForThreadsToFinishInitialization();
      static void incrementUnInitializedThreadCount();
      static void decrementUnInitializedThreadCount();
      static void setExitInProgress() { _exitInProgress = true; };
      static bool exitInProgress() { return _exitInProgress; };
      void interruptCkpthread();

      bool connectToCoordinator(bool dieOnError=true);
      bool tryConnectToCoordinator();
      void connectToCoordinatorWithoutHandshake();
      void connectToCoordinatorWithHandshake();
      // np > -1  means it is restarting process that have np processes in its
      //           computation group
      // np == -1 means it is new pure process, so coordinator needs to
      //           generate compGroup ID for it
      // np == -2 means it is service connection from dmtcp_restart - irnore it
      void sendCoordinatorHandshake(const dmtcp::string& procName, 
                                    UniquePid compGroup = UniquePid(),
                                    int np = -1, 
                                    DmtcpMessageType msgType = DMT_HELLO_COORDINATOR);
      void recvCoordinatorHandshake(int *param1 = NULL);

      void writeCheckpointPrefix(int fd);
      void writeTidMaps();

      enum {
        COORD_JOIN      = 0x0001,
        COORD_NEW       = 0x0002,
        COORD_FORCE_NEW = 0x0004,
        COORD_BATCH     = 0x0008,
        COORD_ANY       = COORD_JOIN | COORD_NEW 
      };
      static void startCoordinatorIfNeeded(int modes, int isRestart=0);
      static void startNewCoordinator(int modes, int isRestart=0);

    protected:
      void sendUserCommand(char c, int* result = NULL);
    private:
      static DmtcpWorker theInstance;
    private:
      jalib::JSocket _coordinatorSocket;
      UniquePid      _coordinatorId;
      jalib::JSocket _restoreSocket;
      static bool _stdErrMasked;// = false;
      static bool _stdErrClosed;
      static bool _exitInProgress;
  };

}

#endif
