#include <sys/shm.h>
#include <errno.h>
#include <assert.h>
#include <string.h>

#include "mmap_internal.h"
#include "shm_internal.h"

/* Attach the shared memory segment associated with SHMID to the data
 *    segment of the calling process.  SHMADDR and SHMFLG determine how
 *       and where the segment is attached.  */

void*
__wrap_shmat(int shmid, const void *shmaddr, int shmflg)
{
  void *addr = NULL;
  size_t len = PAGE_SIZE;

  if (shmaddr == NULL) {
    int idx = getShmIdx(shmid);
    if (idx != -1) {
      // The region was created by a regular shmget call
      addr = getNextAddr(shms[idx].size);
      len = shms[idx].size;
    } else {
      // The region was not created by a regular shmget
      // call. Perhaps, it was an inline syscall?
      struct shmid_ds ds = {0};
      int rc = shmctl(shmid, IPC_STAT, &ds);
      assert(rc == 0);
      addShm(shmid, ds.shm_segsz);
      addr = getNextAddr(ds.shm_segsz);
      len = ds.shm_segsz;
    }
  }
  void *ret = __real_shmat(shmid, addr, shmflg);
  if (ret != (void*)-1) {
    int idx = getMmapIdx(addr);
    if (idx != -1) {
      mmaps[idx].len = len;
      mmaps[idx].unmapped = 0;
    } else {
      mmaps[numRegions].addr = ret;
      mmaps[numRegions].len = len;
      mmaps[numRegions].unmapped = 0;
      numRegions = (numRegions + 1) % MAX_TRACK;
    }
  }
  return ret;
}

// TODO: Remove mappings on shmdt

// stub_warning (shmat)
