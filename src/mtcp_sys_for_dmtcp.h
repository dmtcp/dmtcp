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
