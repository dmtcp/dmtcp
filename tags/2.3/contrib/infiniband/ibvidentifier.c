#include "ibvidentifier.h"

struct ibv_id * create_ibv_id(int qpn, int lid, void * buffer, int size)
{
  srand48(getpid() * time(NULL));
  if (size != sizeof(struct ibv_id))
    return NULL;

  struct ibv_id * id = buffer;

  id->qpn = qpn;
  id->lid = lid;
  id->psn = lrand48() & 0xffffff;

  return id;
}
