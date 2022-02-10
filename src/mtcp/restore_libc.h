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

#include "ldt.h"
#include "mtcp_header.h"
#include "protectedfds.h"

#ifdef __cplusplus
extern "C" {
#endif // ifdef __cplusplus

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

#define PRINTF(fmt, ...)                                                     \
  do {                                                                       \
    /* In some cases, the user stack may be very small (less than 10KB). */  \
    /* We will overrun the buffer with just two extra stack frames. */       \
    char buf[256];                                                           \
    int c = snprintf(buf, sizeof(buf), "[%d] %s:%d in %s; REASON= " fmt,     \
                     getpid(), __FILE__, __LINE__, __FUNCTION__,             \
                     ## __VA_ARGS__);                                        \
    if (c >= sizeof(buf)) {                                                  \
      c = sizeof(buf)-1;                                                     \
    }                                                                        \
    buf[c] = '\n'; /* c is number of chars written (excl. null char.) */     \
    /* assign to rc in order to avoid 'unused result' compiler warnings */   \
    ssize_t rc __attribute__((unused));                                      \
    rc = write(PROTECTED_STDERR_FD, buf, c+1);                               \
  } while (0);

#ifdef LOGGING
# define DPRINTF PRINTF
#else // ifdef LOGGING
# define DPRINTF(args ...) // debug printing
#endif // ifdef LOGGING

#define ASSERT(condition)                            \
  do {                                               \
    if (!(condition)) {                              \
      PRINTF("Assertion failed: %s\n", # condition); \
      _exit(0);                                      \
    }                                                \
  } while (0);

#define ASSERT_NOT_REACHED()                   \
  do {                                         \
    PRINTF("NOT_REACHED Assertion failed.\n"); \
    _exit(0);                                  \
  } while (0);


int TLSInfo_GetTidOffset();
int TLSInfo_GetPidOffset();
void TLSInfo_PostRestart();
void TLSInfo_VerifyPidTid(pid_t pid, pid_t tid);
void TLSInfo_UpdatePid();
void TLSInfo_SaveTLSState(ThreadTLSInfo *tlsInfo);
void TLSInfo_RestoreTLSState(ThreadTLSInfo *tlsInfo);
void TLSInfo_SetThreadSysinfo(void *sysinfo);
void *TLSInfo_GetThreadSysinfo();
int TLSInfo_HaveThreadSysinfoOffset();

void Thread_RestoreAllThreads(void);

#ifdef __cplusplus
}
#endif // ifdef __cplusplus
#endif // ifndef TLSINFO_H
