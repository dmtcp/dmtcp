/***************************************************************************
 *   Copyright (C) 2006 by Jason Ansel                                     *
 *   jansel@ccs.neu.edu                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include "uniquepid.h"
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sstream>
#include "constants.h"
#include "jconvert.h"
#include "jfilesystem.h"

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
    theProcess() = dmtcp::UniquePid ( theUniqueHostId() , ::getpid(), ::time(NULL) );
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
  static std::string checkpointFilename_str = "";
  if ( !checkpointFilename_initialized )
  {
    checkpointFilename_initialized = true;
    std::ostringstream os;

    const char* dir = getenv ( ENV_VAR_CHECKPOINT_DIR );
    if ( dir != NULL ){
      os << dir << '/';
    }

    os << CHECKPOINT_FILE_PREFIX 
       << jalib::Filesystem::GetProgramName() 
       << '_' << ThisProcess()
       << ".dmtcp";
    
    checkpointFilename_str = os.str();
  }
  return checkpointFilename_str.c_str();
}

std::string dmtcp::UniquePid::dmtcpTableFilename()
{
  static int count = 0;
  std::ostringstream os;

  os << "/tmp/dmtcpConTable." 
     << ThisProcess()
     << '_' << jalib::XToString ( count++ );
  return os.str();
}

const char* dmtcp::UniquePid::ptsSymlinkFilename ( char *ptsname )
{
  char *devicename = ptsname + strlen ( "/dev/pts/" );

  //this must be static so std::string isn't destructed
  static std::string ptsSymlinkFilename_str;

  ptsSymlinkFilename_str = "/tmp/pts_" + ThisProcess().toString() + '_';
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

std::ostream& std::operator<< ( std::ostream& o,const dmtcp::UniquePid& id )
{
  o << std::hex << id.hostid() << '-' << std::dec << id.pid() << '-' << std::hex << id.time() << std::dec;
  return o;
}

std::string dmtcp::UniquePid::toString() const{
  std::ostringstream o;
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
