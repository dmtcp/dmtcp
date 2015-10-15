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

#include <string.h>
#include <pwd.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "constants.h"
#include "util.h"
#include "protectedfds.h"
#include "uniquepid.h"
#include "coordinatorapi.h" // for COORD_JOIN, COORD_NEW, COORD_ANY
#include "../jalib/jassert.h"
#include "../jalib/jfilesystem.h"
#include "../jalib/jsocket.h"
#include  "../jalib/jconvert.h"

using namespace dmtcp;

void Util::getCoordHostAndPort(CoordinatorMode mode,
                               const char **host, int *port)
{
  if (SharedData::initialized()) {
    *host = SharedData::coordHost().c_str();
    *port = SharedData::coordPort();
    return;
  }

  static bool _firstTime = true;
  static const char *_cachedHost;
  static int _cachedPort;

  if (_firstTime) {
    // Set host to cmd line (if --cord-host) or env var or DEFAULT_HOST
    if (*host == NULL) {
      if (getenv(ENV_VAR_NAME_HOST)) {
        *host = getenv(ENV_VAR_NAME_HOST);
      } else if (getenv("DMTCP_HOST")) { // deprecated
        *host = getenv("DMTCP_HOST");
      } else {
        *host = DEFAULT_HOST;
      }
    }

    // Set port to cmd line (if --coord-port) or env var
    //   or 0 (if --new-coordinator from cmd line) or DEFAULT_PORT
    if (*port == UNINITIALIZED_PORT) {
      if (getenv(ENV_VAR_NAME_PORT)) {
        *port = jalib::StringToInt(getenv(ENV_VAR_NAME_PORT));
      } else if (getenv("DMTCP_PORT")) { // deprecated
        *port = jalib::StringToInt(getenv("DMTCP_PORT"));
      } else if (mode & COORD_NEW) {
        *port = 0;
      } else {
        *port = DEFAULT_PORT;
      }
    }

    _cachedHost = *host;
    _cachedPort = *port;
    _firstTime = false;

  } else {
    // We might have gotten a user-requested port of 0 (random port) before,
    //   and now the user is passing in the actual coordinator port.
    if (*port > 0 && _cachedPort == 0) {
      _cachedPort = *port;
    }
    *host = _cachedHost;
    *port = _cachedPort;
  }
}
void Util::setCoordPort(int port)
{
  const char *host = NULL;
  // mode will be ignored, since this is not the first time we call this.
  Util::getCoordHostAndPort(COORD_ANY, &host, &port);
}

void Util::writeCoordPortToFile(int port, const char *portFile)
{
  if (portFile != NULL && strlen(portFile) > 0) {
    int fd = open(portFile, O_CREAT|O_WRONLY|O_TRUNC, 0600);
    JWARNING(fd != -1) (JASSERT_ERRNO) (portFile)
      .Text("Failed to open port file.");
    char port_buf[30];
    memset(port_buf, '\0', sizeof(port_buf));
    sprintf(port_buf, "%d", port);
    writeAll(fd, port_buf, strlen(port_buf));
    fsync(fd);
    close(fd);
  }
}

/*
 * calcTmpDir() computes the TmpDir to be used by DMTCP. It does so by using
 * DMTCP_TMPDIR env, current username, and hostname. Once computed, we open the
 * directory on file descriptor PROTECTED_TMPDIR_FD.
 *
 * This mechanism was introduced to avoid calls to gethostname(), getpwuid()
 * etc. while DmtcpWorker was still initializing (in constructor) or the
 * process was restarting. gethostname(), getpwuid() will create a socket
 * connect to some DNS server to find out hostname and username. The socket is
 * closed only at next exec() and thus it leaves a dangling socket in the
 * worker process. To resolve this issue, we make sure to call calcTmpDir() only
 * from dmtcp_launch and dmtcp_restart process and once the user process
 * has been exec()ed, we use SharedData::getTmpDir() only.
 */
string Util::calcTmpDir(const char *tmpdirenv)
{
  char hostname[256];
  memset(hostname, 0, sizeof(hostname));

  JASSERT ( gethostname(hostname, sizeof(hostname)) == 0 ||
	    errno == ENAMETOOLONG ).Text ( "gethostname() failed" );

  char *userName = const_cast<char *>("");
  if ( getpwuid ( getuid() ) != NULL ) {
    userName = getpwuid ( getuid() ) -> pw_name;
  } else if ( getenv("USER") != NULL ) {
    userName = getenv("USER");
  }

  if (tmpdirenv) {
    // tmpdirenv was set by --tmpdir
  } else if (getenv("DMTCP_TMPDIR")) {
    tmpdirenv = getenv("DMTCP_TMPDIR");
  } else if (getenv("TMPDIR")) {
    tmpdirenv = getenv("TMPDIR");
  } else {
    tmpdirenv = "/tmp";
  }

  JASSERT(mkdir(tmpdirenv, S_IRWXU) == 0 || errno == EEXIST)
          (JASSERT_ERRNO) (tmpdirenv)
    .Text("Error creating base directory (--tmpdir/DMTCP_TMPDIR/TMPDIR)");

  ostringstream o;
  o << tmpdirenv << "/dmtcp-" << userName << "@" << hostname;
  string tmpDir = o.str();

  JASSERT(mkdir(tmpDir.c_str(), S_IRWXU) == 0 || errno == EEXIST)
          (JASSERT_ERRNO) (tmpDir)
    .Text("Error creating tmp directory");


  JASSERT(0 == access(tmpDir.c_str(), X_OK|W_OK)) (tmpDir)
    .Text("ERROR: Missing execute- or write-access to tmp dir");

  return tmpDir;
}

void Util::initializeLogFile(string tmpDir, string procname, string prevLogPath)
{
  UniquePid::ThisProcess(true);
#ifdef DEBUG
  // Initialize JASSERT library here
  ostringstream o;
  o << tmpDir;
  o << "/jassertlog.";
  o << UniquePid::ThisProcess();
  o << "_";
  if (procname.empty()) {
    o << jalib::Filesystem::GetProgramName();
  } else {
    o << procname;
  }

  JASSERT_SET_LOG(o.str(), tmpDir, UniquePid::ThisProcess().toString());

  ostringstream a;
  a << "\n========================================";
  a << "\nProcess Information";
  a << "\n========================================";
  a << "\nThis Process: " << UniquePid::ThisProcess()
    << "\nParent Process: " << UniquePid::ParentProcess();

  if (!prevLogPath.empty()) {
    a << "\nPrev JAssertLog path: " << prevLogPath;
  }

  a << "\nArgv: ";
  vector<string> args = jalib::Filesystem::GetProgramArgs();
  size_t i;
  for (i = 0; i < args.size(); i++) {
    a << " " << args[i];
  }

  a << "\nEnvironment: ";
  for (i = 0; environ[i] != NULL; i++) {
    a << " " << environ[i] << ";";
  }
  a << "\n========================================\n";

  JLOG(a.str().c_str());
#else
  JASSERT_SET_LOG("", tmpDir, UniquePid::ThisProcess().toString());
#endif
  if (getenv(ENV_VAR_QUIET)) {
    jassert_quiet = *getenv(ENV_VAR_QUIET) - '0';
  } else {
    // jassert.cpp initializes jassert_quiet to 0
  }
#ifdef QUIET
  jassert_quiet = 2;
#endif
  unsetenv(ENV_VAR_STDERR_PATH);
}
