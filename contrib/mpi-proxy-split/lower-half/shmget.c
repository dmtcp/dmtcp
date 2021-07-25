#include <sys/shm.h>
#include <errno.h>

#include "shm_internal.h"

int shmidx = 0;
ShmInfo_t shms[MAX_SHM_TRACK] = {0};

int
getShmIdx(int shmid)
{
  for (int i = 0; i < MAX_SHM_TRACK; i++) {
    if (shms[i].shmid == shmid) {
      return i;
    }
  }
  return -1;
}

int
addShm(int shmid, size_t size)
{
  shms[shmidx].shmid = shmid;
  shms[shmidx].size = size;
  shmidx = (shmidx + 1) % MAX_SHM_TRACK;
}

int
__wrap_shmget(key_t key, size_t size, int shmflg)
{
  int rc = __real_shmget(key, size, shmflg);
  if (rc > 0) {
    addShm(rc, size);
  }
  return rc;
}
