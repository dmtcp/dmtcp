// For arch_prctl() for x86_64
// A modern Linux 'man arch_proctl' suggests sys/prctl.h, for arch_prctl()
//  in libc, but let's be conservative for now.
#include <asm/unistd.h>
#include <unistd.h>
