#!/bin/bash

export PATH="/user/artempol/openmpi-build/bin/":$PATH
export LD_LIBRARY_PATH="/user/artempol/openmpi-build/lib/":$LD_LIBRARY_PATH

echo `which mpicc`" -o hellompi -g hellompi.c"
`which mpicc` -o hellompi -g -lpmi -L./lib/ hellompi.c

g++ -fPIC -g -c hijack.cpp
g++ -shared -g -o libpmihj.so hijack.o