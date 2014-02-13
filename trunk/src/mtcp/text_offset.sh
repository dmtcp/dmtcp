#!/bin/sh

if test "$1" = ""; then
  echo Usage:  $0 BINARY_FILE
  exit 1
fi

if test `uname -m` = x86_64; then
  echo 0x`readelf -S $1 | grep '\.text' | sed -e 's^.* 0*^^'`
else
  echo 0x`readelf -S $1 | grep '\.text' | \
             sed -e 's^.*PROGBITS * [0-9]* *0*^^' | sed -e 's^ .*^^'`
fi
