#!/bin/bash

export CFLAGS="$CFLAGS -I/opt/slurm-2.6.5/include/slurm/ "
export LDFLAGS="$LDFLAGS -L/opt/slurm-2.6.5/lib/ -lpmi "
./configure --prefix=/home/research/artpol/openmpi_build --with-slurm --with-pmi
