#!/bin/sh

# FIXME: USER FORGOT TO USE srun (detect and report error?):
# [40000] NOTE at socketconnlist.cpp:218 in scanForPreExisting; REASON='found pre-existing socket... will not be restored'
# fd = 3
# device = socket:[1350385918]
# [Sat Apr 10 09:03:13 2021] [unknown] Fatal error in MPI_Init: Other MPI error, error stack:
# MPIR_Init_thread(537):
# MPID_Init(246).......: channel initialization failed
# MPID_Init(647).......:  PMI2 init failed: 1
# bin/mana_launch: line 48: 15391 Aborted
# $dir/dmtcp_launch $options -h $host --no-gzip --join --disable-dl-plugin --with-plugin $PWD/lib/dmtcp/libmana.so "$target"

# FIXME: USER FORGOT TO USE srun and there is no salloc (detect and report error?):
# + bin/dmtcp_launch 10 -i -h cori03 --no-gzip --join --disable-dl-plugin --with-plugin /global/homes/g/gdc0/mana-rohgarg-orig/lib/dmtcp/libmana.so contrib/mpi-proxy-split/test/ping_pong.mana.exe
# *** ERROR:Executable to run w/ DMTCP appears not to be readable,
# ***or no such executable in path.

dir=`dirname $0`

if [ -z "$1" ]; then
  echo "USAGE:  $0 [--verbose] [DMTCP_OPTIONS ...] [--ckptdir DIR]" \\
  echo "                                                       MANA_EXECUTABLE"
  echo "        For DMTCP options, do: $0 --help"
  echo "  NOTE: MANA_EXECUTABLE must be compiled with libmpidummy.so"
  echo "        See $dir/../contrib/mpi-proxy-split/test/ for examples."
  exit 1
fi

options=""
help=0
verbose=0
srun_sbatch_found=0
while [ -n "$1" ]; do
  if [ "$1" == --verbose ]; then
    verbose=1
  elif [ "$1" == --help ]; then
    help=1
  elif [ "$1" == srun ] || [ "$1" == sbatch ]; then
    srun_sbatch_found=1
  elif [ "$1" == --ckptdir ]; then
    if [ ! -d "$2" ]; then
      echo "$0: --ckptdir $2: Checkpoint directory doesn't exist"
      exit 9
    fi
    options="$options $1" 
  else
    options="$options $1"
  fi
  shift
done

if [ "$help" -eq 1 ]; then
  $dir/dmtcp_launch --help $options
  exit 0
fi

if [ "$NERSC_HOST" = "cori" ]; then
  if [ -z "$SLURM_JOB_ID" ]; then
    echo "SLURM_JOB_ID env variable not set; No salloc/sbatch jobs running?"
    exit 2
  fi
  
  if [ "$srun_sbatch_found" -eq 1 ]; then
    echo ""
    echo "  *** Usage: srun/sbatch ... mana_launch ... MPI_EXECUTABLE ..."
    echo ""
    exit 3
  fi
else
  if [ -z "$MPI_LOCALNRANKS" ]; then
    echo "MPI_LOCALNRANKS env variable not set; No 'mpirun -np XX' used?"
  fi
fi

if ls -d ckpt_rank_* 2>/dev/null 1>&2; then
  echo 'Checkpoint files already in current directory:  ls -d ckpt_rank_*'
  echo 'Please move or delete previous checkpoint before running "mana_launch"'
  exit 4
fi

host=`hostname`
submissionHost=`grep Host: $HOME/.mana | sed -e 's%Host: %%'|sed -e 's% .*$%%'`
submissionPort=`grep Port: $HOME/.mana | sed -e 's%Port: %%'|sed -e 's% .*$%%'`

coordinator_found=0
$dir/dmtcp_command -s -h $submissionHost -p $submissionPort 1>/dev/null \
                                                     && coordinator_found=1
if [ "$coordinator_found" == 0 ]; then
  echo "*** Checking for coordinator:"
  set -x
    # `dirname $0`/dmtcp_command -s -h `hostname`
    $dir/dmtcp_command --status --coord-host $submissionHost \
                                --coord-port $submissionPort
  set +x
  echo "  No coordinator detected.   Try:"
  echo "    $dir/mana_coordinator"
  echo "  Or:"
  echo "    $dir/dmtcp_coordinator --mpi --exit-on-last -q --daemon"
  exit 5
fi

if [ "$verbose" == 0 ]; then
  options="-q -q $options"
fi

plugindir="`dirname $0`/.."

# if [ -z "$SLURM_NTASKS" ]; then
#   echo ""
#   echo "*******************************************************"
#   echo "* SLURM_NTASKS env. var. not detected.                *"
#   echo "* Did you forget to run mana_launch with srun/sbatch? *"
#   echo "*******************************************************"
#   echo ""
# fi

# FIXME: Should we detect if the MANA job was not linked with libmpidummy.so
#        and point the user toward dmtcp_launch/dmtcp_restart?
#        Since mana_launch includes the MANA plugin, it's not for non-MPI jobs.
# if ldd "$target" | grep -q libmpidummy.so; then
# fi

if [ "$verbose" == 1 ]; then
  set -x
fi

# TEMPORARY WORKAROUND:  set MPICH_SMP_SINGLE_COPY_OFF=1
#   In VASP5 with RPA, it failed on restart, trying to copy to a
#   /SYSV0* file (created by shmat or XPMEM), but mpi_plugin.cpp says that
#   /SYSV0* is not saved at ckpt, and so its data is not saved and restored.
#   NOTE: The upper half should not know about this file. Why do we
#         need to restore pre-ckpt data on a new MPICH in the lower half? 
#         (We could create xpmam plugin for DMTCP, but it shouldn't be needed.)

env MPICH_SMP_SINGLE_COPY_OFF=1 \
  $dir/dmtcp_launch  --coord-host $submissionHost \
          --coord-port $submissionPort --no-gzip \
          --join-coordinator --disable-dl-plugin \
          --with-plugin $plugindir/lib/dmtcp/libmana.so $options

# srun -n1 -c1 --cpu-bind=cores bin/dmtcp_launch  -i10 -h `hostname` --no-gzip --join --disable-dl-plugin --with-plugin $PWD/lib/dmtcp/libmana.so contrib/mpi-proxy-split/test/mpi_hello_world.mana.exe

# srun -n1 -c1 --cpu-bind=cores bin/mana_launch  -i10 contrib/mpi-proxy-split/test/mpi_hello_world.mana.exe
