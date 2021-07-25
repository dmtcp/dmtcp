// This could be libmpi.a or libproxy.a, with code to translate
//   between an MPI function and its address (similarly to dlsym()).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <asm/prctl.h>
#include <sys/prctl.h>
#include <sys/auxv.h>
#include <linux/limits.h>
#include <mpi.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>

#include "libproxy.h"
#include "mpi_copybits.h"
#include "procmapsutils.h"
#include "lower_half_api.h"

LowerHalfInfo_t lh_info = {0};
// This is the allocated buffer for lh_info.memRange
MemRange_t lh_memRange = {0};

static ucontext_t g_appContext;

static void* MPI_Fnc_Ptrs[] = {
  NULL,
  FOREACH_FNC(GENERATE_FNC_PTR)
  NULL,
};

// Local functions

static void
getDataFromMaps(const Area *text, Area *data, Area *heap)
{
  Area area;
  int mapsfd = open("/proc/self/maps", O_RDONLY);
  // text_area
  while (readMapsLine(mapsfd, &area)) {
    // First area after the text segment is the data segment
    if (area.addr >= text->endAddr) {
      *data = area;
      break;
    }
  }
  // NOTE: Assume that data and heap are contiguous.
  void *heap_sbrk = sbrk(0);
  while (readMapsLine(mapsfd, &area)) {
    if (strstr(area.name, "[heap]") && area.endAddr >= (VA)heap_sbrk) {
      *heap = area;
      break;
    }
  }
  close(mapsfd);
}

// FIXME: This code is duplicated in proxy and plugin. Refactor into utils.
static void
getTextSegmentRange(pid_t proc,                 // IN
                    unsigned long *start,       // OUT
                    unsigned long *end,         // OUT
                    unsigned long *stackstart)  // OUT
{
  // From man 5 proc: See entry for /proc/[pid]/stat
  int pid;
  char cmd[PATH_MAX]; char state;
  int ppid; int pgrp; int session; int tty_nr; int tpgid;
  unsigned flags;
  unsigned long minflt; unsigned long cminflt; unsigned long majflt;
  unsigned long cmajflt; unsigned long utime; unsigned long stime;
  long cutime; long cstime; long priority; long nice;
  long num_threads; long itrealvalue;
  unsigned long long starttime;
  unsigned long vsize;
  long rss;
  unsigned long rsslim; unsigned long startcode; unsigned long endcode;
  unsigned long startstack; unsigned long kstkesp; unsigned long kstkeip;
  unsigned long signal_map; unsigned long blocked; unsigned long sigignore;
  unsigned long sigcatch; unsigned long wchan; unsigned long nswap;
  unsigned long cnswap;
  int exit_signal; int processor;
  unsigned rt_priority; unsigned policy;

  FILE *f = NULL;
  if (proc == -1) {
    f = fopen("/proc/self/stat", "r");
  } else {
    char pids[] = "/proc/XXXXXX/stat";
    snprintf(pids, sizeof pids, "/proc/%u/stat", proc);
    f = fopen(pids, "r");
  }
  if (f) {
    fscanf(f, "%d "
              "%s %c "
              "%d %d %d %d %d "
              "%u "
              "%lu %lu %lu %lu %lu %lu "
              "%ld %ld %ld %ld %ld %ld "
              "%llu "
              "%lu "
              "%ld "
              "%lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu "
              "%d %d %u %u",
           &pid,
           cmd, &state,
           &ppid, &pgrp, &session, &tty_nr, &tpgid,
           &flags,
           &minflt, &cminflt, &majflt, &cmajflt, &utime, &stime,
           &cutime, &cstime, &priority, &nice, &num_threads, &itrealvalue,
           &starttime,
           &vsize,
           &rss,
           &rsslim, &startcode, &endcode, &startstack, &kstkesp, &kstkeip,
           &signal_map, &blocked, &sigignore, &sigcatch, &wchan, &nswap,
           &cnswap,
           &exit_signal, &processor,
           &rt_priority, &policy);
  }
  fclose(f);
  *start      = startcode;
  *end        = endcode;
  *stackstart = startstack;
}

static char**
copyArgv(int argc, char **argv)
{
  char **new_argv = malloc((argc+1) * sizeof *new_argv);
  for(int i = 0; i < argc; ++i)
  {
      size_t length = strlen(argv[i])+1;
      new_argv[i] = malloc(length);
      memcpy(new_argv[i], argv[i], length);
  }
  new_argv[argc] = NULL;
  return new_argv;
}

static int
isValidFd(int fd)
{
  return fcntl(fd, F_GETFL, 0) != -1;
}

// Global functions

void
updateEnviron(const char **newenviron)
{
  __environ = (char **)newenviron;
}

int
getRank()
{
  int ret = MPI_Init(NULL, NULL);
  int world_rank = -1;
  if (ret != -1) {
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  }
  return world_rank;
}

void*
mydlsym(enum MPI_Fncs fnc)
{
  if (fnc < MPI_Fnc_NULL || fnc > MPI_Fnc_Invalid) {
    return NULL;
  }
  return MPI_Fnc_Ptrs[fnc];
}

__attribute__((constructor))
void first_constructor()
{
  static int firstTime = 1;

  if (firstTime) {
    DLOG(NOISE, "(1) Constructor: We'll pass information to the parent.\n");
    firstTime = 0;

    // Pre-initialize this component of lh_info.
    // mtcp_restart analyzes the memory layout, and then writes this to us.
    // lh_memRange is the memory range to be used for any mmap's by lower half.
    read(0, &lh_memRange, sizeof(lh_memRange));

    unsigned long start, end, stackstart;
    unsigned long pstart, pend, pstackstart;
    unsigned long fsaddr = 0;
    Area txt, data, heap;
    getTextSegmentRange(getpid(), &start, &end, &stackstart);
    getTextSegmentRange(getppid(), &pstart, &pend, &pstackstart);
    syscall(SYS_arch_prctl, ARCH_GET_FS, &fsaddr);
    start = ROUND_UP(start);
    end   = ROUND_UP(end);
    txt.addr = (VA)start;
    txt.endAddr = (VA)end;
    getDataFromMaps(&txt, &data, &heap);

    // TODO: Verify that this gives us the right value every time
    // Perhaps use proc maps in the future?
    int argc = *(int*)stackstart;
    char **argv = (char**)(stackstart + sizeof(unsigned long));

    lh_info.startText = (void*)start;
    lh_info.endText = (void*)end;
    lh_info.startData = (void*)data.addr;
    lh_info.endOfHeap = (void*)heap.endAddr;
    lh_info.libc_start_main = &__libc_start_main;
    lh_info.main = &main;
    lh_info.libc_csu_init = &__libc_csu_init;
    lh_info.libc_csu_fini = &__libc_csu_fini;
    lh_info.fsaddr = (void*)fsaddr;
    lh_info.lh_AT_PHNUM = getauxval(AT_PHNUM);
    lh_info.lh_AT_PHDR = getauxval(AT_PHDR);
    lh_info.g_appContext = (void*)&g_appContext;
    lh_info.lh_dlsym = (void*)&mydlsym;
    lh_info.getRankFptr = (void*)&getRank;
    lh_info.parentStackStart = (void*)pstackstart;
    lh_info.updateEnvironFptr = (void*)&updateEnviron;
    lh_info.getMmappedListFptr = (void*)&getMmappedList;
    lh_info.resetMmappedListFptr = (void*)&resetMmappedList;
    lh_info.memRange = lh_memRange;
    DLOG(INFO, "startText: %p, endText: %p, startData: %p, endOfHeap; %p\n",
        lh_info.startText, lh_info.endText, lh_info.startData, lh_info.endOfHeap);

    // Write lh_info to stadout, for mtcp_split_process.c to read.
    write(1, &lh_info, sizeof lh_info);
    // It's okay to have an infinite loop here.  Our parent has promised to
    // kill us after it copies our bits.  So, this child doesn't need to exit.
    while(1);
  } else {
    DLOG(NOISE, "(2) Constructor: Running in the parent?\n");
  }
}

__attribute__((destructor))
void second_destructor()
{
  // Destructor: The application called exit in the destructor to
  // get here. After this, we call setcontext() to get back in the
  // application.
  DLOG(NOISE, "Destructor!\n");
}
