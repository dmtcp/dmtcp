/****************************************************************************
 *  Copyright (C) 2012-2014 by Artem Y. Polyakov <artpol84@gmail.com>       *
 *                                                                          *
 *  This file is part of the RM plugin for DMTCP                            *
 *                                                                          *
 *  RM plugin is free software: you can redistribute it and/or              *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  RM plugin is distributed in the hope that it will be useful,            *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include <stdlib.h>
#include <linux/limits.h>
#include <sys/wait.h>
#include <pthread.h>
#include <vector>
#include <list>
#include <string>
#include "util.h"
#include "procmapsarea.h"
#include "jalib.h"
#include "jassert.h"
#include "jconvert.h"
#include "jfilesystem.h"
#include "rm_main.h"
#include "rm_utils.h"

using namespace dmtcp;

int dmtcp::findLib_byname(string pattern, string &libpath)
{
  // /proc/self/maps looks like: "<start addr>-<end addr> <mode> <offset> <device> <inode> <libpath>
  // we need to extract libpath
  ProcMapsArea area;
  int ret = -1;

  // we will search for first libpath and first libname
  int fd = _real_open ( "/proc/self/maps", O_RDONLY);

  if( fd < 0 ){
    JTRACE("Cannot open /proc/self/maps file");
    return -1;
  }

  while( Util::readProcMapsLine(fd, &area) ){
    libpath = area.name;
    //JTRACE("Inspect new /proc/seft/maps line")(libpath);
    if( libpath.size() == 0 ){
      //JTRACE("anonymous region, skip");
      continue;
    }

    if( libpath.find(pattern) != string::npos ){
      // This is the library path that contains libtorque.  This is what we need.
      //JTRACE("Found libpath")(pattern)(libpath);
      ret = 0;
      break;
    }else{
      //JTRACE("Libpath not found")(pattern)(libpath);
    }
  }

  _real_close(fd);
  return ret;
}

int dmtcp::findLib_byfunc(string fname, string &libpath)
{
  // /proc/self/maps looks like: "<start addr>-<end addr> <mode> <offset> <device> <inode> <libpath>
  // We need to extract libpath.
  ProcMapsArea area;
  int ret = -1;

  // We will search for the first libpath and the first libname.
  int fd = _real_open ( "/proc/self/maps", O_RDONLY);

  if( fd < 0 ){
    JTRACE("Cannot open /proc/self/maps file");
    return -1;
  }

  while( Util::readProcMapsLine(fd, &area) ){
    libpath = area.name;
    //JTRACE("Inspect new /proc/seft/maps line")(libpath);
    if( libpath.size() == 0 ){
      //JTRACE("anonymous region, skip");
      continue;
    }

    if( libpath.find("libdmtcp") != string::npos ){
      //JTRACE("dmtcp plugin, skip")(libpath);
      continue;
    }

    void *handle = dlopen(libpath.c_str(),RTLD_LAZY);
    if( handle == NULL ){
      //JTRACE("Cannot open libpath, skip")(libpath);
      continue;
    }
    void *fptr = dlsym(handle,fname.c_str());
    if( fptr != NULL ){
      // Able to find the requested symbol.
      //JTRACE("Found libpath by content:")(fname)(libpath);
      dlclose(handle);
      ret = 0;
      break;
    }
    dlclose(handle);
    //JTRACE("Function not found in Libpath")(fname)(libpath);
  }

  _real_close(fd);
  return ret;
}
