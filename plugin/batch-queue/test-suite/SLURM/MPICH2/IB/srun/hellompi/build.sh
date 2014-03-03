#!/bin/bash

. patch_env.sh

which mpicc
mpicc -o hellompi -I$SLURM_PMI_INC -L$SLURM_PMI_LIB -lpmi -g hellompi.c