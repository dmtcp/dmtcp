#!/bin/bash


if [ -n "$SLURM_JOBID" ] || [ -n "$SLURM_JOB_ID" ]; then
  NODE=$SLURM_NODEID
  if [ -z "$NODE" ]; then
    # something goes wrong. Shouldn't happen
    echo "Cannot determine SLURM_NODEID. Exit."
    set
    exit 0
  fi
  
  # Determine total number of nodes
  NODES=$DMTCP_REMLAUNCH_NODES
  if [ -z "$NODES" ] || [ "$NODE" -ge "$NODES" ]; then
    # something goes wrong. Shouldn't happen
    echo "No DMTCP environment or bad ID values: ID=$NODE, IDS=$NODES. Exit."
    set
    exit 0
  fi
  
  eval "LOCAL_SLOTS=\${DMTCP_REMLAUNCH_${NODE}_SLOTS}"
  if [ "${LOCAL_SLOTS}" = 0 ] || [ -z "${LOCAL_SLOTS}" ]; then
    echo "`hostname`: nothing to launch \${DMTCP_REMLAUNCH_${NODE}_SLOTS} = ${LOCAL_SLOTS}"
    set
    exit 0
  fi

  if [ "$SLURM_LOCALID" -ge $LOCAL_SLOTS ]; then
    echo "`hostname`: Will not use SLURM_LOCALID=$SLURM_LOCALID for launch, max is $LOCAL_SLOTS"
    exit 0
  fi

  eval "LOCAL_FILES=\$DMTCP_REMLAUNCH_${NODE}_${SLURM_LOCALID}"
  if [ -z "$LOCAL_FILES" ]; then
    echo "`hostname`: Bad LOCAL_FILES variable DMTCP_REMLAUNCH_${NODE}_${SLURM_LOCALID}"
    set
    exit 0
  fi

  dmtcp_restart --join --host $DMTCP_HOST --port $DMTCP_PORT $LOCAL_FILES
  if [ -d ./LOGS ]; then
    cp -R $SLURMTMPDIR/dmtcp* ./LOGS/
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
  dmtcp_restart --join --host $DMTCP_HOST --port $DMTCP_PORT $LOCAL_FILES
  if [ -d ./LOGS ]; then
    cp -R /tmp/dmtcp* ./LOGS/
  fi

fi
