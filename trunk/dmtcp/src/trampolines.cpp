#include <string.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <../jalib/jassert.h>
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
#include "synchronizationlogging.h"
#endif
#include "syscallwrappers.h"

// Defines void _dmtcp_setup_trampolines()
//   (used in constructor in dmtcpworker.cpp)

#ifdef __x86_64__
static char asm_jump[] = {
    // mov    $0x1234567812345678,%rax
    0x48, 0xb8, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 
    // jmpq   *%rax
    0xff, 0xe0
};
// Beginning of address in asm_jump:
# define ADDR_OFFSET 2
#else
static char asm_jump[] = {
    0xb8, 0x78, 0x56, 0x34, 0x12, // mov    $0x12345678,%eax
    0xff, 0xe0                    // jmp    *%eax
};
// Beginning of address in asm_jump:
# define ADDR_OFFSET 1
#endif

#define ASM_JUMP_LEN sizeof(asm_jump)
#define INSTALL_TRAMPOLINE(name) \
  memcpy(name##_addr, name##_trampoline_jump, ASM_JUMP_LEN)
#define UNINSTALL_TRAMPOLINE(name) \
  memcpy(name##_addr, name##_displaced_instructions, ASM_JUMP_LEN)
#define SETUP_TRAMPOLINE(func)                                          \
  do {                                                                  \
    long pagesize = sysconf(_SC_PAGESIZE);                              \
    long page_base;                                                     \
    /************ Find libc func and set up permissions. **********/    \
    /* We assume that no one is wrapping func yet. */                   \
    void *handle = dlopen(LIBC_FILENAME, RTLD_NOW);                     \
    func##_addr = dlsym(handle, #func);                                 \
    /* Base address of page where func resides. */                      \
    page_base = (long)func##_addr - ((long)func##_addr % pagesize);     \
    /* Give that whole page RWX permissions. */                         \
    int retval = mprotect((void *)page_base, pagesize,                  \
        PROT_READ | PROT_WRITE | PROT_EXEC);                            \
    JASSERT ( retval != -1 ) ( errno );                                 \
    /************ Set up trampoline injection code. ***********/        \
    /* Trick to get "free" conversion of a long value to the            \
       character-array representation of that value. Different sizes of \
       long and endian-ness are handled automatically. */               \
    union u {                                                           \
      long val;                                                         \
      char bytes[sizeof(long)];                                         \
    } data;                                                             \
    data.val = (long)&func##_trampoline;                                \
    memcpy(func##_trampoline_jump, asm_jump, ASM_JUMP_LEN);              \
    /* Insert real trampoline address into injection code. */           \
    memcpy(func##_trampoline_jump+ADDR_OFFSET, data.bytes, sizeof(long)); \
    /* Save displaced instructions for later restoration. */            \
    memcpy(func##_displaced_instructions, func##_addr, ASM_JUMP_LEN);   \
    /* Inject trampoline. */                                            \
    INSTALL_TRAMPOLINE(func);                                           \
  } while (0)

static char sbrk_trampoline_jump[ASM_JUMP_LEN];
static char sbrk_displaced_instructions[ASM_JUMP_LEN];
static void *sbrk_addr = NULL;

#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
static char mmap_trampoline_jump[ASM_JUMP_LEN];
static char mmap_displaced_instructions[ASM_JUMP_LEN];
static void *mmap_addr = NULL;
/* Used by _mmap_no_sync(). */
__attribute__ ((visibility ("hidden"))) __thread int mmap_no_sync = 0;
static inline void memfence() {  asm volatile ("mfence" ::: "memory"); }
#endif // SYNCHRONIZATION_LOG_AND_REPLAY


/* Read-write lock initializers.  */
#ifdef __USE_GNU
# if __WORDSIZE == 64
#  define PTHREAD_RWLOCK_PREFER_WRITER_RECURSIVE_INITIALIZER_NP \
 { { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,					      \
       PTHREAD_RWLOCK_PREFER_WRITER_NP } }
# else
#  if __BYTE_ORDER == __LITTLE_ENDIAN
#   define PTHREAD_RWLOCK_PREFER_WRITER_RECURSIVE_INITIALIZER_NP \
 { { 0, 0, 0, 0, 0, 0, PTHREAD_RWLOCK_PREFER_WRITER_NP, \
     0, 0, 0, 0 } }
#  else
#   define PTHREAD_RWLOCK_PREFER_WRITER_RECURSIVE_INITIALIZER_NP \
 { { 0, 0, 0, 0, 0, 0, 0, 0, 0, PTHREAD_RWLOCK_PREFER_WRITER_NP,\
     0 } }
#  endif
# endif
#endif


/* All calls by glibc to extend or shrink the heap go through __sbrk(). On
 * restart, the kernel may extend the end of data beyond where we want it. So
 * sbrk will present an abstraction corresponding to the original end of heap
 * before restart. FIXME: Potentially a user could call brk() directly, in
 * which case we would want a wrapper for that too. */
static void *sbrk_wrapper(intptr_t increment)
{
  static void *curbrk = NULL;
  void *oldbrk = NULL;
  /* Initialize curbrk. */
  if (curbrk == NULL) {
    /* The man page says syscall returns int, but unistd.h says long int. */
    long int retval = syscall(SYS_brk, NULL);
    curbrk = (void *)retval;
  } 
  oldbrk = curbrk;
  curbrk = (void *)((char *)curbrk + increment);
  if (increment > 0) {
    syscall(SYS_brk, curbrk);
  }
  return oldbrk;
}

/* Calls to sbrk will land here. */
static void sbrk_trampoline(intptr_t increment)
{
  /* Save registers we will clobber (why doesn't the compiler do this?) */
#ifdef __x86_64__
  asm("pushq %rcx\n"
      "pushq %rdx");
#else
  asm("push %ecx\n"
      "push %edx");
#endif
  /* Unpatch sbrk. */
  UNINSTALL_TRAMPOLINE(sbrk);
  void *retval = sbrk_wrapper(increment);
  /* Repatch sbrk. */
  INSTALL_TRAMPOLINE(sbrk);
#ifdef __x86_64__
  asm("mov %0,%%rax\n"    /* Set return value */
      "pop %%rdx\n"       /* Restore clobbered registers. */
      "pop %%rcx\n"
      "add $0x20,%%rsp\n" /* Reset stack pointer */
      "pop %%rbp\n"
      "ret":: "r"(retval)); /* Return to caller. */
#else
  asm("mov %0,%%eax\n"    /* Set return value */
      "pop %%edx\n"       /* Restore clobbered registers. */
      "pop %%ecx\n"
      "add $0x24,%%esp\n" /* Reset stack pointer */
      "pop %%ebx\n"
      "pop %%ebp\n"
      "ret":: "r"(retval)); /* Return to caller. */
#endif
  // Should never reach this line.
}

#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
/* This could either be a normal dmtcp wrapper, or a hook function which calls
   a normal dmtcp wrapper. In this case, this is just a hook function which
   calls the real mmap wrapper (in mallocwrappers.cpp). I did it this way so
   that the real mmap wrapper could be relatively unchanged. Also, this way the
   default is to go through the regular mmap wrapper, and only if a call to
   mmap misses the wrapper does it go through the trampoline maze. */
static void *mmap_wrapper(void *addr, size_t length, int prot,
    int flags, int fd, off_t offset)
{
  void *retval;
  if (IN_MMAP_WRAPPER || MMAP_NO_SYNC) {
    retval = _real_mmap(addr,length,prot,flags,fd,offset);
  } else {
    retval = mmap(addr,length,prot,flags,fd,offset);
  }
  return retval;
}

/* Trampoline to be installed in libc's mmap(). 

 It would be best to leave this function alone whenever possible. If anything
 is added, it is likely the stack frame size will change from what is
 hard-coded in here (0x34 on 32-bit).

 If you do make modifications which change the stack frame, disassemble this
 function and see what the compiler has said for the stack frame adjustment
 (e.g. "sub 0x34,%esp"). Then adjust the "add" instruction at the end to be the
 same size. */
static void mmap_trampoline(void *addr, size_t length, int prot,
    int flags, int fd, off_t offset)
{
  /* Interesting note: we get the arguments set up for free, since mmap is
     patched to jump directly to this function. */
  /* Save registers we will clobber (why doesn't the compiler do this?) */
#ifdef __x86_64__
  asm("pushq %rcx\n"
      "pushq %rdx");
#else
  asm("push %ecx\n"
      "push %edx");
#endif
  /* Unpatch mmap. */
  UNINSTALL_TRAMPOLINE(mmap);
  /* Call mmap mini trampoline, which will eventually call _real_mmap. */
  void *retval = mmap_wrapper(addr,length,prot,flags,fd,offset);
  /* Repatch mmap. */
  INSTALL_TRAMPOLINE(mmap);
#ifdef __x86_64__
  asm("mov %0,%%rax\n"    /* Set return value */
      "pop %%rdx\n"       /* Restore clobbered registers. */
      "pop %%rcx\n"
      "add $0x48,%%rsp\n" /* Reset stack pointer */
      "pop %%rbx\n"
      "pop %%rbp\n"
      "ret":: "r"(retval)); /* Return to caller. */
#else
  asm("mov %0,%%eax\n"    /* Set return value */
      "pop %%edx\n"       /* Restore clobbered registers. */
      "pop %%ecx\n"
      "add $0x34,%%esp\n" /* Reset stack pointer */
      "pop %%ebx\n"
      "pop %%ebp\n"
      "ret":: "r"(retval)); /* Return to caller. */
#endif
  // Should never reach this line.
}
#endif //SYNCHRONIZATION_LOG_AND_REPLAY

/* Any trampolines which should be installed are done so via this function.
   Called from DmtcpWorker constructor. */
void _dmtcp_setup_trampolines()
{
  SETUP_TRAMPOLINE(sbrk);
#ifdef SYNCHRONIZATION_LOG_AND_REPLAY
  SETUP_TRAMPOLINE(mmap);
#endif
}
