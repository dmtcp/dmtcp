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

static void main_new_stack(RestoreInfo *rinfoIn);

NO_OPTIMIZE
int
main(int argc, char *argv[], char **environ)
{
  int mtcp_sys_errno;

  mtcp_restart_process_args(argc, argv, environ, &main_new_stack);
  MTCP_ASSERT(0);
}

void main_new_stack(RestoreInfo *rinfo)
{
  int mtcp_sys_errno;
  MtcpHeader mtcpHdr;

  MTCP_ASSERT(rinfo->fd != -1);

  int rc = mtcp_readfile(rinfo->fd, &mtcpHdr, sizeof (mtcpHdr));
  MTCP_ASSERT(rc == sizeof (mtcpHdr));
  MTCP_ASSERT(mtcp_strcmp(mtcpHdr.signature, MTCP_SIGNATURE) == 0);

  mtcp_restart(rinfo, &mtcpHdr);
}
