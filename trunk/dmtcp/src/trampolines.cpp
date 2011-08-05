#include <string.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include "constants.h"
#include "trampolines.h"
#include "syscallwrappers.h"
#include "../jalib/jassert.h"

static trampoline_info_t sbrk_trampoline_info;

/* All calls by glibc to extend or shrink the heap go through __sbrk(). On
 * restart, the kernel may extend the end of data beyond where we want it. So
 * sbrk will present an abstraction corresponding to the original end of heap
 * before restart. FIXME: Potentially a user could call brk() directly, in
 * which case we would want a wrapper for that too. */
static void *sbrk_wrapper(intptr_t increment)
{
  static void *curbrk = NULL;
  void *oldbrk = NULL;
  /* Initialize curbrk. */
  if (curbrk == NULL) {
    /* The man page says syscall returns int, but unistd.h says long int. */
    long int retval = syscall(SYS_brk, NULL);
    curbrk = (void *)retval;
  }
  oldbrk = curbrk;
  curbrk = (void *)((char *)curbrk + increment);
  if (increment > 0) {
    syscall(SYS_brk, curbrk);
  }
  return oldbrk;
}

/* Calls to sbrk will land here. */
static void *sbrk_trampoline(intptr_t increment)
{
  /* Unpatch sbrk. */
  UNINSTALL_TRAMPOLINE(sbrk_trampoline_info);
  void *retval = sbrk_wrapper(increment);
  /* Repatch sbrk. */
  INSTALL_TRAMPOLINE(sbrk_trampoline_info);
  return retval;
}

/* Any trampolines which should be installed are done so via this function.
   Called from DmtcpWorker constructor. */
void _dmtcp_setup_trampolines()
{
  dmtcp_setup_trampoline("sbrk", (void*) &sbrk_trampoline,
                         &sbrk_trampoline_info);
}
