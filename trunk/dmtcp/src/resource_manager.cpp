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

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "resource_manager.h"
#include "syscallwrappers.h"
#include "dmtcpalloc.h"
#include  "../jalib/jconvert.h"
#include  "../jalib/jassert.h"
#include  "../jalib/jfilesystem.h"


// ----------------- global data ------------------------//
enum rmgr_type_t { Empty, None, torque, sge, lsf } rmgr_type = Empty;

// TODO: Do we need locking here?
//static pthread_mutex_t global_mutex = PTHREAD_MUTEX_INITIALIZER;

dmtcp::string torque_home; // = $PBS_HOME
unsigned long torque_jobid = 0;
dmtcp::string torque_jobname = "";

static rmgr_type_t get_rmgr_type()
{
  // TODO: Do we need locking here?
  //JASSERT(_real_pthread_mutex_lock(&global_mutex) == 0);
  rmgr_type_t loc_rmgr_type = rmgr_type;
  // TODO: Do we need locking here?
  //JASSERT(_real_pthread_mutex_unlock(&global_mutex) == 0);
  return loc_rmgr_type;
}

static void set_rmgr_type(rmgr_type_t nval)
{
  // TODO: Do we need locking here?
  //JASSERT(_real_pthread_mutex_lock(&global_mutex) == 0);
  rmgr_type = nval;
  // TODO: Do we need locking here?
  //JASSERT(_real_pthread_mutex_unlock(&global_mutex) == 0);
}


static void clear_path(dmtcp::string &path)
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

static void rem_trailing_slash(dmtcp::string &path)
{
    size_t i = path.size() - 1;
    while( (path[i] == ' ' || path[i] == '/' || path == "\\" ) && i>0 )
      i--;
    if( i+1 < path.size() )
      path = path.substr(0,i+1);
}

static void probeTorque();

//----------------- General -----------------------------//
bool runUnderRMgr()
{

  if( get_rmgr_type() == Empty ){
    probeTorque();
    // probeSGE();
    // probeLSF();

    if( get_rmgr_type() == Empty )
      set_rmgr_type(None);
  }

  return ( rmgr_type == None ) ? false : true;
}

bool isResMgrFile(dmtcp::string &path)
{
  if( isTorqueIOFile(path) || isTorqueFile("",path) )
    return true;
  return false;
}
//---------------------------- Torque Resource Manager ---------------------//


// -------------- This functions probably should run with global_mutex locked! -----------------------//

static void setup_job()
{
  char *ptr;
  if( ptr = getenv("PBS_JOBID")){
    dmtcp::string str = ptr, digits = "0123456789";
    size_t pos = str.find_first_not_of(digits);
    char *eptr;
    str = str.substr(0,pos);
    torque_jobid = strtoul(str.c_str(),&eptr,10);
  }

  if( ptr = getenv("PBS_JOBNAME") ) {
    torque_jobname = ptr;
  }else{
    torque_jobname = "";
  }
  JTRACE("Result:")(torque_jobid)(torque_jobname);
}


static dmtcp::string torque_home_nodefile(char *ptr)
{
  // Usual nodefile path is: $PBS_HOME/aux/nodefile-name
  dmtcp::string nodefile = ptr;
  // clear nodefile path from duplicated slashes
  clear_path(nodefile);
  
  // start of file name entry
  size_t file_start = nodefile.find_last_of("/\\");
  if( file_start == dmtcp::string::npos || file_start == 0 ){
    JTRACE("No slashes in the nodefile path");
    return "";
  }
  // start of aux entry
  size_t aux_start = nodefile.find_last_of("/\\", file_start-1);
  if( aux_start == dmtcp::string::npos || aux_start == 0 ){
    JTRACE("Only one slash exist in nodefile path");
    return "";
  }
  
  dmtcp::string aux_name = nodefile.substr(aux_start+1, file_start - (aux_start+1));

  JTRACE("Looks like we can grap PBS_HOME from PBS_NODEFILE")(nodefile)(file_start)(aux_start)(aux_name);
  
  // Last check: if lowest file directory is "aux"  
  if( aux_name != "aux" ){
    JTRACE("Wrond aux name");
    return "";    
  }

  return nodefile.substr(0,aux_start);
}

static void setup_torque_env()
{
  char *ptr;
  if( ptr = getenv("PBS_HOME")){
    torque_home = ptr;
  }else if( ptr = getenv("PBS_SERVER_HOME") ) {
    torque_home = ptr;
  } else if( ptr = getenv("PBS_NODEFILE") ) {
      torque_home = torque_home_nodefile(ptr);
  }else{
    torque_home = "";
  }

  if( torque_home.size() ){
    clear_path(torque_home);
    rem_trailing_slash(torque_home);
  }
}

// -------------- (END) This functions probably should run with global_mutex locked! (END) -----------------------//

static void probeTorque()
{
  JTRACE("Start");
  if( (getenv("PBS_ENVIRONMENT") != NULL) && (NULL != getenv("PBS_JOBID")) ){
    JTRACE("We run under Torque PBS!");
    // TODO: Do we need locking here?
    //JASSERT(_real_pthread_mutex_lock(&global_mutex) == 0);
    rmgr_type = torque;
    // setup Torque PBS home dir
    setup_torque_env();
    setup_job();
    // TODO: Do we need locking here?
    //JASSERT(_real_pthread_mutex_unlock(&global_mutex) == 0);
  }
}

static int queryPbsConfig(dmtcp::string option, dmtcp::string &pbs_config)
{
  int fds[2];
  const char *pbs_config_path = "pbs-config";
  static const char *pbs_config_args[] = { "pbs-config", option.c_str(), NULL };
  int cpid;

  if( pipe(fds) == -1){
    // just go away - we cannot serve this request
    JTRACE("Cannot create pipe to execute pbs-config to find Torque/PBS library!");
    return -1;
  }

  cpid = _real_fork();

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
  // JTRACE ( "Set pipe fds[0] as non-blocking");
  int flags = fcntl(fds[0], F_GETFL);
  fcntl(fds[0], F_SETFL, flags | O_NONBLOCK);

  //JTRACE ( "Read pbs-config output from pipe");
  pbs_config = "";
  char buf[256];
  int count = 0;
  while( (count = read(fds[0], buf, 255)) > 0 ){
    buf[count] = '\0';
    pbs_config += dmtcp::string() + buf;
  }

  JTRACE ( "pbs-config output:")(pbs_config);
  return 0;
}

int findLibTorque(dmtcp::string &libpath, dmtcp::string &libname)
{
  // config looks like: "-L<libpath> -l<libname> -Wl,--rpath -Wl,<libpath>"
  // we will search for first libpath and first libname
  dmtcp::string config;

  if( queryPbsConfig("--libs",config) ){
    // failed to read pbs-config
    return -1;
  }

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


bool isTorqueFile(dmtcp::string relpath, dmtcp::string &path)
{
  JTRACE("Start");
  switch( rmgr_type ){
  case Empty:
    probeTorque();
    if( rmgr_type != torque )
      return false;
    break;
  case torque:
    break;
  default:
    return false;
  }

  if( torque_home.size() == 0 )
    return false;

  dmtcp::string abspath = torque_home + "/" + relpath;
  JTRACE("Compare path with")(path)(abspath);
  if( path.size() < abspath.size() )
    return false;

  if( path.substr(0,abspath.size()) == abspath )
    return true;

  return false;
}

bool isTorqueHomeFile(dmtcp::string &path)
{
  // check if file is in home directory 
  char *ptr;
  dmtcp::string hpath = "";

  if( ptr = getenv("HOME") ){
    hpath = dmtcp::string() + ptr;
    JTRACE("Home directory:")(hpath)(path);
  }else{
    JTRACE("Cannot determine user HOME directory!");
    return false;
  }

  if( hpath.size() >= path.size() ){
    JTRACE("Length of path is less than home dir");
    return false;
  }

  if( path.substr(0,hpath.size()) != hpath ){
    JTRACE("prefix of path is not home directory")(path)(hpath);
    return false;
  }

  dmtcp::string suffix1 = ".OU", suffix2 = ".ER";

  if( !( (path.substr(path.size() - suffix1.size()) == suffix1) || 
        (path.substr(path.size() - suffix2.size()) == suffix2) ) ){
    JTRACE("path has no .OU or .ER suffix")(path);
    return false;
  }

  char jobid[256];
  sprintf(jobid,"%d",torque_jobid);
  dmtcp::string spool_path = hpath + "/.pbs_spool/" + jobid;
  dmtcp::string home_path = hpath + jobid;

  if( path.substr(0,spool_path.size()) == spool_path ){
    JTRACE("File is located in $HOME/.pbs_spool/. It is Torque/PBS stdio file")(path);
    return true;
  }

  if( path.substr(0,home_path.size()) == home_path ){
    JTRACE("File is located in $HOME/. It is Torque/PBS stdio file")(path);
    return true;
  }

  return false;
}

bool isTorqueIOFile(dmtcp::string &path)
{
  // Check if file is located in $PBS_HOME/spool
  // If so - it is Torque stdio file
  if( isTorqueFile("spool", path) )
    return true;

  if( isTorqueHomeFile(path) ){
    // Torque can be configured to write directly into users home directory.
    // In this case we need to check file pattern:
  }
  return false;
}

bool isTorqueStdout(dmtcp::string &path)
{
  if( !isTorqueIOFile(path) )
    return false;

  dmtcp::string suffix = ".OU";

  if( (path.substr(path.size() - suffix.size()) == suffix) ){ 
    return true;
  }

  return false;
}

bool isTorqueStderr(dmtcp::string &path)
{
  if( !isTorqueIOFile(path) )
    return false;

  dmtcp::string suffix = ".ER";

  if( (path.substr(path.size() - suffix.size()) == suffix) ){ 
    return true;
  }

  return false;
}

bool isTorqueNodeFile(dmtcp::string &path)
{
  // if this file is not located in $PBS_HOME/aux/ directory
  // it can't be node_file
  if( isTorqueFile("aux", path) )
    return true;
}
