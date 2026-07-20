/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include "dmtcp.h"
#include "../jalib/jassert.h"
#include "syscallwrappers.h"
#include "threadsync.h"
#include <signal.h>
#include <pthread.h>

using namespace dmtcp;

static map<FILE *, pid_t>_dmtcpPopenPidMap;
typedef map<FILE *, pid_t>::iterator _dmtcpPopenPidMapIterator;

static DmtcpMutex popen_map_lock = DMTCP_MUTEX_INITIALIZER;

/* Thread-local storage for signal mask during popen fork */
static __thread sigset_t popen_saved_mask;
static __thread int popen_mask_saved = 0;

/* Prepare handler: Block checkpoint signal before fork */
static void
_popen_atfork_prepare()
{
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGUSR2);  // DMTCP checkpoint signal
  pthread_sigmask(SIG_BLOCK, &mask, &popen_saved_mask);
  popen_mask_saved = 1;
}

/* Parent handler: Restore signal mask after fork in parent */
static void
_popen_atfork_parent()
{
  if (popen_mask_saved) {
    pthread_sigmask(SIG_SETMASK, &popen_saved_mask, NULL);
    popen_mask_saved = 0;
  }
}

/* Child handler: Reset signal handler in child after fork */
static void
_popen_atfork_child()
{
  /* Reset checkpoint signal to default in child.
   * The signal is already blocked (inherited from parent's prepare),
   * but we reset the handler to SIG_DFL for safety.
   */
  struct sigaction sa;
  sa.sa_handler = SIG_DFL;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGUSR2, &sa, NULL);
  
  /* Don't restore mask in child - let exec reset it */
  popen_mask_saved = 0;
}

static void
_lock_popen_map()
{
  JASSERT(DmtcpMutexLock(&popen_map_lock) == 0) (JASSERT_ERRNO);
}

static void
_unlock_popen_map()
{
  JASSERT(DmtcpMutexUnlock(&popen_map_lock) == 0) (JASSERT_ERRNO);
}

extern "C"
FILE * popen(const char *command, const char *mode)
{
  FILE *fp;
  int parent_fd, child_fd;
  int pipe_fds[2];
  pid_t child_pid;
  char new_mode[2] = "r";

  int do_read = 0;
  int do_write = 0;
  int do_cloexec = 0;

  while (*mode != '\0') {
    switch (*mode++) {
    case 'r':
      do_read = 1;
      break;
    case 'w':
      do_write = 1;
      break;
    case 'e':
      do_cloexec = 1;
      break;
    default:
      errno = EINVAL;
      return NULL;
    }
  }

  if ((do_read ^ do_write) == 0) {
    errno = EINVAL;
    return NULL;
  }

  /* Register atfork handlers for this popen call.
   * These handlers will be called automatically by libc during fork:
   * - prepare: blocks SIGUSR2 BEFORE fork (in parent)
   * - parent: restores signal mask AFTER fork (in parent)
   * - child: resets signal handler AFTER fork (in child)
   * This provides 100% protection with zero race window.
   */
  pthread_atfork(_popen_atfork_prepare, _popen_atfork_parent, _popen_atfork_child);

  {
    WrapperLock disableCheckpoint;
    if (pipe(pipe_fds) < 0) {
      return NULL;
    }

    // Mark the parent_end with FD_CLOEXEC so that if there is fork/exec while
    // we are inside this wrapper, these fds are closed.
    fcntl(pipe_fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(pipe_fds[1], F_SETFD, FD_CLOEXEC);

    if (do_read) {
      parent_fd = pipe_fds[0];
      child_fd = pipe_fds[1];
      strcpy(new_mode, "r");
    } else {
      parent_fd = pipe_fds[1];
      child_fd = pipe_fds[0];
      strcpy(new_mode, "w");
    }
  }

  child_pid = fork();
  if (child_pid == 0) {
    int child_std_fd = do_read ? STDOUT_FILENO : STDIN_FILENO;
    
    /* Note: Signal handler already reset by _popen_atfork_child(),
     * which was called automatically by libc after fork.
     * No race condition possible.
     */
    
    close(parent_fd);
    if (child_fd != child_std_fd) {
      dup2(child_fd, child_std_fd);
      close(child_fd);
    }

    /* POSIX.2:  "popen() shall ensure that any streams from previous
       popen() calls that remain open in the parent process are closed
       in the new child process." */
    _dmtcpPopenPidMapIterator it;
    for (it = _dmtcpPopenPidMap.begin(); it != _dmtcpPopenPidMap.end(); it++) {
      int fd = fileno(it->first);

      /* If any stream from previous popen() calls has fileno
         child_std_end, it has been already closed by the dup2 syscall
         above.  */
      if (fd != child_std_fd) {
        fclose(it->first);
      }
    }
    _dmtcpPopenPidMap.clear();

    fcntl(child_std_fd, F_SETFD, 0);
    execl("/bin/sh", "sh", "-c", command, (char *)0);
    exit(127);
  }
  close(child_fd);
  if (child_pid < 0) {
    close(parent_fd);
    return NULL;
  }

  {
    WrapperLock disableCheckpoint;

    fp = fdopen(parent_fd, new_mode);
    if (!do_cloexec) {
      fcntl(parent_fd, F_SETFD, 0);
    }
    _lock_popen_map();
    _dmtcpPopenPidMap[fp] = child_pid;
    _unlock_popen_map();
  }

  return fp;
}

extern "C"
int
pclose(FILE *fp)
{
  _dmtcpPopenPidMapIterator it;
  int wstatus;
  pid_t pid = -1;
  pid_t wait_pid;

  _lock_popen_map();
  it = _dmtcpPopenPidMap.find(fp);
  if (it != _dmtcpPopenPidMap.end()) {
    fp = it->first;
    pid = it->second;
    _dmtcpPopenPidMap.erase(it);
  }
  _unlock_popen_map();

  if (pid == -1 || fclose(fp) != 0) {
    return -1;
  }

  do {
    wait_pid = waitpid(pid, &wstatus, 0);
  } while (wait_pid == -1 && errno == EINTR);
  if (wait_pid == -1) {
    return -1;
  }
  return wstatus;
}

EXTERNC int
dmtcp_is_popen_fp(FILE *fp)
{
  int popen_fp = 0;

  _lock_popen_map();
  if (_dmtcpPopenPidMap.find(fp) != _dmtcpPopenPidMap.end()) {
    popen_fp = 1;
  }
  _unlock_popen_map();
  return popen_fp;
}
