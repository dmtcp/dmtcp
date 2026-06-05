//File: dmtcp_init_issue.cpp
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <sys/types.h>
#include <unistd.h>
#include "dmtcp.h"

extern "C" char *dmtcp_get_thread_assert_buffer(size_t *size)
  __attribute__((weak));

static void
verifyAssertBufferAvailableAtInit()
{
  static const size_t kExpectedAssertBufferSize = 4096;

  if (dmtcp_get_thread_assert_buffer == nullptr) {
    std::cerr << "dmtcp_get_thread_assert_buffer is unavailable" << std::endl;
    std::exit(2);
  }

  size_t size = 0;
  char *buffer = dmtcp_get_thread_assert_buffer(&size);
  if (buffer == nullptr || size != kExpectedAssertBufferSize) {
    std::cerr << "unexpected DMTCP assert buffer at INIT: buffer=" << buffer
              << " size=" << size << std::endl;
    std::exit(3);
  }

  buffer[0] = '\0';
  if (buffer[0] != '\0') {
    std::cerr << "DMTCP assert buffer is not writable at INIT" << std::endl;
    std::exit(4);
  }
}

void dmtcp_event_hook(DmtcpEvent_t event, DmtcpEventData_t* data)
{
  switch (event)
  {
    case DMTCP_EVENT_INIT:
    {
      verifyAssertBufferAvailableAtInit();

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
