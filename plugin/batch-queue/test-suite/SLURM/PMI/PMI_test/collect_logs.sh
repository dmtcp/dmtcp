#!/bin/bash


if [ "$SLURM_LOCALID" -eq "0" ]; then
  if [ -d ./LOGS ]; then
    cp -R $SLURMTMPDIR/dmtcp* ./LOGS/
  fi
fi
