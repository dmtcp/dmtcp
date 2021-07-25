#ifndef _LOWER_HALF_API_H
#define _LOWER_HALF_API_H

#include <stdint.h>

#include "libproxy.h"

#define GENERATE_ENUM(ENUM)    MPI_Fnc_##ENUM
#define GENERATE_FNC_PTR(FNC)  &MPI_##FNC
#define GENERATE_FNC_STRING(FNC)  "MPI_" #FNC
#define PAGE_SIZE              0x1000
#define HUGE_PAGE              0x200000

#ifdef MAIN_AUXVEC_ARG
/* main gets passed a pointer to the auxiliary.  */
# define MAIN_AUXVEC_DECL , void *
# define MAIN_AUXVEC_PARAM , auxvec
#else
# define MAIN_AUXVEC_DECL
# define MAIN_AUXVEC_PARAM
#endif // ifdef MAIN_AUXVEC_ARG

#define ROUND_UP(addr)  \
    ((unsigned long)(addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

#define ROUND_DOWN(addr) ((unsigned long)addr & ~(PAGE_SIZE - 1))

#define ROUND_UP_HUGE(addr) \
    ((unsigned long)(addr + HUGE_PAGE - 1) & ~(HUGE_PAGE - 1))

// Shared data structures

typedef struct __MemRange
{
  void *start;  // Start of the address range for lower half memory allocations
  void *end;    // End of the address range for lower half memory allocations
} MemRange_t;

typedef struct __MmapInfo
{
  void *addr;   // Start address of mmapped region
  size_t len;   // Length (in bytes) of mmapped region
  int unmapped; // 1 if the region was unmapped; 0 otherwise
  int guard;    // 1 if the region has additional guard pages around it; 0 otherwise
} MmapInfo_t;

// The transient lh_proxy process introspects its memory layout and passes this
// information back to the main application process using this struct.
typedef struct _LowerHalfInfo
{
  void *startText; // Start address of text segment (R-X) of lower half
  void *endText;   // End address of text segmeent (R-X) of lower half
  void *startData; // Start address of data segment (RW-) of lower half
  void *endOfHeap; // Pointer to the end of heap segment of lower half
  void *libc_start_main; // Pointer to libc's __libc_start_main function in statically-linked lower half
  void *main;      // Pointer to the main() function in statically-linked lower half
  void *libc_csu_init; // Pointer to libc's __libc_csu_init() function in statically-linked lower half
  void *libc_csu_fini; // Pointer ot libc's __libc_csu_fini() function in statically-linked lower half
  void *fsaddr; // The base value of the FS register of the lower half
  uint64_t lh_AT_PHNUM; // The number of program headers (AT_PHNUM) from the auxiliary vector of the lower half
  uint64_t lh_AT_PHDR;  // The address of the program headers (AT_PHDR) from the auxiliary vector of the lower half
  void *g_appContext; // Pointer to ucontext_t of upper half application (defined in the lower half)
  void *lh_dlsym;     // Pointer to mydlsym() function in the lower half
  void *getRankFptr;  // Pointer to getRank() function in the lower half
  void *parentStackStart; // Address to the start of the stack of the parent process (FIXME: Not currently used anywhere)
  void *updateEnvironFptr; // Pointer to updateEnviron() function in the lower half
  void *getMmappedListFptr; // Pointer to getMmapedList() function in the lower half
  void *resetMmappedListFptr; // Pointer to resetMmapedList() function in the lower half
  MemRange_t memRange; // MemRange_t object in the lower half
} LowerHalfInfo_t;

enum MPI_Fncs {
  MPI_Fnc_NULL,
  FOREACH_FNC(GENERATE_ENUM)
  MPI_Fnc_Invalid,
};

__attribute__ ((unused))
static const char *MPI_Fnc_strings[] = {
  "MPI_Fnc_NULL",
  FOREACH_FNC(GENERATE_FNC_STRING)
  "MPI_Fnc_Invalid"
};


// Useful type definitions

typedef int (*mainFptr)(int argc, char *argv[], char *envp[]);
typedef void (*finiFptr) (void);
typedef int (*libcFptr_t) (int (*main) (int, char **, char ** MAIN_AUXVEC_DECL),
                           int ,
                           char **,
                           __typeof (main) ,
                           void (*fini) (void),
                           void (*rtld_fini) (void),
                           void *);

typedef void* (*proxyDlsym_t)(enum MPI_Fncs fnc);
typedef void* (*updateEnviron_t)(char **environ);
typedef MmapInfo_t* (*getMmappedList_t)(int *num);
typedef void (*resetMmappedList_t)();

// Global variables with lower-half information

// startProxy() (called from splitProcess()) will initialize 'lh_info'
extern LowerHalfInfo_t lh_info;
// Pointer to the custom dlsym implementation (see mydlsym() in libproxy.c) in
// the lower half. This is initialized using the information passed to us by
// the transient lh_proxy process in DMTCP_EVENT_INIT.
// initializeLowerHalf() will initialize this to: (proxyDlsym_t)lh_info.lh_dlsym
extern proxyDlsym_t pdlsym;

// API

// Returns the address of an MPI API in the lower half's MPI library based on
// the given enum value
extern void *mydlsym(enum MPI_Fncs fnc);

// Initializes the MPI library in the lower half (by calling MPI_Init()) and
// returns the MPI rank of the current process
extern int getRank();

// Updates the lower half's global environ pointer (__environ) to the given
// 'newenviron' pointer value
extern void updateEnviron(const char **newenviron);

// Returns a pointer to the first element of a pre-allocated array of
// 'MmapInfo_t' objects and 'num' is set to the number of valid items in
// the array
extern MmapInfo_t* getMmappedList(int *num);

// Clears the global, pre-allocated array of 'MmapInfo_t' objects
extern void resetMmappedList();


#endif // ifndef _LOWER_HALF_API_H
