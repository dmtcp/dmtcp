#pragma once
#ifndef EVENT_CONN_LIST_H
#define EVENT_CONN_LIST_H

#include "connectionlist.h"

namespace dmtcp
{
  class EventConnList : public ConnectionList
  {
    public:
      virtual int protectedFd() { return PROTECTED_EVENT_FDREWIRER_FD; }
      virtual Connection *createDummyConnection(int type);
      static EventConnList& instance();
  };
}

#endif
