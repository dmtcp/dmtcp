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
#ifndef DMTCPPROTECTEDFDS_H
#define DMTCPPROTECTEDFDS_H


#include "constants.h"

#define PROTECTEDFDS (dmtcp::ProtectedFDs::instance())
#define PFD(i) (PROTECTED_FD_START + (i))
#define PROTECTEDFD(i) PFD(i)
#define PROTECTED_STDERR_FD PFD(5)

namespace dmtcp
{

  class ProtectedFDs
  {
    public:
      static ProtectedFDs& instance();
      static bool isProtected ( int fd );
    protected:
      ProtectedFDs();
    private:
//     bool _usageTable[PROTECTED_FD_COUNT];
  };

}

#endif
