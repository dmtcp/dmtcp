#!/bin/bash

. patch_env.sh

which mpicc
mpicc -o md_pc_mpi -g md_pc_mpi.c