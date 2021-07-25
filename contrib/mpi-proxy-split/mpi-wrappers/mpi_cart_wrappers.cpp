#include "mpi_plugin.h"
#include "config.h"
#include "dmtcp.h"
#include "util.h"
#include "jassert.h"
#include "jfilesystem.h"
#include "protectedfds.h"

#include "mpi_nextfunc.h"
#include "record-replay.h"
#include "virtual-ids.h"
#include "p2p_drain_send_recv.h"

using namespace dmtcp_mpi;

USER_DEFINED_WRAPPER(int, Cart_coords, (MPI_Comm) comm, (int) rank,
                     (int) maxdims, (int*) coords)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Cart_coords)(realComm, rank, maxdims, coords);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Cart_create, (MPI_Comm) old_comm, (int) ndims,
                     (const int*) dims, (const int*) periods, (int) reorder,
                     (MPI_Comm *) comm_cart)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(old_comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Cart_create)(realComm, ndims, dims,
                                  periods, reorder, comm_cart);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && LOGGING()) {
    MPI_Comm virtComm = ADD_NEW_COMM(*comm_cart);
    VirtualGlobalCommId::instance().createGlobalId(virtComm);
    *comm_cart = virtComm;
    active_comms.insert(virtComm);
    FncArg ds = CREATE_LOG_BUF(dims, ndims * sizeof(int));
    FncArg ps = CREATE_LOG_BUF(periods, ndims * sizeof(int));
    LOG_CALL(restoreCarts, Cart_create, old_comm, ndims,
             ds, ps, reorder, virtComm);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Cart_get, (MPI_Comm) comm, (int) maxdims,
                     (int*) dims, (int*) periods, (int*) coords)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Cart_get)(realComm, maxdims, dims, periods, coords);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Cart_map, (MPI_Comm) comm, (int) ndims,
                     (const int*) dims, (const int*) periods, (int *) newrank)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  // FIXME: Need to virtualize this newrank??
  retval = NEXT_FUNC(Cart_map)(realComm, ndims, dims, periods, newrank);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && LOGGING()) {
    FncArg ds = CREATE_LOG_BUF(dims, ndims * sizeof(int));
    FncArg ps = CREATE_LOG_BUF(periods, ndims * sizeof(int));
    LOG_CALL(restoreCarts, Cart_map, comm, ndims, ds, ps, newrank);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Cart_rank, (MPI_Comm) comm,
                     (const int*) coords, (int *) rank)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Cart_rank)(realComm, coords, rank);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Cart_shift, (MPI_Comm) comm, (int) direction,
                     (int) disp, (int *) rank_source, (int *) rank_dest)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Cart_shift)(realComm, direction,
                                 disp, rank_source, rank_dest);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && LOGGING()) {
    LOG_CALL(restoreCarts, Cart_shift, comm, direction,
             disp, rank_source, rank_dest);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Cart_sub, (MPI_Comm) comm,
                     (const int*) remain_dims, (MPI_Comm *) new_comm)
{
  int retval;

  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Cart_sub)(realComm, remain_dims, new_comm);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && LOGGING()) {
    int ndims = 0;
    MPI_Cartdim_get(comm, &ndims);
    MPI_Comm virtComm = ADD_NEW_COMM(*new_comm);
    VirtualGlobalCommId::instance().createGlobalId(virtComm);
    *new_comm = virtComm;
    active_comms.insert(virtComm);
    FncArg rs = CREATE_LOG_BUF(remain_dims, ndims * sizeof(int));
    LOG_CALL(restoreCarts, Cart_sub, comm, ndims, rs, virtComm);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Cartdim_get, (MPI_Comm) comm, (int *) ndims)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Cartdim_get)(realComm, ndims);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}


PMPI_IMPL(int, MPI_Cart_coords, MPI_Comm comm, int rank,
          int maxdims, int coords[])
PMPI_IMPL(int, MPI_Cart_create, MPI_Comm old_comm, int ndims,
          const int dims[], const int periods[], int reorder,
          MPI_Comm *comm_cart)
PMPI_IMPL(int, MPI_Cart_get, MPI_Comm comm, int maxdims,
          int dims[], int periods[], int coords[])
PMPI_IMPL(int, MPI_Cart_map, MPI_Comm comm, int ndims,
          const int dims[], const int periods[], int *newrank)
PMPI_IMPL(int, MPI_Cart_rank, MPI_Comm comm, const int coords[], int *rank)
PMPI_IMPL(int, MPI_Cart_shift, MPI_Comm comm, int direction,
          int disp, int *rank_source, int *rank_dest)
PMPI_IMPL(int, MPI_Cart_sub, MPI_Comm comm,
          const int remain_dims[], MPI_Comm *new_comm)
PMPI_IMPL(int, MPI_Cartdim_get, MPI_Comm comm, int *ndims)
