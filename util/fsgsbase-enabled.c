#include <sys/auxv.h>
#include <elf.h>
#include <stdio.h>

/* The user-space FSGSBASE patch was added to Linux kernel version 5.9. */

/* Will be eventually in asm/hwcap.h */
#ifndef HWCAP2_FSGSBASE
# define HWCAP2_FSGSBASE (1 << 1)
#endif

int main() {
  unsigned val = getauxval(AT_HWCAP2);
  if (val & HWCAP2_FSGSBASE) {
    printf("FSGSBASE enabled (both in CPU and Linux kernel)\n");
    return 0;
  } else {
    printf("FSGSBASE _NOT_ enabled\n");
    return 1;
  }
}

/* If FSGSBASE is enabled, then the following user-space versions:
 *   unsigned long int fsbase;
 *   asm volatile("rex.W\n rdfsbase %0" : "=r" (fsbase) :: "memory");
 *   asm volatile("rex.W\n wrfsbase %0" :: "r" (fsbase) : "memory");
 * can replace:
 *   unsigned long int fsbase;
 *   syscall(SYS_arch_prctl, ARCH_GET_FS, fsbase);
 *   syscall(SYS_arch_prctl, ARCH_SET_FS, fsbase);
 */
