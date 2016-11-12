#ifndef SLURM_HELPER_H
#define SLURM_HELPER_H

#include <sys/socket.h>
#include <sys/un.h>


// This is a copy of the code from src/plugin/ipc/utils_ipc.cpp and the SSH
// plugin.
// This code also must be shared between the ssh and batch-queue/rm plugins.

int slurm_sendFd(int restoreFd,
                 int32_t fd,
                 void *data,
                 size_t len,
                 struct sockaddr_un &addr,
                 socklen_t addrLen);
int32_t slurm_receiveFd(int restoreFd, void *data, size_t len);
#endif // SLURM_HELPER_H
