#ifndef _MPI_COPYBITS_H
#define _MPI_COPYBITS_H

#include <stdint.h>
#include <ucontext.h>
#include <link.h>
#include <unistd.h>

#include "lower_half_api.h"

extern int main(int argc, char *argv[], char *envp[]);
extern int __libc_csu_init (int argc, char **argv, char **envp);
extern void __libc_csu_fini (void);

extern int __libc_start_main(int (*main)(int, char **, char **MAIN_AUXVEC_DECL),
                             int argc,
                             char **argv,
                             __typeof (main) init,
                             void (*fini) (void),
                             void (*rtld_fini) (void),
                             void *stack_end);

extern LowerHalfInfo_t lh_info;
extern MemRange_t lh_memRange;

#endif // #ifndef _MPI_COPYBITS_H
