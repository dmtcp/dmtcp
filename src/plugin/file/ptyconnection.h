/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, Gene Cooperman,    *
 *                                                           and Rohan Garg *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu, and         *
 *                                                      rohgarg@ccs.neu.edu *
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

#pragma once
#ifndef PTYCONNECTION_H
#define PTYCONNECTION_H

#include <sys/stat.h>
#include <sys/types.h>

#include "connection.h"

namespace dmtcp
{
class PtyConnection : public Connection
{
  public:
    enum PtyType {
      PTY_INVALID = Connection::PTY,
      PTY_DEV_TTY,
      PTY_CTTY,
      PTY_PARENT_CTTY,
      PTY_MASTER,
      PTY_SLAVE,
      PTY_BSD_MASTER,
      PTY_BSD_SLAVE,
      PTY_EXTERNAL
    };

    PtyConnection() {}

    PtyConnection(int fd, const char *path, int flags, mode_t mode, int type);

    string ptsName() { return _ptsName;  }

    string virtPtsName() { return _virtPtsName;  }

    void markPreExistingCTTY() { _preExistingCTTY = true; }

    virtual void doLocking() override;
    virtual void drain() override;
    virtual void refill(bool isRestart) override;
    virtual void postRestart() override;
    virtual void serializeSubClass(jalib::JBinarySerializer &o) override;
    virtual bool isPreExistingCTTY() const override { return _preExistingCTTY; }

    virtual string str() override { return _masterName + ":" + _ptsName; }

    virtual PtyConnection* clone() override {
      return new PtyConnection(*this);
    }

  private:
    string _masterName;
    string _ptsName;
    string _virtPtsName;
    int64_t _flags;
    int64_t _mode;
    char _ptmxIsPacketMode;
    char _isControllingTTY;
    char _preExistingCTTY;
};
}
#endif // ifndef PTYCONNECTION_H
