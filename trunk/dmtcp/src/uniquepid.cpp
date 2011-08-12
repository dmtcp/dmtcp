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

#include "util.h"
#include "uniquepid.h"
#include <stdlib.h>
#include <string.h>
#include <string>
#include <pwd.h>
#include <sstream>
#include <fcntl.h>
#include <sys/stat.h>
#include "constants.h"
#include "../jalib/jconvert.h"
#include "../jalib/jfilesystem.h"
#include "../jalib/jserialize.h"
#include "syscallwrappers.h"
#include "protectedfds.h"

static dmtcp::string& ckptDirName()
{
  static dmtcp::string str;
  return str;
}
static dmtcp::string& ckptFileName()
{
  static dmtcp::string str;
  return str;
}

inline static long theUniqueHostId()
{
#ifdef USE_GETHOSTID
  return ::gethostid()
#else
  //gethostid() calls socket() on some systems, which we don't want
  char buf[512];
  JASSERT(::gethostname(buf, sizeof(buf))==0)(JASSERT_ERRNO);
  //so return a bad hash of our hostname
  long h = 0;
  for(char* i=buf; *i!='\0'; ++i)
    h = (*i) + (331*h);
  //make it positive for good measure
  return h>0 ? h : -1*h;
#endif
}


static dmtcp::UniquePid& nullProcess()
{
  static char buf[sizeof(dmtcp::UniquePid)];
  static dmtcp::UniquePid* t=NULL;
  if(t==NULL) t = new (buf) dmtcp::UniquePid(0,0,0);
  return *t;
}
static dmtcp::UniquePid& theProcess()
{
  static char buf[sizeof(dmtcp::UniquePid)];
  static dmtcp::UniquePid* t=NULL;
  if(t==NULL) t = new (buf) dmtcp::UniquePid(0,0,0);
  return *t;
}
static dmtcp::UniquePid& parentProcess()
{
  static char buf[sizeof(dmtcp::UniquePid)];
  static dmtcp::UniquePid* t=NULL;
  if(t==NULL) t = new (buf) dmtcp::UniquePid(0,0,0);
  return *t;
}

// _generation field of return value may later have to be modified.
// So, it can't return a const dmtcp::UniquePid
dmtcp::UniquePid& dmtcp::UniquePid::ThisProcess(bool disableJTrace /*=false*/)
{
  if ( theProcess() == nullProcess() )
  {
    theProcess() = dmtcp::UniquePid ( theUniqueHostId() ,
                                      ::_real_getpid(),
                                      ::time(NULL) );
    if (disableJTrace == false) {
      JTRACE ( "recalculated process UniquePid..." ) ( theProcess() );
    }
  }

  return theProcess();
}

dmtcp::UniquePid& dmtcp::UniquePid::ParentProcess()
{
  return parentProcess();
}

/*!
    \fn dmtcp::UniquePid::UniquePid()
 */
dmtcp::UniquePid::UniquePid()
    :_pid ( 0 )
    ,_hostid ( 0 )
{
  memset ( &_time,0,sizeof ( _time ) );
  setPrefix();
}

long  dmtcp::UniquePid::hostid() const
{
  return _hostid;
}

pid_t  dmtcp::UniquePid::pid() const
{
  return _pid;
}

time_t  dmtcp::UniquePid::time() const
{
  return _time;
}

int  dmtcp::UniquePid::generation() const
{
  return _generation;
}

const char* dmtcp::UniquePid::prefix() const
{
  return _prefix;
}

void dmtcp::UniquePid::setPrefix()
{
  memset(_prefix, 0, sizeof(_prefix));
  if (getenv(ENV_VAR_PREFIX_ID) != NULL) {
    strncpy(_prefix, getenv(ENV_VAR_PREFIX_ID), sizeof(_prefix) - 1);
  }
}

void  dmtcp::UniquePid::incrementGeneration()
{
  _generation++;
}

const char* dmtcp::UniquePid::checkpointFilename()
{
  if ( ckptFileName().empty() )
  {
    dmtcp::ostringstream os;

    const char* dir = getenv ( ENV_VAR_CHECKPOINT_DIR );
    if ( dir != NULL ){
      os << dir << '/';
    }

    os << CKPT_FILE_PREFIX
       << jalib::Filesystem::GetProgramName()
       << '_' << ThisProcess()
#ifdef UNIQUE_CHECKPOINT_FILENAMES
       << "_XXXXX"
#endif
       << CKPT_FILE_SUFFIX;

    ckptFileName() = os.str();
  }

#ifdef UNIQUE_CHECKPOINT_FILENAMES
  // Include 5-digit generation number in filename, which changes
  //   after each checkpoint, during same process
  JASSERT( Util::strEndsWith(ckptFileName(), CKPT_FILE_SUFFIX) )
	 ( ckptFileName() )
	 .Text ( "ckptFileName() doesn't end in .dmtcp" );
  sprintf((char *)ckptFileName().c_str()
	  + ckptFileName().length() - strlen("XXXXX" CKPT_FILE_SUFFIX),
	  "%5.5d%s", ThisProcess().generation(), CKPT_FILE_SUFFIX);
#endif
  return ckptFileName().c_str();
}

dmtcp::string dmtcp::UniquePid::checkpointFilesDirName()
{
  if ( ckptDirName().empty() ) {
    ckptDirName() = jalib::Filesystem::BaseName(checkpointFilename());
    ckptDirName().erase(ckptDirName().length() - strlen(CKPT_FILE_SUFFIX));
    ckptDirName() += CKPT_FILES_SUBDIR_SUFFIX;
  }
  return ckptDirName();
}

dmtcp::string dmtcp::UniquePid::dmtcpTableFilename()
{
  static int count = 0;
  dmtcp::ostringstream os;

  os << getTmpDir() << "/dmtcpConTable." << ThisProcess()
     << '_' << jalib::XToString ( count++ );
  return os.str();
}

#ifdef PID_VIRTUALIZATION
dmtcp::string dmtcp::UniquePid::pidTableFilename()
{
  static int count = 0;
  dmtcp::ostringstream os;

  os << getTmpDir() << "/dmtcpPidTable." << ThisProcess()
     << '_' << jalib::XToString ( count++ );
  return os.str();
}
#endif

dmtcp::string dmtcp::UniquePid::getTmpDir()
{
  dmtcp::string device = jalib::Filesystem::ResolveSymlink ( "/proc/self/fd/"
                           + jalib::XToString ( PROTECTED_TMPDIR_FD ) );
  if ( device.empty() ) {
    JWARNING ( false ) .Text ("Unable to determine DMTCP_TMPDIR, retrying.");
    setTmpDir(getenv(ENV_VAR_TMPDIR));
    device = jalib::Filesystem::ResolveSymlink ( "/proc/self/fd/"
               + jalib::XToString ( PROTECTED_TMPDIR_FD ) );
    JASSERT ( !device.empty() )
      .Text ( "Still unable to determine DMTCP_TMPDIR" );
  }
  return device;
}

/*
 * setTmpDir() computes the TmpDir to be used by DMTCP. It does so by using
 * DMTCP_TMPDIR env, current username, and hostname. Once computed, we open the
 * directory on file descriptor PROTECTED_TMPDIR_FD. The getTmpDir() routine
 * finds the TmpDir from looking at PROTECTED_TMPDIR_FD in proc file system.
 *
 * This mechanism was introduced to avoid calls to gethostname(), getpwuid()
 * etc. while DmtcpWorker was still initializing (in constructor) or the
 * process was restarting. gethostname(), getpwuid() will create a socket
 * connect to some DNS server to find out hostname and username. The socket is
 * closed only at next exec() and thus it leaves a dangling socket in the
 * worker process. To resolve this issue, we make sure to call setTmpDir() only
 * from dmtcp_checkpoint and dmtcp_restart process and once the user process
 * has been exec()ed, we use getTmpDir() only.
 */
void dmtcp::UniquePid::setTmpDir(const char* envVarTmpDir) {
  dmtcp::string tmpDir;

  char hostname[256];
  memset(hostname, 0, sizeof(hostname));

  JASSERT ( gethostname(hostname, sizeof(hostname)) == 0 ||
	    errno == ENAMETOOLONG ).Text ( "gethostname() failed" );

  dmtcp::ostringstream o;

  char *userName = const_cast<char *>("");
  if ( getpwuid ( getuid() ) != NULL ) {
    userName = getpwuid ( getuid() ) -> pw_name;
  } else if ( getenv("USER") != NULL ) {
    userName = getenv("USER");
  }

  if (envVarTmpDir) {
    o << envVarTmpDir;
  } else if (getenv("TMPDIR")) {
    o << getenv("TMPDIR") << "/dmtcp-" << userName << "@" << hostname;
  } else {
    o << "/tmp/dmtcp-" << userName << "@" << hostname;
  }

  JASSERT(mkdir(o.str().c_str(), S_IRWXU) == 0 || errno == EEXIST)
    (JASSERT_ERRNO) (o.str())
    .Text("Error creating tmp directory");

  JASSERT(0 == access(o.str().c_str(), X_OK|W_OK)) (o.str())
    .Text("ERROR: Missing execute- or write-access to tmp dir");

  int tmpFd = open ( o.str().c_str(), O_RDONLY  );
  JASSERT(tmpFd != -1);
  JASSERT(dup2(tmpFd, PROTECTED_TMPDIR_FD)==PROTECTED_TMPDIR_FD);
  close ( tmpFd );
}

/*!
    \fn dmtcp::UniquePid::operator<() const
 */
bool dmtcp::UniquePid::operator< ( const UniquePid& that ) const
{
#define TRY_LEQ(param) if(this->param != that.param) return this->param < that.param;
  TRY_LEQ ( _hostid );
  TRY_LEQ ( _pid );
  TRY_LEQ ( _time );
  return false;
}

bool dmtcp::UniquePid::operator== ( const UniquePid& that ) const
{
  return _hostid==that.hostid()
         && _pid==that.pid()
         && _time==that.time()
         && strncmp(_prefix, that.prefix(), sizeof(_prefix)) == 0;
}

dmtcp::ostream& dmtcp::operator<< ( dmtcp::ostream& o,const dmtcp::UniquePid& id )
{
  if (strlen(id.prefix()) != 0) {
    o << id.prefix() << "-";
  }
  o << std::hex << id.hostid() << '-' << std::dec << id.pid() << '-' << std::hex << id.time() << std::dec;
  return o;
}

dmtcp::string dmtcp::UniquePid::toString() const{
  dmtcp::ostringstream o;
  o << *this;
  return o.str();
}


void dmtcp::UniquePid::resetOnFork ( const dmtcp::UniquePid& newId )
{
  // parentProcess() is for inspection tools
  parentProcess() = ThisProcess();
  JTRACE ( "Explicitly setting process UniquePid" ) ( newId );
  theProcess() = newId;
  ckptFileName().clear();
  ckptDirName().clear();
}

bool dmtcp::UniquePid::isNull() const
{
  return (*this == nullProcess());
}

void dmtcp::UniquePid::serialize ( jalib::JBinarySerializer& o )
{
  // NOTE: Do not put JTRACE/JNOTE/JASSERT in here
  UniquePid theCurrentProcess, theParentProcess;

  if ( o.isWriter() )
  {
    theCurrentProcess = ThisProcess();
    theParentProcess = ParentProcess();
  }

  o & theCurrentProcess & theParentProcess;

  if ( o.isReader() )
  {
    theProcess() = theCurrentProcess;
    parentProcess() = theParentProcess;
  }
}
