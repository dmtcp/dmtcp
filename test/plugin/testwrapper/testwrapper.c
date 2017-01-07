#include <stdio.h>
#include <sys/time.h>
#include "config.h"
#include "dmtcp.h"
#include <mpi.h>
void
print_time()
{
  struct timeval val;

  gettimeofday(&val, NULL);
  printf("%ld %ld", (long)val.tv_sec, (long)val.tv_usec);
}

struct _MPISignature{
 void *buffer;
 int count;
 MPI_Datatype mDtype;
 int src;
 int tag;
 MPI_Comm comm;
 MPI_Status *status;
};
typedef struct _MPISignature MPI_CallSign;
int MPI_Recv(
        void *buf,
        int count,
        MPI_Datatype dt,
        int src, int tag,
        MPI_Comm comm,
        MPI_Status *stat
        )
{
    int nu = *(int*)buf;
    MPI_CallSign mpiSign = {.buffer = buf,
        .count = count,
        .mDtype = dt,
        .src = src,
        .tag = tag,
        .comm = comm,
        .status = stat,
    };
    int sz;
    int datasz;
    FILE *fp = fopen("dump.z","w");

    void *_cbuf = malloc((sz = sizeof(mpiSign) +
                (datasz = mpiSign.count * sizeof(mpiSign.mDtype))));

    memcpy(_cbuf, &mpiSign, sizeof(mpiSign));
    memcpy(_cbuf + sizeof(mpiSign), mpiSign.buffer, datasz);

    fwrite(_cbuf, 1, sz, fp);
    fclose(fp);
    printf("rcvng: ");  printf("%d",nu)  ;

    unsigned int result = NEXT_FNC(MPI_Recv)(buf,
            count, dt,src,
            tag, comm, stat );

    return result;
}

static void
checkpoint()
{
  printf("\n*** The plugin %s is being called before checkpointing. ***\n",
         __FILE__);
}

static void
resume()
{
  printf("*** The plugin %s has now been checkpointed. ***\n", __FILE__);
}

static DmtcpBarrier barriers[] = {
  { DMTCP_GLOBAL_BARRIER_PRE_CKPT, checkpoint, "checkpoint" },
  { DMTCP_GLOBAL_BARRIER_RESUME, resume, "resume" }
};

DmtcpPluginDescriptor_t sleep1_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "sleep1",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Sleep1 plugin",
  DMTCP_DECL_BARRIERS(barriers),
  NULL
};

DMTCP_DECL_PLUGIN(sleep1_plugin);
