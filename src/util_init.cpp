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
#include "../jalib/jassert.h"
#include "../jalib/jfilesystem.h"
#include "../jalib/jsocket.h"

using namespace dmtcp;

void Util::writeCoordPortToFile(const char *port, const char *portFile)
{
  if (port != NULL && portFile != NULL && strlen(portFile) > 0) {
    int fd = open(portFile, O_CREAT|O_WRONLY|O_TRUNC, 0600);
    JWARNING(fd != -1) (JASSERT_ERRNO) (portFile)
      .Text("Failed to open port file.");
    writeAll(fd, port, strlen(port));
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
  string tmpDir;
  char hostname[256];
  memset(hostname, 0, sizeof(hostname));

  JASSERT ( gethostname(hostname, sizeof(hostname)) == 0 ||
	    errno == ENAMETOOLONG ).Text ( "gethostname() failed" );

  ostringstream o;

  char *userName = const_cast<char *>("");
  if ( getpwuid ( getuid() ) != NULL ) {
    userName = getpwuid ( getuid() ) -> pw_name;
  } else if ( getenv("USER") != NULL ) {
    userName = getenv("USER");
  }

  if (tmpdirenv) {
    o << tmpdirenv;
  } else if (getenv("TMPDIR")) {
    o << getenv("TMPDIR") << "/dmtcp-" << userName << "@" << hostname;
  } else {
    o << "/tmp/dmtcp-" << userName << "@" << hostname;
  }
  tmpDir = o.str();


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

  JASSERT_SET_LOG(o.str());

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
#endif
  if (getenv(ENV_VAR_QUIET)) {
    jassert_quiet = *getenv(ENV_VAR_QUIET) - '0';
  } else {
    jassert_quiet = 0;
  }
#ifdef QUIET
  jassert_quiet = 2;
#endif
  unsetenv(ENV_VAR_STDERR_PATH);
}
