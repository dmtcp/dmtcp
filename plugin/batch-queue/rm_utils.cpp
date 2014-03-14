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

int findLib_byname(dmtcp::string pattern, dmtcp::string &libpath)
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

  while( dmtcp::Util::readProcMapsLine(fd, &area) ){
    libpath = area.name;
    //JTRACE("Inspect new /proc/seft/maps line")(libpath);
    if( libpath.size() == 0 ){
      //JTRACE("anonymous region, skip");
      continue;
    }

    if( libpath.find(pattern) != dmtcp::string::npos ){
      // this is library path that contains libtorque. This is what we need
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

int findLib_byfunc(dmtcp::string fname, dmtcp::string &libpath)
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

  while( dmtcp::Util::readProcMapsLine(fd, &area) ){
    libpath = area.name;
    //JTRACE("Inspect new /proc/seft/maps line")(libpath);
    if( libpath.size() == 0 ){
      //JTRACE("anonymous region, skip");
      continue;
    }

    if( libpath.find("libdmtcp") != dmtcp::string::npos ){
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
      // able to find requested symbol
      //JTRACE("Found libpath by content:")(fname)(libpath);
      dlclose(handle);
      ret = 0;
      break;
    }
    dlclose(handle);
    //JTRACE("Libpath doesn't contain searched function")(fname)(libpath);
  }

  _real_close(fd);
  return ret;
}
