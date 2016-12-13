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

#include "util.h"
#include <pwd.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "../jalib/jassert.h"
#include "../jalib/jconvert.h"
#include "../jalib/jfilesystem.h"
#include "../jalib/jsocket.h"
#include "constants.h"
#include "coordinatorapi.h"  // for COORD_JOIN, COORD_NEW, COORD_ANY
#include "protectedfds.h"
#include "uniquepid.h"

using namespace dmtcp;

void
Util::writeCoordPortToFile(int port, const char *portFile)
{
  if (portFile != NULL && strlen(portFile) > 0) {
    int fd = open(portFile, O_CREAT | O_WRONLY | O_TRUNC, 0600);
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
char *
Util::calcTmpDir(const char *tmpdirenv)
{
  char *tmpDir = NULL;
  char hostname[256];

  memset(hostname, 0, sizeof(hostname));

  JASSERT(gethostname(hostname, sizeof(hostname)) == 0 ||
          errno == ENAMETOOLONG).Text("gethostname() failed");

  char *userName = const_cast<char *>("");
  if (getpwuid(getuid()) != NULL) {
    userName = getpwuid(getuid())->pw_name;
  } else if (getenv("USER") != NULL) {
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

  tmpDir = (char *) JALLOC_MALLOC(o.str().length() + 1);
  memcpy(tmpDir, o.str().c_str(), o.str().length() + 1);

  JASSERT(mkdir(tmpDir, S_IRWXU) == 0 || errno == EEXIST)
    (JASSERT_ERRNO) (tmpDir)
  .Text("Error creating tmp directory");

  JASSERT(0 == access(tmpDir, X_OK | W_OK)) (tmpDir)
  .Text("ERROR: Missing execute- or write-access to tmp dir");

  return tmpDir;
}

void
Util::initializeLogFile(const char *tmpDir,
                        const char *procname,
                        const char *prevLogPath)
{
  UniquePid::ThisProcess(true);

#ifdef LOGGING

  // Initialize JTRACE logging here
  ostringstream o;
  o << tmpDir;
  o << "/jassertlog.";
  o << UniquePid::ThisProcess();
  o << "_";
  if (procname == NULL) {
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

  if (prevLogPath != NULL) {
    a << "\nPrev JAssertLog path: " << prevLogPath;
  }

  a << "\nArgv: ";
  vector<string>args = jalib::Filesystem::GetProgramArgs();
  size_t i;
  for (i = 0; i < args.size(); i++) {
    a << " " << args[i];
  }

  a << "\nEnvironment: ";
  for (i = 0; environ[i] != NULL; i++) {
    a << " " << environ[i] << ";";
  }
  a << "\n========================================\n";

  // This causes an error when configure is done with --enable-logging
  //   JLOG(a.str().c_str());
#else // ifdef LOGGING
  JASSERT_SET_LOG("", tmpDir, UniquePid::ThisProcess().toString());
#endif // ifdef LOGGING
  if (getenv(ENV_VAR_QUIET)) {
    jassert_quiet = *getenv(ENV_VAR_QUIET) - '0';
  } else {
    // jassert.cpp initializes jassert_quiet to 0
  }
#ifdef QUIET
  jassert_quiet = 2;
#endif // ifdef QUIET
  unsetenv(ENV_VAR_STDERR_PATH);
}
