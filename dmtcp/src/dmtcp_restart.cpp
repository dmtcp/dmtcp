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

#include <unistd.h>
#include <stdlib.h>
#include <string>
#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <vector>

#include "constants.h"
#include "coordinatorapi.h"
#include "util.h"
#include  "../jalib/jassert.h"

#define BINARY_NAME "dmtcp_restart"

using namespace dmtcp;

dmtcp::string dmtcpTmpDir = "/DMTCP/Uninitialized/Tmp/Dir";

// gcc-4.3.4 -Wformat=2 issues false positives for warnings unless the format
// string has at least one format specifier with corresponding format argument.
// Ubuntu 9.01 uses -Wformat=2 by default.
static const char* theUsage =
  "USAGE:\n dmtcp_restart [OPTIONS] <ckpt1.dmtcp> [ckpt2.dmtcp...]\n\n"
  "OPTIONS:\n"
  "  --host, -h, (environment variable DMTCP_HOST):\n"
  "      Hostname where dmtcp_coordinator is run (default: localhost)\n"
  "  --port, -p, (environment variable DMTCP_PORT):\n"
  "      Port where dmtcp_coordinator is run (default: 7779)\n"
  "  --tmpdir, -t, (environment variable DMTCP_TMPDIR):\n"
  "      Directory to store temporary files \n"
  "        (default: $TMDPIR/dmtcp-$USER@$HOST or /tmp/dmtcp-$USER@$HOST)\n"
  "  --join, -j:\n"
  "      Join an existing coordinator, raise error if one doesn't already exist\n"
  "  --new, -n:\n"
  "      Create a new coordinator, raise error if one already exists\n"
  "  --new-coordinator:\n"
  "      Create a new coordinator even if one already exists\n"
  "  --batch, -b:\n"
  "      Enable batch mode i.e. start the coordinator on the same node on\n"
  "        a randomly assigned port (if no port is specified by --port)\n"
  "  --interval, -i, (environment variable DMTCP_CHECKPOINT_INTERVAL):\n"
  "      Time in seconds between automatic checkpoints.\n"
  "      0 implies never (manual ckpt only); if not set and no env var,\n"
  "        use default value set in dmtcp_coordinator or dmtcp_command.\n"
  "      Not allowed if --join is specified\n"
  "      --batch implies -i 3600, unless otherwise specified.\n"
  "  --no-check:\n"
  "      Skip check for valid coordinator and never start one automatically\n"
  "  --quiet, -q, (or set environment variable DMTCP_QUIET = 0, 1, or 2):\n"
  "      Skip banner and NOTE messages; if given twice, also skip WARNINGs\n"
  "  --help:\n"
  "      Print this message and exit.\n"
  "  --version:\n"
  "      Print version information and exit.\n"
  "\n"
  "See " PACKAGE_URL " for more information.\n"
;

//shift args
#define shift argc--,argv++

dmtcp::vector<dmtcp::string> ckptFiles;

int main(int argc, char** argv)
{
  bool autoStartCoordinator=true;
  bool isRestart = true;
  dmtcp::CoordinatorAPI::CoordinatorMode allowedModes =
    dmtcp::CoordinatorAPI::COORD_ANY;

  initializeJalib();

  if (!getenv(ENV_VAR_QUIET)) {
    setenv(ENV_VAR_QUIET, "0", 0);
  }

  if (argc == 1) {
    JASSERT_STDERR << DMTCP_VERSION_AND_COPYRIGHT_INFO;
    JASSERT_STDERR << "(For help:  " << argv[0] << " --help)\n\n";
    return DMTCP_FAIL_RC;
  }

  //process args
  shift;
  while (true) {
    dmtcp::string s = argc>0 ? argv[0] : "--help";
    if (s == "--help" && argc == 1) {
      JASSERT_STDERR << theUsage;
      return DMTCP_FAIL_RC;
    } else if ((s == "--version") && argc == 1) {
      JASSERT_STDERR << DMTCP_VERSION_AND_COPYRIGHT_INFO;
      return DMTCP_FAIL_RC;
    } else if (s == "--no-check") {
      autoStartCoordinator = false;
      shift;
    } else if (s == "-j" || s == "--join") {
      allowedModes = dmtcp::CoordinatorAPI::COORD_JOIN;
      shift;
    } else if (s == "-n" || s == "--new") {
      allowedModes = dmtcp::CoordinatorAPI::COORD_NEW;
      shift;
    } else if (s == "--new-coordinator") {
      allowedModes = dmtcp::CoordinatorAPI::COORD_FORCE_NEW;
      shift;
    } else if (s == "-b" || s == "--batch") {
      allowedModes = dmtcp::CoordinatorAPI::COORD_BATCH;
      shift;
    } else if (s == "-i" || s == "--interval" ||
               (s.c_str()[0] == '-' && s.c_str()[1] == 'i' &&
                isdigit(s.c_str()[2]))) {
      if (isdigit(s.c_str()[2])) { // if -i5, for example
        setenv(ENV_VAR_CKPT_INTR, s.c_str()+2, 1);
        shift;
      } else { // else -i 5
        setenv(ENV_VAR_CKPT_INTR, argv[1], 1);
        shift; shift;
      }
    } else if (argc > 1 && (s == "-h" || s == "--host")) {
      setenv(ENV_VAR_NAME_HOST, argv[1], 1);
      shift; shift;
    } else if (argc > 1 && (s == "-p" || s == "--port")) {
      setenv(ENV_VAR_NAME_PORT, argv[1], 1);
      shift; shift;
    } else if (argc > 1 && (s == "-t" || s == "--tmpdir")) {
      setenv(ENV_VAR_TMPDIR, argv[1], 1);
      shift; shift;
    } else if (s == "-q" || s == "--quiet") {
      *getenv(ENV_VAR_QUIET) = *getenv(ENV_VAR_QUIET) + 1;
      // Just in case a non-standard version of setenv is being used:
      setenv(ENV_VAR_QUIET, getenv(ENV_VAR_QUIET), 1);
      shift;
    } else if ((s.length() > 2 && s.substr(0, 2) == "--") ||
               (s.length() > 1 && s.substr(0, 1) == "-")) {
      JASSERT_STDERR << "Invalid Argument\n";
      JASSERT_STDERR << theUsage;
      return DMTCP_FAIL_RC;
    } else if (argc > 1 && s == "--") {
      shift;
      break;
    } else {
      break;
    }
  }

  dmtcp::UniquePid::setTmpDir(getenv(ENV_VAR_TMPDIR));
  dmtcpTmpDir = dmtcp::UniquePid::getTmpDir();

  jassert_quiet = *getenv(ENV_VAR_QUIET) - '0';

  //make sure JASSERT initializes now, rather than during restart
  Util::initializeLogFile();

  if (jassert_quiet == 0)
    JASSERT_STDERR << DMTCP_BANNER;

  JTRACE("New dmtcp_restart process; _argc_ ckpt images") (argc);

  bool doAbort = false;
  char *minFile = NULL;
  int minPid = -1;
  long currHostid = 0;
  for (; argc > 0; shift) {
    dmtcp::string restorename(argv[0]);
    struct stat buf;
    int rc = stat(restorename.c_str(), &buf);
    if (Util::strStartsWith(restorename, "ckpt_") &&
        Util::strEndsWith(restorename, "_files")) {
      continue;
    } else if (!Util::strEndsWith(restorename, ".dmtcp")) {
      JNOTE("File doesn't have .dmtcp extension. Check Usage.")
        (restorename);
      JASSERT_STDERR << theUsage;
      doAbort = true;
    } else if (rc == -1) {
      char error_msg[1024];
      sprintf(error_msg, "\ndmtcp_restart: ckpt image %s", restorename.c_str());
      perror(error_msg);
      doAbort = true;
    } else if (buf.st_uid != getuid()) { /*Could also run if geteuid() matches*/
      printf("\nProcess uid (%d) doesn't match uid (%d) of\n" \
             "checkpoint image (%s).\n" \
	     "This is dangerous.  Aborting for security reasons.\n" \
             "If you still want to do this (at your own risk),\n" \
             "  then modify dmtcp/src/%s:%d and re-compile.\n",
             getuid(), buf.st_uid, restorename.c_str(), __FILE__, __LINE__ - 6);
      doAbort = true;
    }
    if (doAbort) {
      exit(DMTCP_FAIL_RC);
    }

    JTRACE("Will restart ckpt image _argv[0]_") (argv[0]);
    ckptFiles.push_back(argv[0]);
    dmtcp::UniquePid upid(argv[0]);
    if (minPid == -1) {
      minPid = upid.pid();
      minFile = argv[0];
    }
    JASSERT(upid.hostid() == currHostid || currHostid == 0);
    if (minPid > upid.pid()) {
      minPid = upid.pid();
      minFile = argv[0];
    }
  }

  if (autoStartCoordinator) {
    dmtcp::CoordinatorAPI::startCoordinatorIfNeeded(allowedModes,
                                                    isRestart);
  }

  if (ckptFiles.size() > 0) {
    dmtcp::Util::writeCkptFilenamesToTmpfile(ckptFiles);
  }

  CoordinatorAPI coordinatorAPI;
  coordinatorAPI.connectToCoordinator();

  JASSERT(minFile != NULL);
  dmtcp::Util::runMtcpRestore(minFile);
  JASSERT(false).Text("unreachable");
  return -1;
}
