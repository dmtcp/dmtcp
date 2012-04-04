/*****************************************************************************
 *   Copyright (C) 2008-2012 by Ana-Maria Visan, Kapil Arya, and             *
 *                                                            Gene Cooperman *
 *   amvisan@cs.neu.edu, kapil@cs.neu.edu, and gene@ccs.neu.edu              *
 *                                                                           *
 *   This file is part of the PTRACE plugin of DMTCP (DMTCP:mtcp).           *
 *                                                                           *
 *  DMTCP:mtcp is free software: you can redistribute it and/or              *
 *  modify it under the terms of the GNU Lesser General Public License as    *
 *  published by the Free Software Foundation, either version 3 of the       *
 *  License, or (at your option) any later version.                          *
 *                                                                           *
 *  DMTCP:plugin/ptrace is distributed in the hope that it will be useful,   *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *  GNU Lesser General Public License for more details.                      *
 *                                                                           *
 *  You should have received a copy of the GNU Lesser General Public         *
 *  License along with DMTCP:dmtcp/src.  If not, see                         *
 *  <http://www.gnu.org/licenses/>.                                          *
 *****************************************************************************/

#ifndef PTRACEINFO_H
# define PTRACEINFO_H

#include <sys/types.h>
#include <sys/ptrace.h>
#include <linux/version.h>
// This was needed for:  SUSE LINUX 10.0 (i586) OSS
#ifndef PTRACE_SETOPTIONS
# include <linux/ptrace.h>
#endif
#include <stdarg.h>
#include <linux/unistd.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <list>
#include <semaphore.h>

#include "jassert.h"
#include "jfilesystem.h"

#define MAX_INFERIORS 1024

namespace dmtcp {

  class Inferior {
    public:
      Inferior(pid_t sup = 0, pid_t inf = 0, bool isCkptThr = false) {
        reset(sup, inf, isCkptThr);
      }

      ~Inferior() {}

      void reset(pid_t sup = 0, pid_t inf = 0, bool isCkptThr = false) {
        _superior = sup;
        _tid = inf;
        _isWait4StatusAvailable = false;
        _wait4Status = -1;
        _lastCmd = -1;
        _ptraceOptions = NULL;
        _state = PTRACE_PROC_INVALID;
        _isCkptThread = isCkptThr;
        sem_init(&_sem, 1, 0);
      }

      void semInit() { JASSERT(::sem_init(&_sem, 1, 0) == 0); }
      void semPost() { JASSERT(::sem_post(&_sem) == 0); }
      void semWait() { JASSERT(::sem_wait(&_sem) == 0); }

      pid_t tid(void) { return _tid; }
      pid_t superior(void) { return _superior; }
      bool  isCkptThread(void) { return _isCkptThread; }
      int   lastCmd(void) { return _lastCmd; }
      int   setLastCmd(int cmd) { _lastCmd = cmd; }
      PtraceProcState  state() { return _state; };
      void  setState(PtraceProcState state) { _state = state; };
      void *getPtraceOptions() { return _ptraceOptions; }
      void  setPtraceOptions(void *data) { _ptraceOptions = data; }

      void setWait4Status(int *status, struct rusage *rusage) {
        _isWait4StatusAvailable = true;
        _wait4Status = *status;
        _wait4Rusage = *rusage;
      }

      pid_t getWait4Status(int *status, struct rusage *rusage) {
        if (_isWait4StatusAvailable) {
          *status = _wait4Status;
          *rusage = _wait4Rusage;
          _isWait4StatusAvailable = false;
          return _tid;
        }
        return -1;
      }

      bool isStopped() { return _state == PTRACE_PROC_TRACING_STOP; }

      void  markAsCkptThread() { _isCkptThread = true; };

    private:
      pid_t _superior;
      pid_t _tid;
      bool  _isCkptThread;
      bool  _isWait4StatusAvailable;
      int   _wait4Status;
      struct rusage  _wait4Rusage;
      int   _lastCmd;
      void *_ptraceOptions;
      PtraceProcState  _state;
      sem_t _sem;
  };

  class PtraceSharedData {
    private:
      void do_lock() { JASSERT(pthread_mutex_lock(&_lock) == 0); }
      void do_unlock() { JASSERT(pthread_mutex_unlock(&_lock) == 0); }

    public:
      PtraceSharedData()
        : _isPtracing(false)
        , _numInferiors(0) { }

      bool isPtracing(void) { return _isPtracing; }
      void setPtracing(void) { _isPtracing = true; }
      size_t numInferiors(void) { return _numInferiors; }
      pthread_mutex_t *condMutexPtr() { return &_condMutex; }

      void init(void)
      {
        pthread_mutex_init(&_lock, NULL);
        pthread_mutex_init(&_condMutex, NULL);
      }

      Inferior *getInferior(pid_t tid)
      {
        for (size_t i = 0; i < MAX_INFERIORS; i++) {
          if (_inferiors[i].tid() == tid) {
            return &_inferiors[i];
          }
        }
        return NULL;
      }

      Inferior *insertInferior(pid_t sup, pid_t tid, bool ckptThread = false)
      {
        Inferior *inf = NULL;
        do_lock();
        inf = getInferior(tid);
        if (inf == NULL) {
          for (size_t i = 0; i < MAX_INFERIORS; i++) {
            if (_inferiors[i].tid() == 0) {
              inf = &_inferiors[i];
              _numInferiors++;
              break;
            }
          }
          inf->reset(sup, tid);
        }
        if (ckptThread) {
          inf->markAsCkptThread();
        }
        do_unlock();
        return inf;
      }

      void eraseInferior(Inferior *inf)
      {
        JASSERT(inf != NULL);
        do_lock();
        inf->reset();
        _numInferiors -= 1;
        do_unlock();
      }

    private:
      bool   _isPtracing;
      size_t _numInferiors;
      pthread_mutex_t _lock;
      pthread_mutex_t _condMutex;
      Inferior _inferiors[MAX_INFERIORS];
  };

  class PtraceInfo {
    public:
      PtraceInfo()
        : _sharedData (NULL)
      {}

      ~PtraceInfo(){}

      static PtraceInfo& instance();

      dmtcp::vector<Inferior*> getInferiors(pid_t pid);
      void insertInferior(Inferior *inf);

      void createSharedFile();
      void mapSharedFile();

      void setLastCmd(pid_t tid, int lastCmd);
      void insertInferior(pid_t tid);
      void eraseInferior(pid_t tid);

      void waitForSuperiorAttach();
      void markAsCkptThread();
      bool isPtracing();
      void setPtracing();

      pid_t getWait4Status(pid_t pid, int *status, struct rusage *rusage);
      void processSuccessfulPtraceCmd(int request, pid_t pid,
                                      void *addr, void *data);
      void processSetOptions(pid_t pid, void *data);
      void processPreResumeAttach(pid_t inferior);

    private:
      PtraceSharedData *_sharedData;
      dmtcp::map< pid_t, dmtcp::vector<Inferior*> > _supToInfsMap;
      typedef dmtcp::map< pid_t, dmtcp::vector<Inferior*> >::iterator
        supToInfsMapIter;
      dmtcp::map< pid_t, pid_t > _infToSupMap;
      typedef dmtcp::map< pid_t, pid_t >::iterator infToSupIter;
  };
}
#endif
