#include "eventconnlist.h"
#include <sys/syscall.h>
#include <unistd.h>
#include "eventconnection.h"

using namespace dmtcp;
void
dmtcp_EventConnList_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  EventConnList::instance().eventHook(event, data);

  switch (event) {
  case DMTCP_EVENT_PRESUSPEND:
    break;

  case DMTCP_EVENT_PRECHECKPOINT:
    EventConnList::saveOptions();
    dmtcp_local_barrier("Event::PRE_CKPT");
    EventConnList::leaderElection();
    dmtcp_local_barrier("Event::LEADER_ELECTION");
    EventConnList::drainFd();
    break;

  case DMTCP_EVENT_RESUME:
    EventConnList::resumeRefill();
    break;

  case DMTCP_EVENT_RESTART:
    EventConnList::restart();
    dmtcp_local_barrier("Event::RESTART_POST_RESTART");
    EventConnList::restartRefill();
    break;

  default:  // other events are not registered
    break;
  }
}


DmtcpPluginDescriptor_t eventPlugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "event",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Event plugin",
  dmtcp_EventConnList_EventHook
};

void
ipc_initialize_plugin_event()
{
  dmtcp_register_plugin(eventPlugin);
}

void
dmtcp_EventConn_ProcessFdEvent(int event, int arg1, int arg2)
{
  if (event == SYS_close) {
    EventConnList::instance().processClose(arg1);
  } else if (event == SYS_dup) {
    EventConnList::instance().processDup(arg1, arg2);
  } else {
    JASSERT(false);
  }
}

static EventConnList *eventConnList = NULL;
EventConnList&
EventConnList::instance()
{
  if (eventConnList == NULL) {
    eventConnList = new EventConnList();
  }
  return *eventConnList;
}

Connection *
EventConnList::createDummyConnection(int type)
{
  switch (type) {
#ifdef HAVE_SYS_EPOLL_H
  case Connection::EPOLL:
    return new EpollConnection();   // dummy val

    break;
#endif // ifdef HAVE_SYS_EPOLL_H

#ifdef HAVE_SYS_EVENTFD_H
  case Connection::EVENTFD:
    return new EventFdConnection(0, 0);   // dummy val

    break;
#endif // ifdef HAVE_SYS_EVENTFD_H

#ifdef HAVE_SYS_SIGNALFD_H
  case Connection::SIGNALFD:
    return new SignalFdConnection(0, NULL, 0);   // dummy val

    break;
#endif // ifdef HAVE_SYS_SIGNALFD_H

#ifdef HAVE_SYS_INOTIFY_H
# ifdef DMTCP_USE_INOTIFY
  case Connection::INOTIFY:
    return new InotifyConnection(0);

    break;
# endif // ifdef DMTCP_USE_INOTIFY
#endif // ifdef HAVE_SYS_INOTIFY_H
  }
  return NULL;
}
