#!/bin/bash

# OpenMPI
OMPI_PATH="/home/research/artpol/openmpi_build"
export PATH="$OMPI_PATH/bin/:$PATH"
export LD_LIBRARY_PATH="$OMPI_PATH/lib:$LD_LIBRARY_PATH"
