#pragma once
#ifndef SSHDRAINER_H
#define SSHDRAINER_H

#include "dmtcpalloc.h"
#include "../jalib/jsocket.h"

namespace dmtcp
{

  class SSHDrainer : public jalib::JMultiSocketProgram
  {
    public:
      SSHDrainer() : _timeoutCount(0) {}
      static SSHDrainer& instance();

      void beginDrainOf(int fd, int refillfd = -1);
      void refill();
      virtual void onData(jalib::JReaderInterface* sock);
      virtual void onConnect(const jalib::JSocket&, const struct sockaddr*,
                             socklen_t remoteLen);
      virtual void onTimeoutInterval();
      virtual void onDisconnect(jalib::JReaderInterface* sock);

    private:
      dmtcp::map<int, dmtcp::vector<char> >  _drainedData;
      dmtcp::map<int, int> _refillFd;
      int _timeoutCount;
  };

}

#endif
