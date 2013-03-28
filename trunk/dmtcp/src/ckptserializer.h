/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *   This file is part of the dmtcp/src module of DMTCP (DMTCP:dmtcp/src).  *
 *                                                                          *
 *  DMTCP:dmtcp/src is free software: you can redistribute it and/or        *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP:dmtcp/src is distributed in the hope that it will be useful,      *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#ifndef CKPT_SERIZLIZER_H
#define CKPT_SERIZLIZER_H

#include "constants.h"
#include "dmtcpalloc.h"
#include "processinfo.h"
#include "util.h"
#include "../jalib/jserialize.h"
#include "../jalib/jfilesystem.h"
#include "../jalib/jalloc.h"


namespace dmtcp
{
  namespace CkptSerializer
  {
    int openDmtcpCheckpointFile(const dmtcp::string& path, pid_t *extDecompPid);
    void closeDmtcpCheckpointFile(int fd, pid_t extDecompPid);
    void writeCkptHeader(int fd);
    int readCkptHeader(const dmtcp::string& path, pid_t *extDecompPid,
                       dmtcp::ProcessInfo *pInfo);
  };
}

#endif
