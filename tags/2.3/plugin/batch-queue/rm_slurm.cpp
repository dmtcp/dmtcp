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

/* SLURM resource manager supporting code
*/

#include <stdlib.h>
#include <linux/limits.h>
#include <pthread.h>
#include <vector>
#include <list>
#include <string>
#include <util.h>
#include <jalib.h>
#include <jassert.h>
#include <jconvert.h>
#include <jfilesystem.h>
#include <dmtcp.h>
#include "rm_main.h"
#include "rm_slurm.h"

void probeSlurm()
{
  JTRACE("Start");
  if( (getenv("SLURM_JOBID") != NULL) && (NULL != getenv("SLURM_NODELIST")) ){
    JTRACE("We run under SLURM!");
    // TODO: Do we need locking here?
    //JASSERT(_real_pthread_mutex_lock(&global_mutex) == 0);
    _set_rmgr_type(slurm);
  }
}

void slurm_restore_env()
{
  const dmtcp::string str_uid = dmtcp_get_uniquepid_str();
  const char *ptr = dmtcp_get_tmpdir();
  dmtcp::string tmpdir = dmtcp::string(ptr);
  dmtcp::string filename =  tmpdir + "/slurm_env_" + str_uid;
  FILE *fp = fopen(filename.c_str(),"r");
  if( !fp ){
      JTRACE("Cannot open SLURM environment file. Environment won't be restored!")(filename);
      return;
  }

#define MAX_ENV_LINE 256
  char line[MAX_ENV_LINE];
  bool host_env = false, port_env = false;
  bool tmpdir_env = false;
  while( fgets(line,MAX_ENV_LINE,fp) != NULL ){
    int len = strnlen(line, MAX_ENV_LINE);
    if( line[len-1] == '\n' ){
      line[len-1] = '\0';
    }
    dmtcp::string str = line;
    size_t pos = str.find('=');
    if( pos == dmtcp::string::npos ){
      continue;
    }
    dmtcp::string var = str.substr(0,pos);
    dmtcp::string val = str.substr(pos+1);
    JTRACE("READ ENV LINE:")(var)(val);
    if( var == "SLURM_SRUN_COMM_HOST" ){
      host_env = true;
    }
    if( var == "SLURM_SRUN_COMM_PORT" ){
      port_env = true;
    }
    if( var == "SLURMTMPDIR" ){
      char *env_tmpdir = getenv("SLURMTMPDIR");
      setenv("DMTCP_SLURMTMPDIR_OLD", env_tmpdir, 0);
      tmpdir_env = true;
    }

    setenv(var.c_str(), val.c_str(), 1);
  }
  if( !(host_env && port_env && tmpdir_env) ){
      JTRACE("Not all SLURM Environment was restored: ")(host_env)(port_env)(tmpdir_env);
  }
  char *env_host = getenv("SLURM_SRUN_COMM_HOST");
  char *env_port = getenv("SLURM_SRUN_COMM_PORT");
  char *env_tmpdir = getenv("SLURMTMPDIR");
  JTRACE("Variable at restart")(env_host)(env_port)(env_tmpdir);
}

static void print_args(char *const argv[])
{
  int i;
  dmtcp::string cmdline;
  for (i = 0; argv[i] != NULL; i++ ) {
    cmdline +=  dmtcp::string() + argv[i] + " ";
  }

  JTRACE("Init CMD:")(cmdline);
}

static int patch_srun_cmdline(char * const argv_old[], char ***_argv_new)
{
  // Calculate initial argc
  size_t argc_old;
  for(argc_old=0; argv_old[argc_old] != NULL; argc_old++);
  argc_old++;

  // Prepare DMTCP part of exec* string
  char dmtcpCkptPath[PATH_MAX] = "dmtcp_launch";
  int ret = 0;
//  dmtcp::string ckptCmdPath = dmtcp::Util::getPath("dmtcp_launch");
//  int ret = dmtcp::Util::expandPathname(ckptCmdPath.c_str(),
//                                    dmtcpCkptPath, sizeof(dmtcpCkptPath));

  JTRACE("Expand dmtcp_launch path")(dmtcpCkptPath);

  dmtcp::vector<dmtcp::string> dmtcp_args;
  dmtcp::Util::getDmtcpArgs(dmtcp_args);
  unsigned int dsize = dmtcp_args.size();

  // Prepare final comman line
  *_argv_new = (char**) JALLOC_HELPER_MALLOC(sizeof(char *) * (argc_old + (dsize + 1))); // (dsize+1) is DMTCP part including dmtcpCkptPath
  char **argv_new = *_argv_new;
  memset(argv_new, 0, sizeof(char*) * (argc_old + (dsize + 1)) );

  // Move srun part like: srun --nodes=3 --ntasks=3 --kill-on-bad-exit --nodelist=c2909,c2911,c2913
  // first string is srun and we move it anyway
  // all srun options starts with one or two dashes so we copy until see '-'.
  argv_new[0] = argv_old[0];
  size_t i;
  for(i=1; i < argc_old; i++){
    if( argv_old[i][0] == '-' ){
      argv_new[i] = argv_old[i];
      if( argv_old[i][1] != '-' && (strlen(argv_old[i]) == 2) ){
        // This is not complete handling of srun options.
        // Most of short options like -N, -n have arguments.
        // We assume that if first symbol is '-', secont is not '-' and
        // agv[i] len is equal to 2, say "-N", "-n" we skip second argument
        // options like "-N8", "-n10" are not affected
        i++;
        argv_new[i] = argv_old[i];
      }else{
        // According to srun manpage you should use --nodelist="node1,node2,..."
        // In practice some MPI implementation (i.e. Intel MPI) ignore this rule and use the following syntax:
        // "--nodelist node1,node2,..." this causes full option to be splitted onto two argv strings.
        // Here we handle ony those options that is the point of interest for MPI librarys.
        if( strcmp(argv_old[i] + 2,"nodelist") == 0 ){
          i++;
          argv_new[i] = argv_old[i];
        }
      }
    }else{
      break;
    }
  }
  size_t old_pos = i;
  size_t new_pos = i;

  // Copy dmtcp part so final command looks like: srun <opts> dmtcp_launch <dmtcp_options> orted <orted_options>
  argv_new[new_pos] = strdup(dmtcpCkptPath);
  

  new_pos++;
  for (i = 0; i < dsize; i++, new_pos++) {
    argv_new[new_pos] = strdup(dmtcp_args[i].c_str());
  }
  

  for (; old_pos < argc_old; old_pos++, new_pos++) {
    argv_new[new_pos] = argv_old[old_pos];
  }

  return ret;
}

void close_all_fds()
{
  jalib::IntVector fds =  jalib::Filesystem::ListOpenFds();
  for(size_t i = 0 ; i < fds.size(); i++){
    JTRACE("fds")(i)(fds[i]);
    if( fds[i] > 2 ){
      JTRACE("Close")(i)(fds[i]);
      jalib::close(fds[i]);
    }
  }
  fds =  jalib::Filesystem::ListOpenFds();
  JTRACE("After close:");
  for(size_t i = 0 ; i < fds.size(); i++){
    JTRACE("fds")(i)(fds[i]);
  }

}

extern "C" int execve (const char *filename, char *const argv[],
                       char *const envp[])
{
  if (jalib::Filesystem::BaseName(filename) != "srun") {
    return _real_execve(filename, argv, envp);
  }

  print_args(argv);
  char **argv_new;
  patch_srun_cmdline(argv, &argv_new);

  dmtcp::string cmdline;
  for (int i = 0; argv_new[i] != NULL; i++ ) {
    cmdline +=  dmtcp::string() + argv_new[i] + " ";
  }
  JTRACE( "How command looks from exec*:" );
  JTRACE("CMD:")(cmdline);

  close_all_fds();

  return _real_execve(filename, argv_new, envp);
}

extern "C" int execvp (const char *filename, char *const argv[])
{
  if (jalib::Filesystem::BaseName(filename) != "srun") {
    return _real_execvp(filename, argv);
  }

  print_args(argv);

  char **argv_new;
  patch_srun_cmdline(argv, &argv_new);

  dmtcp::string cmdline;
  for (int i = 0; argv_new[i] != NULL; i++ ) {
    cmdline +=  dmtcp::string() + argv_new[i] + " ";
  }

  JTRACE( "How command looks from exec*:" );
  JTRACE("CMD:")(cmdline);

  close_all_fds();

  return _real_execvp(filename, argv_new);
}

// This function first appeared in glibc 2.11
extern "C" int execvpe (const char *filename, char *const argv[],
                         char *const envp[])
{
  if (jalib::Filesystem::BaseName(filename) != "srun") {
    return _real_execvpe(filename, argv, envp);
  }

  print_args(argv);

  char **argv_new;
  patch_srun_cmdline(argv, &argv_new);

  dmtcp::string cmdline;
  for (int i = 0; argv_new[i] != NULL; i++ ) {
    cmdline +=  dmtcp::string() + argv_new[i] + " ";
  }
  JTRACE( "How command looks from exec*:" );
  JTRACE("CMD:")(cmdline);

  close_all_fds();

  return _real_execvpe(filename, argv_new, envp);
}

bool isSlurmTmpDir(dmtcp::string &str)
{
  char *env_tmpdir = getenv("SLURMTMPDIR");
  if( !env_tmpdir ){
    return false;
  }
  dmtcp::string tpath(env_tmpdir);
  // check if tpath is prefix of str
  size_t pos;
  for(pos = 0; pos < tpath.size(); pos++){
      if( str[pos] != tpath[pos] ){
          break;
      }
  }
  if( pos == tpath.size() )
      return true;
  return false;
}

int slurmShouldCkptFile(const char *path, int *type)
{
  dmtcp::string str(path);

  if (isSlurmTmpDir(str)) {
    *type = SLURM_TMPDIR;
    return 0;
  }
  return 0;
}


int slurmRestoreFile(const char *path, const char *savedFilePath,
                                     int fcntlFlags, int type)
{

  if (type != SLURM_TMPDIR) {
    // Shouldn't happen
    return -1;
  }

  char *env_old = getenv("DMTCP_SLURMTMPDIR_OLD");
  char *env_new = getenv("SLURMTMPDIR");

  JASSERT( env_old && env_new )("Environment is broken")(env_old)(env_new);

  dmtcp::string otmpdir(env_old);
  dmtcp::string ntmpdir(env_new);
  dmtcp::string newpath(path);

  _rm_del_trailing_slash(otmpdir);
  _rm_del_trailing_slash(ntmpdir);

  size_t pos = 0;
  for(pos = 0; pos < otmpdir.length(); pos++){
    if( newpath[pos] != otmpdir[pos] ){
      break;
    }
  }
  JASSERT( pos == otmpdir.length() )
          ("Something is wrong. otmpdir is not prefix of path\n")
          (otmpdir)(path);
  newpath = ntmpdir + newpath.substr(pos);
  JTRACE("new path")(newpath);

  JTRACE("Copying saved Resource Manager file to NEW location")
    (savedFilePath) (newpath);

  dmtcp::string command = "cat ";
  command.append(savedFilePath).append(" > ").append(newpath);
  JASSERT(_real_system(command.c_str()) != -1);

  // Open with initial flags
  int tempfd = _real_open(newpath.c_str(), fcntlFlags);
  JASSERT(tempfd != -1) (path)(newpath)(JASSERT_ERRNO) .Text("open() failed");
  return tempfd;
}
