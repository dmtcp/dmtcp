#!/bin/bash

sleep_interval=15
if [ $1 = "--sleep-interval" ]; then
  sleep_interval="$2"
  echo "Sleep interval specified: $sleep_interval seconds"
  shift
  shift
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

while true
do
  rm -rf $file_path

  if [ "$#" == 0 ];then
    /bin/bash $file &
  else
    /bin/bash "$@" &
  fi

  cpid=$!

  echo "Sleeping for $sleep_interval seconds"
  sleep $sleep_interval

  if [ -f $file_path ]; then
    child_pid=`cat $file_path`
    if [ $cpid != $child_pid ]; then
      echo "invalid contents: contents of $file_path: \"`cat $file_path`\"; expected: %cpid"
      exit
    fi
    wait $cpid
    break
  else
    echo "process died, restarting it"
    kill -9 $cpid
    wait $cpid
  fi
done
