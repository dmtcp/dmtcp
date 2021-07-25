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

using namespace dmtcp_mpi;


USER_DEFINED_WRAPPER(int, Op_create,
                     (MPI_User_function *) user_fn, (int) commute,
                     (MPI_Op *) op)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Op_create)(user_fn, commute, op);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && LOGGING()) {
    MPI_Op virtOp = ADD_NEW_OP(*op);
    *op = virtOp;
    LOG_CALL(restoreOps, Op_create, user_fn, commute, virtOp);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Op_free, (MPI_Op*) op)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Op realOp = MPI_OP_NULL;
  if (op) {
    realOp = VIRTUAL_TO_REAL_OP(*op);
  }
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Op_free)(&realOp);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && LOGGING()) {
    // NOTE: We cannot remove the old op, since we'll need
    // to replay this call to reconstruct any new op that might
    // have been created using this op.
    //
    // realOp = REMOVE_OLD_OP(*op);
    LOG_CALL(restoreOps, Op_free, *op);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}


PMPI_IMPL(int, MPI_Op_create, MPI_User_function *user_fn,
          int commute, MPI_Op *op)
PMPI_IMPL(int, MPI_Op_free, MPI_Op *op)
