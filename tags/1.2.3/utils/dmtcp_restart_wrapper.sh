#!/bin/bash

usage_str='USAGE:
  dmtcp_restart_wrapper.sh [OPTIONS] restart_script_path

OPTIONS:
  --sleep-interval,-i:
      Time in seconds to sleep before checking process status
  --min-seconds,-s:
      Min CPU seconds consumed by the process. 
      If after --sleep-interval seconds, the process has not consumed 
        --min-seconds of CPU time, then it is killed and restarted.
  --max-attempts,-a:
      Max number of attempts to make before trying older ckpt image.
  --start-id:
      Specify the first ckpt-image-suffix "n" to try restarting from. If the
      restart fails for this image, then the script will try to restart with
      ckpt-image with suffix n-1.
  --help,-h:
      Print this message'

sleep_interval=15
min_seconds=2
max_attempts=100
start_suffix= 

if [ $# -gt 0 ]; then
  while [ $# -gt 0 ]
  do
    case "$1" in 
      --help|-h)
        echo "$usage_str"
        exit;;
      --sleep-interval|-i)
        sleep_interval="$2"
        echo "Sleep interval specified: $sleep_interval seconds"
        shift
        shift;;
      --min-seconds|-s)
        min_seconds="$2"
        echo "Min seconds specified: $min_seconds seconds"
        shift
        shift;;
      --max-attempts|-a)
        max_attempts="$2"
        echo "Max attempts specified: $max_attempts"
        shift
        shift;;
      --start-id)
        start_suffix="$2"
        echo "Starting suffix specified: $start_suffix"
        shift
        shift;;
      *) 
        break;;
    esac
  done
fi


if [ $# -eq 0 ]; then
  restart_script="`pwd`/dmtcp_restart_script.sh"
  restart_script_args=
else
  restart_script=$1
  shift;
  restart_script_args="$@"
fi
if [ ! -f $restart_script ]; then
  echo "dmtcp_restart_wrapper: ERROR: file $restart_script not found"
  echo "Exiting..."
  exit
fi

new_restart_script=${restart_script%.sh}_new.sh

# compute tmp dir. The functionality is equivalent to UniquePid::getTimDir()
tmp_dir=
if [ ! -z $DMTCP_TMPDIR ]; then
  tmp_dir=$DMTCP_TMPDIR
elif [ ! -z $TMPDIR ]; then
  tmp_dir=$TMPDIR/dmtcp-$USER'@'`hostname`
else
  tmp_dir=/tmp/dmtcp-$USER'@'`hostname`
fi
echo $tmp_dir
mkdir -p $tmp_dir

restart_script_path=`readlink $restart_script`
if [ -z $restart_script_path ]; then
  restart_script_path=$restart_script
fi
if [ ! -f $restart_script_path ]; then
  echo "dmtcp_restart_wrapper: ERROR: $restart_script_path is not a valid file"
  echo "Exiting..."
  exit
fi

restart_script_name=`basename $restart_script_path .sh`
comp_group=${restart_script_name#dmtcp_restart_script_}
signature_file_path=$tmp_dir/"$comp_group-$$"

ckpt_file_path=`grep ".*\.dmtcp" $restart_script_path|head -1|tr -d '\r\n\t '`
new_ckpt_file_path=$ckpt_file_path

current_attempt=0
current_suffix=$start_suffix

while true
do
  rm -rf $signature_file_path
  current_attempt=$((current_attempt+1))
  echo "File: $new_ckpt_file_path; Attempt: $current_attempt"

  if [ $current_attempt -gt $max_attempts ]; then
    current_attempt=1
    current_suffix=$((current_suffix - 1))
    echo "*****Now trying suffix $current_suffix*********"
  fi

  if [ ! -z $start_suffix ]; then
    if [ $current_suffix -eq 0 ];then
      echo "dmtcp_restart_wrapper: tried to restart all the images, none succeeded"
      echo "Exiting..."
      exit
    fi
    #compute new suffix
    suffix=`printf "%05d" $current_suffix`
    new_ckpt_file_path=`echo $ckpt_file_path|sed -e's^_[0-9]*\.dmtcp^_'$suffix'\.dmtcp^g'`

    # create a new restart script by replacing the original checkpoint file
    # path with the new one
    cat $restart_script_path |sed 's^'$ckpt_file_path'^'$new_ckpt_file_path'^g' > $new_restart_script
  else
    new_restart_script=$restart_script_path
  fi

  #verify that the target checkpoint file exists
  if [ ! -f $new_ckpt_file_path ]; then
    echo "dmtcp_restart_wrapper: ERROR: checkpoint file $new_ckpt_file_path not found."
    echo "Exiting..."
    exit
  fi

  # start the dmtcp_restart_script in background
  if [ -z $restart_script_args ];then
    echo "launching: /bin/bash $new_restart_script &"
    /bin/bash $new_restart_script &
  else
    echo "launching: /bin/bash $new_restart_script $restart_script_args"
    /bin/bash $new_restart_script $restart_script_args &
  fi

  cpid=$!

  echo "Sleeping for $sleep_interval seconds"
  sleep $sleep_interval

  # the following command gets the cpu usage in MMM:SS format
  cpu_usage=`ps -o bsdtime= -p $cpid | tr -d ' '`
  if [ -z $cpu_usage ]; then
    echo "dmtcp_restart_wrapper: process already dead, trying to restart"
    continue
  fi

  cpu_usage_total_sec=$(( ${cpu_usage%:*} * 60 + ${cpu_usage#*:} ))
  #cpu_usage_total_sec=$(( $cpu_usage_minutes * 60 + $cpu_usage_seconds ))

#   if [ $cpu_usage_total_sec -lt $min_seconds ];then
#     echo "dmtcp_restart_wrapper: process running but not consuming any cpu cycles\n"
#     echo "  total cpu_usage in seconds: $cpu_usage_total_sec\n"
#     echo "Killing it and trying to restart**********\n\n"
# 
#     kill -9 $cpid 
# 
#     continue
#   fi

  if [ ! -f $signature_file_path ]; then
    echo "dmtcp_restart_wrapper: Signature file not found even though the process"
    echo "  has consumed $cpu_usage_total_sec seconds of CPU time. This is strange!"
    echo "Killing and restarting it and hoping for the best"
    kill -9 $cpid
    wait $cpid
    continue
  else
    file_contents=`cat $signature_file_path`
    if [ $cpid != $file_contents ]; then
      echo "dmtcp_restart_wrapper: Invalid contents in signature file: $signature_file_path"
      echo "  actual:   $file_contents"
      echo "  expected: $cpid"
      echo "Restarting process and hoping for the best"
      kill -9 $cpid
      wait $cpid
      continue
    fi

    if [ $cpu_usage_total_sec -lt $min_seconds ];then
      echo "dmtcp_restart_wrapper: process running but not consuming any cpu cycles\n"
      echo "  total cpu_usage in seconds: $cpu_usage_total_sec\n"
      echo "Killing it and trying to restart**********\n\n"

      kill -9 $cpid 
      wait $cpid
      continue
    fi

    wait $cpid
    echo "dmtcp_restart_wrapper: process finished successfully"
    break
  fi
done
