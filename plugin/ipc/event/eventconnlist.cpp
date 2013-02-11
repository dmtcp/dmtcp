#include <unistd.h>
#include <sys/syscall.h>
#include "eventconnection.h"
#include "eventconnlist.h"

using namespace dmtcp;
void dmtcp_EventConnList_ProcessEvent(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  dmtcp::EventConnList::instance().processEvent(event, data);
}

void dmtcp_EventConn_ProcessFdEvent(int event, int arg1, int arg2)
{
  if (event == SYS_close) {
    EventConnList::instance().processClose(arg1);
  } else if (event == SYS_dup) {
    EventConnList::instance().processDup(arg1, arg2);
  } else {
    JASSERT(false);
  }
}


static dmtcp::EventConnList *eventConnList = NULL;
dmtcp::EventConnList& dmtcp::EventConnList::instance()
{
  if (eventConnList == NULL) {
    eventConnList = new EventConnList();
  }
  return *eventConnList;
}

Connection *dmtcp::EventConnList::createDummyConnection(int type)
{
  switch (type) {
    case Connection::EPOLL:
      return new EpollConnection(5); //dummy val
      break;
    case Connection::EVENTFD:
      return new EventFdConnection(0, 0); //dummy val
      break;
    case Connection::SIGNALFD:
      return new SignalFdConnection(0, NULL, 0); //dummy val
      break;
#ifdef DMTCP_USE_INOTIFY
    case Connection::INOTIFY:
      return new InotifyConnection(0);
      break;
#endif
  }
  return NULL;
}
