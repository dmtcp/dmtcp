#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpi.h>

#include "config.h"
#include "dmtcp.h"
#include "jassert.h"

#define ENV_MPI_RECORD  "DMTCP_MPI_START_RECORD"
#define ENV_MPI_REPLAY  "DMTCP_MPI_START_REPLAY"

// from dmtcpplugin.cpp
#define SUCCESS             0
#define NOTFOUND            -1
#define TOOLONG             -2
#define DMTCP_BUF_TOO_SMALL -3
#define INTERNAL_ERROR      -4
#define NULL_PTR            -5

struct _MPISignature{
 int count;
 MPI_Datatype mDtype;
 int src;
 int tag;
 MPI_Comm comm;
 MPI_Status status;
 int result;
 char *buffer;
};

typedef struct _MPISignature MPI_CallSign;

enum RUNNING_MODE
{
  NONE,
  RECORD,
  REPLAY,
};

static RUNNING_MODE currMode = NONE;
static FILE* logFile = NULL;

static void
errCheckGetRestartEnv(int ret)
{
    /* ret == -1 is fine; everything else is not */
    if (ret < -1 /* RESTART_ENV_NOT_FOUND */) {
        JASSERT(ret != TOOLONG).Text("mpiwrapper: DMTCP_PATH_PREFIX exceeds "
                "maximum size (10kb). Use a shorter environment variable "
                "or increase MAX_ENV_VAR_SIZE and recompile.");

        JASSERT(ret != DMTCP_BUF_TOO_SMALL).Text("dmtcpplugin: DMTCP_PATH_PREFIX exceeds "
                "dmtcp_get_restart_env()'s MAXSIZE. Use a shorter "
                "environment variable or increase MAXSIZE and recompile.");

        /* all other errors */
        JASSERT(ret >= 0).Text("Fatal error retrieving DMTCP_PATH_PREFIX "
                "environment variable.");
    }
}

static void
recordData(void *buf, MPI_CallSign mpiSign)
{
  JASSERT(logFile);
  JASSERT(currMode == RECORD);

  int sz = sizeof(mpiSign);
  int datasz = mpiSign.count * sizeof(mpiSign.mDtype);
  void *_cbuf = malloc(sz + datasz);
  JASSERT(_cbuf);

  memcpy(_cbuf, &mpiSign, sizeof(mpiSign));
  memcpy((char*)_cbuf + offsetof(MPI_CallSign, buffer), buf, datasz);

  fwrite(_cbuf, sz, 1, logFile);
  free(_cbuf);
  JTRACE("Recorded data");
}

static int
replayData(void *buf, MPI_Status *stat)
{
  JASSERT(logFile);
  JASSERT(currMode == REPLAY);

  MPI_CallSign mpiSign = {0};
  // -1 for the pointer to buffer
  fread(&mpiSign, sizeof(mpiSign) - 1, 1, logFile);

  int sz = sizeof(mpiSign);
  int datasz = mpiSign.count * sizeof(mpiSign.mDtype);
  fread(buf, datasz, 1, logFile);

  *stat = mpiSign.status;
  return mpiSign.result;
}

int MPI_Recv(void *buf, int count, MPI_Datatype dt,
             int src, int tag, MPI_Comm comm, MPI_Status *stat)
{
  int result = 0;
  if (currMode == RECORD) {
    result = NEXT_FNC(MPI_Recv)(buf, count, dt, src, tag, comm, stat);
    MPI_CallSign mpiSign = { .count = count,
                             .mDtype = dt,
                             .src = src,
                             .tag = tag,
                             .comm = comm,
                             .status = *stat,
                             .result = result,
                             .buffer = NULL};
    recordData(buf, mpiSign);
    goto done;
  } else if (currMode == REPLAY) {
    result =  replayData(buf, stat);
    goto done;
  }

  result = NEXT_FNC(MPI_Recv)(buf, count, dt, src, tag, comm, stat);
done:
  return result;
}

static void
checkpoint()
{
  if (logFile) {
    fclose(logFile);
  }
}

static void
restart()
{
  char mpiRecord[10] = {0};
  char mpiReplay[10] = {0};

  int ret = dmtcp_get_restart_env(ENV_MPI_RECORD, mpiRecord,
                                  sizeof(mpiRecord) - 1);
  JTRACE("MPI Recording") (mpiRecord);
  errCheckGetRestartEnv(ret);

  if (ret == SUCCESS) {
    currMode = RECORD;
    goto done;
  }

  ret = dmtcp_get_restart_env(ENV_MPI_REPLAY, mpiReplay,
                              sizeof(mpiReplay) - 1);
  JTRACE("MPI Replaying") (mpiReplay);
  errCheckGetRestartEnv(ret);

  if (ret == SUCCESS) {
    currMode = REPLAY;
    goto done;
  }

done:
  if (currMode == RECORD) {
    logFile = fopen("dump.z","w");
  } else if (currMode = REPLAY) {
    logFile = fopen("dump.z","r");
  } else {
    logFile = NULL;
  }
}

static DmtcpBarrier barriers[] = {
  { DMTCP_GLOBAL_BARRIER_PRE_CKPT, checkpoint, "checkpoint" },
  { DMTCP_GLOBAL_BARRIER_RESTART, restart, "restart" }
};

DmtcpPluginDescriptor_t mpiwrapper_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "mpiwrapper",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "MPI wrapper plugin",
  DMTCP_DECL_BARRIERS(barriers),
  NULL
};

DMTCP_DECL_PLUGIN(mpiwrapper_plugin);
