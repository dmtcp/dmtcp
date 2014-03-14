#!/bin/bash

. patch_env.sh

which mpicc
mpicc -o mc_int -g mc_int.c