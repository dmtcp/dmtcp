#!/bin/sh

# if [ -n "$1" ]; then
#   echo "USAGE:  $0 [--verbose] [DMTCP_OPTIONS ...]"
#   echo "        For other DMTCP coordinator options, do:"
#   echo "          $0 --help"
#   exit 1
# fi

mana_coord=$0
dir=`dirname $0`

options=""
verbose=0
help=0
while [ -n "$1" ]; do
  if [ "$1" == --verbose ]; then
    verbose=1
  elif [ "$1" == --help ]; then
    help=1
  else
    options="$options $1"
  fi
  shift
done

if [ "$help" -eq 1 ]; then
  $dir/dmtcp_coordinator --help $options
  exit 0
fi

if [ "$NERSC_HOST" = "cori" ]; then
  if [ -z "$SLURM_JOB_ID" ]; then
    echo "SLURM_JOB_ID env variable not set; No salloc/sbatch jobs running?"
    echo "For help, do:  $mana_coord --help"
    exit 2
  fi
fi

if [ "$verbose" == 0 ]; then
  options="$options -q -q"
fi

if [ "$verbose" == 1 ]; then
  set -x
fi

coordinator_found=0
$dir/dmtcp_coordinator $options --mpi --exit-on-last -q --daemon && coordinator_found=1
set +x
if [ $coordinator_found -eq 0 ]; then
  exit 3
fi

if [ "$NERSC_HOST" = "cori" ]; then
  if [ -e $HOME/.mana ]; then
    echo "SLURM_JOB_ID: $SLURM_JOB_ID" >> $HOME/.mana
  fi
fi
echo '*** '"Coordinator/job information written to $HOME/.mana"

# srun -n1 -c1 --cpu-bind=cores bin/dmtcp_launch  -i10 -h `hostname` --no-gzip --join --disable-dl-plugin --with-plugin $PWD/lib/dmtcp/libmana.so contrib/mpi-proxy-split/test/mpi_hello_world.mana.exe

# srun -n1 -c1 --cpu-bind=cores bin/mana_launch  -i10 contrib/mpi-proxy-split/test/mpi_hello_world.mana.exe
