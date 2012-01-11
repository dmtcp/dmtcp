/* POSIX.1 `sigaction' call for Linux/x86-64.
   Copyright (C) 2001, 2002, 2003, 2005, 2006 Free Software Foundation, Inc.
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
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */

/* This is based on first half of: ./sysdeps/unix/sysv/linux/x86_64/sigaction.c
 *   from glibc-2.12
 * It is modified for MTCP.
 * It's needed to restore SIGSETXID and SIGCANCEL/SIGTIMER from glibc.
 */

#include <errno.h>
#include <stddef.h>
#include <signal.h>
#include <string.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <stdlib.h>
#define MTCP_SYS_MEMCPY
#include "mtcp_internal.h"  /* Needed for MTCP_PRINTF */
#include "mtcp_sys.h"

/* Defined and allocated in mtcp.c */
extern int dmtcp_exists;

//int __attribute__ ((weak))
//_real_sigaction (int sig, const struct sigaction *act, struct sigaction *oact) {
//  MTCP_PRINTF("This function should never be called when running with"
//              "DMTCP.\n");
//  mtcp_abort();
//}

/* The difference here is that the sigaction structure used in the
   kernel is not the same as we use in the libc.  Therefore we must
   translate it here.  */
// From glibc:./sysdeps/unix/sysv/linux/kernel_sigaction.h
struct kernel_sigaction {
        __sighandler_t k_sa_handler;
        unsigned long sa_flags;
        void (*sa_restorer) (void);
        sigset_t sa_mask;
};

/* We do not globally define the SA_RESTORER flag so do it here.  */
#define SA_RESTORER 0x04000000


extern int (*mtcp_sigaction_entry) (int sig, const struct sigaction *act,
                                struct sigaction *oact);
/* If ACT is not NULL, change the action for SIG to *ACT.
   If OACT is not NULL, put the old action for SIG in *OACT.  */
int __attribute__ ((visibility ("hidden")))
mtcp_sigaction (int sig, const struct sigaction *act,
			      struct sigaction *oact)
{
  int result;
  struct kernel_sigaction kact, koact;

  /* if sig != SIGCANCEL and sig != SIGSETXID, use glibc version */
  if (sig != 32 && sig != 33) {
    if (dmtcp_exists) /* then _real_sigaction defined by DMTCP; avoid wrapper */
      return mtcp_sigaction_entry(sig, act, oact);
    else /* this will go directly to glibc */
      return sigaction(sig, act, oact);
  }

  /* else make direct call to kernel;
   * glibc would hide signal 32 (SIGCANCEL/SIGTIMER) and 33 (SIGSETXID) */
  if (act)
    {
      kact.k_sa_handler = act->sa_handler;
      mtcp_sys_memcpy (&kact.sa_mask, &act->sa_mask, sizeof (sigset_t));
      kact.sa_flags = act->sa_flags | SA_RESTORER;
      /* If MTCP called this, then it must be for MTCP restart.  Hence, MTCP
       * had previously saved the restorer function.  act->sa_restorer is valid.
       */
      kact.sa_restorer = act->sa_restorer;
    }

  /* XXX The size argument hopefully will have to be changed to the
     real size of the user-level sigset_t.  */
  result = mtcp_sys_rt_sigaction( sig, act ? &kact : NULL,
			       oact ? &koact : NULL, _NSIG / 8);
  if (oact && result >= 0)
    {
      oact->sa_handler = koact.k_sa_handler;
      mtcp_sys_memcpy (&oact->sa_mask, &koact.sa_mask, sizeof (sigset_t));
      oact->sa_flags = koact.sa_flags;
      /* During checkpoint, MTCP calls this to save action for signals 32, 33
       * If glibc uses signals 32/33, it'll have already installed the restorer
       */
      oact->sa_restorer = koact.sa_restorer;
    }
  errno = mtcp_sys_errno;  /* needed for mtcp_sys_rt_sigaction */
  return result;
}
