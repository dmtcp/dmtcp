#ifndef UTIL_SSH_H
#define UTIL_SSH_H

#include <sys/socket.h>
#include <sys/un.h>
#include "ipc.h"
#include "dmtcpplugin.h"

extern "C" LIB_PRIVATE
int sendFd(int restoreFd, int fd, void *data, size_t len,
            struct sockaddr_un& addr, socklen_t addrLen) __attribute((weak));
extern "C" LIB_PRIVATE
int receiveFd(int restoreFd, void *data, size_t len) __attribute((weak));

#endif
