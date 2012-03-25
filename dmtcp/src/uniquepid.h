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

#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <iostream>
#include "constants.h"
#include "dmtcpalloc.h"
#include "../jalib/jserialize.h"

#ifndef UNIQUEPID_H
#define UNIQUEPID_H

namespace dmtcp
{
  struct UniquePid
  {
  public:
    static dmtcp::UniquePid& ParentProcess();
    static dmtcp::UniquePid& ComputationId();
    static dmtcp::UniquePid& ThisProcess(bool disableJTrace = false);
    UniquePid();

    UniquePid ( const long& host, const pid_t& pd, const time_t& tm,
                const int& gen = 0 )
        : _pid ( pd ), _hostid ( host ), _time ( tm ), _generation ( gen )
    {setPrefix();}

    long hostid() const { return _hostid; }
    pid_t pid() const { return _pid; }
    time_t time() const { return _time; }
    int generation() const { return _generation; }
    const char* prefix() const { return _prefix; }

    void incrementGeneration();
    static const char* getCkptFilename();
    static dmtcp::string getCkptFilesSubDir();
    static dmtcp::string getCkptDir();
    static void setCkptDir(const char*);
    static void updateCkptDir();
    static void setTmpDir(const char * envVarTmpDir);
    static dmtcp::string getTmpDir();

    static dmtcp::string dmtcpTableFilename();
    static dmtcp::string pidTableFilename();

    static void serialize( jalib::JBinarySerializer& o );

    bool operator< ( const UniquePid& that ) const;
    bool operator== ( const UniquePid& that ) const;
    bool operator!= ( const UniquePid& that ) const { return ! operator== ( that ); }

    static void resetOnFork ( const dmtcp::UniquePid& newId );

    dmtcp::string toString() const;

    bool isNull() const;

  private:
    void setPrefix();

    pid_t _pid; //getpid()
    long  _hostid; //gethostid()
    time_t _time; //time()
    int _generation; //generation()
    char _prefix[32];
  };
}

//to make older versions of gcc work
namespace dmtcp
{
  dmtcp::ostream& operator << ( dmtcp::ostream& o,const dmtcp::UniquePid& id );
}

#endif
