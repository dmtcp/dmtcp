// _DEFAULT_SOURCE for mkstemp  (WHY?)
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
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

int
main()
{
  int count = 1;

  while (1) {
    void *addr = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(addr != MAP_FAILED);
    assert(munmap(addr, 4096) == 0);

    printf(" %2d ", count++);
    fflush(stdout);
    sleep(2);
  }
  return 0;
}
