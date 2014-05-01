#!/bin/bash

if [ -z "$1" ]; then
    echo " Not enough params. Wand process PID"
fi

cat /proc/$1/maps | awk '{ print $1 }' | awk '{ split($0,array,"-"); print "0x"array[2]" 0x"array[1] }' \
        | awk --non-decimal-data '{ print ($1 - $2) }'