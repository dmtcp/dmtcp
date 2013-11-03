#!/bin/sh
#this script was used to initialize a ring of MPDs on teracluster
#the mpd started scripts were too annoying
#it expects a file 'hosts' to list nodes in the cluster
#it takes one arg, the number of remote mpd's to start


set -m

MPD="dmtcp_launch $HOME/mpich2/bin/mpd "
HOST="teracluster"
PORT="7778"

if test -z "$1"
then
 echo "usage $0 N"
 exit 1
fi

#start local
$MPD --listenport=7778 &
sleep 1

for X in `head -n $1 < hosts`
do
  echo "Starting $X..."
  ssh $X "$MPD --host=$HOST --port=$PORT </dev/null >/dev/null 2>/dev/null"&
done

wait


