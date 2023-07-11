/*****************************************************************************
 * Copyright (C) 2010-2014 Kapil Arya <kapil@ccs.neu.edu>                    *
 * Copyright (C) 2010-2014 Gene Cooperman <gene@ccs.neu.edu>                 *
 *                                                                           *
 * DMTCP is free software: you can redistribute it and/or                    *
 * modify it under the terms of the GNU Lesser General Public License as     *
 * published by the Free Software Foundation, either version 3 of the        *
 * License, or (at your option) any later version.                           *
 *                                                                           *
 * DMTCP is distributed in the hope that it will be useful,                  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of            *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
 * GNU Lesser General Public License for more details.                       *
 *                                                                           *
 * You should have received a copy of the GNU Lesser General Public          *
 * License along with DMTCP.  If not, see <http://www.gnu.org/licenses/>.    *
 *****************************************************************************/

#ifndef TLSINFO_H
#define TLSINFO_H

#include <sys/types.h>

#include "ldt.h"
#include "threadinfo.h"

#ifdef __x86_64__
# define eax rax
# define ebx rbx
# define ecx rcx
# define edx rax
# define ebp rbp
# define esi rsi
# define edi rdi
# define esp rsp
# define CLEAN_FOR_64_BIT(args ...)        CLEAN_FOR_64_BIT_HELPER(args)
# define CLEAN_FOR_64_BIT_HELPER(args ...) # args
#elif __i386__
# define CLEAN_FOR_64_BIT(args ...)        # args
#else // ifdef __x86_64__
# define CLEAN_FOR_64_BIT(args ...)        "CLEAN_FOR_64_BIT_undefined"
#endif // ifdef __x86_64__


int TLSInfo_GetTidOffset();
int TLSInfo_GetPidOffset();
void TLSInfo_PostRestart();
void TLSInfo_VerifyPidTid(pid_t pid, pid_t tid);
void TLSInfo_UpdatePid();
void TLSInfo_SaveTLSState(Thread *thread);
void TLSInfo_RestoreTLSState(Thread *thread);
void TLSInfo_RestoreTLSTidPid(Thread *thread);
void TLSInfo_SetThreadSysinfo(void *sysinfo);
void *TLSInfo_GetThreadSysinfo();
int TLSInfo_HaveThreadSysinfoOffset();

#endif // ifndef TLSINFO_H
