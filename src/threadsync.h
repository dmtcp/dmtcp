/****************************************************************************
 *   Copyright (C) 2006-2012 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#ifndef THREADSYNC_H
#define THREADSYNC_H

#include "dmtcpworker.h"

#define WRAPPER_EXECUTION_DISABLE_CKPT()           \
  /*JTRACE("Acquiring wrapperExecutionLock");*/    \
  bool __wrapperExecutionLockAcquired =            \
    dmtcp::ThreadSync::wrapperExecutionLockLock(); \
  if (__wrapperExecutionLockAcquired) {            \
    /*JTRACE("Acquired wrapperExecutionLock"); */  \
  }

#define WRAPPER_EXECUTION_ENABLE_CKPT()              \
  if (__wrapperExecutionLockAcquired) {              \
    /*JTRACE("Releasing wrapperExecutionLock"); */   \
    dmtcp::ThreadSync::wrapperExecutionLockUnlock(); \
  }

#define DUMMY_WRAPPER_EXECUTION_DISABLE_CKPT() \
  bool __wrapperExecutionLockAcquired = false;

#define WRAPPER_EXECUTION_GET_EXCL_LOCK()                \
  bool __wrapperExecutionLockAcquired                    \
    = dmtcp::ThreadSync::wrapperExecutionLockLockExcl(); \
  dmtcp::ThreadSync::unsetOkToGrabLock();

#define WRAPPER_EXECUTION_RELEASE_EXCL_LOCK() \
  WRAPPER_EXECUTION_ENABLE_CKPT();            \
  dmtcp::ThreadSync::setOkToGrabLock();

namespace dmtcp
{
namespace ThreadSync
{
void acquireLocks();
void releaseLocks();
void resetLocks();
void initThread();
void initMotherOfAll();

void destroyDmtcpWorkerLockLock();
void destroyDmtcpWorkerLockUnlock();
int destroyDmtcpWorkerLockTryLock();

void delayCheckpointsLock();
void delayCheckpointsUnlock();

bool wrapperExecutionLockLock();
void wrapperExecutionLockUnlock();
bool wrapperExecutionLockLockExcl();

bool threadCreationLockLock();
void threadCreationLockUnlock();

bool libdlLockLock();
void libdlLockUnlock();
void waitForThreadsToFinishInitialization();
void incrementUninitializedThreadCount();
void decrementUninitializedThreadCount();
void threadFinishedInitialization();

bool isOkToGrabLock();
void setOkToGrabLock();
void unsetOkToGrabLock();

#if TRACK_DLOPEN_DLSYM_FOR_LOCKS
bool isThreadPerformingDlopenDlsym();
void setThreadPerformingDlopenDlsym();
void unsetThreadPerformingDlopenDlsym();
#endif // if TRACK_DLOPEN_DLSYM_FOR_LOCKS
}

class WrapperLock
{
  public:
    WrapperLock(bool _exclusiveLock = false)
      : exclusiveLock(_exclusiveLock)
    {
      dmtcp_initialize_entry_point();

      if (exclusiveLock) {
        lockAcquired = ThreadSync::wrapperExecutionLockLockExcl();
        dmtcp::ThreadSync::unsetOkToGrabLock();
      } else {
        lockAcquired = ThreadSync::wrapperExecutionLockLock();
      }
    }

    virtual ~WrapperLock()
    {
      if (lockAcquired) {
        dmtcp::ThreadSync::wrapperExecutionLockUnlock();
        if (exclusiveLock) {
          dmtcp::ThreadSync::setOkToGrabLock();
        }
      }
    }

  private:
    bool lockAcquired;
    bool exclusiveLock;
};

class WrapperLockExcl : public WrapperLock
{
  public:
    WrapperLockExcl()
      : WrapperLock(true)
    {
    }
};

}
#endif // ifndef THREADSYNC_H
