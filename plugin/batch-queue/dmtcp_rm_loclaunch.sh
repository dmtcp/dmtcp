#!/bin/bash
# ****************************************************************************
# *  Copyright (C) 2012-2014 by Artem Y. Polyakov <artpol84@gmail.com>       *
# *                                                                          *
# *  This file is part of the RM plugin for DMTCP                            *
# *                                                                          *
# *  RM plugin is free software: you can redistribute it and/or              *
# *  modify it under the terms of the GNU Lesser General Public License as   *
# *  published by the Free Software Foundation, either version 3 of the      *
# *  License, or (at your option) any later version.                         *
# *                                                                          *
# *  RM plugin is distributed in the hope that it will be useful,            *
# *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
# *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
# *  GNU Lesser General Public License for more details.                     *
# *                                                                          *
# *  You should have received a copy of the GNU Lesser General Public        *
# *  License along with DMTCP:dmtcp/src.  If not, see                        *
# *  <http://www.gnu.org/licenses/>.                                         *
# ****************************************************************************/

prepare_SLURM_env()
{
  LOCAL_FILES="$1"
  
  # Create temp directory if need
  if [ -n "$DMTCP_TMPDIR" ]; then
    CURRENT_TMPDIR=$DMTCP_TMPDIR/dmtcp-`whoami`@`hostname`
  elif [ -n "$TMPDIR" ]; then
    CURRENT_TMPDIR=$TMPDIR/dmtcp-`whoami`@`hostname`
  else
    CURRENT_TMPDIR=/tmp/dmtcp-`whoami`@`hostname`
  fi
  if [ ! -d "$CURRENT_TMPDIR" ]; then
    mkdir -p $CURRENT_TMPDIR
  fi

  # Create files with SLURM environment
  for CKPT_FILE in $LOCAL_FILES; do
    SUFFIX=${CKPT_FILE%%.dmtcp}
    SLURM_ENV_FILE=$CURRENT_TMPDIR/slurm_env_${SUFFIX##*_}
    echo "SLURM_SRUN_COMM_HOST=$SLURM_SRUN_COMM_HOST" > $SLURM_ENV_FILE
    echo "SLURM_SRUN_COMM_PORT=$SLURM_SRUN_COMM_PORT" >> $SLURM_ENV_FILE
    echo "SLURMTMPDIR=$SLURMTMPDIR" >> $SLURM_ENV_FILE
  done
}

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

  prepare_SLURM_env "$LOCAL_FILES"

  dmtcp_restart --join --coord-host $DMTCP_COORD_HOST \
                       --coord-port $DMTCP_COORD_PORT $LOCAL_FILES

  # set > set.$SLURM_NODEID.$SLURM_LOCALID
  # Accumulate logs from computing nodes
  if [ -d ./LOGS ] && [ ${SLURM_LOCALID} -eq "0" ]; then
    TDIR="$SLURMTMPDIR"
    if [ -z "$TDIR" ]; then
        TDIR=$TMPDIR
    fi
    #echo "TMPDIR=$TDIR"
    if [ -n "$TDIR" ]; then
        cp -R $TDIR/dmtcp* ./LOGS/
        rm -R $TDIR/dmtcp*
    fi
  fi


elif [ "$PBS_ENVIRONMENT" = PBS_BATCH ] && [ -n "$PBS_JOBID" ]; then
  cd $PBS_O_WORKDIR
  NODE=$PBS_NODENUM
  if [ -z "$NODE" ]; then
    # something goes wrong. Shouldn't happen
    echo "Cannot determine number of this node PBS_NODENUM=$PBS_NODENUM. Exit."
    set
    exit 0
  fi

  if [ -z "$1" ]; then
    echo "$0: Not enough parameters: $@. Exit."
    exit 0
  fi  
  eval "$1"

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

  MAX_SLOT=`expr "$LOCAL_SLOTS" - 1`
  LOCAL_FILES=""
  for slot in `seq 0 $MAX_SLOT`; do
    eval "LOCAL_FILES_TMP=\$DMTCP_REMLAUNCH_${NODE}_${slot}"
    LOCAL_FILES=$LOCAL_FILES" "$LOCAL_FILES_TMP
    unset LOCAL_FILES_TMP
  done
  
  if [ -z "$LOCAL_FILES" ]; then
    echo "`hostname`: Bad LOCAL_FILES variable DMTCP_REMLAUNCH_${NODE}_${SLURM_LOCALID}"
    set
    exit 0
  fi

  #echo "LOCAL_FILES=$LOCAL_FILES"
  dmtcp_restart --join --coord-host $DMTCP_COORD_HOST \
                       --coord-port $DMTCP_COORD_PORT $LOCAL_FILES
  if [ -d ./LOGS ]; then
    cp -R /tmp/dmtcp* ./LOGS/
  fi

fi
