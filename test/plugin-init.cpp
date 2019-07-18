//File: dmtcp_init_issue.cpp
#include <iostream>
#include <sys/types.h>
#include <unistd.h>
#include "dmtcp.h"
void dmtcp_event_hook(DmtcpEvent_t event, DmtcpEventData_t* data)
{
  switch (event)
  {
    case DMTCP_EVENT_INIT:
    {
      /* This next line caused a crash in DMTCP 2.5.2 using g++-6.2.
       * The call to std::cout invokes libstdc++.so, which calls malloc(),
       * which calls the DMTCP wrapper for malloc.  The DMTCP malloc()
       * wrapper then tries to initialize DMTCP before the DmtcpWorker
       * constructor, and therefore even before libc:malloc() can
       * be initialized.  At this early stage, DMTCP:malloc() should
       * simply call libc.so:malloc(), without trying to initialize DMTCP
       * (which is fixed in DMTCP-2.6.0 and beyond).
       */
      std::cout << "DMTCP_EVENT_INIT, PID=" << getpid() << std::endl ;
      break ;
    }
    default:
    {
      break ;
    }
  }
}

int main(int argc, char* argv[])
{
  // dmtcp_checkpoint() ;
  while (1);
}
