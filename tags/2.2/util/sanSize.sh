#!/bin/sh
#this script tells the total size of all the ckpt files on teracluster
#it takes no args and expects a file 'hosts' to contain a list of nodes

a=0
for X in 0 1 2 3 4 5 6 7  
do
  for Y in `ls -l /san/global_$X/$USER | tr -s ' ' | cut -f 5 -d ' '` 
  do
    a=`expr $a + $Y` 
  done
done
a=`echo "scale=2; $a/$[1024*1024]" | bc`
#a=`expr $a / $[ 1024 * 1024 ]`
echo "Total Size : $a MB"
#echo "$@, $a" >> ckpt_sizes
wait


