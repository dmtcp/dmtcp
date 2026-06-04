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

#include "coordinatorapi.h"
#include "util.h"

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

static const int JSON_SCHEMA_VERSION = 1;

static const char *
jsonCommandType(char cmdChar)
{
  switch (cmdChar) {
  case 's':
    return "status";
  case 'c':
  case 'b':
  case 'K':
    return "checkpoint";
  case 'k':
    return "kill";
  case 'q':
    return "quit";
  default:
    return "unknown";
  }
}

static bool
jsonCommandSupported(char cmdChar)
{
  switch (cmdChar) {
  case 's':
  case 'c':
  case 'b':
  case 'K':
  case 'k':
  case 'q':
    return true;
  default:
    return false;
  }
}

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

static const char *
coordCmdStatusToJsonErrorCode(int status)
{
  switch (status) {
  case CoordCmdStatus::ERROR_COORDINATOR_NOT_FOUND:
    return "coordinator_not_found";
  case CoordCmdStatus::ERROR_INVALID_COMMAND:
    return "invalid_command";
  case CoordCmdStatus::ERROR_NOT_RUNNING_STATE:
    return "not_running";
  default:
    return "unknown_error";
  }
}

static const char *
coordCmdStatusToJsonMessage(int status)
{
  switch (status) {
  case CoordCmdStatus::ERROR_COORDINATOR_NOT_FOUND:
    return "Coordinator not found";
  case CoordCmdStatus::ERROR_INVALID_COMMAND:
    return "Unknown command";
  case CoordCmdStatus::ERROR_NOT_RUNNING_STATE:
    return "Computation not in running state";
  default:
    return "Unknown error";
  }
}

static void
printJsonString(const char *value)
{
  putchar('"');
  for (const unsigned char *p = (const unsigned char *)value; *p != '\0'; p++) {
    switch (*p) {
    case '"':
      printf("\\\"");
      break;
    case '\\':
      printf("\\\\");
      break;
    case '\b':
      printf("\\b");
      break;
    case '\f':
      printf("\\f");
      break;
    case '\n':
      printf("\\n");
      break;
    case '\r':
      printf("\\r");
      break;
    case '\t':
      printf("\\t");
      break;
    default:
      if (*p < 0x20) {
        printf("\\u%04x", *p);
      } else {
        putchar(*p);
      }
      break;
    }
  }
  putchar('"');
}

static void
printJsonCommandPrefix(const char *type, bool ok)
{
  printf("{\"schema_version\":%d,\"type\":", JSON_SCHEMA_VERSION);
  printJsonString(type);
  printf(",\"phase\":");
  printJsonString(type);
  printf(",\"ok\":%s", ok ? "true" : "false");
}

static void
printJsonCoordinator()
{
  printf(",\"coordinator_host\":");
  printJsonString(getCoordinatorHost());
  printf(",\"coordinator_port\":%d", getCoordinatorPort());
}

static void
printJsonStatusSuccess(int numPeers, int isRunning, int ckptInterval)
{
  printJsonCommandPrefix("status", true);
  printJsonCoordinator();
  printf(",\"num_peers\":%d", numPeers);
  printf(",\"running\":%s", isRunning ? "true" : "false");
  printf(",\"checkpoint_interval\":%d", ckptInterval);
  printf("}\n");
}

static void
printJsonCommandSuccess(const char *type)
{
  printJsonCommandPrefix(type, true);
  printJsonCoordinator();
  printf("}\n");
}

static void
printJsonCheckpointSuccess(int numPeers)
{
  printJsonCommandPrefix("checkpoint", true);
  printJsonCoordinator();
  printf(",\"num_peers\":%d", numPeers);
  printf("}\n");
}

static void
printJsonError(const char *type, int status)
{
  printJsonCommandPrefix(type, false);
  printf(",\"error_code\":");
  printJsonString(coordCmdStatusToJsonErrorCode(status));
  printf(",\"error_message\":");
  printJsonString(coordCmdStatusToJsonMessage(status));
  printJsonCoordinator();
  printf("}\n");
}

static int
printUsageOrJsonError(bool jsonOutput)
{
  if (jsonOutput) {
    printJsonError("unknown", CoordCmdStatus::ERROR_INVALID_COMMAND);
    return 2;
  }

  fprintf(stderr, theUsage, "");
  return 1;
}

// shift args
#define shift argc--, argv++

int
main(int argc, char **argv)
{
  string interval = "";
  string request = "h";
  bool jsonOutput = false;

  setenv("DMTCP_COMMAND", "1", 1); // for jalloc.cpp/sync_bool_compare_adn_swap
  initializeJalib();

  // No need to initialize the log file.
  // Util::initializeLogFile();

  // process args
  shift;
  while (argc > 0) {
    string s = argv[0];
    if ((s == "--help" || s == "-h") && argc == 1) {
      if (jsonOutput) {
        return printUsageOrJsonError(jsonOutput);
      }
      printf("%s", theUsage);
      return 1;
    } else if ((s == "--version") && argc == 1) {
      if (jsonOutput) {
        return printUsageOrJsonError(jsonOutput);
      }
      printf("%s", DMTCP_VERSION_AND_COPYRIGHT_INFO);
      return 1;
    } else if (s == "--json") {
      jsonOutput = true;
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
      fprintf(stderr, theUsage, "");
      return 1;
    } else { // else it's a request
      char *cmd = argv[0];

      // ignore leading dashes
      while (*cmd == '-') {
        cmd++;
      }
      if (*cmd == 'k' && *(cmd+1) == 'c') { // if this is "-kc":
        *cmd = 'K';  // Need to disambiguate '-k' from '-kc' (now '-Kc')
      }
      s = cmd;

      if ((*cmd == 'b' || *cmd == 'K') && *(cmd + 1) != 'c') {
        // If blocking ckpt, next letter must be 'c'; else print the usage
        return printUsageOrJsonError(jsonOutput);
      } else if (*cmd == 's' || *cmd == 'i' || *cmd == 'c' || *cmd == 'b' ||
                 *cmd == 'K' || *cmd == 'k' ||
                 *cmd == 'q' || *cmd == 'l') {
        request = s;
        if (*cmd == 'i') {
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

  int coordCmdStatus = CoordCmdStatus::NOERROR;
  int numPeers;
  int isRunning;
  int ckptInterval;
  char *workerList = NULL;
  // After this, the first char of the request is unique.  We only need that.
  char cmdChar = *(char *)request.c_str();
  if (jsonOutput && !jsonCommandSupported(cmdChar)) {
    printJsonError("unknown", CoordCmdStatus::ERROR_INVALID_COMMAND);
    return 2;
  }
  switch (cmdChar) {
  case 'h':
    return printUsageOrJsonError(jsonOutput);

  case 'i':
    setenv(ENV_VAR_CKPT_INTR, interval.c_str(), 1);
    CoordinatorAPI::connectAndSendUserCommand(cmdChar, &coordCmdStatus);
    printf("Interval changed to %s\n", interval.c_str());
    break;
  case 'b':
  case 'K':

    // blocking prefix
    CoordinatorAPI::connectAndSendUserCommand(cmdChar, &coordCmdStatus);

    // actual command: The request variable must have been "bc" or "Kc".
    CoordinatorAPI::connectAndSendUserCommand('c', &coordCmdStatus,
                                              &numPeers);
    break;
  case 's':
    CoordinatorAPI::connectAndSendUserCommand(cmdChar,
                                              &coordCmdStatus,
                                              &numPeers,
                                              &isRunning,
                                              &ckptInterval);
    break;
  case 'l':
    workerList =
      CoordinatorAPI::connectAndSendUserCommand(cmdChar, &coordCmdStatus);
    break;
  case 'c':
    workerList =
      CoordinatorAPI::connectAndSendUserCommand(cmdChar, &coordCmdStatus,
                                                &numPeers);
    break;
  case 'k':
  case 'q':
    workerList =
      CoordinatorAPI::connectAndSendUserCommand(cmdChar, &coordCmdStatus);
    break;
  default:
    fprintf(stderr, theUsage, "");
    break;
  }

  // check for error
  if (coordCmdStatus != CoordCmdStatus::NOERROR) {
    if (jsonOutput) {
      printJsonError(jsonCommandType(cmdChar), coordCmdStatus);
      return 2;
    }
    switch (coordCmdStatus) {
    case CoordCmdStatus::ERROR_COORDINATOR_NOT_FOUND:
      if (getenv("DMTCP_COORD_PORT") || getenv("DMTCP_PORT")) {
        fprintf(stderr, "Coordinator not found. Please check port and host.\n");
      } else {
        fprintf(stderr,
                "Coordinator not found. "
                "Try specifying port with \'--port\'.\n");
      }
      break;
    case CoordCmdStatus::ERROR_INVALID_COMMAND:
      fprintf(stderr,
              "Unknown command: %c, try 'dmtcp_command --help'\n", cmdChar);
      break;
    case CoordCmdStatus::ERROR_NOT_RUNNING_STATE:
      if (cmdChar == 'K') {
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
    return 2;
  }

  if (jsonOutput) {
    if (cmdChar == 's') {
      printJsonStatusSuccess(numPeers, isRunning, ckptInterval);
    } else if (cmdChar == 'c' || cmdChar == 'b' || cmdChar == 'K') {
      printJsonCheckpointSuccess(numPeers);
    } else {
      printJsonCommandSuccess(jsonCommandType(cmdChar));
    }
  } else if(cmdChar == 's' || cmdChar == 'l'){
    printf("Coordinator:\n");
    const char *host = getCoordinatorHost();
    printf("  Host: %s\n", host);
    const char *port = getenv(ENV_VAR_NAME_PORT);
    if (port == NULL) {
      port = getenv("DMTCP_PORT");                 // deprecated
    }
    printf("  Port: %s\n",
           (port ? port : STRINGIFY(DEFAULT_PORT) " (default port)"));
    if (cmdChar == 's') {
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
        JALLOC_HELPER_FREE(workerList);
      }
    }
  }

  return 0;
}
