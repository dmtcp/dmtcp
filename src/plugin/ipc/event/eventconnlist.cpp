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
    dmtcp_global_barrier("Event::PRE_CKPT");
    EventConnList::leaderElection();
    dmtcp_global_barrier("Event::LEADER_ELECTION");
    EventConnList::drainFd();
    dmtcp_global_barrier("Event::DRAIN");
    EventConnList::ckpt();
    break;

  case DMTCP_EVENT_RESUME:
    EventConnList::resumeRefill();
    dmtcp_global_barrier("Event::RESUME_REFILL");
    EventConnList::resumeResume();
    break;

  case DMTCP_EVENT_RESTART:
    EventConnList::restart();
    dmtcp_global_barrier("Event::RESTART_POST_RESTART");
    EventConnList::restartRegisterNSData();
    dmtcp_global_barrier("Event::RESTART_NS_REGISTER_DATA");
    EventConnList::restartSendQueries();
    dmtcp_global_barrier("Event::RESTART_NS_SEND_QUERIES");
    EventConnList::restartRefill();
    dmtcp_global_barrier("Event::RESTART_REFILL");
    EventConnList::restartResume();
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
