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

#include "procselfmaps.h"
#include <fcntl.h>
#include "jassert.h"
#include "syscallwrappers.h"
#include "util.h"

using namespace dmtcp;


ProcSelfMaps::ProcSelfMaps()
  : dataIdx(0),
  numAreas(0),
  numBytes(0),
  fd(-1),
  numAllocExpands(0)
{
  char buf[4096];

  // NOTE: preExpand() verifies that we have at least 10 chunks pre-allocated
  // for each level of the allocator.  See jalib/jalloc.cpp:preExpand().
  // It assumes no allocation larger than jalloc.cpp:MAX_CHUNKSIZE.
  // Ideally, we would have followed the MTCP C code, and not allocated
  // any memory bewteen the constructor and destructor of ProcSelfMaps.
  // But since C++ is biased toward frequent mallocs (e.g., dynamic vectors),
  // we try to compensate here for the weaknesses of C++.
  // If we use this /proc/self/maps for checkpointing, we must not mmap
  // as part of the allocator prior to writing the memory to the ckpt image.
  jalib::JAllocDispatcher::preExpand();

  numAllocExpands = jalib::JAllocDispatcher::numExpands();

  // FIXME:  Also, any memory allocated and not freed since the calls to
  // setcontext() on the various threads will be a memory leak on restart.
  // We should check for that.

  fd = _real_open("/proc/self/maps", O_RDONLY);
  JASSERT(fd != -1) (JASSERT_ERRNO);
  ssize_t numRead = 0;

  // Get an approximation of the required buffer size.
  do {
    numRead = Util::readAll(fd, buf, sizeof(buf));
    if (numRead > 0) {
      numBytes += numRead;
    }
  } while (numRead > 0);

  // Now allocate a buffer. Note that this will most likely change the layout
  // of /proc/self/maps, so we need to recalculate numBytes.
  size_t size = numBytes + 4096; // Add a one page buffer.
  data = (char *)JALLOC_HELPER_MALLOC(size);
  JASSERT(lseek(fd, 0, SEEK_SET) == 0);

  numBytes = Util::readAll(fd, data, size);
  JASSERT(numBytes > 0) (numBytes);

  // TODO(kapil): Replace this assert with more robust code that would
  // reallocate the buffer with an extended size.
  JASSERT(numBytes < size) (numBytes) (size);

  // TODO(kapil): Validate the read data.
  JASSERT(isValidData());

  _real_close(fd);

  for (size_t i = 0; i < numBytes; i++) {
    if (data[i] == '\n') {
      numAreas++;
    }
  }
}

ProcSelfMaps::~ProcSelfMaps()
{
  JALLOC_HELPER_FREE(data);
  fd = -1;
  dataIdx = 0;
  numAreas = 0;
  numBytes = 0;

  // Verify that JAlloc doesn't expand memory (via mmap)
  // while reading /proc/self/maps.
  // FIXME:  Change from JWARNING to JASSERT when we have confidence in this.
  JWARNING(numAllocExpands == jalib::JAllocDispatcher::numExpands())
    (numAllocExpands)(jalib::JAllocDispatcher::numExpands())
  .Text("JAlloc: memory expanded through call to mmap()."
        "  Inconsistent JAlloc will be a problem on restart");
}

bool
ProcSelfMaps::isValidData()
{
  // TODO(kapil): Add validation check.
  return true;
}

unsigned long int
ProcSelfMaps::readDec()
{
  unsigned long int v = 0;

  while (1) {
    char c = data[dataIdx];
    if ((c >= '0') && (c <= '9')) {
      c -= '0';
    } else {
      break;
    }
    v = v * 10 + c;
    dataIdx++;
  }
  return v;
}

unsigned long int
ProcSelfMaps::readHex()
{
  unsigned long int v = 0;

  while (1) {
    char c = data[dataIdx];
    if ((c >= '0') && (c <= '9')) {
      c -= '0';
    } else if ((c >= 'a') && (c <= 'f')) {
      c -= 'a' - 10;
    } else if ((c >= 'A') && (c <= 'F')) {
      c -= 'A' - 10;
    } else {
      break;
    }
    v = v * 16 + c;
    dataIdx++;
  }
  return v;
}

int
ProcSelfMaps::getNextArea(ProcMapsArea *area)
{
  char rflag, sflag, wflag, xflag;

  if (dataIdx >= numBytes || data[dataIdx] == 0) {
    return 0;
  }

  area->addr = (VA)readHex();
  JASSERT(area->addr != NULL);

  JASSERT(data[dataIdx++] == '-');

  area->endAddr = (VA)readHex();
  JASSERT(area->endAddr != NULL);

  JASSERT(data[dataIdx++] == ' ');

  JASSERT(area->endAddr >= area->addr);
  area->size = area->endAddr - area->addr;

  rflag = data[dataIdx++];
  JASSERT((rflag == 'r') || (rflag == '-'));

  wflag = data[dataIdx++];
  JASSERT((wflag == 'w') || (wflag == '-'));

  xflag = data[dataIdx++];
  JASSERT((xflag == 'x') || (xflag == '-'));

  sflag = data[dataIdx++];
  JASSERT((sflag == 's') || (sflag == 'p'));

  JASSERT(data[dataIdx++] == ' ');

  area->offset = readHex();
  JASSERT(data[dataIdx++] == ' ');

  area->devmajor = readHex();
  JASSERT(data[dataIdx++] == ':');

  area->devminor = readHex();
  JASSERT(data[dataIdx++] == ' ');

  area->inodenum = readDec();

  while (data[dataIdx] == ' ') {
    dataIdx++;
  }

  area->name[0] = '\0';
  if (data[dataIdx] == '/' || data[dataIdx] == '[' || data[dataIdx] == '(') {
    // absolute pathname, or [stack], [vdso], etc.
    // On some machines, deleted files have a " (deleted)" prefix to the
    // filename.
    size_t i = 0;
    while (data[dataIdx] != '\n') {
      area->name[i++] = data[dataIdx++];
      JASSERT(i < sizeof(area->name));
    }
    area->name[i] = '\0';
  }

  JASSERT(data[dataIdx++] == '\n');

  area->prot = 0;
  if (rflag == 'r') {
    area->prot |= PROT_READ;
  }
  if (wflag == 'w') {
    area->prot |= PROT_WRITE;
  }
  if (xflag == 'x') {
    area->prot |= PROT_EXEC;
  }

  area->flags = MAP_FIXED;
  if (sflag == 's') {
    area->flags |= MAP_SHARED;
  }
  if (sflag == 'p') {
    area->flags |= MAP_PRIVATE;
  }
  if (area->name[0] == '\0') {
    area->flags |= MAP_ANONYMOUS;
  }

  area->properties = 0;

  return 1;
}
