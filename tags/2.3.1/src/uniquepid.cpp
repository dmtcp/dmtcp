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

#include <stdlib.h>
#include "uniquepid.h"
#include "constants.h"
#include "../jalib/jconvert.h"
#include "../jalib/jfilesystem.h"
#include "../jalib/jserialize.h"
#include "syscallwrappers.h"
#include "protectedfds.h"
#include "shareddata.h"

using namespace dmtcp;

void dmtcp_UniquePid_EventHook(DmtcpEvent_t event, DmtcpEventData_t *data)
{
  switch (event) {
    case DMTCP_EVENT_RESTART:
      break;

    default:
      break;
  }
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

dmtcp::UniquePid::UniquePid(const char *filename)
{
  char *str = strdup(filename);
  dmtcp::vector<char *> tokens;
  char *token = strtok(str, "_");
  while (token != NULL) {
    tokens.push_back(token);
    token = strtok(NULL, "_");
  } while (token != NULL);
  JASSERT(tokens.size() >= 3);

  char *uidstr = tokens.back();
  char *hostid_str = strtok(uidstr, "-");
  char *pid_str = strtok(NULL, "-");
  char *time_str = strtok(NULL, ".");

  _hostid = strtoll(hostid_str, NULL, 16);
  _pid = strtol(pid_str, NULL, 10);
  _time = strtol(time_str, NULL, 16);
  _generation = 0;
}

// _generation field of return value may later have to be modified.
// So, it can't return a const dmtcp::UniquePid
dmtcp::UniquePid& dmtcp::UniquePid::ThisProcess(bool disableJTrace /*=false*/)
{
  if ( theProcess() == nullProcess() )
  {
    theProcess() = dmtcp::UniquePid ( theUniqueHostId() ,
                                      ::getpid(),
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
{
  _pid = 0;
  _hostid = 0;
  memset(&_time, 0, sizeof(_time));
}

void  dmtcp::UniquePid::incrementGeneration()
{
  _generation++;
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

dmtcp::ostream& dmtcp::operator<< ( dmtcp::ostream& o,const DmtcpUniqueProcessId& id )
{
  o << std::hex << id._hostid<< '-' << std::dec << id._pid << '-' << std::hex << id._time << std::dec;
  return o;
}

bool dmtcp::operator==(const DmtcpUniqueProcessId& a,
                       const DmtcpUniqueProcessId& b)
{
  return a._hostid == b._hostid &&
         a._pid == b._pid &&
         a._time == b._time &&
         a._generation == b._generation;
}

bool dmtcp::operator!=(const DmtcpUniqueProcessId& a,
                       const DmtcpUniqueProcessId& b)
{
  return !(a == b);
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
