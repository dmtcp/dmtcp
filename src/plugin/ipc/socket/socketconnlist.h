#pragma once
#ifndef SOCKETCONNLIST_H
#define SOCKETCONNLIST_H

// THESE INCLUDES ARE IN RANDOM ORDER.  LET'S CLEAN IT UP AFTER RELEASE. - Gene
# include <signal.h>
# include <stdint.h>
# include <sys/socket.h>
# include <sys/stat.h>
# include <sys/types.h>
# include <sys/types.h>
# include <unistd.h>

# include "jbuffer.h"

# include "connectionlist.h"
# include "socketconnection.h"
namespace dmtcp
{
class SocketConnList : public ConnectionList
{
  public:
    static SocketConnList &instance();

    static void saveOptions() { instance().preLockSaveOptions(); }

    static void leaderElection() { instance().preCkptFdLeaderElection(); }

    // NS = Name Service
    static void ckptRegisterNSData() { instance().preCkptRegisterNSData(); }

    static void ckptSendQueries() { instance().preCkptSendQueries(); }

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

    void preCkptRegisterNSData();
    void preCkptSendQueries();

    virtual int protectedFd() { return PROTECTED_SOCKET_FDREWIRER_FD; }

    virtual void scanForPreExisting();
    virtual Connection *createDummyConnection(int type);
};
}
#endif // ifndef SOCKETCONNLIST_H
