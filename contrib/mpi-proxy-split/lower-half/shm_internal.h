#ifndef SHM_INTERNAL_H
# define SHM_INTERNAL_H 1

#include "lower_half_api.h"

#ifndef __set_errno
# define __set_errno(Val) errno = (Val)
#endif

typedef struct __ShmInfo
{
  int shmid;
  size_t size;
} ShmInfo_t;

extern void* __real_shmat(int shmid, const void *shmaddr, int shmflg);
extern int __real_shmget(key_t key, size_t size, int shmflg);

extern int getShmIdx(int shmid);
extern int addShm(int shmid, size_t size);

// FIXME: Make this dynamic
#define MAX_SHM_TRACK 20
extern int shmidx;
extern ShmInfo_t shms[MAX_SHM_TRACK];

#endif // ifndef SHM_INTERNAL_H
