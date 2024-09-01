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

    const UniquePid &upid() { return _pInfo.upid(); }
    const UniquePid &uppid() { return _pInfo.uppid(); }

    pid_t pid() const { return _pInfo.pid(); }

    pid_t sid() const { return _pInfo.sid(); }

    bool isRootOfProcessTree() const { return _pInfo.isRootOfProcessTree(); }

    const string& procSelfExe() const { return _pInfo.procSelfExe(); }

    bool isOrphan() { return _pInfo.isOrphan(); }

    string procname() { return _pInfo.procname(); }

    UniquePid compGroup() { return _pInfo.compGroup(); }

    int numPeers() { return _pInfo.numPeers(); }

    uint64_t restoreBufAddr() { return _pInfo.restoreBufAddr(); }
    uint64_t restoreBufLen() { return _pInfo.restoreBufLen(); }

    void initialize();

    void restoreGroup();

    void createDependentChildProcess();

    void createDependentNonChildProcess();

    void createOrphanedProcess(bool createIndependentRootProcesses = false);

    void createProcess(bool createIndependentRootProcesses = false);

    const map<string, string>& getKeyValueMap() const
      { return _pInfo.getKeyValueMap(); }

    const int getElfType() const
      { return _pInfo.elfType(); }

  private:
    string _path;
    ProcessInfo _pInfo;
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

vector<char *> getMtcpArgs(uint64_t restoreBufAddr, uint64_t restoreBufLen);
void publishKeyValueMapToMtcpEnvironment(RestoreTarget *restoreTarget);

void dmtcp_restart_plugin(const string &restartDir,
                          const vector<string> &ckptImages) __attribute((weak));

#endif // #ifdef __DMTCP_RESTART_H__
