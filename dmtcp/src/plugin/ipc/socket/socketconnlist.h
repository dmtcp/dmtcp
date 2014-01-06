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
      virtual void drain();
      virtual void preCkpt();
      virtual void postRestart();
      virtual void registerNSData(bool isRestart);
      virtual void sendQueries(bool isRestart);
      virtual void refill(bool isRestart);

      virtual int protectedFd() { return PROTECTED_SOCKET_FDREWIRER_FD; }
      static SocketConnList& instance();
      virtual void scanForPreExisting();
      virtual Connection *createDummyConnection(int type);
  };
}

#endif
