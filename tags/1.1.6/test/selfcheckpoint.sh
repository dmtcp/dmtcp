#!/bin/sh

# make some heavy bash computations.
TWO=$((1+1))

# freeze itself
sleep 1
dmtcp_nocheckpoint dmtcp_command -bc
sleep 1

# print the result of the heavy computation on restart.
echo $TWO

