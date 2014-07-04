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

/****************************************************************************
 * Coordinator code logic:                                                  *
 * main calls monitorSockets, which acts as a top level event loop.         *
 * monitorSockets calls:  onConnect, onData, onDisconnect, onTimeoutInterval*
 *   when client or dmtcp_command talks to coordinator.                     *
 * onConnect and onData receive a socket parameter, read msg, and pass to:  *
 *   handleUserCommand, which takes single char arg ('s', 'c', 'k', 'q', ...)*
 * handleUserCommand calls broadcastMessage to send data back               *
 * any message sent by broadcastMessage takes effect only on returning      *
 *   back up to top level monitorSockets                                    *
 * Hence, even for checkpoint, handleUserCommand just changes state,        *
 *   broadcasts an initial checkpoint command, and then returns to top      *
 *   level.  Replies from clients then drive further state changes.        *
 * The prefix command 'b' (blocking) from dmtcp_command modifies behavior   *
 *   of 'c' so that the reply to dmtcp_command happens only when clients    *
 *   are back in RUNNING state.                                             *
 * The states for a worker (client) are:                                    *
 * Checkpoint: RUNNING -> SUSPENDED -> FD_LEADER_ELECTION -> DRAINED        *
 *       	  -> CHECKPOINTED -> NAME_SERVICE_DATA_REGISTERED           *
 *                -> DONE_QUERYING -> REFILLED -> RUNNING		    *
 * Restart:    RESTARTING -> CHECKPOINTED -> NAME_SERVICE_DATA_REGISTERED   *
 *                -> DONE_QUERYING -> REFILLED -> RUNNING	            *
 * If debugging, set gdb breakpoint on:					    *
 *   dmtcp::DmtcpCoordinator::onConnect					    *
 *   dmtcp::DmtcpCoordinator::onData					    *
 *   dmtcp::DmtcpCoordinator::handleUserCommand				    *
 *   dmtcp::DmtcpCoordinator::broadcastMessage				    *
 ****************************************************************************/

#include <stdlib.h>
#include "dmtcp_coordinator.h"
#include "constants.h"
#include "protectedfds.h"
#include "dmtcpmessagetypes.h"
#include "lookup_service.h"
#include "syscallwrappers.h"
#include "util.h"
#include "../jalib/jconvert.h"
#include "../jalib/jtimer.h"
#include "../jalib/jfilesystem.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <algorithm>
#include <iomanip>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#undef min
#undef max

#define BINARY_NAME "dmtcp_coordinator"

using namespace dmtcp;

static const char* theHelpMessage =
  "COMMANDS:\n"
  "  l : List connected nodes\n"
  "  s : Print status message\n"
  "  c : Checkpoint all nodes\n"
  "  i : Print current checkpoint interval\n"
  "      (To change checkpoint interval, use dmtcp_command)\n"
  "  k : Kill all nodes\n"
  "  q : Kill all nodes and quit\n"
  "  ? : Show this message\n"
  "\n"
;

static const char* theUsage =
  "Usage: dmtcp_coordinator [OPTIONS] [port]\n"
  "Coordinates checkpoints between multiple processes.\n\n"
  "Options:\n"
  "  -p, --port PORT_NUM (environment variable DMTCP_PORT)\n"
  "      Port to listen on (default: 7779)\n"
  "  --port-file filename\n"
  "      File to write listener port number.\n"
  "      (Useful with '--port 0', which is used to assign a random port)\n"
  "  --ckptdir (environment variable DMTCP_CHECKPOINT_DIR):\n"
  "      Directory to store dmtcp_restart_script.sh (default: ./)\n"
  "  --tmpdir (environment variable DMTCP_TMPDIR):\n"
  "      Directory to store temporary files (default: env var TMDPIR or /tmp)\n"
  "  --exit-on-last\n"
  "      Exit automatically when last client disconnects\n"
  "  --exit-after-ckpt\n"
  "      Exit automatically after checkpoint is created\n"
  "  --daemon\n"
  "      Run silently in the background after detaching from the parent process.\n"
  "  -i, --interval (environment variable DMTCP_CHECKPOINT_INTERVAL):\n"
  "      Time in seconds between automatic checkpoints\n"
  "      (default: 0, disabled)\n"
  "  -q, --quiet \n"
  "      Skip copyright notice.\n"
  "  --help:\n"
  "      Print this message and exit.\n"
  "  --version:\n"
  "      Print version information and exit.\n"
  "\n"
  "COMMANDS:\n"
  "      type '?<return>' at runtime for list\n"
  "\n"
  HELP_AND_CONTACT_INFO
  "\n"
;


static const char* theRestartScriptHeader =
  "#!/bin/bash\n\n"
  "set -m # turn on job control\n\n"
  "#This script launches all the restarts in the background.\n"
  "#Suggestions for editing:\n"
  "#  1. For those processes executing on the localhost, remove\n"
  "#     'ssh <hostname> from the start of the line. \n"
  "#  2. If using ssh, verify that ssh does not require passwords or other\n"
  "#     prompts.\n"
  "#  3. Verify that the dmtcp_restart command is in your path on all hosts,\n"
  "#     otherwise set the remote_prefix appropriately.\n"
  "#  4. Verify DMTCP_HOST and DMTCP_PORT match the location of the\n"
  "#     dmtcp_coordinator. If necessary, add\n"
  "#     'DMTCP_PORT=<dmtcp_coordinator port>' after 'DMTCP_HOST=<...>'.\n"
  "#  5. Remove the '&' from a line if that process reads STDIN.\n"
  "#     If multiple processes read STDIN then prefix the line with\n"
  "#     'xterm -hold -e' and put '&' at the end of the line.\n"
  "#  6. Processes on same host can be restarted with single dmtcp_restart\n"
  "#     command.\n\n\n"
;

static const char* theRestartScriptCheckLocal =
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


static const char* theRestartScriptUsage =
  "usage_str='USAGE:\n"
  "  dmtcp_restart_script.sh [OPTIONS]\n\n"
  "OPTIONS:\n"
  "  --host, -h, (environment variable DMTCP_HOST):\n"
  "      Hostname where dmtcp_coordinator is running\n"
  "  --port, -p, (environment variable DMTCP_PORT):\n"
  "      Port where dmtcp_coordinator is running\n"
  "  --hostfile <arg0> :\n"
  "      Provide a hostfile (One host per line, \"#\" indicates comments)\n"
  "  --ckptdir, -d, (environment variable DMTCP_CHECKPOINT_DIR):\n"
  "      Directory to store checkpoint images\n"
  "      (default: use the same directory used in previous checkpoint)\n"
  "  --restartdir, -d, (environment variable DMTCP_RESTART_DIR):\n"
  "      Directory to read checkpoint images from\n"
  "  --tmpdir, -t, (environment variable DMTCP_TMPDIR):\n"
  "      Directory to store temporary files \n"
  "        (default: $TMDPIR/dmtcp-$USER@$HOST or /tmp/dmtcp-$USER@$HOST)\n"
  "  --no-strict-uid-checking:\n"
  "      Disable uid checking for the checkpoint image.  This allows the\n"
  "        checkpoint image to be restarted by a different user than the one\n"
  "        that create it. (environment variable DMTCP_DISABLE_UID_CHECKING)\n"
  "  --interval, -i, (environment variable DMTCP_CHECKPOINT_INTERVAL):\n"
  "      Time in seconds between automatic checkpoints\n"
  "      (Default: Use pre-checkpoint value)\n"
  "  --help:\n"
  "      Print this message and exit.\'\n"
  "\n\n"
;

static const char* theRestartScriptCmdlineArgHandler =
  "if [ $# -gt 0 ]; then\n"
  "  while [ $# -gt 0 ]\n"
  "  do\n"
  "    if [ $1 = \"--help\" ]; then\n"
  "      echo \"$usage_str\"\n"
  "      exit\n"
  "    elif [ $# -ge 1 ]; then\n"
  "      case \"$1\" in \n"
  "        --host|-h)\n"
  "          coord_host=\"$2\"\n"
  "          shift; shift;;\n"
  "        --port|-p)\n"
  "          coord_port=\"$2\"\n"
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
  "        --no-strict-uid-checking)\n"
  "          noStrictUidChecking=\"--no-strict-uid-checking\"\n"
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

static const char* theRestartScriptSingleHostProcessing =
  "ckpt_files=\"\"\n"
  "if [ ! -z \"$DMTCP_RESTART_DIR\" ]; then\n"
  "  for tmp in $given_ckpt_files; do\n"
  "    ckpt_files=\"$DMTCP_RESTART_DIR/$(basename $tmp) $ckpt_files\"\n"
  "  done\n"
  "else\n"
  "  ckpt_files=$given_ckpt_files\n"
  "fi\n\n"

  "coordinator_info=\"--host $coord_host --port $coord_port\"\n"

  "tmpdir=\n"
  "if [ ! -z \"$DMTCP_TMPDIR\" ]; then\n"
  "  tmpdir=\"--tmpdir $DMTCP_TMPDIR\"\n"
  "fi\n\n"

  "ckpt_dir=\n"
  "if [ ! -z \"$DMTCP_CKPT_DIR\" ]; then\n"
  "  ckpt_dir=\"--ckptdir $DMTCP_CKPT_DIR\"\n"
  "fi\n\n"

  "exec $dmt_rstr_cmd $coordinator_info $ckpt_dir \\\n"
  "  $maybejoin --interval \"$checkpoint_interval\" $tmpdir $noStrictUidChecking \\\n"
  "  $ckpt_files\n"
;

static const char* theRestartScriptMultiHostProcessing =
  "worker_ckpts_regexp=\\\n"
  "\'[^:]*::[ \\t\\n]*\\([^ \\t\\n]\\+\\)[ \\t\\n]*:\\([a-z]\\+\\):[ \\t\\n]*\\([^:]\\+\\)\'\n\n"

  "worker_hosts=$(\\\n"
  "  echo $worker_ckpts | sed -e \'s/\'\"$worker_ckpts_regexp\"\'/\\1 /g\')\n"
  "restart_modes=$(\\\n"
  "  echo $worker_ckpts | sed -e \'s/\'\"$worker_ckpts_regexp\"\'/: \\2/g\')\n"
  "ckpt_files_groups=$(\\\n"
  "  echo $worker_ckpts | sed -e \'s/\'\"$worker_ckpts_regexp\"\'/: \\3/g\')\n"
  "\n"
  "if [ ! -z \"$hostfile\" ]; then\n"
  "  worker_hosts=$(\\\n"
  "    cat \"$hostfile\" | sed -e \'s/#.*//\' -e \'s/[ \\t\\r]*//\' -e \'/^$/ d\')\n"
  "fi\n\n"

  "localhost_ckpt_files_group=\n\n"

  "num_worker_hosts=$(echo $worker_hosts | wc -w)\n\n"

  "maybejoin=\n"
  "if [ \"$num_worker_hosts\" != \"1\" ]; then\n"
  "  maybejoin='--join'\n"
  "fi\n\n"

  "for worker_host in $worker_hosts\n"
  "do\n\n"
  "  ckpt_files_group=$(\\\n"
  "    echo $ckpt_files_groups | sed -e \'s/[^:]*:[ \\t\\n]*\\([^:]*\\).*/\\1/\')\n"
  "  ckpt_files_groups=$(echo $ckpt_files_groups | sed -e \'s/[^:]*:[^:]*//\')\n"
  "\n"
  "  mode=$(echo $restart_modes | sed -e \'s/[^:]*:[ \\t\\n]*\\([^:]*\\).*/\\1/\')\n"
  "  restart_modes=$(echo $restart_modes | sed -e \'s/[^:]*:[^:]*//\')\n\n"
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

  "  check_local $worker_host\n"
  "  if [ \"$is_local_node\" -eq 1 -o \"$num_worker_hosts\" == \"1\" ]; then\n"
  "    localhost_ckpt_files_group=\"$new_ckpt_files_group\"\n"
  "    continue\n"
  "  fi\n"

  "  if [ -z $maybebg ]; then\n"
  "    $maybexterm /usr/bin/ssh -t \"$worker_host\" \\\n"
  "      $remote_dmt_rstr_cmd --host \"$coord_host\" --port \"$coord_port\"\\\n"
  "      $ckpt_dir --join --interval \"$checkpoint_interval\" $tmpdir \\\n"
  "      $new_ckpt_files_group\n"
  "  else\n"
  "    $maybexterm /usr/bin/ssh \"$worker_host\" \\\n"
  // In Open MPI 1.4, without this (sh -c ...), orterun hangs at the
  // end of the computation until user presses enter key.
  "      \"/bin/sh -c \'$remote_dmt_rstr_cmd --host $coord_host --port $coord_port\\\n"
  "      $ckpt_dir --join --interval \"$checkpoint_interval\" $tmpdir \\\n"
  "      $new_ckpt_files_group\'\" &\n"
  "  fi\n\n"
  "done\n\n"
  "if [ -n \"$localhost_ckpt_files_group\" ]; then\n"
  "exec $dmt_rstr_cmd --host \"$coord_host\" --port \"$coord_port\" \\\n"
  "  $ckpt_dir $maybejoin --interval \"$checkpoint_interval\" $tmpdir $noStrictUidChecking $localhost_ckpt_files_group\n"
  "fi\n\n"

  "#wait for them all to finish\n"
  "wait\n"
;

static int thePort = -1;
static dmtcp::string thePortFile;

static bool exitOnLast = false;
static bool blockUntilDone = false;
static bool exitAfterCkpt = false;
static int blockUntilDoneRemote = -1;

static dmtcp::DmtcpCoordinator prog;

/* The coordinator can receive a second checkpoint request while processing the
 * first one.  If the second request comes at a point where the coordinator has
 * broadcasted DMTCP_DO_SUSPEND message but the workers haven't replied, the
 * coordinator sends another DMTCP_DO_SUSPEND message.  The workers having
 * replied to the first DMTCP_DO_SUSPEND message (by suspending all the user
 * threads) are waiting for the next message (DMT_DO_FD_LEADER_ELECTION or
 * DMT_KILL_PEER), however they receive DMT_DO_SUSPEND message and thus exit()
 * indicating an error.
 * The fix to this problem is to introduce a global
 * variable "workersRunningAndSuspendMsgSent" which, as the name implies,
 * indicates that the DMT_DO_SUSPEND message has been sent and the coordinator
 * is waiting for replies from the workers.  If this variable is set, the
 * coordinator will not process another checkpoint request.
*/
static bool workersRunningAndSuspendMsgSent = false;

static bool killInProgress = false;
static bool uniqueCkptFilenames = false;

/* If dmtcp_launch/dmtcp_restart specifies '-i', theCheckpointInterval
 * will be reset accordingly (valid for current computation).  If dmtcp_command
 * specifies '-i' (or if user interactively invokes 'i' in coordinator),
 * then both theCheckpointInterval and theDefaultCheckpointInterval are set.
 * A value of '0' means:  never checkpoint (manual checkpoint only).
 */
static uint32_t theCheckpointInterval = 0; /* Current checkpoint interval */
static uint32_t theDefaultCheckpointInterval = 0; /* Reset to this on new comp. */
static struct timespec startTime;
static bool isRestarting = false;

const int STDIN_FD = fileno ( stdin );

JTIMER ( checkpoint );
JTIMER ( restart );

static UniquePid compId;
static int numPeers = -1;
static time_t curTimeStamp = -1;
static time_t ckptTimeStamp = -1;

static dmtcp::LookupService lookupService;
static dmtcp::string localHostName;
static dmtcp::string localPrefix;
static dmtcp::string remotePrefix;

static dmtcp::string coordHostname;
static struct in_addr localhostIPAddr;

static dmtcp::string ckptDir;

#define MAX_EVENTS 10000
struct epoll_event events[MAX_EVENTS];
int epollFd;
static jalib::JSocket *listenSock = NULL;

static void removeStaleSharedAreaFile();
static void preExitCleanup();

static pid_t _nextVirtualPid = INITIAL_VIRTUAL_PID;

static int theNextClientNumber = 1;
dmtcp::vector<CoordClient*> clients;

dmtcp::CoordClient::CoordClient(const jalib::JSocket& sock,
                                const struct sockaddr_storage *addr,
                                socklen_t len,
                                dmtcp::DmtcpMessage &hello_remote,
				int isNSWorker)
  : _sock(sock)
{
  _isNSWorker = isNSWorker;
  _realPid = hello_remote.realPid;
  _clientNumber = theNextClientNumber++;
  _identity = hello_remote.from;
  _state = hello_remote.state;
  struct sockaddr_in *in = (struct sockaddr_in*) addr;
  _ip = inet_ntoa(in->sin_addr);
}

void dmtcp::CoordClient::readProcessInfo(dmtcp::DmtcpMessage& msg)
{
  if (msg.extraBytes > 0) {
    char* extraData = new char[msg.extraBytes];
    _sock.readAll(extraData, msg.extraBytes);
    _hostname = extraData;
    _progname = extraData + _hostname.length() + 1;
    if (msg.extraBytes > _hostname.length() + _progname.length() + 2) {
      _prefixDir = extraData + _hostname.length() + _progname.length() + 2;
    }
    delete [] extraData;
  }
}

pid_t dmtcp::DmtcpCoordinator::getNewVirtualPid()
{
  pid_t pid = -1;
  JASSERT(_virtualPidToClientMap.size() < MAX_VIRTUAL_PID/1000)
    .Text("Exceeded maximum number of processes allowed");
  while (1) {
    pid = _nextVirtualPid;
    _nextVirtualPid += 1000;
    if (_nextVirtualPid > MAX_VIRTUAL_PID) {
      _nextVirtualPid = INITIAL_VIRTUAL_PID;
    }
    if (_virtualPidToClientMap.find(pid) == _virtualPidToClientMap.end()) {
      break;
    }
  }
  JASSERT(pid != -1) .Text("Not Reachable");
  return pid;
}

void dmtcp::DmtcpCoordinator::handleUserCommand(char cmd, DmtcpMessage* reply /*= NULL*/)
{
  if (reply != NULL) reply->coordCmdStatus = CoordCmdStatus::NOERROR;

  switch ( cmd ){
  case 'b': case 'B':  // prefix blocking command, prior to checkpoint command
    JTRACE ( "blocking checkpoint beginning..." );
    blockUntilDone = true;
    break;
  case 'x': case 'X':  // prefix exit command, prior to checkpoint command
    JTRACE ( "Will exit after creating the checkpoint..." );
    exitAfterCkpt = true;
    break;
  case 'c': case 'C':
    JTRACE ( "checkpointing..." );
    if(startCheckpoint()){
      if (reply != NULL) reply->numPeers = getStatus().numPeers;
    }else{
      if (reply != NULL) reply->coordCmdStatus = CoordCmdStatus::ERROR_NOT_RUNNING_STATE;
    }
    break;
  case 'i': case 'I':
    JTRACE("setting timeout interval...");
    updateCheckpointInterval ( theCheckpointInterval );
    if (theCheckpointInterval == 0)
      printf("Current Checkpoint Interval:"
             " Disabled (checkpoint manually instead)\n");
    else
      printf("Current Checkpoint Interval: %d\n", theCheckpointInterval);
    if (theDefaultCheckpointInterval == 0)
      printf("Default Checkpoint Interval:"
             " Disabled (checkpoint manually instead)\n");
    else
      printf("Default Checkpoint Interval: %d\n", theDefaultCheckpointInterval);
    break;
  case 'l': case 'L':
  case 't': case 'T':
    JASSERT_STDERR << "Client List:\n";
    JASSERT_STDERR << "#, PROG[virtPID:realPID]@HOST, DMTCP-UNIQUEPID, STATE\n";
    for (size_t i = 0; i < clients.size(); i++) {
      JASSERT_STDERR << clients[i]->clientNumber()
        << ", " << clients[i]->progname()
        << "[" << clients[i]->identity().pid() << ":" << clients[i]->realPid()
        << "]@" << clients[i]->hostname()
#ifdef PRINT_REMOTE_IP
        << "(" << clients[i]->ip() << ")"
#endif
        << ", " << clients[i]->identity()
        << ", " << clients[i]->state().toString()
        << '\n';
    }
    break;
  case 'q': case 'Q':
  {
    JNOTE ( "killing all connected peers and quitting ..." );
    broadcastMessage ( DMT_KILL_PEER );
    JASSERT_STDERR << "DMTCP coordinator exiting... (per request)\n";
    for (size_t i = 0; i < clients.size(); i++) {
      clients[i]->sock().close();
    }
    listenSock->close();
    preExitCleanup();
    JTRACE ("Exiting ...");
    exit ( 0 );
    break;
  }
  case 'k': case 'K':
    JNOTE ( "Killing all connected Peers..." );
    //FIXME: What happens if a 'k' command is followed by a 'c' command before
    //       the *real* broadcast takes place?         --Kapil
    broadcastMessage ( DMT_KILL_PEER );
    break;
  case 'h': case 'H': case '?':
    JASSERT_STDERR << theHelpMessage;
    break;
  case 's': case 'S':
    {
      ComputationStatus s = getStatus();
      bool running = s.minimumStateUnanimous &&
		     s.minimumState==WorkerState::RUNNING;
      if (reply == NULL){
        printf("Status...\n");
        printf("NUM_PEERS=%d\n", s.numPeers);
        printf("RUNNING=%s\n", (running?"yes":"no"));
        fflush(stdout);
        if (!running) {
          JTRACE("raw status")(s.minimumState)(s.minimumStateUnanimous);
        }
      } else {
        reply->numPeers = s.numPeers;
        reply->isRunning = running;
      }
    }
    break;
  case ' ': case '\t': case '\n': case '\r':
    //ignore whitespace
    break;
  default:
    JTRACE("unhandled user command")(cmd);
    if (reply != NULL){
      reply->coordCmdStatus = CoordCmdStatus::ERROR_INVALID_COMMAND;
    }
  }
  return;
}

void dmtcp::DmtcpCoordinator::updateMinimumState(dmtcp::WorkerState oldState)
{
  WorkerState newState = minimumState();

  if ( oldState == WorkerState::RUNNING
       && newState == WorkerState::SUSPENDED )
  {
    JNOTE ( "locking all nodes" );
    broadcastMessage(DMT_DO_FD_LEADER_ELECTION, getStatus().numPeers );
  }
  if ( oldState == WorkerState::SUSPENDED
       && newState == WorkerState::FD_LEADER_ELECTION )
  {
    JNOTE ( "draining all nodes" );
    broadcastMessage ( DMT_DO_DRAIN );
  }
  if ( oldState == WorkerState::FD_LEADER_ELECTION
       && newState == WorkerState::DRAINED )
  {
    JNOTE ( "checkpointing all nodes" );
    broadcastMessage ( DMT_DO_CHECKPOINT );
  }

#ifdef COORD_NAMESERVICE
  if ( oldState == WorkerState::DRAINED
       && newState == WorkerState::CHECKPOINTED )
  {
    writeRestartScript();
    if (exitAfterCkpt) {
      JNOTE("Checkpoint Done. Killing all peers.");
      broadcastMessage(DMT_KILL_PEER);
      exitAfterCkpt = false;
    } else {
      JNOTE ( "building name service database" );
      lookupService.reset();
      broadcastMessage ( DMT_DO_REGISTER_NAME_SERVICE_DATA );
    }
  }
  if ( oldState == WorkerState::RESTARTING
       && newState == WorkerState::CHECKPOINTED )
  {
    JTIMER_STOP ( restart );

    lookupService.reset();
    JNOTE ( "building name service database (after restart)" );
    broadcastMessage ( DMT_DO_REGISTER_NAME_SERVICE_DATA );
  }
  if ( oldState == WorkerState::CHECKPOINTED
       && newState == WorkerState::NAME_SERVICE_DATA_REGISTERED ){
    JNOTE ( "entertaining queries now" );
    broadcastMessage ( DMT_DO_SEND_QUERIES );
  }
  if ( oldState == WorkerState::NAME_SERVICE_DATA_REGISTERED
       && newState == WorkerState::DONE_QUERYING ){
    JNOTE ( "refilling all nodes" );
    broadcastMessage ( DMT_DO_REFILL );
  }
  if ( oldState == WorkerState::DONE_QUERYING
       && newState == WorkerState::REFILLED )
#else
    if ( oldState == WorkerState::DRAINED
         && newState == WorkerState::CHECKPOINTED )
    {
      writeRestartScript();
      if (exitAfterCkpt) {
        JNOTE("Checkpoint Done. Killing all peers.");
        broadcastMessage(DMT_KILL_PEER);
        exitAfterCkpt = false;
      } else {
        JNOTE ( "refilling all nodes" );
        broadcastMessage ( DMT_DO_REFILL );
      }
    }
  if ( oldState == WorkerState::RESTARTING
       && newState == WorkerState::CHECKPOINTED )
  {
    JTIMER_STOP ( restart );

    JNOTE ( "refilling all nodes (after checkpoint)" );
    broadcastMessage ( DMT_DO_REFILL );
  }
  if ( oldState == WorkerState::CHECKPOINTED
       && newState == WorkerState::REFILLED )
#endif
  {
    JNOTE ( "restarting all nodes" );
    broadcastMessage ( DMT_DO_RESUME );

    JTIMER_STOP ( checkpoint );
    isRestarting = false;

    updateCheckpointInterval( theCheckpointInterval );

    if (blockUntilDone) {
      DmtcpMessage blockUntilDoneReply(DMT_USER_CMD_RESULT);
      JNOTE ( "replying to dmtcp_command:  we're done" );
      // These were set in dmtcp::DmtcpCoordinator::onConnect in this file
      jalib::JSocket remote ( blockUntilDoneRemote );
      remote << blockUntilDoneReply;
      remote.close();
      blockUntilDone = false;
      blockUntilDoneRemote = -1;
    }
  }
}

void dmtcp::DmtcpCoordinator::onData(CoordClient *client)
{
  DmtcpMessage msg;
  JASSERT(client != NULL);

  client->sock() >> msg;
  msg.assertValid();
  char *extraData = 0;
  if (msg.extraBytes > 0) {
    extraData = new char[msg.extraBytes];
    client->sock().readAll(extraData, msg.extraBytes);
  }

  switch ( msg.type )
  {
    case DMT_OK:
    {
      WorkerState oldState = client->state();
      client->setState ( msg.state );
      ComputationStatus s = getStatus();
      WorkerState newState = s.minimumState;
      /* It is possible for minimumState to be RUNNING while one or more
       * processes are still in REFILLED state.
       */
      if (s.minimumState == WorkerState::RUNNING && !s.minimumStateUnanimous &&
          s.maximumState == WorkerState::REFILLED) {
        /* If minimumState is RUNNING, and not every processes is in RUNNING
         * state, the maximumState must be REFILLED. This is the case when we
         * are performing ckpt-resume or rst-resume).
         */
        newState = s.maximumState;
      }

      JTRACE ("got DMT_OK message")
        ( msg.from )( msg.state )( oldState )( newState );

      updateMinimumState(oldState);
      break;
    }
    case DMT_UNIQUE_CKPT_FILENAME:
      uniqueCkptFilenames = true;
      // Fall though
    case DMT_CKPT_FILENAME:
    {
      JASSERT ( extraData!=0 )
        .Text ( "extra data expected with DMT_CKPT_FILENAME message" );
      dmtcp::string ckptFilename;
      dmtcp::string hostname;
      ckptFilename = extraData;
      hostname = extraData + ckptFilename.length() + 1;

      JTRACE ( "recording restart info" ) ( ckptFilename ) ( hostname );
      _restartFilenames[hostname].push_back ( ckptFilename );
    }
    break;
    case DMT_GET_CKPT_DIR:
    {
      DmtcpMessage reply(DMT_GET_CKPT_DIR_RESULT);
      reply.extraBytes = ckptDir.length() + 1;
      client->sock() << reply;
      client->sock().writeAll(ckptDir.c_str(), reply.extraBytes);
    }
    break;
    case DMT_UPDATE_CKPT_DIR:
    {
      JASSERT(extraData != 0)
        .Text("extra data expected with DMT_UPDATE_CKPT_DIR message");
      if (strcmp(ckptDir.c_str(), extraData) != 0) {
        ckptDir = extraData;
        JNOTE("Updated ckptDir") (ckptDir);
      }
    }
    break;

#ifdef COORD_NAMESERVICE
    case DMT_REGISTER_NAME_SERVICE_DATA:
    {
      JTRACE ("received REGISTER_NAME_SERVICE_DATA msg") (client->identity());
      lookupService.registerData(msg, (const void*) extraData);
    }
    break;

    case DMT_REGISTER_NAME_SERVICE_DATA_SYNC:
    {
      JTRACE ("received REGISTER_NAME_SERVICE_DATA_SYNC msg") (client->identity());
      lookupService.registerData(msg, (const void*) extraData);
      dmtcp::DmtcpMessage response(DMT_REGISTER_NAME_SERVICE_DATA_SYNC_RESPONSE);
      JTRACE("Sending NS response to the client...");
      client->sock() << response;
    }
    break;
    case DMT_NAME_SERVICE_QUERY:
    {
      JTRACE ("received NAME_SERVICE_QUERY msg") (client->identity());
      lookupService.respondToQuery(client->sock(), msg,
                                   (const void*) extraData);
    }
    break;
#endif

    case DMT_UPDATE_PROCESS_INFO_AFTER_FORK:
    {
        JNOTE("Updating process Information after fork()")
          (client->hostname()) (client->progname()) (msg.from) (client->identity());
        client->identity(msg.from);
        client->realPid(msg.realPid);
    }
    break;
    case DMT_UPDATE_PROCESS_INFO_AFTER_EXEC:
    {
      dmtcp::string progname = extraData;
      JNOTE("Updating process Information after exec()")
        (progname) (msg.from) (client->identity());
      client->progname(progname);
      client->identity(msg.from);
    }
    break;

    case DMT_NULL:
      JWARNING(false) (msg.type) .Text("unexpected message from worker. Closing connection");
      onDisconnect(client);
      break;
    default:
      JASSERT ( false ) ( msg.from ) ( msg.type )
        .Text ( "unexpected message from worker" );
  }

  delete[] extraData;
}


static void removeStaleSharedAreaFile()
{
  ostringstream o;
  o << Util::getTmpDir()
    << "/dmtcpSharedArea." << compId << "." << std::hex << curTimeStamp;
  JTRACE("Removing sharedArea file.") (o.str());
  unlink(o.str().c_str());
}

static void preExitCleanup()
{
  removeStaleSharedAreaFile();
  JTRACE("Removing port-file") (thePortFile);
  unlink(thePortFile.c_str());
}

void dmtcp::DmtcpCoordinator::onDisconnect(CoordClient *client)
{
  if (client->isNSWorker()) {
    client->sock().close();
    delete client;
    return;
  }
  for (size_t i = 0; i < clients.size(); i++) {
    if (clients[i] == client) {
      clients.erase(clients.begin() + i);
      break;
    }
  }
  client->sock().close();
  JNOTE ( "client disconnected" ) ( client->identity() );
  _virtualPidToClientMap.erase(client->virtualPid());

  ComputationStatus s = getStatus();
  if (s.numPeers < 1) {
    if (exitOnLast) {
      JNOTE ("last client exited, shutting down..");
      handleUserCommand('q');
    } else {
      removeStaleSharedAreaFile();
    }
    // If a kill in is progress, the coordinator refuses any new connections,
    // thus we need to reset it to false once all the processes in the
    // computations have disconnected.
    killInProgress = false;
    if (theCheckpointInterval != theDefaultCheckpointInterval) {
      updateCheckpointInterval(theDefaultCheckpointInterval);
      JNOTE ( "CheckpointInterval reset on end of current computation" )
        ( theCheckpointInterval );
    }
  } else {
    updateMinimumState(client->state());
  }
}

void dmtcp::DmtcpCoordinator::initializeComputation()
{
  //this is the first connection, do some initializations
  workersRunningAndSuspendMsgSent = false;
  killInProgress = false;
  //_nextVirtualPid = INITIAL_VIRTUAL_PID;

  // theCheckpointInterval can be overridden later by msg from this client.
  updateCheckpointInterval( theDefaultCheckpointInterval );

  // drop current computation group to 0
  compId = dmtcp::UniquePid(0,0,0);
  curTimeStamp = 0; // Drop timestamp to 0
  numPeers = -1; // Drop number of peers to unknown
  blockUntilDone = false;
  //exitAfterCkpt = false;
}

void dmtcp::DmtcpCoordinator::onConnect()
{
  struct sockaddr_storage remoteAddr;
  socklen_t remoteLen = sizeof(remoteAddr);
  jalib::JSocket remote = listenSock->accept(&remoteAddr, &remoteLen);
  JTRACE("accepting new connection") (remote.sockfd()) (JASSERT_ERRNO);

  if (!remote.isValid()) {
    remote.close();
    return;
  }

  dmtcp::DmtcpMessage hello_remote;
  hello_remote.poison();
  JTRACE("Reading from incoming connection...");
  remote >> hello_remote;
  if (!remote.isValid()) {
    remote.close();
    return;
  }

#ifdef COORD_NAMESERVICE
  if (hello_remote.type == DMT_NAME_SERVICE_WORKER) {
    CoordClient *client = new CoordClient(remote, &remoteAddr, remoteLen,
		                          hello_remote);

    addDataSocket(client);
    return;
  }
  if (hello_remote.type == DMT_NAME_SERVICE_QUERY) {
    JASSERT(hello_remote.extraBytes > 0) (hello_remote.extraBytes);
    char *extraData = new char[hello_remote.extraBytes];
    remote.readAll(extraData, hello_remote.extraBytes);

    JTRACE ("received NAME_SERVICE_QUERY msg on running") (hello_remote.from);
    lookupService.respondToQuery(remote, hello_remote, extraData);
    delete [] extraData;
    remote.close();
    return;
  }
  if (hello_remote.type == DMT_REGISTER_NAME_SERVICE_DATA) {
    JASSERT(hello_remote.extraBytes > 0) (hello_remote.extraBytes);
    char *extraData = new char[hello_remote.extraBytes];
    remote.readAll(extraData, hello_remote.extraBytes);

    JTRACE ("received REGISTER_NAME_SERVICE_DATA msg on running") (hello_remote.from);
    lookupService.registerData(hello_remote, (const void*) extraData);
    delete [] extraData;
    remote.close();
    return;
  }
  if (hello_remote.type == DMT_REGISTER_NAME_SERVICE_DATA_SYNC) {
    JASSERT(hello_remote.extraBytes > 0) (hello_remote.extraBytes);
    char *extraData = new char[hello_remote.extraBytes];
    remote.readAll(extraData, hello_remote.extraBytes);

    JTRACE ("received REGISTER_NAME_SERVICE_DATA msg on running") (hello_remote.from);
    lookupService.registerData(hello_remote, (const void*) extraData);
    delete [] extraData;
    dmtcp::DmtcpMessage response(DMT_REGISTER_NAME_SERVICE_DATA_SYNC_RESPONSE);
    JTRACE("Reading from incoming connection...");
    remote << response;
    remote.close();
    return;
  }
#endif



  if (hello_remote.type == DMT_USER_CMD) {
    updateCheckpointInterval(hello_remote.theCheckpointInterval);
    processDmtUserCmd(hello_remote, remote);
    return;
  }

  if (killInProgress) {
    JNOTE("Connection request received in the middle of killing computation. "
          "Sending it the kill message.");
    DmtcpMessage msg;
    msg.type = DMT_KILL_PEER;
    remote << msg;
    remote.close();
    return;
  }

  // If no client is connected to Coordinator, then there can be only zero data
  // sockets OR there can be one data socket and that should be STDIN.
  if (clients.size() == 0) {
    initializeComputation();
  }

  CoordClient *client = new CoordClient(remote, &remoteAddr, remoteLen,
                                        hello_remote);

  if( hello_remote.extraBytes > 0 ){
    client->readProcessInfo(hello_remote);
  }

  if (hello_remote.type == DMT_RESTART_WORKER) {
    if (!validateRestartingWorkerProcess(hello_remote, remote,
                                         &remoteAddr, remoteLen)) {
      return;
    }
    client->virtualPid(hello_remote.from.pid());
    _virtualPidToClientMap[client->virtualPid()] = client;
    isRestarting = true;
  } else if (hello_remote.type == DMT_NEW_WORKER) {
    JASSERT(hello_remote.state == WorkerState::RUNNING ||
            hello_remote.state == WorkerState::UNKNOWN);
    JASSERT(hello_remote.virtualPid == -1);
    client->virtualPid(getNewVirtualPid());
    if (!validateNewWorkerProcess(hello_remote, remote, client,
                                  &remoteAddr, remoteLen)) {
      return;
    }
    _virtualPidToClientMap[client->virtualPid()] = client;
  } else {
    JASSERT(false) (hello_remote.type)
      .Text("Connect request from Unknown Remote Process Type");
  }

  updateCheckpointInterval(hello_remote.theCheckpointInterval);
  JNOTE ( "worker connected" ) ( hello_remote.from );

  clients.push_back(client);
  addDataSocket(client);

  JTRACE("END") (clients.size());
}

void dmtcp::DmtcpCoordinator::processDmtUserCmd( DmtcpMessage& hello_remote,
						 jalib::JSocket& remote )
{
  //dmtcp_command doesn't handshake (it is antisocial)
  JTRACE("got user command from dmtcp_command")(hello_remote.coordCmd);
  DmtcpMessage reply;
  reply.type = DMT_USER_CMD_RESULT;
  // if previous 'b' blocking prefix command had set blockUntilDone
  if (blockUntilDone && blockUntilDoneRemote == -1  &&
      hello_remote.coordCmd == 'c') {
    // Reply will be done in dmtcp::DmtcpCoordinator::onData in this file.
    blockUntilDoneRemote = remote.sockfd();
    handleUserCommand( hello_remote.coordCmd, &reply );
  } else if ( (hello_remote.coordCmd == 'i')
               && hello_remote.theCheckpointInterval >= 0 ) {
//    theDefaultCheckpointInterval = hello_remote.theCheckpointInterval;
//    theCheckpointInterval = theDefaultCheckpointInterval;
    handleUserCommand( hello_remote.coordCmd, &reply );
    remote << reply;
    remote.close();
  } else {
    handleUserCommand( hello_remote.coordCmd, &reply );
    remote << reply;
    remote.close();
  }
  return;
}

bool dmtcp::DmtcpCoordinator::validateRestartingWorkerProcess
	 (DmtcpMessage& hello_remote, jalib::JSocket& remote,
          const struct sockaddr_storage* remoteAddr, socklen_t remoteLen)
{
  struct timeval tv;
  const struct sockaddr_in *sin = (const struct sockaddr_in*) remoteAddr;
  dmtcp::string remoteIP = inet_ntoa(sin->sin_addr);
  dmtcp::DmtcpMessage hello_local ( dmtcp::DMT_ACCEPT );

  JASSERT(hello_remote.state == WorkerState::RESTARTING) (hello_remote.state);

  if (compId == dmtcp::UniquePid(0,0,0)) {
    JASSERT ( minimumState() == WorkerState::UNKNOWN )
      .Text ( "Coordinator should be idle at this moment" );
    // Coordinator is free at this moment - set up all the things
    compId = hello_remote.compGroup;
    numPeers = hello_remote.numPeers;
    JASSERT(gettimeofday(&tv, NULL) == 0);
    // Get the resolution down to 100 mili seconds.
    curTimeStamp = (tv.tv_sec << 4) | (tv.tv_usec / (100*1000));
    JNOTE ( "FIRST dmtcp_restart connection.  Set numPeers. Generate timestamp" )
      ( numPeers ) ( curTimeStamp ) ( compId );
    JTIMER_START(restart);
  } else if (minimumState() != WorkerState::RESTARTING &&
             minimumState() != WorkerState::CHECKPOINTED) {
    JNOTE ("Computation not in RESTARTING or CHECKPOINTED state."
           "  Reject incoming restarting computation process.")
      (compId) (hello_remote.compGroup) (minimumState());
    hello_local.type = dmtcp::DMT_REJECT_NOT_RESTARTING;
    remote << hello_local;
    remote.close();
    return false;
  } else if ( hello_remote.compGroup != compId) {
    JNOTE ("Reject incoming restarting computation process"
           " since it is not from current computation")
      ( compId ) ( hello_remote.compGroup );
    hello_local.type = dmtcp::DMT_REJECT_WRONG_COMP;
    remote << hello_local;
    remote.close();
    return false;
  }
  // dmtcp_restart already connected and compGroup created.
  // Computation process connection
  JASSERT ( curTimeStamp != 0 );

  JTRACE("Connection from (restarting) computation process")
    ( compId ) ( hello_remote.compGroup ) ( minimumState() );

  hello_local.coordTimeStamp = curTimeStamp;
  if (Util::strStartsWith(remoteIP, "127.")) {
    memcpy(&hello_local.ipAddr, &localhostIPAddr, sizeof localhostIPAddr);
  } else {
    memcpy(&hello_local.ipAddr, &sin->sin_addr, sizeof localhostIPAddr);
  }
  remote << hello_local;

  // NOTE: Sending the same message twice. We want to make sure that the
  // worker process receives/processes the first messages as soon as it
  // connects to the coordinator. The second message will be processed in
  // postRestart routine in DmtcpWorker.
  //
  // The reason to do this is the following. The dmtcp_restart process
  // connects to the coordinator at a very early stage. Later on, before
  // exec()'ing into mtcp_restart, it reconnects to the coordinator using
  // it's original UniquiePid and closes the earlier socket connection.
  // However, the coordinator might process the disconnect() before it
  // processes the connect() which would lead to a situation where the
  // coordinator is not connected to any worker processes. The coordinator
  // would now process the connect() and may reject the worker because the
  // worker state is RESTARTING, but the minimumState() is UNKNOWN.
  //remote << hello_local;

  return true;
}

bool dmtcp::DmtcpCoordinator::validateNewWorkerProcess
  (DmtcpMessage& hello_remote, jalib::JSocket& remote, CoordClient *client,
   const struct sockaddr_storage* remoteAddr, socklen_t remoteLen)
{
  const struct sockaddr_in *sin = (const struct sockaddr_in*) remoteAddr;
  dmtcp::string remoteIP = inet_ntoa(sin->sin_addr);
  dmtcp::DmtcpMessage hello_local(dmtcp::DMT_ACCEPT);
  hello_local.virtualPid = client->virtualPid();
  ComputationStatus s = getStatus();

  JASSERT(hello_remote.state == WorkerState::RUNNING ||
          hello_remote.state == WorkerState::UNKNOWN) (hello_remote.state);

  if (workersRunningAndSuspendMsgSent == true) {
    /* Worker trying to connect after SUSPEND message has been sent.
     * This happens if the worker process is executing a fork() system call
     * when the DMT_DO_SUSPEND is broadcasted. We need to make sure that the
     * child process is allowed to participate in the current checkpoint.
     */
    JASSERT(s.numPeers > 0) (s.numPeers);
    JASSERT(s.minimumState != WorkerState::SUSPENDED) (s.minimumState);

    // Handshake
    hello_local.compGroup = compId;
    remote << hello_local;

    // Now send DMT_DO_SUSPEND message so that this process can also
    // participate in the current checkpoint
    DmtcpMessage suspendMsg (dmtcp::DMT_DO_SUSPEND);
    suspendMsg.compGroup = compId;
    remote << suspendMsg;

  } else if (s.numPeers > 0 && s.minimumState != WorkerState::RUNNING &&
             s.minimumState != WorkerState::UNKNOWN) {
    // If some of the processes are not in RUNNING state
    JNOTE("Current computation not in RUNNING state."
          "  Refusing to accept new connections.")
      (compId) (hello_remote.from)
      (s.numPeers) (s.minimumState);
    hello_local.type = dmtcp::DMT_REJECT_NOT_RUNNING;
    remote << hello_local;
    remote.close();
    return false;

  } else if (hello_remote.compGroup != UniquePid()) {
    // New Process trying to connect to Coordinator but already has compGroup
    JNOTE ("New process, but already has computation group. Rejecting")
      (hello_remote.compGroup);

    hello_local.type = dmtcp::DMT_REJECT_WRONG_COMP;
    remote << hello_local;
    remote.close();
    return false;

  } else {
    // If first process, create the new computation group
    if (compId == UniquePid(0,0,0)) {
      struct timeval tv;
      // Connection of new computation.
      compId = UniquePid(hello_remote.from.hostid(), client->virtualPid(),
                         hello_remote.from.time(),
                         hello_remote.from.generation());

      localPrefix.clear();
      localHostName.clear();
      remotePrefix.clear();
      if (!client->prefixDir().empty()) {
        localPrefix = client->prefixDir();
        localHostName = client->hostname();
      }
      JASSERT(gettimeofday(&tv, NULL) == 0);
      // Get the resolution down to 100 mili seconds.
      curTimeStamp = (tv.tv_sec << 4) | (tv.tv_usec / (100*1000));
      numPeers = -1;
      JTRACE("First process connected.  Creating new computation group")
        (compId);
    } else {
      JTRACE("New process connected")
        (hello_remote.from) (client->prefixDir()) (client->virtualPid());
      if (client->hostname() == localHostName) {
        JASSERT(client->prefixDir() == localPrefix)
          (client->prefixDir()) (localPrefix);
      }
      if (!client->prefixDir().empty() && client->hostname() != localHostName) {
        if (remotePrefix.empty()) {
          JASSERT (compId != UniquePid(0,0,0));
          remotePrefix = client->prefixDir();
        } else if (remotePrefix != client->prefixDir()) {
          JNOTE("This node has different prefixDir than the rest of the "
                "remote nodes. Rejecting connection!")
            (remotePrefix) (localPrefix) (client->prefixDir());
          hello_local.type = dmtcp::DMT_REJECT_WRONG_PREFIX;
          remote << hello_local;
          remote.close();
          return false;
        }
      }
    }
    hello_local.compGroup = compId;
    hello_local.coordTimeStamp = curTimeStamp;
    if (Util::strStartsWith(remoteIP, "127.")) {
      memcpy(&hello_local.ipAddr, &localhostIPAddr, sizeof localhostIPAddr);
    } else {
      memcpy(&hello_local.ipAddr, &sin->sin_addr, sizeof localhostIPAddr);
    }
    remote << hello_local;
  }
  return true;
}

bool dmtcp::DmtcpCoordinator::startCheckpoint()
{
  uniqueCkptFilenames = false;
  ComputationStatus s = getStatus();
  if ( s.minimumState == WorkerState::RUNNING && s.minimumStateUnanimous
       && !workersRunningAndSuspendMsgSent )
  {
    time(&ckptTimeStamp);
    JTIMER_START ( checkpoint );
    _restartFilenames.clear();
    JNOTE ( "starting checkpoint, suspending all nodes" )( s.numPeers );
    compId.incrementGeneration();
    JNOTE("Incremented Generation") (compId.generation());
    // Pass number of connected peers to all clients
    broadcastMessage(DMT_DO_SUSPEND);

    // Suspend Message has been sent but the workers are still in running
    // state.  If the coordinator receives another checkpoint request from user
    // at this point, it should fail.
    workersRunningAndSuspendMsgSent = true;
    return true;
  } else {
    if (s.numPeers > 0) {
      JTRACE ( "delaying checkpoint, workers not ready" ) ( s.minimumState )
	     ( s.numPeers );
    }
    return false;
  }
}

void dmtcp::DmtcpCoordinator::broadcastMessage(DmtcpMessageType type,
                                               int numPeers)
{
  DmtcpMessage msg;
  msg.type = type;
  msg.compGroup = compId;
  if (numPeers > 0) {
    msg.numPeers = numPeers;
  }

  if (msg.type == DMT_KILL_PEER && clients.size() > 0) {
    killInProgress = true;
  } else if (msg.type == DMT_DO_FD_LEADER_ELECTION) {
    // All the workers are in SUSPENDED state, now it is safe to reset
    // this flag.
    workersRunningAndSuspendMsgSent = false;
  }

  for (size_t i = 0; i < clients.size(); i++) {
    clients[i]->sock() << msg;
  }
  JTRACE ("sending message")( type );
}

dmtcp::DmtcpCoordinator::ComputationStatus dmtcp::DmtcpCoordinator::getStatus() const
{
  ComputationStatus status;
  const static int INITIAL_MIN = WorkerState::_MAX;
  const static int INITIAL_MAX = WorkerState::UNKNOWN;
  int min = INITIAL_MIN;
  int max = INITIAL_MAX;
  int count = 0;
  bool unanimous = true;
  for (size_t i = 0; i < clients.size(); i++) {
    int cliState = clients[i]->state().value();
    count++;
    unanimous = unanimous && (min==cliState || min==INITIAL_MIN);
    if ( cliState < min ) min = cliState;
    if ( cliState > max ) max = cliState;
  }

  status.minimumState = ( min==INITIAL_MIN ? WorkerState::UNKNOWN
			  : (WorkerState::eWorkerState)min );
  if( status.minimumState == WorkerState::CHECKPOINTED &&
      isRestarting && count < numPeers ){
    JTRACE("minimal state counted as CHECKPOINTED but not all processes"
	   " are connected yet.  So we wait.") ( numPeers ) ( count );
    status.minimumState = WorkerState::RESTARTING;
  }
  status.minimumStateUnanimous = unanimous;

  status.maximumState = ( max==INITIAL_MAX ? WorkerState::UNKNOWN
			  : (WorkerState::eWorkerState)max );
  status.numPeers = count;
  return status;
}

void dmtcp::DmtcpCoordinator::writeRestartScript()
{
  dmtcp::ostringstream o;
  dmtcp::string uniqueFilename;

  o << dmtcp::string(ckptDir) << "/"
    << RESTART_SCRIPT_BASENAME << "_" << compId;
  if (uniqueCkptFilenames) {
    o << "_" << std::setw(5) << std::setfill('0') << compId.generation();
  }
  o << "." << RESTART_SCRIPT_EXT;
  uniqueFilename = o.str();

  const bool isSingleHost = (_restartFilenames.size() == 1);

  dmtcp::map< dmtcp::string, dmtcp::vector<dmtcp::string> >::const_iterator host;
  dmtcp::vector<dmtcp::string>::const_iterator file;

  char hostname[80];
  char timestamp[80];
  gethostname ( hostname, 80 );

  JTRACE ( "writing restart script" ) ( uniqueFilename );

  FILE* fp = fopen ( uniqueFilename.c_str(),"w" );
  JASSERT ( fp!=0 )(JASSERT_ERRNO)( uniqueFilename )
    .Text ( "failed to open file" );

  fprintf ( fp, "%s", theRestartScriptHeader );
  fprintf ( fp, "%s", theRestartScriptCheckLocal );
  fprintf ( fp, "%s", theRestartScriptUsage );

  ctime_r(&ckptTimeStamp, timestamp);
  // Remove the trailing '\n'
  timestamp[strlen(timestamp) - 1] = '\0';
  fprintf ( fp, "ckpt_timestamp=\"%s\"\n\n", timestamp );

  fprintf ( fp, "coord_host=$" ENV_VAR_NAME_HOST "\n"
                "if test -z \"$" ENV_VAR_NAME_HOST "\"; then\n"
                "  coord_host=%s\nfi\n\n"
                "coord_port=$" ENV_VAR_NAME_PORT "\n"
                "if test -z \"$" ENV_VAR_NAME_PORT "\"; then\n"
                "  coord_port=%d\nfi\n\n"
                "checkpoint_interval=$" ENV_VAR_CKPT_INTR "\n"
                "if test -z \"$" ENV_VAR_CKPT_INTR "\"; then\n"
                "  checkpoint_interval=%d\nfi\n"
                "export DMTCP_CHECKPOINT_INTERVAL=${checkpoint_interval}\n\n",
                hostname, thePort, theCheckpointInterval );

  fprintf ( fp, "%s", theRestartScriptCmdlineArgHandler );

  fprintf ( fp, "dmt_rstr_cmd=%s/" DMTCP_RESTART_CMD "\n"
                "which $dmt_rstr_cmd > /dev/null 2>&1"
                " || dmt_rstr_cmd=" DMTCP_RESTART_CMD "\n"
                "which $dmt_rstr_cmd > /dev/null 2>&1"
                " || echo \"$0: $dmt_rstr_cmd not found\"\n"
                "which $dmt_rstr_cmd > /dev/null 2>&1 || exit 1\n\n",
                jalib::Filesystem::GetProgramDir().c_str());

  fprintf ( fp, "local_prefix=%s\n", localPrefix.c_str() );
  fprintf ( fp, "remote_prefix=%s\n", remotePrefix.c_str() );
  fprintf ( fp, "remote_dmt_rstr_cmd=" DMTCP_RESTART_CMD "\n"
                "if ! test -z \"$remote_prefix\"; then\n"
                "  remote_dmt_rstr_cmd=\"$remote_prefix/bin/" DMTCP_RESTART_CMD "\"\n"
                "fi\n\n" );

  fprintf ( fp, "# Number of hosts in the computation = %zd\n"
                "# Number of processes in the computation = %d\n\n",
                _restartFilenames.size(), getStatus().numPeers );

  if ( isSingleHost ) {
    JTRACE ( "Single HOST" );

    host=_restartFilenames.begin();
    dmtcp::ostringstream o;
    for ( file=host->second.begin(); file!=host->second.end(); ++file ) {
      o << " " << *file;
    }
    fprintf ( fp, "given_ckpt_files=\"%s\"\n\n", o.str().c_str());

    fprintf ( fp, "%s", theRestartScriptSingleHostProcessing );
  }
  else
  {
    fprintf ( fp, "%s",
              "# SYNTAX:\n"
              "#  :: <HOST> :<MODE>: <CHECKPOINT_IMAGE> ...\n"
              "# Host names and filenames must not include \':\'\n"
              "# At most one fg (foreground) mode allowed; it must be last.\n"
              "# \'maybexterm\' and \'maybebg\' are set from <MODE>.\n");

    fprintf ( fp, "%s", "worker_ckpts=\'" );
    for ( host=_restartFilenames.begin(); host!=_restartFilenames.end(); ++host ) {
      fprintf ( fp, "\n :: %s :bg:", host->first.c_str() );
      for ( file=host->second.begin(); file!=host->second.end(); ++file ) {
        fprintf ( fp," %s", file->c_str() );
      }
    }
    fprintf ( fp, "%s", "\n\'\n\n" );

    fprintf( fp,  "# Check for resource manager\n"
                  "discover_rm_path=$(which dmtcp_discover_rm)\n"
                  "if [ -n \"$discover_rm_path\" ]; then\n"
                  "  eval $(dmtcp_discover_rm -t)\n"
                  "  srun_path=$(which srun 2> /dev/null)\n"
                  "  llaunch=`which dmtcp_rm_loclaunch`\n"
                  "  if [ $RES_MANAGER = \"SLURM\" ] && [ -n \"$srun_path\" ]; then\n"
                  "    eval $(dmtcp_discover_rm -n \"$worker_ckpts\")\n"
                  "    if [ -n \"$DMTCP_DISCOVER_RM_ERROR\" ]; then\n"
                  "        echo \"Restart error: $DMTCP_DISCOVER_RM_ERROR\"\n"
                  "        echo \"Allocated resources: $manager_resources\"\n"
                  "        exit 0\n"
                  "    fi\n"
                  "    export DMTCP_REMLAUNCH_NODES=$DMTCP_REMLAUNCH_NODES\n"
                  "    bound=$(($DMTCP_REMLAUNCH_NODES - 1))\n"
                  "    for i in $(seq 0 $bound); do\n"
                  "      eval \"val=\\${DMTCP_REMLAUNCH_${i}_SLOTS}\"\n"
                  "      export DMTCP_REMLAUNCH_${i}_SLOTS=\"$val\"\n"
                  "      bound2=$(($val - 1))\n"
                  "      for j in $(seq 0 $bound2); do\n"
                  "        eval \"ckpts=\\${DMTCP_REMLAUNCH_${i}_${j}}\"\n"
                  "        export DMTCP_REMLAUNCH_${i}_${j}=\"$ckpts\"\n"
                  "      done\n"
                  "    done\n"
                  "    $srun_path \"$llaunch\"\n"
                  "    exit 0\n"
                  "  elif [ $RES_MANAGER = \"TORQUE\" ]; then\n"
                  "    #eval $(dmtcp_discover_rm \"$worker_ckpts\")\n"
                  "    #if [ -n \"$new_worker_ckpts\" ]; then\n"
                  "    #  worker_ckpts=\"$new_worker_ckpts\"\n"
                  "    #fi\n"
                  "    eval $(dmtcp_discover_rm -n \"$worker_ckpts\")\n"
                  "    if [ -n \"$DMTCP_DISCOVER_RM_ERROR\" ]; then\n"
                  "        echo \"Restart error: $DMTCP_DISCOVER_RM_ERROR\"\n"
                  "        echo \"Allocated resources: $manager_resources\"\n"
                  "        exit 0\n"
                  "    fi\n"
                  "    arguments=\"PATH=$PATH DMTCP_HOST=$DMTCP_HOST DMTCP_PORT=$DMTCP_PORT\"\n"
                  "    arguments=$arguments\" DMTCP_CHECKPOINT_INTERVAL=$DMTCP_CHECKPOINT_INTERVAL\"\n"
                  "    arguments=$arguments\" DMTCP_TMPDIR=$DMTCP_TMPDIR\"\n"
                  "    arguments=$arguments\" DMTCP_REMLAUNCH_NODES=$DMTCP_REMLAUNCH_NODES\"\n"
                  "    bound=$(($DMTCP_REMLAUNCH_NODES - 1))\n"
                  "    for i in $(seq 0 $bound); do\n"
                  "      eval \"val=\\${DMTCP_REMLAUNCH_${i}_SLOTS}\"\n"
                  "      arguments=$arguments\" DMTCP_REMLAUNCH_${i}_SLOTS=\\\"$val\\\"\"\n"
                  "      bound2=$(($val - 1))\n"
                  "      for j in $(seq 0 $bound2); do\n"
                  "        eval \"ckpts=\\${DMTCP_REMLAUNCH_${i}_${j}}\"\n"
                  "        arguments=$arguments\" DMTCP_REMLAUNCH_${i}_${j}=\\\"$ckpts\\\"\"\n"
                  "      done\n"
                  "    done\n"
                  "    pbsdsh -u \"$llaunch\" \"$arguments\"\n"
                  "    exit 0\n"
                  "  fi\n"
                  "fi\n"
                  "\n\n");

    fprintf ( fp, "%s", theRestartScriptMultiHostProcessing );
  }

  fclose ( fp );
  {
    dmtcp::string filename = RESTART_SCRIPT_BASENAME "." RESTART_SCRIPT_EXT;
    dmtcp::string dirname = jalib::Filesystem::DirName(uniqueFilename);
    int dirfd = open(dirname.c_str(), O_DIRECTORY | O_RDONLY);
    JASSERT(dirfd != -1) (dirname) (JASSERT_ERRNO);

    /* Set execute permission for user. */
    struct stat buf;
    JASSERT(stat(uniqueFilename.c_str(), &buf) == 0);
    JASSERT(chmod(uniqueFilename.c_str(), buf.st_mode | S_IXUSR) == 0);
    // Create a symlink from
    //   dmtcp_restart_script.sh -> dmtcp_restart_script_<curCompId>.sh
    unlink(filename.c_str());
    JTRACE("linking \"dmtcp_restart_script.sh\" filename to uniqueFilename")
      (filename) (dirname) (uniqueFilename);
    // FIXME:  Handle error case of symlink()
    JWARNING(symlinkat(uniqueFilename.c_str(), dirfd, filename.c_str()) == 0);
    JASSERT(close(dirfd) == 0);
  }
  _restartFilenames.clear();
}

static void SIGINTHandler(int signum)
{
  prog.handleUserCommand('q');
}

static void setupSIGINTHandler()
{
  struct sigaction action;
  action.sa_handler = SIGINTHandler;
  sigemptyset ( &action.sa_mask );
  action.sa_flags = 0;
  sigaction ( SIGINT, &action, NULL );
}

static void calcLocalAddr()
{
  dmtcp::string cmd;
  char hostname[HOST_NAME_MAX];
  JASSERT(gethostname(hostname, sizeof hostname) == 0) (JASSERT_ERRNO);
  struct addrinfo *result;
  struct addrinfo *res;
  int error;
  struct addrinfo hints;

  memset(&localhostIPAddr, 0, sizeof localhostIPAddr);
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  hints.ai_protocol = 0;
  hints.ai_canonname = NULL;
  hints.ai_addr = NULL;
  hints.ai_next = NULL;

  /* resolve the domain name into a list of addresses */
  error = getaddrinfo(hostname, NULL, &hints, &result);
  if (error == 0) {
    /* loop over all returned results and do inverse lookup */
    for (res = result; res != NULL; res = res->ai_next) {
      char name[NI_MAXHOST] = "";
      struct sockaddr_in *s = (struct sockaddr_in*) res->ai_addr;

      error = getnameinfo(res->ai_addr, res->ai_addrlen, name, NI_MAXHOST, NULL, 0, 0);
      if (error != 0) {
        JTRACE("getnameinfo() failed.") (gai_strerror(error));
        continue;
      }
      if (Util::strStartsWith(name, hostname)) {
        JASSERT(sizeof localhostIPAddr == sizeof s->sin_addr);
        memcpy(&localhostIPAddr, &s->sin_addr, sizeof s->sin_addr);
      }
    }
  } else {
    if (error == EAI_SYSTEM) {
      perror("getaddrinfo");
    } else {
      JTRACE("Error in getaddrinfo") (gai_strerror(error));
    }
    inet_aton("127.0.0.1", &localhostIPAddr);
  }
  coordHostname = hostname;
}

void dmtcp::DmtcpCoordinator::onTimeoutInterval()
{
  if (theCheckpointInterval > 0) {
    startCheckpoint();
    updateCheckpointInterval(theCheckpointInterval);
  }
}

void dmtcp::DmtcpCoordinator::updateCheckpointInterval(uint32_t interval)
{
  if ( interval != DMTCPMESSAGE_SAME_CKPT_INTERVAL &&
      interval != theCheckpointInterval) {
    int oldInterval = theCheckpointInterval;
    theCheckpointInterval = interval;
    JNOTE ( "CheckpointInterval updated (for this computation only)" )
      ( oldInterval ) ( theCheckpointInterval );
  }
  if (interval > 0) {
    JASSERT(clock_gettime(CLOCK_MONOTONIC, &startTime) == 0) (JASSERT_ERRNO);
  }
}

int dmtcp::DmtcpCoordinator::getRemainingTimeoutMS()
{
  int timeout = -1;
  if (theCheckpointInterval > 0) {
    struct timespec curTime;
    JASSERT(clock_gettime(CLOCK_MONOTONIC, &curTime) == 0) (JASSERT_ERRNO);
    timeout = startTime.tv_sec + theCheckpointInterval - curTime.tv_sec;
    timeout *= 1000;
    if (timeout < 0) {
      timeout = 0;
    }
  }
  return timeout;
}

void dmtcp::DmtcpCoordinator::eventLoop(bool daemon)
{
  struct epoll_event ev;
  epollFd = epoll_create(MAX_EVENTS);
  JASSERT(epollFd != -1) (JASSERT_ERRNO);

  ev.events = EPOLLIN;
  ev.data.ptr = listenSock;
  JASSERT(epoll_ctl(epollFd, EPOLL_CTL_ADD, listenSock->sockfd(), &ev) != -1)
    (JASSERT_ERRNO);

  if (!daemon &&
      // epoll_ctl below fails if STDIN is pointing to /dev/null.
      // Not sure why.
      jalib::Filesystem::GetDeviceName(0) != "/dev/null" &&
      jalib::Filesystem::GetDeviceName(0) != "/dev/zero" &&
      jalib::Filesystem::GetDeviceName(0) != "/dev/random") {
    ev.events = EPOLLIN;
#ifdef EPOLLRDHUP
    ev.events |= EPOLLRDHUP;
#endif
    ev.data.ptr = (void*) STDIN_FILENO;
    JASSERT(epoll_ctl(epollFd, EPOLL_CTL_ADD, STDIN_FILENO, &ev) != -1)
      (JASSERT_ERRNO);
  }

  while (true) {
    int timeout = getRemainingTimeoutMS();
    int nfds = epoll_wait(epollFd, events, MAX_EVENTS, timeout);
    if (nfds == -1 && errno == EINTR) continue;
    JASSERT(nfds != -1) (JASSERT_ERRNO);

    if (nfds == 0 && theCheckpointInterval > 0) {
      onTimeoutInterval();
    }

    for (int n = 0; n < nfds; ++n) {
      void *ptr = events[n].data.ptr;
      if ((events[n].events & EPOLLHUP) ||
#ifdef EPOLLRDHUP
          (events[n].events & EPOLLRDHUP) ||
#endif
          (events[n].events & EPOLLERR)) {
        JASSERT(ptr != listenSock);
        if (ptr == (void*) STDIN_FILENO) {
          JASSERT(epoll_ctl(epollFd, EPOLL_CTL_DEL, STDIN_FILENO, &ev) != -1)
            (JASSERT_ERRNO);
          close(STDIN_FD);
        } else {
          onDisconnect((CoordClient*)ptr);
        }
      } else if (events[n].events & EPOLLIN) {
        if (ptr == (void*) listenSock) {
          onConnect();
        } else if (ptr == (void*) STDIN_FILENO) {
          char buf[1];
          int ret = Util::readAll(STDIN_FD, buf, sizeof(buf));
          JASSERT(ret != -1) (JASSERT_ERRNO);
          if (ret > 0) {
            handleUserCommand(buf[0]);
          } else {
            JNOTE("closing stdin");
            JASSERT(epoll_ctl(epollFd, EPOLL_CTL_DEL, STDIN_FILENO, &ev) != -1)
              (JASSERT_ERRNO);
            close(STDIN_FD);
          }
        } else {
          onData((CoordClient*)ptr);
        }
      }
    }
  }
}

void dmtcp::DmtcpCoordinator::addDataSocket(CoordClient *client)
{
  struct epoll_event ev;

#ifdef EPOLLRDHUP
  ev.events = EPOLLIN | EPOLLRDHUP;
#else
  ev.events = EPOLLIN;
#endif
  ev.data.ptr = client;
  JASSERT(epoll_ctl(epollFd, EPOLL_CTL_ADD, client->sock().sockfd(), &ev) != -1)
    (JASSERT_ERRNO);
}

#define shift argc--; argv++

int main ( int argc, char** argv )
{
  initializeJalib();

  //parse port
  thePort = DEFAULT_PORT;
  const char* portStr = getenv ( ENV_VAR_NAME_PORT );
  if ( portStr != NULL ) thePort = jalib::StringToInt ( portStr );

  bool daemon = false;
  bool quiet = false;

  shift;
  while(argc > 0){
    dmtcp::string s = argv[0];
    if(s=="-h" || s=="--help"){
      printf("%s", theUsage);
      return 1;
    } else if ((s=="--version") && argc==1){
      printf("%s", DMTCP_VERSION_AND_COPYRIGHT_INFO);
      return 1;
    }else if(s == "-q" || s == "--quiet"){
      quiet = true;
      shift;
    }else if(s=="--exit-on-last"){
      exitOnLast = true;
      shift;
    }else if(s=="--exit-after-ckpt"){
      exitAfterCkpt = true;
      shift;
    }else if(s=="--daemon"){
      daemon = true;
      shift;
    }else if(argc>1 && (s == "-i" || s == "--interval")){
      setenv(ENV_VAR_CKPT_INTR, argv[1], 1);
      shift; shift;
    }else if(argc>1 && (s == "-p" || s == "--port")){
      thePort = jalib::StringToInt( argv[1] );
      shift; shift;
    }else if(argc>1 && s == "--port-file"){
      thePortFile = argv[1];
      shift; shift;
    }else if(argc>1 && (s == "-c" || s == "--ckptdir")){
      setenv(ENV_VAR_CHECKPOINT_DIR, argv[1], 1);
      shift; shift;
    }else if(argc>1 && (s == "-t" || s == "--tmpdir")){
      setenv(ENV_VAR_TMPDIR, argv[1], 1);
      shift; shift;
    }else if(argc == 1){ //last arg can be port
      char *endptr;
      long x = strtol(argv[0], &endptr, 10);
      if ((ssize_t)strlen(argv[0]) != endptr - argv[0]) {
        fprintf(stderr, theUsage, DEFAULT_PORT);
        return 1;
      } else {
        thePort = jalib::StringToInt( argv[0] );
        shift;
      }
      x++, x--; // to suppress unused variable warning
    }else{
      fprintf(stderr, theUsage, DEFAULT_PORT);
      return 1;
    }
  }

  if (!quiet) {
    fprintf(stderr, DMTCP_VERSION_AND_COPYRIGHT_INFO);
    fprintf(stderr, "(Use flag \"-q\" to hide this message.)\n\n");
  }

  dmtcp::Util::setTmpDir(getenv(ENV_VAR_TMPDIR));

  dmtcp::Util::initializeLogFile();

  JTRACE ( "New DMTCP coordinator starting." )
    ( dmtcp::UniquePid::ThisProcess() );

  if ( thePort < 0 )
  {
    fprintf(stderr, theUsage, DEFAULT_PORT);
    return 1;
  }

  calcLocalAddr();

  if (getenv(ENV_VAR_CHECKPOINT_DIR) != NULL) {
    ckptDir = getenv(ENV_VAR_CHECKPOINT_DIR);
  } else {
    ckptDir = ".";
  }

  /*Test if the listener socket is already open*/
  if ( fcntl(PROTECTED_COORD_FD, F_GETFD) != -1 ) {
    listenSock = new jalib::JServerSocket ( PROTECTED_COORD_FD );
    JASSERT ( listenSock->port() != -1 ) .Text ( "Invalid listener socket" );
    JTRACE ( "Using already created listener socker" ) ( listenSock->port() );
  } else {

    errno = 0;
    listenSock = new jalib::JServerSocket(jalib::JSockAddr::ANY, thePort, 128);
    JASSERT ( listenSock->isValid() ) ( thePort ) ( JASSERT_ERRNO )
      .Text ( "Failed to create listen socket."
       "\nIf msg is \"Address already in use\", this may be an old coordinator."
       "\nKill default coordinator and try again:  dmtcp_command -q"
       "\nIf that fails, \"pkill -9 dmtcp_coord\","
       " and try again in a minute or so." );
  }

  thePort = listenSock->port();
  if (!thePortFile.empty()) {
    string coordPort= jalib::XToString(thePort);
    Util::writeCoordPortToFile(coordPort.c_str(), thePortFile.c_str());
  }

  //parse checkpoint interval
  const char* interval = getenv ( ENV_VAR_CKPT_INTR );
  if ( interval != NULL ) {
    theDefaultCheckpointInterval = jalib::StringToInt ( interval );
    theCheckpointInterval = theDefaultCheckpointInterval;
  }

#if 0
  JASSERT_STDERR <<
    "dmtcp_coordinator starting..." <<
    "\n    Port: " << thePort <<
    "\n    Checkpoint Interval: ";
  if(theCheckpointInterval==0)
    JASSERT_STDERR << "disabled (checkpoint manually instead)";
  else
    JASSERT_STDERR << theCheckpointInterval;
  JASSERT_STDERR  <<
    "\n    Exit on last client: " << exitOnLast << "\n";
#else

  fprintf(stderr, "dmtcp_coordinator starting..."
          "\n    Host: %s (%s)"
          "\n    Port: %d"
          "\n    Checkpoint Interval: ",
          coordHostname.c_str(), inet_ntoa(localhostIPAddr), thePort);
  if(theCheckpointInterval==0)
    fprintf(stderr, "disabled (checkpoint manually instead)");
  else
    fprintf(stderr, "%d", theCheckpointInterval);
  fprintf(stderr, "\n    Exit on last client: %d\n", exitOnLast);
#endif

  if (daemon) {
    JASSERT_STDERR  << "Backgrounding...\n";
    int fd = open("/dev/null", O_RDWR);
    JASSERT(dup2(fd, STDIN_FILENO) == STDIN_FILENO);
    JASSERT(dup2(fd, STDOUT_FILENO) == STDOUT_FILENO);
    JASSERT(dup2(fd, STDERR_FILENO) == STDERR_FILENO);
    JASSERT_CLOSE_STDERR();
    if (fd > STDERR_FILENO) {
      close(fd);
    }

    if (fork() > 0) {
      JTRACE ( "Parent Exiting after fork()" );
      exit(0);
    }
    //pid_t sid = setsid();
  } else {
    JASSERT_STDERR  <<
      "Type '?' for help." <<
      "\n\n";
  }

  /* We set up the signal handler for SIGINT so that it would send the
   * DMT_KILL_PEER message to all the connected peers before exiting.
   */
  setupSIGINTHandler();

  prog.eventLoop(daemon);
  return 0;
}
