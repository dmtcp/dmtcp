/*****************************************************************************
 *   Copyright (C) 2008-2013 Ana-Maria Visan, Kapil Arya, and Gene Cooperman *
 *   amvisan@cs.neu.edu, kapil@cs.neu.edu, and gene@ccs.neu.edu              *
 *                                                                           *
 *  This file is part of the PTRACE plugin of DMTCP (DMTCP:plugin/ptrace).   *
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

/************************************************************************
 * For ARM, ptrace support, and especially PTRACE_SINGLESTEP was added
 * only recently (3/2013):
 *     https://bugs.eclipse.org/bugs/show_bug.cgi?id=403422
 *     https://bugs.eclipse.org/bugs/show_bug.cgi?id=404253
 *     http://stackoverflow.com/questions/16806276/how-ptrace-implemented-in-arm
 ************************************************************************/

#ifndef _GNU_SOURCE
# define _GNU_SOURCE  /* Needed for syscall declaration */
#endif // ifndef _GNU_SOURCE
#define _XOPEN_SOURCE 500 /* _XOPEN_SOURCE >= 500 needed for getsid */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <thread_db.h>
#include <unistd.h>

#include "dmtcp.h"
#include "jassert.h"
#include "jfilesystem.h"
#include "ptrace.h"
#include "ptraceinfo.h"
#include "util.h"

// Match up this definition with the one in src/constants.h
#define DMTCP_FAKE_SYSCALL 1023

#define EFLAGS_OFFSET      (64)
#ifdef __x86_64__

// Found in /usr/include/asm/ptrace.h for struct pt_regs (alias for user_regs?)
# define AX_REG      rax
# define ORIG_AX_REG orig_rax
# define SP_REG      rsp
# define IP_REG      rip

// SIGRETURN_INST_16 is the machine code for the assembly statement syscall.
// The specific syscall to be detected is sigreturn.
# define SIGRETURN_INST_16 0x050f
#elif __i386__
# define AX_REG            eax
# define ORIG_AX_REG       orig_eax
# define SP_REG            esp
# define IP_REG            eip

// SIGRETURN_INST_16 is the machine code for the assembly statement syscall.
// The specific syscall to be detected is sigreturn.
# define SIGRETURN_INST_16 0x80cd
#elif __arm__

// Found in /usr/include/asm/ptrace.h, called from /usr/include/linux/ptrace.h
# define ARM_r0      uregs[0]
# define ARM_ORIG_r0 uregs[17]
# define ARM_sp      uregs[13]
# define ARM_lr      uregs[14]
# define ARM_pc      uregs[15]
# define ARM_cpsr    uregs[16]

// FIXME:  ARM also uses sigreturn.  however, more debugging is needed before
// ptrace works for ARM.
# define SIGRETURN_INST_16 -1
#elif __aarch64__
# warning "TODO: Implementation for ARM64."

/* AArch64 uses PTRACE_GETREGSET */
# undef PTRACE_GETREGS
# define PTRACE_GETREGS    PTRACE_GETREGSET
# define SIGRETURN_INST_16 -1
# define NUM_ARM_REGS      18
#else // ifdef __x86_64__
# error Unknown architecture
#endif // ifdef __x86_64__

using namespace dmtcp;

static const unsigned char DMTCP_SYS_sigreturn = 0x77;
static const unsigned char DMTCP_SYS_rt_sigreturn = 0xad;
static const unsigned char linux_syscall[] = { 0xcd, 0x80 };

static void ptrace_detach_user_threads();
static void ptrace_attach_threads(int isRestart);
static void ptrace_wait_for_inferior_to_reach_syscall(pid_t inf, int sysno);
static void ptrace_single_step_thread(Inferior *infInfo, int isRestart);
static PtraceProcState procfs_state(int tid);

extern "C" int
dmtcp_is_ptracing()
{
  return PtraceInfo::instance().isPtracing();
}

void
ptrace_process_pre_suspend_user_thread()
{
  if (PtraceInfo::instance().isPtracing()) {
    ptrace_detach_user_threads();
  }
}

void
ptrace_process_resume_user_thread(int isRestart)
{
  if (PtraceInfo::instance().isPtracing()) {
    ptrace_attach_threads(isRestart);
  }
  JTRACE("Waiting for Sup Attach") (GETTID());
  PtraceInfo::instance().waitForSuperiorAttach();
  JTRACE("Done Waiting for Sup Attach") (GETTID());
}

static void
ptrace_attach_threads(int isRestart)
{
  pid_t inferior;
  int status;

  vector<pid_t>inferiors;
  Inferior *inf;

  inferiors = PtraceInfo::instance().getInferiorVector(GETTID());
  if (inferiors.size() == 0) {
    return;
  }

  JTRACE("Attaching to inferior threads") (GETTID()) (inferiors.size());

  // Attach to all inferior user threads.
  for (size_t i = 0; i < inferiors.size(); i++) {
    inferior = inferiors[i];
    inf = PtraceInfo::instance().getInferior(inferiors[i]);
    JASSERT(inf->state() != PTRACE_PROC_INVALID) (GETTID()) (inferior);
    if (!inf->isCkptThread()) {
      JASSERT(_real_ptrace(PTRACE_ATTACH, inferior, 0, 0) != -1)
        (GETTID()) (inferior) (JASSERT_ERRNO);
      JASSERT(_real_wait4(inferior, &status, __WALL, NULL) != -1)
        (inferior) (JASSERT_ERRNO);
      JASSERT(_real_ptrace(PTRACE_SETOPTIONS, inferior, 0,
                           inf->getPtraceOptions()) != -1)
        (GETTID()) (inferior) (inf->getPtraceOptions()) (JASSERT_ERRNO);

      // Run all user threads until the end of syscall(DMTCP_FAKE_SYSCALL)
      PtraceInfo::instance().processPreResumeAttach(inferior);
      ptrace_wait_for_inferior_to_reach_syscall(inferior, DMTCP_FAKE_SYSCALL);
    }
  }

  // Attach to and run all user ckpthreads until the end of
  // syscall(DMTCP_FAKE_SYSCALL)
  for (size_t i = 0; i < inferiors.size(); i++) {
    inf = PtraceInfo::instance().getInferior(inferiors[i]);
    inferior = inferiors[i];
    if (inf->isCkptThread()) {
      JASSERT(_real_ptrace(PTRACE_ATTACH, inferior, 0, 0) != -1)
        (GETTID()) (inferior) (JASSERT_ERRNO);
      JASSERT(_real_wait4(inferior, &status, __WALL, NULL) != -1)
        (inferior) (JASSERT_ERRNO);
      JASSERT(_real_ptrace(PTRACE_SETOPTIONS, inferior, 0,
                           inf->getPtraceOptions()) != -1)
        (GETTID()) (inferior) (inf->getPtraceOptions()) (JASSERT_ERRNO);

      // Wait for all inferiors to execute dummy syscall 'DMTCP_FAKE_SYSCALL'.
      PtraceInfo::instance().processPreResumeAttach(inferior);
      ptrace_wait_for_inferior_to_reach_syscall(inferior, DMTCP_FAKE_SYSCALL);
    }
  }

  // Singlestep all user threads out of the signal handler
  for (size_t i = 0; i < inferiors.size(); i++) {
    inferior = inferiors[i];
    inf = PtraceInfo::instance().getInferior(inferiors[i]);
    int lastCmd = inf->lastCmd();
    if (!inf->isCkptThread()) {
      /* After attach, the superior needs to singlestep the inferior out of
       * stopthisthread, aka the signal handler. */
      ptrace_single_step_thread(inf, isRestart);
      if (inf->isStopped() && (lastCmd == PTRACE_CONT ||
                               lastCmd == PTRACE_SYSCALL)) {
        JASSERT(_real_ptrace(lastCmd, inferior, 0, 0) != -1)
          (GETTID()) (inferior) (JASSERT_ERRNO);
      }
    }
  }

  // Move ckpthreads to next step (depending on state)
  for (size_t i = 0; i < inferiors.size(); i++) {
    inferior = inferiors[i];
    inf = PtraceInfo::instance().getInferior(inferiors[i]);
    int lastCmd = inf->lastCmd();
    if (inf->isCkptThread() && !inf->isStopped() &&
        (lastCmd == PTRACE_CONT || lastCmd == PTRACE_SYSCALL)) {
      JASSERT(_real_ptrace(lastCmd, inferior, 0, 0) != -1)
        (GETTID()) (inferior) (JASSERT_ERRNO);
    }
  }

  JTRACE("thread done") (GETTID());
}

static void
ptrace_wait_for_inferior_to_reach_syscall(pid_t inferior, int sysno)
{
#if defined(__i386__) || defined(__x86_64__)
  struct user_regs_struct regs;
#elif defined(__arm__)
  struct user_regs regs;
#elif defined(__aarch64__)
  struct user_pt_regs aarch64_regs;
  struct iovec iov;
  iov.iov_base = &aarch64_regs;
  iov.iov_len = sizeof(aarch64_regs);
#endif // if defined(__i386__) || defined(__x86_64__)
  int syscall_number;
  int status;
  int count = 0;
  while (1) {
    count++;
    JASSERT(_real_ptrace(PTRACE_SYSCALL, inferior, 0, 0) == 0)
      (inferior) (JASSERT_ERRNO);
    JASSERT(_real_wait4(inferior, &status, __WALL, NULL) == inferior)
      (inferior) (JASSERT_ERRNO);

#if defined(__aarch64__)
    JASSERT(_real_ptrace(PTRACE_GETREGS, inferior, 0, (void *)&iov) == 0)
      (inferior) (JASSERT_ERRNO);
#else // if defined(__aarch64__)
    JASSERT(_real_ptrace(PTRACE_GETREGS, inferior, 0, &regs) == 0)
      (inferior) (JASSERT_ERRNO);
#endif // if defined(__aarch64__)

#if defined(__i386__) || defined(__x86_64__)
    syscall_number = regs.ORIG_AX_REG;
#elif (__arm__)
    syscall_number = regs.ARM_ORIG_r0;
#elif (__aarch64__)

    /* iov.iov_base points to &aarch64_regs, so it's
     * okay to use it directly here for readability.
     */
    syscall_number = aarch64_regs.regs[8];
#endif // if defined(__i386__) || defined(__x86_64__)
    if (syscall_number == sysno) {
      JASSERT(_real_ptrace(PTRACE_SYSCALL, inferior, 0, (void *)0) == 0)
        (inferior) (JASSERT_ERRNO);
      JASSERT(_real_wait4(inferior, &status, __WALL, NULL) == inferior)
        (inferior) (JASSERT_ERRNO);
      break;
    }
  }
}

static void
ptrace_single_step_thread(Inferior *inferiorInfo, int isRestart)
{
#if defined(__i386__) || defined(__x86_64__)
  struct user_regs_struct regs;
#elif defined(__arm__)
  struct user_regs regs;
#elif defined(__aarch64__)
  static struct user_pt_regs aarch64_regs;
  struct iovec iov;
  iov.iov_base = &aarch64_regs;
  iov.iov_len = sizeof(aarch64_regs);
#endif // if defined(__i386__) || defined(__x86_64__)
  long peekdata;
  unsigned long addr;
  unsigned long int eflags;

  pid_t inferior = inferiorInfo->tid();
  pid_t superior = GETTID();
  int last_command = inferiorInfo->lastCmd();
  char inferior_st = inferiorInfo->state();

  while (1) {
    int status;
    JASSERT(_real_ptrace(PTRACE_SINGLESTEP, inferior, 0, 0) != -1)
      (superior) (inferior) (JASSERT_ERRNO);
    if (_real_wait4(inferior, &status, 0, NULL) == -1) {
      JASSERT(_real_wait4(inferior, &status, __WCLONE, NULL) != -1)
        (superior) (inferior) (JASSERT_ERRNO);
    }
    if (WIFEXITED(status)) {
      JTRACE("thread is dead") (inferior) (WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
      JTRACE("thread terminated by signal") (inferior);
    }

#if defined(__aarch64__)
    JASSERT(_real_ptrace(PTRACE_GETREGS, inferior, 0, (void *)&iov) != -1)
      (superior) (inferior) (JASSERT_ERRNO);
#else // if defined(__aarch64__)
    JASSERT(_real_ptrace(PTRACE_GETREGS, inferior, 0, &regs) != -1)
      (superior) (inferior) (JASSERT_ERRNO);
#endif // if defined(__aarch64__)

#ifdef __x86_64__

    /* For 64 bit architectures. */
    peekdata = _real_ptrace(PTRACE_PEEKDATA, inferior, (void *)regs.IP_REG, 0);
    long inst = peekdata & 0xffff;
    if (inst == SIGRETURN_INST_16 && regs.AX_REG == 0xf)
#elif __i386__

    /* For 32 bit architectures.*/
    peekdata = _real_ptrace(PTRACE_PEEKDATA, inferior, (void *)regs.IP_REG, 0);
    long inst = peekdata & 0xffff;
    if (inst == SIGRETURN_INST_16 && (regs.AX_REG == DMTCP_SYS_sigreturn ||
                                      regs.AX_REG == DMTCP_SYS_rt_sigreturn))
#elif __arm__

    /* For ARM architectures. */
    peekdata = _real_ptrace(PTRACE_PEEKDATA, inferior, (void *)regs.ARM_pc, 0);
    long inst = peekdata & 0xffff;
    if (inst == SIGRETURN_INST_16 && regs.ARM_r0 == 0xf)
#elif __aarch64__

    /* For ARM64 architectures. */

    /* Check if we are returning from a checkpoint signal.
     */
# warning "TODO: Implementation for ARM64."
    peekdata = _real_ptrace(PTRACE_PEEKDATA,
                            inferior,
                            (void *)aarch64_regs.pc,
                            0);
    long inst = peekdata & 0xffff;
    if (inst == SIGRETURN_INST_16 && aarch64_regs.regs[0] == 0xf)
#endif // ifdef __x86_64__
    {
      if (isRestart) { /* Restart time. */
        // FIXME: TODO:
        if (last_command == PTRACE_SINGLESTEP) {
#if defined(__i386__) || defined(__x86_64__)
          if (regs.AX_REG != DMTCP_SYS_rt_sigreturn) {
            addr = regs.SP_REG;
          } else {
            addr = regs.SP_REG + 8;
            addr = _real_ptrace(PTRACE_PEEKDATA, inferior, (void *)addr, 0);
            addr += 20;
          }
#elif defined(__arm__)
          if (regs.ARM_r0 != DMTCP_SYS_rt_sigreturn) {
            addr = regs.ARM_sp;
          } else {
            addr = regs.ARM_sp + 8;
            addr = _real_ptrace(PTRACE_PEEKDATA, inferior, (void *)addr, 0);
            addr += 20;
          }
#endif // if defined(__i386__) || defined(__x86_64__)
          addr += EFLAGS_OFFSET;
          errno = 0;
          JASSERT((int)(eflags = _real_ptrace(PTRACE_PEEKDATA, inferior,
                                              (void *)addr, 0)) != -1)
            (superior) (inferior) (JASSERT_ERRNO);
          eflags |= 0x0100;
          JASSERT(_real_ptrace(PTRACE_POKEDATA, inferior, (void *)addr,
                               (void *)eflags) != -1)
            (superior) (inferior) (JASSERT_ERRNO);
        } else if (inferior_st != PTRACE_PROC_TRACING_STOP) {
          /* TODO: remove in future as GROUP restore becames stable
           *                                                    - Artem */
          JASSERT(_real_ptrace(PTRACE_CONT, inferior, 0, 0) != -1)
            (superior) (inferior) (JASSERT_ERRNO);
        }
      } else { /* Resume time. */
        if (inferior_st != PTRACE_PROC_TRACING_STOP) {
          JASSERT(_real_ptrace(PTRACE_CONT, inferior, 0, 0) != -1)
            (superior) (inferior) (JASSERT_ERRNO);
        }
      }

      /* In case we have checkpointed at a breakpoint, we don't want to
       * hit the same breakpoint twice. Thus this code. */

      // TODO: FIXME: Replace this code with a raise(SIGTRAP) and see what
      // happens
      if (inferior_st == PTRACE_PROC_TRACING_STOP) {
        JASSERT(_real_ptrace(PTRACE_SINGLESTEP, inferior, 0, 0) != -1)
          (superior) (inferior) (JASSERT_ERRNO);
        if (_real_wait4(inferior, &status, 0, NULL) == -1) {
          JASSERT(_real_wait4(inferior, &status, __WCLONE, NULL) != -1)
            (superior) (inferior) (JASSERT_ERRNO);
        }
      }
      break;
    }
  } // while(1)
}

/* This function detaches the user threads. */
static void
ptrace_detach_user_threads()
{
  PtraceProcState pstate;
  int status;
  struct rusage rusage;

  vector<pid_t>inferiors;
  Inferior *inf;

  inferiors = PtraceInfo::instance().getInferiorVector(GETTID());

  for (size_t i = 0; i < inferiors.size(); i++) {
    pid_t inferior = inferiors[i];
    inf = PtraceInfo::instance().getInferior(inferiors[i]);
    void *data = (void *)(unsigned long)dmtcp_get_ckpt_signal();
    pstate = procfs_state(inferiors[i]);
    if (pstate == PTRACE_PROC_INVALID) {
      JTRACE("Inferior does not exist.") (inferior);
      PtraceInfo::instance().eraseInferior(inferior);
      continue;
    }
    inf->setState(pstate);
    inf->semInit();

    if (inf->isCkptThread()) {
      data = NULL;
    }
    int ret = _real_wait4(inferior, &status, __WALL | WNOHANG, &rusage);
    if (ret > 0) {
      if (!WIFSTOPPED(status) || WSTOPSIG(status) != dmtcp_get_ckpt_signal()) {
        inf->setWait4Status(&status, &rusage);
      }
    }
    pstate = procfs_state(inferiors[i]);
    if (pstate == PTRACE_PROC_RUNNING || pstate == PTRACE_PROC_SLEEPING) {
      syscall(SYS_tkill, inferior, SIGSTOP);
      _real_wait4(inferior, &status, __WALL, NULL);
      JASSERT(_real_wait4(inferior, &status, __WALL | WNOHANG, NULL) == 0)
        (inferior) (JASSERT_ERRNO);
    }
    if (_real_ptrace(PTRACE_DETACH, inferior, 0, data) == -1) {
      JASSERT(errno == ESRCH)
        (GETTID()) (inferior) (JASSERT_ERRNO);
      PtraceInfo::instance().eraseInferior(inferior);
      continue;
    }
    pstate = procfs_state(inferiors[i]);
    if (pstate == PTRACE_PROC_STOPPED) {
      kill(inferior, SIGCONT);
    }
    JTRACE("Detached thread") (inferior);
  }
}

static PtraceProcState
procfs_state(int pid)
{
  int fd;
  char buf[512];
  char *str;
  const char *key = "State:";
  int len = strlen(key);

  snprintf(buf, sizeof(buf), "/proc/%d/status", (int)pid);
  fd = _real_open(buf, O_RDONLY, 0);
  if (fd < 0) {
    JTRACE("open() failed") (buf);
    return PTRACE_PROC_INVALID;
  }


  Util::readAll(fd, buf, sizeof buf);
  close(fd);
  str = strstr(buf, key);
  JASSERT(str != NULL);
  str += len;

  while (*str == ' ' || *str == '\t') {
    str++;
  }

  if (strcasestr(str, "T (stopped)") != NULL) {
    return PTRACE_PROC_STOPPED;
  } else if (strcasestr(str, "T (tracing stop)") != NULL) {
    return PTRACE_PROC_TRACING_STOP;
  } else if (strcasestr(str, "S (sleeping)") != NULL ||
             strcasestr(str, "D (disk sleep)") != NULL) {
    return PTRACE_PROC_SLEEPING;
  } else if (strcasestr(str, "R (running)") != NULL) {
    return PTRACE_PROC_RUNNING;
  }
  return PTRACE_PROC_UNDEFINED;
}

/*****************************************************************************
 ****************************************************************************/

extern "C" pid_t
waitpid(pid_t pid, int *stat, int options)
{
  return wait4(pid, stat, options, NULL);
}

extern "C" pid_t
wait4(pid_t pid, void *stat, int options, struct rusage *rusage)
{
  int status;
  struct rusage rusagebuf;
  pid_t retval;
  int *stat_loc = (int *)stat;
  bool repeat = false;

  if (stat_loc == NULL) {
    stat_loc = &status;
  }

  if (rusage == NULL) {
    rusage = &rusagebuf;
  }

  retval = PtraceInfo::instance().getWait4Status(pid, stat_loc, rusage);
  if (retval != -1) {
    return retval;
  }

  do {
    retval = _real_wait4(pid, stat_loc, options, rusage);
    DMTCP_PLUGIN_DISABLE_CKPT();
    if (retval > 0 && PtraceInfo::instance().isInferior(retval)) {
      if (WIFSTOPPED(*stat_loc) &&
          WSTOPSIG(*stat_loc) == dmtcp_get_ckpt_signal()) {
        /* Inferior got STOPSIGNAL, this should not be passed to gdb process as
         * we are performing checkpoint at this time. We should reexecute the
         * _real_wait4 to get the status that the gdb process would want to
         * process.
         */
        repeat = true;
      } else if (WIFSTOPPED(*stat_loc)) {
        PtraceInfo::instance().setLastCmd(retval, -1);
      } else if (WIFEXITED(*stat_loc) || WIFSIGNALED(*stat_loc)) {
        PtraceInfo::instance().eraseInferior(retval);
      }
    }
    DMTCP_PLUGIN_ENABLE_CKPT();
  } while (repeat);

  return retval;
}

extern "C" long
ptrace(enum __ptrace_request request, ...)
{
  va_list ap;
  pid_t pid;
  void *addr;
  void *data;

  va_start(ap, request);
  pid = va_arg(ap, pid_t);
  addr = va_arg(ap, void *);
  data = va_arg(ap, void *);
  va_end(ap);

  DMTCP_PLUGIN_DISABLE_CKPT();
  PtraceInfo::instance().setPtracing();

  long ptrace_ret = _real_ptrace(request, pid, addr, data);

  if (ptrace_ret != -1) {
    PtraceInfo::instance().processSuccessfulPtraceCmd(request, pid,
                                                      addr, data);
  }

  DMTCP_PLUGIN_ENABLE_CKPT();
  return ptrace_ret;
}

/*****************************************************************************
 ****************************************************************************/
typedef td_err_e (*td_thr_get_info_funcptr_t)(const td_thrhandle_t *,
                                              td_thrinfo_t *);
static td_thr_get_info_funcptr_t td_thr_get_info_funcptr = NULL;

static td_err_e
dmtcp_td_thr_get_info(const td_thrhandle_t *th_p, td_thrinfo_t *ti_p)
{
  td_err_e td_err;

  td_err = (*td_thr_get_info_funcptr)(th_p, ti_p);

  if (th_p->th_unique != 0 || (int)ti_p->ti_lid < 40000) {
    JASSERT(dmtcp_real_to_virtual_pid != NULL);
    pid_t virtPid = dmtcp_real_to_virtual_pid((int)ti_p->ti_lid);
    JASSERT(virtPid != (int)ti_p->ti_lid) (virtPid);
    ti_p->ti_lid = (lwpid_t)virtPid;
  }

  // ti_p->ti_lid  =  (lwpid_t) REAL_TO_VIRTUAL_PID ((int) ti_p->ti_lid);
  // ti_p->ti_tid =  (thread_t) REAL_TO_VIRTUAL_PID ((int) ti_p->ti_tid);
  return td_err;
}

/* gdb calls dlsym on td_thr_get_info.  We need to wrap td_thr_get_info for
   tid virtualization. It should be safe to comment this out if you don't
   need to checkpoint gdb.
*/
extern "C" void *dlsym(void *handle, const char *symbol)
{
  static __typeof__(&dlsym)libc_dlsym_fnptr = NULL;
  if (libc_dlsym_fnptr == NULL) {
    libc_dlsym_fnptr = (__typeof__(&dlsym))dmtcp_get_libc_dlsym_addr();
  }

  void *fptr = libc_dlsym_fnptr(handle, symbol);

  if (strcmp(symbol, "td_thr_get_info") == 0 && fptr != NULL) {
    td_thr_get_info_funcptr = (td_thr_get_info_funcptr_t)fptr;
    return (void *)&dmtcp_td_thr_get_info;
  }

  return fptr;
}
