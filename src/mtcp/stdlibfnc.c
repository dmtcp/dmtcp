#include "mtcp_util.h"
#include "mtcp_sys.h"
#include <stdint.h>

#if defined(__powerpc64__) || defined(__ppc64__)
extern int main(int argc, char *argv[], char **environ);

void
mtcp_ppc64_start(void *initial_sp)
{
  int mtcp_sys_errno;
  long *stack = (long *)initial_sp;
  int argc = (int)stack[0];
  char **argv = (char **)&stack[1];
  char **envp = argv + argc + 1;

  if (initial_sp == NULL) {
    mtcp_abort();
  }

  if (argc < 0 || argc > 131072) {
    mtcp_abort();
  }

  {
    int result = main(argc, argv, envp);
    mtcp_sys_exit(result);
  }
  (void)mtcp_sys_errno;
  while (1) {}
}
#endif

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
  int result;
#if !defined(__powerpc64__) && !defined(__ppc64__)
  char **envp;
#endif

#if defined(__powerpc64__) || defined(__ppc64__)
  (void)main;
  (void)argc;
  (void)argv;
  (void)init;
  (void)fini;
  (void)rtld_fini;
  (void)stack_end;
  mtcp_abort();
  result = 1;
#else
  if (main == NULL) {
    MTCP_PRINTF("FATAL: __libc_start_main received NULL main pointer\n");
    mtcp_abort();
  }

  if (argv == NULL) {
    MTCP_PRINTF("FATAL: __libc_start_main received NULL argv pointer\n");
    mtcp_abort();
  }

  envp = argv + argc + 1;
  result = main(argc, argv, envp);
#endif

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

  MTCP_PRINTF("MTCP Internal Error: %s Not Implemented.\n", __func__);
  mtcp_abort();
}

void
__gcc_personality_v0(void)
{
  int mtcp_sys_errno;

  MTCP_PRINTF("MTCP Internal Error: %s Not Implemented.\n", __func__);
  mtcp_abort();
}

void
__intel_security_cookie(void)
{
  int mtcp_sys_errno;

  MTCP_PRINTF("MTCP Internal Error: %s Not Implemented.\n", __func__);
  mtcp_abort();
}

void
__intel_security_check_cookie(void)
{
  int mtcp_sys_errno;

  MTCP_PRINTF("MTCP Internal Error: %s Not Implemented.\n", __func__);
  mtcp_abort();
}
