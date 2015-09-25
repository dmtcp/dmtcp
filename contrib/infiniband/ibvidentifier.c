#include "ibvidentifier.h"

ibv_qp_id * create_ibv_id(int qpn, int lid, void * buffer, int size)
{
  srand48(getpid() * time(NULL));
  if (size != sizeof(ibv_qp_id)) {
    return NULL;
  }

  ibv_qp_id * id = buffer;

  id->qpn = qpn;
  id->lid = lid;
  id->psn = lrand48() & 0xffffff;

  return id;
}
