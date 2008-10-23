/***************************************************************************
 *   Copyright (C) 2006 by Jason Ansel                                     *
 *   jansel@ccs.neu.edu                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <iostream>

#ifndef UNIQUEPID_H
#define UNIQUEPID_H

namespace dmtcp
{

  struct UniquePid
  {
  public:
    static dmtcp::UniquePid& ParentProcess();
    static const dmtcp::UniquePid& ThisProcess();
    UniquePid();
    UniquePid ( long host, pid_t pd, time_t tm )
        : _pid ( pd ), _hostid ( host ), _time ( tm ) {}

    long hostid() const;
    pid_t pid() const;
    time_t time() const;
    static const char* checkpointFilename();
    static std::string dmtcpTableFilename();
    static const char* ptsSymlinkFilename ( char *pts );

    bool operator< ( const UniquePid& that ) const;
    bool operator== ( const UniquePid& that ) const;
    bool operator!= ( const UniquePid& that ) const { return ! operator== ( that ); }

    static void resetOnFork ( const dmtcp::UniquePid& newId );

    std::string toString() const;

    bool isNull() const;
  private:
    pid_t _pid; //getpid()
    long  _hostid; //gethostid()
    time_t _time; //time()
  };

}

//to make older versions of gcc work
namespace std
{
  std::ostream& operator << ( std::ostream& o,const dmtcp::UniquePid& id );
}

#endif



