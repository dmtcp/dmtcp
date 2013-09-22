#!/bin/bash


if [ -n "$SLURM_JOBID" ] || [ -n "$SLURM_JOB_ID" ]; then
  ID=$SLURM_NODEID
  if [ -z "$ID" ]; then
    # something goes wrong. Shouldn't happen
    echo "Cannot determine SLURM_NODEID. Exit."
    set
    exit 0
  fi
  
  # Determine total number of nodes
  IDS=$DMTCP_REMLAUNCH_IDS
  if [ -z "$IDS" ] || [ "$ID" -ge "$IDS" ]; then
    # something goes wrong. Shouldn't happen
    echo "No DMTCP environment or bad ID values: ID=$ID, IDS=$IDS. Exit."
    set
    exit 0
  fi
  
  eval "LOCAL_FILES=\${DMTCP_REMLAUNCH_$ID}"
  
  set >> `hostname`.set
  if [ $SLURM_LOCALID = 0 ]; then
    echo "dmtcp_restart --join --host $DMTCP_HOST $LOCAL_FILES" > `hostname`.out
    which dmtcp_restart >> `hostname`.out
    dmtcp_restart --join --host $DMTCP_HOST $LOCAL_FILES > `hostname`.dmtcp
    echo "after dmtcp_restart" >> `hostname`.out
    cp -R /tmp/* ./LOGS/
  fi

  

fi