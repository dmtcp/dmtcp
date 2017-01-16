#pragma once
#ifndef SSH_H
#define SSH_H

#include <sys/syscall.h>
#include "ipc.h"

# define _real_execve    NEXT_FNC(execve)
# define _real_execvp    NEXT_FNC(execvp)
# define _real_execvpe   NEXT_FNC(execvpe)

# define SSHD_BINARY     "dmtcp_sshd"
#define RSH_BINARY "rsh"
# define SSHD_RECEIVE_FD 100

extern "C" void dmtcp_ssh_register_fds(int isSshd,
                                       int in,
                                       int out,
                                       int err,
                                       int sock,
                                       int noStrictHostKeyChecking,
                                       int rshProcess)
__attribute((weak));

void client_loop(int ssh_stdin, int ssh_stdout, int ssh_stderr, int remoteSock);

void dmtcp_ssh_drain();
void dmtcp_ssh_resume();
void dmtcp_ssh_restart();
#endif // ifndef SSH_H
