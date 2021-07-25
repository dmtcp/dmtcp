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

if [ "$1" == "--help" -o "$1" == -h ] && [ -z "$2" ]; then
  echo "USAGE: mana_restart [--verbose] [DMTCP_OPTIONS ...]" \
                                                "[--restartdir MANA_CKPT_DIR]"
  echo "       Default for MANA_CKPT_DIR is current directory"
  echo "       For DMTCP options, do: $0 --help --help"
  echo " MANA_CKPT_DIR should contain the checkpoint subdirs: ckpt_rank_*"
  exit 1
fi

dir=`dirname $0`
host=`hostname`
submissionHost=`grep Host: $HOME/.mana | sed -e 's%Host: %%'|sed -e 's% .*$%%'`
submissionPort=`grep Port: $HOME/.mana | sed -e 's%Port: %%'|sed -e 's% .*$%%'`

options=""
restartdir=""
verbose=0
help=0
while [ -n "$1" ]; do
  if [ "$1" == --verbose ]; then
    verbose=1
  elif [ "$1" == --help ]; then
    help=1
  elif [ "$1" == --restartdir ]; then
    restartdir="$2"
    if [ ! -d "$restartdir" ]; then
      echo "$0: --restartdir $restartdir: Restart directory doesn't exist"
      exit 9
    fi
    options="$options $1"
  else
    options="$options $1"
  fi
  shift
done

if [ "$help" -eq 1 ]; then
  $dir/dmtcp_restart --help $options
  exit 0
fi

if [ "$restartdir" == "" ]; then
  options="$options --restartdir ./"
fi

if [ "$NERSC_HOST" = "cori" ]; then
  if [ -z "$SLURM_JOB_ID" ]; then
    echo "SLURM_JOB_ID env variable not set; No salloc/sbatch jobs running?"
    exit 2
  fi
fi


coordinator_found=0
$dir/dmtcp_command -s -h $submissionHost -p $submissionPort 1>/dev/null \
                                                        && coordinator_found=1
if [ "$coordinator_found" == 0 ]; then
  echo "*** Checking for coordinator:"
  set -x
    $dir/dmtcp_command --status --coord-host $submissionHost \
                                --coord-port $submissionPort
  set +x
  echo "  No coordinator detected.   Try:"
  echo "    $dir/mana_coordinator"
  echo "  Or:"
  echo "    $dir/dmtcp_coordinator --mpi --exit-on-last -q --daemon"
  exit 3
fi

if [ "$NERSC_HOST" = "cori" ]; then
  if [ -z "$SLURM_NTASKS" ]; then
    echo ""
    echo "********************************************************"
    echo "* SLURM_NTASKS env. var. not detected.                 *"
    echo "* Did you forget to run mana_restart with srun/sbatch? *"
    echo "********************************************************"
    echo ""
  fi
elif [ -z "$MPI_LOCALNRANKS" ]; then
  echo ""
  echo "**********************************************************"
  echo "* MPI_LOCALNRANKS env. var. not detected.                *"
  echo "* Did you forget to run mana_restart with mpirun -np XX? *"
  echo "**********************************************************"
  echo ""
fi

# If not verbose, use --quiet option
if [ "$verbose" == 0 ]; then
  # $options must be last; It usually includes the directory for ckpt files.
  options="-q -q $options"
fi

if [ "$verbose" == 1 ]; then
  set -x
fi

$dir/dmtcp_restart  --mpi --join-coordinator \
                    --coord-host $submissionHost --coord-port $submissionPort \
                    $options

#  ~/new_mana/mana/bin/dmtcp_restart --mpi -j -h `hostname`  --restartdir ./
#  srun -N 2 mana_restart ./
