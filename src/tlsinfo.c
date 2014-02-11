#include <elf.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <sys/personality.h>
#include <linux/version.h>
#include <gnu/libc-version.h>
#include "threadinfo.h"
#include "mtcp_sys_for_dmtcp.h"

int mtcp_sys_errno;

#ifdef __x86_64__
# define ELF_AUXV_T Elf64_auxv_t
# define UINT_T uint64_t
#else
  // else __i386__ and __arm__
# define ELF_AUXV_T Elf32_auxv_t
# define UINT_T uint32_t
#endif

/* These functions are not defined for x86_64. */
#ifdef __i386__
# define tlsinfo_get_thread_area(args...) \
    syscall(SYS_get_thread_area, args)
# define tlsinfo_set_thread_area(args...) \
    syscall(SYS_set_thread_area, args)
#endif

#ifdef __x86_64__
# include <asm/prctl.h>
# include <sys/prctl.h>
/* man arch_prctl has both signatures, and prctl.h above has no declaration.
 *  int arch_prctl(int code, unsigned long addr);
 *  int arch_prctl(int code, unsigned long addr);
 */
int arch_prctl();
#if 0
// I don't see why you would want a direct kernel call inside DMTCP.
// Removing this will remove the dependency on mtcp_sys.h.  - Gene
static unsigned long int myinfo_gs;
/* ARE THE _GS OPERATIONS NECESSARY? */
#  define tlsinfo_get_thread_area(uinfo) \
    ( mtcp_inline_syscall(arch_prctl,2,ARCH_GET_FS, \
         (unsigned long int)(&(((struct user_desc *)uinfo)->base_addr))), \
      mtcp_inline_syscall(arch_prctl,2,ARCH_GET_GS, &myinfo_gs) \
    )
#  define tlsinfo_set_thread_area(uinfo) \
    ( mtcp_inline_syscall(arch_prctl,2,ARCH_SET_FS, \
	*(unsigned long int *)&(((struct user_desc *)uinfo)->base_addr)), \
      mtcp_inline_syscall(arch_prctl,2,ARCH_SET_GS, myinfo_gs) \
    )
# else
static unsigned long int myinfo_gs;
/* ARE THE _GS OPERATIONS NECESSARY? */
#  define tlsinfo_get_thread_area(uinfo) \
     ( arch_prctl(ARCH_GET_FS, \
         (unsigned long int)(&(((struct user_desc *)uinfo)->base_addr))), \
       arch_prctl(ARCH_GET_GS, &myinfo_gs) \
     )
#  define tlsinfo_set_thread_area(uinfo) \
    ( arch_prctl(ARCH_SET_FS, \
	*(unsigned long int *)&(((struct user_desc *)uinfo)->base_addr)), \
      arch_prctl(ARCH_SET_GS, myinfo_gs) \
    )
# endif
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

#  define tlsinfo_get_thread_area(uinfo) \
  ({ asm volatile ("mrc     p15, 0, %0, c13, c0, 3  @ load_tp_hard\n\t" \
                   : "=r" (myinfo_gs) ); \
    myinfo_gs = myinfo_gs - 1216; /* sizeof(struct pthread) = 1216 */ \
    *(unsigned long int *)&(((struct user_desc *)uinfo)->base_addr) \
      = myinfo_gs; \
    myinfo_gs; })
#  define tlsinfo_set_thread_area(uinfo) \
    ( myinfo_gs = \
        *(unsigned long int *)&(((struct user_desc *)uinfo)->base_addr), \
      (mtcp_sys_kernel_set_tls(myinfo_gs+1216), 0) \
      /* 0 return value at end means success */ )
#endif /* end __arm__ */

/* TLS segment registers used differently in i386 and x86_64. - Gene */
#ifdef __i386__
# define TLSSEGREG gs
#elif __x86_64__
# define TLSSEGREG fs
#elif __arm__
/* FIXME: fs IS NOT AN arm REGISTER.  BUT THIS IS USED ONLY AS A FIELD NAME.
 *   ARM uses a register in coprocessor 15 as the thread-pointer (TLS Register)
 */
# define TLSSEGREG fs
#endif

/* Offset computed (&x.pid - &x) for
 *   struct pthread x;
 * as found in:  glibc-2.5/nptl/descr.h
 * It was 0x4c and 0x48 for pid and tid for i386.
 * Roughly, the definition is:
 *glibc-2.5/nptl/descr.h:
 * struct pthread
 * {
 *  union {
 *   tcbheader_t tcbheader;
 *   void *__padding[16];
 *  };
 *  list_t list;
 *  pid_t tid;
 *  pid_t pid;
 *  ...
 * } __attribute ((aligned (TCB_ALIGNMENT)));
 *
 *glibc-2.5/nptl/sysdeps/pthread/list.h:
 * typedef struct list_head
 * {
 *  struct list_head *next;
 *  struct list_head *prev;
 * } list_t;
 *
 * NOTE: glibc-2.10 changes the size of __padding from 16 to 24.  --KAPIL
 *
 * NOTE: glibc-2.11 further changes the size tcphead_t without updating the
 *       size of __padding in struct pthread. We need to add an extra 512 bytes
 *       to accommodate this.                                    -- KAPIL
 */

#if !__GLIBC_PREREQ (2,1)
# error "glibc version too old"
#endif

// NOTE: tls_tid_offset, tls_pid_offset determine offset independently of
//     glibc version.  These STATIC_... versions serve as a double check.
// Calculate offsets of pid/tid in pthread 'struct user_desc'
// The offsets are needed for two reasons:
//  1. glibc pthread functions cache the pid; must update this after restart
//  2. glibc pthread functions cache the tid; pthread functions pass address
//     of cached tid to clone, and MTCP grabs it; But MTCP is still missing
//     the address where pthread cached the tid of motherofall.  So, it can't
//     update.
static int STATIC_TLS_TID_OFFSET()
{
  static int offset = -1;
  if (offset != -1)
    return offset;

  char *ptr;
  long major = strtol(gnu_get_libc_version(), &ptr, 10);
  long minor = strtol(ptr+1, NULL, 10);
  ASSERT (major == 2);

  if (minor >= 11) {
#ifdef __x86_64__
    offset = 26*sizeof(void *) + 512;
#else
    offset = 26*sizeof(void *);
#endif
  } else if (minor == 10) {
    offset = 26*sizeof(void *);
  } else {
    offset = 18*sizeof(void *);
  }

  return offset;
}

#if 0
# if __GLIBC_PREREQ (2,11)
#  ifdef __x86_64__
#   define STATIC_TLS_TID_OFFSET() (26*sizeof(void *) + 512)
#  else
#   define STATIC_TLS_TID_OFFSET() (26*sizeof(void *))
#  endif

# elif __GLIBC_PREREQ (2,10)
#   define STATIC_TLS_TID_OFFSET() (26*sizeof(void *))

# else
#   define STATIC_TLS_TID_OFFSET() (18*sizeof(void *))
# endif
#endif

# define STATIC_TLS_PID_OFFSET() (STATIC_TLS_TID_OFFSET() + sizeof(pid_t))

/* WHEN WE HAVE CONFIDENCE IN THIS VERSION, REMOVE ALL OTHER __GLIBC_PREREQ
 * AND MAKE THIS THE ONLY VERSION.  IT SHOULD BE BACKWARDS COMPATIBLE.
 */
/* These function definitions should succeed independently of the glibc version.
 * They use get_thread_area() to match (tid, pid) and find offset.
 * In other code, on restart, that offset is used to set (tid,pid) to
 *   the latest tid and pid of the new thread, instead of the (tid,pid)
 *   of the original thread.
 * SEE: "struct pthread" in glibc-2.XX/nptl/descr.h for 'struct pthread'.
 */
static int tls_tid_offset(void);

/* Can remove the unused attribute when this __GLIBC_PREREQ is the only one. */
static char *memsubarray (char *array, char *subarray, size_t len)
					 __attribute__ ((unused));
static int get_tls_segreg(void);
static void *get_tls_base_addr(void);

static int tls_tid_offset(void)
{
  static int tid_offset = -1;
  if (tid_offset == -1) {
    struct {pid_t tid; pid_t pid;} tid_pid;
    /* struct pthread has adjacent fields, tid and pid, in that order.
     * Try to find at what offset that bit patttern occurs in struct pthread.
     */
    char * tmp;
    tid_pid.tid = THREAD_REAL_PID();
    tid_pid.pid = THREAD_REAL_PID();
    /* Get entry number of current thread descriptor from its segment register:
     * Segment register / 8 is the entry_number for the "thread area", which
     * is of type 'struct user_desc'.   The base_addr field of that struct
     * points to the struct pthread for the thread with that entry_number.
     * The tid and pid are contained in the 'struct pthread'.
     *   So, to access the tid/pid fields, first find the entry number.
     * Then fill in the entry_number field of an empty 'struct user_desc', and
     * get_thread_area(struct user_desc *uinfo) will fill in the rest.
     * Then use the filled in base_address field to get the 'struct pthread'.
     * The function tcp_get_tls_base_addr() returns this 'struct pthread' addr.
     */
    void * pthread_desc = get_tls_base_addr();
    /* A false hit for tid_offset probably can't happen since a new
     * 'struct pthread' is zeroed out before adding tid and pid.
     * pthread_desc below is defined as 'struct pthread' in glibc:nptl/descr.h
     */
    tmp = memsubarray((char *)pthread_desc, (char *)&tid_pid, sizeof(tid_pid));
    if (tmp == NULL) {
      PRINTF("WARNING: Couldn't find offsets of tid/pid in thread_area.\n"
             "  Now relying on the value determined using the\n"
             "  glibc version with which DMTCP was compiled.");
      return STATIC_TLS_TID_OFFSET();
      //mtcp_abort();
    }

    tid_offset = tmp - (char *)pthread_desc;
    if (tid_offset != STATIC_TLS_TID_OFFSET()) {
      PRINTF("WARNING: tid_offset (%d) different from expected.\n"
             "  It is possible that DMTCP was compiled with a different\n"
             "  glibc version than the one it's dynamically linking to.\n"
             "  Continuing anyway.  If this fails, please try again.",
             tid_offset);
    }
    DPRINTF("tid_offset: %d\n", tid_offset);
    if (tid_offset % sizeof(int) != 0) {
      PRINTF("WARNING: tid_offset is not divisible by sizeof(int).\n"
             "  Now relying on the value determined using the\n"
             "  glibc version with which DMTCP was compiled.");
      return STATIC_TLS_TID_OFFSET();
      //mtcp_abort();
    }
    /* Should we do a double-check, and spawn a new thread and see
     *  if its TID matches at this tid_offset?  This would give greater
     *  confidence, but for the reasons above, it's probably not necessary.
     */
  }
  return tid_offset;
}

static int tls_pid_offset(void)
{
  static int pid_offset = -1;
  struct {pid_t tid; pid_t pid;} tid_pid;
  if (pid_offset == -1) {
    int tid_offset = tls_tid_offset();
    pid_offset = tid_offset + (char *)&(tid_pid.pid) - (char *)&tid_pid;
    DPRINTF("pid_offset: %d\n", pid_offset);
  }
  return pid_offset;
}

static char *memsubarray (char *array, char *subarray, size_t len)
{
   char *i_ptr;
   size_t j;
   int word1 = *(int *)subarray;
   // Assume subarray length is at least sizeof(int) and < 2048.
   ASSERT (len >= sizeof(int));
   for (i_ptr = array; i_ptr < array+2048; i_ptr++) {
     if (*(int *)i_ptr == word1) {
       for (j = 0; j < len; j++)
	 if (i_ptr[j] != subarray[j])
	   break;
	if (j == len)
	  return i_ptr;
     }
   }
   return NULL;
}

static int get_tls_segreg(void)
{
  segreg_t tlssegreg;
#ifdef __i386__
  asm volatile ("movw %%gs,%0" : "=g" (tlssegreg)); /* any general register */
#elif __x86_64__
  /* q = a,b,c,d for i386; 8 low bits of r class reg for x86_64 */
  asm volatile ("movl %%fs,%0" : "=q" (tlssegreg));
#elif __arm__
  asm volatile ("mrc     p15, 0, %0, c13, c0, 3  @ load_tp_hard\n\t"
                : "=r" (tlssegreg));
#endif
  return (int)tlssegreg;
}

static void* get_tls_base_addr()
{
  struct user_desc gdtentrytls;

  gdtentrytls.entry_number = get_tls_segreg() / 8;
  if (tlsinfo_get_thread_area(&gdtentrytls) == -1) {
    PRINTF("Error getting GDT TLS entry: %d\n", errno);
    _exit(0);
  }
  return (void *)(*(unsigned long *)&(gdtentrytls.base_addr));
}

// Returns value for AT_SYSINFO in kernel's auxv
// Ideally:  mtcp_at_sysinfo() == *mtcp_addr_sysinfo()
// Best if we call this early, before the user makes problems
// by moving environment variables, putting in a weird stack, etc.
extern char **environ;
static void * get_at_sysinfo()
{
  void **stack;
  int i;
  ELF_AUXV_T *auxv;
  static char **my_environ = NULL;

  if (my_environ == NULL)
    my_environ = environ;
#if 0
  // Walk the stack.
#if defined(__i386__) || defined(__x86_64__)
  asm volatile (CLEAN_FOR_64_BIT(mov %%ebp, %0\n\t)
                : "=g" (stack) );
#else
# error "current architecture not supported"
#endif
  MTCP_PRINTF("stack 2: %p\n", stack);

  // When popping stack/%ebp yields zero, that's the ELF loader telling us that
  // this is "_start", the first call frame, which was created by ELF.
  for ( ; *stack != NULL; stack = *stack )
    ;

  // Go beyond first call frame:
  // Next look for &(argv[argc]) on stack;  (argv[argc] == NULL)
  for (i = 1; stack[i] != NULL; i++)
    ;
  // Do some error checks
  if ( &(stack[i]) - stack > 100000 ) {
    MTCP_PRINTF("Error:  overshot stack\n");
    exit(1);
  }
  stack = &stack[i];
#else
  stack = (void **)&my_environ[-1];

  if (*stack != NULL) {
    PRINTF("Error: This should be argv[argc] == NULL and it's not. NO &argv[argc]");
    _exit(0);
  }
#endif
  // stack[-1] should be argv[argc-1]
  if ( (void **)stack[-1] < stack || (void **)stack[-1] > stack + 100000 ) {
    PRINTF("Error: candidate argv[argc-1] failed consistency check");
    _exit(0);
  }
  for (i = 1; stack[i] != NULL; i++)
    if ( (void **)stack[i] < stack || (void **)stack[i] > stack + 10000 ) {
      PRINTF("Error: candidate argv[i] failed consistency check");
      _exit(0);
    }
  stack = &stack[i+1];
  // Now stack is beginning of auxiliary vector (auxv)
  // auxv->a_type = AT_NULL marks the end of auxv
  for (auxv = (ELF_AUXV_T *)stack; auxv->a_type != AT_NULL; auxv++) {
    // mtcp_printf("0x%x 0x%x\n", auxv->a_type, auxv->a_un.a_val);
    if ( auxv->a_type == (UINT_T)AT_SYSINFO ) {
      //JNOTE("AT_SYSINFO") (&auxv->a_un.a_val) (auxv->a_un.a_val);
      return (void *)auxv->a_un.a_val;
    }
  }
  return NULL;  /* Couldn't find AT_SYSINFO */
}

// From glibc-2.7: glibc-2.7/nptl/sysdeps/i386/tls.h
// SYSINFO_OFFSET given by:
//  #include "glibc-2.7/nptl/sysdeps/i386/tls.h"
//  tcbhead_t dummy;
//  #define SYSINFO_OFFSET &(dummy.sysinfo) - &dummy

// Some reports say it was 0x18 in past.  Should we also check that?
#define DEFAULT_SYSINFO_OFFSET "0x10"

int TLSInfo_HaveThreadSysinfoOffset()
{
#ifdef RESET_THREAD_SYSINFO
  static int result = -1; // Reset to 0 or 1 on first call.
#else
  static int result = 0;
#endif
  if (result == -1) {
    void * sysinfo;
#if defined(__i386__) || defined(__x86_64__)
  asm volatile (CLEAN_FOR_64_BIT(mov %%gs:) DEFAULT_SYSINFO_OFFSET ", %0\n\t"
                : "=r" (sysinfo) );
#elif defined(__arm__)
  asm volatile ("mrc     p15, 0, %0, c13, c0, 3  @ load_tp_hard\n\t"
                : "=r" (sysinfo) );
#else
# error "current architecture not supported"
#endif
    result = (sysinfo == get_at_sysinfo());
  }
  return result;
}

// AT_SYSINFO is what kernel calls sysenter address in vdso segment.
// Kernel saves it for each thread in %gs:SYSINFO_OFFSET ??
//  as part of kernel TCB (thread control block) at beginning of TLS ??
void *TLSInfo_GetThreadSysinfo()
{
  void *sysinfo;
#if defined(__i386__) || defined(__x86_64__)
  asm volatile (CLEAN_FOR_64_BIT(mov %%gs:) DEFAULT_SYSINFO_OFFSET ", %0\n\t"
                : "=r" (sysinfo) );
#elif defined(__arm__)
  asm volatile ("mrc     p15, 0, %0, c13, c0, 3  @ load_tp_hard\n\t"
                : "=r" (sysinfo) );
#else
# error "current architecture not supported"
#endif
  return sysinfo;
}

void TLSInfo_SetThreadSysinfo(void *sysinfo) {
#if defined(__i386__) || defined(__x86_64__)
  asm volatile (CLEAN_FOR_64_BIT(mov %0, %%gs:) DEFAULT_SYSINFO_OFFSET "\n\t"
                : : "r" (sysinfo) );
#elif defined(__arm__)
  mtcp_sys_kernel_set_tls(sysinfo);
#else
# error "current architecture not supported"
#endif
}

/*****************************************************************************
 *
 *
 *****************************************************************************/
void TLSInfo_VerifyPidTid(pid_t pid, pid_t tid)
{
  pid_t tls_pid, tls_tid;
  char *addr = (char*)get_tls_base_addr();
  tls_pid = *(pid_t *) (addr + tls_pid_offset());
  tls_tid = *(pid_t *) (addr + tls_tid_offset());

  if ((tls_pid != pid) || (tls_tid != tid)) {
    PRINTF("ERROR: getpid(%d), tls pid(%d), and tls tid(%d) must all match\n",
           THREAD_REAL_PID(), tls_pid, tls_tid);
    _exit(0);
  }
}

void TLSInfo_UpdatePid()
{
  pid_t  *tls_pid = (pid_t *) ((char*)get_tls_base_addr() + tls_pid_offset());
  *tls_pid = THREAD_REAL_PID();
}

/*****************************************************************************
 *
 *  Save state necessary for TLS restore
 *  Linux saves stuff in the GDT, switching it on a per-thread basis
 *
 *****************************************************************************/
void TLSInfo_SaveTLSState (Thread *thread)
{
  int i;

#ifdef __i386__
  asm volatile ("movw %%fs,%0" : "=m" (thread->fs));
  asm volatile ("movw %%gs,%0" : "=m" (thread->gs));
#elif __x86_64__
  //asm volatile ("movl %%fs,%0" : "=m" (thread->fs));
  //asm volatile ("movl %%gs,%0" : "=m" (thread->gs));
#elif __arm__
  // Follow x86_64 for arm.
#endif

  memset (thread->gdtentrytls, 0, sizeof thread->gdtentrytls);

/* FIXME:  IF %fs IS NOT READ into thread->fs AT BEGINNING OF THIS
 *   FUNCTION, HOW CAN WE REFER TO IT AS  thread->TLSSEGREG?
 *   WHAT IS THIS CODE DOING?
 */
  i = thread->TLSSEGREG / 8;
  thread->gdtentrytls[0].entry_number = i;
  if (tlsinfo_get_thread_area (&(thread->gdtentrytls[0])) == -1) {
    PRINTF("Error saving GDT TLS entry: %d\n", errno);
    _exit(0);
  }
}

/*****************************************************************************
 *
 *  Restore the GDT entries that are part of a thread's state
 *
 *  The kernel provides set_thread_area system call for a thread to alter a
 *  particular range of GDT entries, and it switches those entries on a
 *  per-thread basis.  So from our perspective, this is per-thread state that is
 *  saved outside user addressable memory that must be manually saved.
 *
 *****************************************************************************/
void TLSInfo_RestoreTLSState(Thread *thread)
{
  /* The assumption that this points to the pid was checked by that tls_pid crap
   * near the beginning
   */
  *(pid_t *)(*(unsigned long *)&(thread->gdtentrytls[0].base_addr)
             + tls_pid_offset()) = THREAD_REAL_PID();

  /* Likewise, we must jam the new pid into the mother thread's tid slot
   * (checked by tls_tid carpola)
   */
  if (thread->tid == THREAD_REAL_PID()) {
    *(pid_t *)(*(unsigned long *)&(thread->gdtentrytls[0].base_addr)
               + tls_tid_offset()) = THREAD_REAL_PID();
  }

  /* Restore all three areas */
  if (tlsinfo_set_thread_area (&(thread->gdtentrytls[0])) != 0) {
    PRINTF("Error restoring GDT TLS entry: %d\n", errno);
    _exit(0);
  }

  /* Restore the rest of the stuff */

#ifdef __i386__
  asm volatile ("movw %0,%%fs" : : "m" (thread->fs));
  asm volatile ("movw %0,%%gs" : : "m" (thread->gs));
#elif __x86_64__
/* Don't directly set fs.  It would only set 32 bits, and we just
 *  set the full 64-bit base of fs, using sys_set_thread_area,
 *  which called arch_prctl.
 *asm volatile ("movl %0,%%fs" : : "m" (thread->fs));
 *asm volatile ("movl %0,%%gs" : : "m" (thread->gs));
 */
#elif __arm__
/* ARM treats this same as x86_64 above. */
#endif
}

