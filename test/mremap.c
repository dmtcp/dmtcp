#define _GNU_SOURCE
#include <assert.h>
#include <unistd.h>
#include <sys/mman.h>

int main() {
    long page_size = sysconf(_SC_PAGESIZE);
    void *old_addr1, *old_addr2, *new_addr;
    while (1) {
      old_addr1 = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      old_addr2 = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      munmap(old_addr2, page_size);
      // We now know that old_addr1 is mapped and old_addr2 is not mapped.
      new_addr = mremap(old_addr1, page_size, page_size,
                        MREMAP_MAYMOVE | MREMAP_FIXED, old_addr2);
      assert (new_addr == old_addr2);
      munmap(old_addr2, page_size);
      sleep(1);
    }
    return 0;
}
