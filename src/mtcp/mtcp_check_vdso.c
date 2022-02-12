/*****************************************************************************
 * Copyright (C) 2006-1014 Gene Cooperman <gene@ccs.neu.edu>                 *
 *                                                                           *
 * DMTCP is free software: you can redistribute it and/or                    *
 * modify it under the terms of the GNU Lesser General Public License as     *
 * published by the Free Software Foundation, either version 3 of the        *
 * License, or (at your option) any later version.                           *
 *                                                                           *
 * DMTCP is distributed in the hope that it will be useful,                  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of            *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
 * GNU Lesser General Public License for more details.                       *
 *                                                                           *
 * You should have received a copy of the GNU Lesser General Public          *
 * License along with DMTCP.  If not, see <http://www.gnu.org/licenses/>.    *
 *****************************************************************************/

/* To test:  gcc -DSTANDALONE THIS_FILE; ./a.out */

/* This file does not make calls to libc.  But that is overkill.  This
 * should be used _before_ we move into restarting without the use of libc.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h> /* getrlimit, setrlimit */
#include <limits.h> /* defines PATH_MAX */
#include <sys/personality.h>
#define ADDR_NO_RANDOMIZE  0x0040000  /* In case of old Linux, not defined */
#define ADDR_COMPAT_LAYOUT 0x0200000  /* Not yet defined as of Ubuntu 8.04 */
#include <unistd.h>
#include <errno.h>
#include <elf.h> // For value of AT_SYSINFO, Elf??_auxv_t
#include "mtcp_sys.h" // For CLEAN_FOR_64BIT; for mtcp_sys_kernel_set_tls (ARM)
#include "mtcp_util.h"

#define MAX_NEW_ENVP_SIZE 1024

#ifdef __x86_64__
# define ELF_AUXV_T Elf64_auxv_t
# define UINT_T uint64_t
#else
# define ELF_AUXV_T Elf32_auxv_t
# define UINT_T uint32_t
#endif

static int setPersonalityEnv(const char* name, const char* val, char **environ);
static int unsetPersonalityEnv(const char* name, char **environ);
// Returns value for AT_SYSINFO in kernel's auxv
// Ideally:  mtcp_at_sysinfo() == *mtcp_addr_sysinfo()
// Best if we call this early, before the user makes problems
// by moving environment variables, putting in a weird stack, etc.
static void * get_at_sysinfo(char **environ) {
  int mtcp_sys_errno;
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
    MTCP_PRINTF("This should be argv[argc] == NULL and it's not.\n"
        "NO &argv[argc], stack: %p\n", stack);
    mtcp_sys_exit(1);
  }
#endif
  // stack[-1] should be argv[argc-1]
  if ( (void **)stack[-1] < stack || (void **)stack[-1] > stack + 100000 ) {
    MTCP_PRINTF("candidate argv[argc-1] failed consistency check\n");
    mtcp_sys_exit(1);
  }
  for (i = 1; stack[i] != NULL; i++)
    if ( (void **)stack[i] < stack || (void **)stack[i] > stack + 10000 ) {
      MTCP_PRINTF("candidate argv[%d] failed consistency check\n", i);
      mtcp_sys_exit(1);
    }
  stack = &stack[i+1];
  // Now stack is beginning of auxiliary vector (auxv)
  // auxv->a_type = AT_NULL marks the end of auxv
  for (auxv = (ELF_AUXV_T *)stack; auxv->a_type != AT_NULL; auxv++) {
    // mtcp_printf("0x%x 0x%x\n", auxv->a_type, auxv->a_un.a_val);
    if ( auxv->a_type == (UINT_T)AT_SYSINFO ) {
      MTCP_PRINTF("AT_SYSINFO      (at 0x%p) is:  0x%lx\n",
        &auxv->a_un.a_val, auxv->a_un.a_val);
      return (void *)(uintptr_t)auxv->a_un.a_val;
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

int mtcp_have_thread_sysinfo_offset(char **environ) {
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
#elif defined(__aarch64__)
  asm volatile ("mrs     %0, tpidr_el0" : "=r" (sysinfo) );
#else
# error "current architecture not supported"
#endif
    result = (sysinfo == get_at_sysinfo(environ));
  }
  return result;
}

// AT_SYSINFO is what kernel calls sysenter address in vdso segment.
// Kernel saves it for each thread in %gs:SYSINFO_OFFSET ??
//  as part of kernel TCB (thread control block) at beginning of TLS ??
void *mtcp_get_thread_sysinfo() {
  void *sysinfo;
#if defined(__i386__) || defined(__x86_64__)
  asm volatile (CLEAN_FOR_64_BIT(mov %%gs:) DEFAULT_SYSINFO_OFFSET ", %0\n\t"
                : "=r" (sysinfo) );
#elif defined(__arm__)
  asm volatile ("mrc     p15, 0, %0, c13, c0, 3  @ load_tp_hard\n\t"
                : "=r" (sysinfo) );
#elif defined(__aarch64__)
  asm volatile ("mrs     %0, tpidr_el0" : "=r" (sysinfo) );
#else
# error "current architecture not supported"
#endif
  return sysinfo;
}

void mtcp_set_thread_sysinfo(void *sysinfo) {
#if defined(__i386__) || defined(__x86_64__)
  asm volatile (CLEAN_FOR_64_BIT(mov %0, %%gs:) DEFAULT_SYSINFO_OFFSET "\n\t"
                : : "r" (sysinfo) );
#elif defined(__arm__)
  int mtcp_sys_errno = 0;
  mtcp_sys_kernel_set_tls(sysinfo);
#elif defined(__aarch64__)
  asm volatile ("msr     tpidr_el0, %[gs]" : : [gs] "r" (sysinfo) );
#else
# error "current architecture not supported"
#endif
}

// We turn off va_addr_rand(/proc/sys/kernel/randomize_va_space).
// For a _given_ binary,
// this fixes the address of the vdso.  Luckily, on restart, we
// get our vdso from mtcp_restart.  So, we need to maintain two
// vdso segments:  one from the user binary and one from each
// invocation of mtcp_restart during iterated restarts.
# define NO_RAND_VA_PERSONALITY 1

//======================================================================
// Get and set AT_SYSINFO for purposes of patching address in vdso

//======================================================================
// Used to check if vdso is an issue

#define MAX_ARGS 500
static int write_args(char **vector, char *filename) {
  int mtcp_sys_errno;
  ssize_t i;
  int fd;
  char strings[10001];
  char *str = strings;

  if (-1 == (fd = mtcp_sys_open2(filename, O_RDONLY))) {
    MTCP_PRINTF("Error %d opening %s\n", filename, mtcp_sys_errno);
    mtcp_sys_exit(1);
  }
  strings[1000] = '\0';
  ssize_t num_read = mtcp_read_all(fd, strings, 10000);
  mtcp_sys_close(fd);

  if (num_read == -1)
    return -1;

  for (i = 0; str - strings < num_read && i < MAX_ARGS; i++) {
    vector[i] = str;
    while (*str++ != '\0')
      ;
  }
  vector[i] = NULL;
  return 0;
}

static char *mygetenv(const char *name, char **environ) {
  int i = 1;
  size_t len = mtcp_strlen(name);
  for (i = 0; i < MAX_NEW_ENVP_SIZE && environ[i] !=NULL; i++) {
    if (mtcp_strstartswith(environ[i], name)) {
      if (mtcp_strlen(environ[i]) > len && environ[i][len] == '=') {
        return &(environ[i][len+1]);
      }
    }
  }
  return NULL;
}

static unsigned long getenv_oldpers(char **environ) {
  int mtcp_sys_errno;
  unsigned long oldpers = 0;
  char *oldpers_str = mygetenv("MTCP_OLDPERS", environ);
  if (oldpers_str == NULL) {
    MTCP_PRINTF("internal error!\n");
    mtcp_sys_exit(1);
  }
  while (*oldpers_str != '\0')
    oldpers = (oldpers << 1) + (*oldpers_str++ == '1' ? 1 : 0);
  return oldpers;
}

static int setenv_oldpers(int oldpers, char **environ) {
    static char oldpers_str[sizeof(oldpers)*8+1];
    int i = sizeof(oldpers_str);
    oldpers_str[--i] = '\0';
    while (i >= 0) {
      oldpers_str[i--] = ((oldpers & 1) ? '1' : '0');
      oldpers = oldpers >> 1;
    }
    return setPersonalityEnv("MTCP_OLDPERS", oldpers_str, environ);
}

/* Turn off randomize_va (by re-exec'ing) or warn user if vdso_enabled is on. */
void mtcp_check_vdso(char **environ)
{
  int mtcp_sys_errno;
  char buf;
#ifdef RESET_THREAD_SYSINFO
  get_at_sysinfo(environ); /* Initialize pointer to environ for later calls */
#endif

#ifdef NO_RAND_VA_PERSONALITY
  /* Set ADDR_NO_RANDOMIZE bit;
   * In Ubuntu Linux 2.6.24 kernel, This places vdso in  a different
   * fixed position in mtcp_init (since /lib/ld-2.7.so is inserted
   * above [vdso] and below [stack].  mtcp_restart has no /lib/ld-2.7.so.
   */
  int pers = mtcp_sys_personality(0xffffffffUL); /* get current personality */
  if (pers & ADDR_NO_RANDOMIZE) { /* if no addr space randomization ... */
    if (mygetenv("MTCP_OLDPERS", environ) != NULL) {
      /* restore orig pre-exec personality */
      mtcp_sys_personality(getenv_oldpers(environ));
      if (-1 == unsetPersonalityEnv("MTCP_OLDPERS", environ))
        MTCP_PRINTF("Error: unsetenv\n");
    }
    return; /* skip the rest */
  }

  if (! (pers & ADDR_NO_RANDOMIZE)) /* if addr space randomization ... */
  {
    unsigned long oldpers = pers;
    /* then turn off randomization and (just in case) remove
     * ADDR_COMPAT_LAYOUT
     */
    mtcp_sys_personality((pers | ADDR_NO_RANDOMIZE) & ~ADDR_COMPAT_LAYOUT);
    if ( ADDR_NO_RANDOMIZE & mtcp_sys_personality(0xffffffffUL) ) /* if it's off now */
    { char runtime[PATH_MAX+1];
      int i = mtcp_sys_readlink("/proc/self/exe", runtime, PATH_MAX);
      if ( i != -1)
      { char *argv[MAX_ARGS+1];

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
        write_args(argv, "/proc/self/cmdline");
        runtime[i] = '\0';
        setenv_oldpers(oldpers, environ);
        mtcp_sys_execve(runtime, argv, environ);
      }
      if (-1 == mtcp_sys_personality(oldpers)) /* reset if we couldn't exec */
        MTCP_PRINTF("Error %d in personality\n", mtcp_sys_errno);
    }
  }
#endif

  /* We failed to turn off address space rand., but maybe vdso is not enabled
   * On newer kernels, there is no /proc/sys/vm/vdso_enabled, we will cross our
   *  fingers and continue anyways.
   */
  int fd = mtcp_sys_open2("/proc/sys/vm/vdso_enabled", O_RDONLY);
  if (fd == -1)
    return;  /* In older kernels, if it doesn't exist, it can't be enabled. */
  if (mtcp_read_all(fd, &buf, sizeof(buf)) != sizeof(buf)) {
    MTCP_PRINTF("Error %d reading /proc/sys/vm/vdso_enabled\n", mtcp_sys_errno);
    mtcp_sys_exit(1);
  }
  if (-1 == mtcp_sys_close(fd)) {
    MTCP_PRINTF("Error %d closing /proc/sys/vm/vdso_enabled\n", mtcp_sys_errno);
    mtcp_sys_exit(1);
  }
  /* This call also caches AT_SYSINFO for use by mtcp_set_thread_sysinfo() */
  if (mtcp_have_thread_sysinfo_offset(environ))
    return;
  if (buf == '1') {
    MTCP_PRINTF("\n\n\nPROBLEM:  cat /proc/sys/vm/vdso_enabled returns 1\n"
    "  Further, I failed to find SYSINFO_OFFSET in TLS.\n"
    "  Can't work around this problem.\n"
    "  Please run this program again after doing as root:\n"
    "                                    echo 0 > /proc/sys/vm/vdso_enabled\n"
    "  Alternatively, upgrade kernel to one that allows for a personality\n"
    "  with ADDR_NO_RANDOMIZE in /usr/include/linux/personality.h.\n");
    mtcp_sys_exit(1);
  }
}

static char* newEnv[MAX_NEW_ENVP_SIZE];
static int newEnvInitialized = 0;
static int setPersonalityEnv(const char* name, const char* val, char **environ)
{
  int mtcp_sys_errno;
  int i;
  static char buf[64];
  if (!newEnvInitialized) {
    for (i = 0; environ[i] != NULL; i++) {
      if (i >= MAX_NEW_ENVP_SIZE) {
        MTCP_PRINTF("Too large envp\n");
        mtcp_abort();
      }
      newEnv[i] = environ[i];
    }
    while (i < MAX_NEW_ENVP_SIZE) {
      newEnv[i++] = NULL;
    }
    newEnvInitialized = 1;
    environ = newEnv;
  }

  char *ptr = (char*) mygetenv(name, environ);
  size_t len = mtcp_strlen(val);

  if (ptr == NULL) {
    if (mtcp_strlen(name) + len + 2 > sizeof(buf)) {
      MTCP_PRINTF("buffer too small\n");
      mtcp_abort();
    }

    mtcp_strncpy(buf, name, mtcp_strlen(name));
    mtcp_strncat(buf, "=", 1);
    mtcp_strncat(buf, val, len);

    for (i = 0; newEnv[i] != NULL; i++);
    if (i >= MAX_NEW_ENVP_SIZE) {
      MTCP_PRINTF("Too large envp\n");
      mtcp_abort();
    }
    newEnv[i] = buf;
    return 0;
  }

  // FIXME: Re-check the logic here and find out the corner case(s).
  if (len > 0) {
    mtcp_strncpy(ptr, val, len);
  } else {
    *ptr = '\0';
  }
  return 0;
}

static int unsetPersonalityEnv(const char* name, char **environ)
{
  int i = 1;
  size_t len = mtcp_strlen(name);
  for (i = 0; i < MAX_NEW_ENVP_SIZE; i++) {
    if (mtcp_strstartswith(environ[i], name)) {
      if (mtcp_strlen(environ[i]) > len && environ[i][len] == '=') {
        break;
      }
    }
  }

  while (i < MAX_NEW_ENVP_SIZE - 1 && environ[i] != NULL) {
    environ[i] = environ[i+1];
    i++;
  }
  return 1;
}

#ifdef STANDALONE
int main() {
  mtcp_check_vdso();
  // FIXME: Replace it with mtcp_sys_XXX
  //system("echo ulimit -s | sh");
  return 0;
}
#endif
