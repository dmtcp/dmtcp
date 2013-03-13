#!/bin/sh
#this script was used to free up space after testing on teracluster
#it also wipes out debugging logs
#it takes no args and expects a file 'hosts' to contain a list of nodes

for X in localhost `cat < hosts`
do
#  echo "Clearing $X..."
  ssh $X ls -l /tmp |grep ckpt_ | grep $USER 
done

wait


