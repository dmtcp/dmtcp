#!/bin/bash

  if [ -d ./LOGS ] && [ ${SLURM_LOCALID} -eq "0" ]; then
    TDIR="$SLURMTMPDIR"
    if [ -z "$TDIR" ]; then
        TDIR=$TMPDIR
    fi
    echo "TMPDIR=$TDIR"
    if [ -n "$TDIR" ]; then
        cp -R $TDIR/dmtcp* ./LOGS/
        rm -R $TDIR/dmtcp*
    fi
  fi
