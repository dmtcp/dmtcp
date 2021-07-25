// FIXME:  Why do we have all these libc headers if we're not using libc here?
#include <asm/prctl.h>
#include <sys/prctl.h>
#include <sys/personality.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <ucontext.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <link.h>

#include "mtcp_sys.h"
#include "mtcp_util.h"
#include "mtcp_header.h"
#include "mtcp_split_process.h"

int mtcp_sys_errno;
LowerHalfInfo_t lh_info;
// FIXME:  The value of pdlsym must be passed from mtcp_restart to
//         the upper half, so that NEXT_FNC() in mpi-wrappers in libmana.so in 
//         can use the updated pdlsym in case the address of the lower half
//         in the restarted process has changed.
//         But the lower half is loaded at a fixed address.  So, the address of
//         pdlsym can be a fixed address that is unused by the upper half.
//         In this case, we don't need to pass pdlsym to the upper half.
static proxyDlsym_t pdlsym; // initialized to (proxyDlsym_t)lh_info.lh_dlsym

static unsigned long origPhnum;
static unsigned long origPhdr;

static void patchAuxv(ElfW(auxv_t) *, unsigned long , unsigned long , int );
static int mmap_iov(const struct iovec *iov, int prot, char *argv0);
static int read_lh_proxy_bits(pid_t childpid, char *argv0);
static pid_t startProxy(char *argv0, char **envp);
static int initializeLowerHalf(void);
static MemRange_t setLhMemRange(void);

int
splitProcess(char *argv0, char **envp)
{
#if 0
  compileProxy();
#endif
  DPRINTF("Initializing Lower-Half Proxy");
  pid_t childpid = startProxy(argv0, envp);
  int ret = -1;
  if (childpid > 0) {
    // Parent has read from pipefd_out; child is ready.
    ret = read_lh_proxy_bits(childpid, argv0);
    mtcp_sys_kill(childpid, SIGKILL);
    mtcp_sys_wait4(childpid, NULL, 0, NULL);
  }
  if (ret == 0) {
    ret = initializeLowerHalf();
  }
  return ret;
}

// Local functions
static void
patchAuxv(ElfW(auxv_t) *av, unsigned long phnum, unsigned long phdr, int save)
{
  for (; av->a_type != AT_NULL; ++av) {
    switch (av->a_type) {
      case AT_PHNUM:
        if (save) {
          origPhnum = av->a_un.a_val;
          av->a_un.a_val = phnum;
        } else {
          av->a_un.a_val = origPhnum;
        }
        break;
      case AT_PHDR:
        if (save) {
         origPhdr = av->a_un.a_val;
         av->a_un.a_val = phdr;
        } else {
          av->a_un.a_val = origPhdr;
        }
        break;
      default:
        break;
    }
  }
}

static int
mmap_iov(const struct iovec *iov, int prot, char *argv0)
{
  int mtcp_sys_errno;
  void *base = (void *)ROUND_DOWN(iov->iov_base);
  size_t len = ROUND_UP(iov->iov_len);
  int flags =  MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS;
  // Get fd for "lh_proxy", for use in mmap().  Kernel will then
  //   display the lh_proxy pathname in /proc/PID/maps.
  char *last_component = mtcp_strrchr(argv0, '/');
  MTCP_ASSERT(mtcp_strlen("lh_proxy") <= mtcp_strlen(last_component+1));
  mtcp_strcpy(last_component+1, "lh_proxy");
  int imagefd = mtcp_sys_open(argv0, O_RDONLY, 0);
  if (imagefd == -1) {
    MTCP_PRINTF("Error opening %s: %d\n", argv0, mtcp_sys_errno);
    return -1;
  }
  flags ^= MAP_ANONYMOUS;
  void *addr = mtcp_sys_mmap(base, len, prot, flags, imagefd, 0);
  if (addr == MAP_FAILED) {
    MTCP_PRINTF("Error mmaping memory: %p, %lu Bytes, %d", base, len,
                mtcp_sys_errno);
    return -1;
  }
  mtcp_sys_close(imagefd);
  return 0;
}

static int
read_lh_proxy_bits(pid_t childpid, char *argv0)
{
  int ret = -1;
  const int IOV_SZ = 2;
  struct iovec remote_iov[IOV_SZ];
  // text segment
  remote_iov[0].iov_base = lh_info.startText;
  remote_iov[0].iov_len = (unsigned long)lh_info.endText -
                          (unsigned long)lh_info.startText;
  ret = mmap_iov(&remote_iov[0], PROT_READ|PROT_EXEC|PROT_WRITE, argv0);
  // data segment
  remote_iov[1].iov_base = lh_info.startData;
  remote_iov[1].iov_len = (unsigned long)lh_info.endOfHeap -
                          (unsigned long)lh_info.startData;
  ret = mmap_iov(&remote_iov[1], PROT_READ|PROT_WRITE, argv0);
  // NOTE:  In our case local_iov will be same as remote_iov.
  // NOTE:  This requires same privilege as ptrace_attach (valid for child)
  int i = 0;
  for (i = 0; i < IOV_SZ; i++) {
    DPRINTF("Reading segment from lh proxy: %p, %lu Bytes\n",
            remote_iov[i].iov_base, remote_iov[i].iov_len);
    ret = mtcp_sys_process_vm_readv(childpid, remote_iov + i /* local_iov */,
                                    1, remote_iov + i, 1, 0);
    MTCP_ASSERT(ret != -1);
    // If the next assert fails, then we had a partial read.
    MTCP_ASSERT(ret == remote_iov[i].iov_len);
  }

  // Can remove PROT_WRITE now that we've oppulated the text segment.
  ret = mtcp_sys_mprotect((void *)ROUND_DOWN(remote_iov[0].iov_base),
                          ROUND_UP(remote_iov[0].iov_len),
                          PROT_READ | PROT_EXEC);
  return ret;
}

// Returns the PID of the proxy child process
static pid_t
startProxy(char *argv0, char **envp)
{
  int pipefd_in[2] = {0};
  int pipefd_out[2] = {0};
  int mtcp_sys_errno;

  if (mtcp_sys_pipe(pipefd_in) < 0) {
    MTCP_PRINTF("Failed to create pipe: %d", mtcp_sys_errno);
    return -1;
  }
  if (mtcp_sys_pipe(pipefd_out) < 0) {
    MTCP_PRINTF("Failed to create pipe: %d", mtcp_sys_errno);
    return -1;
  }

  // See "Lower-half loading and initialization" from MANA documentation
  //
  // Child does execve to lh_proxy from mpi-split-process/lower-half.
  // lh_proxy executes the constructor in libproxy.c that intializes lh_info
  // and writes lh_info to the stdout, which was redirected to pipefd_in.
  // Parent reads lh_info from child through pipefd_out. 
  // Parent calls read_lh_proxy_bits, which calls procss_vm_read on child.
  // Then the parent kills the child.
  int childpid = mtcp_sys_fork();
  switch (childpid) {
    case -1:
      MTCP_PRINTF("Failed to fork proxy: %d", mtcp_sys_errno);
      break;

    case 0: // in child
    {
      mtcp_sys_dup2(pipefd_out[1], 1); // Will write lh_info to stdout.
      mtcp_sys_close(pipefd_out[1]);
      mtcp_sys_close(pipefd_out[0]); // Close reading end of pipe.

      char* args[] = {"NO_SUCH_EXECUTABLE", NULL};
      // Replace ".../mtcp_restart" by ".../lh_proxy" in argv0/args[0]
      args[0] = argv0;
      char *last_component = mtcp_strrchr(args[0], '/');
      MTCP_ASSERT(mtcp_strlen("lh_proxy") <= mtcp_strlen(last_component+1));
      mtcp_strcpy(last_component+1, "lh_proxy");

      // Move reading end of pipe to stadin of lh_proxy.
      // Can then write pipefd_out[1] to lh_proxy.
      //   (But that would be better if lh_proxy simply wrote to stdout.)
      // Then, lh_proxy can read lh_mem_range (type: MemRange_t).
      mtcp_sys_dup2(pipefd_in[0], 0);
      mtcp_sys_close(pipefd_in[0]);

      // FIXME: This is platform-dependent.  The lower half has hardwired
      //        addresses.  They must be changed for each platform.
      mtcp_sys_personality(ADDR_NO_RANDOMIZE);
      MTCP_ASSERT(mtcp_sys_execve(args[0], args, envp) != -1);
      break;
    }

    default: // in parent
    {
      // Write memory range for mmap's by lower half to stdin of lh_proxy.
      MemRange_t mem_range = setLhMemRange();
      mtcp_sys_write(pipefd_in[1], &mem_range, sizeof(mem_range));
      mtcp_sys_close(pipefd_in[1]); // close writing end of pipe
      // Read full lh_info struct from stdout of lh_proxy, including orig memRange.
      mtcp_sys_close(pipefd_out[1]); // close write end of pipe
      if (mtcp_sys_read(pipefd_out[0], &lh_info, sizeof lh_info) < sizeof lh_info) {
        MTCP_PRINTF("Read fewer bytes than expected");
        break;
      }
      mtcp_sys_close(pipefd_out[0]);
    }
  }
  return childpid;
}

int
getMappedArea(Area *area, char *name) {
  Area area_map;
  int mapsfd = mtcp_sys_open2("/proc/self/maps", O_RDONLY);
  if (mapsfd < 0) {
    DPRINTF("Failed to open proc maps\n");
    mtcp_abort();
  }
  while (mtcp_readmapsline(mapsfd, &area_map)) {
    if (mtcp_strstr(area_map.name, name)) {
      *area = area_map;
      mtcp_sys_close(mapsfd);
      return 1; // found
    }
  }
  mtcp_sys_close(mapsfd);
  return 0; // not found
}

static MemRange_t
setLhMemRange()
{
  Area area;

  const uint64_t ONE_GB = 0x40000000;
  const uint64_t TWO_GB = 0x80000000;
  MemRange_t lh_mem_range;

  int found = getMappedArea(&area, "[stack]");
  if (found) {
#if !defined(MANA_USE_LH_FIXED_ADDRESS)
    lh_mem_range.start = (VA)area.addr - TWO_GB;
    lh_mem_range.end = (VA)area.addr - ONE_GB;
#else
    lh_mem_range.start = 0x2aab00000000;
    lh_mem_range.end =   0x2aab00000000 + ONE_GB;
#endif
  } else {
    DPRINTF("Failed to find [stack] memory segment\n");
    mtcp_abort();
  }
  return lh_mem_range;
}

static int
initializeLowerHalf()
{
  int ret = 0;
  int lh_initialized = 0;
  unsigned long argcAddr = (unsigned long)lh_info.parentStackStart;
  resetMmappedList_t resetMmaps = (resetMmappedList_t)lh_info.resetMmappedListFptr;

  // NOTE:
  // argv[0] is 1 LP_SIZE ahead of argc, i.e., startStack + sizeof(void*)
  // Stack End is 1 LP_SIZE behind argc, i.e., startStack - sizeof(void*)
  void *stack_end = (void*)(argcAddr - sizeof(unsigned long));
  int argc = *(int*)argcAddr;
  char **argv = (char**)(argcAddr + sizeof(unsigned long));
  char **ev = &argv[argc + 1];
  // char **ev = &((unsigned long*)stack_end[argc + 1]);
  pdlsym = (proxyDlsym_t)lh_info.lh_dlsym;

  // Copied from glibc source
  ElfW(auxv_t) *auxvec;
  {
    char **evp = ev;
    while (*evp++ != NULL);
    auxvec = (ElfW(auxv_t) *) evp;
  }
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  resetMmaps();
  // Set the auxiliary vector to correspond to the values of the lower half
  // (which is statically linked, unlike the upper half). Without this, glibc
  // will get confused during the initialization.
  patchAuxv(auxvec, lh_info.lh_AT_PHNUM, lh_info.lh_AT_PHDR, 1);
  // Save upper half's ucontext_t in a global object in the lower half, as
  // specified by the lh_info.g_appContext pointer
  getcontext((ucontext_t*)lh_info.g_appContext);

  if (!lh_initialized) {
    lh_initialized = 1;
    libcFptr_t fnc = (libcFptr_t)lh_info.libc_start_main;
    fnc((mainFptr)lh_info.main, argc, argv,
        (mainFptr)lh_info.libc_csu_init,
        (finiFptr)lh_info.libc_csu_fini, 0, stack_end);
  }
  DPRINTF("After getcontext");
  patchAuxv(auxvec, 0, 0, 0);
  RETURN_TO_UPPER_HALF();
  return ret;
}
