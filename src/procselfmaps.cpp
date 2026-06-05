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
#include "util_assert.h"

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
  // any memory between the constructor and destructor of ProcSelfMaps.
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
  ASSERT_ERRNO(fd != -1, "failed to open /proc/self/maps");
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
  ASSERT_SYSCALL_SUCCESS_MSG(lseek(fd, 0, SEEK_SET),
               "failed to rewind /proc/self/maps");

  numBytes = Util::readAll(fd, data, size);
  ASSERT(numBytes > 0 && numBytes < size,
         "unexpected /proc/self/maps read size: bytes={} buffer_size={}",
         numBytes, size);

  // TODO(kapil): Replace this assert with more robust code that would
  // reallocate the buffer with an extended size.
  ASSERT(numBytes < size,
         "/proc/self/maps buffer is too small: bytes={} buffer_size={}",
         numBytes, size);

  // TODO(kapil): Validate the read data.
  ASSERT(isValidData(), "invalid /proc/self/maps data");

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
  WARNING(numAllocExpands == jalib::JAllocDispatcher::numExpands(),
          "JAlloc expanded through mmap while reading /proc/self/maps; "
          "inconsistent JAlloc will be a problem on restart: before={} "
          "after={}",
          numAllocExpands, jalib::JAllocDispatcher::numExpands());
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
  ASSERT(area->addr != 0, "proc maps entry has a null start address");

  char sep = data[dataIdx++];
  ASSERT(sep == '-', "malformed proc maps entry: expected '-' at index={} "
         "got={}", dataIdx - 1, sep);

  area->endAddr = (VA)readHex();
  ASSERT(area->endAddr != 0, "proc maps entry has a null end address");

  sep = data[dataIdx++];
  ASSERT(sep == ' ', "malformed proc maps entry: expected space after end "
         "address at index={} got={}", dataIdx - 1, sep);

  ASSERT(area->endAddr >= area->addr,
         "proc maps entry has descending address range: start={} end={}",
         area->addr, area->endAddr);
  area->size = area->endAddr - area->addr;

  rflag = data[dataIdx++];
  ASSERT((rflag == 'r') || (rflag == '-'),
         "invalid read permission in proc maps entry: index={} value={}",
         dataIdx - 1, rflag);

  wflag = data[dataIdx++];
  ASSERT((wflag == 'w') || (wflag == '-'),
         "invalid write permission in proc maps entry: index={} value={}",
         dataIdx - 1, wflag);

  xflag = data[dataIdx++];
  ASSERT((xflag == 'x') || (xflag == '-'),
         "invalid execute permission in proc maps entry: index={} value={}",
         dataIdx - 1, xflag);

  sflag = data[dataIdx++];
  ASSERT((sflag == 's') || (sflag == 'p'),
         "invalid sharing flag in proc maps entry: index={} value={}",
         dataIdx - 1, sflag);

  sep = data[dataIdx++];
  ASSERT(sep == ' ', "malformed proc maps entry: expected space before "
         "offset at index={} got={}", dataIdx - 1, sep);

  area->offset = readHex();
  sep = data[dataIdx++];
  ASSERT(sep == ' ', "malformed proc maps entry: expected space after "
         "offset at index={} got={}", dataIdx - 1, sep);

  area->devmajor = readHex();
  sep = data[dataIdx++];
  ASSERT(sep == ':', "malformed proc maps entry: expected ':' after device "
         "major at index={} got={}", dataIdx - 1, sep);

  area->devminor = readHex();
  sep = data[dataIdx++];
  ASSERT(sep == ' ', "malformed proc maps entry: expected space after device "
         "minor at index={} got={}", dataIdx - 1, sep);

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
      ASSERT(i < sizeof(area->name),
             "proc maps entry name is too long: index={} max={}", i,
             sizeof(area->name));
    }
    area->name[i] = '\0';
  }

  sep = data[dataIdx++];
  ASSERT(sep == '\n', "malformed proc maps entry: expected newline at "
         "index={} got={}", dataIdx - 1, sep);

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

  if (area->name[0] == '\0' ||
      area->name[0] == '[') { // [heap], [stack], etc.
    area->flags |= MAP_ANONYMOUS;
  }

  area->mmapFileSize = -1;
  area->properties = 0;

  return 1;
}

void
ProcSelfMaps::getStackInfo(ProcMapsArea *area)
{
  ASSERT(dataIdx == 0,
         "ProcSelfMaps::getStackInfo requires fresh iterator: dataIdx={}",
         dataIdx);

  void *stackFrameAddr = __builtin_frame_address(0);
  while (getNextArea(area)) {
    if (area->addr < stackFrameAddr && area->endAddr > stackFrameAddr) {
      return;
    }
  }
  ASSERT(false, "stack mapping not found in /proc/self/maps: frame={}",
         stackFrameAddr);
}
