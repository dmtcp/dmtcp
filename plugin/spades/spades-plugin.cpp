#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>

#include "dmtcp.h"
#include "config.h"
#include "jassert.h"
#include "../src/constants.h"
#include "../src/uniquepid.h"

using namespace dmtcp;

dmtcp::string ckptDir;
dmtcp::string outputDirName;
dmtcp::string outputDirAbsPath;
dmtcp::string tmpDirName;
dmtcp::string tmpDirAbsPath;
dmtcp::string uniqueSuffix;
pid_t leader_pid;
int ckptedTmp = 0;

// Every process will try to get the lock
void pre_ckpt()
{
  /*
    QUE: should we use random wait to give preference to one process?
    struct timespec tim;
    tim.tv_sec = 1;
    tim.tv_nsec = 500000L * rand() / RAND_MAX;
    nanosleep(&tim ,(struct timespec *)NULL);
  */

  // get the output directory of spades
  ckptDir = getenv(ENV_VAR_CHECKPOINT_DIR);
  JASSERT (ckptDir != "") ("checkpoint dir is not set!");

  // open the file in the output directory
  dmtcp::string flock_file  = ckptDir + "/precious_file";
  int flag = O_RDONLY;
  if( access(flock_file.c_str(), F_OK ) == -1 ) {
    flag = O_RDWR |  O_CREAT;
  }
  int fd = open(flock_file.c_str(), flag, 0644);
  JASSERT ((fd != -1) || (errno == EACCES)) (JASSERT_ERRNO) (fd);
  int lock = LOCK_EX;
  if (fd != -1) {
    // get the lock
    int ret = flock(fd, lock);
    if (ret != -1) {
      JTRACE("leader is elected") (getpid());
      leader_pid = getpid();
    }
    close(fd);
    unlink(flock_file.c_str());
  }
}

void save_dir(dmtcp::string dirAbsPath, dmtcp::string dirName)
{
  // make the directory if not exist
  ostringstream mkdirCmd;
  mkdirCmd << "mkdir -p "<< ckptDir << "/" << dirName
    << "-bck-" << uniqueSuffix;
  int ret = system(mkdirCmd.str().c_str());
  JASSERT(ret != -1) ("Pre-ckpt: mkdir command failed for spades")
    (mkdirCmd.str());
  JTRACE("directory make command")(mkdirCmd.str());

  // copy the directory
  ostringstream cmd;
  cmd << "cp -rf " << dirAbsPath << "/. " << ckptDir
    << "/" << dirName << "-bck-" << uniqueSuffix;
  ret = system(cmd.str().c_str());
  JASSERT(ret != -1) ("Pre-ckpt: copy command failed for spades") (cmd.str());
  JTRACE("directory copied command ")(cmd.str());
}

int is_leader()
{
  return (getpid() == leader_pid);
}

void setup_global_info()
{
  dmtcp::string ckptSuffix = UniquePid::ThisProcess().toString();
  uniqueSuffix = ckptSuffix.substr(ckptSuffix.find_last_of('-')+1);
  tmpDirAbsPath = getenv(ENV_VAR_SPADES_TMPDIR);
  outputDirAbsPath = getenv(ENV_VAR_SPADES_OUTDIR);
  JASSERT (outputDirAbsPath != "") ("Spades output dir name is not found");
  // directory name
  size_t index = outputDirAbsPath.find_last_of("/");
  outputDirName = outputDirAbsPath.substr(index+1);
}

/* 
 * Every process select the leader to save the precious files
 */
void drain_precious_files()
{
  // save the precioud files if the current process is the leader
  if (is_leader())
  {
    setup_global_info();
    save_dir(outputDirAbsPath, outputDirName);

    if (tmpDirAbsPath != "")
    {
      // directory name
      size_t index = tmpDirAbsPath.find_last_of("/");
      tmpDirName = tmpDirAbsPath.substr(index+1);
      save_dir(tmpDirAbsPath, tmpDirName);
      ckptedTmp = 1;
    }
  }
}

void restore_dir(dmtcp::string dirAbsPath, dmtcp::string dirName)
{
  JTRACE("Dir is restore back to the original path");
  ostringstream mkdirCmd;

  // make the directory if doesn't exist
  mkdirCmd << "mkdir -p "<< dirAbsPath;
  int ret = system(mkdirCmd.str().c_str());
  JASSERT(ret != -1) ("Restore: mkdir command failed for spades")
    (mkdirCmd.str());
  JTRACE("directory restore command")(mkdirCmd.str());

  // copy the output directory
  ostringstream cmd;
  cmd << "cp -rf " << ckptDir << "/" << dirName
    << "-bck-" << uniqueSuffix << "/. " << dirAbsPath << "/";
  ret = system(cmd.str().c_str());
  JASSERT(ret != -1) ("Restore: copy command failed for spades") (cmd.str());
  JTRACE("directory copied command ")(cmd.str());
}

/*
  restore the precious directory
*/
void restart()
{
  if (is_leader()) {
    restore_dir(outputDirAbsPath, outputDirName);
    // restore tmp-dir if it was saved
    if (ckptedTmp) {
      restore_dir(tmpDirAbsPath, tmpDirName);
    }
  }
}

static void
cuda_plugin_event_hook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
    case DMTCP_EVENT_INIT:
      {
        JTRACE("*** DMTCP_EVENT_INIT");
        JTRACE("Spades Plugin intialized");
        break;
      }
    case DMTCP_EVENT_PRECHECKPOINT:
      {
        pre_ckpt();
        drain_precious_files();
        break;
      }
    case DMTCP_EVENT_RESTART:
      {
        restart();
        break;
      }
    case DMTCP_EVENT_EXIT:
      JTRACE("*** DMTCP_EVENT_EXIT");
      break;
    default:
      break;
  }
}


DmtcpPluginDescriptor_t spades_plugin = {
  DMTCP_PLUGIN_API_VERSION,
  PACKAGE_VERSION,
  "spades_plugin",
  "DMTCP",
  "dmtcp@ccs.neu.edu",
  "Cuda Split Plugin",
  cuda_plugin_event_hook
};

DMTCP_DECL_PLUGIN(spades_plugin);

