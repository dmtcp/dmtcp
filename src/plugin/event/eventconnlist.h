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

    static void resumeRefill() { instance().refill(false); }

    static void restart() { instance().postRestart(); }

    static void restartRefill() { instance().refill(true); }

    virtual int protectedFd() override { return PROTECTED_EVENT_FDREWIRER_FD; }

    virtual Connection *createDummyConnection(int type) override;

    virtual ConnectionList *cloneInstance() override
    {
      return new EventConnList(*this);
    }
};
}
#endif // ifndef EVENT_CONN_LIST_H
