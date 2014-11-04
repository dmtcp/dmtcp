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
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <assert.h>
#include "rm_main.h"
#include "rm_slurm.h"
#include "slurm_helper.h"

static const char *srunHelper = "dmtcp_srun_helper";

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
  fclose(fp);
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

/*
 * This routine hijacks original srun program in following way:
 * orig:
 * srun <srun-opts> <binary> <binary-opts>
 * new :
 * dmtcp_srun_helper dmtcp_nocheckpoint | srun <srun-opts> | dmtcp_launch <dmtcp-opts> --explicit-srun |  <binary> <binary-opts>
 * <---------- dmtcp hijack -----------> <-orig srun------> <------------- dmtcp hijack --------------> <----- orig binary----->
 *
 * This complexy is because we want to launch dmtcp srun helper that will knowingly be under
 * checkpoint control and will help to correctly redirect srun IO after restart.
 * The idea is derived from Kapil's SSH plugin.
 */
static int patch_srun_cmdline(char * const argv_old[], char ***_argv_new)
{
  // Calculate initial argc
  size_t argc_old;
  for(argc_old=0; argv_old[argc_old] != NULL; argc_old++);

  // Prepare DMTCP part of exec* string
  char dmtcpNoCkptPath[] = "dmtcp_nocheckpoint";
  char dmtcpCkptPath[] = "dmtcp_launch";
  int ret = 0;

  JTRACE("Expand dmtcp_launch path")(dmtcpCkptPath);

  dmtcp::vector<dmtcp::string> dmtcp_args;
  dmtcp::Util::getDmtcpArgs(dmtcp_args);
  unsigned int dsize = dmtcp_args.size();

  // Prepare final comman line of length:
  // 1 /*end NULL*/ + 2 /* helper prefix*/ + dsize + 2 /* dmtcpCkptPath and --explicit-srun */
  int vect_size = sizeof(char *) * (argc_old + 5 + dsize);
  *_argv_new = (char**) JALLOC_HELPER_MALLOC(vect_size);
  char **argv_new = *_argv_new;
  memset(argv_new, 0, vect_size );

  // Form DMTCP command to launch dmtcp_srun_helper
  size_t new_pos = 0, i, old_pos = 0;
  argv_new[new_pos++] = strdup(srunHelper);
  argv_new[new_pos++] = strdup(dmtcpNoCkptPath);

  // Move srun part like: srun --nodes=3 --ntasks=3 --kill-on-bad-exit --nodelist=c2909,c2911,c2913
  // first string is srun and we move it anyway
  // all srun options starts with one or two dashes so we copy until see '-'.
  argv_new[new_pos++] = argv_old[old_pos++];
  for(; old_pos < argc_old; old_pos++){
    if( argv_old[old_pos][0] == '-' ){
      argv_new[new_pos++] = argv_old[old_pos];
      if( argv_old[old_pos][1] != '-' && (strlen(argv_old[old_pos]) == 2) ){
        // This is not complete handling of srun options.
        // Most of short options like -N, -n have arguments.
        // We assume that if first symbol is '-', secont is not '-' and
        // agv[i] len is equal to 2, say "-N", "-n" we skip second argument
        // options like "-N8", "-n10" are not affected
        argv_new[new_pos++] = argv_old[++old_pos];
      }else{
        // According to srun manpage you should use --nodelist="node1,node2,..."
        // In practice some MPI implementation (i.e. Intel MPI) ignore this rule and use the following syntax:
        // "--nodelist node1,node2,..." this causes full option to be splitted onto two argv strings.
        // Here we handle ony those options that is the point of interest for MPI librarys.
        if( strcmp(argv_old[old_pos] + 2,"nodelist") == 0 ){
          argv_new[new_pos++] = argv_old[++old_pos];
        }
      }
    }else{
      break;
    }
  }
  // Copy dmtcp part so final command looks like: srun <opts> dmtcp_launch <dmtcp_options> orted <orted_options>
  argv_new[new_pos++] = strdup(dmtcpCkptPath);
  for (i = 0; i < dsize; i++, new_pos++) {
    argv_new[new_pos] = strdup(dmtcp_args[i].c_str());
  }
  argv_new[new_pos++] = strdup("--explicit-srun");

  for (; old_pos < argc_old; ) {
    argv_new[new_pos++] = argv_old[old_pos++];
  }

  return ret;
}

static void occupate_stdio()
{
  int fd;
  for(fd=0; fd<3; fd++){
    CHECK_FWD_TO_DEV_NULL(fd);
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

  char helper_path[PATH_MAX];
  JASSERT(dmtcp::Util::expandPathname(srunHelper, helper_path, sizeof(helper_path)) == 0 );
  // We need this step to protect stdio fd's from being opened for other purposes.
  occupate_stdio();
  return _real_execve(helper_path, argv_new, envp);
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

  // We need this step to protect stdio fd's from being opened for other purposes.
  occupate_stdio();
  return _real_execvp(srunHelper, argv_new);
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

  // We need this step to protect stdio fd's from being opened for other purposes.
  occupate_stdio();
  return _real_execvpe(srunHelper, argv_new, envp);
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

// Deal with srun
static bool is_srun_helper = false;
static int srun_stdin;
static int srun_stdout;
static int srun_stderr;
static int *srun_pid = NULL;

// FIXME: this is a hackish solution. TODO: add this to plugin API.
extern "C" void process_fd_event(int event, int arg1, int arg2 = -1);

extern "C" void slurm_srun_handler_register(int in, int out, int err, int *pid)
{
  process_fd_event(SYS_close, in);
  process_fd_event(SYS_close, out);
  process_fd_event(SYS_close, err);
  srun_stdin = in;
  srun_stdout = out;
  srun_stderr = err;
  srun_pid = pid;
  is_srun_helper = true;
}

static int connect_to_restart_helper(char *path)
{
  static struct sockaddr_un sa;
  memset(&sa, 0, sizeof(sa));
  int sd = _real_socket(AF_UNIX, SOCK_STREAM, 0);
  JASSERT( sd >= 0 );
  sa.sun_family = AF_UNIX;
  memcpy(&sa.sun_path, path, sizeof(sa.sun_path));
  JASSERT( _real_connect(sd,(struct sockaddr*) &sa, sizeof(sa)) == 0);
  return sd;
}

static int create_fd_tx_socket(sockaddr_un *sa)
{
  socklen_t slen = sizeof(*sa);
  memset(sa, 0, sizeof(*sa));
  int sd = _real_socket(AF_UNIX, SOCK_DGRAM, 0);
  JASSERT( sd >= 0 );
  sa->sun_family = AF_UNIX;
  JASSERT( _real_bind(sd, (struct sockaddr*)sa, sizeof(sa->sun_family)) == 0);
  JASSERT( getsockname(sd,(struct sockaddr *)sa, &slen) == 0);
  return sd;
}

void get_fd(int txfd, int fd)
{
  int data;
  int ret = slurm_receiveFd(txfd, &data, sizeof(data));
  JASSERT( ret >= 0 );
  if( fd < 0 ){ // We don't want this fd
    _real_close(ret);
  } else if( fd != ret ){
    _real_close(fd);
    JASSERT(_real_dup2(ret, fd) == fd);
    _real_close(ret);
  }
}

int move_fd_after(int fd, int min_fd)
{
  if( fd > min_fd )
    return fd;
  int i = min_fd + 1;
  while( i < 65000 ){
    if( _real_fcntl(i,F_GETFL) == -1 ){
      // this fd is free
      JASSERT(_real_dup2(fd, i) == i);
      _real_close(fd);
      return i;
    }
    i++;
  }
  return -1;
}

extern "C" pid_t dmtcp_get_real_pid();

static void restart_helper()
{
  char *path = getenv(DMTCP_SRUN_HELPER_ADDR_ENV);
  if( !(is_srun_helper && path) ){
    return;
  }
  int min_fd = MAX(MAX(srun_stdin, srun_stdout),srun_stderr);
  int sd = connect_to_restart_helper(path);
  JASSERT( (sd = move_fd_after(sd,min_fd)) >= min_fd );

  // TODO: is this normal way to obtain real PID?
  int pid = dmtcp_get_real_pid();

  JASSERT( write(sd, &pid, sizeof(pid)) == sizeof(pid) );
  JASSERT( read(sd,srun_pid, sizeof(*srun_pid)) == sizeof(*srun_pid));

  struct sockaddr_un sa;
  int txfd = create_fd_tx_socket(&sa);
  JASSERT( (txfd = move_fd_after(txfd,min_fd)) >= min_fd );
  JASSERT( write(sd, &sa, sizeof(sa)) == sizeof(sa));

  get_fd(txfd, srun_stdin);
  get_fd(txfd, srun_stdout);
  get_fd(txfd, srun_stderr);
  _real_close(sd);
  _real_close(txfd);
}


void slurmRestoreHelper( bool isRestart )
{
  if( isRestart && is_srun_helper){
    JTRACE("This is srun helper. Restore it");
//    {
//      int delay = 1;
//      while (delay) {
//        sleep(1);
//      }
//    }
    restart_helper();
  }
}


