#!/bin/bash

# MPICH2 settings
MPICH2_PATH="/usr/mpi/gcc/mvapich2-1.9"
export PATH="$MPICH2_PATH/bin/:$PATH"
export LD_LIBRARY_PATH="$MPICH2_PATH/lib:$LD_LIBRARY_PATH"
