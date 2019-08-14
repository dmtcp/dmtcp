/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *  This file is part of DMTCP.                                             *
 *                                                                          *
 *  DMTCP is free software: you can redistribute it and/or                  *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP is distributed in the hope that it will be useful,                *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include "restartscript.h"

#include <iomanip>
#include <iostream>
#include <string>

#include <fcntl.h>
#include <stdio.h>

#include <sys/stat.h>

#include "constants.h"

#include "jassert.h"
#include "jfilesystem.h"

namespace dmtcp
{
namespace RestartScript
{
static const char *header =
  "#!/bin/bash\n\n"
  "set -m # turn on job control\n\n"
  "#This script launches all the restarts in the background.\n"
  "#Suggestions for editing:\n"
  "#  1. For those processes executing on the localhost, remove\n"
  "#     'ssh <hostname> from the start of the line.\n"
  "#  2. If using ssh, verify that ssh does not require passwords or other\n"
  "#     prompts.\n"
  "#  3. Verify that the dmtcp_restart command is in your path on all hosts,\n"
  "#     otherwise set the dmt_rstr_cmd appropriately.\n"
  "#  4. Verify DMTCP_COORD_HOST and DMTCP_COORD_PORT match the location of\n"
  "#     the dmtcp_coordinator. If necessary, add\n"
  "#     'DMTCP_COORD_PORT=<dmtcp_coordinator port>' after\n"
  "#     'DMTCP_COORD_HOST=<...>'.\n"
  "#  5. Remove the '&' from a line if that process reads STDIN.\n"
  "#     If multiple processes read STDIN then prefix the line with\n"
  "#     'xterm -hold -e' and put '&' at the end of the line.\n"
  "#  6. Processes on same host can be restarted with single dmtcp_restart\n"
  "#     command.\n\n\n"
;


static const char *checkLocal =
  "check_local()\n"
  "{\n"
  "  worker_host=$1\n"
  "  unset is_local_node\n"
  "  worker_ip=$(gethostip -d $worker_host 2> /dev/null)\n"
  "  if [ -z \"$worker_ip\" ]; then\n"
  "    worker_ip=$(nslookup $worker_host | grep -A1 'Name:' | grep 'Address:' | sed -e 's/Address://' -e 's/ //' -e 's/	//')\n"
  "  fi\n"
  "  if [ -z \"$worker_ip\" ]; then\n"
  "    worker_ip=$(getent ahosts $worker_host |grep \"^[0-9]\\+\\.[0-9]\\+\\.[0-9]\\+\\.[0-9]\\+ *STREAM\" | cut -d' ' -f1)\n"
  "  fi\n"
  "  if [ -z \"$worker_ip\" ]; then\n"
  "    echo Could not find ip-address for $worker_host. Exiting...\n"
  "    exit 1\n"
  "  fi\n"
  "  ifconfig_path=$(which ifconfig)\n"
  "  if [ -z \"$ifconfig_path\" ]; then\n"
  "    ifconfig_path=\"/sbin/ifconfig\"\n"
  "  fi\n"
  "  output=$($ifconfig_path -a | grep \"inet addr:.*${worker_ip} .*Bcast\")\n"
  "  if [ -n \"$output\" ]; then\n"
  "    is_local_node=1\n"
  "  else\n"
  "    is_local_node=0\n"
  "  fi\n"
  "}\n\n\n";

static const char *slurmHelperContactFunction =
  "pass_slurm_helper_contact()\n"
  "{\n"
  "  LOCAL_FILES=\"$1\"\n"
  "  # Create temp directory if needed\n"
  "  if [ -n \"$DMTCP_TMPDIR\" ]; then\n"
  "    CURRENT_TMPDIR=$DMTCP_TMPDIR/dmtcp-`whoami`@`hostname`\n"
  "  elif [ -n \"$TMPDIR\" ]; then\n"
  "    CURRENT_TMPDIR=$TMPDIR/dmtcp-`whoami`@`hostname`\n"
  "  else\n"
  "    CURRENT_TMPDIR=/tmp/dmtcp-`whoami`@`hostname`\n"
  "  fi\n"
  "  if [ ! -d \"$CURRENT_TMPDIR\" ]; then\n"
  "    mkdir -p $CURRENT_TMPDIR\n"
  "  fi\n"
  "  # Create files with SLURM environment\n"
  "  for CKPT_FILE in $LOCAL_FILES; do\n"
  "    SUFFIX=${CKPT_FILE%%.dmtcp}\n"
  "    SLURM_ENV_FILE=$CURRENT_TMPDIR/slurm_env_${SUFFIX##*_}\n"
  "    echo \"DMTCP_SRUN_HELPER_ADDR=$DMTCP_SRUN_HELPER_ADDR\" >> $SLURM_ENV_FILE\n"
  "  done\n"
  "}\n\n\n";

static const char *usage =
  "usage_str='USAGE:\n"
  "  dmtcp_restart_script.sh [OPTIONS]\n\n"
  "OPTIONS:\n"
  "  --coord-host, -h, (environment variable DMTCP_COORD_HOST):\n"
  "      Hostname where dmtcp_coordinator is running\n"
  "  --coord-port, -p, (environment variable DMTCP_COORD_PORT):\n"
  "      Port where dmtcp_coordinator is running\n"
  "  --hostfile <arg0> :\n"
  "      Provide a hostfile (One host per line, \"#\" indicates comments)\n"
  "  --ckptdir, -d, (environment variable DMTCP_CHECKPOINT_DIR):\n"
  "      Directory to store checkpoint images\n"
  "      (default: use the same directory used in previous checkpoint)\n"
  "  --restartdir, -d, (environment variable DMTCP_RESTART_DIR):\n"
  "      Directory to read checkpoint images from\n"
  "  --tmpdir, -t, (environment variable DMTCP_TMPDIR):\n"
  "      Directory to store temporary files (default: $TMDPIR or /tmp)\n"
  "  --no-strict-checking:\n"
  "      Disable uid checking for checkpoint image. This allows the\n"
  "      checkpoint image to be restarted by a different user than the one\n"
  "      that created it.  And suppress warning about running as root.\n"
  "      (environment variable DMTCP_DISABLE_STRICT_CHECKING)\n"
  "  --interval, -i, (environment variable DMTCP_CHECKPOINT_INTERVAL):\n"
  "      Time in seconds between automatic checkpoints\n"
  "      (Default: Use pre-checkpoint value)\n"
  "  --coord-logfile PATH (environment variable DMTCP_COORD_LOG_FILENAME\n"
  "              Coordinator will dump its logs to the given file\n"
  "  --help:\n"
  "      Print this message and exit.\'\n"
  "\n\n"
;

static const char *cmdlineArgHandler =
  "if [ $# -gt 0 ]; then\n"
  "  while [ $# -gt 0 ]\n"
  "  do\n"
  "    if [ $1 = \"--help\" ]; then\n"
  "      echo \"$usage_str\"\n"
  "      exit\n"
  "    elif [ $# -ge 1 ]; then\n"
  "      case \"$1\" in\n"
  "        --coord-host|--host|-h)\n"
  "          coord_host=\"$2\"\n"
  "          shift; shift;;\n"
  "        --coord-port|--port|-p)\n"
  "          coord_port=\"$2\"\n"
  "          shift; shift;;\n"
  "        --coord-logfile)\n"
  "          DMTCP_COORD_LOGFILE=\"$2\"\n"
  "          shift; shift;;\n"
  "        --hostfile)\n"
  "          hostfile=\"$2\"\n"
  "          if [ ! -f \"$hostfile\" ]; then\n"
  "            echo \"ERROR: hostfile $hostfile not found\"\n"
  "            exit\n"
  "          fi\n"
  "          shift; shift;;\n"
  "        --restartdir|-d)\n"
  "          DMTCP_RESTART_DIR=$2\n"
  "          shift; shift;;\n"
  "        --ckptdir|-d)\n"
  "          DMTCP_CKPT_DIR=$2\n"
  "          shift; shift;;\n"
  "        --tmpdir|-t)\n"
  "          DMTCP_TMPDIR=$2\n"
  "          shift; shift;;\n"
  "        --no-strict-checking)\n"
  "          noStrictChecking=\"--no-strict-checking\"\n"
  "          shift;;\n"
  "        --interval|-i)\n"
  "          checkpoint_interval=$2\n"
  "          shift; shift;;\n"
  "        *)\n"
  "          echo \"$0: unrecognized option \'$1\'. See correct usage below\"\n"
  "          echo \"$usage_str\"\n"
  "          exit;;\n"
  "      esac\n"
  "    elif [ $1 = \"--help\" ]; then\n"
  "      echo \"$usage_str\"\n"
  "      exit\n"
  "    else\n"
  "      echo \"$0: Incorrect usage.  See correct usage below\"\n"
  "      echo\n"
  "      echo \"$usage_str\"\n"
  "      exit\n"
  "    fi\n"
  "  done\n"
  "fi\n\n"
;

static const char *singleHostProcessing =
  "ckpt_files=\"\"\n"
  "if [ ! -z \"$DMTCP_RESTART_DIR\" ]; then\n"
  "  for tmp in $given_ckpt_files; do\n"
  "    ckpt_files=\"$DMTCP_RESTART_DIR/$(basename $tmp) $ckpt_files\"\n"
  "  done\n"
  "else\n"
  "  ckpt_files=$given_ckpt_files\n"
  "fi\n\n"

  "coordinator_info=\"--coord-host $coord_host --coord-port $coord_port\"\n"

  "tmpdir=\n"
  "if [ ! -z \"$DMTCP_TMPDIR\" ]; then\n"
  "  tmpdir=\"--tmpdir $DMTCP_TMPDIR\"\n"
  "fi\n\n"

  "ckpt_dir=\n"
  "if [ ! -z \"$DMTCP_CKPT_DIR\" ]; then\n"
  "  ckpt_dir=\"--ckptdir $DMTCP_CKPT_DIR\"\n"
  "fi\n\n"

  "coord_logfile=\n"
  "if [ ! -z \"$DMTCP_COORD_LOGFILE\" ]; then\n"
  "  coord_logfile=\"--coord-logfile $DMTCP_COORD_LOGFILE\"\n"
  "fi\n\n"

  "exec $dmt_rstr_cmd $coordinator_info $ckpt_dir \\\n"
  "  $maybejoin --interval \"$checkpoint_interval\" $tmpdir $noStrictChecking $coord_logfile\\\n"
  "  $ckpt_files\n"
;

static const char *multiHostProcessing =
  "worker_ckpts_regexp=\\\n"
  "\'[^:]*::[ \\t\\n]*\\([^ \\t\\n]\\+\\)[ \\t\\n]*:\\([a-z]\\+\\):[ \\t\\n]*\\([^:]\\+\\)[ \\t\\n]*:\\([^:]\\+\\)\'\n\n"

  "worker_hosts=$(\\\n"
  "  echo $worker_ckpts | sed -e \'s/\'\"$worker_ckpts_regexp\"\'/\\1 /g\')\n"
  "restart_modes=$(\\\n"
  "  echo $worker_ckpts | sed -e \'s/\'\"$worker_ckpts_regexp\"\'/: \\2/g\')\n"
  "ckpt_files_groups=$(\\\n"
  "  echo $worker_ckpts | sed -e \'s/\'\"$worker_ckpts_regexp\"\'/: \\3/g\')\n"
  "remote_cmd=$(\\\n"
  "  echo $worker_ckpts | sed -e \'s/\'\"$worker_ckpts_regexp\"\'/: \\4/g\')\n"
  "\n"
  "if [ ! -z \"$hostfile\" ]; then\n"
  "  worker_hosts=$(\\\n"
  "    cat \"$hostfile\" | sed -e \'s/#.*//\' -e \'s/[ \\t\\r]*//\' -e \'/^$/ d\')\n"
  "fi\n\n"

  "localhost_ckpt_files_group=\n\n"

  "num_worker_hosts=$(echo $worker_hosts | wc -w)\n\n"

  "maybejoin=\n"
  "if [ \"$num_worker_hosts\" != \"1\" ]; then\n"
  "  maybejoin='--join-coordinator'\n"
  "fi\n\n"

  "for worker_host in $worker_hosts\n"
  "do\n\n"
  "  ckpt_files_group=$(\\\n"
  "    echo $ckpt_files_groups | sed -e \'s/[^:]*:[ \\t\\n]*\\([^:]*\\).*/\\1/\')\n"
  "  ckpt_files_groups=$(echo $ckpt_files_groups | sed -e \'s/[^:]*:[^:]*//\')\n"
  "\n"
  "  mode=$(echo $restart_modes | sed -e \'s/[^:]*:[ \\t\\n]*\\([^:]*\\).*/\\1/\')\n"
  "  restart_modes=$(echo $restart_modes | sed -e \'s/[^:]*:[^:]*//\')\n\n"
  "  remote_shell_cmd=$(echo $remote_cmd | sed -e \'s/[^:]*:[ \\t\\n]*\\([^:]*\\).*/\\1/\')\n"
  "  remote_cmd=$(echo $remote_cmd | sed -e \'s/[^:]*:[^:]*//\')\n\n"
  "  maybexterm=\n"
  "  maybebg=\n"
  "  case $mode in\n"
  "    bg) maybebg=\'bg\';;\n"
  "    xterm) maybexterm=xterm;;\n"
  "    fg) ;;\n"
  "    *) echo \"WARNING: Unknown Mode\";;\n"
  "  esac\n\n"
  "  if [ -z \"$ckpt_files_group\" ]; then\n"
  "    break;\n"
  "  fi\n\n"

  "  new_ckpt_files_group=\"\"\n"
  "  for tmp in $ckpt_files_group\n"
  "  do\n"
  "      if  [ ! -z \"$DMTCP_RESTART_DIR\" ]; then\n"
  "        tmp=$DMTCP_RESTART_DIR/$(basename $tmp)\n"
  "      fi\n"
  "      new_ckpt_files_group=\"$new_ckpt_files_group $tmp\"\n"
  "  done\n\n"

  "tmpdir=\n"
  "if [ ! -z \"$DMTCP_TMPDIR\" ]; then\n"
  "  tmpdir=\"--tmpdir $DMTCP_TMPDIR\"\n"
  "fi\n\n"

  "coord_logfile=\n"
  "if [ ! -z \"$DMTCP_COORD_LOGFILE\" ]; then\n"
  "  coord_logfile=\"--coord-logfile $DMTCP_COORD_LOGFILE\"\n"
  "fi\n\n"

  "  check_local $worker_host\n"
  "  if [ \"$is_local_node\" -eq 1 -o \"$num_worker_hosts\" == \"1\" ]; then\n"
  "    localhost_ckpt_files_group=\"$new_ckpt_files_group $localhost_ckpt_files_group\"\n"
  "    continue\n"
  "  fi\n"

  "  if [ -z $maybebg ]; then\n"
  "    $maybexterm /usr/bin/$remote_shell_cmd -t \"$worker_host\" \\\n"
  "      $dmt_rstr_cmd --coord-host \"$coord_host\""
                                             " --cord-port \"$coord_port\"\\\n"
  "      $ckpt_dir --join-coordinator --interval \"$checkpoint_interval\""
                                             " $tmpdir \\\n"
  "      $new_ckpt_files_group\n"
  "  else\n"
  "    $maybexterm /usr/bin/$remote_shell_cmd \"$worker_host\" \\\n"
  // In Open MPI 1.4, without this (sh -c ...), orterun hangs at the
  // end of the computation until user presses enter key.
  "      \"/bin/sh -c \'$dmt_rstr_cmd --coord-host $coord_host"
                                                " --coord-port $coord_port\\\n"
  "      $ckpt_dir --join-coordinator --interval \"$checkpoint_interval\""
                                                " $tmpdir \\\n"
  "      $new_ckpt_files_group\'\" &\n"
  "  fi\n\n"
  "done\n\n"
  "if [ -n \"$localhost_ckpt_files_group\" ]; then\n"
  "exec $dmt_rstr_cmd --coord-host \"$coord_host\""
  " --coord-port \"$coord_port\" $coord_logfile \\\n"
  "  $ckpt_dir $maybejoin --interval \"$checkpoint_interval\" $tmpdir $noStrictChecking $localhost_ckpt_files_group\n"
  "fi\n\n"

  "#wait for them all to finish\n"
  "wait\n"
;

string
writeScript(const string &ckptDir,
            bool uniqueCkptFilenames,
            const time_t &ckptTimeStamp,
            const uint32_t theCheckpointInterval,
            const int thePort,
            const UniquePid &compId,
            const map<string, vector<string> > &restartFilenames,
            const map<string, vector<string> >& rshCmdFileNames,
            const map<string, vector<string> >& sshCmdFileNames)
{
  ostringstream o;
  string uniqueFilename;

  o << string(ckptDir) << "/"
    << RESTART_SCRIPT_BASENAME << "_" << compId;
  if (uniqueCkptFilenames) {
    o << "_" << std::setw(5) << std::setfill('0') <<
        compId.computationGeneration();
  }
  o << "." << RESTART_SCRIPT_EXT;
  uniqueFilename = o.str();

  const bool isSingleHost = ((rshCmdFileNames.size() == 0) && (sshCmdFileNames.size() == 0) && (restartFilenames.size() == 1));

  map<string, vector<string> >::const_iterator host;

  size_t numPeers = 0;
  for (host = restartFilenames.begin();
       host != restartFilenames.end();
       host++) {
    numPeers += host->second.size();
  }
  for (host = rshCmdFileNames.begin();
       host != rshCmdFileNames.end();
       host++) {
    numPeers += host->second.size();
  }
  for (host = sshCmdFileNames.begin();
       host != sshCmdFileNames.end();
       host++) {
    numPeers += host->second.size();
  }


  vector<string>::const_iterator file;

  char hostname[80];
  char timestamp[80];
  gethostname(hostname, 80);

  JTRACE("writing restart script") (uniqueFilename);

  FILE *fp = fopen(uniqueFilename.c_str(), "w");
  JASSERT(fp != 0)(JASSERT_ERRNO)(uniqueFilename)
  .Text("failed to open file");

  fprintf(fp, "%s", header);
  fprintf(fp, "%s", checkLocal);
  fprintf(fp, "%s", slurmHelperContactFunction);
  fprintf(fp, "%s", usage);

  ctime_r(&ckptTimeStamp, timestamp);

  // Remove the trailing '\n'
  timestamp[strlen(timestamp) - 1] = '\0';
  fprintf(fp, "ckpt_timestamp=\"%s\"\n\n", timestamp);

  fprintf ( fp, "remote_shell_cmd=\"ssh\"\n\n");

  fprintf(fp,
          "coord_host=$" ENV_VAR_NAME_HOST "\n"
          "if test -z \"$" ENV_VAR_NAME_HOST "\"; then\n"
          "  coord_host=%s\nfi\n\n"
          "coord_port=$" ENV_VAR_NAME_PORT "\n"
          "if test -z \"$" ENV_VAR_NAME_PORT "\"; then\n"
          "  coord_port=%d\nfi\n\n"
          "checkpoint_interval=$" ENV_VAR_CKPT_INTR "\n"
          "if test -z \"$" ENV_VAR_CKPT_INTR "\"; then\n"
          "  checkpoint_interval=%d\nfi\n"
          "export DMTCP_CHECKPOINT_INTERVAL=${checkpoint_interval}\n\n",
          hostname,
          thePort,
          theCheckpointInterval);

  fprintf(fp, "%s", cmdlineArgHandler);

  fprintf(fp,
          "dmt_rstr_cmd=%s/" DMTCP_RESTART_CMD "\n"
          "which $dmt_rstr_cmd > /dev/null 2>&1"
          " || dmt_rstr_cmd=" DMTCP_RESTART_CMD "\n"
          "which $dmt_rstr_cmd > /dev/null 2>&1"
          " || echo \"$0: $dmt_rstr_cmd not found\"\n"
          "which $dmt_rstr_cmd > /dev/null 2>&1 || exit 1\n\n",
          jalib::Filesystem::GetProgramDir().c_str());

  fprintf(fp, "# Number of hosts in the computation = %zu\n"
          "# Number of processes in the computation = %zu\n\n",
          (restartFilenames.size()
            + sshCmdFileNames.size() + rshCmdFileNames.size()),
          numPeers);

  if (isSingleHost) {
    JTRACE("Single HOST");

    host = restartFilenames.begin();
    ostringstream o;
    for (file = host->second.begin(); file != host->second.end(); ++file) {
      o << " " << *file;
    }
    fprintf(fp, "given_ckpt_files=\"%s\"\n\n", o.str().c_str());

    fprintf(fp, "%s", singleHostProcessing);
  } else {
    fprintf(fp, "%s",
            "# SYNTAX:\n"
            "#  :: <HOST> :<MODE>: <CHECKPOINT_IMAGE> ... :<REMOTE SHELL CMD>\n"
            "# Host names and filenames must not include \':\'\n"
            "# At most one fg (foreground) mode allowed; it must be last.\n"
            "# \'maybexterm\' and \'maybebg\' are set from <MODE>.\n");

    fprintf(fp, "%s", "worker_ckpts=\'");
    for ( host=rshCmdFileNames.begin(); host!=rshCmdFileNames.end(); ++host ) {
      fprintf ( fp, "\n :: %s :bg:", host->first.c_str() );
      for ( file=host->second.begin(); file!=host->second.end(); ++file ) {
        fprintf( fp," %s", file->c_str() );
      }
      fprintf(fp, " : rsh");
    }
    for ( host=sshCmdFileNames.begin(); host!=sshCmdFileNames.end(); ++host ) {
      fprintf ( fp, "\n :: %s :bg:", host->first.c_str() );
      for ( file=host->second.begin(); file!=host->second.end(); ++file ) {
        fprintf( fp," %s", file->c_str() );
      }
      fprintf(fp, " : ssh");
    }
    string defaultShellType;
    for (host = restartFilenames.begin();
         host != restartFilenames.end();
         ++host) {
      fprintf(fp, "\n :: %s :bg:", host->first.c_str());
      for (file = host->second.begin(); file != host->second.end(); ++file) {
        fprintf(fp, " %s", file->c_str());
      }

      /*
       * Process which are launched on local machine without using an rsh/ssh
       * command are part of restartFilenames. So we have to choose the default
       * shell command for them.  Search if some process is launched on the
       * local machine via rsh/ssh command and use that. In case it's not found,
       * give preference to ssh when no rsh command is found.
       */

      if(sshCmdFileNames.find(host->first) != sshCmdFileNames.end()) {
        defaultShellType = "ssh";
      } else if(rshCmdFileNames.find(host->first) != rshCmdFileNames.end()) {
        defaultShellType = "rsh";
      } else {
        defaultShellType = rshCmdFileNames.empty() ? "ssh" : "rsh";
      }
      fprintf (fp, " : %s", defaultShellType.c_str());
    }
    fprintf(fp, "%s", "\n\'\n\n");

    fprintf(fp,
            "# Check for resource manager\n"
            "ibrun_path=$(which ibrun 2> /dev/null)\n"
            "if [ ! -n \"$ibrun_path\" ]; then\n"
            "  discover_rm_path=$(which dmtcp_discover_rm 2> /dev/null)\n"
            "  if [ -n \"$discover_rm_path\" ]; then\n"
            "    eval $(dmtcp_discover_rm -t)\n"
            "    srun_path=$(which srun 2> /dev/null)\n"
            "    llaunch=`which dmtcp_rm_loclaunch`\n"
            "    if [ $RES_MANAGER = \"SLURM\" ] && [ -n \"$srun_path\" ]; then\n"
            "      eval $(dmtcp_discover_rm -n \"$worker_ckpts\")\n"
            "      if [ -n \"$DMTCP_DISCOVER_RM_ERROR\" ]; then\n"
            "          echo \"Restart error: $DMTCP_DISCOVER_RM_ERROR\"\n"
            "          echo \"Allocated resources: $manager_resources\"\n"
            "          exit 0\n"
            "      fi\n"
            "      export DMTCP_REMLAUNCH_NODES=$DMTCP_REMLAUNCH_NODES\n"
            "      bound=$(($DMTCP_REMLAUNCH_NODES - 1))\n"
            "      for i in $(seq 0 $bound); do\n"
            "        eval \"val=\\${DMTCP_REMLAUNCH_${i}_SLOTS}\"\n"
            "        export DMTCP_REMLAUNCH_${i}_SLOTS=\"$val\"\n"
            "        bound2=$(($val - 1))\n"
            "        for j in $(seq 0 $bound2); do\n"
            "          eval \"ckpts=\\${DMTCP_REMLAUNCH_${i}_${j}}\"\n"
            "          export DMTCP_REMLAUNCH_${i}_${j}=\"$ckpts\"\n"
            "        done\n"
            "      done\n"
            "      if [ \"$DMTCP_DISCOVER_PM_TYPE\" = \"HYDRA\" ]; then\n"
            "        export DMTCP_SRUN_HELPER_SYNCFILE=`mktemp ./tmp.XXXXXXXXXX`\n"
            "        rm $DMTCP_SRUN_HELPER_SYNCFILE\n"
            "        dmtcp_srun_helper -r $srun_path \"$llaunch\"\n"
            "        if [ ! -f $DMTCP_SRUN_HELPER_SYNCFILE ]; then\n"
            "          echo \"Error launching application\"\n"
            "          exit 1\n"
            "        fi\n"
            "        # export helper contact info\n"
            "        . $DMTCP_SRUN_HELPER_SYNCFILE\n"
            "        pass_slurm_helper_contact \"$DMTCP_LAUNCH_CKPTS\"\n"
            "        rm $DMTCP_SRUN_HELPER_SYNCFILE\n"
            "        dmtcp_restart --join-coordinator"
            " --coord-host $DMTCP_COORD_HOST --coord-port $DMTCP_COORD_PORT"
            " $DMTCP_LAUNCH_CKPTS\n"
            "      else\n"
            "        DMTCP_REMLAUNCH_0_0=\"$DMTCP_REMLAUNCH_0_0"
            " $DMTCP_LAUNCH_CKPTS\"\n"
            "        $srun_path \"$llaunch\"\n"
            "      fi\n"
            "      exit 0\n"
            "    elif [ $RES_MANAGER = \"TORQUE\" ]; then\n"
            "      #eval $(dmtcp_discover_rm \"$worker_ckpts\")\n"
            "      #if [ -n \"$new_worker_ckpts\" ]; then\n"
            "      #  worker_ckpts=\"$new_worker_ckpts\"\n"
            "      #fi\n"
            "      eval $(dmtcp_discover_rm -n \"$worker_ckpts\")\n"
            "      if [ -n \"$DMTCP_DISCOVER_RM_ERROR\" ]; then\n"
            "          echo \"Restart error: $DMTCP_DISCOVER_RM_ERROR\"\n"
            "          echo \"Allocated resources: $manager_resources\"\n"
            "          exit 0\n"
            "      fi\n"
            "      arguments=\"PATH=$PATH DMTCP_COORD_HOST=$DMTCP_COORD_HOST"
            " DMTCP_COORD_PORT=$DMTCP_COORD_PORT\"\n"
            "      arguments=$arguments\" DMTCP_CHECKPOINT_INTERVAL=$DMTCP_CHECKPOINT_INTERVAL\"\n"
            "      arguments=$arguments\" DMTCP_TMPDIR=$DMTCP_TMPDIR\"\n"
            "      arguments=$arguments\" DMTCP_REMLAUNCH_NODES=$DMTCP_REMLAUNCH_NODES\"\n"
            "      bound=$(($DMTCP_REMLAUNCH_NODES - 1))\n"
            "      for i in $(seq 0 $bound); do\n"
            "        eval \"val=\\${DMTCP_REMLAUNCH_${i}_SLOTS}\"\n"
            "        arguments=$arguments\" DMTCP_REMLAUNCH_${i}_SLOTS=\\\"$val\\\"\"\n"
            "        bound2=$(($val - 1))\n"
            "        for j in $(seq 0 $bound2); do\n"
            "          eval \"ckpts=\\${DMTCP_REMLAUNCH_${i}_${j}}\"\n"
            "          arguments=$arguments\" DMTCP_REMLAUNCH_${i}_${j}=\\\"$ckpts\\\"\"\n"
            "        done\n"
            "      done\n"
            "      pbsdsh -u \"$llaunch\" \"$arguments\"\n"
            "      exit 0\n"
            "    fi\n"
            "  fi\n"
            "fi\n"
            "\n\n"
            );

    fprintf(fp, "%s", multiHostProcessing);
  }

  fclose(fp);
  {
    string filename = RESTART_SCRIPT_BASENAME "." RESTART_SCRIPT_EXT;
    string dirname = jalib::Filesystem::DirName(uniqueFilename);
    int dirfd = open(dirname.c_str(), O_DIRECTORY | O_RDONLY);
    JASSERT(dirfd != -1) (dirname) (JASSERT_ERRNO);

    /* Set execute permission for user. */
    struct stat buf;
    JASSERT(::stat(uniqueFilename.c_str(), &buf) == 0);
    JASSERT(chmod(uniqueFilename.c_str(), buf.st_mode | S_IXUSR) == 0);

    // Create a symlink from
    // dmtcp_restart_script.sh -> dmtcp_restart_script_<curCompId>.sh
    unlink(filename.c_str());
    JTRACE("linking \"dmtcp_restart_script.sh\" filename to uniqueFilename")
      (filename) (dirname) (uniqueFilename);

    // FIXME:  Handle error case of symlink()
    JWARNING(symlinkat(basename(uniqueFilename.c_str()), dirfd,
                       filename.c_str()) == 0) (JASSERT_ERRNO);
    JASSERT(close(dirfd) == 0);
  }
  return uniqueFilename;
}
} // namespace dmtcp {
} // namespace RestartScript {
