#!/bin/sh
#this script can be used to kill all user processes running on the remote nodes.
#it takes no args and expects a file 'hosts' to contain a list of nodes

for X in `cat < hosts`
do
  echo "Clearing $X..."
  ssh $X kill -9 -1 & 
done

wait


