#ifndef PTRACE_H
#define PTRACE_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifndef EXTERNC
# ifdef __cplusplus
#  define EXTERNC extern "C"
# else
#  define EXTERNC
# endif
#endif

enum truefalse {
  FALSE = 0,
  TRUE
};

#define LIB_PRIVATE __attribute__ ((visibility ("hidden")))

EXTERNC struct ptrace_info *get_next_ptrace_info(int index);
EXTERNC sigset_t signals_set;
EXTERNC void jalib_ckpt_unlock();
EXTERNC const char* ptrace_get_tmpdir();
#endif
