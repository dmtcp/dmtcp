#ifndef SLURM_HELPER_H
#define SLURM_HELPER_H

#include <sys/socket.h>
#include <sys/un.h>

#define DMTCP_SLURM_HELPER_ADDR_ENV "DMTCP_SRUN_HELPER_ADDR"

//-------------------------------------8<------------------------------------------------//
// This is a copy of the code from src/plugin/ipc/utils_ipc.cpp and SSH plugin
// This code also must be shared between ssh ans rm plugins.

int slurm_sendFd(int restoreFd, int32_t fd, void *data, size_t len,
           struct sockaddr_un& addr, socklen_t addrLen);
int32_t slurm_receiveFd(int restoreFd, void *data, size_t len);


#endif // SLURM_HELPER_H
