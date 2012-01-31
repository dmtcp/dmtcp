/****************************************************************************
 *   Copyright (C) 2006-2010 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *   This file is part of the dmtcp/src module of DMTCP (DMTCP:dmtcp/src).  *
 *                                                                          *
 *  DMTCP:dmtcp/src is free software: you can redistribute it and/or        *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP:dmtcp/src is distributed in the hope that it will be useful,      *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

// This file was originally contributed
// by Artem Y. Polyakov <artpol84@gmail.com>.

/* Torque PBS resource manager wrappers
   Torque PBS contains libtorque library that provides API for communications
   with MOM Node management servers to obtain information about allocated resources
   and use them. In particular spawn programs on remote nodes using tm_spawn.

   To keep track and control under all processes spawned using any method (like exec, ssh)
   we need also to wrap tm_spawn function
*/


#include <malloc.h>
#include <dlfcn.h>
#include <string.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <list>
#include <string>
#include "constants.h"
#include "connectionmanager.h"
#include "uniquepid.h"
#include "dmtcpworker.h"
#include "virtualpidtable.h"
#include "sysvipc.h"
#include "syscallwrappers.h"
#include "syslogwrappers.h"
#include "util.h"
#include  "../jalib/jconvert.h"
#include  "../jalib/jassert.h"
#include  "../jalib/jfilesystem.h"


// -------------------- Torque PBS tm.h definitions -----------------------------------//
// need to be keeped in sync with "tm.h" file in libtorque of Torque PBS resource manager
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


static int get_dmtcp_args(dmtcp::vector<dmtcp::string> &dmtcp_args, bool full_path = false)
{
/* This code is taken from dmtcpworker.cpp from processSshCommand function
   In future this code should be moved into separate function to avoid 
   duplication
*/
  const char * prefixPath           = getenv ( ENV_VAR_PREFIX_PATH );
  const char * coordinatorAddr      = getenv ( ENV_VAR_NAME_HOST );

  char buf[256];
  if (coordinatorAddr == NULL) {
    JASSERT(gethostname(buf, sizeof(buf)) == 0) (JASSERT_ERRNO);
    coordinatorAddr = buf;
  }
  const char * coordinatorPortStr   = getenv ( ENV_VAR_NAME_PORT );
  const char * sigckpt              = getenv ( ENV_VAR_SIGCKPT );
  const char * compression          = getenv ( ENV_VAR_COMPRESSION );
#ifdef HBICT_DELTACOMP
  const char * deltacompression     = getenv ( ENV_VAR_DELTACOMPRESSION );
#endif
  const char * ckptOpenFiles        = getenv ( ENV_VAR_CKPT_OPEN_FILES );
  const char * ckptDir              = getenv ( ENV_VAR_CHECKPOINT_DIR );
  const char * tmpDir               = getenv ( ENV_VAR_TMPDIR );
  if (getenv(ENV_VAR_QUIET)) {
    jassert_quiet                   = *getenv ( ENV_VAR_QUIET ) - '0';
  } else {
    jassert_quiet = 0;
  }

  //modify the command
  dmtcp_args.clear();

  dmtcp::string prefix = "";
  if (prefixPath != NULL) {
    prefix += dmtcp::string() + prefixPath + "/bin/";
  }

  prefix += DMTCP_CHECKPOINT_CMD;

  if (full_path) {
    char full_path[UTIL_MAX_PATH_LEN] = "";
    // TODO: find dmtcp_restart full path
    int ret = dmtcp::Util::expandPathname(prefix.c_str(), full_path, sizeof(full_path));

    JTRACE("Expand dmtcp_checkpoint path")(prefix)(ret)(full_path);

    if( ret == 0 ){
      // expand successful
      prefix = dmtcp::string() + full_path;
    }
  }

  dmtcp_args.push_back(prefix);

  if ( coordinatorAddr != NULL ){
    dmtcp_args.push_back( "--host" );
    dmtcp_args.push_back( coordinatorAddr );
  }

  if ( coordinatorPortStr != NULL ){
    dmtcp_args.push_back( "--port" );
    dmtcp_args.push_back( coordinatorPortStr );
  }

  if ( sigckpt != NULL ){
    dmtcp_args.push_back( "--mtcp-checkpoint-signal" );
    dmtcp_args.push_back( sigckpt );
  }

  if ( prefixPath != NULL ){
    dmtcp_args.push_back( "--prefix" );
    dmtcp_args.push_back( prefixPath );
  }

  if ( ckptDir != NULL ){
    dmtcp_args.push_back( "--ckptdir" );
    dmtcp_args.push_back( ckptDir );
  }

  if ( tmpDir != NULL ){
    dmtcp_args.push_back( "--tmpdir" );
    dmtcp_args.push_back( tmpDir );
  }

  if ( ckptOpenFiles != NULL ){
    dmtcp_args.push_back( "--checkpoint-open-files" );
  }

  if ( compression != NULL ) {
    if ( strcmp ( compression, "0" ) == 0 )
      dmtcp_args.push_back( "--no-gzip" );
    else
      dmtcp_args.push_back( "--gzip" );
  }

#ifdef HBICT_DELTACOMP
  if (deltacompression != NULL) {
    if (strcmp(deltacompression, "0") == 0)
      dmtcp_args.push_bacBk( "--no-hbict" );
    else
      dmtcp_args.push_back( "--hbict" );
  }
#endif
  return 0;
}

//------------------------------- SSH exec wrapper -----------------------------------------//

void processSshCommand(dmtcp::string programName,
                              dmtcp::vector<dmtcp::string>& args)
{
  char buf[256];

  JTRACE("processSshCommand");

  JASSERT ( jalib::Filesystem::GetProgramName() == "ssh" );
  //make sure coordinator connection is closed
  _real_close ( PROTECTED_COORD_FD );

  JASSERT ( args.size() >= 3 ) ( args.size() )
    .Text ( "ssh must have at least 3 args to be wrapped (ie: ssh host cmd)" );

  //find command part
  size_t commandStart = 2;
  for ( size_t i = 1; i < args.size(); ++i )
  {
    if ( args[i][0] != '-' )
    {
      commandStart = i + 1;
      break;
    }
  }
  JASSERT ( commandStart < args.size() && args[commandStart][0] != '-' )
    ( commandStart ) ( args.size() ) ( args[commandStart] )
    .Text ( "failed to parse ssh command line" );

  //find the start of the command
  dmtcp::string& cmd = args[commandStart];

  dmtcp::vector<dmtcp::string> dmtcp_args;

  get_dmtcp_args(dmtcp_args);

  dmtcp::string prefix = "";

  JTRACE("dmtcp_args.size():")(dmtcp_args.size());
  if( dmtcp_args.size() ){
    prefix = dmtcp_args[0] + " --ssh-slave ";
    for(int i = 1; i < dmtcp_args.size(); i++){
      prefix += dmtcp::string() +  dmtcp_args[i] + " ";
    }
  }

  JTRACE("Prefix")(prefix);


  // process command
  size_t semipos, pos;
  size_t actpos = dmtcp::string::npos;
  for(semipos = 0; (pos = cmd.find(';',semipos+1)) != dmtcp::string::npos;
      semipos = pos, actpos = pos);

  if( actpos > 0 && actpos != dmtcp::string::npos ){
    cmd = cmd.substr(0,actpos+1) + prefix + cmd.substr(actpos+1);
  } else {
    cmd = prefix + cmd;
  }

  //now repack args
  dmtcp::string newCommand = "";
  char** argv = new char*[args.size() +2];
  memset ( argv,0,sizeof ( char* ) * ( args.size() +2 ) );

  for ( size_t i=0; i< args.size(); ++i )
  {
    argv[i] = ( char* ) args[i].c_str();
    newCommand += args[i] + ' ';
  }

  JNOTE ( "re-running SSH with checkpointing" ) ( newCommand );

  restoreUserLDPRELOAD();
  //now re-call ssh
  _real_execvp ( argv[0], argv );

  //should be unreachable
  JASSERT ( false ) ( cmd ) ( JASSERT_ERRNO ).Text ( "exec() failed" );
}

//--------------------- Torque/PBS tm_spawn remote exec wrapper ----------------------------------------------//

int parsePBSconfig(dmtcp::string &config, dmtcp::string &libpath, dmtcp::string &libname)
{
  // config looks like: "-L<libpath> -l<libname> -Wl,--rpath -Wl,<libpath>"
  // we will search for first libpath and first libname

  bool libpath_found = false, libname_found = false;
  dmtcp::vector<dmtcp::string> params;
  dmtcp::string delim = " \n\t";
  params.clear();
  libpath = " ";
  libname = " ";

  size_t first = config.find_first_not_of(delim);
  while( first != dmtcp::string::npos ){
    size_t last = config.find_first_of(delim,first);
    if( last != dmtcp::string::npos ){
      dmtcp::string s(config,first,last-first);
      params.push_back(s);
      first = config.find_first_not_of(delim,last);
    }else{
      first = dmtcp::string::npos;
    }
  }

  // get -L & -l arguments
  for(int i=0; i < params.size(); i++){
    dmtcp::string &s = params[i];
    if( s[0] == '-' ){
      if( s[1] == 'L' ){
        dmtcp::string tmp(s,2,s.size() - 2);
        libpath = tmp;
        libpath_found = true;
      }else if( s[1] == 'l' ){
        dmtcp::string tmp(s,2,s.size() - 2);
        libname = tmp;
        libname_found = true;
      }
    }
  }
  JTRACE("Torque PBS libname and libpath")(libname)(libpath);
  return !(libpath_found && libname_found);
}

int findLibTorque(dmtcp::string &libpath, dmtcp::string &libname)
{
  int fds[2];
  const char *pbs_config_path = "pbs-config";
  static const char *pbs_config_args[] = { "pbs-config", "--libs", NULL };
  int cpid;

  JASSERT(pipe(fds) != -1).Text("Cannot create pipe to execute pbs-config to find Torque/PBS library!");
  cpid = _real_fork();
  JASSERT(cpid != -1).Text("ERROR: Cannot fork to execute gunzip to decompress checkpoint file!");

  if( cpid < 0 ){
    JTRACE( "ERROR: cannot execute pbs-config. Will not run tm_spawn!");
    return -1;
  }
  if( cpid == 0 ){
    JTRACE ( "child process, will exec into external de-compressor");
    fds[1] = dup(dup(dup(fds[1])));
    close(fds[0]);
    JASSERT(dup2(fds[1], STDOUT_FILENO) == STDOUT_FILENO);
    close(fds[1]);
    _real_execvp(pbs_config_path, (char **)pbs_config_args);
    /* should not get here */
    JASSERT(false)("ERROR: Failed to exec pbs-config. tm_spawn will fail with TM_BADINIT")(strerror(errno));
    exit(0);
  }

  /* parent process */
  JTRACE ( "created child process for pbs-config")(cpid);
  int status;
  if( _real_waitpid(cpid,&status,0) < 0 ){
    return -1;
  }
  if( !( WIFEXITED(status) && WEXITSTATUS(status) == 0 ) ){
    return -1;
  }

  // set descriptor as non-blocking
  JTRACE ( "Set pipe fds[0] as non-blocking");
  int flags = fcntl(fds[0], F_GETFL);
  fcntl(fds[0], F_SETFL, flags | O_NONBLOCK);

  JTRACE ( "Read pbs-config output from pipe");

  dmtcp::string pbs_config;
  char buf[256];
  int count = 0;
  while( (count = read(fds[0], buf, 255)) > 0 ){
    buf[count] = '\0';
    pbs_config += dmtcp::string() + buf;
  }

  JTRACE ( "Parse pbs-config output:")(pbs_config);

  return parsePBSconfig(pbs_config, libpath, libname);
}

static int libtorque_init()
{
  int ret = 0;

  // lock _libtorque_handle
  JASSERT(_real_pthread_mutex_lock(&_libtorque_mutex) == 0);
  if( _libtorque_handle == NULL ){
    // find library using pbs-config
    dmtcp::string libname, libpath;
    if( findLibTorque(libpath,libname) ){
      ret = -1;
      goto unlock;
    }
    // construct full torque library path
    libpath += "/lib" + libname + ".so";
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




extern "C" int tm_spawn(int argc, char **argv, char **envp, tm_node_id where, tm_task_id *tid, tm_event_t *event)
{

  JNOTE("In tm_spawn wrapper");

  if( libtorque_init() )
    return TM_BADINIT;

  dmtcp::vector<dmtcp::string> dmtcp_args;

  get_dmtcp_args(dmtcp_args,true);

  unsigned int dsize = dmtcp_args.size();
  const char *new_argv[ argc + dsize ];

  dmtcp::string cmdline = dmtcp::string();

  for(int i=0; i < dsize; i++){
    new_argv[i] = dmtcp_args[i].c_str();
  }

  for(int i=0; i < argc; i++){
    new_argv[ dsize + i ] = argv[i];
  }

  for(int i=0; i< dsize + argc; i++){
    cmdline +=  dmtcp::string() + new_argv[i] + " ";
  }

  JNOTE ( "call Torque PBS tm_spawn API to run command on remote host" ) ( argv[0] ) (where);
  JNOTE("CMD:")(cmdline);
  int ret = tm_spawn_ptr(argc + dsize,(char **)new_argv,envp,where,tid,event);

  return ret;
}

