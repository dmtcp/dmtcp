/****************************************************************************
 *  Copyright (C) 2012-2013 by Artem Y. Polyakov <artpol84@gmail.com>       *
 *                                                                          *
 *  This file is part of the Torque plugin for DMTCP                        *
 *                                                                          *
 *  Torque plugin is free software: you can redistribute it and/or          *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  Torque plugin is distributed in the hope that it will be useful,        *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

/* Update by Kapil Arya to create the Torque DMTCP plugin. */

/* Torque PBS resource manager wrappers
   Torque PBS contains libtorque library that provides API for communications
   with MOM Node management servers to obtain information about allocated
   resources and use them. In particular spawn programs on remote nodes using
   tm_spawn.

   To keep track and control under all processes spawned using any method (like
   exec, ssh) we need also to wrap tm_spawn function
*/


#include <stdlib.h>
#include <linux/limits.h>
#include <pthread.h>
#include <vector>
#include <list>
#include <string>
#include "util.h"
#include "resource_manager.h"
#include "jalib.h"
#include "jassert.h"
#include "jconvert.h"
#include "jfilesystem.h"

// -------------------- Torque PBS tm.h definitions -------------------------//
// Keep in sync with "tm.h" file in libtorque of Torque PBS resource manager
#define TM_SUCCESS  0
#define TM_ESYSTEM  17000
#define TM_ENOEVENT  17001
#define TM_ENOTCONNECTED 17002
#define TM_EUNKNOWNCMD  17003
#define TM_ENOTIMPLEMENTED 17004
#define TM_EBADENVIRONMENT 17005
#define TM_ENOTFOUND  17006
#define TM_BADINIT  17007

typedef int tm_node_id;
typedef int tm_task_id;
typedef int  tm_event_t;

static pthread_mutex_t _libtorque_mutex = PTHREAD_MUTEX_INITIALIZER;
static void *_libtorque_handle = NULL;
typedef int (*tm_spawn_t)(int argc, char **argv, char **envp, tm_node_id where, tm_task_id *tid, tm_event_t *event);
tm_spawn_t tm_spawn_ptr;

enum ResMgrFileType
{
  TORQUE_IO,
  TORQUE_NODE
};

//--------------------- Torque/PBS tm_spawn remote exec wrapper -------------//

static int libtorque_init()
{
  int ret = 0;

  // lock _libtorque_handle
  JASSERT(_real_pthread_mutex_lock(&_libtorque_mutex) == 0);
  if( _libtorque_handle == NULL ){
    // find library using pbs-config
    dmtcp::string libpath;
    if( findLibTorque(libpath) ){
      ret = -1;
      goto unlock;
    }
    // initialize tm_spawn_ptr
    JNOTE("Initialize libtorque dlopen handler")(libpath);
    char *error = NULL;
    _libtorque_handle = _real_dlopen(libpath.c_str(),RTLD_LAZY);
    if( !_libtorque_handle ){
      error = dlerror();
      if( error )
        JTRACE("Cannot open libtorque.so. Will not wrap tm_spawn")(error);
      else
        JTRACE("Cannot open libtorque.so. Will not wrap tm_spawn");
      ret = -1;
      goto unlock;
    }

    dlerror();
    tm_spawn_ptr = (tm_spawn_t)_real_dlsym(_libtorque_handle, "tm_spawn");
    if( tm_spawn_ptr == NULL ){
      error = dlerror();
      if( error )
        JTRACE("Cannot load tm_spawn from libtorque.so. Will not wrap it!")(error);
      else
        JTRACE("Cannot load tm_spawn from libtorque.so. Will not wrap it!");
      ret = -1;
      goto unlock;
    }
  }
unlock:
  JASSERT(_real_pthread_mutex_unlock(&_libtorque_mutex) == 0);
  return ret;
}

extern "C" int tm_spawn(int argc, char **argv, char **envp, tm_node_id where,
                        tm_task_id *tid, tm_event_t *event)
{
  int ret;
  JNOTE("In tm_spawn wrapper");
  if( libtorque_init() )
    return TM_BADINIT;

  char dmtcpCkptPath[PATH_MAX] = "";
  dmtcp::string ckptCmdPath = dmtcp::Util::getPath("dmtcp_checkpoint");
  ret = dmtcp::Util::expandPathname(ckptCmdPath.c_str(),
                                    dmtcpCkptPath, sizeof(dmtcpCkptPath));

  JTRACE("Expand dmtcp_checkpoint path")(dmtcpCkptPath);

  dmtcp::vector<dmtcp::string> dmtcp_args;
  dmtcp::Util::getDmtcpArgs(dmtcp_args);
  unsigned int dsize = dmtcp_args.size();
  const char *new_argv[ argc + (dsize + 1)]; // (dsize+1) is DMTCP part including dmtcpCkptPath
  dmtcp::string cmdline;
  size_t i;

  for(i = 0; i < (unsigned) argc; i++){
      JNOTE("arg[i]:")(i)(argv[i]);
  }

  new_argv[0] = dmtcpCkptPath;
  for (i = 0; i < dsize; i++) {
    new_argv[1 + i] = dmtcp_args[i].c_str();
  }
  for (int j = 0; j < argc; j++) {
    new_argv[(1 + dsize) + j] = argv[j];
  }
  for (i = 0; i< dsize + argc + 1; i++ ) {
    cmdline +=  dmtcp::string() + new_argv[i] + " ";
  }

  JNOTE ( "call Torque PBS tm_spawn API to run command on remote host" )
        ( argv[0] ) (where);
  JNOTE("CMD:")(cmdline);
  ret = tm_spawn_ptr(argc + dsize + 1,(char **)new_argv,envp,where,tid,event);

  return ret;
}

extern "C" int dmtcp_is_bq_file(const char *path)
{
  dmtcp::string str(path);
  return isTorqueIOFile(str) || isTorqueFile("", str);
}

extern "C" int dmtcp_bq_should_ckpt_file(const char *path, int *type)
{
  dmtcp::string str(path);
  if (isTorqueIOFile(str)) {
    *type = TORQUE_IO;
    return 1;
  } else if (isTorqueNodeFile(str) || *type == TORQUE_NODE) {
    *type = TORQUE_NODE;
    return 1;
  }
  return 0;
}

extern "C" int dmtcp_bq_restore_file(const char *path,
                                     const char *savedFilePath,
                                     int fcntlFlags, int type)
{
  dmtcp::string newpath;
//  char *ptr = getenv("PBS_HOME");
//  if (ptr)
//    JTRACE("Have access to pbs env:") (ptr);
//  else
//    JTRACE("Have NO access to pbs env");

  int tempfd = -1;
  if (type == TORQUE_NODE) {
    JTRACE("Restore Torque Node file");
    char newpath_tmpl[] = "/tmp/dmtcp_torque_nodefile.XXXXXX";
    if (mkstemp(newpath_tmpl) == -1) {
      strcpy(newpath_tmpl,"/tmp/dmtcp_torque_nodefile");
    }
    newpath = newpath_tmpl;
    tempfd = _real_open(newpath.c_str(), O_CREAT | O_WRONLY,
            (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP) );
    JASSERT(tempfd != -1) (path)(newpath)(JASSERT_ERRNO) .Text("open() failed");
  } else if (type == TORQUE_IO) {
    dmtcp::string str(path);
    JTRACE("Restore Torque IO file");
    if (isTorqueStdout(str)) {
      JTRACE("Restore Torque STDOUT file");
      tempfd = 1;
    } else if (isTorqueStderr(str)) {
      JTRACE("Restore Torque STDERR file");
      tempfd = 2;
    } else{
      return -1;
    }

    // get new file name
    dmtcp::string procpath = "/proc/self/fd/" + jalib::XToString(tempfd);
    newpath = jalib::Filesystem::ResolveSymlink(procpath);
  }

  JTRACE("Copying saved Resource Manager file to NEW location")
    (savedFilePath) (newpath);

  dmtcp::string command = "cat ";
  command.append(savedFilePath).append(" > ").append(newpath);
  JASSERT(_real_system(command.c_str()) != -1);

  // Reopen with initial flags
  if( type == TORQUE_NODE) {
    _real_close(tempfd);
    tempfd = _real_open(newpath.c_str(), fcntlFlags);
    JASSERT(tempfd != -1) (path)(newpath)(JASSERT_ERRNO) .Text("open() failed");
  }

  return tempfd;
}
