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
#include "protectedfds.h"

#include "constants.h"
#include  "../jalib/jassert.h"
#include  "../jalib/jconvert.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>



dmtcp::ProtectedFDs& dmtcp::ProtectedFDs::instance()
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
