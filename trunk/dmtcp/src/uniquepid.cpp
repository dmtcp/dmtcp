/****************************************************************************
 *   Copyright (C) 2006-2008 by Jason Ansel, Kapil Arya, and Gene Cooperman *
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

#include "uniquepid.h"
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sstream>
#include "constants.h"
#include  "../jalib/jconvert.h"
#include  "../jalib/jfilesystem.h"
#include "syscallwrappers.h"
#include "../jalib/jserialize.h"

inline static long theUniqueHostId(){
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

const dmtcp::UniquePid& dmtcp::UniquePid::ThisProcess()
{
  if ( theProcess() == nullProcess() )
  {
    theProcess() = dmtcp::UniquePid ( theUniqueHostId() , 
                                      ::_real_getpid(), 
                                      ::time(NULL) );
    JTRACE ( "recalculated process UniquePid..." ) ( theProcess() );
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


static bool checkpointFilename_initialized = false;
const char* dmtcp::UniquePid::checkpointFilename()
{
  static dmtcp::string checkpointFilename_str = "";
  if ( !checkpointFilename_initialized )
  {
    checkpointFilename_initialized = true;
    dmtcp::ostringstream os;

    const char* dir = getenv ( ENV_VAR_CHECKPOINT_DIR );
    if ( dir != NULL ){
      os << dir << '/';
    }

    os << CHECKPOINT_FILE_PREFIX
       << jalib::Filesystem::GetProgramName()
       << '_' << ThisProcess()
#ifdef UNIQUE_CHECKPOINT_FILENAMES
       << "_0000"
#endif
       << ".dmtcp";

    checkpointFilename_str = os.str();
  }
  return checkpointFilename_str.c_str();
}

dmtcp::string dmtcp::UniquePid::dmtcpTableFilename()
{
  static int count = 0;
  dmtcp::ostringstream os;

  os << getenv(ENV_VAR_TMPDIR) << "/dmtcpConTable."
     << ThisProcess()
     << '_' << jalib::XToString ( count++ );
  return os.str();
}

#ifdef PID_VIRTUALIZATION
dmtcp::string dmtcp::UniquePid::pidTableFilename()
{
  static int count = 0;
  dmtcp::ostringstream os;

  os << getenv(ENV_VAR_TMPDIR) << "/dmtcpPidTable."
     << ThisProcess()
     << '_' << jalib::XToString ( count++ );
  return os.str();
}
#endif

const char* dmtcp::UniquePid::ptsSymlinkFilename ( char *ptsname )
{
  char *devicename = ptsname + strlen ( "/dev/pts/" );

  //this must be static so dmtcp::string isn't destructed
  static dmtcp::string ptsSymlinkFilename_str;

  ptsSymlinkFilename_str = getenv(ENV_VAR_TMPDIR);
  ptsSymlinkFilename_str += "/pts_" + ThisProcess().toString() + '_';
  ptsSymlinkFilename_str += devicename;

  return ptsSymlinkFilename_str.c_str();
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
         && _time==that.time();
}

dmtcp::ostream& dmtcp::operator<< ( dmtcp::ostream& o,const dmtcp::UniquePid& id )
{
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
  checkpointFilename_initialized = false;
}

bool dmtcp::UniquePid::isNull() const
{
  return (*this == nullProcess());
}

void dmtcp::UniquePid::serialize ( jalib::JBinarySerializer& o )
{
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

