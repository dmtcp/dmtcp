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

#include "protectedfds.h"

#include "constants.h"
#include  "../jalib/jassert.h"
#include  "../jalib/jconvert.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>



dmtcp::ProtectedFDs& dmtcp::ProtectedFDs::Instance()
{
  static ProtectedFDs inst;
  return inst;
}

dmtcp::ProtectedFDs::ProtectedFDs()
{
//    memset(_usageTable, 0, sizeof(_usageTable));

  //setup out "busy" fd
//    _usageTable[0] = true;
  int tmp = open ( "/dev/null",O_RDONLY );
  JASSERT ( tmp > 0 ) ( tmp );
  JASSERT ( PFD ( 0 ) == dup2 ( tmp,PFD ( 0 ) ) ) ( PFD ( 0 ) ) ( tmp );
  close ( tmp );

  //"lock" all protected fds so system wont allocate them
  for ( int i=1; i<PROTECTED_FD_COUNT; ++i )
  {
    JASSERT ( PFD ( i ) == dup2 ( PFD ( 0 ),PFD ( i ) ) ) ( i );
  }
}

bool dmtcp::ProtectedFDs::isProtected ( int fd )
{
  return jalib::Between ( PFD ( 0 ),fd,PFD ( PROTECTED_FD_COUNT )-1 );
}

// int dmtcp::ProtectedFDs::acquire()
// {
//    for(int i=1; i<PROTECTED_FD_COUNT; ++i)
//    {
//        if(!_usageTable[i])
//        {
//            _usageTable[i] = true;
//            return PFD(i);
//        }
//    }
//    JASSERT(false).Text("ran out of protected FDs");
// }
//
// void dmtcp::ProtectedFDs::release(int fd)
// {
//     int i = fd - PROTECTED_FD_START;
//     JASSERT(isProtected(i))(fd)(i);
//     _usageTable[i] = false;
//     //"lock the fd"
//     JASSERT(fd == dup2(PFD(0),fd))(fd);
// }

// int dmtcp::ProtectedFDs::convertToProtected(int srcFd)
// {
//     JASSERT(srcFd >= 0)(srcFd);
//     int newFd = acquire();
//     JASSERT(newFd == dup2(srcFd, newFd))(srcFd)(newFd);
//     close(srcFd);
//     return newFd;
// }
