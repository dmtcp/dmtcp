// _DEFAULT_SOURCE for mkstemp  (WHY?)
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

// For open()
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dlfcn.h>

#define ATOMIC_SHARED volatile __attribute((aligned))

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
  static __typeof__(&mmap) _real_mmap = (__typeof__(&mmap)) - 1;
  if (_real_mmap == (__typeof__(&mmap)) - 1) {
    _real_mmap = (__typeof__(&mmap)) dlsym(RTLD_NEXT, "mmap");
    assert(_real_mmap != NULL);
  }

  void *retval = _real_mmap(addr, length, prot, flags, fd, offset);
  return retval;
}

// Map the test binary the way the loader maps a library whose LOAD segments
// are aligned more coarsely than its size (e.g., libgomp's 2M alignment): one
// reservation running past EOF, with the gap made PROT_NONE.  This leaves
// file-backed areas whose offset is at and beyond EOF, which must survive
// checkpoint and restart.
static char *
map_past_eof()
{
  long pageSize = sysconf(_SC_PAGESIZE);
  int fd = open("/proc/self/exe", O_RDONLY);
  assert(fd != -1);

  struct stat st;
  assert(fstat(fd, &st) == 0);
  size_t filePages = (st.st_size + pageSize - 1) / pageSize;

  // Layout: [file pages: r--] [one page past EOF: ---] [two pages: r--]
  // The last area's offset lies strictly beyond EOF.
  size_t len = (filePages + 3) * pageSize;
  char *addr = mmap(0, len, PROT_READ, MAP_PRIVATE, fd, 0);
  assert(addr != MAP_FAILED);
  assert(mprotect(addr + filePages * pageSize, pageSize, PROT_NONE) == 0);
  close(fd);

  return addr;
}

int
main()
{
  int count = 1;
  char *pastEof = map_past_eof();

  while (1) {
    // The file-backed part must still hold the ELF header after restart.
    assert(memcmp(pastEof, "\177ELF", 4) == 0);

    void *addr = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(addr != MAP_FAILED);
    assert(munmap(addr, 4096) == 0);

    printf(" %2d ", count++);
    fflush(stdout);
    sleep(2);
  }
  return 0;
}
