/*****************************************************************************
 * Copyright (C) 2014 Kapil Arya <kapil@ccs.neu.edu>                         *
 * Copyright (C) 2014 Gene Cooperman <gene@ccs.neu.edu>                      *
 *                                                                           *
 * DMTCP is free software: you can redistribute it and/or                    *
 * modify it under the terms of the GNU Lesser General Public License as     *
 * published by the Free Software Foundation, either version 3 of the        *
 * License, or (at your option) any later version.                           *
 *                                                                           *
 * DMTCP is distributed in the hope that it will be useful,                  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of            *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
 * GNU Lesser General Public Licendmtcp/src/mtcp/mtcp_restart.cse for more details.                       *
 *                                                                           *
 * You should have received a copy of the GNU Lesser General Public          *
 * License along with DMTCP.  If not, see <http://www.gnu.org/licenses/>.    *
 *****************************************************************************/

#define _GNU_SOURCE 1

#include "config.h"
#include "mtcp_header.h"
#include "mtcp_sys.h"
#include "mtcp_restart.h"
#include "mtcp_util.h"

#if defined(__powerpc64__) || defined(__ppc64__)
void main_new_stack(RestoreInfo *rinfoIn);
void main_new_stack_body(RestoreInfo *rinfoIn);
#else
static void main_new_stack(RestoreInfo *rinfoIn);
#endif

NO_OPTIMIZE
int
main(int argc, char *argv[], char **environ)
{
  int mtcp_sys_errno;

#if !defined(__powerpc64__) && !defined(__ppc64__)
  mtcp_printf("TRACE: entered main argc=%d argv=%p environ=%p argv0=%s main_new_stack=%p\n",
              argc, argv, environ,
              (argc > 0 && argv != NULL && argv[0] != NULL) ? argv[0] : "(null)",
              &main_new_stack);
#endif

  mtcp_restart_process_args(argc, argv, environ, &main_new_stack);
  MTCP_ASSERT(0);
}

#if defined(__powerpc64__) || defined(__ppc64__)
void main_new_stack_body(RestoreInfo *rinfo)
#else
static void main_new_stack(RestoreInfo *rinfo)
#endif
{
  int mtcp_sys_errno;

#if !defined(__powerpc64__) && !defined(__ppc64__)
  mtcp_printf("TRACE: entered main_new_stack rinfo=%p fd=%d restore_func=%p stack_offset=%p old_stack=%p new_stack=%p\n",
              rinfo, rinfo ? rinfo->fd : -1,
              rinfo ? rinfo->restore_func : 0,
              rinfo ? rinfo->stack_offset : 0,
              rinfo ? rinfo->old_stack_addr : 0,
              rinfo ? rinfo->new_stack_addr : 0);
#endif

  MTCP_ASSERT(rinfo->fd != -1);

  int rc = mtcp_readfile(rinfo->fd, &rinfo->ckptHdr, sizeof (rinfo->ckptHdr));
#if !defined(__powerpc64__) && !defined(__ppc64__)
  mtcp_printf("TRACE: main_new_stack read ckpt header rc=%d signature=%.16s\n",
              rc, rinfo->ckptHdr.ckptSignature);
  mtcp_printf("TRACE: main_new_stack restoreBuf=%p-%p\n",
              rinfo->ckptHdr.restoreBuf.startAddr,
              rinfo->ckptHdr.restoreBuf.endAddr);
#endif
#if defined(__powerpc64__) || defined(__ppc64__)
  if (rc != sizeof (rinfo->ckptHdr)) {
    mtcp_abort();
  }
#else
  MTCP_ASSERT(rc == sizeof (rinfo->ckptHdr));
#endif

  mtcp_restart(rinfo);
}
