#!/bin/sh
#this script was used to free up space after testing on teracluster
#it also wipes out debugging logs
#it takes no args and expects a file 'hosts' to contain a list of nodes

for X in localhost `cat < hosts`
do
  echo "Clearing $X..."
  ssh "$X" 'rm -f \
    /tmp/dmtcpConTable.* \
    /tmp/mpd2.* \
    /tmp/dmtcp-*/*.log \
    /dev/shm/ckpt*mtcp \
    ~/san/ckpt*mtcp \
    /tmp/ckpt*mtcp \
    /tmp/pts*'
done
wait
