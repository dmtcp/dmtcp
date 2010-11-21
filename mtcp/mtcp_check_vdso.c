/*****************************************************************************
 *   Copyright (C) 2006-2010 by Michael Rieker, Jason Ansel, Kapil Arya, and *
 *                                                            Gene Cooperman *
 *   mrieker@nii.net, jansel@csail.mit.edu, kapil@ccs.neu.edu, and           *
 *                                                          gene@ccs.neu.edu *
 *                                                                           *
 *   This file is part of the MTCP module of DMTCP (DMTCP:mtcp).             *
 *                                                                           *
 *  DMTCP:mtcp is free software: you can redistribute it and/or              *
 *  modify it under the terms of the GNU Lesser General Public License as    *
 *  published by the Free Software Foundation, either version 3 of the       *
 *  License, or (at your option) any later version.                          *
 *                                                                           *
 *  DMTCP:dmtcp/src is distributed in the hope that it will be useful,       *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *  GNU Lesser General Public License for more details.                      *
 *                                                                           *
 *  You should have received a copy of the GNU Lesser General Public         *
 *  License along with DMTCP:dmtcp/src.  If not, see                         *
 *  <http://www.gnu.org/licenses/>.                                          *
 *****************************************************************************/

/* To test:  gcc -DSTANDALONE THIS_FILE; ./a.out */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/utsname.h> /* uname */
#include <sys/time.h>
#include <sys/resource.h> /* getrlimit, setrlimit */
#include <sys/personality.h>
#define ADDR_NO_RANDOMIZE  0x0040000  /* In case of old Linux, not defined */
#define ADDR_COMPAT_LAYOUT 0x0200000  /* Not yet defined as of Ubuntu 8.04 */
#include <unistd.h>
#include <errno.h>
#include <elf.h> // For value of AT_SYSINFO, Elf??_auxv_t
#include "mtcp_sys.h" // For CLEAN_FOR_64BIT
#include "mtcp_internal.h" // For CLEAN_FOR_64BIT and MAXPATHLEN

// We turn off va_addr_rand(/proc/sys/kernel/randomize_va_space).  
// For a _given_ binary,
// this fixes the address of the vdso.  Luckily, on restart, we
// get our vdso from mtcp_restart.  So, we need to maintain two
// vdso segments:  one from the user binary and one from each
// invocation of mtcp_restart during iterated restarts.
# define NO_RAND_VA_PERSONALITY 1

//======================================================================
// Get and set AT_SYSINFO for purposes of patching address in vdso

#ifdef __x86_64__
# define ELF_AUXV_T Elf64_auxv_t
# define UINT_T uint64_t
#else
# define ELF_AUXV_T Elf32_auxv_t
# define UINT_T uint32_t
#endif

// Returns value for AT_SYSINFO in kernel's auxv
// Ideally:  mtcp_at_sysinfo() == *mtcp_addr_sysinfo()
// Best if we call this early, before the user makes problems
// by moving environment variables, putting in a weird stack, etc.
extern char **environ;
static void * get_at_sysinfo() {
  void **stack;
  int i;
  ELF_AUXV_T *auxv;
  static char **my_environ = NULL;

  if (my_environ == NULL)
    my_environ = environ;
#if 0
  // Walk the stack.
  asm volatile (CLEAN_FOR_64_BIT(mov %%ebp, %0\n\t)
                : "=g" (stack) );
  mtcp_printf("stack 2: %p\n", stack);

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
    mtcp_printf("Error:  overshot stack\n");
    exit(1);
  }
  stack = &stack[i];
#else
  stack = (void **)&my_environ[-1];
  if (*stack != NULL) {
    mtcp_printf("This should be argv[argc] == NULL and it's not.\n"
	"NO &argv[argc], stack: %p\n", stack);
    exit(1);
  }
#endif
  // stack[-1] should be argv[argc-1]
  if ( (void **)stack[-1] < stack || (void **)stack[-1] > stack + 100000 ) {
    mtcp_printf("candidate argv[argc-1] failed consistency check\n");
    exit(1);
  }
  for (i = 1; stack[i] != NULL; i++)
    if ( (void **)stack[i] < stack || (void **)stack[i] > stack + 10000 ) {
      mtcp_printf("candidate argv[%d] failed consistency check\n", i);
      exit(1);
    }
  stack = &stack[i+1];
  // Now stack is beginning of auxiliary vector (auxv)
  // auxv->a_type = AT_NULL marks the end of auxv
  for (auxv = (ELF_AUXV_T *)stack; auxv->a_type != AT_NULL; auxv++) {
    // mtcp_printf("0x%x 0x%x\n", auxv->a_type, auxv->a_un.a_val);
    if ( auxv->a_type == (UINT_T)AT_SYSINFO ) {
      mtcp_printf("AT_SYSINFO      (at 0x%p) is:  0x%lx\n",
        &auxv->a_un.a_val, auxv->a_un.a_val);
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

int mtcp_have_thread_sysinfo_offset() {
#ifdef RESET_THREAD_SYSINFO
  static int result = -1; // Reset to 0 or 1 on first call.
#else
  static int result = 0;
#endif
  if (result == -1) {
    void * sysinfo;
    asm (CLEAN_FOR_64_BIT(mov %%gs:) DEFAULT_SYSINFO_OFFSET ", %0\n\t"
	 : "=r" (sysinfo));
    result = (sysinfo == get_at_sysinfo());
  }
  return result;
}

// AT_SYSINFO is what kernel calls sysenter address in vdso segment.
// Kernel saves it for each thread in %gs:SYSINFO_OFFSEt ??
//  as part of kernel TCB (thread control block) at beginning of TLS ??
void *mtcp_get_thread_sysinfo() {
  void *sysinfo;
  asm volatile (CLEAN_FOR_64_BIT(mov %%gs:) DEFAULT_SYSINFO_OFFSET ", %0\n\t"
                : "=r" (sysinfo) );
  return sysinfo;
}

void mtcp_set_thread_sysinfo(void *sysinfo) {
  asm volatile (CLEAN_FOR_64_BIT(mov %0, %%gs:) DEFAULT_SYSINFO_OFFSET "\n\t"
                : : "r" (sysinfo) );
}

//======================================================================
// Used to check if vdso is an issue

#define MAX_ARGS 500
static int write_args(char **vector, char *filename) {
  int num_read = 0, i;
  int retval, fd;
  char strings[10001];
  char *str = strings;

  if (-1 == (fd = open(filename, O_RDONLY))) {
    perror("open");
    exit(1);
  }
  while ( 0 != (retval = read(fd, strings + num_read, 10000)) ) {
    if (retval > 0)
      num_read += retval;
    if (retval == 0)
      break; /* end-of-file; done reading */
    if (retval == -1 && errno != EAGAIN && errno != EINTR)
        break; /* unrecoverable error: exit */
    /* else recoverable error */
  }
  close(fd);
  if (retval == -1)
    return -1;
  
  for (i = 0; str - strings < num_read && i < MAX_ARGS; i++) {
    vector[i] = str;
    while (*str++ != '\0')
      ;
  }
  vector[i] = NULL;
  return 0;
}

static unsigned long getenv_oldpers() {
    unsigned long oldpers = 0;
    char *oldpers_str = getenv("MTCP_OLDPERS");
    if (oldpers_str == NULL) {
      mtcp_printf("MTCP: internal error: %s:%d\n", __FILE__, __LINE__);
      exit(1);
    }
    while (*oldpers_str != '\0')
      oldpers = (oldpers << 1) + (*oldpers_str++ == '1' ? 1 : 0);
    return oldpers;
}

static int setenv_oldpers(int oldpers) {
    static char oldpers_str[sizeof(oldpers)*8+1];
    int i = sizeof(oldpers_str); 
    oldpers_str[i--] = '\0';
    while (i >= 0) {
      oldpers_str[i--] = ((oldpers & 1) ? '1' : '0');
      oldpers = oldpers >> 1;
    }
    return setenv("MTCP_OLDPERS", oldpers_str, 1);
}

/* Turn off randomize_va (by re-exec'ing) or warn user if vdso_enabled is on. */
void mtcp_check_vdso_enabled() {
  char buf[1];
  struct utsname utsname;
#ifdef RESET_THREAD_SYSINFO
  get_at_sysinfo(); /* Initialize pointer to environ for later calls */
#endif

#ifdef NO_RAND_VA_PERSONALITY
  /* Set ADDR_NO_RANDOMIZE bit;
   * In Ubuntu Linux 2.6.24 kernel, This places vdso in  a different
   * fixed position in mtcp_init (since /lib/ld-2.7.so is inserted
   * above [vdso] and below [stack].  mtcp_restart has no /lib/ld-2.7.so.
   */
  int pers = personality(0xffffffffUL); /* get current personality */
  if (pers & ADDR_NO_RANDOMIZE) { /* if no addr space randomization ... */
    if (getenv("MTCP_OLDPERS") != NULL) {
      personality(getenv_oldpers()); /* restore orig pre-exec personality */
      if (-1 == unsetenv("MTCP_OLDPERS"))
        perror("unsetenv");
    }
    return; /* skip the rest */
  }

  if (! (pers & ADDR_NO_RANDOMIZE)) /* if addr space randomization ... */
  { 
    unsigned long oldpers = pers;
    /* then turn off randomization and (just in case) remove ADDR_COMPAT_LAYOUT*/
    personality((pers | ADDR_NO_RANDOMIZE) & ~ADDR_COMPAT_LAYOUT);
    if ( ADDR_NO_RANDOMIZE & personality(0xffffffffUL) ) /* if it's off now */
    { char runtime[MAXPATHLEN+1];
      int i = readlink("/proc/self/exe", runtime, MAXPATHLEN);
      if ( i != -1)
      { char *argv[MAX_ARGS+1];
        extern char **environ;
	struct rlimit rlim;

	/* "make" has the capability to raise RLIMIT_STACK to infinity.
	 * This is a problem.  When the kernel (2.6.24 or later) detects this,
	 * it falls back to an older "standard" memory layout for libs.
	 * 
	 * "standard" memory layout puts [vdso] segment in low memory, which 
	 *  MTCP currently doesn't handle properly.
	 *
	 * glibc:nptl/sysdeps/<ARCH>/pthreaddef.h defines the default stack for 
	 *  pthread_create to be ARCH_STACK_DEFAULT_SIZE if rlimit is set to be
	 *  unlimited. We follow the same default.
	 */
//#ifdef __x86_64__
//# define ARCH_STACK_DEFAULT_SIZE (32 * 1024 * 1024)
//#else
//# define ARCH_STACK_DEFAULT_SIZE (2 * 1024 * 1024)
//#endif 
        /*
         * XXX: TODO: Due to some reason, manual restart of checkpointed
         *  processes fails if  ARCH_STACK_DEFAULT_SIZE is less than 256MB. It
         *  has to do with VDSO. The location of VDSO section conflicts with the
         *  location of process libraries and hence it is unmapped which causes
         *  failure during thre restarting phase. If we set the stack limit to
         *  256 MB or higher, we donot see this bug. 
         * It Should also be noted that the process will call setrlimit to set
         *  the resource limites to their pre-checkpoint values.
         */
#define ARCH_STACK_DEFAULT_SIZE (256 * 1024 * 1024)
	 
	if ( -1 == getrlimit(RLIMIT_STACK, &rlim) ||
             ( rlim.rlim_cur = rlim.rlim_max = ARCH_STACK_DEFAULT_SIZE,
	       setrlimit(RLIMIT_STACK, &rlim),
	       getrlimit(RLIMIT_STACK, &rlim),
	       rlim.rlim_max == RLIM_INFINITY )
	   ) {
          mtcp_printf("Failed to reduce RLIMIT_STACK"
			  " below RLIM_INFINITY\n");
	  exit(1);
	}
	write_args(argv, "/proc/self/cmdline");
        runtime[i] = '\0';
	setenv_oldpers(oldpers);
        execve(runtime, argv, environ);
      }
      if (-1 == personality(oldpers)); /* reset if we couldn't exec */
	perror("personality");
    }
  }
#endif

  /* We failed to turn off address space rand., but maybe vdso is not enabled 
   * On newer kernels, there is no /proc/sys/vm/vdso_enabled, we will cross our
   *  fingers and continue anyways.
   */
  FILE * stream = fopen("/proc/sys/vm/vdso_enabled", "r");
  if (stream == NULL)
    return;  /* In older kernels, if it doesn't exist, it can't be enabled. */
  clearerr(stream);
  if (fread(buf, sizeof(buf[0]), 1, stream) < 1) {
    if (ferror(stream)) {
      perror("fread");
      exit(1);
    }
  }
  if (-1 == fclose(stream)) {
    perror("fclose");
    exit(1);
  }
  /* This call also caches AT_SYSINFO for use by mtcp_set_thread_sysinfo() */
  if (mtcp_have_thread_sysinfo_offset())
    return;
  if (buf[0] == '1') {
    mtcp_printf("\n\n\nPROBLEM:  cat /proc/sys/vm/vdso_enabled returns 1\n"
    "  Further, I failed to find SYSINFO_OFFSET in TLS.\n"
    "  Can't work around this problem.\n"
    "  Please run this program again after doing as root:\n"
    "                                    echo 0 > /proc/sys/vm/vdso_enabled\n"
    "  Alternatively, upgrade kernel to one that allows for a personality\n"
    "  with ADDR_NO_RANDOMIZE in /usr/include/linux/personality.h.\n");
    exit(1);
  }
}

#ifdef STANDALONE
int main() {
  mtcp_check_vdso_enabled();
  system("echo ulimit -s | sh");
  return 0;
}
#endif
