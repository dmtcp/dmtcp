#ifndef MEM_BARRIER_H
#define MEM_BARRIER_H

#if defined(__i386__) || defined(__x86_64__)
# if defined(__i386__) && defined(__PIC__)
// FIXME:  After DMTCP-1.2.5, this can be made only case for i386/x86_64
#  define RMB asm volatile ("lfence" \
                           : : : "eax", "ecx", "edx", "memory")
#  define WMB asm volatile ("sfence" \
                           : : : "eax", "ecx", "edx", "memory")
#  define IMB
# else
#  define RMB asm volatile ("xorl %%eax,%%eax ; cpuid" \
                           : : : "eax", "ebx", "ecx", "edx", "memory")
#  define WMB asm volatile ("xorl %%eax,%%eax ; cpuid" \
                           : : : "eax", "ebx", "ecx", "edx", "memory")
#  define IMB
# endif
#elif defined(__arm__)
# define RMB asm volatile ("dsb ; dmb" : : : "memory")
# define WMB asm volatile ("dsb ; dmb" : : : "memory")
# define IMB asm volatile ("isb" : : : "memory")
#else
# error "instruction architecture not implemented"
#endif

#endif
