/****************************************************************************
 *   Copyright (C) 2006-2012 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#ifndef THREADLIST_H
#define THREADLIST_H

#include <ucontext.h>
#include <signal.h>
#include <sys/types.h>
#include <asm/ldt.h>
#include "jassert.h"

#ifdef __i386__
  typedef unsigned short segreg_t;
#elif __x86_64__
  typedef unsigned int segreg_t;
#elif __arm__
  typedef unsigned int segreg_t;
#endif

#ifdef __x86_64__
# define eax rax
# define ebx rbx
# define ecx rcx
# define edx rax
# define ebp rbp
# define esi rsi
# define edi rdi
# define esp rsp
# define CLEAN_FOR_64_BIT(args...) CLEAN_FOR_64_BIT_HELPER(args)
# define CLEAN_FOR_64_BIT_HELPER(args...) #args
#elif __i386__
# define CLEAN_FOR_64_BIT(args...) #args
#else
# define CLEAN_FOR_64_BIT(args...) "CLEAN_FOR_64_BIT_undefined"
#endif

typedef int (*fptr)(void*);
namespace dmtcp
{
  class Thread;
  enum ThreadState {
    ST_RUNNING,
    ST_SIGNALED,
    ST_SUSPINPROG,
    ST_SUSPENDED,
    ST_ZOMBIE,
    ST_CKPNTHREAD
  };

  class Thread {
    public:
#ifdef JALIB_ALLOCATOR
      static void* operator new(size_t nbytes, void* p) { return p; }
      static void* operator new(size_t nbytes) { JALLOC_HELPER_NEW(nbytes); }
      static void  operator delete(void* p) { JALLOC_HELPER_DELETE(p); }
#endif
      void init(int (*fn)(void*) = NULL, void *arg = NULL, int flags = 0,
           int *ptid = NULL, int *ctid = NULL);

      void updateTid();
      bool updateState(ThreadState newval, ThreadState oldval);

      void setupLongjump();
      void performLongjump();
      void saveSigState();
      void restoreSigState();

      pid_t tid;
      Thread *next;
      Thread *prev;
      int state;

      int (*fn)(void *);
      void *arg;
      int flags;
      pid_t *ptid;
      pid_t *ctid;

      pid_t virtual_tid;
      sigset_t sigblockmask; // blocked signals
      sigset_t sigpending;   // pending signals

      void *saved_sp; // at restart, we use a temporary stack just
      //   beyond original stack (red zone)
      segreg_t fs, gs;  // thread local storage pointers
      struct user_desc gdtentrytls[1];

      ///JA: new code ported from v54b
#ifdef SETJMP
      sigjmp_buf jmpbuf;     // sigjmp_buf saved by sigsetjmp on ckpt
#else
      ucontext_t savctx;     // context saved on suspend
#endif

  };

  namespace ThreadList {
    pid_t _real_pid();
    pid_t _real_tid();
    int _real_tgkill(pid_t tgid, pid_t tid, int sig);

    void init();
    void resetOnFork();
    void postRestart();
    void killCkpthread();
    void threadExit();

    Thread *getNewThread();
    void addToActiveList();
    void threadIsDead (Thread *thread);
    void emptyFreeList();

    void stopthisthread (int sig);
    void suspendThreads();
    void resumeThreads();
    void waitForAllRestored(Thread *thisthread);

  };
};
#endif
