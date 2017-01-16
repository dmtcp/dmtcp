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

#ifndef __RESTART_SCRIPT_H__
#define __RESTART_SCRIPT_H__

#include <time.h>

#include "dmtcpalloc.h"
#include "uniquepid.h"

namespace dmtcp
{
namespace RestartScript
{
string writeScript(const string &ckptDir,
                   bool uniqueCkptFilenames,
                   const time_t &ckptTimeStamp,
                   const uint32_t theCheckpointInterval,
                   const int thePort,
                   const UniquePid &compId,
                   const map<string, vector<string> > &restartFilenames,
                   const map<string, vector<string> >& rshFilenames,
                   const map<string, vector<string> >& sshFilenames);
} // namespace dmtcp {
} // namespace RestartScript {
#endif // #ifndef __RESTART_SCRIPT_H__
