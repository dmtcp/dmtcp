#pragma once
#ifndef SOCKETCONNLIST_H
#define SOCKETCONNLIST_H

// THESE INCLUDES ARE IN RANDOM ORDER.  LET'S CLEAN IT UP AFTER RELEASE. - Gene
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include "jbuffer.h"
#include "socketconnection.h"
#include "connectionlist.h"

namespace dmtcp
{
  class SocketConnList : public ConnectionList
  {
    public:
      static SocketConnList& instance();

      static void saveOptions() { instance().preLockSaveOptions(); }
      static void leaderElection() { instance().preCkptFdLeaderElection(); }
      static void drainFd() { instance().drain(); }
      static void ckpt() { instance().preCkpt(); }

      static void resumeRefill() { instance().refill(false); }
      static void resumeResume() { instance().resume(false); }

      static void restart() { instance().postRestart(); }
      static void restartRegisterNSData() { instance().registerNSData(); }
      static void restartSendQueries() { instance().sendQueries(); }
      static void restartRefill() { instance().refill(true); }
      static void restartResume() { instance().resume(true); }

      virtual void drain();
      virtual void preCkpt();
      virtual void postRestart();
      virtual void registerNSData();
      virtual void sendQueries();
      virtual void refill(bool isRestart);

      virtual int protectedFd() { return PROTECTED_SOCKET_FDREWIRER_FD; }
      virtual void scanForPreExisting();
      virtual Connection *createDummyConnection(int type);
  };
}

#endif
