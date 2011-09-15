#include <sys/types.h>
#include "uniquepid.h"
#include "../jalib/jalloc.h"
#include "constants.h"
#include "ptrace.h"
#include "ptracewrappers.h"
#ifdef PTRACE

static struct ptrace_info callbackGetNextPtraceInfo (int index);
static void callbackPtraceInfoListCommand (struct cmd_info cmd);
static void callbackJalibCkptUnlock ();
static int callbackPtraceInfoListSize ();

sigset_t signals_set;

MtcpPtraceFuncPtrs_t mtcpPtraceFuncPtrs;

static void initializeMtcpPtraceFuncPtrs()
{
  mtcpPtraceFuncPtrs.init_thread_local =
    (mtcp_init_thread_local_t) get_mtcp_symbol("mtcp_init_thread_local");
  mtcpPtraceFuncPtrs.init_ptrace =
    (mtcp_init_ptrace_t) get_mtcp_symbol("mtcp_init_ptrace");
  mtcpPtraceFuncPtrs.set_ptrace_callbacks =
    (mtcp_set_ptrace_callbacks_t) get_mtcp_symbol("mtcp_set_ptrace_callbacks");

  mtcpPtraceFuncPtrs.get_ptrace_waitpid_info =
    (mtcp_get_ptrace_waitpid_info_t)
      get_mtcp_symbol("mtcp_get_ptrace_waitpid_info");

  mtcpPtraceFuncPtrs.is_ptracing =
    (mtcp_is_ptracing_t) get_mtcp_symbol("mtcp_is_ptracing");
}

void initializeMtcpPtraceEngine()
{
  // FIXME: Do we need this anymore?
  sigemptyset (&signals_set);
  // FIXME: Suppose the user did:  dmtcp_checkpoint --mtcp-checkpoint-signal ..
  sigaddset (&signals_set, MTCP_DEFAULT_SIGNAL);

  initializeMtcpPtraceFuncPtrs();

  (*mtcpPtraceFuncPtrs.init_ptrace)(dmtcp::UniquePid::getTmpDir().c_str());

  (*mtcpPtraceFuncPtrs.set_ptrace_callbacks)(&callbackGetNextPtraceInfo,
                                       &callbackPtraceInfoListCommand,
                                       &callbackJalibCkptUnlock,
                                       &callbackPtraceInfoListSize
                                      );
}

void ptraceProcessCloneStartFn()
{
  mtcpPtraceFuncPtrs.init_thread_local();
}

void ptraceCallbackPreCheckpoint()
{
  if (!mtcpPtraceFuncPtrs.is_ptracing()) {
    JALIB_CKPT_UNLOCK();
  }
}

static struct ptrace_info callbackGetNextPtraceInfo (int index)
{
  return get_next_ptrace_info(index);
}

static void callbackPtraceInfoListCommand (struct cmd_info cmd)
{
  ptrace_info_list_command(cmd);
}

static void callbackJalibCkptUnlock ()
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

#endif
