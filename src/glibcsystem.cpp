/* Copyright (C) 1991-2000,2002,2003,2005,2007 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with DMTCP:dmtcp/src.  If not, see
   <http://www.gnu.org/licenses/>.  */

/*
 * This code is a simplified version of do_system() from glibc-2.9.
 * Its primary use is to be called by the wrapper, execwrappers.cpp:system().
 * This way, calls to system() go to our do_system(), which inherits our
 * wrappers around fork/exec.
 *   Without this, any call to system() would go directly to glibc, where
 * it would pick up the _unwrapped_ versions of fork/exec already defined
 * in glibc.  If you really want an unwrapped version of glibc's system(),
 * then call execwrappers.cpp:_real_system().
 */
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define  SHELL_PATH "/bin/sh"   /* Path of the shell.  */
#define  SHELL_NAME "sh"     /* Name to give it.  */

/* Execute LINE as a shell command, returning its status.  */
__attribute__((visibility("hidden")))
int
do_system(const char *line)
{
  int status, save;
  pid_t pid;
  struct sigaction sa;
  struct sigaction intr, quit;
  sigset_t omask;

  sa.sa_handler = SIG_IGN;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);

  if (sigaction(SIGINT, &sa, &intr) < 0) {
    goto out;
  }
  if (sigaction(SIGQUIT, &sa, &quit) < 0) {
    save = errno;
    goto out_restore_sigint;
  }

  /* We reuse the bitmap in the 'sa' structure.  */
  sigaddset(&sa.sa_mask, SIGCHLD);
  save = errno;
  if (sigprocmask(SIG_BLOCK, &sa.sa_mask, &omask) < 0) {
    if (errno == ENOSYS) {
      errno = save;
    } else {
      save = errno;
      (void)sigaction(SIGQUIT, &quit, (struct sigaction *)NULL);
out_restore_sigint:
      (void)sigaction(SIGINT, &intr, (struct sigaction *)NULL);
      errno = save;

      // set_errno (save);
out:
      return -1;
    }
  }

  pid = fork();
  if (pid == (pid_t)0) {
    /* Child side.  */
    const char *new_argv[4];
    new_argv[0] = SHELL_NAME;
    new_argv[1] = "-c";
    new_argv[2] = line;
    new_argv[3] = NULL;

    /* Restore the signals.  */
    (void)sigaction(SIGINT, &intr, (struct sigaction *)NULL);
    (void)sigaction(SIGQUIT, &quit, (struct sigaction *)NULL);
    (void)sigprocmask(SIG_SETMASK, &omask, (sigset_t *)NULL);

    /* Exec the shell.  */
    (void)execve(SHELL_PATH, (char *const *)new_argv, __environ);
    _exit(127);
  } else if (pid < (pid_t)0) {
    /* The fork failed.  */
    status = -1;
  } else {
    /* Parent side.  */

    /* Note that system() is a cancellation point.  But since we call
       waitpid() which itself is a cancellation point we do not
       have to do anything here.  */
    do {
      if (TEMP_FAILURE_RETRY(waitpid(pid, &status, 0)) != pid) {
        status = -1;
        if (status == -1 && errno == ECHILD) {
          break;
        }
      }
    } while (WIFEXITED(status) == 0);
  }

  save = errno;
  if (((sigaction(SIGINT, &intr, (struct sigaction *)NULL)
        | sigaction(SIGQUIT, &quit, (struct sigaction *)NULL)) != 0)
      || sigprocmask(SIG_SETMASK, &omask, (sigset_t *)NULL) != 0) {
    /* glibc cannot be used on systems without waitpid.  */
    if (errno == ENOSYS) {
      errno = save;
    } else {
      status = -1;
    }
  }

  return status;
}
