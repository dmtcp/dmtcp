#!/bin/sh
#this script list the processes owned by user on all nodes of teracluster 
#it takes no args and expects a file 'hosts' to contain a list of nodes

for X in localhost `cat < hosts`
do
  echo "Listing Processes from $X..."
  ssh $X "ps aux |grep vsrid| grep -v YACCO | (while read Y; do echo $X \$Y; done)" &
  #ssh $X "ps aux |grep $USER| grep -v YACCO | (while read Y; do echo $X \$Y; done)" &
done

wait


