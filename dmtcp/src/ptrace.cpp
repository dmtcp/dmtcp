#include <sys/types.h>
#include <dlfcn.h>
#include "../jalib/jalloc.h"
#include "../jalib/jassert.h"
#include "ptrace.h"
#include "mtcp_ptrace.h"
#include "ptracewrappers.h"
#include "dmtcpmodule.h"
#include <sys/stat.h>
#ifdef PTRACE

static struct ptrace_info callbackGetNextPtraceInfo (int index);
static void callbackPtraceInfoListCommand (struct cmd_info cmd);
static void callbackJalibCkptUnlock ();
static int callbackPtraceInfoListSize ();

static int originalStartup = 1;

void ptraceProcessCloneStartFn()
{
  mtcp_init_thread_local();
}

void ptraceProcessThreadCreation(void *data)
{
  pid_t tid = (pid_t) (unsigned long) data;
  mtcp_ptrace_process_thread_creation(tid);
}

extern "C" void jalib_ckpt_unlock()
{
  JALIB_CKPT_UNLOCK();
}

static int callbackPtraceInfoListSize ()
{
  return ptrace_info_list_size();
}

# ifndef RECORD_REPLAY
   // RECORD_REPLAY defines its own __libc_memalign wrapper.
   // So, we won't interfere with it here.
#  include <malloc.h>
// This is needed to fix what is arguably a bug in libdl-2.10.so
//   (and probably extending from versions 2.4 at least through 2.11).
// In libdl-2.10.so dl-tls.c:allocate_and_init  calls __libc_memalign
//    but dl-tls.c:dl_update_slotinfo just calls free .
// So, TLS is allocated by libc malloc and can be freed by a malloc library
//    defined by user.  This is a bug.
// This happens only in a multi-threaded programs for which TLS is allocated.
// So, we intercept __libc_memalign and point it to memalign to have a match.
// We do the same for __libc_free.  libdl.so doesn't currently define
//    __libc_free, but the code must be prepared to accept this.
// An alternative to defining __libc_memalign would have been using
//    the glibc __memalign_hook() function.
//extern "C"
//void *__libc_memalign(size_t boundary, size_t size) {
//  return memalign(boundary, size);
//}
//// libdl.so doesn't define __libc_free, but in case it does in the future ...
//extern "C"
//void __libc_free(void * ptr) {
//  free(ptr);
//}
# endif

void ptraceInit()
{
  mtcp_init_ptrace();
  ptrace_init_data_structures();
}

void mtcp_process_stop_signal_event(void *data)
{
  JASSERT(data != NULL);
  DmtcpSendStopSignalInfo *info = (DmtcpSendStopSignalInfo*) data;

  mtcp_ptrace_send_stop_signal(info->tid, info->retry_signalling, info->retval);
}
void ptraceProcessWaitForSuspendMsg()
{
  if (originalStartup) {
    originalStartup = 0;
  } else {
    mtcp_ptrace_process_post_restart_resume_ckpt_thread();
  }

  if (mtcp_is_ptracing()) {
    /* No need for a mutex. We're before the barrier. */
    jalib_ckpt_unlock_ready = 0;
  }
  mtcp_ptrace_process_post_ckpt_resume_ckpt_thread();
}

void ptraceProcessGotSuspendMsg(void *data)
{
  /* One of the threads is the ckpt thread. Don't count that in. */
  // FIXME: Take care of invalid threads
  nthreads = (unsigned long) data;
  mtcp_ptrace_process_pre_suspend_ckpt_thread();
}

void ptraceProcessStartPreCkptCB()
{
  mtcp_ptrace_process_post_suspend_ckpt_thread();
}

void ptraceProcessResumeUserThread(void *data)
{
  DmtcpResumeUserThreadInfo *info = (DmtcpResumeUserThreadInfo*) data;
  mtcp_ptrace_process_resume_user_thread(info->is_ckpt, info->is_restart);
}

extern "C" void ptrace_dmtcp_process_event(DmtcpEvent_t event, void* data)
{
  switch (event) {
    case DMTCP_EVENT_INIT:
      ptraceInit();
      break;
    case DMTCP_EVENT_WAIT_FOR_SUSPEND_MSG:
      ptraceProcessWaitForSuspendMsg();
      break;
    case DMTCP_EVENT_GOT_SUSPEND_MSG:
      ptraceProcessGotSuspendMsg(data);
      break;
    case DMTCP_EVENT_START_PRE_CKPT_CB:
      ptraceProcessStartPreCkptCB();
      break;
    case DMTCP_EVENT_THREAD_CREATED:
      ptraceProcessThreadCreation(data);
      break;
    case DMTCP_EVENT_CKPT_THREAD_START:
      mtcp_ptrace_process_ckpt_thread_creation();
      break;
    case DMTCP_EVENT_THREAD_START:
      ptraceProcessCloneStartFn();
      break;
    case DMTCP_EVENT_PRE_SUSPEND_USER_THREAD:
      mtcp_ptrace_process_pre_suspend_user_thread();
      break;
    case DMTCP_EVENT_RESUME_USER_THREAD:
      ptraceProcessResumeUserThread(data);
      break;
    case DMTCP_EVENT_SEND_STOP_SIGNAL:
      mtcp_process_stop_signal_event(data);
      break;

    case DMTCP_EVENT_PRE_EXIT:
    case DMTCP_EVENT_PRE_CHECKPOINT:
    case DMTCP_EVENT_POST_LEADER_ELECTION:
    case DMTCP_EVENT_POST_DRAIN:
    case DMTCP_EVENT_POST_CHECKPOINT:
    case DMTCP_EVENT_POST_RESTART:
    default:
      break;
  }

  DMTCP_CALL_NEXT_PROCESS_DMTCP_EVENT(event, data);
  return;
}

extern "C" const char* ptrace_get_tmpdir()
{
  char ptrace_tmpdir[256];
  strcpy(ptrace_tmpdir, dmtcp_get_tmpdir());
  strcat(ptrace_tmpdir, "/");
  strcat(ptrace_tmpdir, dmtcp_get_computation_id_str());

  struct stat buf;
  if (stat(ptrace_tmpdir, &buf)) {
    if (mkdir(ptrace_tmpdir, S_IRWXU)) {
      printf("Error creating tmp directory %s, error: \n",
             ptrace_tmpdir, strerror(errno));
      abort();
    }
  }
  return ptrace_tmpdir;
}

#endif
