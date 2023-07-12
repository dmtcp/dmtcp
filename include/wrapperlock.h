/****************************************************************************
 *   Copyright (C) 2023 by Jason Ansel, Kapil Arya, and Gene Cooperman      *
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

#ifndef DMTCP_WRAPPER_LOCK_H
#define DMTCP_WRAPPER_LOCK_H

namespace dmtcp
{
class WrapperLock
{
  public:
    WrapperLock(const WrapperLock&) = delete; // no copies
    WrapperLock& operator=(const WrapperLock&) = delete; // no self-assignments

    WrapperLock(bool _exclusiveLock = false);
    virtual ~WrapperLock();
};

class WrapperLockExcl : public WrapperLock
{
  public:
    WrapperLockExcl() : WrapperLock(true) { }
};

}
#endif // ifndef DMTCP_WRAPPER_LOCK_H
