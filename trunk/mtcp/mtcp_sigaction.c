#define __USE_GNU
#include <ucontext.h>

#ifdef __x86_64__
# include "ucontext_i_sym.h"
# include "mtcp_sigaction_x86_64.ic"
#else
# include "mtcp_sigaction_i386.ic"
#endif
