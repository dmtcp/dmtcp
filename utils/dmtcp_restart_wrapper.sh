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
start_suffix=0

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


if [ "$#" == 0 ]; then
  file="`pwd`/dmtcp_restart_script.sh"
else
  file=$1
fi

restart_file_path=`readlink $file`
restart_file_name=`basename $restart_file_path .sh`
comp_group=${restart_file_name#dmtcp_restart_script_}

file_name="$comp_group-$$"

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

file_path=$tmp_dir/$file_name

current_attempt=0
current_suffix=$start_suffix
new_filename=$file

while true
do
  rm -rf $file_path

  if [ $current_attempt -eq $max_attempts ]; then
    $current_attempt=0
    $current_suffix=$((current_suffix - 1))
    echo "*****Now trying suffix $current_suffix*********"
  fi

  if [ $start_suffix -ne 0 ]; then
    if [ $current_suffix -eq 0 ];then
      echo "dmtcp_restart_wrapper: done trying to restart all the images, none succeeded"
      exit
    fi
    s=`printf "%05d" $current_suffix`
    new_filename=${file%.sh}_new.sh
    cat $file |sed 's/_'$comp_group'\(_[0-9]*\)\.dmtcp/'$comp_group'_'$s'\.dmtcp/g' > $new_filename 
  fi


  if [ "$#" == 0 ];then
    /bin/bash $new_filename &
  else
    shift
    /bin/bash $new_filename "$@" &
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
  cpu_usage_minutes=${cpu_usage%:*}
  cpu_usage_seconds=${cpu_usage#*:}
  cpu_usage_total_sec=$(( $cpu_usage_minutes * 60 + $cpu_usage_seconds ))

#   if [ $cpu_usage_total_sec -lt $min_seconds ];then
#     echo "dmtcp_restart_wrapper: process running but not consuming any cpu cycles\n"
#     echo "  total cpu_usage in seconds: $cpu_usage_total_sec\n"
#     echo "Killing it and trying to restart**********\n\n"
# 
#     kill -9 $cpid 
# 
#     continue
#   fi

  if [ ! -f $file_path ]; then
    echo "dmtcp_restart_wrapper: Signature file not found even though the process"
    echo "  has consumed $cpu_usage_total_seconds seconds of CPU time. This is strange!"
    echo "Killing and restarting it and hoping for the best"
    kill -9 $cpid
    wait $cpid
  else
    file_contents=`cat $file_path`
    if [ $cpid != $file_contents ]; then
      echo "dmtcp_restart_wrapper: Invalid contents in signature file: $file_path"
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

      continue
    fi

    wait $cpid
    break
  fi
done
