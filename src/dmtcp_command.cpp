/****************************************************************************
 *   Copyright (C) 2006-2010 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coordinatorapi.h"
#include "json.h"
#include "util.h"
#include "dmtcp_assert.h"

#define BINARY_NAME "dmtcp_command"

using namespace dmtcp;

// gcc-4.3.4 -Wformat=2 issues false positives for warnings unless the format
// string has at least one format specifier with corresponding format argument.
// Ubuntu 9.01 uses -Wformat=2 by default.
static const char *theUsage =
  "Usage:  dmtcp_command [OPTIONS] COMMAND [COMMAND...]\n"
  "Send a command to the dmtcp_coordinator remotely.\n\n"
  "Options:\n\n"
  "  -h, --coord-host HOSTNAME (environment variable DMTCP_COORD_HOST)\n"
  "              Hostname where dmtcp_coordinator is run (default: localhost)\n"
  "  -p, --coord-port PORT_NUM (environment variable DMTCP_COORD_PORT)\n"
  "              Port where dmtcp_coordinator is run (default: "
                                                  STRINGIFY(DEFAULT_PORT) ")\n"
  "  --help\n"
  "              Print this message and exit.\n"
  "  --version\n"
  "              Print version information and exit.\n"
  "  --json\n"
  "              Print machine-readable JSON output for supported commands.\n"
  "\n"
  "Commands for Coordinator:\n"
  "    -s, --status:          Print status message\n"
  "    -l, --list:            List connected clients\n"
  "    -c, --checkpoint:      Checkpoint all nodes\n"
// Could add -B as synonym for -bc
  "    -bc, --bcheckpoint:    Checkpoint all nodes, dmtcp_command blocks until"
                                                                      " done\n"
// Could add -K as synonym for -kc
  "    -kc, --kcheckpoint     Checkpoint all nodes, kill all nodes when done\n"
// "    -xc, --xcheckpoint  deprecated synonym for '-kc': kill nodes if done\n"
  "    -i, --interval <val>   Update ckpt interval to <val> seconds (0=never)\n"
  "    -k, --kill             Kill all nodes\n"
  "    -q, --quit             Kill all nodes and quit\n"
  "\n"
  HELP_AND_CONTACT_INFO
  "\n";

static const char *
getCoordinatorHost()
{
  const char *host = getenv(ENV_VAR_NAME_HOST);
  if (host == NULL) {
    host = getenv("DMTCP_HOST");                 // deprecated
  }
  return host != NULL ? host : "localhost";
}

static int
getCoordinatorPort()
{
  const char *port = getenv(ENV_VAR_NAME_PORT);
  if (port == NULL) {
    port = getenv("DMTCP_PORT");                 // deprecated
  }
  return port != NULL ? atoi(port) : DEFAULT_PORT;
}

static void
freeWorkerList(char **workerList)
{
  if (*workerList != NULL) {
    JALLOC_HELPER_FREE(*workerList);
    *workerList = NULL;
  }
}

static int
printUsageOrJsonError(bool jsonOutput)
{
  if (jsonOutput) {
    DmtcpMessage response(DMT_USER_CMD_RESULT);
    response.coordCmd = DMT_INVALID_COORDINATOR_COMMAND;
    response.coordCmdStatus = DMT_COORD_INVALID_COMMAND;
    string json =
      response.toCoordinatorCmdJson(getCoordinatorHost(),
                                    getCoordinatorPort());
    printf("%s\n", json.c_str());
    return 2;
  }

  fprintf(stderr, theUsage, "");
  return 1;
}

static int
printJsonCommandSuccess(CoordinatorCmd command)
{
  DmtcpMessage response(DMT_USER_CMD_RESULT);
  response.coordCmd = command;
  response.coordCmdStatus = DMT_COORD_SUCCESS;
  string json =
    response.toCoordinatorCmdJson(getCoordinatorHost(),
                                  getCoordinatorPort());
  printf("%s\n", json.c_str());
  return 0;
}

static int
printJsonVersion()
{
  Json json;
  json.appendField("schema_version", 1);
  json.appendField("command", "DMT_VERSION");
  json.appendField("command_status", "DMT_COORD_SUCCESS");
  json.appendField("version", DMTCP_VERSION_AND_COPYRIGHT_INFO);
  printf("%s\n", json.str().c_str());
  return 0;
}

// shift args
#define shift argc--, argv++

int
main(int argc, char **argv)
{
  string interval = "";
  CoordinatorCmd command = DMT_HELP;
  bool jsonOutput = false;

  setenv("DMTCP_COMMAND", "1", 1); // for jalloc.cpp/sync_bool_compare_adn_swap
  initializeJalib();

  // No need to initialize the log file.
  // initializeLogFile();

  // process args
  shift;
  while (argc > 0) {
    string s = argv[0];
    if ((s == "--help" || s == "-h") && argc == 1) {
      if (jsonOutput) {
        return printJsonCommandSuccess(DMT_HELP);
      }
      printf("%s", theUsage);
      return 1;
    } else if ((s == "--version") && argc == 1) {
      if (jsonOutput) {
        return printJsonVersion();
      }
      printf("%s", DMTCP_VERSION_AND_COPYRIGHT_INFO);
      return 1;
    } else if (s == "--json") {
      jsonOutput = true;
      setLogLevel(LogLevel::Error);
      shift;
    } else if (argc > 1 &&
               (s == "-h" || s == "--coord-host" || s == "--host")) {
      setenv(ENV_VAR_NAME_HOST, argv[1], 1);
      shift; shift;
    } else if (argc > 1 &&
               (s == "-p" || s == "--coord-port" || s == "--port")) {
      setenv(ENV_VAR_NAME_PORT, argv[1], 1);
      shift; shift;
    } else if (argv[0][0] == '-' && argv[0][1] == 'p' &&
               isdigit(argv[0][2])) { // else if -p0, for example
      setenv(ENV_VAR_NAME_PORT, argv[0] + 2, 1);
      shift;
    } else if (s == "h" || s == "-h" || s == "--help" || s == "?") {
      if (jsonOutput) {
        return printJsonCommandSuccess(DMT_HELP);
      }
      fprintf(stderr, theUsage, "");
      return 1;
    } else { // else it's a request
      char *cmd = argv[0];

      // ignore leading dashes
      while (*cmd == '-') {
        cmd++;
      }
      s = cmd;

      CoordinatorCmd parsedCommand = CoordinatorAPI::parseCoordinatorCmd(cmd);
      if (*cmd == 'i' && (cmd[1] == '\0' || isdigit(cmd[1]))) {
        parsedCommand = DMT_UPDATE_CKPT_INTERVAL;
      }

      if (parsedCommand != DMT_INVALID_COORDINATOR_COMMAND) {
        command = parsedCommand;
        if (command == DMT_UPDATE_CKPT_INTERVAL) {
          if (isdigit(cmd[1])) { // if -i5, for example
            interval = cmd + 1;
          } else { // else -i 5
            if (argc == 1) {
              return printUsageOrJsonError(jsonOutput);
            }
            interval = argv[1];
            shift;
          }
        }
        shift;
      } else {
        return printUsageOrJsonError(jsonOutput);
      }
    }
  }

  CoordinatorCmdStatus coordCmdStatus = DMT_COORD_SUCCESS;
  int numPeers = 0;
  int isRunning = 0;
  int ckptInterval = 0;
  char *workerList = NULL;
  DmtcpMessage coordResponse(DMT_USER_CMD_RESULT);
  coordResponse.coordCmd = command;
  switch (command) {
  case DMT_HELP:
    if (jsonOutput) {
      return printJsonCommandSuccess(DMT_HELP);
    }
    fprintf(stderr, theUsage, "");
    return 1;

  case DMT_UPDATE_CKPT_INTERVAL:
    setenv(ENV_VAR_CKPT_INTR, interval.c_str(), 1);
    CoordinatorAPI::connectAndSendUserCommand(command, &coordCmdStatus,
                                              NULL, NULL, NULL,
                                              &coordResponse);
    if (!jsonOutput) {
      printf("Interval changed to %s\n", interval.c_str());
    }
    break;
  case DMT_BLOCKING_CKPT:
  case DMT_KILL_AFTER_CKPT:
    CoordinatorAPI::connectAndSendUserCommand(command, &coordCmdStatus,
                                              &numPeers, NULL, NULL,
                                              &coordResponse);
    break;
  case DMT_STATUS:
    CoordinatorAPI::connectAndSendUserCommand(command,
                                              &coordCmdStatus,
                                              &numPeers,
                                              &isRunning,
                                              &ckptInterval,
                                              &coordResponse);
    break;
  case DMT_LIST:
    workerList =
      CoordinatorAPI::connectAndSendUserCommand(command, &coordCmdStatus,
                                                NULL, NULL, NULL,
                                                &coordResponse);
    break;
  case DMT_CHECKPOINT:
    workerList =
      CoordinatorAPI::connectAndSendUserCommand(command, &coordCmdStatus,
                                                &numPeers, NULL, NULL,
                                                &coordResponse);
    break;
  case DMT_KILL:
  case DMT_QUIT:
    workerList =
      CoordinatorAPI::connectAndSendUserCommand(command, &coordCmdStatus,
                                                NULL, NULL, NULL,
                                                &coordResponse);
    break;
  default:
    fprintf(stderr, theUsage, "");
    break;
  }

  // check for error
  if (coordCmdStatus != DMT_COORD_SUCCESS) {
    if (jsonOutput) {
      string json =
        coordResponse.toCoordinatorCmdJson(getCoordinatorHost(),
                                               getCoordinatorPort());
      printf("%s\n", json.c_str());
      freeWorkerList(&workerList);
      return 2;
    }
    switch (coordCmdStatus) {
    case DMT_COORD_NOT_FOUND:
      if (getenv("DMTCP_COORD_PORT") || getenv("DMTCP_PORT")) {
        fprintf(stderr, "Coordinator not found. Please check port and host.\n");
      } else {
        fprintf(stderr,
                "Coordinator not found. "
                "Try specifying port with \'--port\'.\n");
      }
      break;
    case DMT_COORD_INVALID_COMMAND:
      fprintf(stderr,
              "Unknown command, try 'dmtcp_command --help'\n");
      break;
    case DMT_COORD_NOT_RUNNING:
      if (command == DMT_KILL_AFTER_CKPT) {
        printf("Computation was checkpointed and killed.\n");
      } else {
        fprintf(stderr,
                "Error, computation not in running state."
                "  Either a checkpoint is\n"
                " currently happening or there are no connected processes.\n");
      }
      break;
    default:
      fprintf(stderr, "Unknown error\n");
      break;
    }
    freeWorkerList(&workerList);
    return 2;
  }

  if (jsonOutput) {
    string json =
      coordResponse.toCoordinatorCmdJson(getCoordinatorHost(),
                                         getCoordinatorPort(),
                                         workerList);
    printf("%s\n", json.c_str());
    freeWorkerList(&workerList);
  } else if (command == DMT_STATUS || command == DMT_LIST) {
    printf("Coordinator:\n");
    const char *host = getCoordinatorHost();
    printf("  Host: %s\n", host);
    const char *port = getenv(ENV_VAR_NAME_PORT);
    if (port == NULL) {
      port = getenv("DMTCP_PORT");                 // deprecated
    }
    printf("  Port: %s\n",
           (port ? port : STRINGIFY(DEFAULT_PORT) " (default port)"));
    if (command == DMT_STATUS) {
      printf("Status...\n");
      printf("  NUM_PEERS=%d\n", numPeers);
      printf("  RUNNING=%s\n", (isRunning ? "yes" : "no"));
      if (ckptInterval) {
        printf("  CKPT_INTERVAL=%d\n", ckptInterval);
      } else {
        printf("  CKPT_INTERVAL=0 (checkpoint manually)\n");
      }
    } else {
      if (workerList) {
        printf("%s", workerList);
        freeWorkerList(&workerList);
      }
    }
  }
  freeWorkerList(&workerList);

  return 0;
}
