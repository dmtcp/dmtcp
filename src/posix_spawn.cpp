#include <errno.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>

#include "dmtcp.h"
#include "jassert.h"
#include "syscallwrappers.h"

#define SPAWN_ERROR 127

enum __spawn_action_type { spawn_do_close, spawn_do_dup2, spawn_do_open };

/* This data structure is internal to glibc. We should create wrappers arround
 * posix_spawn_file_actions_addopen, etc., to capture the state transparently */
struct __spawn_action {
  __spawn_action_type tag;

  union {
    struct {
      int fd;
    } close_action;
    struct {
      int fd;
      int newfd;
    } dup2_action;
    struct {
      int fd;
      char *path;
      int oflag;
      mode_t mode;
    } open_action;
  } action;
};

/* Spawn a new process executing PATH with the attributes describes in *ATTRP.
   Before running the process perform the actions described in FILE-ACTIONS. */
int
dmtcp_spawn(pid_t *pid,
            const char *file,
            const posix_spawn_file_actions_t *file_actions,
            const posix_spawnattr_t *attrp,
            char *const argv[],
            char *const envp[])
{
  /* Do this once.  */
  short int flags = attrp == NULL ? 0 : attrp->__flags;

  // TODO(kapil): Add support for POSIX_SPAWN_USEVFORK.
  pid_t new_pid = fork();

  // Parent.
  if (new_pid != 0) {
    if (new_pid < 0) {
      return errno;
    }

    /* The call was successful.  Store the PID if necessary.  */
    if (pid != NULL) {
      *pid = new_pid;
    }

    return 0;
  }

  /* Set signal mask.  */
  if ((flags & POSIX_SPAWN_SETSIGMASK) != 0 &&
      sigprocmask(SIG_SETMASK, &attrp->__ss, NULL) != 0) {
    exit(SPAWN_ERROR);
  }

  /* Set signal default action.  */
  if ((flags & POSIX_SPAWN_SETSIGDEF) != 0) {
    struct sigaction sa = {};
    sa.sa_handler = SIG_DFL;

    int ckptSig = dmtcp_get_ckpt_signal();
    for (int sig = 1; sig < _NSIG; ++sig) {
      if (sig != ckptSig && sigismember(&attrp->__sd, sig) != 0 &&
          sigaction(sig, &sa, NULL) != 0) {
        JTRACE("sigaction failed");
        exit(SPAWN_ERROR);
      }
    }
  }

  /* Set the process group ID.  */
  if ((flags & POSIX_SPAWN_SETPGROUP) != 0 && setpgid(0, attrp->__pgrp) != 0) {
    JTRACE("setpgid failed");
    exit(SPAWN_ERROR);
  }

  /* Set the effective user and group IDs.  */
  if ((flags & POSIX_SPAWN_RESETIDS) != 0 &&
      (seteuid(getuid()) != 0 || setegid(getgid()) != 0)) {
    JTRACE("setegid failed");
    exit(SPAWN_ERROR);
  }

  /* Execute the file actions.  */
  if (file_actions != NULL) {
    for (int cnt = 0; cnt < file_actions->__used; ++cnt) {
      struct __spawn_action *action = &file_actions->__actions[cnt];

      switch (action->tag) {
        case spawn_do_close:
          if (close(action->action.close_action.fd) != 0) {
            JTRACE("Close failed");
          }
          break;

        case spawn_do_open: {
          int new_fd = open(action->action.open_action.path,
                            action->action.open_action.oflag | O_LARGEFILE,
                            action->action.open_action.mode);

          if (new_fd == -1) {
            /* The `open' call failed.  */
            JTRACE("open failed");
            exit(SPAWN_ERROR);
          }

          /* Make sure the desired file descriptor is used.  */
          if (new_fd != action->action.open_action.fd) {
            if (dup2(new_fd, action->action.open_action.fd) !=
                action->action.open_action.fd) {
              /* The `dup2' call failed.  */
              JTRACE("dup failed");
              exit(SPAWN_ERROR);
            }

            if (close(new_fd) != 0) {
              /* The `close' call failed.  */
              JTRACE("close failed");
              exit(SPAWN_ERROR);
            }
          }
        } break;

        case spawn_do_dup2:
          if (dup2(action->action.dup2_action.fd,
                   action->action.dup2_action.newfd) !=
              action->action.dup2_action.newfd) {
            /* The `dup2' call failed.  */
            JTRACE("dup2 failed");
            exit(SPAWN_ERROR);
          }
          break;
      }
    }
  }

  int ret = execve(file, argv, envp);

  exit(SPAWN_ERROR);

  return ret; // to suppress compiler warning.
}

extern "C" int
posix_spawn(pid_t *pid,
            const char *path,
            const posix_spawn_file_actions_t *file_actions,
            const posix_spawnattr_t *attrp,
            char *const argv[],
            char *const envp[])
{
  return dmtcp_spawn(pid, path, file_actions, attrp, argv, envp);
}

extern "C" int
posix_spawnp(pid_t *pid,
             const char *file,
             const posix_spawn_file_actions_t *file_actions,
             const posix_spawnattr_t *attrp,
             char *const argv[],
             char *const envp[])
{
  return dmtcp_spawn(pid, file, file_actions, attrp, argv, envp);
}
