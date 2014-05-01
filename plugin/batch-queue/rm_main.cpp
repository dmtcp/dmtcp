/****************************************************************************
 *  Copyright (C) 2012-2014 by Artem Y. Polyakov <artpol84@gmail.com>       *
 *                                                                          *
 *  This file is part of the RM plugin for DMTCP                            *
 *                                                                          *
 *  RM plugin is free software: you can redistribute it and/or              *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  RM plugin is distributed in the hope that it will be useful,            *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "dmtcpalloc.h"
#include "util.h"
#include  "../jalib/jassert.h"
#include "rm_main.h"
#include "rm_torque.h"
#include "rm_slurm.h"
#include "rm_pmi.h"

extern "C" int dmtcp_batch_queue_enabled(void) { return 1; }

void dmtcp_event_hook(DmtcpEvent_t event, DmtcpEventData_t* data)
{
  JTRACE("Start");

  switch (event) {
  case DMTCP_EVENT_THREADS_SUSPEND:
    JTRACE("DMTCP_EVENT_THREADS_SUSPEND");
    runUnderRMgr();
    rm_shutdown_pmi();
    break;
  case DMTCP_EVENT_THREADS_RESUME:
    JTRACE("DMTCP_EVENT_THREADS_RESUME");
    rm_restore_pmi();
    break;
  case DMTCP_EVENT_RESTART:
    JTRACE("DMTCP_EVENT_RESTART")(_get_rmgr_type());
    if ( _get_rmgr_type() == slurm ){
	JTRACE("Call restore_env()");
      slurm_restore_env();
    }
    break;
  default:
    break;
  }

  DMTCP_NEXT_EVENT_HOOK(event, data);
}


// ----------------- global data ------------------------//
static rmgr_type_t rmgr_type = Empty;

// TODO: Do we need locking here?
//static pthread_mutex_t global_mutex = PTHREAD_MUTEX_INITIALIZER;

rmgr_type_t _get_rmgr_type()
{
  // TODO: Do we need locking here?
  //JASSERT(_real_pthread_mutex_lock(&global_mutex) == 0);
  rmgr_type_t loc_rmgr_type = rmgr_type;
  // TODO: Do we need locking here?
  //JASSERT(_real_pthread_mutex_unlock(&global_mutex) == 0);
  return loc_rmgr_type;
}

void _set_rmgr_type(rmgr_type_t nval)
{
  // TODO: Do we need locking here?
  //JASSERT(_real_pthread_mutex_lock(&global_mutex) == 0);
  rmgr_type = nval;
  // TODO: Do we need locking here?
  //JASSERT(_real_pthread_mutex_unlock(&global_mutex) == 0);
}


void _rm_clear_path(dmtcp::string &path)
{
  size_t i;
  for(i=0;i<path.size();i++){
    if( path[i] == '/' || path[i] == '\\' ){
      size_t j = i+1;
      while( (path[j] == '/' || path[j] == '\\') && j < path.size() ){
        j++;
      }
      if( j != i+1 ){
        path.erase(i+1,j-(i+1));
      }
    }
  }
}

void _rm_del_trailing_slash(dmtcp::string &path)
{
    size_t i = path.size() - 1;
    while( (path[i] == ' ' || path[i] == '/' || path == "\\" ) && i>0 )
      i--;
    if( i+1 < path.size() )
      path = path.substr(0,i+1);
}

//----------------- General -----------------------------//
bool runUnderRMgr()
{

  if( _get_rmgr_type() == Empty ){
    probeTorque();
    probeSlurm();
    // probeSGE();
    // probeLSF();

    if( _get_rmgr_type() == Empty )
      _set_rmgr_type(None);
  }

  return ( _get_rmgr_type() == None ) ? false : true;
}

//---------------------------- Torque Resource Manager ---------------------//

extern "C" int dmtcp_is_bq_file(const char *path)
{
  dmtcp::string str(path);

  if( !runUnderRMgr() )
    return false;

  if( _get_rmgr_type() == torque )
    return isTorqueIOFile(str) || isTorqueFile("", str);
  else if( _get_rmgr_type() == slurm ){
      return isSlurmTmpDir(str);
  }else
    return false;
}

extern "C" int dmtcp_bq_should_ckpt_file(const char *path, int *type)
{

  if( !runUnderRMgr() )
    return false;

  if( _get_rmgr_type() == torque ){
    return torqueShouldCkptFile(path,type);
  }else if( _get_rmgr_type() == slurm ){
    return slurmShouldCkptFile(path,type);
  }
  return 0;
}

extern "C" int dmtcp_bq_restore_file(const char *path,
                                     const char *savedFilePath,
                                     int fcntlFlags, int type)
{
  dmtcp::string newpath;

  int tempfd = -1;
  if( _get_rmgr_type() == torque ){
    tempfd = torqueRestoreFile(path, savedFilePath,fcntlFlags, type);
  }else if( _get_rmgr_type() == slurm ){
      tempfd = slurmRestoreFile(path, savedFilePath,fcntlFlags, type);
  }

  return tempfd;
}
