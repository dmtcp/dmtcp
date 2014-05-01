#!/bin/bash

if [ -z "$1" ]; then
    echo " Not enough params. Wand process PID"
fi

cat /proc/$1/maps | awk '{ if( $6 == "" ) print "anonym "$1; else print $6" "$1 }' | awk '{ split($2,array,"-"); print $1" 0x"array[2]" 0x"array[1] }' \
        | awk --non-decimal-data '{ print $1": "($2 - $3) }'