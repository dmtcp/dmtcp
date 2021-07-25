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
#include "two-phase-algo.h"
#include "p2p_drain_send_recv.h"

using namespace dmtcp_mpi;

USER_DEFINED_WRAPPER(int, Comm_size, (MPI_Comm) comm, (int *) world_size)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Comm_size)(realComm, world_size);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_rank, (MPI_Comm) comm, (int *) world_rank)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Comm_rank)(realComm, world_rank);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_create, (MPI_Comm) comm, (MPI_Group) group,
                     (MPI_Comm *) newcomm)
{
  std::function<int()> realBarrierCb = [=]() {
    int retval;
    DMTCP_PLUGIN_DISABLE_CKPT();
    MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
    MPI_Group realGroup = VIRTUAL_TO_REAL_GROUP(group);
    JUMP_TO_LOWER_HALF(lh_info.fsaddr);
    retval = NEXT_FUNC(Comm_create)(realComm, realGroup, newcomm);
    RETURN_TO_UPPER_HALF();
    if (retval == MPI_SUCCESS && LOGGING()) {
      MPI_Comm virtComm = ADD_NEW_COMM(*newcomm);
      VirtualGlobalCommId::instance().createGlobalId(virtComm);
      *newcomm = virtComm;
      active_comms.insert(virtComm);
      LOG_CALL(restoreComms, Comm_create, comm, group, virtComm);
    }
    DMTCP_PLUGIN_ENABLE_CKPT();
    return retval;
  };
  return twoPhaseCommit(comm, realBarrierCb);
}

USER_DEFINED_WRAPPER(int, Abort, (MPI_Comm) comm, (int) errorcode)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Abort)(realComm, errorcode);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_compare,
                     (MPI_Comm) comm1, (MPI_Comm) comm2, (int*) result)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm1 = VIRTUAL_TO_REAL_COMM(comm1);
  MPI_Comm realComm2 = VIRTUAL_TO_REAL_COMM(comm2);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Comm_compare)(realComm1, realComm2, result);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

int
MPI_Comm_free_internal(MPI_Comm *comm)
{
  int retval;
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(*comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Comm_free)(&realComm);
  RETURN_TO_UPPER_HALF();
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_free, (MPI_Comm *) comm)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int retval = MPI_Comm_free_internal(comm);
  if (retval == MPI_SUCCESS && LOGGING()) {
    // NOTE: We cannot remove the old comm from the map, since
    // we'll need to replay this call to reconstruct any other comms that
    // might have been created using this comm.
    //
    // realComm = REMOVE_OLD_COMM(*comm);
    // CLEAR_COMM_LOGS(*comm);
    active_comms.erase(*comm);
    LOG_CALL(restoreComms, Comm_free, *comm);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_set_errhandler,
                     (MPI_Comm) comm, (MPI_Errhandler) errhandler)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Comm_set_errhandler)(realComm, errhandler);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && LOGGING()) {
    LOG_CALL(restoreComms, Comm_set_errhandler, comm, errhandler);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Topo_test,
                     (MPI_Comm) comm, (int *) status)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Topo_test)(realComm, status);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_split_type, (MPI_Comm) comm, (int) split_type,
                     (int) key, (MPI_Info) inf, (MPI_Comm*) newcomm)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Comm_split_type)(realComm, split_type, key, inf, newcomm);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && LOGGING()) {
    MPI_Comm virtComm = ADD_NEW_COMM(*newcomm);
    VirtualGlobalCommId::instance().createGlobalId(virtComm);
    *newcomm = virtComm;
    active_comms.insert(virtComm);
    LOG_CALL(restoreComms, Comm_split_type, comm,
             split_type, key, inf, virtComm);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Attr_get, (MPI_Comm) comm, (int) keyval,
                     (void*) attribute_val, (int*) flag)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  int realCommKeyval = VIRTUAL_TO_REAL_COMM_KEYVAL(keyval);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Attr_get)(realComm, realCommKeyval, attribute_val, flag);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Attr_delete, (MPI_Comm) comm, (int) keyval)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  int realCommKeyval = VIRTUAL_TO_REAL_COMM_KEYVAL(keyval);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Attr_delete)(realComm, realCommKeyval);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && LOGGING()) {
    LOG_CALL(restoreComms, Attr_delete, comm, keyval);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Attr_put, (MPI_Comm) comm,
                     (int) keyval, (void*) attribute_val)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  int realCommKeyval = VIRTUAL_TO_REAL_COMM_KEYVAL(keyval);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Attr_put)(realComm, realCommKeyval, attribute_val);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && LOGGING()) {
    LOG_CALL(restoreComms, Attr_put, comm, keyval, attribute_val);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_create_keyval,
                     (MPI_Comm_copy_attr_function *) comm_copy_attr_fn,
                     (MPI_Comm_delete_attr_function *) comm_delete_attr_fn,
                     (int *) comm_keyval, (void *) extra_state)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Comm_create_keyval)(comm_copy_attr_fn,
                                         comm_delete_attr_fn,
                                         comm_keyval, extra_state);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && LOGGING()) {
    int virtCommKeyval = ADD_NEW_COMM_KEYVAL(*comm_keyval);
    *comm_keyval = virtCommKeyval;
    LOG_CALL(restoreComms, Comm_create_keyval,
             comm_copy_attr_fn, comm_delete_attr_fn,
             virtCommKeyval, extra_state);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_free_keyval, (int *) comm_keyval)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  int realCommKeyval = VIRTUAL_TO_REAL_COMM_KEYVAL(*comm_keyval);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Comm_free_keyval)(&realCommKeyval);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && LOGGING()) {
    // NOTE: We cannot remove the old comm_keyval from the map, since
    // we'll need to replay this call to reconstruct any other comms that
    // might have been created using this comm_keyval.
    LOG_CALL(restoreComms, Comm_free_keyval, *comm_keyval);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

int
MPI_Comm_create_group_internal(MPI_Comm comm, MPI_Group group, int tag,
                               MPI_Comm *newcomm)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  MPI_Group realGroup = VIRTUAL_TO_REAL_GROUP(group);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Comm_create_group)(realComm, realGroup, tag, newcomm);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Comm_create_group, (MPI_Comm) comm,
                     (MPI_Group) group, (int) tag, (MPI_Comm *) newcomm)
{
  std::function<int()> realBarrierCb = [=]() {
    int retval = MPI_Comm_create_group_internal(comm, group, tag, newcomm);
    if (retval == MPI_SUCCESS && LOGGING()) {
      MPI_Comm virtComm = ADD_NEW_COMM(*newcomm);
      VirtualGlobalCommId::instance().createGlobalId(virtComm);
      *newcomm = virtComm;
      active_comms.insert(virtComm);
      LOG_CALL(restoreComms, Comm_create_group, comm, group, tag, virtComm);
    }
    return retval;
  };
  return twoPhaseCommit(comm, realBarrierCb);
}

PMPI_IMPL(int, MPI_Comm_size, MPI_Comm comm, int *world_size)
PMPI_IMPL(int, MPI_Comm_rank, MPI_Group group, int *world_rank)
PMPI_IMPL(int, MPI_Abort, MPI_Comm comm, int errorcode)
PMPI_IMPL(int, MPI_Comm_create, MPI_Comm comm, MPI_Group group,
          MPI_Comm *newcomm)
PMPI_IMPL(int, MPI_Comm_compare, MPI_Comm comm1, MPI_Comm comm2, int *result)
PMPI_IMPL(int, MPI_Comm_free, MPI_Comm *comm)
PMPI_IMPL(int, MPI_Comm_set_errhandler, MPI_Comm comm,
          MPI_Errhandler errhandler)
PMPI_IMPL(int, MPI_Topo_test, MPI_Comm comm, int* status)
PMPI_IMPL(int, MPI_Comm_split_type, MPI_Comm comm, int split_type, int key,
          MPI_Info info, MPI_Comm *newcomm)
PMPI_IMPL(int, MPI_Attr_get, MPI_Comm comm, int keyval,
          void *attribute_val, int *flag)
PMPI_IMPL(int, MPI_Attr_delete, MPI_Comm comm, int keyval)
PMPI_IMPL(int, MPI_Attr_put, MPI_Comm comm, int keyval, void *attribute_val)
PMPI_IMPL(int, MPI_Comm_create_keyval,
          MPI_Comm_copy_attr_function * comm_copy_attr_fn,
          MPI_Comm_delete_attr_function * comm_delete_attr_fn,
          int *comm_keyval, void *extra_state)
PMPI_IMPL(int, MPI_Comm_free_keyval, int *comm_keyval)
PMPI_IMPL(int, MPI_Comm_create_group, MPI_Comm comm, MPI_Group group,
          int tag, MPI_Comm *newcomm)
