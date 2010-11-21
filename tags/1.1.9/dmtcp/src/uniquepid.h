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

#include "dmtcpalloc.h"
#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <iostream>
#include "constants.h"
#include "../jalib/jserialize.h"

#ifndef UNIQUEPID_H
#define UNIQUEPID_H

namespace dmtcp
{

  struct UniquePid
  {
  public:
    static dmtcp::UniquePid& ParentProcess();
    static dmtcp::UniquePid& ThisProcess(bool disableJTrace = false);
    UniquePid();
    UniquePid(pid_t pid);
    UniquePid ( long host, pid_t pd, time_t tm )
        : _pid ( pd ), _hostid ( host ), _time ( tm ), _generation ( 0 ) {}
    UniquePid ( long host, pid_t pd, time_t tm, int gen )
        : _pid ( pd ), _hostid ( host ), _time ( tm ), _generation ( gen) {}

    long hostid() const;
    pid_t pid() const;
    time_t time() const;
    int generation() const;
    void incrementGeneration();
    static const char* checkpointFilename();
    static dmtcp::string dmtcpTableFilename();
#ifdef PID_VIRTUALIZATION
    static dmtcp::string pidTableFilename();
#endif
    static const char* ptsSymlinkFilename ( char *pts );
    static void setTmpDir(const char * envVarTmpDir);
    static dmtcp::string getTmpDir();

    static void serialize( jalib::JBinarySerializer& o );

    bool operator< ( const UniquePid& that ) const;
    bool operator== ( const UniquePid& that ) const;
    bool operator!= ( const UniquePid& that ) const { return ! operator== ( that ); }

    static void resetOnFork ( const dmtcp::UniquePid& newId );

    dmtcp::string toString() const;

    bool isNull() const;

  private:
    pid_t _pid; //getpid()
    long  _hostid; //gethostid()
    time_t _time; //time()
    int _generation; //generation()
  };
}

//to make older versions of gcc work
namespace dmtcp
{
  dmtcp::ostream& operator << ( dmtcp::ostream& o,const dmtcp::UniquePid& id );
}

#endif
