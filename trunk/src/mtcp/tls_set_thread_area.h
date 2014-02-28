#ifndef TLS_SET_THREAD_AREA_H
#define TLS_SET_THREAD_AREA_H

/* These functions are not defined for x86_64. */
#ifdef __i386__
# define tls_set_thread_area(args...) mtcp_sys_set_thread_area(args)
#endif

#ifdef __x86_64__
# include <asm/prctl.h>
# include <sys/prctl.h>
/* man arch_prctl has both signatures, and prctl.h above has no declaration.
 *  int arch_prctl(int code, unsigned long addr);
 *  int arch_prctl(int code, unsigned long addr);
 */
int arch_prctl();
static unsigned long int myinfo_gs;
# define tls_set_thread_area(uinfo) \
    ( mtcp_inline_syscall(arch_prctl,2,ARCH_SET_FS, \
	*(unsigned long int *)&(((struct user_desc *)uinfo)->base_addr)), \
      mtcp_inline_syscall(arch_prctl,2,ARCH_SET_GS, myinfo_gs) \
    )
#endif /* end __x86_64__ */

#ifdef __arm__
/* This allocation hack will work only if calls to mtcp_sys_get_thread_area
 * and mtcp_sys_get_thread_area are both inside the same file (mtcp.c).
 * This is all because get_thread_area is not implemented for arm.
 *     For ARM, the thread pointer seems to point to the next slot
 * after the 'struct pthread'.  Why??  So, we subtract that address.
 * After that, tid/pid will be located at  offset 104/108 as expected
 * for glibc-2.13.
 * NOTE:  'struct pthread' defined in glibc/nptl/descr.h
 *     The value below (1216) is current for glibc-2.13.
 *     May have to update 'sizeof(struct pthread)' for new versions of glibc.
 *     We can automate this by searching for negative offset from end
 *     of 'struct pthread' in tls_tid_offset, tls_pid_offset in mtcp.c.
 */
static unsigned int myinfo_gs;
# define tls_set_thread_area(uinfo) \
    ( myinfo_gs = \
        *(unsigned long int *)&(((struct user_desc *)uinfo)->base_addr), \
      (mtcp_sys_kernel_set_tls(myinfo_gs+1216), 0) \
      /* 0 return value at end means success */ )
#endif /* end __arm__ */

#endif
