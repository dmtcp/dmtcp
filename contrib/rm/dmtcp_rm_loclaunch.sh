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
  if [ $SLURM_LOCALID = 0 ]; then
    dmtcp_restart --join --host $DMTCP_HOST $LOCAL_FILES > `hostname`.dmtcp
    if [ -d ./LOGS ]; then
      cp -R /tmp/* ./LOGS/
    fi
  fi
elif [ "$PBS_ENVIRONMENT" = PBS_BATCH ] && [ -n "$PBS_JOBID" ]; then
  cd $PBS_O_WORKDIR
  ID=$PBS_NODENUM
  if [ -z "$ID" ]; then
    # something goes wrong. Shouldn't happen
    echo "Cannot determine TORQUE_NODENUM. Exit."
    set
    exit 0
  fi

  if [ -z "$1" ]; then
    echo "$0: Not enough parameters: $@. Exit."
    exit 0
  fi  
  eval "$1"

  # Determine total number of nodes
  IDS=$DMTCP_REMLAUNCH_IDS
  if [ -z "$IDS" ] || [ "$ID" -ge "$IDS" ]; then
    # something goes wrong. Shouldn't happen
    echo "No DMTCP environment or bad ID values: ID=$ID, IDS=$IDS. Exit."
    set
    exit 0
  fi
  
  eval "LOCAL_FILES=\${DMTCP_REMLAUNCH_$ID}"
  dmtcp_restart --join --host $DMTCP_HOST $LOCAL_FILES > `hostname`.dmtcp
  if [ -d ./LOGS ]; then
    cp -R /tmp/dmtcp* ./LOGS/
  fi

fi