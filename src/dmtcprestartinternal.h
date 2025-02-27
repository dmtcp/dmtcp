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
#ifndef __DMTCP_RESTART_H__
#define __DMTCP_RESTART_H__

#include <map>
#include <vector>
#include "config.h"
#include "constants.h"
#include "processinfo.h"
#include "uniquepid.h"

#define BINARY_NAME         "dmtcp_restart"

using namespace dmtcp;

class RestoreTarget
{
  public:
    RestoreTarget(const string &path);

    int fd() const { return _fd; }

    UniquePid upid() { return _ckptHdr.upid; }
    UniquePid uppid() { return _ckptHdr.uppid; }
    UniquePid compGroup() { return _ckptHdr.compGroup; }


    pid_t pid() const { return _ckptHdr.pid; }

    pid_t sid() const { return _ckptHdr.sid; }

    bool isRootOfProcessTree() const { return _ckptHdr.isRootOfProcessTree; }

    string procSelfExe() const { return _ckptHdr.procSelfExe; }

    bool isOrphan() { return _ckptHdr.ppid == 1; }

    bool isGroupLeader() const { return _ckptHdr.pid == _ckptHdr.gid; }

    string procname() { return _ckptHdr.procname; }

    int numPeers() { return _ckptHdr.numPeers; }

    MemRegion restoreBuf() { return _ckptHdr.restoreBuf; }

    const int getElfType() const { return _ckptHdr.elfType; }


    void initialize();

    void restoreGroup();

    void createDependentChildProcess();

    void createDependentNonChildProcess();

    void createOrphanedProcess(bool createIndependentRootProcesses = false);

    void createProcess(bool createIndependentRootProcesses = false);

  private:
    string _path;
    DmtcpCkptHeader _ckptHdr;
    int _fd;
};

class DmtcpRestart
{
  public:
    DmtcpRestart(int argc, char **argv, const string &binaryName, const string &mtcpRestartBinaryName);
    void processCkptImages();

    int argc;
    char **argv;
    string binaryName;
    string mtcpRestartBinaryName;
    vector<string> ckptImages;
    string restartDir;
    bool runMpiProxy = 0;
};

vector<char *> getMtcpArgs(MemRegion restoreBuf);

void dmtcp_restart_plugin(const string &restartDir,
                          const vector<string> &ckptImages) __attribute((weak));

#endif // #ifdef __DMTCP_RESTART_H__
