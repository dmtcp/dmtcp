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

/* Update by Kapil Arya to create the Torque DMTCP plugin. */

/* The Torque PBS resource manager supporting code.

   Torque PBS contains the libtorque library, which provides an API for
   communication with the MOM Node management servers.  The library obtains
   information about the allocated resources and uses it.  In particular the
   spawn programs on the remote nodes use tm_spawn.

   To keep track of and control all processes spawned using any method (such as
   exec, ssh), we also need to wrap the tm_spawn function.
*/

#include "rm_torque.h"
#include <linux/limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <list>
#include <string>
#include <vector>
#include "jalib.h"
#include "jassert.h"
#include "jconvert.h"
#include "jfilesystem.h"
#include "procmapsarea.h"
#include "rm_main.h"
#include "rm_utils.h"
#include "util.h"

// -------------------- Torque PBS tm.h definitions -------------------------//
// Keep in sync with "tm.h" file in the libtorque library of the Torque PBS
// resource manager.
#define TM_SUCCESS         0
#define TM_ESYSTEM         17000
#define TM_ENOEVENT        17001
#define TM_ENOTCONNECTED   17002
#define TM_EUNKNOWNCMD     17003
#define TM_ENOTIMPLEMENTED 17004
#define TM_EBADENVIRONMENT 17005
#define TM_ENOTFOUND       17006
#define TM_BADINIT         17007

using namespace dmtcp;

typedef int tm_node_id;
typedef int tm_task_id;
typedef int tm_event_t;

static pthread_mutex_t _libtorque_mutex = PTHREAD_MUTEX_INITIALIZER;
static void *_libtorque_handle = NULL;
typedef int (*tm_spawn_t)(int argc, char **argv, char **envp, tm_node_id where,
                          tm_task_id *tid, tm_event_t *event);
tm_spawn_t tm_spawn_ptr;

static void setup_job();
static string torque_home_nodefile(char *ptr);
static void setup_torque_env();

static string&
torque_home()
{
  static string inst = ""; return inst;
}

static string&
torque_jobname()
{
  static string inst = ""; return inst;
}

unsigned long torque_jobid = 0;

// --------------------- Torque/PBS initialization  -------------//

void
dmtcp::probeTorque()
{
  JTRACE("Start");
  if ((getenv("PBS_ENVIRONMENT") != NULL) && (NULL != getenv("PBS_JOBID"))) {
    JTRACE("We run under Torque PBS!");

    // TODO: Do we need locking here?
    // JASSERT(_real_pthread_mutex_lock(&global_mutex) == 0);
    _set_rmgr_type(torque);

    // Set up the Torque PBS home dir.
    setup_torque_env();
    setup_job();

    // TODO: Do we need locking here?
    // JASSERT(_real_pthread_mutex_unlock(&global_mutex) == 0);
  }
}

static int
queryPbsConfig(string option, string &pbs_config)
{
  int fds[2];
  const char *pbs_config_path = "pbs-config";
  static const char *pbs_config_args[] = { "pbs-config", option.c_str(), NULL };
  int cpid;

  if (pipe(fds) == -1) {
    // Just go away - we cannot serve this request.
    JTRACE(
      "Cannot create pipe to execute pbs-config to find Torque/PBS library!");
    return -1;
  }

  cpid = _real_fork();

  if (cpid < 0) {
    JTRACE("ERROR: cannot execute pbs-config. Will not run tm_spawn!");
    return -1;
  }
  if (cpid == 0) {
    JTRACE("child process, will exec into external de-compressor");
    fds[1] = _real_dup(_real_dup(_real_dup(fds[1])));
    close(fds[0]);
    JASSERT(_real_dup2(fds[1], STDOUT_FILENO) == STDOUT_FILENO);
    close(fds[1]);
    _real_execvp(pbs_config_path, (char **)pbs_config_args);

    /* should not get here */
    JASSERT(false)(
      "ERROR: Failed to exec pbs-config. tm_spawn will fail with TM_BADINIT")(
      strerror(errno));
    exit(0);
  }

  /* parent process */
  JTRACE("created child process for pbs-config")(cpid);
  int status;
  if (waitpid(cpid, &status, 0) < 0) {
    return -1;
  }
  if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
    return -1;
  }

  // set descriptor as non-blocking
  // JTRACE ( "Set pipe fds[0] as non-blocking");
  int flags = fcntl(fds[0], F_GETFL);
  fcntl(fds[0], F_SETFL, flags | O_NONBLOCK);

  // JTRACE ( "Read pbs-config output from pipe");
  pbs_config = "";
  char buf[256];
  int count = 0;
  while ((count = read(fds[0], buf, 255)) > 0) {
    buf[count] = '\0';
    pbs_config += string() + buf;
  }

  JTRACE("pbs-config output:")(pbs_config);
  return 0;
}

int
findLibTorque_pbsconfig(string &libpath)
{
  // Config looks like: "-L<libpath> -l<libname> -Wl,--rpath -Wl,<libpath>"
  // We will search for the first libpath and the first libname.
  string libname, config;

  if (queryPbsConfig("--libs", config)) {
    // failed to read pbs-config
    return -1;
  }

  bool name_found = false, path_found = false;
  vector<string>params;
  string delim = " \n\t";
  params.clear();
  libpath = " ";
  libname = " ";

  size_t first = config.find_first_not_of(delim);
  while (first != string::npos) {
    size_t last = config.find_first_of(delim, first);
    if (last != string::npos) {
      string s(config, first, last - first);
      params.push_back(s);
      first = config.find_first_not_of(delim, last);
    } else {
      first = string::npos;
    }
  }

  // get -L & -l arguments
  for (size_t i = 0; i < params.size(); i++) {
    string &s = params[i];
    if (s[0] == '-') {
      if (s[1] == 'L') {
        string tmp(s, 2, s.size() - 2);
        libpath = tmp;
        path_found = true;
      } else if (s[1] == 'l') {
        string tmp(s, 2, s.size() - 2);
        libname = tmp;
        name_found = true;
      }
    }
  }

  if (name_found && path_found) {
    // construct full torque library path
    libpath += "/lib" + libname + ".so";
    JTRACE("Torque PBS libpath")(libpath);
    return 0;
  } else {
    return -1;
  }
}

int
findLibTorque(string &libpath)
{
  bool found = false;
  string pattern = "libtorque";

  if (!findLib_byname(pattern, libpath)) {
    found = true;
  } else if (!findLibTorque_pbsconfig(libpath)) {
    found = true;
  }

  JTRACE("Torque PBS libpath")(libpath);
  return !found;
}

// -------------- This functions probably should run with global_mutex locked!
// -----------------------//

static void
setup_job()
{
  char *ptr;

  if ((ptr = getenv("PBS_JOBID"))) {
    string str = ptr, digits = "0123456789";
    size_t pos = str.find_first_not_of(digits);
    char *eptr;
    str = str.substr(0, pos);
    torque_jobid = strtoul(str.c_str(), &eptr, 10);
  }

  if ((ptr = getenv("PBS_JOBNAME"))) {
    torque_jobname() = ptr;
  }
  JTRACE("Result:")(torque_jobid)(torque_jobname());
}

static string
torque_home_nodefile(char *ptr)
{
  // Usual nodefile path is: $PBS_HOME/aux/nodefile-name
  string nodefile = ptr;

  // clear nodefile path from duplicated slashes
  _rm_clear_path(nodefile);

  // start of file name entry
  size_t file_start = nodefile.find_last_of("/\\");
  if (file_start == string::npos || file_start == 0) {
    JTRACE("No slashes in the nodefile path");
    return "";
  }

  // start of aux entry
  size_t aux_start = nodefile.find_last_of("/\\", file_start - 1);
  if (aux_start == string::npos || aux_start == 0) {
    JTRACE("Only one slash exist in nodefile path");
    return "";
  }

  string aux_name =
    nodefile.substr(aux_start + 1, file_start - (aux_start + 1));

  JTRACE("Looks like we can grap PBS_HOME from PBS_NODEFILE")(nodefile)(
    file_start)(aux_start)(aux_name);

  // Last check: if lowest file directory is "aux"
  if (aux_name != "aux") {
    JTRACE("Wrong aux name");
    return "";
  }

  return nodefile.substr(0, aux_start);
}

static void
setup_torque_env()
{
  char *ptr;

  if ((ptr = getenv("PBS_HOME"))) {
    torque_home() = ptr;
  } else if ((ptr = getenv("PBS_SERVER_HOME"))) {
    torque_home() = ptr;
  } else if ((ptr = getenv("PBS_NODEFILE"))) {
    torque_home() = torque_home_nodefile(ptr);
  }

  if (torque_home().size()) {
    _rm_clear_path(torque_home());
    _rm_del_trailing_slash(torque_home());
  }
}

// -------------- (END) This functions probably should run with global_mutex
// locked! (END) -----------------------//


bool
dmtcp::isTorqueFile(string relpath, string &path)
{
  JTRACE("Start");
  switch (_get_rmgr_type()) {
  case Empty:
    probeTorque();
    if (_get_rmgr_type() != torque) {
      return false;
    }
    break;
  case torque:
    break;
  default:
    return false;
  }

  if (torque_home().size() == 0) {
    return false;
  }

  string abspath = torque_home() + "/" + relpath;
  JTRACE("Compare path with")(path)(abspath);
  if (path.size() < abspath.size()) {
    return false;
  }

  if (path.substr(0, abspath.size()) == abspath) {
    return true;
  }

  return false;
}

bool
dmtcp::isTorqueHomeFile(string &path)
{
  // Check if the file is in the home directory.
  char *ptr;
  string hpath = "";

  if ((ptr = getenv("HOME"))) {
    hpath = string() + ptr;
    JTRACE("Home directory:")(hpath)(path);
  } else {
    JTRACE("Cannot determine user HOME directory!");
    return false;
  }

  if (hpath.size() >= path.size()) {
    JTRACE("Length of path is less than home dir");
    return false;
  }

  if (path.substr(0, hpath.size()) != hpath) {
    JTRACE("prefix of path is not home directory")(path)(hpath);
    return false;
  }

  string suffix1 = ".OU", suffix2 = ".ER";

  if (!((path.substr(path.size() - suffix1.size()) == suffix1) ||
        (path.substr(path.size() - suffix2.size()) == suffix2))) {
    JTRACE("path has no .OU or .ER suffix")(path);
    return false;
  }

  char jobid[256];
  sprintf(jobid, "%lu", torque_jobid);
  string spool_path = hpath + "/.pbs_spool/" + jobid;
  string home_path = hpath + jobid;

  if (path.substr(0, spool_path.size()) == spool_path) {
    JTRACE("File is located in $HOME/.pbs_spool/. It is Torque/PBS stdio file")(
      path);
    return true;
  }

  if (path.substr(0, home_path.size()) == home_path) {
    JTRACE("File is located in $HOME/. It is Torque/PBS stdio file")(path);
    return true;
  }

  return false;
}

bool
dmtcp::isTorqueIOFile(string &path)
{
  // Check if the file is located in $PBS_HOME/spool
  // If so, it is the Torque stdio file.
  if (isTorqueFile("spool", path)) {
    return true;
  }

  if (isTorqueHomeFile(path)) {
    // Torque can be configured to write directly into a user's home directory.
    // In this case, we need to check the file pattern:
  }
  return false;
}

bool
dmtcp::isTorqueStdout(string &path)
{
  if (!isTorqueIOFile(path)) {
    return false;
  }

  string suffix = ".OU";

  if ((path.substr(path.size() - suffix.size()) == suffix)) {
    return true;
  }

  return false;
}

bool
dmtcp::isTorqueStderr(string &path)
{
  if (!isTorqueIOFile(path)) {
    return false;
  }

  string suffix = ".ER";

  if ((path.substr(path.size() - suffix.size()) == suffix)) {
    return true;
  }

  return false;
}

bool
dmtcp::isTorqueNodeFile(string &path)
{
  // If this file is not located in the $PBS_HOME/aux/ directory,
  // it can't be node_file.
  return isTorqueFile("aux", path);
}

// --------------------- Torque/PBS tm_spawn remote exec wrapper -------------//

static int
libtorque_init()
{
  int ret = 0;

  // lock _libtorque_handle
  JASSERT(_real_pthread_mutex_lock(&_libtorque_mutex) == 0);
  if (_libtorque_handle == NULL) {
    // find library using pbs-config
    string libpath;
    if (findLibTorque(libpath)) {
      ret = -1;
      goto unlock;
    }

    // initialize tm_spawn_ptr
    JTRACE("Initialize libtorque dlopen handler")(libpath);
    char *error = NULL;
    _libtorque_handle = _real_dlopen(libpath.c_str(), RTLD_LAZY);
    if (!_libtorque_handle) {
      error = dlerror();
      if (error) {
        JTRACE("Cannot open libtorque.so. Will not wrap tm_spawn")(error);
      } else {
        JTRACE("Cannot open libtorque.so. Will not wrap tm_spawn");
      }
      ret = -1;
      goto unlock;
    }

    dlerror();
    tm_spawn_ptr = (tm_spawn_t)_real_dlsym(_libtorque_handle, "tm_spawn");
    if (tm_spawn_ptr == NULL) {
      error = dlerror();
      if (error) {
        JTRACE("Cannot load tm_spawn from libtorque.so. Will not wrap it!")(
          error);
      } else {
        JTRACE("Cannot load tm_spawn from libtorque.so. Will not wrap it!");
      }
      ret = -1;
      goto unlock;
    }
  }
unlock:
  JASSERT(_real_pthread_mutex_unlock(&_libtorque_mutex) == 0);
  return ret;
}

extern "C" int
tm_spawn(int argc,
         char **argv,
         char **envp,
         tm_node_id where,
         tm_task_id *tid,
         tm_event_t *event)
{
  int ret;

  JTRACE("In tm_spawn wrapper");
  if (libtorque_init()) {
    return TM_BADINIT;
  }

  char dmtcpCkptPath[PATH_MAX] = "";
  string ckptCmdPath = Util::getPath("dmtcp_launch");
  ret = Util::expandPathname(ckptCmdPath.c_str(),
                             dmtcpCkptPath, sizeof(dmtcpCkptPath));

  JTRACE("Expand dmtcp_launch path")(dmtcpCkptPath);

  char **dmtcp_args = Util::getDmtcpArgs();
  size_t num_dmtcp_args = 0;
  while (dmtcp_args[num_dmtcp_args] != NULL) {
    num_dmtcp_args++;
  }

  // (num_dmtcp_args + 1) args are for DMTCP, including dmtcpCkptPath.
  const char *new_argv[argc + (num_dmtcp_args + 1)];
  string cmdline;
  size_t i;

  for (i = 0; i < (unsigned)argc; i++) {
    JTRACE("arg[i]:")(i)(argv[i]);
  }

  new_argv[0] = dmtcpCkptPath;
  for (i = 0; i < num_dmtcp_args; i++) {
    new_argv[1 + i] = dmtcp_args[i];
  }
  for (int j = 0; j < argc; j++) {
    new_argv[(1 + num_dmtcp_args) + j] = argv[j];
  }
  for (i = 0; i < num_dmtcp_args + argc + 1; i++) {
    cmdline += string() + new_argv[i] + " ";
  }

  JTRACE("call Torque PBS tm_spawn API to run command on remote host")
    (argv[0]) (where);
  JTRACE("CMD:")(cmdline);
  ret = tm_spawn_ptr(argc + num_dmtcp_args + 1,
                     (char **)new_argv,
                     envp,
                     where,
                     tid,
                     event);

  return ret;
}

int
dmtcp::torqueShouldCkptFile(const char *path, int *type)
{
  string str(path);

  if (isTorqueIOFile(str)) {
    *type = TORQUE_IO;
    return 1;
  } else if (isTorqueNodeFile(str) || *type == TORQUE_NODE) {
    *type = TORQUE_NODE;
    return 1;
  }
  return 0;
}

int
dmtcp::torqueRestoreFile(const char *path,
                         const char *savedFilePath,
                         int fcntlFlags,
                         int type)
{
  string newpath;

  int tempfd = -1;

  if (type == TORQUE_NODE) {
    JTRACE("Restore Torque Node file");
    char newpath_tmpl[] = "/tmp/dmtcp_torque_nodefile.XXXXXX";
    if (mkstemp(newpath_tmpl) == -1) {
      strcpy(newpath_tmpl, "/tmp/dmtcp_torque_nodefile");
    }
    newpath = newpath_tmpl;
    tempfd = _real_open(newpath.c_str(), O_CREAT | O_WRONLY,
                        (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP));
    JASSERT(tempfd != -1) (path)(newpath)(JASSERT_ERRNO).Text("open() failed");
  } else if (type == TORQUE_IO) {
    string str(path);
    JTRACE("Restore Torque IO file");
    if (isTorqueStdout(str)) {
      JTRACE("Restore Torque STDOUT file");
      tempfd = 1;
    } else if (isTorqueStderr(str)) {
      JTRACE("Restore Torque STDERR file");
      tempfd = 2;
    } else {
      return -1;
    }

    // get new file name
    string procpath = "/proc/self/fd/" + jalib::XToString(tempfd);
    newpath = jalib::Filesystem::ResolveSymlink(procpath);
  }

  JTRACE("Copying saved Resource Manager file to NEW location")
    (savedFilePath) (newpath);

  string command = "cat ";
  command.append(savedFilePath).append(" > ").append(newpath);
  JASSERT(_real_system(command.c_str()) != -1);

  // Reopen with initial flags
  if (type == TORQUE_NODE) {
    _real_close(tempfd);
    tempfd = _real_open(newpath.c_str(), fcntlFlags);
    JASSERT(tempfd != -1) (path)(newpath)(JASSERT_ERRNO).Text("open() failed");
  }

  return tempfd;
}
