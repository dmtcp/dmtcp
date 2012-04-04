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

#include <sys/types.h>
#include <sys/stat.h>
#include "jalloc.h"
#include "jassert.h"
#include "ptrace.h"
#include "ptraceinfo.h"
#include "dmtcpplugin.h"
#include "util.h"

static int originalStartup = 1;

void ptraceInit()
{
  dmtcp::PtraceInfo::instance().createSharedFile();
  dmtcp::PtraceInfo::instance().mapSharedFile();
}

void ptraceWaitForSuspendMsg(void *data)
{
  dmtcp::PtraceInfo::instance().markAsCkptThread();
  if (!originalStartup) {
    dmtcp::PtraceInfo::instance().waitForSuperiorAttach();
    originalStartup = 0;
  }
}

void ptraceProcessResumeUserThread(void *data)
{
  DmtcpResumeUserThreadInfo *info = (DmtcpResumeUserThreadInfo*) data;
  ptrace_process_resume_user_thread(info->is_ckpt, info->is_restart);
  JNOTE("") (GETTID());
}

extern "C" void dmtcp_process_event(DmtcpEvent_t event, void* data)
{
  switch (event) {
    case DMTCP_EVENT_INIT:
      ptraceInit();
      break;

    case DMTCP_EVENT_WAIT_FOR_SUSPEND_MSG:
      ptraceWaitForSuspendMsg(data);
      break;

    case DMTCP_EVENT_PRE_SUSPEND_USER_THREAD:
      ptrace_process_pre_suspend_user_thread();
      break;

    case DMTCP_EVENT_RESUME_USER_THREAD:
      ptraceProcessResumeUserThread(data);
      break;

    case DMTCP_EVENT_RESET_ON_FORK:
      originalStartup = 1;
      break;

    case DMTCP_EVENT_THREAD_CREATED:
    case DMTCP_EVENT_THREAD_START:
    case DMTCP_EVENT_GOT_SUSPEND_MSG:
    case DMTCP_EVENT_START_PRE_CKPT_CB:
    case DMTCP_EVENT_POST_RESTART_REFILL:
    case DMTCP_EVENT_PRE_CKPT:
    case DMTCP_EVENT_POST_LEADER_ELECTION:
    case DMTCP_EVENT_POST_DRAIN:
    case DMTCP_EVENT_POST_CKPT:
    case DMTCP_EVENT_POST_RESTART:
    default:
      break;
  }

  NEXT_DMTCP_PROCESS_EVENT(event, data);
  return;
}
