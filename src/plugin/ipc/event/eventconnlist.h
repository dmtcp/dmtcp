#pragma once
#ifndef EVENT_CONN_LIST_H
# define EVENT_CONN_LIST_H

# include "connectionlist.h"

namespace dmtcp
{
class EventConnList : public ConnectionList
{
  public:
    static EventConnList &instance();

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

    virtual int protectedFd() { return PROTECTED_EVENT_FDREWIRER_FD; }

    virtual Connection *createDummyConnection(int type);
};
}
#endif // ifndef EVENT_CONN_LIST_H
