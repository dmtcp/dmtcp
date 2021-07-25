#!/bin/sh

# FIXME: USER FORGOT TO DO 'salloc' (detect and report error?):
# gdc0@cori11:~/mana-rohgarg-orig> srun -N 2 bin/mana_launch --verbose contrib/mpi-proxy-split/test/ping_pong.mana.exe
# srun: error: No architecture specified, cannot estimate job costs.
# srun: error: Unable to allocate resources: Unspecified error


# if [ -z "$1" ]; then
#   echo "USAGE:  $0 [--verbose] [DMTCP_OPTIONS ...]"
#   echo "        For DMTCP options, do: $0 --verbose --help"
#   exit 1
# fi

dir=`dirname $0`
host=`hostname`
submissionHost=`grep Host: $HOME/.mana | sed -e 's%Host: %%'|sed -e 's% .*$%%'`
submissionPort=`grep Port: $HOME/.mana | sed -e 's%Port: %%'|sed -e 's% .*$%%'`

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
  $dir/dmtcp_command --help $options
  exit 0
fi

coordinator_found=0
$dir/dmtcp_command -s -h $submissionHost -p $submissionPort 1>/dev/null \
                   && coordinator_found=1
if [ "$coordinator_found" == 0 ]; then
  echo "*** Checking for coordinator:"
  set -x
    $dir/dmtcp_command --status \
                --coord-host $submissionHost --coord-port $submissionPort
  set +x
  echo "  No coordinator detected.   Try:"
  echo "    $dir/mana_coordinator"
  echo "  Or:"
  echo "    $dir/dmtcp_coordinator --mpi --exit-on-last -q --daemon"
  echo "  For help, do:  $dir/mana_command --help"
  exit 2
fi

if [ "$verbose" == 1 ]; then set -x; fi
$dir/dmtcp_command -h $submissionHost -p $submissionPort --list $options
set +x
