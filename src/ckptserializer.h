/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#ifndef CKPT_SERIZLIZER_H
#define CKPT_SERIZLIZER_H

#include "dmtcpalloc.h"
#include "processinfo.h"


namespace dmtcp
{
  namespace CkptSerializer
  {
    int openCkptFileToRead(const string& path);
    int openCkptFileToWrite(const string& path);
    void createCkptDir();
    void writeCkptImage(void *mtcpHdr, size_t mtcpHdrLen);
    void writeDmtcpHeader(int fd);
    int readCkptHeader(const string& path, ProcessInfo *pInfo);
  };
}

#endif
