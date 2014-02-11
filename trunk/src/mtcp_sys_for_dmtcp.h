// For arch_prctl() for x86_64
// A modern Linux 'man arch_proctl' suggests sys/prctl.h, for arch_prctl()
//  in libc, but let's be conservative for now.
#include <asm/unistd.h>
#include <unistd.h>

#ifdef __i386__
// For get_thread_area, set_thread_area
# define _GNU_SOURCE
# include <unistd.h>
# include <sys/syscall.h>
# include <linux/unistd.h>
# include <asm/ldt.h>
#endif

#ifdef __arm__
# if 1
/* Next macro uses 'mcr', a kernel-mode instr. on ARM */
#  define mtcp_sys_kernel_set_tls(args...)  \
    syscall(__ARM_NR_set_tls,args)
# else
   /* else inline kernel call */
#  include "mtcp/mtcp_sys.h"
# endif
#endif
