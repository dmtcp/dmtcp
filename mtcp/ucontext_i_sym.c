/* This is taken from glibc-2-12:./sysdeps/unix/sysv/linux/x86_64/ucontext_i.sym
 * and modified.  It is used as:
 *   gcc -o ucontext_i_sym ucontext_i_sym.c
 *   ./ucontext_i_sym > ucontext_i_sym.h
 *   rm ucontext_i_sym
 * It is needed by mtcp_sigaction_x86_64.c
 */

#include <stdio.h>
#define __USE_GNU
#include <ucontext.h>

#define offsetof(type,memb) ((int)(size_t)&((type*)0)->memb)

#define ucontext(member)	offsetof (ucontext_t, member)
#define mcontext(member)	ucontext (uc_mcontext.member)
#define mreg(reg)		mcontext (gregs[REG_##reg])

int main() {
printf("#define oRBP %d\n",		mreg (RBP));
printf("#define oRSP %d\n",		mreg (RSP));
printf("#define oRBX %d\n",		mreg (RBX));
printf("#define oR8 %d\n",		mreg (R8));
printf("#define oR9 %d\n",		mreg (R9));
printf("#define oR10 %d\n",		mreg (R10));
printf("#define oR11 %d\n",		mreg (R11));
printf("#define oR12 %d\n",		mreg (R12));
printf("#define oR13 %d\n",		mreg (R13));
printf("#define oR14 %d\n",		mreg (R14));
printf("#define oR15 %d\n",		mreg (R15));
printf("#define oRDI %d\n",		mreg (RDI));
printf("#define oRSI %d\n",		mreg (RSI));
printf("#define oRDX %d\n",		mreg (RDX));
printf("#define oRAX %d\n",		mreg (RAX));
printf("#define oRCX %d\n",		mreg (RCX));
printf("#define oRIP %d\n",		mreg (RIP));
printf("#define oEFL %d\n",		mreg (EFL));
printf("#define oFPREGS %d\n",		mcontext (fpregs));
printf("#define oSIGMASK %d\n",	ucontext (uc_sigmask));
printf("#define oFPREGSMEM %d\n",	ucontext (__fpregs_mem));
printf("#define oMXCSR %d\n",		ucontext (__fpregs_mem.mxcsr));
return 0;
}
