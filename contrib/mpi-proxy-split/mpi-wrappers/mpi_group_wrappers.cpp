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

USER_DEFINED_WRAPPER(int, Comm_group, (MPI_Comm) comm, (MPI_Group *) group)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Comm realComm = VIRTUAL_TO_REAL_COMM(comm);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Comm_group)(realComm, group);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && LOGGING()) {
    MPI_Group virtGroup = ADD_NEW_GROUP(*group);
    *group = virtGroup;
    LOG_CALL(restoreGroups, Comm_group, comm, *group);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Group_size, (MPI_Group) group, (int *) size)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Group realGroup = VIRTUAL_TO_REAL_GROUP(group);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Group_size)(realGroup, size);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

int
MPI_Group_free_internal(MPI_Group *group)
{
  int retval;
  MPI_Group realGroup = VIRTUAL_TO_REAL_GROUP(*group);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Group_free)(&realGroup);
  RETURN_TO_UPPER_HALF();
  return retval;
}

USER_DEFINED_WRAPPER(int, Group_free, (MPI_Group *) group)
{
  DMTCP_PLUGIN_DISABLE_CKPT();
  int retval = MPI_Group_free_internal(group);
  if (retval == MPI_SUCCESS && LOGGING()) {
    // NOTE: We cannot remove the old group, since we'll need
    // to replay this call to reconstruct any comms that might
    // have been created using this group.
    //
    // realGroup = REMOVE_OLD_GROUP(*group);
    // CLEAR_GROUP_LOGS(*group);
    LOG_CALL(restoreGroups, Group_free, *group);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Group_compare, (MPI_Group) group1,
                     (MPI_Group) group2, (int *) result)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Group realGroup1 = VIRTUAL_TO_REAL_GROUP(group1);
  MPI_Group realGroup2 = VIRTUAL_TO_REAL_GROUP(group2);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Group_compare)(realGroup1, realGroup2, result);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Group_rank, (MPI_Group) group, (int *) rank)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Group realGroup = VIRTUAL_TO_REAL_GROUP(group);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Group_rank)(realGroup, rank);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Group_incl, (MPI_Group) group, (int) n,
                     (const int*) ranks, (MPI_Group *) newgroup)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Group realGroup = VIRTUAL_TO_REAL_GROUP(group);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Group_incl)(realGroup, n, ranks, newgroup);
  RETURN_TO_UPPER_HALF();
  if (retval == MPI_SUCCESS && LOGGING()) {
    MPI_Group virtGroup = ADD_NEW_GROUP(*newgroup);
    *newgroup = virtGroup;
    FncArg rs = CREATE_LOG_BUF(ranks, n * sizeof(int));
    LOG_CALL(restoreGroups, Group_incl, group, n, rs, *newgroup);
  }
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

USER_DEFINED_WRAPPER(int, Group_translate_ranks, (MPI_Group) group1,
                     (int) n, (const int) ranks1[], (MPI_Group) group2,
                     (int) ranks2[])
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Group realGroup1 = VIRTUAL_TO_REAL_GROUP(group1);
  MPI_Group realGroup2 = VIRTUAL_TO_REAL_GROUP(group2);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Group_translate_ranks)(realGroup1, n, ranks1,
                                            realGroup2, ranks2);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}

PMPI_IMPL(int, MPI_Comm_group, MPI_Comm comm, MPI_Group *group)
PMPI_IMPL(int, MPI_Group_size, MPI_Group group, int *size)
PMPI_IMPL(int, MPI_Group_free, MPI_Group *group)
PMPI_IMPL(int, MPI_Group_compare, MPI_Group group1,
          MPI_Group group2, int *result)
PMPI_IMPL(int, MPI_Group_rank, MPI_Group group, int *rank)
PMPI_IMPL(int, MPI_Group_incl, MPI_Group group, int n,
          const int *ranks, MPI_Group *newgroup)
PMPI_IMPL(int, MPI_Group_translate_ranks, MPI_Group group1, int n,
          const int ranks1[], MPI_Group group2, int ranks2[]);
