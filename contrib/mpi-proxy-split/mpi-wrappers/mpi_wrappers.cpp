#include "mpi_plugin.h"  // lh_info declared via lower_half_api.h
#include "config.h"
#include "dmtcp.h"
#include "util.h"
#include "jassert.h"
#include "jfilesystem.h"
#include "protectedfds.h"
#include "mpi_nextfunc.h"
#include "virtual-ids.h"
#include "p2p_drain_send_recv.h"

#if 0
DEFINE_FNC(int, Init, (int *) argc, (char ***) argv)
DEFINE_FNC(int, Init_thread, (int *) argc, (char ***) argv,
           (int) required, (int *) provided)
#else
USER_DEFINED_WRAPPER(int, Init, (int *) argc, (char ***) argv) {
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Init)(argc, argv);
  RETURN_TO_UPPER_HALF();
  initialize_drain_send_recv();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}
USER_DEFINED_WRAPPER(int, Init_thread, (int *) argc, (char ***) argv,
                     (int) required, (int *) provided) {
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Init_thread)(argc, argv, required, provided);
  RETURN_TO_UPPER_HALF();
  initialize_drain_send_recv();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}
#endif
// FIXME: See the comment in the wrapper function, defined
// later in the file.
// DEFINE_FNC(int, Finalize, (void))
DEFINE_FNC(double, Wtime, (void))
DEFINE_FNC(int, Finalized, (int *) flag)
DEFINE_FNC(int, Get_processor_name, (char *) name, (int *) resultlen)
DEFINE_FNC(int, Initialized, (int *) flag)

USER_DEFINED_WRAPPER(int, Finalize, (void))
{
  // FIXME: With Cray MPI, for some reason, MPI_Finalize hangs when
  // running on multiple nodes. The MPI Finalize call tries to pthread_cancel
  // a polling thread in the lower half and then blocks on join. But,
  // for some reason, the polling thread never comes out of an ioctl call
  // and remains blocked.
  //
  // The workaround here is to simply return success to the caller without
  // calling into the real Finalize function in the lower half. This way
  // the application can proceed to exit without getting blocked forever.
  return MPI_SUCCESS;
}

USER_DEFINED_WRAPPER(int, Get_count,
                     (const MPI_Status *) status, (MPI_Datatype) datatype,
                     (int *) count)
{
  int retval;
  DMTCP_PLUGIN_DISABLE_CKPT();
  MPI_Datatype realType = VIRTUAL_TO_REAL_TYPE(datatype);
  JUMP_TO_LOWER_HALF(lh_info.fsaddr);
  retval = NEXT_FUNC(Get_count)(status, realType, count);
  RETURN_TO_UPPER_HALF();
  DMTCP_PLUGIN_ENABLE_CKPT();
  return retval;
}


PMPI_IMPL(int, MPI_Init, int *argc, char ***argv)
PMPI_IMPL(int, MPI_Finalize, void)
PMPI_IMPL(int, MPI_Finalized, int *flag)
PMPI_IMPL(int, MPI_Get_processor_name, char *name, int *resultlen)
PMPI_IMPL(double, MPI_Wtime, void)
PMPI_IMPL(int, MPI_Initialized, int *flag)
PMPI_IMPL(int, MPI_Init_thread, int *argc, char ***argv,
          int required, int *provided)
PMPI_IMPL(int, MPI_Get_count, const MPI_Status *status, MPI_Datatype datatype,
          int *count)
