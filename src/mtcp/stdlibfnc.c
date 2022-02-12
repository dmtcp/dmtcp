#include "mtcp_util.h"
#include "mtcp_sys.h"

int
__libc_start_main(int (*main)(int, char **, char **MAIN_AUXVEC_DECL),
                  int argc,
                  char **argv,
                  __typeof (main) init,
                  void (*fini) (void),
                  void (*rtld_fini) (void),
                  void *stack_end)
{
  int mtcp_sys_errno;
  char **envp = argv + argc + 1;
  int result = main(argc, argv, envp);

  mtcp_sys_exit(result);
  (void)mtcp_sys_errno; /* Stop compiler warning about unused variable */
  while (1) {}
}

int
__libc_csu_init(int argc, char **argv, char **envp)
{
  return 0;
}

void
__libc_csu_fini(void) {}

void __stack_chk_fail(void);   /* defined at end of file */
void
abort(void) { mtcp_abort(); }

/* Implement memcpy() and memset() inside mtcp_restart. Although we are not
 * calling memset, the compiler may generate a call to memset() when trying to
 * initialize a large array etc.
 */
void *
memset(void *s, int c, size_t n)
{
  return mtcp_memset(s, c, n);
}

void *
memcpy(void *dest, const void *src, size_t n)
{
  return mtcp_memcpy(dest, src, n);
}

// gcc can generate calls to these.
// Eventually, we'll isolate the PIC code in a library, and this can go away.
void
__stack_chk_fail(void)
{
  int mtcp_sys_errno;

  MTCP_PRINTF("ERROR: Stack Overflow detected.\n");
  mtcp_abort();
}

void
__stack_chk_fail_local(void)
{
  int mtcp_sys_errno;

  MTCP_PRINTF("ERROR: Stack Overflow detected.\n");
  mtcp_abort();
}

void
__stack_chk_guard(void)
{
  int mtcp_sys_errno;

  MTCP_PRINTF("ERROR: Stack Overflow detected.\n");
  mtcp_abort();
}

void
_Unwind_Resume(void)
{
  int mtcp_sys_errno;

  MTCP_PRINTF("MTCP Internal Error: %s Not Implemented.\n", __FUNCTION__);
  mtcp_abort();
}

void
__gcc_personality_v0(void)
{
  int mtcp_sys_errno;

  MTCP_PRINTF("MTCP Internal Error: %s Not Implemented.\n", __FUNCTION__);
  mtcp_abort();
}

void
__intel_security_cookie(void)
{
  int mtcp_sys_errno;

  MTCP_PRINTF("MTCP Internal Error: %s Not Implemented.\n", __FUNCTION__);
  mtcp_abort();
}

void
__intel_security_check_cookie(void)
{
  int mtcp_sys_errno;

  MTCP_PRINTF("MTCP Internal Error: %s Not Implemented.\n", __FUNCTION__);
  mtcp_abort();
}
